#ifndef ABERRANT_ENGINE_BASEOBJECT_HPP
#define ABERRANT_ENGINE_BASEOBJECT_HPP

#include <concepts>
#include <vector>

class Component;
class GameObject;

class BaseObject {
    friend class Component;
    friend class GameObject;

    GameObject* parent = nullptr;
    bool isActive = true;
    bool isDestroyed = false;

    void setParent(GameObject* obj);

    template <std::derived_from<Component> T>
    void GetComponentsInChildrenInternal(std::vector<T*>& components);

protected:
    void setIsActive(const bool value);

    virtual void WakeInternal() = 0;

public:
    virtual ~BaseObject() = default;

    GameObject* getParent() const;
    GameObject* gameObject();

    template <std::derived_from<Component> T>
    T* GetComponent();

    template <std::derived_from<Component> T>
    std::vector<T*> GetComponents();

    template <std::derived_from<Component> T>
    T* GetComponentInChildren();

    template <std::derived_from<Component> T>
    std::vector<T*> GetComponentsInChildren();

    bool getIsActive() const;

    virtual bool IsActiveInHierarchy() = 0;

    void Destroy();

    static void Destroy(BaseObject* obj);
};

#endif