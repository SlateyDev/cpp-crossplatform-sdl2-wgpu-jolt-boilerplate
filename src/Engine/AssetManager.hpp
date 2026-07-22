#ifndef ABERRANT_ENGINE_ASSETMANAGER_HPP
#define ABERRANT_ENGINE_ASSETMANAGER_HPP

#include <shared_mutex>
#include <unordered_map>
#include <webgpu.h>

#include "AssetTypes.hpp"

class AssetManager
{
    WGPUDevice device{};
    WGPUQueue queue{};

    std::unordered_map<AssetId, std::unique_ptr<AssetRecordBase>> assets;
    std::unordered_map<std::string, AssetId> pathToId;
    mutable std::shared_mutex mapMutex;

    // size_t cpuBudgetBytes {512ull * 1024 * 1024};
    // size_t gpuBudgetBytes {1024ull * 1024 * 1024};
    // size_t cpuUsedBytes {0};
    // size_t gpuUsedBytes {0};

    // uint64_t unloadGraceFrames {180};

    template<typename T>
    const T* TryGetTyped(AssetHandle<T> handle, AssetType expected) const;

public:
    AssetManager(WGPUDevice device, WGPUQueue queue);
};

#endif
