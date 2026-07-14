#include "Scene.hpp"

#include "BaseObject.hpp"
#include "GameObject.hpp"
#include "Renderable.hpp"

void Scene::setIsActive(const bool value) {
    isActive = value;
}

void Scene::TraverseGameObjectRenderables(const GameObject& parent) {
    AddRenderables(parent);
    for (const auto gameObject : parent.children) {
        if (gameObject->getIsActive()) continue;
        TraverseGameObjectRenderables(*gameObject);
    }
}

void Scene::AddRenderables(const GameObject& parent) {
    for (auto component : parent.components) {
        if (!component->getIsActive()) continue;
        if (auto renderable = dynamic_cast<Renderable*>(component)) {
            renderables.push_back(renderable);
        }
    }
}

bool Scene::getIsActive() const {
    return isActive;
}

void Scene::WakeScene() {
    setIsActive(true);
    for (const auto sceneObject : objectsInScene) {
        sceneObject->WakeInternal();
    }
}

void Scene::Update(const float dt) {
    if (!isActive) return;

    for (const auto sceneObject : objectsInScene) {
        if (sceneObject->getParent() != nullptr) continue;
        if (!sceneObject->getIsActive()) continue;
        sceneObject->Update(dt);
    }

    if (!objectsToCleanup.empty()) {
        for (const auto obj : objectsToCleanup) {
            if (auto component = dynamic_cast<Component*>(obj)) {
                auto go = component->getParent();
                go->components.erase(std::remove(go->components.begin(), go->components.end(), component), go->components.end());
            } else if (auto gameObject = dynamic_cast<GameObject*>(obj)) {
                if (gameObject->getParent() != nullptr) {
                    gameObject->getParent()->children.erase(std::remove(gameObject->getParent()->children.begin(), gameObject->getParent()->children.end(), gameObject), gameObject->getParent()->children.end());
                    std::erase(objectsInScene, gameObject);
                }
            }
        }
        for (const auto objectToCleanup : objectsToCleanup) {
            delete objectToCleanup;
        }
        objectsToCleanup.clear();
    }
}

void Scene::RefreshRenderables() {
    renderables.clear();

    for (const auto sceneObject : objectsInScene) {
        if (sceneObject->getParent() != nullptr) continue;
        if (!sceneObject->getIsActive()) continue;
        TraverseGameObjectRenderables(*sceneObject);
    }
}

void Scene::Render(Frustum &cameraFrustum, const bool shadowRender) const {
    if (!isActive) return;

    std::vector<Renderable*> renderablesToRender;

    for (auto renderable : renderables) {
        if (shadowRender && !renderable->hasShadow) continue;
        if (auto [center, radius] = renderable->GetBoundingSphere(); cameraFrustum.SphereIn(center, radius)) {
            renderablesToRender.push_back(renderable);
        }
    }

    for (const auto renderable : renderablesToRender) {
        renderable->Render();
    }
}
