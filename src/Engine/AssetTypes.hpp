#ifndef ABERRANT_ENGINE_ASSETTYPES_HPP
#define ABERRANT_ENGINE_ASSETTYPES_HPP

#include <memory>
#include <atomic>
#include <string>
#include <vector>

enum class AssetType : int
{
    Texture,
    Mesh,
    Material,
    Model,
    Audio,
    Shader,
};

enum class AssetState : int
{
    Unloaded,
    LoadingIO,
    Decoding,
    UploadQueued,
    UploadingGPU,
    Ready,
    Failed,
};

struct AssetId
{
    int value{}; // hash(path + type) or GUID
    bool operator==(const AssetId&) const = default;
};

template<typename T>
struct AssetHandle
{
    AssetId id{};
    int generation{};
    bool IsValid() const {return id.value != 0;}
};

template<>
struct std::hash<AssetId>
{
    size_t operator()(const AssetId& a) const noexcept
    {
        return std::hash<int>{}(a.value);
    }
};

struct AssetRecordBase {
    AssetId id{};
    AssetType type{};
    std::string path;

    std::atomic<AssetState> state {AssetState::Unloaded};

    uint32_t generation {1};
    size_t memoryBytes {0};
    uint64_t lastUsedFrame {0};
    bool pinned {false};

    std::vector<AssetId> dependencies;
    virtual ~AssetRecordBase() = default;
};

template<typename T>
struct AssetRecord : AssetRecordBase {
    T resource {};
    bool hasResource {false};   // false until ready
    std::string error;
};

#endif