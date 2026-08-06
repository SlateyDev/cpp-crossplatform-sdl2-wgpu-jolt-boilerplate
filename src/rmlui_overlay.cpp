#include "rmlui_overlay.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Debugger.h>

#include <SDL_image.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

WGPUStringView ToWgpuString(const char *text)
{
    WGPUStringView view = WGPU_STRING_VIEW_INIT;
    view.data = text;
    view.length = WGPU_STRLEN;
    return view;
}

std::string EscapeRmlText(const std::string &value)
{
    std::string escaped;
    escaped.reserve(value.size() + 16);
    for (const char ch : value) {
        switch (ch) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

int ToRmlKeyModifiers(const Uint16 sdlModifiers)
{
    int modifiers = 0;
    if ((sdlModifiers & KMOD_CTRL) != 0u) {
        modifiers |= Rml::Input::KM_CTRL;
    }
    if ((sdlModifiers & KMOD_SHIFT) != 0u) {
        modifiers |= Rml::Input::KM_SHIFT;
    }
    if ((sdlModifiers & KMOD_ALT) != 0u) {
        modifiers |= Rml::Input::KM_ALT;
    }
    if ((sdlModifiers & KMOD_GUI) != 0u) {
        modifiers |= Rml::Input::KM_META;
    }
    if ((sdlModifiers & KMOD_CAPS) != 0u) {
        modifiers |= Rml::Input::KM_CAPSLOCK;
    }
    if ((sdlModifiers & KMOD_NUM) != 0u) {
        modifiers |= Rml::Input::KM_NUMLOCK;
    }
    return modifiers;
}

int ToRmlMouseButtonIndex(const Uint8 sdlButton)
{
    switch (sdlButton) {
        case SDL_BUTTON_LEFT: return 0;
        case SDL_BUTTON_RIGHT: return 1;
        case SDL_BUTTON_MIDDLE: return 2;
        default: return -1;
    }
}

bool LoadBinaryFile(const std::string &path, std::vector<uint8_t> &outData)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    const auto endPos = file.tellg();
    if (endPos <= 0) {
        return false;
    }
    const size_t size = static_cast<size_t>(endPos);
    outData.resize(size);
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char *>(outData.data()), static_cast<std::streamsize>(size));
    return file.good();
}

class OverlaySystemInterface final : public Rml::SystemInterface {
public:
    bool LogMessage(const Rml::Log::Type type, const Rml::String &message) override
    {
        const char *level = "info";
        if (type == Rml::Log::LT_WARNING) {
            level = "warn";
        } else if (type == Rml::Log::LT_ERROR) {
            level = "error";
        } else if (type == Rml::Log::LT_ASSERT) {
            level = "assert";
        }
        std::cerr << "[RmlUi][" << level << "] " << message << '\n';
        return true;
    }
};

