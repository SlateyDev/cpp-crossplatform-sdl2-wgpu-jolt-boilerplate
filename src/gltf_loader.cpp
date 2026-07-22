#include "gltf_loader.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <array>
#include <climits>
#include <vector>

#include "structures.hpp"
#include "Engine/EngineTexture.hpp"

namespace {

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

            if (primitive.material->pbr_metallic_roughness.base_color_texture.texture == nullptr) {
                parsedPrimitives.push_back(Primitive::CreateFromPremadeData(gpuState.device, vertices, indices, "sample.png"));
            } else {
                parsedPrimitives.push_back(Primitive::CreateFromPremadeData(gpuState.device, vertices, indices, primitive.material->name));
            }
        }
    }

    for (cgltf_size materialIndex = 0; materialIndex < data->materials_count; ++materialIndex) {
        const auto &material = data->materials[materialIndex];

        if (material.pbr_metallic_roughness.base_color_texture.texture != nullptr) {
            const auto* image = material.pbr_metallic_roughness.base_color_texture.texture->image;
            if (image == nullptr || image->uri == nullptr) {
                outError = "Material base color texture is missing image URI.";
                cgltf_free(data);
                return false;
            }

            std::string textureLoadError;
            const auto materialTexture = assetManager.RequestTexture(image->uri, textureLoadError);
            if (!materialTexture) {
                outError = textureLoadError.empty() ? "Failed to load material texture." : textureLoadError;
                cgltf_free(data);
                return false;
            }

            const auto bindGroupEntries = std::to_array<WGPUBindGroupEntry>({
                {.binding = 0, .textureView = materialTexture->getTextureView()},
                {.binding = 1, .sampler = gpuState.defaultSampler},
                // {binding = 2, textureView = normalTextureView},
                // {binding = 3, sampler = normalSampler},
                // {binding = 4, textureView = metallicRoughnessTextureView},
                // {binding = 5, sampler = metallicRoughnessSampler},
                // {binding = 6, textureView = emissiveTextureView},
                // {binding = 7, sampler = emissiveSampler},
                // {binding = 8, textureView = occlusionTextureView},
                // {binding = 9, sampler = occlusionSampler},
            });
            const WGPUBindGroupDescriptor bindGroupDesc {
                .label = "Bind Group",
                .layout = gpuState.defaultSamplerBindGroupLayout,
                .entryCount = bindGroupEntries.size(),
                .entries = bindGroupEntries.data(),
            };

            const auto newBindGroup = wgpuDeviceCreateBindGroup(
                gpuState.device,
                &bindGroupDesc);

            materials[material.name] = UnlitMaterial{
                .baseColorTexture = materialTexture->getTexture(),
                .baseColorTextureView = materialTexture->getTextureView(),
                .bindGroup = newBindGroup,
            };
        }
    }

    cgltf_free(data);

    if (parsedPrimitives.empty()) {
        outError = "No triangle primitives found in glTF file.";
        return false;
    }

    outPrimitives = std::move(parsedPrimitives);
    return true;
}
