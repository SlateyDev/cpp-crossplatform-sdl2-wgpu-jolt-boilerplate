#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <webgpu/webgpu.h>
#include <iostream>

#include "primitive.h"
#include "structures.h"

std::tuple<WGPUTexture, WGPUTextureView> LoadImageTexture(const GpuState &gpuState, const std::string& filepath);

bool LoadGltfPrimitives(
    const GpuState &gpuState,
    const std::string &gltfPath,
    std::unordered_map<std::string, UnlitMaterial> &materials,
    std::vector<Primitive> &outPrimitives,
    std::string &outError
);
