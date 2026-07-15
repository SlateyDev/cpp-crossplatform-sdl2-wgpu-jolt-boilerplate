#include "EngineTexture.hpp"

#include <iostream>

// #include <queue>

bool EngineTexture::LoadImage(std::string fileName) {
    this->fileName = fileName;

    SDL_Surface* surface = IMG_Load(("./assets/" + fileName).c_str());
    if (!surface) {
        std::cerr << "Failed to load image: " << fileName << " Error: " << IMG_GetError() << std::endl;
        return false;
    }
    std::cout << "Loaded texture: " << fileName << " - dimensions: " << surface->w << "x" << surface->h << std::endl;

    SDL_Surface* convertedSurface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_BGRA32, 0);
    SDL_FreeSurface(surface);

    if (!convertedSurface) {
        std::cerr << "Failed to convert surface format for: " << fileName << " Error: " << SDL_GetError() << std::endl;
        return false;
    }
    this->surface = convertedSurface;

    const auto width = static_cast<uint32_t>(convertedSurface->w);
    const auto height = static_cast<uint32_t>(convertedSurface->h);

    // const WGPUTextureDescriptor textureDesc {
    //     .label = {filepath.c_str(), WGPU_STRLEN},
    //     .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment,
    //     .dimension = WGPUTextureDimension_2D,
    //     .size = {width, height, 1},
    //     .format = WGPUTextureFormat_BGRA8Unorm,
    //     .mipLevelCount = 1,
    //     .sampleCount = 1,
    // };
    // WGPUTexture texture = wgpuDeviceCreateTexture(gpuState.device, &textureDesc);

    // const WGPUTexelCopyTextureInfo destination {
    //     .texture = texture,
    //     .mipLevel = 0,
    //     .origin = {0, 0, 0},
    //     .aspect = WGPUTextureAspect_All,
    // };
    // const WGPUTexelCopyBufferLayout dataLayout {
    //     .offset = 0,
    //     .bytesPerRow = static_cast<uint32_t>(convertedSurface->pitch),
    //     .rowsPerImage = height,
    // };
    // const WGPUExtent3D writeSize {
    //     .width = width,
    //     .height = height,
    //     .depthOrArrayLayers = 1,
    // };
    // wgpuQueueWriteTexture(gpuState.queue, &destination, convertedSurface->pixels, convertedSurface->pitch * height, &dataLayout, &writeSize);

    // SDL_FreeSurface(convertedSurface);

    // return {texture, wgpuTextureCreateView(texture, nullptr)};
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
    // if (texture) wgpuTextureRelease(texture);
    // texture = nullptr;
    // if (view) wgpuTextureViewRelease(view);
    // view = nullptr;
    if (this->surface) {
        SDL_FreeSurface(this->surface);
    }
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
