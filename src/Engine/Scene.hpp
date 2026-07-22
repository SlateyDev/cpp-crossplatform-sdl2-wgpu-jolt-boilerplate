#ifndef ABERRANT_ENGINE_SCENE_HPP
#define ABERRANT_ENGINE_SCENE_HPP

#include <vector>

#include "Frustum.hpp"

class BaseObject;
class GameObject;
class Renderable;

class Scene {
    friend class GameObject;

    bool isActive = false;

    void setIsActive(const bool value);

    std::vector<GameObject*> objectsInScene;
    std::vector<BaseObject*> objectsToCleanup;
    std::vector<Renderable*> renderables;

    void TraverseGameObjectRenderables(const GameObject& parent);

    void AddRenderables(const GameObject& parent);

public:
    bool getIsActive() const;

    void WakeScene();
    void Update(const float dt);

    void RefreshRenderables();
    void Render(Frustum& cameraFrustum, bool shadowRender = false) const;
};

#endif