#include "gltf_loader.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <array>
#include <climits>
#include <cstdint>
#include <vector>

#include "structures.hpp"
#include "Engine/EngineTexture.hpp"

namespace {

constexpr const char *kFallbackMaterialKey = "__fallback__/base_color";
constexpr const char *kFallbackMetallicRoughnessKey = "__fallback__/metallic_roughness";
constexpr const char *kFallbackNormalKey = "__fallback__/normal";
constexpr const char *kFallbackEmissiveKey = "__fallback__/emissive";

const char *CgltfResultToString(const cgltf_result result)
{
    switch (result) {
    case cgltf_result_success:
        return "success";
    case cgltf_result_data_too_short:
        return "data too short";
    case cgltf_result_unknown_format:
        return "unknown format";
    case cgltf_result_invalid_json:
        return "invalid json";
    case cgltf_result_invalid_gltf:
        return "invalid gltf";
    case cgltf_result_invalid_options:
        return "invalid options";
    case cgltf_result_file_not_found:
        return "file not found";
    case cgltf_result_io_error:
        return "io error";
    case cgltf_result_out_of_memory:
        return "out of memory";
    case cgltf_result_legacy_gltf:
        return "legacy gltf";
    default:
        return "unknown error";
    }
}

const cgltf_accessor *FindAttribute(const cgltf_primitive &primitive, const cgltf_attribute_type type)
{
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const cgltf_attribute &attribute = primitive.attributes[i];
        if (attribute.type == type && attribute.data != nullptr) {
            return attribute.data;
        }
    }
    return nullptr;
}

bool ReadVec3(const cgltf_accessor *accessor, std::vector<glm::vec3> &output)
{
    if (!accessor || accessor->count == 0) {
        return false;
    }

    output.resize(static_cast<size_t>(accessor->count));
    std::vector<float> unpacked(static_cast<size_t>(accessor->count) * 3);
    const cgltf_size unpackedCount = cgltf_accessor_unpack_floats(accessor, unpacked.data(), unpacked.size());
    if (unpackedCount < accessor->count * 3) {
        return false;
    }

    for (cgltf_size i = 0; i < accessor->count; ++i) {
        const size_t base = static_cast<size_t>(i) * 3;
        output[static_cast<size_t>(i)] = glm::vec3(unpacked[base], unpacked[base + 1], unpacked[base + 2]);
    }

    return true;
}

bool ReadVec2(const cgltf_accessor *accessor, std::vector<glm::vec2> &output)
{
    if (!accessor || accessor->count == 0) {
        return false;
    }

    output.resize(static_cast<size_t>(accessor->count));
    std::vector<float> unpacked(static_cast<size_t>(accessor->count) * 2);
    const cgltf_size unpackedCount = cgltf_accessor_unpack_floats(accessor, unpacked.data(), unpacked.size());
    if (unpackedCount < accessor->count * 2) {
        return false;
    }

    for (cgltf_size i = 0; i < accessor->count; ++i) {
        const size_t base = static_cast<size_t>(i) * 2;
        output[static_cast<size_t>(i)] = glm::vec2(unpacked[base], unpacked[base + 1]);
    }

    return true;
}

bool ReadIndices(const cgltf_accessor *accessor, std::vector<int> &output)
{
    if (!accessor || accessor->count == 0) {
        return false;
    }

    output.resize(static_cast<size_t>(accessor->count));
    for (cgltf_size i = 0; i < accessor->count; ++i) {
        const cgltf_uint index = cgltf_accessor_read_index(accessor, i);
        if (index > static_cast<cgltf_uint>(INT_MAX)) {
            return false;
        }
        output[static_cast<size_t>(i)] = static_cast<int>(index);
    }

    return true;
}

std::string GetMaterialKey(const cgltf_material *material, const cgltf_data *data)
{
    if (material == nullptr) {
        return kFallbackMaterialKey;
    }
    if (material->name != nullptr && material->name[0] != '\0') {
        return material->name;
    }
    const auto materialIndex = static_cast<size_t>(material - data->materials);
    return "__gltf_material_" + std::to_string(materialIndex);
}

} // namespace