class OverlayRenderInterface final : public Rml::RenderInterface {
public:
    bool Initialize(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat surfaceFormat, uint32_t width, uint32_t height)
    {
        device_ = device;
        queue_ = queue;
        surfaceFormat_ = surfaceFormat;
        width_ = std::max<uint32_t>(1, width);
        height_ = std::max<uint32_t>(1, height);

        WGPUShaderSourceWGSL wgslSource {
            .chain = {
                .sType = WGPUSType_ShaderSourceWGSL,
            },
            .code = ToWgpuString(R"(
struct VertexInput {
    @location(0) position : vec2f,
    @location(1) uv : vec2f,
    @location(2) color : vec4f,
}

struct VertexOutput {
    @builtin(position) position : vec4f,
    @location(0) uv : vec2f,
    @location(1) color : vec4f,
}

@vertex
fn vs_main(input : VertexInput) -> VertexOutput {
    var out : VertexOutput;
    out.position = vec4f(input.position, 0.0, 1.0);
    out.uv = input.uv;
    out.color = input.color;
    return out;
}

@group(0) @binding(0) var uSampler : sampler;
@group(0) @binding(1) var uTexture : texture_2d<f32>;

@fragment
fn fs_main(input : VertexOutput) -> @location(0) vec4f {
    return textureSample(uTexture, uSampler, input.uv) * input.color;
}
            )"),
        };
        const WGPUShaderModuleDescriptor shaderDesc{
            .nextInChain = &wgslSource.chain,
        };
        shaderModule_ = wgpuDeviceCreateShaderModule(device_, &shaderDesc);
        if (!shaderModule_) {
            return false;
        }

        constexpr auto samplerLayoutEntry = WGPUBindGroupLayoutEntry{
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = {
                .type = WGPUSamplerBindingType_Filtering,
            },
        };
        constexpr auto textureLayoutEntry = WGPUBindGroupLayoutEntry{
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false,
            },
        };
        constexpr auto bindGroupEntries = std::to_array<WGPUBindGroupLayoutEntry>({
            samplerLayoutEntry,
            textureLayoutEntry,
        });
        const WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc{
            .label = ToWgpuString("RmlUi Texture BindGroupLayout"),
            .entryCount = static_cast<uint32_t>(bindGroupEntries.size()),
            .entries = bindGroupEntries.data(),
        };
        bindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bindGroupLayoutDesc);
        if (!bindGroupLayout_) {
            return false;
        }

        const WGPUSamplerDescriptor samplerDesc{
            .label = ToWgpuString("RmlUi Sampler"),
            .addressModeU = WGPUAddressMode_ClampToEdge,
            .addressModeV = WGPUAddressMode_ClampToEdge,
            .addressModeW = WGPUAddressMode_ClampToEdge,
            .magFilter = WGPUFilterMode_Linear,
            .minFilter = WGPUFilterMode_Linear,
            .mipmapFilter = WGPUMipmapFilterMode_Linear,
            .maxAnisotropy = 1,
        };
        sampler_ = wgpuDeviceCreateSampler(device_, &samplerDesc);
        if (!sampler_) {
            return false;
        }

        const WGPUPipelineLayoutDescriptor pipelineLayoutDesc{
            .label = ToWgpuString("RmlUi Pipeline Layout"),
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &bindGroupLayout_,
        };
        pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDesc);
        if (!pipelineLayout_) {
            return false;
        }

        constexpr auto blendState = WGPUBlendState{
            .color = WGPUBlendComponent{
                .operation = WGPUBlendOperation_Add,
                .srcFactor = WGPUBlendFactor_One,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
            },
            .alpha = WGPUBlendComponent{
                .operation = WGPUBlendOperation_Add,
                .srcFactor = WGPUBlendFactor_One,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
            },
        };
        const WGPUColorTargetState colorTarget {
            .format = surfaceFormat_,
            .blend = &blendState,
            .writeMask = WGPUColorWriteMask_All,
        };
        const WGPUFragmentState fragmentState{
            .module = shaderModule_,
            .entryPoint = ToWgpuString("fs_main"),
            .targetCount = 1,
            .targets = &colorTarget,
        };
        constexpr auto attributes = std::to_array<WGPUVertexAttribute>({
            WGPUVertexAttribute{
                .format = WGPUVertexFormat_Float32x2,
                .offset = offsetof(UiVertex, position),
                .shaderLocation = 0,
            },
            WGPUVertexAttribute{
                .format = WGPUVertexFormat_Float32x2,
                .offset = offsetof(UiVertex, uv),
                .shaderLocation = 1,
            },
            WGPUVertexAttribute{
                .format = WGPUVertexFormat_Float32x4,
                .offset = offsetof(UiVertex, color),
                .shaderLocation = 2,
            },
        });
        const WGPUVertexBufferLayout vertexLayout{
            .stepMode = WGPUVertexStepMode_Vertex,
            .arrayStride = sizeof(UiVertex),
            .attributeCount = static_cast<uint32_t>(attributes.size()),
            .attributes = attributes.data(),
        };

        const WGPURenderPipelineDescriptor pipelineDesc{
            .label = ToWgpuString("RmlUi Render Pipeline"),
            .layout = pipelineLayout_,
            .vertex = WGPUVertexState{
                .module = shaderModule_,
                .entryPoint = ToWgpuString("vs_main"),
                .bufferCount = 1,
                .buffers = &vertexLayout,
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
        pipeline_ = wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
        if (!pipeline_) {
            return false;
        }

        constexpr std::array<uint8_t, 4> whitePixel = {255, 255, 255, 255};
        whiteTextureHandle_ = CreateTextureFromRgba(whitePixel.data(), 1, 1);
        return whiteTextureHandle_ != 0;
    }

    void Shutdown()
    {
        drawCommands_.clear();
        pendingGeometryReleases_.clear();
        pendingTextureReleases_.clear();
        collectingFrame_ = false;
        geometries_.clear();
        for (auto &[_, texture] : textures_) {
            ReleaseTextureResource(texture.get());
        }
        textures_.clear();

        if (indexBuffer_) {
            wgpuBufferRelease(indexBuffer_);
            indexBuffer_ = nullptr;
            indexCapacity_ = 0;
        }
        if (vertexBuffer_) {
            wgpuBufferRelease(vertexBuffer_);
            vertexBuffer_ = nullptr;
            vertexCapacity_ = 0;
        }
        if (pipeline_) {
            wgpuRenderPipelineRelease(pipeline_);
            pipeline_ = nullptr;
        }
        if (pipelineLayout_) {
            wgpuPipelineLayoutRelease(pipelineLayout_);
            pipelineLayout_ = nullptr;
        }
        if (sampler_) {
            wgpuSamplerRelease(sampler_);
            sampler_ = nullptr;
        }
        if (bindGroupLayout_) {
            wgpuBindGroupLayoutRelease(bindGroupLayout_);
            bindGroupLayout_ = nullptr;
        }
        if (shaderModule_) {
            wgpuShaderModuleRelease(shaderModule_);
            shaderModule_ = nullptr;
        }
        whiteTextureHandle_ = 0;
    }

    void SetDimensions(const uint32_t width, const uint32_t height)
    {
        width_ = std::max<uint32_t>(1, width);
        height_ = std::max<uint32_t>(1, height);
    }

    void BeginFrame()
    {
        drawCommands_.clear();
        pendingGeometryReleases_.clear();
        pendingTextureReleases_.clear();
        collectingFrame_ = true;
    }

    void Render(const WGPURenderPassEncoder pass) const
    {
        if (!pass || !pipeline_ || drawCommands_.empty()) {
            FlushDeferredReleases();
            return;
        }

        std::vector<UiVertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<DrawBatch> batches;

        vertices.reserve(8192);
        indices.reserve(16384);
        batches.reserve(drawCommands_.size());

        for (const DrawCommand &command : drawCommands_) {
            const auto geometryIt = geometries_.find(command.geometry);
            if (geometryIt == geometries_.end()) {
                continue;
            }
            const GeometryData &geometry = *geometryIt->second;
            if (geometry.indices.empty()) {
                continue;
            }

            const uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
            const uint32_t firstIndex = static_cast<uint32_t>(indices.size());
            for (const Rml::Vertex &vertex : geometry.vertices) {
                const float x = vertex.position.x + command.translation.x;
                const float y = vertex.position.y + command.translation.y;
                const float ndcX = (x / static_cast<float>(width_)) * 2.0f - 1.0f;
                const float ndcY = 1.0f - (y / static_cast<float>(height_)) * 2.0f;

                constexpr float colorScale = 1.0f / 255.0f;
                vertices.push_back(UiVertex{
                    .position = {ndcX, ndcY},
                    .uv = {vertex.tex_coord.x, vertex.tex_coord.y},
                    .color = {
                        static_cast<float>(vertex.colour.red) * colorScale,
                        static_cast<float>(vertex.colour.green) * colorScale,
                        static_cast<float>(vertex.colour.blue) * colorScale,
                        static_cast<float>(vertex.colour.alpha) * colorScale,
                    },
                });
            }

            for (int index : geometry.indices) {
                indices.push_back(baseVertex + static_cast<uint32_t>(std::max(0, index)));
            }

            batches.push_back(DrawBatch{
                .firstIndex = firstIndex,
                .indexCount = static_cast<uint32_t>(geometry.indices.size()),
                .texture = command.texture == 0 ? whiteTextureHandle_ : command.texture,
                .scissorEnabled = command.scissorEnabled,
                .scissorRegion = command.scissorRegion,
            });
        }

        if (vertices.empty() || indices.empty() || batches.empty()) {
            return;
        }

        if (!EnsureBuffers(vertices.size(), indices.size())) {
            return;
        }

        const auto vertexBytes = vertices.size() * sizeof(UiVertex);
        const auto indexBytes = indices.size() * sizeof(uint32_t);
        wgpuQueueWriteBuffer(queue_, vertexBuffer_, 0, vertices.data(), vertexBytes);
        wgpuQueueWriteBuffer(queue_, indexBuffer_, 0, indices.data(), indexBytes);

        wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer_, 0, vertexBytes);
        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer_, WGPUIndexFormat_Uint32, 0, indexBytes);

        for (const DrawBatch &batch : batches) {
            const auto textureIt = textures_.find(batch.texture);
            if (textureIt == textures_.end() || !textureIt->second || !textureIt->second->bindGroup) {
                continue;
            }

            constexpr bool kUseRmlScissor = false;
            if (kUseRmlScissor && batch.scissorEnabled) {
                const int left = std::clamp(batch.scissorRegion.Left(), 0, static_cast<int>(width_));
                const int top = std::clamp(batch.scissorRegion.Top(), 0, static_cast<int>(height_));
                const int right = std::clamp(batch.scissorRegion.Right(), 0, static_cast<int>(width_));
                const int bottom = std::clamp(batch.scissorRegion.Bottom(), 0, static_cast<int>(height_));
                const uint32_t width = static_cast<uint32_t>(std::max(0, right - left));
                const uint32_t height = static_cast<uint32_t>(std::max(0, bottom - top));
                if (width == 0 || height == 0) {
                    continue;
                }
                wgpuRenderPassEncoderSetScissorRect(pass, static_cast<uint32_t>(left), static_cast<uint32_t>(top), width, height);
            } else {
                wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, width_, height_);
            }

            wgpuRenderPassEncoderSetBindGroup(pass, 0, textureIt->second->bindGroup, 0, nullptr);
            wgpuRenderPassEncoderDrawIndexed(pass, batch.indexCount, 1, batch.firstIndex, 0, 0);
        }
        FlushDeferredReleases();
    }

    Rml::CompiledGeometryHandle CompileGeometry(const Rml::Span<const Rml::Vertex> vertices, const Rml::Span<const int> indices) override
    {
        const auto handle = static_cast<Rml::CompiledGeometryHandle>(nextGeometryHandle_++);
        auto data = std::make_unique<GeometryData>();
        data->vertices.assign(vertices.begin(), vertices.end());
        data->indices.assign(indices.begin(), indices.end());
        geometries_.emplace(handle, std::move(data));
        return handle;
    }

    void RenderGeometry(const Rml::CompiledGeometryHandle geometry, const Rml::Vector2f translation, const Rml::TextureHandle texture) override
    {
        drawCommands_.push_back(DrawCommand{
            .geometry = geometry,
            .translation = translation,
            .texture = texture,
            .scissorEnabled = scissorEnabled_,
            .scissorRegion = scissorRegion_,
        });
    }

    void ReleaseGeometry(const Rml::CompiledGeometryHandle geometry) override
    {
        if (collectingFrame_) {
            pendingGeometryReleases_.push_back(geometry);
        } else {
            geometries_.erase(geometry);
        }
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i &textureDimensions, const Rml::String &source) override
    {
        SDL_Surface *loadedSurface = IMG_Load(source.c_str());
        if (!loadedSurface) {
            return 0;
        }
        SDL_Surface *convertedSurface = SDL_ConvertSurfaceFormat(loadedSurface, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(loadedSurface);
        if (!convertedSurface) {
            return 0;
        }

        const auto width = static_cast<uint32_t>(std::max(1, convertedSurface->w));
        const auto height = static_cast<uint32_t>(std::max(1, convertedSurface->h));
        textureDimensions = Rml::Vector2i(static_cast<int>(width), static_cast<int>(height));
        const auto handle = CreateTextureFromRgba(static_cast<const uint8_t *>(convertedSurface->pixels), width, height);
        SDL_FreeSurface(convertedSurface);
        return handle;
    }

    Rml::TextureHandle GenerateTexture(const Rml::Span<const Rml::byte> source, const Rml::Vector2i sourceDimensions) override
    {
        const auto width = static_cast<uint32_t>(std::max(1, sourceDimensions.x));
        const auto height = static_cast<uint32_t>(std::max(1, sourceDimensions.y));
        return CreateTextureFromRgba(source.data(), width, height);
    }

    void ReleaseTexture(const Rml::TextureHandle texture) override
    {
        if (collectingFrame_) {
            pendingTextureReleases_.push_back(texture);
            return;
        }
        const auto it = textures_.find(texture);
        if (it == textures_.end()) {
            return;
        }
        ReleaseTextureResource(it->second.get());
        textures_.erase(it);
    }

    void EnableScissorRegion(const bool enable) override
    {
        scissorEnabled_ = enable;
    }

    void SetScissorRegion(const Rml::Rectanglei region) override
    {
        scissorRegion_ = region;
    }

private:
    struct UiVertex {
        std::array<float, 2> position{};
        std::array<float, 2> uv{};
        std::array<float, 4> color{};
    };

    struct GeometryData {
        std::vector<Rml::Vertex> vertices;
        std::vector<int> indices;
    };

    struct TextureData {
        WGPUTexture texture = nullptr;
        WGPUTextureView view = nullptr;
        WGPUBindGroup bindGroup = nullptr;
    };

    struct DrawCommand {
        Rml::CompiledGeometryHandle geometry = 0;
        Rml::Vector2f translation = {0.0f, 0.0f};
        Rml::TextureHandle texture = 0;
        bool scissorEnabled = false;
        Rml::Rectanglei scissorRegion{};
    };

    struct DrawBatch {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        Rml::TextureHandle texture = 0;
        bool scissorEnabled = false;
        Rml::Rectanglei scissorRegion{};
    };

    bool EnsureBuffers(const size_t vertexCount, const size_t indexCount) const
    {
        if (vertexCount == 0 || indexCount == 0) {
            return true;
        }

        const auto vertexByteCount = static_cast<uint64_t>(vertexCount * sizeof(UiVertex));
        if (!vertexBuffer_ || vertexCapacity_ < vertexByteCount) {
            if (vertexBuffer_) {
                wgpuBufferRelease(vertexBuffer_);
                vertexBuffer_ = nullptr;
            }
            vertexCapacity_ = std::max<uint64_t>(vertexByteCount, 64u * 1024u);
            const WGPUBufferDescriptor vertexBufferDesc{
                .label = ToWgpuString("RmlUi Vertex Buffer"),
                .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                .size = vertexCapacity_,
            };
            vertexBuffer_ = wgpuDeviceCreateBuffer(device_, &vertexBufferDesc);
            if (!vertexBuffer_) {
                return false;
            }
        }

        const auto indexByteCount = static_cast<uint64_t>(indexCount * sizeof(uint32_t));
        if (!indexBuffer_ || indexCapacity_ < indexByteCount) {
            if (indexBuffer_) {
                wgpuBufferRelease(indexBuffer_);
                indexBuffer_ = nullptr;
            }
            indexCapacity_ = std::max<uint64_t>(indexByteCount, 64u * 1024u);
            const WGPUBufferDescriptor indexBufferDesc{
                .label = ToWgpuString("RmlUi Index Buffer"),
                .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
                .size = indexCapacity_,
            };
            indexBuffer_ = wgpuDeviceCreateBuffer(device_, &indexBufferDesc);
            if (!indexBuffer_) {
                return false;
            }
        }

        return true;
    }

    Rml::TextureHandle CreateTextureFromRgba(const uint8_t *pixels, const uint32_t width, const uint32_t height)
    {
        if (!pixels || width == 0 || height == 0) {
            return 0;
        }

        auto textureData = std::make_unique<TextureData>();
        const WGPUTextureDescriptor textureDesc{
            .label = ToWgpuString("RmlUi Texture"),
            .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            .dimension = WGPUTextureDimension_2D,
            .size = {width, height, 1},
            .format = WGPUTextureFormat_RGBA8Unorm,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };
        textureData->texture = wgpuDeviceCreateTexture(device_, &textureDesc);
        if (!textureData->texture) {
            return 0;
        }

        const WGPUTexelCopyTextureInfo destination{
            .texture = textureData->texture,
            .mipLevel = 0,
            .origin = {0, 0, 0},
            .aspect = WGPUTextureAspect_All,
        };
        const uint32_t unpaddedBytesPerRow = width * 4;
        const uint32_t alignedBytesPerRow = (unpaddedBytesPerRow + 255u) & ~255u;
        std::vector<uint8_t> uploadBuffer;
        const uint8_t *uploadData = pixels;
        size_t uploadSize = static_cast<size_t>(unpaddedBytesPerRow) * static_cast<size_t>(height);
        if (alignedBytesPerRow != unpaddedBytesPerRow) {
            uploadBuffer.resize(static_cast<size_t>(alignedBytesPerRow) * static_cast<size_t>(height), 0);
            for (uint32_t row = 0; row < height; ++row) {
                const auto *src = pixels + static_cast<size_t>(row) * static_cast<size_t>(unpaddedBytesPerRow);
                auto *dst = uploadBuffer.data() + static_cast<size_t>(row) * static_cast<size_t>(alignedBytesPerRow);
                std::memcpy(dst, src, unpaddedBytesPerRow);
            }
            uploadData = uploadBuffer.data();
            uploadSize = uploadBuffer.size();
        }
        const WGPUTexelCopyBufferLayout dataLayout{
            .offset = 0,
            .bytesPerRow = alignedBytesPerRow,
            .rowsPerImage = height,
        };
        const WGPUExtent3D writeSize{
            .width = width,
            .height = height,
            .depthOrArrayLayers = 1,
        };
        wgpuQueueWriteTexture(queue_, &destination, uploadData, uploadSize, &dataLayout, &writeSize);

        textureData->view = wgpuTextureCreateView(textureData->texture, nullptr);
        if (!textureData->view) {
            ReleaseTextureResource(textureData.get());
            return 0;
        }

        const auto bindEntries = std::to_array<WGPUBindGroupEntry>({
            WGPUBindGroupEntry{
                .binding = 0,
                .sampler = sampler_,
            },
            WGPUBindGroupEntry{
                .binding = 1,
                .textureView = textureData->view,
            },
        });
        const WGPUBindGroupDescriptor bindGroupDesc{
            .label = ToWgpuString("RmlUi Texture BindGroup"),
            .layout = bindGroupLayout_,
            .entryCount = static_cast<uint32_t>(bindEntries.size()),
            .entries = bindEntries.data(),
        };
        textureData->bindGroup = wgpuDeviceCreateBindGroup(device_, &bindGroupDesc);
        if (!textureData->bindGroup) {
            ReleaseTextureResource(textureData.get());
            return 0;
        }

        const auto handle = static_cast<Rml::TextureHandle>(nextTextureHandle_++);
        textures_.emplace(handle, std::move(textureData));
        return handle;
    }

    void FlushDeferredReleases() const
    {
        for (const auto handle : pendingGeometryReleases_) {
            geometries_.erase(handle);
        }
        pendingGeometryReleases_.clear();

        for (const auto handle : pendingTextureReleases_) {
            const auto it = textures_.find(handle);
            if (it == textures_.end()) {
                continue;
            }
            ReleaseTextureResource(it->second.get());
            textures_.erase(it);
        }
        pendingTextureReleases_.clear();
        collectingFrame_ = false;
    }

    static void ReleaseTextureResource(TextureData *textureData)
    {
        if (!textureData) {
            return;
        }

        if (textureData->bindGroup) {
            wgpuBindGroupRelease(textureData->bindGroup);
            textureData->bindGroup = nullptr;
        }
        if (textureData->view) {
            wgpuTextureViewRelease(textureData->view);
            textureData->view = nullptr;
        }
        if (textureData->texture) {
            wgpuTextureRelease(textureData->texture);
            textureData->texture = nullptr;
        }
    }

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUTextureFormat surfaceFormat_ = WGPUTextureFormat_Undefined;
    uint32_t width_ = 1;
    uint32_t height_ = 1;

    WGPUShaderModule shaderModule_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPURenderPipeline pipeline_ = nullptr;
    mutable WGPUBuffer vertexBuffer_ = nullptr;
    mutable WGPUBuffer indexBuffer_ = nullptr;
    mutable uint64_t vertexCapacity_ = 0;
    mutable uint64_t indexCapacity_ = 0;

    bool scissorEnabled_ = false;
    Rml::Rectanglei scissorRegion_{};

    uint64_t nextGeometryHandle_ = 1;
    uint64_t nextTextureHandle_ = 1;
    Rml::TextureHandle whiteTextureHandle_ = 0;
    mutable std::unordered_map<Rml::CompiledGeometryHandle, std::unique_ptr<GeometryData>> geometries_;
    mutable std::unordered_map<Rml::TextureHandle, std::unique_ptr<TextureData>> textures_;
    std::vector<DrawCommand> drawCommands_;
    mutable std::vector<Rml::CompiledGeometryHandle> pendingGeometryReleases_;
    mutable std::vector<Rml::TextureHandle> pendingTextureReleases_;
    mutable bool collectingFrame_ = false;
};

