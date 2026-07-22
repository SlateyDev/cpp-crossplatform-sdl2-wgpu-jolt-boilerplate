#ifndef ABERRANT_ENGINE_MESH_HPP
#define ABERRANT_ENGINE_MESH_HPP

#include <string>
#include <vector>
#include <webgpu.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct Mesh
{
    std::string materialResourceName;
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    WGPUBuffer vertexBuffer = nullptr;
    uint32_t vertexCount = 0;
    std::vector<int> indices;
    WGPUBuffer indexBuffer = nullptr;
    uint32_t indexCount = 0;
};

#endif