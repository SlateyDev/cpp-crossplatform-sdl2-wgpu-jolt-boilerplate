#ifndef ABERRANT_PRIMITIVE_HPP
#define ABERRANT_PRIMITIVE_HPP

#include "glm/glm.hpp"

#include <string>
#include <vector>
#include <webgpu/webgpu.h>

struct Vertex {
    glm::vec3 position;
    glm::vec2 uv;
    glm::vec3 normal;
};

class Primitive {
    public:
        std::string materialResourceName;
        WGPUBuffer vertexBuffer = nullptr;
        uint32_t vertexCount = 0;
        WGPUBuffer indexBuffer = nullptr;
        uint32_t indexCount = 0;
        glm::vec3 localBoundsCenter{0.0f, 0.0f, 0.0f};
        float localBoundsRadius = 0.0f;

        static Primitive CreateFromPremadeData(WGPUDevice device, const std::vector<Vertex> &vertices, const std::vector<int> &indices, std::string materialKey);
};

extern const std::vector<Vertex> boxVertices;
extern const std::vector<int> boxIndices;
extern const std::vector<Vertex> planeVertices;
extern const std::vector<int> planeIndices;

#endif