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
#include <SDL_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "mesh_instance.hpp"
#include "gltf_loader.hpp"
#include "primitive.hpp"
#include "structures.hpp"

#include "Engine/GameObject.hpp"
#include "Engine/AssetManager.hpp"
#include "Engine/AudioManager.hpp"
#include "Engine/EngineTexture.hpp"

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

struct alignas(16) ModelMatrixUniform {
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    glm::mat4 normalMatrix = glm::mat4(1.0f);
};

constexpr uint32_t SHADOW_MAP_SIZE = 2048;

constexpr auto OPEN_GL_TO_WGPU_MATRIX = glm::mat4(
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 0.5, 0.0,
    0.0, 0.0, 0.5, 1.0);

struct alignas(16) LightUniform {
    struct alignas(16) Cascade {
        glm::mat4 viewProjectionMatrix;
        float splitDepth;
    } cascades[CASCADE_COUNT];
    glm::vec4 position = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
};

struct alignas(16) CameraUniform {
    glm::mat4 viewProjectionMatrix = glm::mat4(1.0f);
    glm::vec4 position = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 forward = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
};



auto directionalLightPosition = glm::vec3(50.0f, 100.0f, -100.0f);
auto directionalLight = LightUniform{
    .position = glm::vec4(directionalLightPosition, 1.0f),
};

struct Mesh {
    std::vector<Primitive> primitives;
};

std::unordered_map<std::string, WGPUShaderModule> shaders;
std::unordered_map<std::string, WGPUPipelineLayout> pipelineLayouts;
std::unordered_map<std::string, WGPURenderPipeline> pipelines;
std::unordered_map<std::string, Mesh> meshes;

std::unordered_map<std::string, UnlitMaterial> materials;

std::vector<MeshInstance*> objects;

MeshInstance gameObject1;
MeshInstance gameObject2;
MeshInstance gameObject3;

AudioManager audioManager;
constexpr const char* DEFAULT_SFX_NAME = "default_sfx";
constexpr const char* DEFAULT_MUSIC_NAME = "default_music";
constexpr const char* DEFAULT_SFX_PATH = "assets/audio/sfx.wav";
constexpr const char* DEFAULT_MUSIC_PATH = "assets/audio/music.ogg";

constexpr WGPUTextureFormat DEPTH_FORMAT = WGPUTextureFormat_Depth32Float;

struct Camera {
    glm::vec3 position{0.0f, 0.0f, -4.0f};
    glm::vec3 rotation{0.0f, 0.0f, 1.0f};
    glm::vec3 up{0.0f, -1.0f, 0.0f};
    float fov = glm::radians(72.0f);
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

constexpr float CAMERA_SPEED = 0.3f;
constexpr float CAMERA_MOVE_SPEED = 4.0f;
constexpr float CAMERA_MOVE_BOOST = 3.0f;

struct FlyCamera : public Camera {
    float pitch = 0.0f;
    float yaw = 0.0f;

    void MoveForward(const float delta)
    {
        position += glm::normalize(glm::vec3(rotation.x, rotation.y, rotation.z)) * delta;
    }

    void MoveRight(const float delta)
    {
        position += glm::normalize(glm::cross(rotation, up)) * delta;
    }

    void MoveVertical(const float delta)
    {
        position += -glm::normalize(up) * delta;
    }

    void AdjustPitch(const float delta)
    {
        pitch = std::clamp<float>(pitch - delta * glm::radians(CAMERA_SPEED), -glm::radians(89.0f), glm::radians(89.0f));
        UpdateRotation();
    }

    void AdjustYaw(const float delta)
    {
        yaw += delta * glm::radians(CAMERA_SPEED);
        UpdateRotation();
    }