constexpr auto kOverlayDocument = R"(
<rml>
<head>
<style>
body { margin: 0; color: #d6e3ff; font-family: OverlayDefault; font-size: 18px; }
#root { width: 100px; height: 100px; }
#fps { position: absolute; top: 8px; left: 8px; width: 100px; text-align: left; white-space: nowrap; color: #e5ef3b; }
#status { position: absolute; left: 8px; top: 8px; width: 60%; white-space: nowrap; }
#status-hover { display: block; }
#status-character { color: #f4c7c7; display: block; }
#debug { position: absolute; left: 8px; top: 8px; white-space: pre; color: #d0dbe8; }
#console {
    position: absolute;
    left: 8px;
    width: 100px;
    top: 8px;
    height: 100px;
    background-color: rgba(8, 8, 18, 210);
    border: 1px #4a5473;
    padding: 8px;
}
#console.hidden { display: none; }
#console-title { color: #f0f4ff; margin-bottom: 6px; }
#console-lines { white-space: pre; color: #c8d8ea; height: 80%; overflow-y: scroll; overflow-x: hidden; }
#console-lines scrollbarvertical { width: 12px; }
#console-lines scrollbarvertical slidertrack { background-color: rgba(60, 72, 98, 120); }
#console-lines scrollbarvertical sliderbar { min-height: 24px; background-color: rgba(160, 182, 214, 180); }
#console-lines scrollbarvertical sliderbar:hover { background-color: rgba(188, 206, 234, 220); }
#console-input { color: #ffffff; margin-top: 6px; }
</style>
</head>
<body>
<div id="root">
  <div id="fps"></div>
  <div id="status">
    <div id="status-hover"></div>
    <div id="status-character"></div>
  </div>
  <div id="debug"></div>
  <div id="console" class="hidden">
    <div id="console-title">Console</div>
    <div id="console-lines"></div>
    <div id="console-input"></div>
  </div>
