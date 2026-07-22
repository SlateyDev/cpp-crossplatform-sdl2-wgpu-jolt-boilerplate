#include "EngineTexture.hpp"

#include <iostream>

// #include <queue>

bool EngineTexture::LoadImage(const std::string& fileName) {
    this->fileName = fileName;

    SDL_Surface* loadedTexture = IMG_Load(("./assets/" + fileName).c_str());
    if (!loadedTexture) {
        std::cerr << "Failed to load image: " << fileName << " Error: " << IMG_GetError() << std::endl;
        return false;
    }
    std::cout << "Loaded texture: " << fileName << " - dimensions: " << loadedTexture->w << "x" << loadedTexture->h << std::endl;

    SDL_Surface* convertedSurface = SDL_ConvertSurfaceFormat(loadedTexture, SDL_PIXELFORMAT_BGRA32, 0);
    SDL_FreeSurface(loadedTexture);

    if (!convertedSurface) {
        std::cerr << "Failed to convert surface format for: " << fileName << " Error: " << SDL_GetError() << std::endl;
        return false;
    }
    surface = convertedSurface;
    return true;
}
// EngineTexture::EngineTexture(const WGPUDevice device, const WGPUQueue queue, const glm::vec4 colour) : EngineTexture(device, queue, static_cast<int>(colour.r * 255), static_cast<int>(colour.g * 255), static_cast<int>(colour.b * 255), static_cast<int>(colour.a * 255)) {
// }
//
// EngineTexture::EngineTexture(const WGPUDevice device, const WGPUQueue queue, const int r, const int g, const int b, const int a) {
//     constexpr WGPUTextureDescriptor textureDesc {
//         .label = "Colour Texture",
//         .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
//         .dimension = WGPUTextureDimension_2D,
//         .size = {1, 1, 1},
//         .format = WGPUTextureFormat_RGBA8Unorm,
//         .mipLevelCount = 1,
//         .sampleCount = 1,
//     };
//     texture = wgpuDeviceCreateTexture(device, &textureDesc);
//
//     const int colour[4] {
//         r, g, b, a
//     };
//     const WGPUTexelCopyTextureInfo copyTextureInfo {
//         .texture = texture,
//         .mipLevel = 0,
//         .origin = {},
//         .aspect = WGPUTextureAspect_All,
//     };
//     constexpr WGPUTexelCopyBufferLayout copyBufferLayout {
//         .offset = 0,
//         .bytesPerRow = 256,
//         .rowsPerImage = 1,
//     };
//     constexpr WGPUExtent3D writeSize = {1, 1, 1};
//     wgpuQueueWriteTexture(queue, &copyTextureInfo, &colour, sizeof(colour), &copyBufferLayout, &writeSize);
// }

EngineTexture::~EngineTexture() {
    if (texture) wgpuTextureRelease(texture);
    texture = nullptr;
    if (view) wgpuTextureViewRelease(view);
    view = nullptr;
    if (surface) SDL_FreeSurface(surface);
    surface = nullptr;
}

bool EngineTexture::CreateTextureAndView(WGPUDevice device, WGPUQueue queue)
{
    if (!surface) return false;

    const auto width = static_cast<uint32_t>(surface->w);
    const auto height = static_cast<uint32_t>(surface->h);

    const WGPUTextureDescriptor textureDesc {
        .label = {"Texture", WGPU_STRLEN},
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment,
        .dimension = WGPUTextureDimension_2D,
        .size = {width, height, 1},
        .format = WGPUTextureFormat_BGRA8Unorm,
        .mipLevelCount = 1,
        .sampleCount = 1,
    };
    texture = wgpuDeviceCreateTexture(device, &textureDesc);

    const WGPUTexelCopyTextureInfo destination {
        .texture = texture,
        .mipLevel = 0,
        .origin = {0, 0, 0},
        .aspect = WGPUTextureAspect_All,
    };
    const WGPUTexelCopyBufferLayout dataLayout {
        .offset = 0,
        .bytesPerRow = static_cast<uint32_t>(surface->pitch),
        .rowsPerImage = height,
    };
    const WGPUExtent3D writeSize {
        .width = width,
        .height = height,
        .depthOrArrayLayers = 1,
    };
    wgpuQueueWriteTexture(queue, &destination, surface->pixels, surface->pitch * height, &dataLayout, &writeSize);

    view = wgpuTextureCreateView(texture, nullptr);

    return true;
}

WGPUTexture EngineTexture::getTexture() const
{
    return texture;
}

WGPUTextureView EngineTexture::getTextureView() const
{
    return view;
}

// EngineTexture * EngineTexture::FromColour(const WGPUDevice device, const WGPUQueue queue, const glm::vec4 colour) {
//     return new EngineTexture(device, queue, colour);
// }
//
// EngineTexture * EngineTexture::FromColour(const WGPUDevice device, const WGPUQueue queue, const int r, const int g, const int b, const int a) {
//     return new EngineTexture(device, queue, r, g, b, a);
// }
//
// WGPUTextureView EngineTexture::CreateView() {
//     view = wgpuTextureCreateView(texture, nullptr);
//     return view;
// }