    void UpdateRotation()
    {
        const float xzLen = std::cos(pitch);
        rotation.x = xzLen * std::cos(yaw + glm::pi<float>() * 0.5f);
        rotation.y = std::sin(pitch);
        rotation.z = xzLen * std::sin(yaw + glm::pi<float>() * 0.5f);
    }
};

FlyCamera flyCamera;

constexpr Uint32 OVERLAY_CHAR_WIDTH = 5;
constexpr Uint32 OVERLAY_CHAR_HEIGHT = 7;
constexpr Uint32 OVERLAY_CHAR_SPACING = 1;
constexpr Uint32 OVERLAY_LINE_SPACING = 2;
constexpr Uint32 OVERLAY_MARGIN = 8;
constexpr float OVERLAY_TEXT_SCALE = 2.0f;
constexpr size_t OVERLAY_MAX_DEBUG_MESSAGES = 8;
constexpr Uint64 OVERLAY_MIN_VERTEX_CAPACITY = 4096;
constexpr size_t OVERLAY_VERTEX_RESERVE_SIZE = 8192;
constexpr double FPS_UPDATE_INTERVAL_SECONDS = 0.25;
constexpr int FPS_DISPLAY_PRECISION = 1;
constexpr size_t OVERLAY_MAX_CONSOLE_LINES = 32;
constexpr float OVERLAY_CONSOLE_HEIGHT_RATIO = 0.4f;
constexpr size_t OVERLAY_MAX_CONSOLE_INPUT_LENGTH = 96;
constexpr int CONSOLE_RESULT_PRECISION = 10;

struct OverlayVertex {
    glm::vec2 position;
    glm::vec4 color;
};

struct OverlayState {
    WGPUShaderModule shader = nullptr;
    WGPURenderPipeline pipeline = nullptr;
    WGPUBuffer vertexBuffer = nullptr;
    Uint64 vertexCapacity = 0;
    std::deque<std::string> debugMessages;
    std::deque<std::string> consoleLines;
    std::string consoleInput;
    bool consoleOpen = false;
    Uint64 fpsCounterStart = 0;
    Uint32 fpsFrameCount = 0;
    float currentFps = 0.0f;
};

OverlayState overlayState;

void PushDebugMessage(const std::string &message, const bool logAsError = false)
{
    if (message.empty()) {
        return;
    }
    if (logAsError) {
        std::cerr << message << '\n';
    } else {
        std::cout << message << '\n';
    }
    overlayState.debugMessages.push_back(message);
    while (overlayState.debugMessages.size() > OVERLAY_MAX_DEBUG_MESSAGES) {
        overlayState.debugMessages.pop_front();
    }
}

std::string TrimWhitespace(const std::string &value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

void PushConsoleLine(const std::string &message)
{
    if (message.empty()) {
        return;
    }
    overlayState.consoleLines.push_back(message);
    while (overlayState.consoleLines.size() > OVERLAY_MAX_CONSOLE_LINES) {
        overlayState.consoleLines.pop_front();
    }
}

bool ParseDoubleArgument(const std::string &text, double &outValue)
{
    if (text.empty()) {
        return false;
    }
    char *endPtr = nullptr;
    const char *startPtr = text.c_str();
    outValue = std::strtod(text.c_str(), &endPtr);
    return endPtr != startPtr && *endPtr == '\0';
}

void ExecuteConsoleCommand(const std::string &commandLine)
{
    const std::string trimmed = TrimWhitespace(commandLine);
    if (trimmed.empty()) {
        return;
    }

    PushConsoleLine("CMD: " + trimmed);

    std::istringstream stream(trimmed);
    std::string command;
    stream >> command;
    std::ranges::transform(command, command.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (command == "print") {
        std::string message;
        std::getline(stream, message);
        message = TrimWhitespace(message);
        if (message.empty()) {
            PushConsoleLine("Usage: print <message>");
        } else {
            PushConsoleLine(message);
        }
        return;
    }

    if (command == "add") {
        std::string left;
        std::string right;
        std::string extra;
        stream >> left >> right >> extra;
        if (left.empty() || right.empty() || !extra.empty()) {
            PushConsoleLine("Usage: add <number1> <number2>");
            return;
        }

        double leftValue = 0.0;
        double rightValue = 0.0;
        if (!ParseDoubleArgument(left, leftValue) || !ParseDoubleArgument(right, rightValue)) {
            PushConsoleLine("add expects numeric arguments");
            return;
        }

        const double result = leftValue + rightValue;
        std::ostringstream resultStream;
        resultStream << std::setprecision(CONSOLE_RESULT_PRECISION) << result;
        PushConsoleLine("Result: " + resultStream.str());
        return;
    }

    PushConsoleLine("Unknown command: " + command);
}

void SetConsoleOpen(AppState &app, const bool open)
{
    overlayState.consoleOpen = open;
    if (overlayState.consoleOpen) {
        overlayState.consoleInput.clear();
        app.mouseLookEnabled = false;
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_StartTextInput();
        PushConsoleLine("Console opened");
        return;
    }
    SDL_StopTextInput();
    PushConsoleLine("Console closed");
}

struct alignas(16) RotationUniform {
    float angle = 0.0f;
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

#ifdef TRIANGLE_SAMPLE
bool EnsureRotationResources(GpuState &gpu)
{
    if (gpu.rotationUniformBuffer && gpu.rotationBindGroupLayout && gpu.rotationBindGroup && gpu.pipelineLayout) {
        return true;
    }

    gpu.rotationUniformBuffer = [&] {
        const WGPUBufferDescriptor bufferDesc {
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = sizeof(RotationUniform),
            .mappedAtCreation = 0,
        };
        return wgpuDeviceCreateBuffer(gpu.device, &bufferDesc);
    }();
    if (!gpu.rotationUniformBuffer) {
        return false;
    }

    gpu.rotationBindGroupLayout = [&] {
        constexpr auto bglEntries = std::to_array<WGPUBindGroupLayoutEntry>({
            {
                .binding = 0,
                .visibility = WGPUShaderStage_Vertex,
                .buffer = {
                    .type = WGPUBufferBindingType_Uniform,
                    .hasDynamicOffset = 0,
                    .minBindingSize = sizeof(RotationUniform),
                },
            },
        });
        const WGPUBindGroupLayoutDescriptor bglDesc {
            .entryCount = static_cast<uint32_t>(bglEntries.size()),
            .entries = bglEntries.data(),
        };
        return wgpuDeviceCreateBindGroupLayout(gpu.device, &bglDesc);
    }();
    if (!gpu.rotationBindGroupLayout) {
        return false;
    }

    gpu.rotationBindGroup = [&] {
        const auto bgEntries = std::to_array<WGPUBindGroupEntry>({
            {
                .binding = 0,
                .buffer = gpu.rotationUniformBuffer,
                .offset = 0,
                .size = sizeof(RotationUniform),
            },
        });
        const WGPUBindGroupDescriptor bgDesc {
            .layout = gpu.rotationBindGroupLayout,
            .entryCount = static_cast<uint32_t>(bgEntries.size()),
            .entries = bgEntries.data(),
        };
        return wgpuDeviceCreateBindGroup(gpu.device, &bgDesc);
    }();
    if (!gpu.rotationBindGroup) {
        return false;
    }

    gpu.pipelineLayout = [&] {
        const WGPUPipelineLayoutDescriptor pipelineLayoutDesc {
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &gpu.rotationBindGroupLayout,
        };
        return wgpuDeviceCreatePipelineLayout(gpu.device, &pipelineLayoutDesc);
    }();
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

    const WGPUShaderSourceWGSL wgslSource {
        .chain = WGPUChainedStruct{
            .next = nullptr,
            .sType = WGPUSType_ShaderSourceWGSL
        },
        .code = ToWgpuString(kTriangleShader),
    };

    const WGPUShaderModuleDescriptor shaderDesc {
        .nextInChain = &wgslSource.chain,
    };

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(gpu.device, &shaderDesc);
    if (!shaderModule) {
        return nullptr;
    }

    const WGPUColorTargetState colorTarget {
        .format = format,
        .writeMask = WGPUColorWriteMask_All,
    };

    const WGPUFragmentState fragmentState {
        .module = shaderModule,
        .entryPoint = ToWgpuString("fs_main"),
        .targetCount = 1,
        .targets = &colorTarget,
    };

    const WGPURenderPipelineDescriptor pipelineDesc {
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
#endif

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
    if (gpu.shadowBindGroup) {
        wgpuBindGroupRelease(gpu.shadowBindGroup);
        gpu.shadowBindGroup = nullptr;
    }
    if (gpu.shadowBindGroupLayout) {
        wgpuBindGroupLayoutRelease(gpu.shadowBindGroupLayout);
        gpu.shadowBindGroupLayout = nullptr;
    }
    if (gpu.shadowSampler) {
        wgpuSamplerRelease(gpu.shadowSampler);
        gpu.shadowSampler = nullptr;
    }
    for (auto &view : gpu.shadowDepthTextureViews) {
        if (view) {
            wgpuTextureViewRelease(view);
            view = nullptr;
        }
    }
    if (gpu.shadowDepthTextureArrayView) {
        wgpuTextureViewRelease(gpu.shadowDepthTextureArrayView);
        gpu.shadowDepthTextureArrayView = nullptr;
    }
    if (gpu.shadowDepthTexture) {
        wgpuTextureRelease(gpu.shadowDepthTexture);
        gpu.shadowDepthTexture = nullptr;
    }
    if (gpu.defaultSampler) {
        wgpuSamplerRelease(gpu.defaultSampler);
        gpu.defaultSampler = nullptr;
    }
    if (gpu.defaultSamplerBindGroupLayout) {
        wgpuBindGroupLayoutRelease(gpu.defaultSamplerBindGroupLayout);
        gpu.defaultSamplerBindGroupLayout = nullptr;
    }
#ifdef TRIANGLE_SAMPLE
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
#endif
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

void CreateDepthTexture(AppState &app)
{
    if (app.gpu.depthTexture) {
        std::cout << "Releasing existing depth texture" << std::endl;
        wgpuTextureRelease(app.gpu.depthTexture);
        app.gpu.depthTexture = nullptr;
    }
    std::cout << "Creating depth texture of size: " << app.gpu.surfaceConfig.width << "x" << app.gpu.surfaceConfig.height << std::endl;
    app.gpu.depthTexture = [&] {
        const WGPUTextureDescriptor depthDesc {
            .label = ToWgpuString("Depth Texture"),
            .usage = WGPUTextureUsage_RenderAttachment,
            .dimension = WGPUTextureDimension_2D,
            .size = {app.gpu.surfaceConfig.width, app.gpu.surfaceConfig.height, 1},
            .format = DEPTH_FORMAT,
            .mipLevelCount = 1,
            .sampleCount = 1,
            .viewFormatCount = 0,
        };
        return wgpuDeviceCreateTexture(app.gpu.device, &depthDesc);
    }();

    if (app.gpu.depthTextureView) {
        std::cout << "Releasing existing depth texture view" << std::endl;
        wgpuTextureViewRelease(app.gpu.depthTextureView);
        app.gpu.depthTextureView = nullptr;
    }
    std::cout << "Creating depth texture view" << std::endl;
    app.gpu.depthTextureView = [&] {
        const WGPUTextureViewDescriptor viewDesc {
            .label = ToWgpuString("Depth Texture View"),
            .format = DEPTH_FORMAT,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_DepthOnly,
        };
        return wgpuTextureCreateView(app.gpu.depthTexture, &viewDesc);
    }();
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

    auto selectedMode = WGPUPresentMode_Fifo;
    for (size_t i = 0; i < caps.presentModeCount; ++i) {
        if (caps.presentModes[i] == WGPUPresentMode_Mailbox) {
            selectedMode = WGPUPresentMode_Mailbox;
            break;
        }
    }

    app.gpu.surfaceConfig = WGPUSurfaceConfiguration{
        .device = app.gpu.device,
        .format = ChooseSurfaceFormat(caps),
        .usage = WGPUTextureUsage_RenderAttachment,
        .width = app.gpu.width,
        .height = app.gpu.height,
        .alphaMode = caps.alphaModeCount > 0 ? caps.alphaModes[0] : WGPUCompositeAlphaMode_Auto,
        .presentMode = selectedMode,
    };
    wgpuSurfaceConfigure(app.gpu.surface, &app.gpu.surfaceConfig);
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    CreateDepthTexture(app);

    app.gpu.defaultSampler = [&] {
        WGPUSamplerDescriptor samplerDesc = {
            .label = ToWgpuString("Sampler Descriptor"),
            .addressModeU = WGPUAddressMode_ClampToEdge,
            .addressModeV = WGPUAddressMode_ClampToEdge,
            .addressModeW = WGPUAddressMode_ClampToEdge,
            .magFilter = WGPUFilterMode_Linear,
            .minFilter = WGPUFilterMode_Linear,
            .mipmapFilter = WGPUMipmapFilterMode_Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 32.0f,
            .compare = WGPUCompareFunction_Undefined,
            .maxAnisotropy = 1
        };
        return wgpuDeviceCreateSampler(app.gpu.device, &samplerDesc);
    }();
    if (!app.gpu.defaultSampler) {
        std::cerr << "Failed to create default sampler\n";
        return false;
    }

    app.gpu.defaultSamplerBindGroupLayout = [&] {
        const auto entries = std::to_array<WGPUBindGroupLayoutEntry>({
            WGPUBindGroupLayoutEntry{
                .binding = 0,
                .visibility = WGPUShaderStage_Fragment,
                .texture = WGPUTextureBindingLayout{
                    .sampleType = WGPUTextureSampleType_Float,
                    .viewDimension = WGPUTextureViewDimension_2D,
                    .multisampled = false,
                },
            },
            WGPUBindGroupLayoutEntry{
                .binding = 1,
                .visibility = WGPUShaderStage_Fragment,
                .sampler = WGPUSamplerBindingLayout{
                    .type = WGPUSamplerBindingType_Filtering,
                },
            },
        });
        const WGPUBindGroupLayoutDescriptor desc{
            .label = ToWgpuString("Default Sampler Bind Group Layout"),
            .entryCount = static_cast<uint32_t>(entries.size()),
            .entries = entries.data(),
        };
        return wgpuDeviceCreateBindGroupLayout(app.gpu.device, &desc);
    }();

    if (!app.gpu.defaultSamplerBindGroupLayout) {
        std::cerr << "Failed to create default sampler bind group layout\n";
        return false;
    }

    app.gpu.projectionMatrix = glm::perspective(
        flyCamera.fov,
        static_cast<float>(app.gpu.width) / static_cast<float>(app.gpu.height),
        flyCamera.nearPlane,
        flyCamera.farPlane
    );

    app.gpu.viewMatrix = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 4.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

#ifdef TRIANGLE_SAMPLE
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
#endif
    return true;
}

std::array<glm::vec3, 8> BuildCascadeFrustumCorners(const AppState &app, const float nearDist, const float farDist) {
    const float aspect = static_cast<float>(app.gpu.width) / static_cast<float>(app.gpu.height);
    const float halfFovTan = tan(flyCamera.fov * 0.5f);

    const glm::vec3 forward = glm::normalize(flyCamera.rotation);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));

    const glm::vec3 nearCenter = flyCamera.position + (forward * nearDist);
    const glm::vec3 farCenter = flyCamera.position + (forward * farDist);

    const float nearHalfHeight = nearDist * halfFovTan;
    const float nearHalfWidth = nearHalfHeight * aspect;
    const float farHalfHeight = farDist * halfFovTan;
    const float farHalfWidth = farHalfHeight * aspect;

    const std::array corners = {
        nearCenter - right * nearHalfWidth + up * nearHalfHeight,
        nearCenter + right * nearHalfWidth + up * nearHalfHeight,
        nearCenter + right * nearHalfWidth - up * nearHalfHeight,
        nearCenter - right * nearHalfWidth - up * nearHalfHeight,
        farCenter - right * farHalfWidth + up * farHalfHeight,
        farCenter + right * farHalfWidth + up * farHalfHeight,
        farCenter + right * farHalfWidth - up * farHalfHeight,
        farCenter - right * farHalfWidth - up * farHalfHeight,
    };
    return corners;
}

std::array<float, CASCADE_COUNT> CalculateCascadeSplits() {
    std::array<float, CASCADE_COUNT> splits{};

    const float nearPlane = flyCamera.nearPlane;
    const float farPlane = flyCamera.farPlane;

    for (uint32_t i = 0; i < CASCADE_COUNT; ++i) {
        constexpr float lambda = 0.5f;
        const float p = static_cast<float>(i + 1) / static_cast<float>(CASCADE_COUNT);
        const float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);
        const float uniformSplit = nearPlane + (farPlane - nearPlane) * p;
        splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
    }
    return splits;
}

void UpdateCascadeData(const AppState &app) {
    const auto splits = CalculateCascadeSplits();
    const auto lightDir = glm::normalize(-glm::vec3(directionalLight.position));

    for (auto i = 0; i < CASCADE_COUNT; ++i) {
        const auto cascadeNear = i == 0 ? flyCamera.nearPlane : splits[i - 1];
        const auto cascadeFar = splits[i];
        const auto corners = BuildCascadeFrustumCorners(app, cascadeNear, cascadeFar);

        glm::vec3 frustumCenter(0.0f);
        for (const auto &corner : corners) {
            frustumCenter += corner;
        }
        frustumCenter /= static_cast<float>(corners.size());

        auto lightUp = glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(lightDir, lightUp)) > 0.99f) {
            lightUp = glm::vec3(1.0f, 0.0f, 0.0f);
        }

        auto lightRight = glm::normalize(glm::cross(lightUp, lightDir));
        lightUp = glm::normalize(glm::cross(lightDir, lightRight));

        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float minZ = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::min();
        float maxY = std::numeric_limits<float>::min();
        float maxZ = std::numeric_limits<float>::min();
        for (const auto &corner : corners) {
            const auto offset = corner - frustumCenter;
            const auto x= glm::dot(offset, lightRight);
            const auto y= glm::dot(offset, lightUp);
            const auto z= glm::dot(offset, lightDir);
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            minZ = std::min(minZ, z);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
            maxZ = std::max(maxZ, z);
        }

        auto radius = std::max(std::max(std::abs(minX), std::abs(maxX)), std::max(std::abs(minY), std::abs(maxY)));
        radius = std::max(radius, std::max(std::abs(minZ), std::abs(maxZ))) + 10.0f;

        auto lightPos = frustumCenter - lightDir * radius;
        auto lightView = glm::lookAt(lightPos, frustumCenter, lightUp);
        auto lightProj = glm::ortho(-radius, radius, -radius, radius, -2.0f * radius, 2.0f * radius);

        directionalLight.cascades[i].viewProjectionMatrix = OPEN_GL_TO_WGPU_MATRIX * lightProj * lightView;
        directionalLight.cascades[i].splitDepth = cascadeFar;
    }
}

void RenderObjects(WGPURenderPassEncoder pass) {
    for (const auto& obj : objects) {
        wgpuRenderPassEncoderSetBindGroup(pass, 1, obj->uniformBindGroup, 0, nullptr);

        auto it = meshes.find(obj->meshName);

        if (it != meshes.end()) {
            const Mesh& mesh = it->second;
            for (const auto& primitive : mesh.primitives) {
                const auto& material = materials[primitive.materialResourceName];
                wgpuRenderPassEncoderSetBindGroup(pass, 2, material.bindGroup, 0, nullptr);
                if (primitive.vertexCount != 0 && primitive.vertexBuffer != nullptr) {
                    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, primitive.vertexBuffer, 0, WGPU_WHOLE_SIZE);
                }
                if (primitive.indexCount != 0 && primitive.indexBuffer != nullptr) {
                    wgpuRenderPassEncoderSetIndexBuffer(pass, primitive.indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
                }

                if (primitive.indexCount > 0) {
                    wgpuRenderPassEncoderDrawIndexed(pass, primitive.indexCount, 1, 0, 0, 0);
                } else {
                    wgpuRenderPassEncoderDraw(pass, primitive.vertexCount, 1, 0, 0);
                }
            }
        }
    }
}

void RenderShadowObjects(WGPURenderPassEncoder pass) {
    for (const auto& obj : objects) {
        wgpuRenderPassEncoderSetBindGroup(pass, 1, obj->uniformBindGroup, 0, nullptr);

        auto it = meshes.find(obj->meshName);

        if (it != meshes.end()) {
            const Mesh& mesh = it->second;
            for (const auto& primitive : mesh.primitives) {
                if (primitive.vertexCount != 0 && primitive.vertexBuffer != nullptr) {
                    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, primitive.vertexBuffer, 0, WGPU_WHOLE_SIZE);
                }
                if (primitive.indexCount != 0 && primitive.indexBuffer != nullptr) {
                    wgpuRenderPassEncoderSetIndexBuffer(pass, primitive.indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
                }

                if (primitive.indexCount > 0) {
                    wgpuRenderPassEncoderDrawIndexed(pass, primitive.indexCount, 1, 0, 0, 0);
                } else {
                    wgpuRenderPassEncoderDraw(pass, primitive.vertexCount, 1, 0, 0);
                }
            }
        }
    }
}

std::array<uint8_t, OVERLAY_CHAR_HEIGHT> GetGlyphRows(char c)
{
    const char upperChar = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    switch (upperChar) {
        case 'A': return {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
        case 'B': return {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
        case 'C': return {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
        case 'D': return {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C};
        case 'E': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
        case 'F': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
        case 'G': return {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F};
        case 'H': return {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
        case 'I': return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
        case 'J': return {0x1F, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C};
        case 'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
        case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
        case 'M': return {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
        case 'N': return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
        case 'O': return {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
        case 'P': return {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
        case 'Q': return {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
        case 'R': return {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
        case 'S': return {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
        case 'T': return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
        case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
        case 'V': return {0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04};
        case 'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
        case 'X': return {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
        case 'Y': return {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
        case 'Z': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
        case '0': return {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
        case '1': return {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
        case '2': return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
        case '3': return {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
        case '4': return {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
        case '5': return {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
        case '6': return {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
        case '7': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
        case '8': return {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
        case '9': return {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E};
        case '.': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
        case ':': return {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00};
        case '-': return {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
        case '_': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
        case '/': return {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
        case '\\': return {0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01};
        case ',': return {0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x08};
        case '(': return {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02};
        case ')': return {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08};
        case '[': return {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E};
        case ']': return {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E};
        case '!': return {0x04, 0x04, 0x04, 0x04, 0x00, 0x00, 0x04};
        case ' ': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        default: return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
    }
}

void AppendOverlayQuad(std::vector<OverlayVertex> &vertices, const float x0, const float y0, const float x1, const float y1,
                       const glm::vec4 &color, const float width, const float height)
{
    const float left = (x0 / width) * 2.0f - 1.0f;
    const float right = (x1 / width) * 2.0f - 1.0f;
    const float top = 1.0f - (y0 / height) * 2.0f;
    const float bottom = 1.0f - (y1 / height) * 2.0f;

    vertices.push_back({glm::vec2(left, top), color});
    vertices.push_back({glm::vec2(right, top), color});
    vertices.push_back({glm::vec2(right, bottom), color});
    vertices.push_back({glm::vec2(left, top), color});
    vertices.push_back({glm::vec2(right, bottom), color});
    vertices.push_back({glm::vec2(left, bottom), color});
}

void AppendOverlayText(std::vector<OverlayVertex> &vertices, const std::string &text, const float x, const float y,
                       const float scale, const glm::vec4 &color, const uint32_t screenWidth, const uint32_t screenHeight)
{
    float cursorX = x;
    const float advance = static_cast<float>(OVERLAY_CHAR_WIDTH + OVERLAY_CHAR_SPACING) * scale;
    const float pixelSize = scale;
    for (const char ch : text) {
        const auto rows = GetGlyphRows(ch);
        for (uint32_t row = 0; row < OVERLAY_CHAR_HEIGHT; ++row) {
            for (uint32_t col = 0; col < OVERLAY_CHAR_WIDTH; ++col) {
                const uint8_t mask = static_cast<uint8_t>(1u << (OVERLAY_CHAR_WIDTH - 1u - col));
                if ((rows[row] & mask) == 0) {
                    continue;
                }
                const float x0 = cursorX + static_cast<float>(col) * pixelSize;
                const float y0 = y + static_cast<float>(row) * pixelSize;
                const float x1 = x0 + pixelSize;
                const float y1 = y0 + pixelSize;
                AppendOverlayQuad(vertices, x0, y0, x1, y1, color, static_cast<float>(screenWidth), static_cast<float>(screenHeight));
            }
        }
        cursorX += advance;
    }
}

bool EnsureOverlayVertexBuffer(AppState &app, const uint64_t requiredVertices)
{
    if (requiredVertices == 0) {
        return true;
    }
    if (overlayState.vertexBuffer != nullptr && overlayState.vertexCapacity >= requiredVertices) {
        return true;
    }
    if (overlayState.vertexBuffer != nullptr) {
        wgpuBufferRelease(overlayState.vertexBuffer);
        overlayState.vertexBuffer = nullptr;
        overlayState.vertexCapacity = 0;
    }
    overlayState.vertexCapacity = std::max<uint64_t>(requiredVertices, OVERLAY_MIN_VERTEX_CAPACITY);
    const WGPUBufferDescriptor descriptor{
        .label = ToWgpuString("Debug Overlay Vertex Buffer"),
        .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
        .size = overlayState.vertexCapacity * sizeof(OverlayVertex),
    };
    overlayState.vertexBuffer = wgpuDeviceCreateBuffer(app.gpu.device, &descriptor);
    return overlayState.vertexBuffer != nullptr;
}

bool CreateOverlayResources(AppState &app, WGPUShaderModule overlayShader)
{
    overlayState.shader = overlayShader;
    if (!overlayState.shader) {
        return false;
    }

    const auto blendState = WGPUBlendState{
        .color = WGPUBlendComponent{
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_SrcAlpha,
            .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
        },
        .alpha = WGPUBlendComponent{
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_One,
            .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
        },
    };
    const auto colorTarget = WGPUColorTargetState{
        .format = app.gpu.surfaceConfig.format,
        .blend = &blendState,
        .writeMask = WGPUColorWriteMask_All,
    };
    const auto fragmentState = WGPUFragmentState{
        .module = overlayState.shader,
        .entryPoint = ToWgpuString("fs_main"),
        .targetCount = 1,
        .targets = &colorTarget,
    };
    const auto attributes = std::to_array<WGPUVertexAttribute>({
        WGPUVertexAttribute{
            .format = WGPUVertexFormat_Float32x2,
            .offset = offsetof(OverlayVertex, position),
            .shaderLocation = 0,
        },
        WGPUVertexAttribute{
            .format = WGPUVertexFormat_Float32x4,
            .offset = offsetof(OverlayVertex, color),
            .shaderLocation = 1,
        },
    });
    const auto vertexBufferLayout = WGPUVertexBufferLayout{
        .stepMode = WGPUVertexStepMode_Vertex,
        .arrayStride = sizeof(OverlayVertex),
        .attributeCount = static_cast<uint32_t>(attributes.size()),
        .attributes = attributes.data(),
    };
    const auto pipelineDesc = WGPURenderPipelineDescriptor{
        .label = ToWgpuString("Debug Overlay Pipeline"),
        .layout = nullptr,
        .vertex = WGPUVertexState{
            .module = overlayState.shader,
            .entryPoint = ToWgpuString("vs_main"),
            .bufferCount = 1,
            .buffers = &vertexBufferLayout,
        },
        .primitive = WGPUPrimitiveState{
            .topology = WGPUPrimitiveTopology_TriangleList,
            .stripIndexFormat = WGPUIndexFormat_Undefined,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = WGPUCullMode_None,
        },
        .depthStencil = nullptr,
        .multisample = WGPUMultisampleState{
            .count = 1,
            .mask = ~0u,
        },
        .fragment = &fragmentState,
    };
    overlayState.pipeline = wgpuDeviceCreateRenderPipeline(app.gpu.device, &pipelineDesc);
    return overlayState.pipeline != nullptr;
}

void ReleaseOverlayResources()
{
    if (overlayState.vertexBuffer != nullptr) {
        wgpuBufferRelease(overlayState.vertexBuffer);
        overlayState.vertexBuffer = nullptr;
        overlayState.vertexCapacity = 0;
    }
    if (overlayState.pipeline != nullptr) {
        wgpuRenderPipelineRelease(overlayState.pipeline);
        overlayState.pipeline = nullptr;
    }
    if (overlayState.shader != nullptr) {
        wgpuShaderModuleRelease(overlayState.shader);
        overlayState.shader = nullptr;
    }
}

void UpdateFpsCounter()
{
    const Uint64 now = SDL_GetPerformanceCounter();
    if (overlayState.fpsCounterStart == 0) {
        overlayState.fpsCounterStart = now;
    }
    overlayState.fpsFrameCount += 1;
    const Uint64 elapsed = now - overlayState.fpsCounterStart;
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    const double elapsedSeconds = static_cast<double>(elapsed) / frequency;
    if (elapsedSeconds >= FPS_UPDATE_INTERVAL_SECONDS) {
        overlayState.currentFps = static_cast<float>(static_cast<double>(overlayState.fpsFrameCount) / elapsedSeconds);
        overlayState.fpsFrameCount = 0;
        overlayState.fpsCounterStart = now;
    }
}

void RenderOverlay(AppState &app, WGPURenderPassEncoder pass)
{
    if (!overlayState.pipeline) {
        return;
    }

    std::vector<OverlayVertex> vertices;
    vertices.reserve(OVERLAY_VERTEX_RESERVE_SIZE);

    std::ostringstream fpsStream;
    fpsStream << std::fixed << std::setprecision(FPS_DISPLAY_PRECISION) << overlayState.currentFps;
    const std::string fpsText = "FPS: " + fpsStream.str();
    const float charAdvance = static_cast<float>(OVERLAY_CHAR_WIDTH + OVERLAY_CHAR_SPACING) * OVERLAY_TEXT_SCALE;
    const float fpsWidth = static_cast<float>(fpsText.size()) * charAdvance;
    const float fpsX = std::max(0.0f, static_cast<float>(app.gpu.width) - static_cast<float>(OVERLAY_MARGIN) - fpsWidth);
    const float fpsY = static_cast<float>(OVERLAY_MARGIN);
    AppendOverlayText(
        vertices,
        fpsText,
        fpsX,
        fpsY,
        OVERLAY_TEXT_SCALE,
        glm::vec4(0.9f, 0.95f, 0.2f, 1.0f),
        app.gpu.width,
        app.gpu.height
    );

    const float lineHeight = static_cast<float>(OVERLAY_CHAR_HEIGHT) * OVERLAY_TEXT_SCALE + static_cast<float>(OVERLAY_LINE_SPACING);
    const float usableWidth = static_cast<float>(std::max<int>(1, static_cast<int>(app.gpu.width) - static_cast<int>(OVERLAY_MARGIN * 2)));
    const float maxCharsPerLine = std::max(
        1.0f,
        usableWidth / charAdvance
    );
    std::vector<std::string> visibleMessages;
    visibleMessages.reserve(overlayState.debugMessages.size());
    for (const auto &message : overlayState.debugMessages) {
        if (message.empty()) {
            continue;
        }
        std::string line = message;
        if (line.size() > static_cast<size_t>(maxCharsPerLine)) {
            line.resize(static_cast<size_t>(maxCharsPerLine));
        }
        visibleMessages.push_back(std::move(line));
    }

    const float debugStartY = std::max(
        static_cast<float>(OVERLAY_MARGIN),
        static_cast<float>(app.gpu.height) - static_cast<float>(OVERLAY_MARGIN) - lineHeight * static_cast<float>(visibleMessages.size())
    );
    for (size_t i = 0; i < visibleMessages.size(); ++i) {
        AppendOverlayText(
            vertices,
            visibleMessages[i],
            static_cast<float>(OVERLAY_MARGIN),
            debugStartY + lineHeight * static_cast<float>(i),
            OVERLAY_TEXT_SCALE,
            glm::vec4(0.85f, 0.9f, 0.95f, 1.0f),
            app.gpu.width,
            app.gpu.height
        );
    }

    if (overlayState.consoleOpen) {
        const float panelX = static_cast<float>(OVERLAY_MARGIN);
        const float panelY = static_cast<float>(OVERLAY_MARGIN);
        const float panelWidth = std::max(1.0f, static_cast<float>(app.gpu.width) - static_cast<float>(OVERLAY_MARGIN * 2));
        const float panelHeight = std::max(
            lineHeight * 4.0f,
            static_cast<float>(app.gpu.height) * OVERLAY_CONSOLE_HEIGHT_RATIO
        );
        const float panelBottom = std::min(
            static_cast<float>(app.gpu.height) - static_cast<float>(OVERLAY_MARGIN),
            panelY + panelHeight
        );
        AppendOverlayQuad(
            vertices,
            panelX,
            panelY,
            panelX + panelWidth,
            panelBottom,
            glm::vec4(0.03f, 0.03f, 0.06f, 0.82f),
            static_cast<float>(app.gpu.width),
            static_cast<float>(app.gpu.height)
        );

        const float consoleTextX = panelX + static_cast<float>(OVERLAY_MARGIN);
        const float consoleTitleY = panelY + static_cast<float>(OVERLAY_MARGIN);
        const float consoleInputY = panelBottom - static_cast<float>(OVERLAY_MARGIN) - lineHeight;
        const float textAreaHeight = std::max(0.0f, consoleInputY - (consoleTitleY + lineHeight));
        const size_t maxHistoryLines = std::max<size_t>(1, static_cast<size_t>(textAreaHeight / lineHeight));

        AppendOverlayText(
            vertices,
            "Console",
            consoleTextX,
            consoleTitleY,
            OVERLAY_TEXT_SCALE,
            glm::vec4(0.9f, 0.95f, 1.0f, 1.0f),
            app.gpu.width,
            app.gpu.height
        );

        const size_t linesToDraw = std::min(maxHistoryLines, overlayState.consoleLines.size());
        const size_t lineStart = overlayState.consoleLines.size() - linesToDraw;
        const float consoleTextWidth = std::max(1.0f, panelWidth - static_cast<float>(OVERLAY_MARGIN * 2));
        const size_t maxConsoleChars = std::max<size_t>(1, static_cast<size_t>(consoleTextWidth / charAdvance));
        for (size_t i = 0; i < linesToDraw; ++i) {
            std::string line = overlayState.consoleLines[lineStart + i];
            if (line.size() > maxConsoleChars) {
                line.resize(maxConsoleChars);
            }
            AppendOverlayText(
                vertices,
                line,
                consoleTextX,
                consoleTitleY + lineHeight * static_cast<float>(i + 1),
                OVERLAY_TEXT_SCALE,
                glm::vec4(0.8f, 0.9f, 0.95f, 1.0f),
                app.gpu.width,
                app.gpu.height
            );
        }

        std::string inputLine = "Input: " + overlayState.consoleInput + "_";
        if (inputLine.size() > maxConsoleChars) {
            const std::string marker = "Input: ...";
            if (maxConsoleChars > marker.size()) {
                const size_t tailSize = maxConsoleChars - marker.size();
                inputLine = marker + inputLine.substr(inputLine.size() - tailSize);
            } else {
                inputLine = inputLine.substr(0, maxConsoleChars);
            }
        }
        AppendOverlayText(
            vertices,
            inputLine,
            consoleTextX,
            consoleInputY,
            OVERLAY_TEXT_SCALE,
            glm::vec4(0.95f, 0.95f, 0.95f, 1.0f),
            app.gpu.width,
            app.gpu.height
        );
    }

    if (vertices.empty()) {
        return;
    }
    if (!EnsureOverlayVertexBuffer(app, vertices.size())) {
        return;
    }
    wgpuQueueWriteBuffer(app.gpu.queue, overlayState.vertexBuffer, 0, vertices.data(), vertices.size() * sizeof(OverlayVertex));
    wgpuRenderPassEncoderSetPipeline(pass, overlayState.pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, overlayState.vertexBuffer, 0, vertices.size() * sizeof(OverlayVertex));
    wgpuRenderPassEncoderDraw(pass, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
}

bool DrawFrame(AppState &app)
{
    WGPUSurfaceTexture surfaceTexture{};
    wgpuSurfaceGetCurrentTexture(app.gpu.surface, &surfaceTexture);

    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        return ConfigureSurface(app);
    }

#ifdef TRIANGLE_SAMPLE
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
#endif

    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, nullptr);
    if (!view) {
        wgpuTextureRelease(surfaceTexture.texture);
        return false;
    }

    gameObject1.rotation = glm::quat(glm::vec3(0.0f, std::sin(static_cast<float>(SDL_GetTicks()) * 0.001f) * 1.2f, 0.0f));
    gameObject2.rotation = glm::quat(glm::vec3(0.0f, static_cast<float>(SDL_GetTicks()) * 0.001f, 0.0f));
    // gameObject3.rotation = glm::quat(glm::vec3(0.0f, static_cast<float>(SDL_GetTicks()) * 0.001f, 0.0f));

    const auto forward = glm::normalize(flyCamera.rotation);
    app.gpu.viewMatrix = glm::lookAt(
        flyCamera.position,
        flyCamera.position + forward,
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    const auto transform = OPEN_GL_TO_WGPU_MATRIX * app.gpu.projectionMatrix * app.gpu.viewMatrix;
    const auto cameraData = CameraUniform{
        .viewProjectionMatrix = transform,
        .position = glm::vec4(flyCamera.position, 1.0f),
        .forward = glm::vec4(forward, 0.0f),
    };
    UpdateCascadeData(app);

    wgpuQueueWriteBuffer(
        app.gpu.queue,
        app.gpu.cameraUniformBuffer,
        0,
        &cameraData,
        sizeof(CameraUniform)
    );

    for (auto &object : objects) {
        auto modelMatrix = glm::translate(glm::mat4(1.0f), object->translation)
                         * glm::mat4_cast(object->rotation)
                         * glm::scale(glm::mat4(1.0f), object->scale);
        auto normalMatrix = glm::transpose(glm::inverse(modelMatrix));
        ModelMatrixUniform modelMatrices{
            .modelMatrix = modelMatrix,
            .normalMatrix = normalMatrix,
        };
        wgpuQueueWriteBuffer(
            app.gpu.queue,
            object->uniformBuffer,
            0,
            &modelMatrices,
            sizeof(ModelMatrixUniform)
        );
    }

    auto shadowLight = directionalLight;
    for (int cascadeIndex = 0; cascadeIndex < CASCADE_COUNT; ++cascadeIndex) {
        shadowLight.cascades[0] = directionalLight.cascades[cascadeIndex];
        wgpuQueueWriteBuffer(app.gpu.queue, app.gpu.lightUniformBuffer, 0, &shadowLight, sizeof(LightUniform));

        auto shadowCommandEncoder = wgpuDeviceCreateCommandEncoder(app.gpu.device, nullptr);

        const auto depthStencilAttachment = WGPURenderPassDepthStencilAttachment{
            .view = app.gpu.shadowDepthTextureViews[cascadeIndex],
            .depthLoadOp = WGPULoadOp_Clear,
            .depthStoreOp = WGPUStoreOp_Store,
            .depthClearValue = 1.0f,
        };
        const auto renderPassDesc = WGPURenderPassDescriptor{
            .colorAttachmentCount = 0,
            .depthStencilAttachment = &depthStencilAttachment,
        };
        auto shadowRenderPassEncoder = wgpuCommandEncoderBeginRenderPass(shadowCommandEncoder, &renderPassDesc);

        wgpuRenderPassEncoderSetPipeline(shadowRenderPassEncoder, pipelines["shadowCaster"]);
        wgpuRenderPassEncoderSetBindGroup(shadowRenderPassEncoder, 0, app.gpu.sceneBindGroup, 0, nullptr);
        RenderShadowObjects(shadowRenderPassEncoder);

        wgpuRenderPassEncoderEnd(shadowRenderPassEncoder);
        wgpuRenderPassEncoderRelease(shadowRenderPassEncoder);

        auto shadowCommandBuffer = wgpuCommandEncoderFinish(shadowCommandEncoder, nullptr);
        wgpuCommandEncoderRelease(shadowCommandEncoder);
        wgpuQueueSubmit(app.gpu.queue, 1, &shadowCommandBuffer);
        wgpuCommandBufferRelease(shadowCommandBuffer);
    }

    wgpuQueueWriteBuffer(
        app.gpu.queue,
        app.gpu.lightUniformBuffer,
        0,
        &directionalLight,
        sizeof(LightUniform)
    );

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
        .clearValue = WGPUColor{0.2, 0.2, 0.2, 1.0},
    };
    WGPURenderPassDepthStencilAttachment depthAttachment {
        .view = app.gpu.depthTextureView, // No depth attachment for this simple example
        .depthLoadOp = WGPULoadOp_Clear,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 1.0f,
        .stencilLoadOp = WGPULoadOp_Undefined,
        .stencilStoreOp = WGPUStoreOp_Undefined,
        .stencilClearValue = 0,
        .stencilReadOnly = true,
    };
    WGPURenderPassDescriptor passDesc {
        .colorAttachmentCount = 1,
        .colorAttachments = &colorAttachment,
        .depthStencilAttachment = &depthAttachment,
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    wgpuRenderPassEncoderSetPipeline(pass, pipelines["forwardRenderer"]);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, app.gpu.sceneBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 3, app.gpu.shadowBindGroup, 0, nullptr);

    RenderObjects(pass);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPURenderPassColorAttachment colorAttachment2 {
        .view = view,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = WGPULoadOp_Load,
        .storeOp = WGPUStoreOp_Store,
    };

    WGPURenderPassDescriptor passDesc2 {
        .colorAttachmentCount = 1,
        .colorAttachments = &colorAttachment2,
    };

    pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc2);
    RenderOverlay(app, pass);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

#ifdef TRIANGLE_SAMPLE
    passDesc.depthStencilAttachment = nullptr;
    pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, app.gpu.pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, app.gpu.rotationBindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
#endif

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
            PushDebugMessage("Surface reconfigured after window resize");
        } else if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            app.mouseLookEnabled = false;
            SDL_SetRelativeMouseMode(SDL_FALSE);
            PushDebugMessage("Mouse-look disabled due to focus loss");
        } else if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0 && ev.key.keysym.scancode == SDL_SCANCODE_GRAVE) {
            SetConsoleOpen(app, !overlayState.consoleOpen);
        } else if (overlayState.consoleOpen && ev.type == SDL_TEXTINPUT) {
            for (size_t i = 0; ev.text.text[i] != '\0'; ++i) {
                const unsigned char ch = static_cast<unsigned char>(ev.text.text[i]);
                if (ch < 32u || ch > 126u) {
                    continue;
                }
                if (overlayState.consoleInput.size() >= OVERLAY_MAX_CONSOLE_INPUT_LENGTH) {
                    break;
                }
                overlayState.consoleInput.push_back(static_cast<char>(ch));
            }
        } else if (overlayState.consoleOpen && ev.type == SDL_KEYDOWN) {
            if (ev.key.keysym.scancode == SDL_SCANCODE_BACKSPACE && !overlayState.consoleInput.empty()) {
                overlayState.consoleInput.pop_back();
            } else if (ev.key.keysym.scancode == SDL_SCANCODE_RETURN || ev.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) {
                ExecuteConsoleCommand(overlayState.consoleInput);
                overlayState.consoleInput.clear();
            } else if (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                SetConsoleOpen(app, false);
            }
        } else if (overlayState.consoleOpen) {
            // STOP PROCESSING FURTHER EVENTS BECAUSE CONSOLE IS OPEN
        } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT && !app.mouseLookEnabled) {
            if (SDL_SetRelativeMouseMode(SDL_TRUE) == 0) {
                app.mouseLookEnabled = true;
                PushDebugMessage("Mouse-look enabled");
            } else {
                PushDebugMessage(std::string("Failed to enable mouse-look mode: ") + SDL_GetError(), true);
            }
        } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT && app.mouseLookEnabled) {
            if (SDL_SetRelativeMouseMode(SDL_FALSE) == 0) {
                app.mouseLookEnabled = false;
                PushDebugMessage("Mouse-look disabled");
            } else {
                PushDebugMessage(std::string("Failed to disable mouse-look mode: ") + SDL_GetError(), true);
            }
        } else if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0 && ev.key.keysym.scancode == SDL_SCANCODE_TAB) {
            const bool enableMouseLook = !app.mouseLookEnabled;
            if (!enableMouseLook || SDL_SetRelativeMouseMode(SDL_TRUE) == 0) {
                app.mouseLookEnabled = enableMouseLook;
                PushDebugMessage(enableMouseLook ? "Mouse-look enabled" : "Mouse-look disabled");
            } else {
                PushDebugMessage(std::string("Failed to enable mouse-look mode: ") + SDL_GetError(), true);
            }
            if (!enableMouseLook) {
                SDL_SetRelativeMouseMode(SDL_FALSE);
            }
        } else if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0 && ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            app.mouseLookEnabled = false;
            SDL_SetRelativeMouseMode(SDL_FALSE);
        } else if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0 && ev.key.keysym.scancode == SDL_SCANCODE_1) {
            if (audioManager.PlaySoundEffect(DEFAULT_SFX_NAME) < 0) {
                PushDebugMessage(std::string("Failed to play sound effect '") + DEFAULT_SFX_NAME + std::string("': ") + Mix_GetError(), true);
            }
        } else if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0 && ev.key.keysym.scancode == SDL_SCANCODE_M) {
            if (audioManager.IsMusicPlaying()) {
                audioManager.StopMusic();
            } else {
                if (!audioManager.PlayMusic(DEFAULT_MUSIC_NAME, -1)) {
                    PushDebugMessage(std::string("Failed to play music '") + DEFAULT_MUSIC_NAME + std::string("': ") + Mix_GetError(), true);
                }
            }
        } else if (ev.type == SDL_MOUSEMOTION && app.mouseLookEnabled) {
            app.mouseDeltaX += static_cast<float>(ev.motion.xrel);
            app.mouseDeltaY += static_cast<float>(ev.motion.yrel);
        }
    }
}

void FramerateLimiter(const double targetHz = 120.0f)
{
    const double targetFrameSec = 1.0 / targetHz;
    const Uint64 freq = SDL_GetPerformanceFrequency();

    static Uint64 nextFrame = SDL_GetPerformanceCounter();
    nextFrame += static_cast<Uint64>(targetFrameSec * freq);

    while (true) {
        const Uint64 now = SDL_GetPerformanceCounter();
        if (now >= nextFrame) break;

        if (const double remainingSec = static_cast<double>(nextFrame - now) / static_cast<double>(freq); remainingSec > 0.002)
        {
            SDL_Delay(static_cast<Uint32>((remainingSec - 0.001) * 1000.0));
        }
        else
        {
            // spin/yield
            SDL_Delay(0);
        }
    }
}

void UpdateFrameTiming(AppState &app)
{
    const Uint64 now = SDL_GetPerformanceCounter();
    if (app.lastFrameCounter == 0) {
        app.lastFrameCounter = now;
        app.frameDeltaSeconds = 1.0f / 60.0f;
        UpdateFpsCounter();
        return;
    }

    const Uint64 elapsed = now - app.lastFrameCounter;
    app.lastFrameCounter = now;
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    app.frameDeltaSeconds = static_cast<float>(static_cast<double>(elapsed) / frequency);
    app.frameDeltaSeconds = std::clamp(app.frameDeltaSeconds, 0.0f, 0.1f);
    UpdateFpsCounter();
}

void UpdateCameraFromInput(AppState &app)
{
    if (overlayState.consoleOpen) {
        app.mouseDeltaX = 0.0f;
        app.mouseDeltaY = 0.0f;
        return;
    }

    const Uint8 *keyboardState = SDL_GetKeyboardState(nullptr);
    float moveDelta = CAMERA_MOVE_SPEED * app.frameDeltaSeconds;
    if (keyboardState[SDL_SCANCODE_LSHIFT] || keyboardState[SDL_SCANCODE_RSHIFT]) {
        moveDelta *= CAMERA_MOVE_BOOST;
    }

    if (keyboardState[SDL_SCANCODE_W]) {
        flyCamera.MoveForward(moveDelta);
    }
    if (keyboardState[SDL_SCANCODE_S]) {
        flyCamera.MoveForward(-moveDelta);
    }
    if (keyboardState[SDL_SCANCODE_A]) {
        flyCamera.MoveRight(moveDelta);
    }
    if (keyboardState[SDL_SCANCODE_D]) {
        flyCamera.MoveRight(-moveDelta);
    }
    if (keyboardState[SDL_SCANCODE_E]) {
        flyCamera.MoveVertical(moveDelta);
    }
    if (keyboardState[SDL_SCANCODE_Q]) {
        flyCamera.MoveVertical(-moveDelta);
    }

    if (app.mouseLookEnabled) {
        if (app.mouseDeltaX != 0.0f) {
            flyCamera.AdjustYaw(app.mouseDeltaX);
        }
        if (app.mouseDeltaY != 0.0f) {
            flyCamera.AdjustPitch(app.mouseDeltaY);
        }
    }

    app.mouseDeltaX = 0.0f;
    app.mouseDeltaY = 0.0f;
}

#if defined(__EMSCRIPTEN__)
void WasmMainLoop(void *userdata)
{
    auto *app = static_cast<AppState *>(userdata);
    UpdateFrameTiming(*app);
    PumpEvents(*app);
    if (!app->running) {
        emscripten_cancel_main_loop();
        return;
    }
    UpdateCameraFromInput(*app);
    DrawFrame(*app);
}
#endif

} // namespace

std::string readShaderFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filepath);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

WGPUShaderModule createShaderModule(WGPUDevice device, const std::string& filepath) {
    const std::string shaderCode = readShaderFile(filepath);
    WGPUShaderSourceWGSL wgslSource {
        .chain = WGPUChainedStruct{
            .next = nullptr,
            .sType = WGPUSType_ShaderSourceWGSL
        },
        .code = {shaderCode.c_str(), WGPU_STRLEN},
    };

    const WGPUShaderModuleDescriptor shaderDesc {
        .nextInChain = &wgslSource.chain,
    };

    return wgpuDeviceCreateShaderModule(device, &shaderDesc);
}

int main()
{
    auto player = GameObject::Instantiate(glm::vec3(2.0f, 0.0f, -2.0f), glm::identity<glm::quat>());
    IMG_Init(IMG_INIT_PNG);

    gameObject1.meshName = "cube";
    gameObject1.translation = glm::vec3(0.0f, 0.0f, 0.0f);
    gameObject1.rotation = glm::quat(glm::vec3(0.0f, 0.0f, 0.0f));
    gameObject1.scale = glm::vec3(1.0f, 1.0f, 1.0f);

    gameObject2.meshName = "duck";
    gameObject2.translation = glm::vec3(2.0f, -2.0f, 0.0f);
    gameObject2.rotation = glm::quat(glm::vec3(0.0f, 0.0f, 0.0f));
    gameObject2.scale = glm::vec3(100.0f, 100.0f, 100.0f);

    gameObject3.meshName = "plane";
    gameObject3.translation = glm::vec3(0.0f, -4.0f, 0.0f);
    gameObject3.rotation = glm::quat(glm::vec3(-0.5f * glm::pi<float>(), 0.0f, 0.0f));
    gameObject3.scale = glm::vec3(40.0f, 40.0f, 40.0f);

    objects.push_back(&gameObject1);
    // objects.push_back(gameObject2);
    objects.push_back(&gameObject3);

    SDL_SetMainReady();

    JoltRuntime jolt;
    if (!jolt.Initialize()) {
        PushDebugMessage("Failed to initialize Jolt", true);
        return 1;
    }
    PushDebugMessage("Jolt initialized");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        PushDebugMessage(std::string("SDL_Init failed: ") + SDL_GetError(), true);
        return 1;
    }
    SDL_StopTextInput();
    PushDebugMessage("SDL initialized");

    if (std::string audioError; !audioManager.Initialize(audioError, 32)) {
        std::cerr << "Audio initialization failed: " << audioError << '\n';
    } else {
        if (std::filesystem::exists(DEFAULT_SFX_PATH)) {
            if (!audioManager.LoadSoundEffect(DEFAULT_SFX_NAME, DEFAULT_SFX_PATH, audioError)) {
                std::cerr << "Failed to load sound effect from " << DEFAULT_SFX_PATH << ": " << audioError << '\n';
            } else {
                std::cout << "Loaded sound effect: " << DEFAULT_SFX_PATH << '\n';
            }
        } else {
            std::cout << "No default sound effect found at " << DEFAULT_SFX_PATH << '\n';
        }

        if (std::filesystem::exists(DEFAULT_MUSIC_PATH)) {
            if (!audioManager.LoadMusic(DEFAULT_MUSIC_NAME, DEFAULT_MUSIC_PATH, audioError)) {
                std::cerr << "Failed to load music from " << DEFAULT_MUSIC_PATH << ": " << audioError << '\n';
            } else {
                std::cout << "Loaded music track: " << DEFAULT_MUSIC_PATH << '\n';
            }
        } else {
            std::cout << "No default music track found at " << DEFAULT_MUSIC_PATH << '\n';
        }
    }

    AppState app;
    app.window = SDL_CreateWindow(
        "Sizzle Engine",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280,
        720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
    );
    if (!app.window) {
        PushDebugMessage(std::string("SDL_CreateWindow failed: ") + SDL_GetError(), true);
        SDL_Quit();
        return 1;
    }
    PushDebugMessage("SDL window created");

    if (!InitializeGraphics(app)) {
        PushDebugMessage("Failed to initialize WebGPU", true);
        ReleaseOverlayResources();
        ReleaseGpu(app.gpu);
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return 1;
    }
    PushDebugMessage("WebGPU initialized");

    shaders["forwardShader"] = createShaderModule(app.gpu.device, "assets/shaders/forward_renderer.wgsl");
    shaders["shadowCaster"] = createShaderModule(app.gpu.device, "assets/shaders/shadow_caster.wgsl");
    shaders["overlayShader"] = createShaderModule(app.gpu.device, "assets/shaders/overlay_shader.wgsl");
    if (!CreateOverlayResources(app, shaders["overlayShader"])) {
        PushDebugMessage("Failed to create debug text overlay resources", true);
        ReleaseOverlayResources();
        ReleaseGpu(app.gpu);
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return 1;
    }
    PushDebugMessage("Debug text overlay initialized");

    AssetManager assetManager(app.gpu.device, app.gpu.queue);

    std::vector<Primitive> cube_primitives = std::vector<Primitive>{
        Primitive::CreateFromPremadeData(app.gpu.device, boxVertices, boxIndices, "sample.png"),
    };
    meshes["cube"] = Mesh { .primitives = cube_primitives };

    std::vector<Primitive> plane_primitives = std::vector<Primitive>{
        Primitive::CreateFromPremadeData(app.gpu.device, planeVertices, planeIndices, "sample.png"),
    };
    meshes["plane"] = Mesh { .primitives = plane_primitives };

    const std::string modelPath = "assets/BoomBox.gltf";
    if (std::filesystem::exists(modelPath)) {
        std::vector<Primitive> boomBoxPrimitives;
        std::string loadError;
        if (LoadGltfPrimitives(app.gpu, assetManager, modelPath, materials, boomBoxPrimitives, loadError)) {
            meshes["duck"] = Mesh{ .primitives = std::move(boomBoxPrimitives) };
            objects.push_back(&gameObject2);
            PushDebugMessage("Loaded glTF model: " + modelPath);
        } else {
            PushDebugMessage("Failed to load glTF model: " + modelPath + " error: " + loadError, true);
        }
    } else {
        PushDebugMessage("BoomBox glTF not found, skipping: " + modelPath);
    }

    WGPUBufferDescriptor lightUniformBufferDesc {
        .label = ToWgpuString("Light Uniform Buffer"),
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(LightUniform),
    };
    app.gpu.lightUniformBuffer = wgpuDeviceCreateBuffer(app.gpu.device, &lightUniformBufferDesc);

    WGPUBufferDescriptor cameraUniformBufferDesc {
        .label = ToWgpuString("Camera Uniform Buffer"),
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = sizeof(CameraUniform),
    };
    app.gpu.cameraUniformBuffer = wgpuDeviceCreateBuffer(app.gpu.device, &cameraUniformBufferDesc);

    std::cout << "Creating bind group layouts for meshes\n";
    const auto meshBindGroupLayoutEntries = std::to_array<WGPUBindGroupLayoutEntry>({
        WGPUBindGroupLayoutEntry{
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
            },
        },
    });
    WGPUBindGroupLayoutDescriptor meshBindGroupLayoutDesc {
        .label = ToWgpuString("Mesh Bind Group Layout"),
        .entryCount = static_cast<uint32_t>(meshBindGroupLayoutEntries.size()),
        .entries = meshBindGroupLayoutEntries.data(),
    };
    app.gpu.meshBindGroupLayout = wgpuDeviceCreateBindGroupLayout(app.gpu.device, &meshBindGroupLayoutDesc);

    for (auto &object : objects) {
        std::cout << "Creating uniform buffer for object with mesh: " << object->meshName << "\n";
        WGPUBufferDescriptor uniformBufferDesc{
            .label = ToWgpuString("Mesh Uniform Buffer"),
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = sizeof(ModelMatrixUniform),
        };
        object->uniformBuffer = wgpuDeviceCreateBuffer(app.gpu.device, &uniformBufferDesc);

        std::cout << "Creating bind group for object with mesh: " << object->meshName << "\n";
        const auto meshBindGroupEntries = std::to_array<WGPUBindGroupEntry>({
            WGPUBindGroupEntry{
                .binding = 0,
                .buffer = object->uniformBuffer,
                .size = sizeof(ModelMatrixUniform),
            }
        });
        WGPUBindGroupDescriptor meshBindGroupDesc{
            .label = ToWgpuString("Mesh Bind Group"),
            .layout = app.gpu.meshBindGroupLayout,
            .entryCount = static_cast<uint32_t>(meshBindGroupEntries.size()),
            .entries = meshBindGroupEntries.data(),
        };
        object->uniformBindGroup = wgpuDeviceCreateBindGroup(app.gpu.device, &meshBindGroupDesc);
    }

    app.gpu.sceneBindGroupLayout = [&] {
        const auto entries = std::to_array<WGPUBindGroupLayoutEntry>({
            WGPUBindGroupLayoutEntry{
                .binding = 0,
                .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                .buffer = WGPUBufferBindingLayout{
                    .type = WGPUBufferBindingType_Uniform,
                },
            },
            WGPUBindGroupLayoutEntry{
                .binding = 1,
                .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                .buffer = WGPUBufferBindingLayout{
                    .type = WGPUBufferBindingType_Uniform,
                },
            },
        });
        const WGPUBindGroupLayoutDescriptor desc{
            .label = ToWgpuString("Scene Bind Group Layout"),
            .entryCount = static_cast<uint32_t>(entries.size()),
            .entries = entries.data(),
        };
        std::cout << "Creating scene bind group layout\n";
        return wgpuDeviceCreateBindGroupLayout(app.gpu.device, &desc);
    }();

    app.gpu.sceneBindGroup = [&] {
        const auto entries = std::to_array<WGPUBindGroupEntry>({
            WGPUBindGroupEntry{
                .binding = 0,
                .buffer = app.gpu.cameraUniformBuffer,
                .size = sizeof(CameraUniform),
            },
            WGPUBindGroupEntry{
                .binding = 1,
                .buffer = app.gpu.lightUniformBuffer,
                .size = sizeof(LightUniform),
            },
        });
        const WGPUBindGroupDescriptor desc{
            .label = ToWgpuString("Scene Bind Group"),
            .layout = app.gpu.sceneBindGroupLayout,
            .entryCount = static_cast<uint32_t>(entries.size()),
            .entries = entries.data(),
        };
        std::cout << "Creating scene bind group\n";
        return wgpuDeviceCreateBindGroup(app.gpu.device, &desc);
    }();

    app.gpu.shadowDepthTexture = [&] {
        const WGPUTextureDescriptor desc{
            .label = ToWgpuString("Shadow Depth Texture"),
            .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
            .dimension = WGPUTextureDimension_2D,
            .size = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, CASCADE_COUNT},
            .format = DEPTH_FORMAT,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };
        std::cout << "Creating shadow depth texture of size: " << SHADOW_MAP_SIZE << "x" << SHADOW_MAP_SIZE << " with " << CASCADE_COUNT << " cascades\n";
        return wgpuDeviceCreateTexture(app.gpu.device, &desc);
    }();

    for (uint32_t cascadeIndex = 0; cascadeIndex < CASCADE_COUNT; ++cascadeIndex) {
        app.gpu.shadowDepthTextureViews[cascadeIndex] = [&] {
            const WGPUTextureViewDescriptor desc{
                .label = ToWgpuString("Shadow Depth Texture View"),
                .format = DEPTH_FORMAT,
                .dimension = WGPUTextureViewDimension_2D,
                .baseMipLevel = 0,
                .mipLevelCount = 1,
                .baseArrayLayer = cascadeIndex,
                .arrayLayerCount = 1,
                .aspect = WGPUTextureAspect_DepthOnly,
            };
            std::cout << "Creating shadow depth texture view for cascade " << cascadeIndex << "\n";
            return wgpuTextureCreateView(app.gpu.shadowDepthTexture, &desc);
        }();
    }

    app.gpu.shadowDepthTextureArrayView = [&] {
        const WGPUTextureViewDescriptor desc{
            .label = ToWgpuString("Shadow Depth Texture View"),
            .format = DEPTH_FORMAT,
            .dimension = WGPUTextureViewDimension_2DArray,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = CASCADE_COUNT,
            .aspect = WGPUTextureAspect_DepthOnly,
        };
        std::cout << "Creating shadow depth texture array view for all cascades\n";
        return wgpuTextureCreateView(app.gpu.shadowDepthTexture, &desc);
    }();

    app.gpu.shadowSampler = [&] {
        const WGPUSamplerDescriptor desc{
            .label = ToWgpuString("Shadow Comparison Sampler"),
            .addressModeU = WGPUAddressMode_ClampToEdge,
            .addressModeV = WGPUAddressMode_ClampToEdge,
            .addressModeW = WGPUAddressMode_ClampToEdge,
            .magFilter = WGPUFilterMode_Linear,
            .minFilter = WGPUFilterMode_Linear,
            .mipmapFilter = WGPUMipmapFilterMode_Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 32.0f,
            .compare = WGPUCompareFunction_LessEqual,
            .maxAnisotropy = 1,
        };
        std::cout << "Creating shadow comparison sampler\n";
        return wgpuDeviceCreateSampler(app.gpu.device, &desc);
    }();

    app.gpu.shadowBindGroupLayout = [&] {
        const auto entries = std::to_array<WGPUBindGroupLayoutEntry>({
            WGPUBindGroupLayoutEntry{
                .binding = 0,
                .visibility = WGPUShaderStage_Fragment,
                .texture = WGPUTextureBindingLayout{
                    .sampleType = WGPUTextureSampleType_Depth,
                    .viewDimension = WGPUTextureViewDimension_2DArray,
                    .multisampled = false,
                },
            },
            WGPUBindGroupLayoutEntry{
                .binding = 1,
                .visibility = WGPUShaderStage_Fragment,
                .sampler = WGPUSamplerBindingLayout{
                    .type = WGPUSamplerBindingType_Comparison,
                },
            },
        });
        const WGPUBindGroupLayoutDescriptor desc{
            .label = ToWgpuString("Shadow Bind Group Layout"),
            .entryCount = static_cast<uint32_t>(entries.size()),
            .entries = entries.data(),
        };
        std::cout << "Creating shadow bind group layout\n";
        return wgpuDeviceCreateBindGroupLayout(app.gpu.device, &desc);
    }();

    app.gpu.shadowBindGroup = [&] {
        const auto entries = std::to_array<WGPUBindGroupEntry>({
            WGPUBindGroupEntry{
                .binding = 0,
                .textureView = app.gpu.shadowDepthTextureArrayView,
            },
            WGPUBindGroupEntry{
                .binding = 1,
                .sampler = app.gpu.shadowSampler,
            },
        });
        const WGPUBindGroupDescriptor desc{
            .label = ToWgpuString("Shadow Bind Group"),
            .layout = app.gpu.shadowBindGroupLayout,
            .entryCount = static_cast<uint32_t>(entries.size()),
            .entries = entries.data(),
        };
        return wgpuDeviceCreateBindGroup(app.gpu.device, &desc);
    }();

    std::string sampleTextureError;
    const auto sampleTexture = assetManager.RequestTexture("sample.png", sampleTextureError);
    if (!sampleTexture) {
        PushDebugMessage("Failed to load default texture sample.png: " + sampleTextureError, true);
        ReleaseOverlayResources();
        return 1;
    }

    const auto texture = sampleTexture->getTexture();
    const auto textureView = sampleTexture->getTextureView();
    materials["sample.png"] = UnlitMaterial{
        .id = 0,
        .baseColorTexture = texture,
        .baseColorTextureView = textureView,
        .bindGroup = [&] {
            const auto entries = std::to_array<WGPUBindGroupEntry>({
                WGPUBindGroupEntry{
                    .binding = 0,
                    .textureView = textureView,
                },
                WGPUBindGroupEntry{
                    .binding = 1,
                    .sampler = app.gpu.defaultSampler,
                },
            });
            const WGPUBindGroupDescriptor desc{
                .label = ToWgpuString("Sample Texture Bind Group"),
                .layout = app.gpu.defaultSamplerBindGroupLayout,
                .entryCount = static_cast<uint32_t>(entries.size()),
                .entries = entries.data(),
            };
            return wgpuDeviceCreateBindGroup(app.gpu.device, &desc);
        }(),
    };

    std::cout << "Creating forward renderer pipeline layout\n";
    pipelineLayouts["forwardRenderer"] = [&] {
        const auto layouts = std::to_array<WGPUBindGroupLayout>({
            app.gpu.sceneBindGroupLayout,
            app.gpu.meshBindGroupLayout,
            app.gpu.defaultSamplerBindGroupLayout,
            app.gpu.shadowBindGroupLayout,
        });
        const WGPUPipelineLayoutDescriptor desc{
            .label = ToWgpuString("Forward Renderer Pipeline Layout"),
            .bindGroupLayoutCount = static_cast<uint32_t>(layouts.size()),
            .bindGroupLayouts = layouts.data(),
        };
        return wgpuDeviceCreatePipelineLayout(app.gpu.device, &desc);
    }();

    std::cout << "Creating forward renderer pipeline\n";
    pipelines["forwardRenderer"] = [&] {
        WGPUVertexAttribute vertexAttributes[] = {
            {
                .format = WGPUVertexFormat_Float32x3,
                .offset = offsetof(Vertex, position),
                .shaderLocation = 0,
            },
            {
                .format = WGPUVertexFormat_Float32x2,
                .offset = offsetof(Vertex, uv),
                .shaderLocation = 1,
            },
            {
                .format = WGPUVertexFormat_Float32x3,
                .offset = offsetof(Vertex, normal),
                .shaderLocation = 2,
            },
        };
        auto vertexBufferLayouts = std::to_array<WGPUVertexBufferLayout>({
            WGPUVertexBufferLayout{
                .stepMode = WGPUVertexStepMode_Vertex,
                .arrayStride = sizeof(Vertex),
                .attributeCount = static_cast<uint32_t>(std::size(vertexAttributes)),
                .attributes = vertexAttributes,
            }
        });
        WGPUBlendState blendState {
            .color = {
                .operation = WGPUBlendOperation_Add,
                .srcFactor = WGPUBlendFactor_SrcAlpha,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
            },
            .alpha = {
                .operation = WGPUBlendOperation_Add,
                .srcFactor = WGPUBlendFactor_SrcAlpha,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
            },
        };
        auto colorTargetState = std::to_array<WGPUColorTargetState>({
            {
                .format = WGPUTextureFormat_BGRA8Unorm,
                .blend = &blendState,
                .writeMask = WGPUColorWriteMask_All,
            }
        });
        WGPUFragmentState fragmentState{
            .module = shaders["forwardShader"],
            .entryPoint = ToWgpuString("fs_main"),
            .targetCount = static_cast<uint32_t>(colorTargetState.size()),
            .targets = colorTargetState.data(),
        };
        WGPUDepthStencilState depthStencilState{
            .format = DEPTH_FORMAT,
            .depthWriteEnabled = WGPUOptionalBool_True,
            .depthCompare = WGPUCompareFunction_Less,
            .stencilFront = {
                .compare = WGPUCompareFunction_Always,
                .failOp = WGPUStencilOperation_Keep,
                .depthFailOp = WGPUStencilOperation_Keep,
                .passOp = WGPUStencilOperation_Keep,
            },
            .stencilBack = {
                .compare = WGPUCompareFunction_Always,
                .failOp = WGPUStencilOperation_Keep,
                .depthFailOp = WGPUStencilOperation_Keep,
                .passOp = WGPUStencilOperation_Keep,
            },
            .stencilReadMask = 0,
            .stencilWriteMask = 0,
        };
        WGPURenderPipelineDescriptor pipelineDesc{
            .label = ToWgpuString("Forward Renderer Pipeline"),
            .layout = pipelineLayouts["forwardRenderer"],
            .vertex = {
                .module = shaders["forwardShader"],
                .entryPoint = ToWgpuString("vs_main"),
                .bufferCount = static_cast<uint32_t>(vertexBufferLayouts.size()),
                .buffers = vertexBufferLayouts.data(),
            },
            .primitive = {
                .topology = WGPUPrimitiveTopology_TriangleList,
                .stripIndexFormat = WGPUIndexFormat_Undefined,
                .frontFace = WGPUFrontFace_CCW,
                .cullMode = WGPUCullMode_Back,
            },
            .depthStencil = &depthStencilState,
            .multisample = {
                .count = 1,
                .mask = ~0u,
            },
            .fragment = &fragmentState,
        };
        return wgpuDeviceCreateRenderPipeline(app.gpu.device, &pipelineDesc);
    }();

    pipelineLayouts["shadowCaster"] = [&] {
        const auto layouts = std::to_array<WGPUBindGroupLayout>({
            app.gpu.sceneBindGroupLayout,
            app.gpu.meshBindGroupLayout,
        });
        const WGPUPipelineLayoutDescriptor desc{
            .label = ToWgpuString("Shadow Caster Pipeline Layout"),
            .bindGroupLayoutCount = static_cast<uint32_t>(layouts.size()),
            .bindGroupLayouts = layouts.data(),
        };
        return wgpuDeviceCreatePipelineLayout(app.gpu.device, &desc);
    }();

    pipelines["shadowCaster"] = [&] {
        constexpr WGPUVertexAttribute vertexAttributes[] = {
            {
                .format = WGPUVertexFormat_Float32x3,
                .offset = offsetof(Vertex, position),
                .shaderLocation = 0,
            },
        };
        const auto vertexBufferLayouts = std::to_array<WGPUVertexBufferLayout>({
            WGPUVertexBufferLayout{
                .stepMode = WGPUVertexStepMode_Vertex,
                .arrayStride = sizeof(Vertex),
                .attributeCount = static_cast<uint32_t>(std::size(vertexAttributes)),
                .attributes = vertexAttributes,
            }
        });
        WGPUDepthStencilState depthStencilState{
            .format = DEPTH_FORMAT,
            .depthWriteEnabled = WGPUOptionalBool_True,
            .depthCompare = WGPUCompareFunction_Less,
        };
        WGPURenderPipelineDescriptor pipelineDesc{
            .label = ToWgpuString("Shadow Caster Pipeline"),
            .layout = pipelineLayouts["shadowCaster"],
            .vertex = {
                .module = shaders["shadowCaster"],
                .entryPoint = ToWgpuString("vs_main"),
                .bufferCount = static_cast<uint32_t>(vertexBufferLayouts.size()),
                .buffers = vertexBufferLayouts.data(),
            },
            .primitive = {
                .topology = WGPUPrimitiveTopology_TriangleList,
                .stripIndexFormat = WGPUIndexFormat_Undefined,
                .frontFace = WGPUFrontFace_CCW,
                .cullMode = WGPUCullMode_Back,
            },
            .depthStencil = &depthStencilState,
            .multisample = {
                .count = 1,
                .mask = ~0u,
            },
            // No depth/stencil state for shadow caster in this simple example
            // No fragment state for shadow caster in this simple example
        };
        return wgpuDeviceCreateRenderPipeline(app.gpu.device, &pipelineDesc);
    }();

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop_arg(WasmMainLoop, &app, 0, 1);
    return 0;
#else
    while (app.running) {
        UpdateFrameTiming(app);
        PumpEvents(app);
        if (!app.running) {
            break;
        }
        UpdateCameraFromInput(app);
        if (!DrawFrame(app)) {
            PushDebugMessage("DrawFrame failed", true);
            break;
        }
        FramerateLimiter();
    }

    for (const auto& [key, value] : meshes) {
        for (const auto& primitive : value.primitives) {
            if (primitive.vertexBuffer) {
                wgpuBufferRelease(primitive.vertexBuffer);
            }
            if (primitive.indexBuffer) {
                wgpuBufferRelease(primitive.indexBuffer);
            }
        }
    }

    ReleaseOverlayResources();
    ReleaseGpu(app.gpu);
    audioManager.Shutdown();
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
#endif
}