</div>
</body>
</rml>
)";

std::string JoinVisibleLines(const std::deque<std::string> &lines, const size_t maxLines)
{
    if (lines.empty() || maxLines == 0) {
        return {};
    }

    const size_t start = lines.size() > maxLines ? (lines.size() - maxLines) : 0;
    std::ostringstream stream;
    bool first = true;
    for (size_t i = start; i < lines.size(); ++i) {
        if (!first) {
            stream << "<br/>";
        }
        stream << EscapeRmlText(lines[i]);
        first = false;
    }
    return stream.str();
}

} // namespace

struct RmlUiOverlay::Impl {
    OverlaySystemInterface systemInterface;
    OverlayRenderInterface renderInterface;
    Rml::Context *context = nullptr;
    Rml::ElementDocument *document = nullptr;

    float fps = 0.0f;
    std::string hoverText = "Hover: None";
    std::string characterText = "Character: Airborne";
    std::deque<std::string> debugMessages;
    std::deque<std::string> consoleLines;
    std::string renderedConsoleLinesRml;
    bool pendingConsoleAutoScroll = false;
    bool consoleOpen = false;
    std::string consoleInput;
    uint32_t width = 1;
    uint32_t height = 1;
    std::vector<std::vector<uint8_t>> fontBlobs;
};

