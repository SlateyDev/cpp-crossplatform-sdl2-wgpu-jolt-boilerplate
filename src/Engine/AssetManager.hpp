#ifndef ABERRANT_ENGINE_ASSETMANAGER_HPP
#define ABERRANT_ENGINE_ASSETMANAGER_HPP

#include <shared_mutex>
#include <unordered_map>
#include <memory>
#include <string>
#include <webgpu.h>

#include "AssetTypes.hpp"

class EngineTexture;

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

    static AssetId MakeAssetId(const std::string& path, AssetType type);

public:
    AssetManager(WGPUDevice device, WGPUQueue queue);
    ~AssetManager();
    const EngineTexture* RequestTexture(const std::string& path, std::string& outError);
};

#endif
