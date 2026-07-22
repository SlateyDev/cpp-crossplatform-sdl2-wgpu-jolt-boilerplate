#ifndef ABERRANT_ENGINE_GAMEOBJECT_HPP
#define ABERRANT_ENGINE_GAMEOBJECT_HPP

#include <string>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

#include "BaseObject.hpp"
#include "Transform.hpp"

class Scene;

class GameObject : public BaseObject {
    friend class Scene;
    friend class BaseObject;

    std::string name;

    Scene* scene = nullptr;
    std::vector<GameObject*> children;
    std::vector<Component*> components;

    void setScene(Scene* scene);

protected:
    void WakeInternal() override;

public:
    ~GameObject();

    Scene* getScene() const;
    bool IsActiveInHierarchy() override;

    Transform transform;
    Transform GetWorldTransform() const;

    void SetWorldPositionAndRotation(glm::vec3& position, glm::quat& rotation);

    static GameObject* Instantiate(const glm::vec3& position, const glm::quat& rotation, GameObject* parent = nullptr);

    template <std::derived_from<Component> T>
    T* AddComponent();

    void Update(float dt) const;
};

#endif