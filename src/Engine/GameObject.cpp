#include "GameObject.h"

void GameObject::setScene(Scene *scene) {
    this->scene = scene;
}

Scene* GameObject::getScene() const {
    return scene;
}

bool GameObject::IsActiveInHierarchy() {
    if (!isActive) {
        return false;
    }
    if (parent) {
        return parent->IsActiveInHierarchy();
    }
    return true;
}

Transform GameObject::GetWorldTransform() const {
    if (parent) {
        return Transform::GetWorldTransform(parent->GetWorldTransform(), transform);
    }
    return transform;
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
        if (!(*component)->startCalled && (*component)->IsActive()) {
            (*component)->startCalled = true;
            (*component)->Start();
        }
        (*component)->Update(dt);
    }
    for (auto child = children.begin(); child != children.end(); ++child) {
        (*child)->Update(dt);
    }
}
