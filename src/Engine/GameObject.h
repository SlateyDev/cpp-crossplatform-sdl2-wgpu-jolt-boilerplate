#pragma once

#include <string>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

#include "BaseObject.h"
#include "Scene.h"
#include "Transform.h"

class GameObject : public BaseObject {
    std::string name;

    Scene* scene = nullptr;
    std::vector<GameObject*> children;
    std::vector<Component*> components;

    void setScene(Scene* scene);

protected:
    void WakeInternal() override;

public:
    Scene* getScene() const;
    bool IsActiveInHierarchy() override;

    Transform transform;
    Transform GetWorldTransform() const;

    void SetWorldPositionAndRotation(glm::vec3& position, glm::quat& rotation);

    static GameObject Instantiate(glm::vec3& position, glm::quat& rotation, GameObject* parent = nullptr);

    template <std::derived_from<Component> T>
    T* AddComponent();

    void Update(float dt);

    friend class Scene;
    friend class BaseObject;
};
