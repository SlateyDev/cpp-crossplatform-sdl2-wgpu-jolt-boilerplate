#include "Model.hpp"

// #define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "Mesh.hpp"
#include "Material.hpp"
#include "../primitive.hpp"
#include "../structures.hpp"

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

bool Model::LoadGltf(const std::string& fileName, std::string& outError) {
    outError.clear();
    meshes.clear();
    materials.clear();

    this->fileName = fileName;

    constexpr cgltf_options options{};
    cgltf_data *data = nullptr;

    cgltf_result result = cgltf_parse_file(&options, fileName.c_str(), &data);
    if (result != cgltf_result_success) {
        outError = std::string("Failed to parse glTF: ") + CgltfResultToString(result);
        return false;
    }

    result = cgltf_load_buffers(&options, data, fileName.c_str());
    if (result != cgltf_result_success) {
        outError = std::string("Failed to load glTF buffers: ") + CgltfResultToString(result);
        cgltf_free(data);
        return false;
    }

    for (auto meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex) {
        const auto &mesh = data->meshes[meshIndex];

        for (auto primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex) {
            const auto &primitive = mesh.primitives[primitiveIndex];
            if (primitive.type != cgltf_primitive_type_triangles) continue;

            const auto *positionAccessor = FindAttribute(primitive, cgltf_attribute_type_position);
            if (!positionAccessor) continue;

            std::vector<glm::vec3> positions;
            if (!ReadVec3(positionAccessor, positions)) {
                outError = "Failed to read POSITION accessor.";
                cgltf_free(data);
                return false;
            }

            std::vector normals(positions.size(), glm::vec3(0.0f, 0.0f, 1.0f));
            if (const auto *normalAccessor = FindAttribute(primitive, cgltf_attribute_type_normal)) {
                std::vector<glm::vec3> loadedNormals;
                if (!ReadVec3(normalAccessor, loadedNormals) || loadedNormals.size() != positions.size()) {
                    outError = "Failed to read NORMAL accessor.";
                    cgltf_free(data);
                    return false;
                }
                normals = std::move(loadedNormals);
            }

            std::vector uvs(positions.size(), glm::vec2(0.0f));
            if (const auto *uvAccessor = FindAttribute(primitive, cgltf_attribute_type_texcoord)) {
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

            Mesh newMesh {
                .materialResourceName = primitive.material ? primitive.material->name : "",
                .positions = std::move(positions),
                .normals = std::move(normals),
                .uvs = std::move(uvs),
                .indices = std::move(indices),
            };
            meshes.push_back(newMesh);
        }
    }

    for (auto materialIndex = 0; materialIndex < data->materials_count; ++materialIndex) {
        if (const auto &material = data->materials[materialIndex]; material.pbr_metallic_roughness.base_color_texture.texture != nullptr) {
            const auto [materialTexture, materialTextureView] = ::LoadImageTexture(gpuState, material.pbr_metallic_roughness.base_color_texture.texture->image->uri);

            // const auto bindGroupEntries = std::to_array<WGPUBindGroupEntry>({
            //     {.binding = 0, .textureView = materialTextureView},
            //     {.binding = 1, .sampler = gpuState.defaultSampler},
            //     // {binding = 2, textureView = normalTextureView},
            //     // {binding = 3, sampler = normalSampler},
            //     // {binding = 4, textureView = metallicRoughnessTextureView},
            //     // {binding = 5, sampler = metallicRoughnessSampler},
            //     // {binding = 6, textureView = emissiveTextureView},
            //     // {binding = 7, sampler = emissiveSampler},
            //     // {binding = 8, textureView = occlusionTextureView},
            //     // {binding = 9, sampler = occlusionSampler},
            // });
            // const WGPUBindGroupDescriptor bindGroupDesc {
            //     .label = "Bind Group",
            //     .layout = gpuState.defaultSamplerBindGroupLayout,
            //     .entryCount = bindGroupEntries.size(),
            //     .entries = bindGroupEntries.data(),
            // };

            // const auto newBindGroup = wgpuDeviceCreateBindGroup(
            //     gpuState.device,
            //     &bindGroupDesc);
            //
            // materials[material.name] = UnlitMaterial{
            //     .baseColorTexture = materialTexture,
            //     .baseColorTextureView = materialTextureView,
            //     .bindGroup = newBindGroup,
            // };
        }
    }

    cgltf_free(data);

    if (meshes.empty()) {
        outError = "No suitable meshes found in glTF file.";
        return false;
    }

    return true;
}