bool RmlUiOverlay::Initialize(const WGPUDevice device, const WGPUQueue queue, const WGPUTextureFormat surfaceFormat, const uint32_t width, const uint32_t height)
{
    if (impl != nullptr) {
        return true;
    }

    auto state = std::make_unique<Impl>();
    state->width = std::max<uint32_t>(1, width);
    state->height = std::max<uint32_t>(1, height);
    if (!state->renderInterface.Initialize(device, queue, surfaceFormat, state->width, state->height)) {
        return false;
    }

    Rml::SetSystemInterface(&state->systemInterface);
    Rml::SetRenderInterface(&state->renderInterface);
    if (!Rml::Initialise()) {
        state->renderInterface.Shutdown();
        return false;
    }

    auto loadOverlayFont = [&](const std::string &path) -> bool {
        std::vector<uint8_t> blob;
        if (!LoadBinaryFile(path, blob)) {
            return false;
        }
        state->fontBlobs.push_back(std::move(blob));
        const auto &fontData = state->fontBlobs.back();
        const auto span = Rml::Span<const Rml::byte>(
            reinterpret_cast<const Rml::byte *>(fontData.data()),
            fontData.size()
        );
        if (!Rml::LoadFontFace(span, "OverlayDefault", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto, true)) {
            state->fontBlobs.pop_back();
            return false;
        }
        return true;
    };

    const bool loadedRoboto = loadOverlayFont("assets/fonts/Roboto-Regular.ttf");
    std::cerr << "[RmlUi] Overlay font registration - Roboto: " << loadedRoboto
              << std::endl;

    state->context = Rml::CreateContext("overlay", Rml::Vector2i(static_cast<int>(state->width), static_cast<int>(state->height)));
    if (state->context == nullptr) {
        Rml::Shutdown();
        state->renderInterface.Shutdown();
        return false;
    }

    state->document = state->context->LoadDocumentFromMemory(kOverlayDocument);
    if (state->document == nullptr) {
        Rml::RemoveContext("overlay");
        Rml::Shutdown();
        state->renderInterface.Shutdown();
        return false;
    }
    state->document->Show();

    Rml::Debugger::Initialise(state->context);
    Rml::Debugger::SetVisible(false);

    impl = state.release();
    return true;
}

