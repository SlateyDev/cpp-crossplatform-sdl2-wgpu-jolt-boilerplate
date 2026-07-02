#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/RegisterTypes.h>

#if defined(__EMSCRIPTEN__)
#include <webgpu/webgpu.h>
#include <emscripten/emscripten.h>
#else
#include <webgpu/webgpu.h>
#endif

#define SDL_MAIN_HANDLED
#include <SDL.h>
#if !defined(__EMSCRIPTEN__)
#include <SDL_syswm.h>
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace {

void TraceImpl(const char *inFormat, ...)
{
    va_list args;
    va_start(args, inFormat);
    char buffer[1024];
    std::vsnprintf(buffer, sizeof(buffer), inFormat, args);
    va_end(args);
    std::cout << buffer << '\n';
}

#ifdef JPH_ENABLE_ASSERTS
bool AssertFailedImpl(const char *inExpression, const char *inMessage, const char *inFile, JPH::uint inLine)
{
    std::cerr << "[Jolt Assert] " << inFile << ':' << inLine << " (" << inExpression << ") "
              << (inMessage ? inMessage : "") << '\n';
    return true;
}
#endif

class JoltRuntime {
public:
    bool Initialize()
    {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = TraceImpl;
#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed = AssertFailedImpl;
#endif

        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        constexpr JPH::uint cMaxPhysicsJobs = 1024;
        constexpr JPH::uint cMaxPhysicsBarriers = 1024;
        tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
        jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            cMaxPhysicsJobs,
            cMaxPhysicsBarriers,
            std::max(1u, std::thread::hardware_concurrency() - 1)
        );

        initialized = true;
        return true;
    }

    ~JoltRuntime()
    {
        if (!initialized) {
            return;
        }

        jobSystem.reset();
        tempAllocator.reset();

        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }

private:
    bool initialized = false;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
};

struct GpuState {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUSurfaceConfiguration surfaceConfig{};
    WGPUQueue queue = nullptr;
    WGPUSurface surface = nullptr;
    WGPUBuffer rotationUniformBuffer = nullptr;
    WGPUBindGroupLayout rotationBindGroupLayout = nullptr;
    WGPUBindGroup rotationBindGroup = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
    WGPURenderPipeline pipeline = nullptr;
    uint32_t width = 1280;
    uint32_t height = 720;
};

struct alignas(16) RotationUniform {
    float angle = 0.0f;
};

struct AppState {
    SDL_Window *window = nullptr;
    GpuState gpu;
    bool running = true;
};

WGPUStringView ToWgpuString(const char *text)
{
    WGPUStringView view = WGPU_STRING_VIEW_INIT;
    view.data = text;
    view.length = WGPU_STRLEN;
    return view;
}

WGPUTextureFormat ChooseSurfaceFormat(const WGPUSurfaceCapabilities &caps)
{
    for (size_t i = 0; i < caps.formatCount; ++i) {
        if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm) {
            return caps.formats[i];
        }
    }
    for (size_t i = 0; i < caps.formatCount; ++i) {
        if (caps.formats[i] == WGPUTextureFormat_RGBA8Unorm) {
            return caps.formats[i];
        }
    }
    return caps.formatCount > 0 ? caps.formats[0] : WGPUTextureFormat_BGRA8Unorm;
}

bool EnsureRotationResources(GpuState &gpu)
{
    if (gpu.rotationUniformBuffer && gpu.rotationBindGroupLayout && gpu.rotationBindGroup && gpu.pipelineLayout) {
        return true;
    }

    WGPUBufferDescriptor bufferDesc {
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(RotationUniform),
        .mappedAtCreation = 0,
    };
    gpu.rotationUniformBuffer = wgpuDeviceCreateBuffer(gpu.device, &bufferDesc);
    if (!gpu.rotationUniformBuffer) {
        return false;
    }

    WGPUBindGroupLayoutEntry bglEntry {
        .binding = 0,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .hasDynamicOffset = 0,
            .minBindingSize = sizeof(RotationUniform),
        },
    };

    WGPUBindGroupLayoutDescriptor bglDesc {
        .entryCount = 1,
        .entries = &bglEntry,
    };
    gpu.rotationBindGroupLayout = wgpuDeviceCreateBindGroupLayout(gpu.device, &bglDesc);
    if (!gpu.rotationBindGroupLayout) {
        return false;
    }

    WGPUBindGroupEntry bgEntry {
        .binding = 0,
        .buffer = gpu.rotationUniformBuffer,
        .offset = 0,
        .size = sizeof(RotationUniform),
    };

    WGPUBindGroupDescriptor bgDesc {
        .layout = gpu.rotationBindGroupLayout,
        .entryCount = 1,
        .entries = &bgEntry,
    };
    gpu.rotationBindGroup = wgpuDeviceCreateBindGroup(gpu.device, &bgDesc);
    if (!gpu.rotationBindGroup) {
        return false;
    }

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &gpu.rotationBindGroupLayout,
    };
    gpu.pipelineLayout = wgpuDeviceCreatePipelineLayout(gpu.device, &pipelineLayoutDesc);
    return gpu.pipelineLayout != nullptr;
}

