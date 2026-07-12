#include "Renderable.h"

Renderable::~Renderable() {
}

glm::mat4 Renderable::Transform() {
    return glm::mat4(1.0f);
}

Renderable::BoundingSphere Renderable::GetBoundingSphere() {
    // auto go = gameObject;
    return boundingSphere;
}