void RmlUiOverlay::Shutdown()
{
    if (impl == nullptr) {
        return;
    }

    Rml::Debugger::Shutdown();
    if (impl->document != nullptr) {
        impl->document->Close();
        impl->document = nullptr;
    }
    if (impl->context != nullptr) {
        Rml::RemoveContext("overlay");
        impl->context = nullptr;
    }

    Rml::Shutdown();
    impl->renderInterface.Shutdown();
    delete impl;
    impl = nullptr;
}

void RmlUiOverlay::SetDimensions(const uint32_t width, const uint32_t height) const
{
    if (impl == nullptr) {
        return;
    }
    impl->width = std::max<uint32_t>(1, width);
    impl->height = std::max<uint32_t>(1, height);
    impl->renderInterface.SetDimensions(impl->width, impl->height);
    if (impl->context != nullptr) {
        impl->context->SetDimensions(Rml::Vector2i(static_cast<int>(impl->width), static_cast<int>(impl->height)));
    }
}

void RmlUiOverlay::SetData(
    const float fps,
    const std::string &hoverText,
    const std::string &characterText,
    const std::deque<std::string> &debugMessages,
    const std::deque<std::string> &consoleLines,
    const bool consoleOpen,
    const std::string &consoleInput
) const
{
    if (impl == nullptr) {
        return;
    }
    impl->fps = fps;
    impl->hoverText = hoverText;
    impl->characterText = characterText;
    impl->debugMessages = debugMessages;
    impl->consoleLines = consoleLines;
    impl->consoleOpen = consoleOpen;
    impl->consoleInput = consoleInput;
}

