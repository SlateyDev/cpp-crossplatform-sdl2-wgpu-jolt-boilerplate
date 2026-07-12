#pragma once

#include <glm/glm.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define FRUSTUM_CULLING

struct Frustum {
    enum class FrustumPlanes
    {
        Far = 0,
        Near = 1,
        Bottom = 2,
        Top = 3,
        Right = 4,
        Left = 5,
        MAX = 6
    };

private:
    glm::vec4 planes[FrustumPlanes::MAX];

    static void NormalizePlanes(glm::vec4& plane) {
        plane /= glm::length(plane);
    }

public:
    void Extract(const glm::mat4& projection, const glm::mat4& view) {
#ifdef FRUSTUM_CULLING
        const auto m = projection * view;

        const glm::vec4 row0 = glm::row(m, 0);
        const glm::vec4 row1 = glm::row(m, 1);
        const glm::vec4 row2 = glm::row(m, 2);
        const glm::vec4 row3 = glm::row(m, 3);

        planes[static_cast<int>(FrustumPlanes::Left)] = row3 + row0;
        planes[static_cast<int>(FrustumPlanes::Right)] = row3 - row0;
        planes[static_cast<int>(FrustumPlanes::Bottom)] = row3 + row1;
        planes[static_cast<int>(FrustumPlanes::Top)] = row3 - row1;
        planes[static_cast<int>(FrustumPlanes::Near)] = row3 + row2;
        planes[static_cast<int>(FrustumPlanes::Far)] = row3 - row2;

        for (auto& plane : planes) {
            NormalizePlanes(plane);
        }
#endif
    }

    static float DistanceToPlane(const glm::vec4& plane, const glm::vec3& point) {
        return glm::dot(glm::vec3(plane), point) + plane.w;
    }

    static float DistanceToPlane(const glm::vec4& plane, const float x, const float y, const float z) {
        return glm::dot(glm::vec3(plane), glm::vec3(x, y, z)) + plane.w;
    }

    bool PointIn(const glm::vec3& point) {
#ifdef FRUSTUM_CULLING
        for (const auto& plane : planes) {
            if (DistanceToPlane(plane, point) < 0) {
                return false;
            }
        }
#endif
        return true;
    }

    bool PointIn(const float x, const float y, const float z) {
#ifdef FRUSTUM_CULLING
        for (const auto& plane : planes) {
            if (DistanceToPlane(plane, x, y, z) < 0) {
                return false;
            }
        }
#endif
        return true;
    }

    bool SphereIn(const glm::vec3& center, const float radius) {
#ifdef FRUSTUM_CULLING
        for (const auto& plane : planes) {
            if (DistanceToPlane(plane, center) < -radius) {
                return false;
            }
        }
#endif
        return true;
    }

    bool AABBBoxIn(const glm::vec3& min, const glm::vec3& max) {
#ifdef FRUSTUM_CULLING
        if (PointIn(min.x, min.y, min.z)) return true;
        if (PointIn(min.x, max.y, min.z)) return true;
        if (PointIn(max.x, max.y, min.z)) return true;
        if (PointIn(max.x, min.y, min.z)) return true;
        if (PointIn(min.x, min.y, max.z)) return true;
        if (PointIn(min.x, max.y, max.z)) return true;
        if (PointIn(max.x, max.y, max.z)) return true;
        if (PointIn(max.x, min.y, max.z)) return true;

        for (const auto& plane : planes) {
            auto oneInside = false;

            if (DistanceToPlane(plane, min.x, min.y, min.z) >= 0)
                oneInside = true;

            if (DistanceToPlane(plane, max.x, min.y, min.z) >= 0)
                oneInside = true;

            if (DistanceToPlane(plane, max.x, max.y, min.z) >= 0)
                oneInside = true;

            if (DistanceToPlane(plane, min.x, max.y, min.z) >= 0)
                oneInside = true;

            if (DistanceToPlane(plane, min.x, min.y, max.z) >= 0)
                oneInside = true;

            if (DistanceToPlane(plane, max.x, min.y, max.z) >= 0)
                oneInside = true;

            if (DistanceToPlane(plane, max.x, max.y, max.z) >= 0)
                oneInside = true;

            if (DistanceToPlane(plane, min.x, max.y, max.z) >= 0)
                oneInside = true;

            if (!oneInside) return false;
        }
#endif
        return true;
    }
};
