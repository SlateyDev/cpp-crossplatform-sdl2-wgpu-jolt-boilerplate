#pragma once

#include <deque>
#include <string>

#include <SDL.h>
#include <webgpu/webgpu.h>

class RmlUiOverlay {
public:
    bool Initialize(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat surfaceFormat, uint32_t width, uint32_t height);
    void Shutdown();

    void SetDimensions(uint32_t width, uint32_t height) const;
    void SetData(
        float fps,
        const std::string &hoverText,
        const std::string &characterText,
        const std::deque<std::string> &debugMessages,
        const std::deque<std::string> &consoleLines,
        bool consoleOpen,
        const std::string &consoleInput
    ) const;

    void PrepareFrame() const;
    void Render(WGPURenderPassEncoder pass) const;
    bool ProcessEvent(const SDL_Event &event) const;

private:
    struct Impl;
    Impl *impl = nullptr;
};
