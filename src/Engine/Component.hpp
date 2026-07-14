#pragma once

#include "BaseObject.hpp"

class Component : public BaseObject {
    bool awakeCalled = false;
    bool startCalled = false;

protected:
    void WakeInternal() override;

public:
    bool IsActiveInHierarchy() override;

    virtual void Awake() {}
    virtual void OnEnable() {}
    virtual void Start() {}

    virtual void Update(float dt) {}
    // virtual void FixedUpdate(float dt) {}
    // virtual void LateUpdate(float dt) {}

    virtual void OnDisable() {}
    virtual void OnDestroy() {}

    friend class GameObject;
};
