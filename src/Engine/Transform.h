#pragma once

#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

struct Transform {
    glm::vec3 translation;
    glm::quat rotation;
    glm::vec3 scale;

    static Transform GetWorldTransform(Transform parentWorld, Transform childLocal) {
        return {
            .translation = parentWorld.translation + parentWorld.rotation * (childLocal.translation * parentWorld.scale),
            .rotation = parentWorld.rotation * childLocal.rotation,
            .scale = parentWorld.scale * childLocal.scale,
        };
    }

    static Transform GetLocalTransform(Transform parentWorld, Transform childWorld) {
        auto invParentRotation = glm::inverse(parentWorld.rotation);
        return {
            .translation = invParentRotation * ((childWorld.translation - parentWorld.translation) / parentWorld.scale),
            .rotation = invParentRotation * childWorld.rotation,
            .scale = childWorld.scale / parentWorld.scale,
        };
    }
};
