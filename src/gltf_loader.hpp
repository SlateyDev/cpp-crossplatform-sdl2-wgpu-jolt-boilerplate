#ifndef ABERRANT_GLTF_LOADER_HPP
#define ABERRANT_GLTF_LOADER_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include <webgpu/webgpu.h>
#include <iostream>

#include "primitive.hpp"
#include "structures.hpp"
#include "Engine/AssetManager.hpp"

bool LoadGltfPrimitives(
    const GpuState &gpuState,
    AssetManager& assetManager,
    const std::string &gltfPath,
    std::unordered_map<std::string, UnlitMaterial> &materials,
    std::vector<Primitive> &outPrimitives,
    std::string &outError
);

#endif