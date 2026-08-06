#include "navmesh_runtime.hpp"

#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace {

constexpr int MAX_QUERY_NODES = 2048;
constexpr int MAX_PATH_POLYGONS = 256;
constexpr int MAX_STRAIGHT_PATH_POINTS = 256;
constexpr std::array<float, 3> NEAREST_POLY_HALF_EXTENTS = {2.0f, 4.0f, 2.0f};

class BuildContext final : public rcContext {
public:
    BuildContext()
    {
        enableLog(true);
        resetLog();
    }

    std::string LastLog() const
    {
        if (messages.empty()) {
            return {};
        }
        return messages.back();
    }

protected:
    void doResetLog() override
    {
        messages.clear();
    }

    void doLog(const rcLogCategory, const char *msg, const int len) override
    {
        messages.emplace_back(msg, static_cast<size_t>(len));
    }

private:
    std::vector<std::string> messages;
};

} // namespace

bool NavMeshRuntime::Build(const std::vector<float> &vertices, const std::vector<int> &indices)
{
    Reset();

    if (vertices.size() < 9 || indices.size() < 3 || (vertices.size() % 3) != 0 || (indices.size() % 3) != 0) {
        status = "NavMesh: Invalid source geometry";
        return false;
    }

    BuildContext context;
    rcConfig config{};
    config.cs = 0.3f;
    config.ch = 0.2f;
    config.walkableSlopeAngle = 45.0f;
    config.walkableHeight = static_cast<int>(std::ceil(2.0f / config.ch));
    config.walkableClimb = static_cast<int>(std::floor(0.9f / config.ch));
    config.walkableRadius = static_cast<int>(std::ceil(0.6f / config.cs));
    config.maxEdgeLen = static_cast<int>(12.0f / config.cs);
    config.maxSimplificationError = 1.3f;
    config.minRegionArea = static_cast<int>(rcSqr(8));
    config.mergeRegionArea = static_cast<int>(rcSqr(20));
    config.maxVertsPerPoly = static_cast<int>(DT_VERTS_PER_POLYGON);
    config.detailSampleDist = 6.0f * config.cs;
    config.detailSampleMaxError = 1.0f * config.ch;
    config.borderSize = config.walkableRadius + 3;

    const int vertexCount = static_cast<int>(vertices.size() / 3);
    const int triangleCount = static_cast<int>(indices.size() / 3);
    rcCalcBounds(vertices.data(), vertexCount, config.bmin, config.bmax);
    rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);
    if (config.width <= 0 || config.height <= 0) {
        status = "NavMesh: Invalid build bounds";
        return false;
    }

    rcHeightfield *heightField = rcAllocHeightfield();
    if (heightField == nullptr) {
        status = "NavMesh: Heightfield allocation failed";
        return false;
    }

    auto freeHeightField = [&]() {
        if (heightField != nullptr) {
            rcFreeHeightField(heightField);
            heightField = nullptr;
        }
    };

    if (!rcCreateHeightfield(&context, *heightField, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch)) {
        freeHeightField();
        status = "NavMesh: Heightfield creation failed";
        return false;
    }

    std::vector<int> rasterIndices = indices;
    std::vector<unsigned char> triangleAreas(static_cast<size_t>(triangleCount), 0);
    rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, vertices.data(), vertexCount, rasterIndices.data(), triangleCount, triangleAreas.data());

    const auto walkableTriangleCount = static_cast<int>(std::count_if(
        triangleAreas.begin(),
        triangleAreas.end(),
        [](const unsigned char area) { return area != RC_NULL_AREA; }));
    if (walkableTriangleCount == 0) {
        for (int triangle = 0; triangle < triangleCount; ++triangle) {
            const int baseIndex = triangle * 3;
            std::swap(rasterIndices[baseIndex + 1], rasterIndices[baseIndex + 2]);
        }
        std::fill(triangleAreas.begin(), triangleAreas.end(), 0);
        rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, vertices.data(), vertexCount, rasterIndices.data(), triangleCount, triangleAreas.data());
        if (std::none_of(triangleAreas.begin(), triangleAreas.end(), [](const unsigned char area) { return area != RC_NULL_AREA; })) {
            freeHeightField();
            status = "NavMesh: No walkable triangles (check mesh winding and up-axis)";
            return false;
        }
    }

    if (!rcRasterizeTriangles(&context, vertices.data(), vertexCount, rasterIndices.data(), triangleAreas.data(), triangleCount, *heightField, config.walkableClimb)) {
        freeHeightField();
        status = "NavMesh: Triangle rasterization failed";
        return false;
    }

    rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *heightField);
    rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *heightField);
    rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *heightField);

    rcCompactHeightfield *compactHeightfield = rcAllocCompactHeightfield();
    if (compactHeightfield == nullptr) {
        freeHeightField();
        status = "NavMesh: Compact heightfield allocation failed";
        return false;
    }

    auto freeCompactHeightfield = [&]() {
        if (compactHeightfield != nullptr) {
            rcFreeCompactHeightfield(compactHeightfield);
            compactHeightfield = nullptr;
        }
    };

    if (!rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *heightField, *compactHeightfield)) {
        freeCompactHeightfield();
        freeHeightField();
        status = "NavMesh: Compact heightfield build failed";
        return false;
    }

    freeHeightField();

    if (!rcErodeWalkableArea(&context, config.walkableRadius, *compactHeightfield)) {
        freeCompactHeightfield();
        status = "NavMesh: Walkable area erosion failed";
        return false;
    }

    if (!rcBuildDistanceField(&context, *compactHeightfield) || !rcBuildRegions(&context, *compactHeightfield, config.borderSize, config.minRegionArea, config.mergeRegionArea)) {
        freeCompactHeightfield();
        status = "NavMesh: Region build failed";
        return false;
    }

    rcContourSet *contours = rcAllocContourSet();
    if (contours == nullptr) {
        freeCompactHeightfield();
        status = "NavMesh: Contour allocation failed";
        return false;
    }

    auto freeContours = [&]() {
        if (contours != nullptr) {
            rcFreeContourSet(contours);
            contours = nullptr;
        }
    };

    if (!rcBuildContours(&context, *compactHeightfield, config.maxSimplificationError, config.maxEdgeLen, *contours)) {
        freeContours();
        freeCompactHeightfield();
        status = "NavMesh: Contour build failed";
        return false;
    }

    rcPolyMesh *polyMesh = rcAllocPolyMesh();
    if (polyMesh == nullptr) {
        freeContours();
        freeCompactHeightfield();
        status = "NavMesh: Poly mesh allocation failed";
        return false;
    }

    auto freePolyMesh = [&]() {
        if (polyMesh != nullptr) {
            rcFreePolyMesh(polyMesh);
            polyMesh = nullptr;
        }
    };

    if (!rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *polyMesh)) {
        freePolyMesh();
        freeContours();
        freeCompactHeightfield();
        status = "NavMesh: Poly mesh build failed";
        return false;
    }

    rcPolyMeshDetail *detailMesh = rcAllocPolyMeshDetail();
    if (detailMesh == nullptr) {
        freePolyMesh();
        freeContours();
        freeCompactHeightfield();
        status = "NavMesh: Detail mesh allocation failed";
        return false;
    }

    auto freeDetailMesh = [&]() {
        if (detailMesh != nullptr) {
            rcFreePolyMeshDetail(detailMesh);
            detailMesh = nullptr;
        }
    };

    if (!rcBuildPolyMeshDetail(&context, *polyMesh, *compactHeightfield, config.detailSampleDist, config.detailSampleMaxError, *detailMesh)) {
        freeDetailMesh();
        freePolyMesh();
        freeContours();
        freeCompactHeightfield();
        status = "NavMesh: Detail mesh build failed";
        return false;
    }

    freeContours();
    freeCompactHeightfield();

    if (polyMesh->nverts <= 0 || polyMesh->npolys <= 0) {
        freeDetailMesh();
        freePolyMesh();
        status = "NavMesh: No walkable polygons generated";
        return false;
    }

    if (polyMesh->nvp > DT_VERTS_PER_POLYGON) {
        freeDetailMesh();
        freePolyMesh();
        status = "NavMesh: Polygon vertex limit exceeds Detour configuration";
        return false;
    }

    for (int polygon = 0; polygon < polyMesh->npolys; ++polygon) {
        const unsigned char area = polyMesh->areas[polygon];
        if (area == RC_WALKABLE_AREA) {
            polyMesh->areas[polygon] = 0;
        }
        if (area != RC_NULL_AREA) {
            polyMesh->flags[polygon] = 1;
        }
    }

    dtNavMeshCreateParams params{};
    params.verts = polyMesh->verts;
    params.vertCount = polyMesh->nverts;
    params.polys = polyMesh->polys;
    params.polyAreas = polyMesh->areas;
    params.polyFlags = polyMesh->flags;
    params.polyCount = polyMesh->npolys;
    params.nvp = polyMesh->nvp;
    params.detailMeshes = detailMesh->meshes;
    params.detailVerts = detailMesh->verts;
    params.detailVertsCount = detailMesh->nverts;
    params.detailTris = detailMesh->tris;
    params.detailTriCount = detailMesh->ntris;
    params.walkableHeight = 2.0f;
    params.walkableRadius = 0.6f;
    params.walkableClimb = 0.9f;
    rcVcopy(params.bmin, polyMesh->bmin);
    rcVcopy(params.bmax, polyMesh->bmax);
    params.cs = config.cs;
    params.ch = config.ch;
    params.buildBvTree = true;
    params.tileX = 0;
    params.tileY = 0;
    params.tileLayer = 0;
    params.userId = 1;

    unsigned char *navData = nullptr;
    int navDataSize = 0;
    if (!dtCreateNavMeshData(&params, &navData, &navDataSize)) {
        freeDetailMesh();
        freePolyMesh();
        status = "NavMesh: Detour data creation failed (nvp=" + std::to_string(params.nvp) +
                 ", dtMax=" + std::to_string(static_cast<int>(DT_VERTS_PER_POLYGON)) +
                 ", verts=" + std::to_string(params.vertCount) +
                 ", polys=" + std::to_string(params.polyCount) + ")";
        return false;
    }

    navMesh = dtAllocNavMesh();
    if (navMesh == nullptr) {
        dtFree(navData);
        freeDetailMesh();
        freePolyMesh();
        status = "NavMesh: dtNavMesh allocation failed";
        return false;
    }

    if (dtStatusFailed(navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA))) {
        dtFree(navData);
        dtFreeNavMesh(navMesh);
        navMesh = nullptr;
        freeDetailMesh();
        freePolyMesh();
        status = "NavMesh: dtNavMesh initialization failed";
        return false;
    }

    navQuery = dtAllocNavMeshQuery();
    if (navQuery == nullptr) {
        freeDetailMesh();
        freePolyMesh();
        Reset();
        status = "NavMesh: dtNavMeshQuery allocation failed";
        return false;
    }

    if (dtStatusFailed(navQuery->init(navMesh, MAX_QUERY_NODES))) {
        freeDetailMesh();
        freePolyMesh();
        Reset();
        status = "NavMesh: dtNavMeshQuery initialization failed";
        return false;
    }

    freeDetailMesh();
    freePolyMesh();
    status = "NavMesh: Ready";
    return true;
}

