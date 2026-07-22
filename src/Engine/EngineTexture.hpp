#ifndef ABERRANT_ENGINE_ENGINETEXTURE_HPP
#define ABERRANT_ENGINE_ENGINETEXTURE_HPP

#include <string>
#include <webgpu/webgpu.h>
#include <SDL_image.h>
// #include <glm/glm.hpp>


class EngineTexture {
    std::string fileName;
    SDL_Surface* surface = nullptr;
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;

public:
    bool LoadImage(const std::string& fileName);
    // EngineTexture(WGPUDevice device, WGPUQueue queue, glm::vec4 colour);
    // EngineTexture(WGPUDevice device, WGPUQueue queue, int r, int g, int b, int a);
    ~EngineTexture();

    bool CreateTextureAndView(WGPUDevice device, WGPUQueue queue);
    WGPUTexture getTexture() const;
    WGPUTextureView getTextureView() const;

    // static EngineTexture* FromColour(WGPUDevice device, WGPUQueue queue, glm::vec4 colour);
    // static EngineTexture* FromColour(WGPUDevice device, WGPUQueue queue, int r, int g, int b, int a);
    // WGPUTextureView CreateView();
};

#endif