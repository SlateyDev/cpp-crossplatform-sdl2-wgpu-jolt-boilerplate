#include "engine_texture.hpp"

#include <queue>

EngineTexture::EngineTexture(const WGPUDevice device, const WGPUQueue queue, const glm::vec4 colour) : EngineTexture(device, queue, static_cast<int>(colour.r * 255), static_cast<int>(colour.g * 255), static_cast<int>(colour.b * 255), static_cast<int>(colour.a * 255)) {
}

EngineTexture::EngineTexture(const WGPUDevice device, const WGPUQueue queue, const int r, const int g, const int b, const int a) {
    constexpr WGPUTextureDescriptor textureDesc {
        .label = "Colour Texture",
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = {1, 1, 1},
        .format = WGPUTextureFormat_RGBA8Unorm,
        .mipLevelCount = 1,
        .sampleCount = 1,
    };
    texture = wgpuDeviceCreateTexture(device, &textureDesc);

    const int colour[4] {
        r, g, b, a
    };
    const WGPUTexelCopyTextureInfo copyTextureInfo {
        .texture = texture,
        .mipLevel = 0,
        .origin = {},
        .aspect = WGPUTextureAspect_All,
    };
    constexpr WGPUTexelCopyBufferLayout copyBufferLayout {
        .offset = 0,
        .bytesPerRow = 256,
        .rowsPerImage = 1,
    };
    constexpr WGPUExtent3D writeSize = {1, 1, 1};
    wgpuQueueWriteTexture(queue, &copyTextureInfo, &colour, sizeof(colour), &copyBufferLayout, &writeSize);
}

EngineTexture::~EngineTexture() {
    if (texture) wgpuTextureRelease(texture);
    texture = nullptr;
    if (view) wgpuTextureViewRelease(view);
    view = nullptr;
}

EngineTexture * EngineTexture::FromColour(const WGPUDevice device, const WGPUQueue queue, const glm::vec4 colour) {
    return new EngineTexture(device, queue, colour);
}

EngineTexture * EngineTexture::FromColour(const WGPUDevice device, const WGPUQueue queue, const int r, const int g, const int b, const int a) {
    return new EngineTexture(device, queue, r, g, b, a);
}

WGPUTextureView EngineTexture::CreateView() {
    view = wgpuTextureCreateView(texture, nullptr);
    return view;
}
