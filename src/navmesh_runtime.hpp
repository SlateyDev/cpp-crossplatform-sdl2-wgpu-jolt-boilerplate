#ifndef ABERRANT_NAVMESH_RUNTIME_HPP
#define ABERRANT_NAVMESH_RUNTIME_HPP

#include <glm/vec3.hpp>

#include <array>
#include <string>
#include <vector>

struct dtNavMesh;
struct dtNavMeshQuery;

class NavMeshRuntime {
public:
    bool Build(const std::vector<float> &vertices, const std::vector<int> &indices);
    [[nodiscard]] bool IsReady() const;
    [[nodiscard]] std::string GetStatus() const;
    [[nodiscard]] const std::vector<float> &GetDebugVertices() const;
    [[nodiscard]] const std::vector<int> &GetDebugIndices() const;
    bool FindPath(const glm::vec3 &start, const glm::vec3 &end, std::vector<glm::vec3> &outPath) const;
    ~NavMeshRuntime();

private:
    static std::array<float, 3> ToDetourVector(const glm::vec3 &value);
    void Reset();

    dtNavMesh *navMesh = nullptr;
    dtNavMeshQuery *navQuery = nullptr;
    std::string status = "NavMesh: Not initialized";
    std::vector<float> debugVertices;
    std::vector<int> debugIndices;
};

#endif