void RmlUiOverlay::PrepareFrame() const
{
    if (impl == nullptr || impl->context == nullptr || impl->document == nullptr) {
        return;
    }

    auto *fpsElement = impl->document->GetElementById("fps");
    auto *rootElement = impl->document->GetElementById("root");
    auto *hoverElement = impl->document->GetElementById("status-hover");
    auto *characterElement = impl->document->GetElementById("status-character");
    auto *debugElement = impl->document->GetElementById("debug");
    auto *consoleElement = impl->document->GetElementById("console");
    auto *consoleLinesElement = impl->document->GetElementById("console-lines");
    auto *consoleInputElement = impl->document->GetElementById("console-input");

    std::ostringstream fpsStream;
    fpsStream.setf(std::ios::fixed);
    fpsStream.precision(1);
    fpsStream << "FPS: " << impl->fps;
    if (fpsElement != nullptr) {
        const size_t fpsChars = fpsStream.str().size();
        const int fpsApproxWidth = static_cast<int>(fpsChars * 11u + 8u);
        const int fpsLeft = std::max(8, static_cast<int>(impl->width) - fpsApproxWidth - 8);
        fpsElement->SetProperty("left", std::to_string(fpsLeft) + "px");
        fpsElement->SetProperty("top", "8px");
        fpsElement->SetProperty("width", std::to_string(std::max(1, fpsApproxWidth)) + "px");
        fpsElement->SetProperty("text-align", "left");
        fpsElement->SetInnerRML(EscapeRmlText(fpsStream.str()));
    }
    if (hoverElement != nullptr) {
        hoverElement->SetInnerRML(EscapeRmlText(impl->hoverText));
    }
    if (characterElement != nullptr) {
        characterElement->SetInnerRML(EscapeRmlText(impl->characterText));
    }
    if (debugElement != nullptr) {
        constexpr int overlayMargin = 8;
        constexpr int debugLineHeight = 22;
        const int debugLineCount = static_cast<int>(std::min<size_t>(8, impl->debugMessages.size()));
        const int debugHeight = std::max(1, debugLineCount) * debugLineHeight;
        const int debugTop = std::max(overlayMargin, static_cast<int>(impl->height) - overlayMargin - debugHeight);
        debugElement->SetProperty("left", std::to_string(overlayMargin) + "px");
        debugElement->SetProperty("top", std::to_string(debugTop) + "px");
        debugElement->SetProperty("text-align", "left");
        debugElement->SetInnerRML(JoinVisibleLines(impl->debugMessages, 8));
    }
    if (consoleElement != nullptr) {
        const uint32_t panelWidth = impl->width > 32 ? (impl->width - 32) : impl->width;
        const uint32_t panelHeight = std::max<uint32_t>(120, static_cast<uint32_t>(static_cast<float>(impl->height) * 0.4f));
        consoleElement->SetProperty("left", "8px");
        consoleElement->SetProperty("top", "8px");
        consoleElement->SetProperty("width", std::to_string(panelWidth) + "px");
        consoleElement->SetProperty("height", std::to_string(panelHeight) + "px");
        if (impl->consoleOpen) {
            consoleElement->SetClass("hidden", false);
        } else {
            consoleElement->SetClass("hidden", true);
        }
    }
    if (rootElement != nullptr) {
        rootElement->SetProperty("width", std::to_string(impl->width) + "px");
        rootElement->SetProperty("height", std::to_string(impl->height) + "px");
        rootElement->SetClass("console-open", impl->consoleOpen);
    }
    if (consoleLinesElement != nullptr) {
        const std::string nextConsoleLinesRml = JoinVisibleLines(impl->consoleLines, impl->consoleLines.size());
        if (nextConsoleLinesRml != impl->renderedConsoleLinesRml) {
            constexpr float scrollBottomEpsilon = 1.0f;
            const float maxScrollTopBefore = std::max(0.0f, consoleLinesElement->GetScrollHeight() - consoleLinesElement->GetClientHeight());
            const bool shouldAutoScroll = maxScrollTopBefore <= scrollBottomEpsilon
                || consoleLinesElement->GetScrollTop() >= (maxScrollTopBefore - scrollBottomEpsilon);

            consoleLinesElement->SetInnerRML(nextConsoleLinesRml);
            impl->renderedConsoleLinesRml = std::move(nextConsoleLinesRml);
            impl->pendingConsoleAutoScroll = shouldAutoScroll;
        }
    }
    if (consoleInputElement != nullptr) {
        consoleInputElement->SetInnerRML(EscapeRmlText("Input: " + impl->consoleInput + "_"));
    }

    impl->renderInterface.BeginFrame();
    impl->context->Update();
    if (impl->pendingConsoleAutoScroll && consoleLinesElement != nullptr) {
        consoleLinesElement->SetScrollTop(consoleLinesElement->GetScrollHeight());
        impl->pendingConsoleAutoScroll = false;
    }
    impl->context->Render();
}

