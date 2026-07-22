#include "AssetManager.hpp"

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