bool NavMeshRuntime::IsReady() const
{
    return navMesh != nullptr && navQuery != nullptr;
}

std::string NavMeshRuntime::GetStatus() const
{
    return status;
}

bool NavMeshRuntime::FindPath(const glm::vec3 &start, const glm::vec3 &end, std::vector<glm::vec3> &outPath) const
{
    outPath.clear();
    if (!IsReady()) {
        return false;
    }

    dtQueryFilter queryFilter;
    queryFilter.setIncludeFlags(0xffff);
    queryFilter.setExcludeFlags(0);
    const auto halfExtents = NEAREST_POLY_HALF_EXTENTS;

    const std::array<float, 3> startPoint = ToDetourVector(start);
    const std::array<float, 3> endPoint = ToDetourVector(end);

    dtPolyRef startReference = 0;
    dtPolyRef endReference = 0;
    float nearestStart[3] = {};
    float nearestEnd[3] = {};

    if (dtStatusFailed(navQuery->findNearestPoly(startPoint.data(), halfExtents.data(), &queryFilter, &startReference, nearestStart)) || startReference == 0) {
        return false;
    }
    if (dtStatusFailed(navQuery->findNearestPoly(endPoint.data(), halfExtents.data(), &queryFilter, &endReference, nearestEnd)) || endReference == 0) {
        return false;
    }

    std::array<dtPolyRef, MAX_PATH_POLYGONS> polygons{};
    int polygonCount = 0;
    if (dtStatusFailed(navQuery->findPath(startReference, endReference, nearestStart, nearestEnd, &queryFilter, polygons.data(), &polygonCount, static_cast<int>(polygons.size()))) || polygonCount <= 0) {
        return false;
    }

    std::array<float, 3 * MAX_STRAIGHT_PATH_POINTS> straightPath{};
    std::array<unsigned char, MAX_STRAIGHT_PATH_POINTS> straightPathFlags{};
    std::array<dtPolyRef, MAX_STRAIGHT_PATH_POINTS> straightPathPolygons{};
    int straightPathCount = 0;
    if (dtStatusFailed(navQuery->findStraightPath(
            nearestStart,
            nearestEnd,
            polygons.data(),
            polygonCount,
            straightPath.data(),
            straightPathFlags.data(),
            straightPathPolygons.data(),
            &straightPathCount,
            MAX_STRAIGHT_PATH_POINTS))) {
        return false;
    }

    if (straightPathCount <= 0) {
        return false;
    }

    outPath.reserve(static_cast<size_t>(straightPathCount));
    for (int index = 0; index < straightPathCount; ++index) {
        const int offset = index * 3;
        outPath.emplace_back(straightPath[offset], straightPath[offset + 1], straightPath[offset + 2]);
    }

    return true;
}

NavMeshRuntime::~NavMeshRuntime()
{
    Reset();
}

std::array<float, 3> NavMeshRuntime::ToDetourVector(const glm::vec3 &value)
{
    return {value.x, value.y, value.z};
}

void NavMeshRuntime::Reset()
{
    if (navQuery != nullptr) {
        dtFreeNavMeshQuery(navQuery);
        navQuery = nullptr;
    }

    if (navMesh != nullptr) {
        dtFreeNavMesh(navMesh);
        navMesh = nullptr;
    }
}
