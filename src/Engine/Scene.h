#pragma once

#include <vector>

#include "Frustum.h"
#include "GameObject.h"
#include "Renderable.h"

class Scene {
    bool isActive = false;

    void setIsActive(bool isActive);

    std::vector<GameObject*> objectsInScene;
    std::vector<GameObject*> objectsToCleanup;

    std::vector<Renderable*> renderables;

    void TraverseGameObjectRenderables(GameObject* parent);

    void AddRenderables(GameObject* parent);

public:
    bool getIsActive();

    void WakeScene();
    void Update(float dt);

    void RefreshRenderables();
    void Render(Frustum& cameraFrustum, bool shadowRender = false);
};
