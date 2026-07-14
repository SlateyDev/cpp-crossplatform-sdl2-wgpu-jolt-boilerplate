#include "MeshRenderer.hpp"

#include "GameObject.hpp"

void MeshRenderer::Render() {
    auto worldTransform = getParent()->GetWorldTransform();

    // model.DrawEx(worldTransform, modulate);
}
