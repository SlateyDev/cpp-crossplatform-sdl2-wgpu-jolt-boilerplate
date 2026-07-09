#pragma once

#include <string>
#include <vector>

#include <webgpu/webgpu.h>

#include "primitive.h"

bool LoadGltfPrimitives(
    WGPUDevice device,
    const std::string &gltfPath,
    const std::string &materialKey,
    std::vector<Primitive> &outPrimitives,
    std::string &outError
);
