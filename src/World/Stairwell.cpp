// ---------------------------------------------------------------------------
// Stairwell.cpp
// ---------------------------------------------------------------------------
#include "World/Stairwell.h"

#include "Render/MeshBuilder.h"

#include <algorithm>
#include <cmath>

namespace stairs {
namespace {

constexpr float S    = world::kCellSize;
constexpr float H    = world::kCeilingHeight;
constexpr float LH   = world::kLevelHeight;
constexpr float HT   = world::kWallHalf;
constexpr float R    = world::kStairRise;
constexpr float T    = world::kStairTread;
constexpr float zL   = world::kStairLobbyDepth;
constexpr int   kPerFlight = world::kStairRisers / 2;                   // 10 risers
constexpr float zTop = zL + static_cast<float>(kPerFlight - 1) * T;    // flights end / landing starts
constexpr float yLand = static_cast<float>(kPerFlight) * R;            // landing height (1.7 m)
constexpr float xA   = S * 0.5f - 0.2f;                                // flight A | divider
constexpr float xB   = S * 0.5f + 0.2f;                                // divider | flight B
constexpr float kRailHeight = 0.9f;

/// Exact rotation by quarter turns (no trigonometric round-off, so rotated
/// boxes stay perfectly axis-aligned).
glm::mat4 quarterTurn(int rotation) {
    static const float kCos[4] = {1.0f, 0.0f, -1.0f, 0.0f};
    static const float kSin[4] = {0.0f, 1.0f, 0.0f, -1.0f};
    const int r = ((rotation % 4) + 4) % 4;
    const float c = kCos[r], s = kSin[r];
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(c, 0.0f, -s, 0.0f); // same convention as glm::rotate about +Y
    m[2] = glm::vec4(s, 0.0f, c, 0.0f);
    return m;
}

/// Local-space vertical frame with +Y along `dir` (for cylinders along a slope).
glm::mat4 alignY(const glm::vec3& origin, const glm::vec3& dir) {
    const glm::vec3 y = glm::normalize(dir);
    const glm::vec3 ref = std::fabs(y.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 x = glm::normalize(glm::cross(y, ref));
    const glm::vec3 z = glm::cross(x, y);
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(x, 0.0f);
    m[1] = glm::vec4(y, 0.0f);
    m[2] = glm::vec4(z, 0.0f);
    m[3] = glm::vec4(origin, 1.0f);
    return m;
}

glm::vec2 toWorldXZ(int gx, int gz, int rotation, float lx, float lz) {
    const glm::vec4 p = cellTransform(gx, gz, 0, rotation) * glm::vec4(lx, 0.0f, lz, 1.0f);
    return {p.x, p.z};
}

/// Appends a local-space box (transformed into the world) and optionally its collider.
struct Builder {
    MeshData&          mesh;
    std::vector<AABB>& colliders;
    glm::mat4          toWorld;

    void add(const glm::vec3& mn, const glm::vec3& mx, MaterialId mat, uint8_t faces, bool collide) {
        mesh::BoxDesc d;
        d.min = mn;
        d.max = mx;
        d.material = mat;
        d.faces = faces;
        d.transform = toWorld; // UVs come from cell-local positions: tile sizes divide the cell
        mesh::addBox(mesh, d);
        if (collide) colliders.push_back(AABB(mn, mx).transformed(toWorld));
    }
    void collider(const glm::vec3& mn, const glm::vec3& mx) { colliders.push_back(AABB(mn, mx).transformed(toWorld)); }
};

} // namespace

int entranceSide(int rotation) {
    static const int kSide[4] = {2, 0, 3, 1}; // local south rotated: south, west, north, east
    return kSide[((rotation % 4) + 4) % 4];
}

glm::mat4 cellTransform(int gx, int gz, int lowerLevel, int rotation) {
    const glm::vec3 centre((static_cast<float>(gx) + 0.5f) * S, world::levelFloorY(lowerLevel),
                           (static_cast<float>(gz) + 0.5f) * S);
    glm::mat4 toCentre(1.0f);
    toCentre[3] = glm::vec4(-S * 0.5f, 0.0f, -S * 0.5f, 1.0f);
    glm::mat4 place(1.0f);
    place[3] = glm::vec4(centre, 1.0f);
    return place * quarterTurn(rotation) * toCentre;
}

glm::vec4 holeRect(int gx, int gz, int rotation) {
    const glm::vec2 a = toWorldXZ(gx, gz, rotation, 0.0f, zL);
    const glm::vec2 b = toWorldXZ(gx, gz, rotation, S, S);
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

void build(MeshData& mesh, std::vector<AABB>& colliders, int gx, int gz, int lowerLevel, int rotation) {
    using namespace mesh;
    const glm::mat4 toWorld = cellTransform(gx, gz, lowerLevel, rotation);
    Builder b{mesh, colliders, toWorld};
    const MaterialId concrete = MaterialId::Concrete;
    const MaterialId metal = MaterialId::GrayMetal;

    // Steel nosing strip along the front edge of a tread (visual only).
    auto nosing = [&](float x0, float x1, float top, float edgeZ, float dirZ) {
        const float z0 = dirZ > 0.0f ? edgeZ - 0.004f : edgeZ - 0.04f;
        const float z1 = dirZ > 0.0f ? edgeZ + 0.04f : edgeZ + 0.004f;
        b.add({x0 + 0.02f, top - 0.012f, z0}, {x1 - 0.02f, top + 0.002f, z1}, metal,
              FacePosY | FacePosX | FaceNegX | (dirZ > 0.0f ? FaceNegZ : FacePosZ), false);
    };

    // ---- Flight A: up along +z on the west half. Each step is a slab running
    //      from its riser to the landing, so treads and risers are the only
    //      exposed faces and nothing overlaps coplanar.
    for (int i = 0; i < kPerFlight - 1; ++i) {
        const float y0 = static_cast<float>(i) * R, y1 = y0 + R;
        const float front = zL + static_cast<float>(i) * T;
        b.add({HT, y0, front}, {xA, y1, zTop}, concrete, FacePosY | FaceNegZ, true);
        nosing(HT, xA, y1, front, 1.0f);
    }

    // ---- Half-way landing across the full width, and the solid mass under flight B.
    b.add({HT, 0.0f, zTop}, {S - HT, yLand, S - HT}, concrete, FacePosY | FaceNegZ, true);
    nosing(HT, xA, yLand, zTop, 1.0f);
    b.add({xB, 0.0f, zL}, {S - HT, yLand, zTop}, concrete, FaceNegZ, true);

    // ---- Flight B: up along -z on the east half, arriving at the upper lobby.
    for (int j = 0; j < kPerFlight - 1; ++j) {
        const float y0 = yLand + static_cast<float>(j) * R, y1 = y0 + R;
        const float front = zTop - static_cast<float>(j) * T;
        b.add({xB, y0, zL}, {S - HT, y1, front}, concrete, FacePosY | FacePosZ | FaceNegZ, true);
        nosing(xB, S - HT, y1, front, -1.0f);
    }

    // ---- Divider wall between the flights, floor of L to ceiling of L + 1.
    b.add({xA, 0.0f, zL}, {xB, LH + H, zTop}, MaterialId::Wallpaper, FaceSides, true);

    // ---- Exposed slab: the edge facing the shaft and bands continuing the
    //      cell walls through the slab thickness.
    b.add({0.0f, H, zL - 0.12f}, {S, LH, zL}, concrete, FacePosZ, false);
    b.add({-HT, H, zL}, {HT, LH, S}, concrete, FacePosX, true);
    b.add({S - HT, H, zL}, {S + HT, LH, S}, concrete, FaceNegX, true);
    b.add({0.0f, H, S - HT}, {S, LH, S + HT}, concrete, FaceNegZ, true);

    // ---- Guard wall on the upper storey above the open end of flight A.
    b.add({HT, LH, zL}, {xA, LH + 1.0f, zL + 0.1f}, MaterialId::Wallpaper, FaceAll & ~FaceNegY, false);
    b.add({HT, LH + 1.0f, zL - 0.02f}, {xA, LH + 1.04f, zL + 0.12f}, metal, FaceAll & ~FaceNegY, false);
    b.collider({HT, LH, zL - 0.02f}, {xA, LH + 1.04f, zL + 0.12f});

    // ---- Handrails along both outer walls, parallel to the nosing line.
    auto rail = [&](const glm::vec3& from, const glm::vec3& to, float wallX) {
        const glm::vec3 d = to - from;
        const glm::mat4 m = toWorld * alignY(from, d);
        addCylinder(mesh, m, 0.022f, 0.0f, glm::length(d), 10, metal);
        for (float t : {0.1f, 0.5f, 0.9f}) { // wall brackets
            const glm::vec3 p = from + d * t;
            const float x0 = std::min(wallX, p.x), x1 = std::max(wallX, p.x);
            b.add({x0, p.y - 0.06f, p.z - 0.012f}, {x1, p.y - 0.015f, p.z + 0.012f}, metal, FaceAll, false);
        }
    };
    rail({HT + 0.06f, R + kRailHeight, zL}, {HT + 0.06f, yLand + kRailHeight, zTop}, HT);
    rail({S - HT - 0.06f, yLand + R + kRailHeight, zTop}, {S - HT - 0.06f, LH + kRailHeight, zL}, S - HT);
}

FixtureSpot lobbyFixture(int gx, int gz, int rotation) {
    // Over the foot of flight A rather than the doorway: an open door would
    // otherwise sit right under the tube.
    return {toWorldXZ(gx, gz, rotation, (HT + xA) * 0.5f, (HT + zL) * 0.5f), (rotation & 1) == 0};
}

FixtureSpot shaftFixture(int gx, int gz, int rotation) {
    return {toWorldXZ(gx, gz, rotation, S * 0.5f, (zTop + S - HT) * 0.5f), (rotation & 1) == 0};
}

std::vector<glm::vec3> climbRoute(int gx, int gz, int lowerLevel, int rotation) {
    const glm::vec3 local[] = {
        {S * 0.5f, 0.0f, -1.2f},             // outside the lower entrance
        {S * 0.5f, 0.0f, 0.5f},              // through the doorway
        {1.2f, 0.0f, 1.0f},                  // foot of flight A
        {1.2f, 9.0f * R, zTop - 0.1f},       // top of flight A
        {1.2f, yLand, (zTop + S) * 0.5f},    // landing, west
        {3.8f, yLand, (zTop + S) * 0.5f},    // landing, east
        {3.8f, yLand + R, zTop - 0.15f},     // foot of flight B
        {3.8f, LH - R, zL + 0.2f},           // top of flight B
        {3.8f, LH, 0.7f},                    // upper lobby
        {S * 0.5f, LH, 0.5f},                // upper doorway
        {S * 0.5f, LH, -1.2f},               // outside the upper entrance
    };
    const glm::mat4 m = cellTransform(gx, gz, lowerLevel, rotation);
    std::vector<glm::vec3> out;
    for (const glm::vec3& p : local) out.emplace_back(m * glm::vec4(p, 1.0f));
    return out;
}

} // namespace stairs