void RmlUiOverlay::Render(const WGPURenderPassEncoder pass) const
{
    if (impl == nullptr) {
        return;
    }
    impl->renderInterface.Render(pass);
}

bool RmlUiOverlay::ProcessEvent(const SDL_Event &event) const
{
    if (impl == nullptr || impl->context == nullptr) {
        return false;
    }

    switch (event.type) {
        case SDL_MOUSEMOTION: {
            const int modifiers = ToRmlKeyModifiers(SDL_GetModState());
            impl->context->ProcessMouseMove(event.motion.x, event.motion.y, modifiers);
            return true;
        }
        case SDL_MOUSEBUTTONDOWN: {
            const int buttonIndex = ToRmlMouseButtonIndex(event.button.button);
            if (buttonIndex < 0) {
                return false;
            }
            const int modifiers = ToRmlKeyModifiers(SDL_GetModState());
            impl->context->ProcessMouseMove(event.button.x, event.button.y, modifiers);
            impl->context->ProcessMouseButtonDown(buttonIndex, modifiers);
            return true;
        }
        case SDL_MOUSEBUTTONUP: {
            const int buttonIndex = ToRmlMouseButtonIndex(event.button.button);
            if (buttonIndex < 0) {
                return false;
            }
            const int modifiers = ToRmlKeyModifiers(SDL_GetModState());
            impl->context->ProcessMouseMove(event.button.x, event.button.y, modifiers);
            impl->context->ProcessMouseButtonUp(buttonIndex, modifiers);
            return true;
        }
        case SDL_MOUSEWHEEL: {
            int wheelX = event.wheel.x;
            int wheelY = event.wheel.y;
            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                wheelX = -wheelX;
                wheelY = -wheelY;
            }
            const int modifiers = ToRmlKeyModifiers(SDL_GetModState());
            impl->context->ProcessMouseWheel(Rml::Vector2f(static_cast<float>(wheelX), static_cast<float>(-wheelY)), modifiers);
            return true;
        }
        case SDL_WINDOWEVENT: {
            if (event.window.event == SDL_WINDOWEVENT_LEAVE) {
                impl->context->ProcessMouseLeave();
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}
