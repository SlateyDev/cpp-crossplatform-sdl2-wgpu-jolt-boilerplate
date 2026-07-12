#include "Component.h"

#include "GameObject.h"

void Component::WakeInternal() {
    if (awakeCalled) return;
    if (!IsActiveInHierarchy()) return;

    awakeCalled = true;
    Awake();
}

bool Component::IsActiveInHierarchy() {
    if (!isActive) return false;
    if (parent == nullptr) return false;
    if (!parent->IsActiveInHierarchy()) return false;
    return true;
}