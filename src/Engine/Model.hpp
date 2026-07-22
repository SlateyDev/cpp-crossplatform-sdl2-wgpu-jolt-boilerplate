#ifndef ABERRANT_ENGINE_MODEL_HPP
#define ABERRANT_ENGINE_MODEL_HPP

#include <string>
#include <vector>
#include <glm/glm.hpp>

class AssetManager;

struct Mesh;
struct Material;

class Model
{
    std::string fileName;
    glm::mat4 transform;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;

public:
    bool LoadGltf(const std::string& fileName, AssetManager& assetManager, std::string& outError);
};

#endif