#ifndef ABERRANT_NODE_3D_HPP
#define ABERRANT_NODE_3D_HPP

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

class Node3D {
    public:
        Node3D() = default;
        Node3D(const Node3D&) = default;
        Node3D(Node3D&&) = default;
        Node3D& operator=(const Node3D&) = default;
        Node3D& operator=(Node3D&&) = default;
        ~Node3D() = default;

        glm::vec3 translation{0.0f, 0.0f, 0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

#endif