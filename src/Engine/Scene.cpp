#include "Scene.h"

void Scene::setIsActive(bool isActive) {
}

void Scene::TraverseGameObjectRenderables(GameObject *parent) {
    AddRenderables(parent);
    for (auto gameObject : parent->children) {
        if (gameObject->getIsActive()) continue;
        TraverseGameObjectRenderables(gameObject);
    }
}

void Scene::AddRenderables(GameObject *parent) {
    for (auto component : parent->components) {
        if (!component->getIsActive()) continue;
        if (auto renderable = dynamic_cast<Renderable*>(component)) {
            renderables.push_back(renderable);
        }
    }
}

bool Scene::getIsActive() {
}

void Scene::WakeScene() {
}

void Scene::Update(float dt) {
    if (!isActive) return;

    for (const auto sceneObject : objectsInScene) {
        if (sceneObject->getParent() != nullptr) continue;
        if (!sceneObject->getIsActive()) continue;
        sceneObject->Update(dt);
    }

    if (!objectsToCleanup.empty()) {
        for (const auto obj : objectsToCleanup) {
            if (auto component = dynamic_cast<Component*>(obj)) {
                component->getParent()->components->Remove(component);
            } else if (auto gameObject = dynamic_cast<GameObject*>(obj)) {
                if (gameObject->getParent() != nullptr) {
                    gameObject->getParent()->components.erase(gameObject);
                }
                auto it = std::find(objectsInScene.begin(), objectsInScene.end(), gameObject);
                if (it != objectsInScene.end()) {
                    objectsInScene.erase(it);
                }
            }
        }
        objectsToCleanup.clear();
    }
}

void Scene::RefreshRenderables() {
    renderables.clear();

    for (const auto sceneObject : objectsInScene) {
        if (sceneObject.getParent() != nullptr) continue;
        if (!sceneObject.getIsActive()) continue;
        TraverseGameObjectRenderables(sceneObject);
    }
}

void Scene::Render(Frustum &cameraFrustum, bool shadowRender) {
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
