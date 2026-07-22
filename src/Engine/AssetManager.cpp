#include "AssetManager.hpp"

#include <functional>
#include <mutex>

#include "EngineTexture.hpp"

template <typename T>
const T* AssetManager::TryGetTyped(AssetHandle<T> handle, AssetType expected) const
{
    std::shared_lock lock(mapMutex);
    auto it = assets.find(handle.id);
    if (it == assets.end()) return nullptr;

    auto* base = it->second.get();
    if (base->type != expected) return nullptr;
    if (base->generation != handle.generation) return nullptr;
    if (base->state.load(std::memory_order_acquire) != AssetState::Ready) return nullptr;

    auto* rec = static_cast<AssetRecord<T>*>(base);
    if (!rec->hasResource) return nullptr;
    return &rec->resource;
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
    AssetId assetId = MakeAssetId(path, AssetType::Texture);

    {
        std::shared_lock readLock(mapMutex);
        if (const auto mapped = pathToId.find(path); mapped != pathToId.end()) {
            if (const auto existing = assets.find(mapped->second); existing != assets.end()) {
                auto* base = existing->second.get();
                if (base->type == AssetType::Texture &&
                    base->state.load(std::memory_order_acquire) == AssetState::Ready) {

                    const AssetHandle<std::unique_ptr<EngineTexture>> typedHandle{
                        .id = mapped->second,
                        .generation = static_cast<int>(base->generation),
                    };
                    const auto* existingTexture = TryGetTyped<std::unique_ptr<EngineTexture>>(typedHandle, AssetType::Texture);
                    if (!existingTexture || *existingTexture == nullptr) return nullptr;
                    return existingTexture->get();
                }
            }
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
        if (const auto mapped = pathToId.find(path); mapped != pathToId.end()) {
            if (const auto existing = assets.find(mapped->second); existing != assets.end()) {
                auto* base = existing->second.get();
                if (base->type == AssetType::Texture &&
                    base->state.load(std::memory_order_acquire) == AssetState::Ready) {

                    const AssetHandle<std::unique_ptr<EngineTexture>> typedHandle{
                        .id = mapped->second,
                        .generation = static_cast<int>(base->generation),
                    };
                    const auto* existingTexture = TryGetTyped<std::unique_ptr<EngineTexture>>(typedHandle, AssetType::Texture);
                    if (!existingTexture || *existingTexture == nullptr) return nullptr;
                    return existingTexture->get();
                    }
            }
        }
    }

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

    auto record = std::make_unique<AssetRecord<std::unique_ptr<EngineTexture>>>();
    record->id = assetId;
    record->type = AssetType::Texture;
    record->path = path;
    record->resource = std::move(texture);
    record->hasResource = true;
    record->state.store(AssetState::Ready, std::memory_order_release);
    record->generation = 1;

    assets[assetId] = std::move(record);
    pathToId[path] = assetId;

    const AssetHandle<std::unique_ptr<EngineTexture>> typedHandle{
        .id = assetId,
        .generation = 1,
    };
    const auto* newTexture = TryGetTyped<std::unique_ptr<EngineTexture>>(typedHandle, AssetType::Texture);
    if (!newTexture || *newTexture == nullptr) return nullptr;
    return newTexture->get();
}