WGPURenderPipeline CreateTrianglePipeline(GpuState &gpu, WGPUTextureFormat format)
{
    static constexpr char kTriangleShader[] = R"(
struct RotationUniform {
    angle : f32,
}

@group(0) @binding(0)
var<uniform> u_rotation : RotationUniform;

struct VertexOutput {
    @builtin(position) position : vec4f,
    @location(0) color : vec3f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertex_index : u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(0.0, 0.6),
        vec2f(-0.6, -0.6),
        vec2f(0.6, -0.6)
    );

    var colors = array<vec3f, 3>(
        vec3f(1.0, 0.0, 0.0),
        vec3f(0.0, 1.0, 0.0),
        vec3f(0.0, 0.0, 1.0)
    );

    var out : VertexOutput;
    let p = positions[vertex_index];
    let c = cos(u_rotation.angle);
    let s = sin(u_rotation.angle);
    let rotated = vec2f(
        p.x * c - p.y * s,
        p.x * s + p.y * c
    );

    out.position = vec4f(rotated, 0.0, 1.0);
    out.color = colors[vertex_index];
    return out;
}

@fragment
fn fs_main(in : VertexOutput) -> @location(0) vec4f {
    return vec4f(in.color, 1.0);
}
)";

    WGPUShaderSourceWGSL wgslSource {
        .chain = WGPUChainedStruct{
            .next = nullptr,
            .sType = WGPUSType_ShaderSourceWGSL
        },
        .code = ToWgpuString(kTriangleShader),
    };

    WGPUShaderModuleDescriptor shaderDesc {
        .nextInChain = &wgslSource.chain,
    };

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(gpu.device, &shaderDesc);
    if (!shaderModule) {
        return nullptr;
    }

    WGPUColorTargetState colorTarget {
        .format = format,
        .writeMask = WGPUColorWriteMask_All,
    };

    WGPUFragmentState fragmentState {
        .module = shaderModule,
        .entryPoint = ToWgpuString("fs_main"),
        .targetCount = 1,
        .targets = &colorTarget,
    };

    WGPURenderPipelineDescriptor pipelineDesc {
        .layout = gpu.pipelineLayout,
        .vertex = {
            .module = shaderModule,
            .entryPoint = ToWgpuString("vs_main"),
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .stripIndexFormat = WGPUIndexFormat_Undefined,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None,
        },
        .multisample = {
            .count = 1,
            .mask = ~0u,
            .alphaToCoverageEnabled = 0,
        },
        .fragment = &fragmentState,
    };

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(gpu.device, &pipelineDesc);
    wgpuShaderModuleRelease(shaderModule);
    return pipeline;
}

bool CreateSurfaceFromWindow(WGPUInstance instance, SDL_Window *window, WGPUSurface *outSurface)
{
    if (!instance || !window || !outSurface) {
        return false;
    }

    WGPUSurfaceDescriptor surfaceDesc{};

#if defined(__EMSCRIPTEN__)
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasSource{};
    canvasSource.chain.next = nullptr;
    canvasSource.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvasSource.selector = ToWgpuString("#canvas");
    surfaceDesc.nextInChain = &canvasSource.chain;
#else
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (!SDL_GetWindowWMInfo(window, &wmInfo)) {
        std::cerr << "SDL_GetWindowWMInfo failed: " << SDL_GetError() << '\n';
        return false;
    }

    WGPUSurfaceSourceWindowsHWND winSource{};
    winSource.chain.next = nullptr;
    winSource.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    winSource.hinstance = wmInfo.info.win.hinstance;
    winSource.hwnd = wmInfo.info.win.window;
    surfaceDesc.nextInChain = &winSource.chain;
#endif

    *outSurface = wgpuInstanceCreateSurface(instance, &surfaceDesc);
    return *outSurface != nullptr;
}

