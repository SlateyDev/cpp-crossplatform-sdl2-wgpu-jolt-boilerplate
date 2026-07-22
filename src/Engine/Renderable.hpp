#ifndef ABERRANT_ENGINE_RENDERABLE_HPP
#define ABERRANT_ENGINE_RENDERABLE_HPP

#include <glm/vec3.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Component.hpp"

class Renderable : public Component {
public:
    virtual ~Renderable();

    struct BoundingSphere {
        glm::vec3 center;
        float radius;
    };

    BoundingSphere boundingSphere;
    bool hasShadow;

    virtual void Render() = 0;

    glm::mat4 Transform();
    BoundingSphere GetBoundingSphere();
};

#endif