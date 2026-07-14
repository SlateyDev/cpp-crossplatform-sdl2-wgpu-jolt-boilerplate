#include "GameObject.hpp"

#include <stdexcept>

#include "Component.hpp"
#include "Scene.hpp"
#include "SceneManager.hpp"

void GameObject::setScene(Scene *scene) {
    this->scene = scene;
}

void GameObject::WakeInternal() {
    if (!IsActiveInHierarchy()) return;

    for (const auto child : children) {
        if (!child->IsActiveInHierarchy()) continue;
        child->WakeInternal();
    }

    for (const auto component : components) {
        if (!component->getIsActive()) continue;
        if (component->awakeCalled) continue;
        component->awakeCalled = true;
        component->Awake();
    }
}

Scene* GameObject::getScene() const {
    return scene;
}

bool GameObject::IsActiveInHierarchy() {
    if (!getIsActive()) return false;

    auto current = this;
    while (current) {
        if (!current->getIsActive()) return false;
        current = current->getParent();
    }
    return true;
}

Transform GameObject::GetWorldTransform() const {
    if (parent) {
        return Transform::GetWorldTransform(parent->GetWorldTransform(), transform);
    }
    return transform;
}

void GameObject::SetWorldPositionAndRotation(glm::vec3 &position, glm::quat &rotation) {
    if (parent) {
        const auto parentWorldTransform = parent->GetWorldTransform();
        auto childTransform = transform;
        childTransform.translation = position;
        childTransform.rotation = rotation;
        transform = Transform::GetLocalTransform(parentWorldTransform, childTransform);
    } else {
        transform.translation = position;
        transform.rotation = rotation;
    }
}

GameObject* GameObject::Instantiate(const glm::vec3 &position, const glm::quat &rotation, GameObject *parent) {
    if (parent && parent->isDestroyed) {
        throw std::runtime_error("Tried to instantiate a GameObject with a destroyed parent");
    }

    const auto newGameObject = new GameObject();
    newGameObject->transform = {position, rotation, {1.0f, 1.0f, 1.0f}};
    newGameObject->setParent(parent);
    if (parent) {
        parent->children.push_back(newGameObject);
    }
    if (const auto scene = parent ? parent->getScene() : nullptr) {
        newGameObject->setScene(scene);
    } else {
        newGameObject->setScene(SceneManager::GetInstance().GetActiveScene());
    }
    newGameObject->getScene()->objectsInScene.push_back(newGameObject);
    return newGameObject;
}

template <std::derived_from<Component> T>
T* GameObject::AddComponent(){
    T* component = new T();
    component->setParent(this);
    components.push_back(component);

    if (scene != nullptr && scene->getIsActive() && isActive && component.isActive && !component.awakeCalled) {
        component.Awake();
        component.awakeCalled = true;
    }
    return component;
}

void GameObject::Update(const float dt) {
    for (auto component = components.begin(); component != components.end(); ++component) {
        if (!(*component)->startCalled && (*component)->getIsActive()) {
            (*component)->startCalled = true;
            (*component)->Start();
        }
        (*component)->Update(dt);
    }
    for (auto child = children.begin(); child != children.end(); ++child) {
        (*child)->Update(dt);
    }
}
