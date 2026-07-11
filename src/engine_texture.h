#pragma once

#include <webgpu/webgpu.h>
#include <glm/glm.hpp>

class EngineTexture {
    public:
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
    EngineTexture(WGPUDevice device, WGPUQueue queue, glm::vec4 colour);
    EngineTexture(WGPUDevice device, WGPUQueue queue, int r, int g, int b, int a);
    ~EngineTexture();

    static EngineTexture* FromColour(WGPUDevice device, WGPUQueue queue, glm::vec4 colour);
    static EngineTexture* FromColour(WGPUDevice device, WGPUQueue queue, int r, int g, int b, int a);
    WGPUTextureView CreateView();
};