void ReleaseGpu(GpuState &gpu)
{
    if (gpu.pipeline) {
        wgpuRenderPipelineRelease(gpu.pipeline);
        gpu.pipeline = nullptr;
    }
    if (gpu.pipelineLayout) {
        wgpuPipelineLayoutRelease(gpu.pipelineLayout);
        gpu.pipelineLayout = nullptr;
    }
    if (gpu.rotationBindGroup) {
        wgpuBindGroupRelease(gpu.rotationBindGroup);
        gpu.rotationBindGroup = nullptr;
    }
    if (gpu.rotationBindGroupLayout) {
        wgpuBindGroupLayoutRelease(gpu.rotationBindGroupLayout);
        gpu.rotationBindGroupLayout = nullptr;
    }
    if (gpu.rotationUniformBuffer) {
        wgpuBufferRelease(gpu.rotationUniformBuffer);
        gpu.rotationUniformBuffer = nullptr;
    }
    if (gpu.surface) {
        wgpuSurfaceRelease(gpu.surface);
        gpu.surface = nullptr;
    }
    if (gpu.queue) {
        wgpuQueueRelease(gpu.queue);
        gpu.queue = nullptr;
    }
    if (gpu.device) {
        wgpuDeviceRelease(gpu.device);
        gpu.device = nullptr;
    }
    if (gpu.adapter) {
        wgpuAdapterRelease(gpu.adapter);
        gpu.adapter = nullptr;
    }
    if (gpu.instance) {
        wgpuInstanceRelease(gpu.instance);
        gpu.instance = nullptr;
    }
}

bool ConfigureSurface(AppState &app)
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(app.window, &width, &height);
    app.gpu.width = static_cast<uint32_t>(std::max(1, width));
    app.gpu.height = static_cast<uint32_t>(std::max(1, height));

    WGPUSurfaceCapabilities caps{};
    if (wgpuSurfaceGetCapabilities(app.gpu.surface, app.gpu.adapter, &caps) != WGPUStatus_Success) {
        std::cerr << "Failed to query surface capabilities\n";
        return false;
    }

    app.gpu.surfaceConfig = WGPUSurfaceConfiguration{
        .device = app.gpu.device,
        .format = ChooseSurfaceFormat(caps),
        .usage = WGPUTextureUsage_RenderAttachment,
        .width = app.gpu.width,
        .height = app.gpu.height,
        .alphaMode = caps.alphaModeCount > 0 ? caps.alphaModes[0] : WGPUCompositeAlphaMode_Auto,
        .presentMode = caps.presentModeCount > 0 ? caps.presentModes[0] : WGPUPresentMode_Fifo,
    };
    wgpuSurfaceConfigure(app.gpu.surface, &app.gpu.surfaceConfig);
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    if (!EnsureRotationResources(app.gpu)) {
        std::cerr << "Failed to initialize rotation resources\n";
        return false;
    }

    if (app.gpu.pipeline) {
        wgpuRenderPipelineRelease(app.gpu.pipeline);
        app.gpu.pipeline = nullptr;
    }
    app.gpu.pipeline = CreateTrianglePipeline(app.gpu, app.gpu.surfaceConfig.format);
    return app.gpu.pipeline != nullptr;
}

bool DrawFrame(AppState &app)
{
    WGPUSurfaceTexture surfaceTexture{};
    wgpuSurfaceGetCurrentTexture(app.gpu.surface, &surfaceTexture);

    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        return ConfigureSurface(app);
    }

    RotationUniform rotation {
        .angle = static_cast<float>(SDL_GetTicks()) * 0.0015f,
    };
    wgpuQueueWriteBuffer(
        app.gpu.queue,
        app.gpu.rotationUniformBuffer,
        0,
        &rotation,
        sizeof(rotation)
    );

    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, nullptr);
    if (!view) {
        wgpuTextureRelease(surfaceTexture.texture);
        return false;
    }

    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app.gpu.device, &encoderDesc);
    if (!encoder) {
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(surfaceTexture.texture);
        return false;
    }

    WGPURenderPassColorAttachment colorAttachment {
        .view = view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = WGPUColor{0.08, 0.08, 0.12, 1.0},
    };

    WGPURenderPassDescriptor passDesc {
        .colorAttachmentCount = 1,
        .colorAttachments = &colorAttachment,
    };

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, app.gpu.pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, app.gpu.rotationBindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
    if (!commandBuffer) {
        wgpuCommandEncoderRelease(encoder);
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(surfaceTexture.texture);
        return false;
    }

    wgpuQueueSubmit(app.gpu.queue, 1, &commandBuffer);
#if !defined(__EMSCRIPTEN__)
    wgpuSurfacePresent(app.gpu.surface);
#endif

    wgpuCommandBufferRelease(commandBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surfaceTexture.texture);
    return true;
}

struct AdapterRequestContext {
    std::atomic<bool> *done = nullptr;
    bool *ok = nullptr;
    WGPUAdapter *adapter = nullptr;
};

struct DeviceRequestContext {
    std::atomic<bool> *done = nullptr;
    bool *ok = nullptr;
    WGPUDevice *device = nullptr;
};

