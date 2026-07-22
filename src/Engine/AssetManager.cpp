#include "AssetManager.hpp"

#include <functional>
#include <mutex>

#include "EngineTexture.hpp"

namespace
{
const EngineTexture* GetReadyTextureFromRecord(const AssetRecordBase* base)
{
    if (!base || base->type != AssetType::Texture) return nullptr;
    if (base->state.load(std::memory_order_acquire) != AssetState::Ready) return nullptr;

    const auto* record = static_cast<const AssetRecord<std::unique_ptr<EngineTexture>>*>(base);
    if (!record->hasResource || record->resource == nullptr) return nullptr;
    return record->resource.get();
}

const EngineTexture* FindReadyTextureByPathLocked(
    const std::unordered_map<std::string, AssetId>& pathToId,
    const std::unordered_map<AssetId, std::unique_ptr<AssetRecordBase>>& assets,
    const std::string& path)
{
    const auto mapped = pathToId.find(path);
    if (mapped == pathToId.end()) return nullptr;

    const auto existing = assets.find(mapped->second);
    if (existing == assets.end()) return nullptr;

    return GetReadyTextureFromRecord(existing->second.get());
}

AssetId FindAvailableTextureAssetIdLocked(
    const std::unordered_map<AssetId, std::unique_ptr<AssetRecordBase>>& assets,
    AssetId assetId,
    const std::string& path)
{
    auto existing = assets.find(assetId);
    while (existing != assets.end()) {
        const auto* base = existing->second.get();
        if (base->path == path && base->type == AssetType::Texture) {
            break;
        }

        ++assetId.value;
        if (assetId.value == 0) {
            assetId.value = 1;
        }
        existing = assets.find(assetId);
    }
    return assetId;
}
}

AssetId AssetManager::MakeAssetId(const std::string& path, AssetType type)
{
    const auto key = path + "#" + std::to_string(static_cast<int>(type));
    const auto hashed = static_cast<int>(std::hash<std::string>{}(key) & 0x7FFFFFFF);
    return AssetId{hashed == 0 ? 1 : hashed};
}

AssetManager::AssetManager(WGPUDevice device, WGPUQueue queue) : device(device), queue(queue) {}

AssetManager::~AssetManager()
{
    std::unique_lock writeLock(mapMutex);
    pathToId.clear();
    assets.clear();
}

const EngineTexture* AssetManager::RequestTexture(const std::string& path, std::string& outError)
{
    outError.clear();

    {
        std::shared_lock readLock(mapMutex);
        if (const auto* existingTexture = FindReadyTextureByPathLocked(pathToId, assets, path)) {
            return existingTexture;
        }
    }

    auto texture = std::make_unique<EngineTexture>();
    if (!texture->LoadImage(path)) {
        outError = "Failed to load image: " + path;
        return {};
    }
    if (!texture->CreateTextureAndView(device, queue)) {
        outError = "Failed to create GPU texture from image: " + path;
        return {};
    }

    {
        std::unique_lock writeLock(mapMutex);
        if (const auto* existingTexture = FindReadyTextureByPathLocked(pathToId, assets, path)) {
            return existingTexture;
        }

        const AssetId assetId = FindAvailableTextureAssetIdLocked(assets, MakeAssetId(path, AssetType::Texture), path);

        auto record = std::make_unique<AssetRecord<std::unique_ptr<EngineTexture>>>();
        record->id = assetId;
        record->type = AssetType::Texture;
        record->path = path;
        record->resource = std::move(texture);
        record->hasResource = true;
        record->state.store(AssetState::Ready, std::memory_order_release);
        record->generation = 1;

        const EngineTexture* loadedTexture = record->resource.get();
        assets[assetId] = std::move(record);
        pathToId[path] = assetId;

        return loadedTexture;
    }
}
