#include "MeshRenderer.h"

#include "GameObject.h"

void MeshRenderer::Render() {
    auto worldTransform = getParent()->GetWorldTransform();

    // model.DrawEx(worldTransform, modulate);
}