bool WaitForAdapter(WGPUInstance instance, WGPUSurface surface, WGPUAdapter *outAdapter)
{
    std::atomic<bool> done = false;
    bool ok = false;
    AdapterRequestContext context{&done, &ok, outAdapter};

    WGPURequestAdapterCallbackInfo callbackInfo{};
#if defined(__EMSCRIPTEN__)
    callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
#else
    callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
#endif
    callbackInfo.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView, void *userdata1, void *) {
        auto *state = static_cast<AdapterRequestContext *>(userdata1);
        if (status == WGPURequestAdapterStatus_Success && adapter != nullptr) {
            *state->ok = true;
            *state->adapter = adapter;
        }
        state->done->store(true);
    };
    callbackInfo.userdata1 = &context;

    WGPURequestAdapterOptions adapterOptions {
        .compatibleSurface = surface,
    };
    wgpuInstanceRequestAdapter(instance, &adapterOptions, callbackInfo);

    while (!done.load()) {
#if defined(__EMSCRIPTEN__)
        emscripten_sleep(1);
#else
        wgpuInstanceProcessEvents(instance);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    }

    return ok;
}

bool WaitForDevice(WGPUInstance instance, WGPUAdapter adapter, WGPUDevice *outDevice)
{
    std::atomic<bool> done = false;
    bool ok = false;
    DeviceRequestContext context{&done, &ok, outDevice};

    WGPURequestDeviceCallbackInfo callbackInfo{};
#if defined(__EMSCRIPTEN__)
    callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
#else
    callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
#endif
    callbackInfo.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView, void *userdata1, void *) {
        auto *state = static_cast<DeviceRequestContext *>(userdata1);
        if (status == WGPURequestDeviceStatus_Success && device != nullptr) {
            *state->ok = true;
            *state->device = device;
        }
        state->done->store(true);
    };
    callbackInfo.userdata1 = &context;

    WGPUDeviceDescriptor deviceDesc{};
    wgpuAdapterRequestDevice(adapter, &deviceDesc, callbackInfo);

    while (!done.load()) {
#if defined(__EMSCRIPTEN__)
        emscripten_sleep(1);
#else
        wgpuInstanceProcessEvents(instance);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    }

    return ok;
}

bool InitializeGraphics(AppState &app)
{
    WGPUInstanceDescriptor instanceDesc{};
    app.gpu.instance = wgpuCreateInstance(&instanceDesc);
    if (!app.gpu.instance) {
        std::cerr << "Failed to create WebGPU instance\n";
        return false;
    }

    if (!CreateSurfaceFromWindow(app.gpu.instance, app.window, &app.gpu.surface)) {
        std::cerr << "Failed to create WebGPU surface\n";
        return false;
    }

    if (!WaitForAdapter(app.gpu.instance, app.gpu.surface, &app.gpu.adapter)) {
        std::cerr << "Failed to request WebGPU adapter\n";
        return false;
    }

    if (!WaitForDevice(app.gpu.instance, app.gpu.adapter, &app.gpu.device)) {
        std::cerr << "Failed to request WebGPU device\n";
        return false;
    }

    app.gpu.queue = wgpuDeviceGetQueue(app.gpu.device);
    if (!app.gpu.queue) {
        std::cerr << "Failed to get WebGPU queue\n";
        return false;
    }

    if (!ConfigureSurface(app)) {
        std::cerr << "Failed to configure WebGPU surface\n";
        return false;
    }

    return true;
}

void PumpEvents(AppState &app)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            app.running = false;
        } else if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            ConfigureSurface(app);
        }
    }
}

#if defined(__EMSCRIPTEN__)
void WasmMainLoop(void *userdata)
{
    auto *app = static_cast<AppState *>(userdata);
    PumpEvents(*app);
    if (!app->running) {
        emscripten_cancel_main_loop();
        return;
    }
    DrawFrame(*app);
}
#endif

} // namespace

int main()
{
    SDL_SetMainReady();

    JoltRuntime jolt;
    if (!jolt.Initialize()) {
        std::cerr << "Failed to initialize Jolt\n";
        return 1;
    }
    std::cout << "Jolt initialized\n";

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }
    std::cout << "SDL initialized\n";

    AppState app;
    app.window = SDL_CreateWindow(
        "wgpu + SDL2 triangle",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280,
        720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
    );
    if (!app.window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }
    std::cout << "SDL window created\n";

    if (!InitializeGraphics(app)) {
        ReleaseGpu(app.gpu);
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return 1;
    }
    std::cout << "WebGPU initialized\n";

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop_arg(WasmMainLoop, &app, 0, 1);
    return 0;
#else
    while (app.running) {
        PumpEvents(app);
        if (!app.running) {
            break;
        }
        if (!DrawFrame(app)) {
            std::cerr << "DrawFrame failed\n";
            break;
        }
        SDL_Delay(16);
    }

    ReleaseGpu(app.gpu);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
#endif
}
