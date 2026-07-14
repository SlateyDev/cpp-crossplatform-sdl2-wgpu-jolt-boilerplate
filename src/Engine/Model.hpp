#pragma once

#include <vector>
#include <glm/glm.hpp>

class Mesh;
class Material;

class Model
{
    glm::mat4 transform;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
};
