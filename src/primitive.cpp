#include "primitive.hpp"

const std::vector<Vertex> boxVertices = {
    // Top face
    {glm::vec3(-0.5f, -0.5f,  0.5f), glm::vec2(0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)},
    {glm::vec3( 0.5f, -0.5f,  0.5f), glm::vec2(1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)},
    {glm::vec3( 0.5f,  0.5f,  0.5f), glm::vec2(1.0f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f)},
    {glm::vec3(-0.5f,  0.5f,  0.5f), glm::vec2(0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f)},

    // Bottom face
    {glm::vec3(-0.5f,  0.5f, -0.5f), glm::vec2(1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)},
    {glm::vec3( 0.5f,  0.5f, -0.5f), glm::vec2(0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)},
    {glm::vec3( 0.5f, -0.5f, -0.5f), glm::vec2(0.0f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f)},
    {glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec2(1.0f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f)},

    // Right face
    {glm::vec3( 0.5f, -0.5f, -0.5f), glm::vec2(0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)},
    {glm::vec3( 0.5f,  0.5f, -0.5f), glm::vec2(1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)},
    {glm::vec3( 0.5f,  0.5f,  0.5f), glm::vec2(1.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f)},
    {glm::vec3( 0.5f, -0.5f,  0.5f), glm::vec2(0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f)},

    // Left face
    {glm::vec3(-0.5f, -0.5f,  0.5f), glm::vec2(1.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f)},
    {glm::vec3(-0.5f,  0.5f,  0.5f), glm::vec2(0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f)},
    {glm::vec3(-0.5f,  0.5f, -0.5f), glm::vec2(0.0f, 1.0f), glm::vec3(-1.0f, 0.0f, 0.0f)},
    {glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec2(1.0f, 1.0f), glm::vec3(-1.0f, 0.0f, 0.0f)},

    // Front face
    {glm::vec3( 0.5f,  0.5f, -0.5f), glm::vec2(1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)},
    {glm::vec3(-0.5f,  0.5f, -0.5f), glm::vec2(0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)},
    {glm::vec3(-0.5f,  0.5f,  0.5f), glm::vec2(0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f)},
    {glm::vec3( 0.5f,  0.5f,  0.5f), glm::vec2(1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f)},

    // Back face
    {glm::vec3( 0.5f, -0.5f,  0.5f), glm::vec2(0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)},
    {glm::vec3(-0.5f, -0.5f,  0.5f), glm::vec2(1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)},
    {glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec2(1.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)},
    {glm::vec3( 0.5f, -0.5f, -0.5f), glm::vec2(0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)},
};

const std::vector<int> boxIndices = {
    0, 1, 2, 2, 3, 0, // top
    4, 5, 6, 6, 7, 4, // bottom
    8, 9, 10, 10, 11, 8, // right
    12, 13, 14, 14, 15, 12, // left
    16, 17, 18, 18, 19, 16, // front
    20, 21, 22, 22, 23, 20, // back
};

const std::vector<Vertex> planeVertices = {
    {glm::vec3(-0.5f, -0.5f, 0.0f), glm::vec2(0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)},
    {glm::vec3( 0.5f, -0.5f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)},
    {glm::vec3( 0.5f,  0.5f, 0.0f), glm::vec2(1.0f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f)},
    {glm::vec3(-0.5f,  0.5f, 0.0f), glm::vec2(0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f)},
};

const std::vector<int> planeIndices = {
    0, 1, 2, 2, 3, 0,
};

Primitive Primitive::CreateFromPremadeData(const WGPUDevice device, const std::vector<Vertex> &vertices, const std::vector<int> &indices, std::string materialKey)
{
    auto newPrimitive = Primitive {
        .materialResourceName = materialKey,
        .vertexCount = static_cast<uint32_t>(vertices.size()),
        .indexCount = static_cast<uint32_t>(indices.size()),
    };

    WGPUBufferDescriptor vertexBufferDescriptor = {
        .nextInChain = nullptr,
        .label = "Vertex Buffer",
        .usage = WGPUBufferUsage_Vertex,
        .size = sizeof(Vertex) * newPrimitive.vertexCount,
        .mappedAtCreation = true,
    };

    newPrimitive.vertexBuffer = wgpuDeviceCreateBuffer(device, &vertexBufferDescriptor);

    void *vertexData = wgpuBufferGetMappedRange(newPrimitive.vertexBuffer, 0, vertexBufferDescriptor.size);
    memcpy(vertexData, vertices.data(), vertexBufferDescriptor.size);
    wgpuBufferUnmap(newPrimitive.vertexBuffer);

    WGPUBufferDescriptor indexBufferDescriptor = {
        .nextInChain = nullptr,
        .label = "Index Buffer",
        .usage = WGPUBufferUsage_Index,
        .size = sizeof(int) * newPrimitive.indexCount,
        .mappedAtCreation = true,
    };

    newPrimitive.indexBuffer = wgpuDeviceCreateBuffer(device, &indexBufferDescriptor);

    void *indexData = wgpuBufferGetMappedRange(newPrimitive.indexBuffer, 0, indexBufferDescriptor.size);
    memcpy(indexData, indices.data(), indexBufferDescriptor.size);
    wgpuBufferUnmap(newPrimitive.indexBuffer);

    return newPrimitive;
}
