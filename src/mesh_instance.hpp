#pragma once

#include <webgpu/webgpu.h>
#include <string>

#include "node_3d.hpp"

class MeshInstance : public Node3D {
    public:
        MeshInstance() = default;
        MeshInstance(const MeshInstance&) = default;
        MeshInstance(MeshInstance&&) = default;
        MeshInstance& operator=(const MeshInstance&) = default;
        MeshInstance& operator=(MeshInstance&&) = default;
        ~MeshInstance() = default;

        WGPUBuffer uniformBuffer{};
        WGPUBindGroup uniformBindGroup{};
        std::string meshName;
};