bool LoadGltfPrimitives(
    const GpuState &gpuState,
    AssetManager& assetManager,
    const std::string &gltfPath,
    std::unordered_map<std::string, UnlitMaterial> &materials,
    std::vector<Primitive> &outPrimitives,
    std::string &outError
)
{
    outPrimitives.clear();
    outError.clear();

    cgltf_options options{};
    cgltf_data *data = nullptr;

    cgltf_result result = cgltf_parse_file(&options, gltfPath.c_str(), &data);
    if (result != cgltf_result_success) {
        outError = std::string("Failed to parse glTF: ") + CgltfResultToString(result);
        return false;
    }

    result = cgltf_load_buffers(&options, data, gltfPath.c_str());
    if (result != cgltf_result_success) {
        outError = std::string("Failed to load glTF buffers: ") + CgltfResultToString(result);
        cgltf_free(data);
        return false;
    }

    std::vector<Primitive> parsedPrimitives;
    std::unordered_map<std::string, UnlitMaterial> parsedMaterials;
    const auto releaseParsedMaterials = [&parsedMaterials]() {
        for (auto &[_, parsedMaterial] : parsedMaterials) {
            if (parsedMaterial.bindGroup != nullptr) {
                wgpuBindGroupRelease(parsedMaterial.bindGroup);
                parsedMaterial.bindGroup = nullptr;
            }
            if (parsedMaterial.pbrParamsBuffer != nullptr) {
                wgpuBufferRelease(parsedMaterial.pbrParamsBuffer);
                parsedMaterial.pbrParamsBuffer = nullptr;
            }
        }
        parsedMaterials.clear();
    };

    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex) {
        const cgltf_mesh &mesh = data->meshes[meshIndex];

        for (cgltf_size primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex) {
            const cgltf_primitive &primitive = mesh.primitives[primitiveIndex];
            if (primitive.type != cgltf_primitive_type_triangles) {
                continue;
            }

            const cgltf_accessor *positionAccessor = FindAttribute(primitive, cgltf_attribute_type_position);
            if (!positionAccessor) {
                continue;
            }

            std::vector<glm::vec3> positions;
            if (!ReadVec3(positionAccessor, positions)) {
                outError = "Failed to read POSITION accessor.";
                cgltf_free(data);
                return false;
            }

            std::vector<glm::vec3> normals(positions.size(), glm::vec3(0.0f, 0.0f, 1.0f));
            const cgltf_accessor *normalAccessor = FindAttribute(primitive, cgltf_attribute_type_normal);
            if (normalAccessor) {
                std::vector<glm::vec3> loadedNormals;
                if (!ReadVec3(normalAccessor, loadedNormals) || loadedNormals.size() != positions.size()) {
                    outError = "Failed to read NORMAL accessor.";
                    cgltf_free(data);
                    return false;
                }
                normals = std::move(loadedNormals);
            }

            std::vector<glm::vec2> uvs(positions.size(), glm::vec2(0.0f));
            const cgltf_accessor *uvAccessor = FindAttribute(primitive, cgltf_attribute_type_texcoord);
            if (uvAccessor) {
                std::vector<glm::vec2> loadedUvs;
                if (!ReadVec2(uvAccessor, loadedUvs) || loadedUvs.size() != positions.size()) {
                    outError = "Failed to read TEXCOORD_0 accessor.";
                    cgltf_free(data);
                    return false;
                }
                uvs = std::move(loadedUvs);
            }

            std::vector<int> indices;
            if (primitive.indices) {
                if (!ReadIndices(primitive.indices, indices)) {
                    outError = "Failed to read indices accessor.";
                    cgltf_free(data);
                    return false;
                }
            } else {
                indices.resize(positions.size());
                for (size_t i = 0; i < positions.size(); ++i) {
                    if (i > static_cast<size_t>(INT_MAX)) {
                        outError = "Mesh is too large to index with int.";
                        cgltf_free(data);
                        return false;
                    }
                    indices[i] = static_cast<int>(i);
                }
            }

            std::vector<Vertex> vertices;
            vertices.resize(positions.size());
            for (size_t i = 0; i < positions.size(); ++i) {
                vertices[i] = Vertex{
                    .position = positions[i],
                    .uv = uvs[i],
                    .normal = normals[i],
                };
            }

            parsedPrimitives.push_back(Primitive::CreateFromPremadeData(
                gpuState.device,
                vertices,
                indices,
                GetMaterialKey(primitive.material, data)
            ));
        }
    }

    std::string fallbackTextureError;
    const auto *fallbackBaseColorTexture = assetManager.RequestSolidColorTexture(
        kFallbackMaterialKey, std::array<std::uint8_t, 4>{128u, 128u, 128u, 255u}, fallbackTextureError);
    if (fallbackBaseColorTexture == nullptr) {
        outError = fallbackTextureError.empty() ? "Failed to create fallback base color texture." : fallbackTextureError;
        cgltf_free(data);
        return false;
    }
    const auto *fallbackMetallicRoughnessTexture = assetManager.RequestSolidColorTexture(
        kFallbackMetallicRoughnessKey, std::array<std::uint8_t, 4>{0u, 255u, 0u, 255u}, fallbackTextureError);
    if (fallbackMetallicRoughnessTexture == nullptr) {
        outError = fallbackTextureError.empty() ? "Failed to create fallback metallic-roughness texture." : fallbackTextureError;
        cgltf_free(data);
        return false;
    }
    const auto *fallbackNormalTexture = assetManager.RequestSolidColorTexture(
        kFallbackNormalKey, std::array<std::uint8_t, 4>{128u, 128u, 255u, 255u}, fallbackTextureError);
    if (fallbackNormalTexture == nullptr) {
        outError = fallbackTextureError.empty() ? "Failed to create fallback normal texture." : fallbackTextureError;
        cgltf_free(data);
        return false;
    }
    const auto *fallbackEmissiveTexture = assetManager.RequestSolidColorTexture(
        kFallbackEmissiveKey, std::array<std::uint8_t, 4>{0u, 0u, 0u, 255u}, fallbackTextureError);
    if (fallbackEmissiveTexture == nullptr) {
        outError = fallbackTextureError.empty() ? "Failed to create fallback emissive texture." : fallbackTextureError;
        cgltf_free(data);
        return false;
    }

    for (cgltf_size materialIndex = 0; materialIndex < data->materials_count; ++materialIndex) {
        const auto &material = data->materials[materialIndex];
        WGPUTexture baseColorTexture = fallbackBaseColorTexture->getTexture();
        WGPUTextureView baseColorTextureView = fallbackBaseColorTexture->getTextureView();
        if (material.pbr_metallic_roughness.base_color_texture.texture != nullptr) {
            const auto *image = material.pbr_metallic_roughness.base_color_texture.texture->image;
            if (image == nullptr || image->uri == nullptr) {
                outError = "Material base color texture is missing image URI.";
                releaseParsedMaterials();
                cgltf_free(data);
                return false;
            }
            std::string textureLoadError;
            const auto *materialTexture = assetManager.RequestTexture(image->uri, textureLoadError);
            if (!materialTexture) {
                outError = textureLoadError.empty() ? "Failed to load material texture." : textureLoadError;
                releaseParsedMaterials();
                cgltf_free(data);
                return false;
            }
            baseColorTexture = materialTexture->getTexture();
            baseColorTextureView = materialTexture->getTextureView();
        }

        WGPUTexture metallicRoughnessTexture = fallbackMetallicRoughnessTexture->getTexture();
        WGPUTextureView metallicRoughnessTextureView = fallbackMetallicRoughnessTexture->getTextureView();
        bool hasMetallicRoughnessTexture = false;
        if (material.pbr_metallic_roughness.metallic_roughness_texture.texture != nullptr) {
            const auto *image = material.pbr_metallic_roughness.metallic_roughness_texture.texture->image;
            if (image == nullptr || image->uri == nullptr) {
                outError = "Material metallic-roughness texture is missing image URI.";
                releaseParsedMaterials();
                cgltf_free(data);
                return false;
            }
            std::string textureLoadError;
            const auto *materialTexture = assetManager.RequestTexture(image->uri, textureLoadError);
            if (!materialTexture) {
                outError = textureLoadError.empty() ? "Failed to load metallic-roughness texture." : textureLoadError;
                releaseParsedMaterials();
                cgltf_free(data);
                return false;
            }
            metallicRoughnessTexture = materialTexture->getTexture();
            metallicRoughnessTextureView = materialTexture->getTextureView();
            hasMetallicRoughnessTexture = true;
        }

        WGPUTexture normalTexture = fallbackNormalTexture->getTexture();
        WGPUTextureView normalTextureView = fallbackNormalTexture->getTextureView();
        bool hasNormalTexture = false;
        if (material.normal_texture.texture != nullptr) {
            const auto *image = material.normal_texture.texture->image;
            if (image == nullptr || image->uri == nullptr) {
                outError = "Material normal texture is missing image URI.";
                releaseParsedMaterials();
                cgltf_free(data);
                return false;
            }
            std::string textureLoadError;
            const auto *materialTexture = assetManager.RequestTexture(image->uri, textureLoadError);
            if (!materialTexture) {
                outError = textureLoadError.empty() ? "Failed to load normal texture." : textureLoadError;
                releaseParsedMaterials();
                cgltf_free(data);
                return false;
            }
            normalTexture = materialTexture->getTexture();
            normalTextureView = materialTexture->getTextureView();
            hasNormalTexture = true;
        }

        WGPUTexture emissiveTexture = fallbackEmissiveTexture->getTexture();
        WGPUTextureView emissiveTextureView = fallbackEmissiveTexture->getTextureView();
        bool hasEmissiveTexture = false;
        if (material.emissive_texture.texture != nullptr) {
            const auto *image = material.emissive_texture.texture->image;
            if (image == nullptr || image->uri == nullptr) {
                outError = "Material emissive texture is missing image URI.";
                releaseParsedMaterials();
                cgltf_free(data);
                return false;
            }
            std::string textureLoadError;
            const auto *materialTexture = assetManager.RequestTexture(image->uri, textureLoadError);
            if (!materialTexture) {
                outError = textureLoadError.empty() ? "Failed to load emissive texture." : textureLoadError;
                releaseParsedMaterials();
                cgltf_free(data);
                return false;
            }
            emissiveTexture = materialTexture->getTexture();
            emissiveTextureView = materialTexture->getTextureView();
            hasEmissiveTexture = true;
        }

        const float occlusionStrength = material.occlusion_texture.texture == nullptr ? 1.0f : material.occlusion_texture.strength;
        const auto flags = static_cast<float>(
            (material.alpha_mode == cgltf_alpha_mode_mask ? 1u : 0u) |
            (hasMetallicRoughnessTexture ? 2u : 0u) |
            (hasNormalTexture ? 4u : 0u) |
            (hasEmissiveTexture ? 8u : 0u)
        );
        const PbrMaterialUniform pbrMaterialUniform{
            .baseColorFactor = glm::vec4(
                material.pbr_metallic_roughness.base_color_factor[0],
                material.pbr_metallic_roughness.base_color_factor[1],
                material.pbr_metallic_roughness.base_color_factor[2],
                material.pbr_metallic_roughness.base_color_factor[3]
            ),
            .emissiveFactorMetallic = glm::vec4(
                material.emissive_factor[0],
                material.emissive_factor[1],
                material.emissive_factor[2],
                material.pbr_metallic_roughness.metallic_factor
            ),
            .roughnessOcclusionAlphaCutoffFlags = glm::vec4(
                material.pbr_metallic_roughness.roughness_factor,
                occlusionStrength,
                material.alpha_cutoff,
                flags
            ),
        };

        const WGPUBufferDescriptor pbrParamsBufferDesc{
            .label = {"PBR Material Uniform Buffer", WGPU_STRLEN},
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = sizeof(PbrMaterialUniform),
        };
        const auto pbrParamsBuffer = wgpuDeviceCreateBuffer(gpuState.device, &pbrParamsBufferDesc);
        if (pbrParamsBuffer == nullptr) {
            outError = "Failed to create PBR material uniform buffer.";
            releaseParsedMaterials();
            cgltf_free(data);
            return false;
        }
        wgpuQueueWriteBuffer(gpuState.queue, pbrParamsBuffer, 0, &pbrMaterialUniform, sizeof(PbrMaterialUniform));

        const auto bindGroupEntries = std::to_array<WGPUBindGroupEntry>({
            WGPUBindGroupEntry{.binding = 0, .textureView = baseColorTextureView},
            WGPUBindGroupEntry{.binding = 1, .sampler = gpuState.defaultSampler},
            WGPUBindGroupEntry{.binding = 2, .textureView = metallicRoughnessTextureView},
            WGPUBindGroupEntry{.binding = 3, .textureView = normalTextureView},
            WGPUBindGroupEntry{.binding = 4, .textureView = emissiveTextureView},
            WGPUBindGroupEntry{
                .binding = 5,
                .buffer = pbrParamsBuffer,
                .size = sizeof(PbrMaterialUniform),
            },
        });
        const WGPUBindGroupDescriptor bindGroupDesc{
            .label = {"Material Bind Group", WGPU_STRLEN},
            .layout = gpuState.defaultSamplerBindGroupLayout,
            .entryCount = static_cast<uint32_t>(bindGroupEntries.size()),
            .entries = bindGroupEntries.data(),
        };

        const auto newBindGroup = wgpuDeviceCreateBindGroup(gpuState.device, &bindGroupDesc);
        if (newBindGroup == nullptr) {
            outError = "Failed to create material bind group.";
            wgpuBufferRelease(pbrParamsBuffer);
            releaseParsedMaterials();
            cgltf_free(data);
            return false;
        }

        parsedMaterials[GetMaterialKey(&material, data)] = UnlitMaterial{
            .baseColorTexture = baseColorTexture,
            .baseColorTextureView = baseColorTextureView,
            .metallicRoughnessTexture = metallicRoughnessTexture,
            .metallicRoughnessTextureView = metallicRoughnessTextureView,
            .normalTexture = normalTexture,
            .normalTextureView = normalTextureView,
            .emissiveTexture = emissiveTexture,
            .emissiveTextureView = emissiveTextureView,
            .pbrParamsBuffer = pbrParamsBuffer,
            .bindGroup = newBindGroup,
        };
    }

    cgltf_free(data);

    if (parsedPrimitives.empty()) {
        releaseParsedMaterials();
        outError = "No triangle primitives found in glTF file.";
        return false;
    }

    for (auto &[materialName, parsedMaterial] : parsedMaterials) {
        materials[materialName] = std::move(parsedMaterial);
    }

    outPrimitives = std::move(parsedPrimitives);
    return true;
}
