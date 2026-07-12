#include "BaseObject.h"

GameObject* BaseObject::getParent() const {
    return parent;
}

GameObject* BaseObject::gameObject() {
    if (dynamic_cast<Component*>(this))
        return parent;
    return reinterpret_cast<GameObject *>(this);
}

template<std::derived_from<Component> T>
T * BaseObject::GetComponent() {
    for (auto component : gameObject()->components) {
        if (auto c = dynamic_cast<T*>(component)) {
            return c;
        }
    }
    return nullptr;
}

template<std::derived_from<Component> T>
std::vector<T *> BaseObject::GetComponents() {
    std::vector<T*> components;
    for (auto component : gameObject()->components) {
        if (auto c = dynamic_cast<T*>(component)) {
            components.push_back(c);
        }
    }
    return components;
}

template<std::derived_from<Component> T>
T* BaseObject::GetComponentInChildren() {
    for (auto component : gameObject()->components) {
        if (auto c = dynamic_cast<T*>(component)) {
            return c;
        }
    }
    for (auto child : gameObject()->children) {
        if (auto component = child->GetComponentInChildren<T>()) {
            return component;
        }
    }
    return nullptr;
}

template <std::derived_from<Component> T>
std::vector<T*> BaseObject::GetComponentsInChildren() {
    std::vector<T*> components;
    GetComponentsInChildrenInternal(components);
    return components;
}

template <std::derived_from<Component> T>
void BaseObject::GetComponentsInChildrenInternal(std::vector<T*>& components) {
    for (auto component : components) {
        if (dynamic_cast<T*>(component)) {
            components.push_back(component);
        }
    }
    for (auto child : gameObject()->children) {
        child->GetComponentsInChildrenInternal(components);
    }
}

bool BaseObject::getIsActive() const {
    return !isDestroyed && isActive;
}

void BaseObject::setParent(GameObject *obj) {
    parent = obj;
}

void BaseObject::setIsActive(const bool value) {
    if (isDestroyed) return;

    isActive = value;
    WakeInternal();
}

void BaseObject::Destroy() {
    if (isDestroyed) return;

    if (auto component = dynamic_cast<Component*>(this)) {
        component->OnDestroy();
    //     Program.game.[Friend]scene.[Friend]objectsToCleanup.Add(component);
    } else if (auto go = dynamic_cast<GameObject*>(this)) {
        for (auto child : go->children) {
            child->Destroy();
        }
        for (auto component : go->components) {
            component->isDestroyed = true;
            component->isActive = false;
        }
    //     Program.game.[Friend]scene.[Friend]objectsToCleanup.Add(go);
    }

    isDestroyed = true;
    isActive = false;
}

void BaseObject::Destroy(BaseObject* obj) {
    obj->Destroy();
}