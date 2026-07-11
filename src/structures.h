#pragma once

#include <array>
#include <glm/glm.hpp>
#include <SDL.h>

#include <webgpu/webgpu.h>

// #define TRIANGLE_SAMPLE

constexpr uint32_t CASCADE_COUNT = 4;

struct UnlitMaterial {
    int id;
    WGPUTexture baseColorTexture;
    WGPUTextureView baseColorTextureView;
    WGPUBindGroup bindGroup;
};

struct GpuState {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUSurfaceConfiguration surfaceConfig{};
    WGPUQueue queue = nullptr;
    WGPUSurface surface = nullptr;
#ifdef TRIANGLE_SAMPLE
    WGPUBuffer rotationUniformBuffer = nullptr; //
    WGPUBindGroupLayout rotationBindGroupLayout = nullptr; //
    WGPUBindGroup rotationBindGroup = nullptr; //
    WGPUPipelineLayout pipelineLayout = nullptr; //
    WGPURenderPipeline pipeline = nullptr; //
#endif
    uint32_t width = 1280; //
    uint32_t height = 720; //

    WGPUSampler defaultSampler = nullptr;
    WGPUBindGroupLayout defaultSamplerBindGroupLayout = nullptr;

    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projectionMatrix = glm::mat4(1.0f);

    WGPUBuffer cameraUniformBuffer = nullptr;
    WGPUBindGroupLayout meshBindGroupLayout = nullptr;
    WGPUBuffer lightUniformBuffer = nullptr;
    WGPUBindGroupLayout sceneBindGroupLayout = nullptr;
    WGPUBindGroup sceneBindGroup = nullptr;
    WGPUTexture shadowDepthTexture = nullptr;
    std::array<WGPUTextureView, CASCADE_COUNT> shadowDepthTextureViews{};
    WGPUTextureView shadowDepthTextureArrayView = nullptr;
    WGPUSampler shadowSampler = nullptr;
    WGPUBindGroupLayout shadowBindGroupLayout = nullptr;
    WGPUBindGroup shadowBindGroup = nullptr;
    WGPUTexture depthTexture = nullptr;
    WGPUTextureView depthTextureView = nullptr;
};

struct AppState {
    SDL_Window *window = nullptr;
    GpuState gpu;
    bool running = true;
    bool mouseLookEnabled = false;
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    float frameDeltaSeconds = 1.0f / 60.0f;
    Uint64 lastFrameCounter = 0;
};
