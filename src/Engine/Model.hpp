#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

struct Mesh;
struct Material;

class Model
{
    std::string fileName;
    glm::mat4 transform;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;

public:
    bool LoadGltf(const std::string& fileName, std::string& outError);
};
