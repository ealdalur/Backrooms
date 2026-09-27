// ---------------------------------------------------------------------------
// NavGrid.cpp
// ---------------------------------------------------------------------------
#include "AI/NavGrid.h"

#include "Core/Config.h"
#include "World/ChunkManager.h"
#include "World/WorldGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>

namespace {

constexpr float  S = world::kCellSize;
constexpr int    kSearchRadius = 48;    ///< Max cells from the start A* will consider.
constexpr size_t kMaxCachedCells = 32768;

uint64_t cellKey(const glm::ivec2& c) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32) | static_cast<uint32_t>(c.y);
}
glm::ivec2 keyCell(uint64_t k) {
    return {static_cast<int32_t>(static_cast<uint32_t>(k >> 32)), static_cast<int32_t>(static_cast<uint32_t>(k))};
}
int manhattan(const glm::ivec2& a, const glm::ivec2& b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

} // namespace

NavGrid::NavGrid(const WorldGenerator& generator, const ChunkManager& chunks) : m_generator(generator), m_chunks(chunks) {}

glm::ivec2 NavGrid::cellOf(const glm::vec3& p) {
    return {static_cast<int>(std::floor(p.x / S)), static_cast<int>(std::floor(p.z / S))};
}

glm::vec3 NavGrid::cellCenter(const glm::ivec2& cell, int level) {
    return {(static_cast<float>(cell.x) + 0.5f) * S, world::levelFloorY(level), (static_cast<float>(cell.y) + 0.5f) * S};
}

bool NavGrid::walkable(int level, const glm::ivec2& cell) const {
    return m_generator.cellRole(level, cell.x, cell.y) == world::CellRole::Room &&
           m_chunks.isLoadedAt(cellCenter(cell, level), level);
}

world::EdgeType NavGrid::edgeType(int level, int gx, int gz, world::EdgeAxis axis) const {
    return m_generator.edge(level, gx, gz, axis);
}

bool NavGrid::edgeBetween(const glm::ivec2& a, const glm::ivec2& b, int& gx, int& gz, world::EdgeAxis& axis) {
    const glm::ivec2 d = b - a;
    if (d == glm::ivec2(1, 0))  { gx = b.x; gz = b.y; axis = world::EdgeAxis::West;  return true; }
    if (d == glm::ivec2(-1, 0)) { gx = a.x; gz = a.y; axis = world::EdgeAxis::West;  return true; }
    if (d == glm::ivec2(0, 1))  { gx = b.x; gz = b.y; axis = world::EdgeAxis::South; return true; }
    if (d == glm::ivec2(0, -1)) { gx = a.x; gz = a.y; axis = world::EdgeAxis::South; return true; }
    return false;
}

bool NavGrid::canStep(int level, const glm::ivec2& a, const glm::ivec2& b, const NavProfile& profile) const {
    int gx, gz;
    world::EdgeAxis axis;
    if (!edgeBetween(a, b, gx, gz, axis)) return false;
    const world::EdgeType type = m_generator.edge(level, gx, gz, axis);
    if (type == world::EdgeType::Wall) return false;
    if (type == world::EdgeType::Door && !profile.passDoors) return false;
    return walkable(level, b);
}

glm::vec3 NavGrid::crossing(int level, const glm::ivec2& a, const glm::ivec2& b) const {
    int gx, gz;
    world::EdgeAxis axis;
    if (!edgeBetween(a, b, gx, gz, axis)) return cellCenter(b, level);
    const float y = world::levelFloorY(level);
    if (axis == world::EdgeAxis::West) return {static_cast<float>(gx) * S, y, (static_cast<float>(gz) + 0.5f) * S};
    return {(static_cast<float>(gx) + 0.5f) * S, y, static_cast<float>(gz) * S};
}

bool NavGrid::findPath(int level, const glm::ivec2& start, const glm::ivec2& goal, const NavProfile& profile,
                       std::vector<glm::ivec2>& out, int maxExpansions) const {
    struct Node {
        float    g;
        uint64_t parent;
        bool     closed;
    };
    using Entry = std::pair<float, uint64_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    std::unordered_map<uint64_t, Node> nodes;
    nodes.reserve(static_cast<size_t>(maxExpansions) * 2);

    const uint64_t startKey = cellKey(start);
    nodes[startKey] = {0.0f, startKey, false};
    open.push({static_cast<float>(manhattan(start, goal)), startKey});
    uint64_t best = startKey;
    int bestH = manhattan(start, goal);
    bool found = false;

    const glm::ivec2 steps[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int expansions = 0; !open.empty() && expansions < maxExpansions;) {
        const uint64_t k = open.top().second;
        open.pop();
        Node& node = nodes[k];
        if (node.closed) continue;
        node.closed = true;
        ++expansions;

        const glm::ivec2 cell = keyCell(k);
        const int h = manhattan(cell, goal);
        if (h < bestH) {
            bestH = h;
            best = k;
        }
        if (cell == goal) {
            best = k;
            found = true;
            break;
        }
        const float g = node.g;
        for (const glm::ivec2& s : steps) {
            const glm::ivec2 nb = cell + s;
            if (std::abs(nb.x - start.x) > kSearchRadius || std::abs(nb.y - start.y) > kSearchRadius) continue;
            if (!canStep(level, cell, nb, profile)) continue;
            float cost = 1.0f;
            if (profile.darkPreference != 0.0f) cost += profile.darkPreference * cellBrightness(level, nb);
            if (profile.doorCost != 0.0f) {
                int gx, gz;
                world::EdgeAxis axis;
                edgeBetween(cell, nb, gx, gz, axis);
                if (m_generator.edge(level, gx, gz, axis) == world::EdgeType::Door) cost += profile.doorCost;
            }
            if (profile.cellCost) cost += profile.cellCost(nb);
            const uint64_t nk = cellKey(nb);
            auto it = nodes.find(nk);
            if (it != nodes.end() && (it->second.closed || it->second.g <= g + cost)) continue;
            nodes[nk] = {g + cost, k, false};
            open.push({g + cost + static_cast<float>(manhattan(nb, goal)), nk});
        }
    }

    out.clear();
    for (uint64_t k = best;; k = nodes[k].parent) {
        out.push_back(keyCell(k));
        if (k == startKey) break;
    }
    std::reverse(out.begin(), out.end());
    return found;
}

bool NavGrid::edgeBlocksSight(int level, int gx, int gz, world::EdgeAxis axis, float along) const {
    switch (m_generator.edge(level, gx, gz, axis)) {
    case world::EdgeType::Open:
        return false;
    case world::EdgeType::Archway:
        return std::fabs(along - S * 0.5f) > world::kArchWidth * 0.5f - 0.05f;
    case world::EdgeType::Door: {
        const float halfOpening = world::kDoorOpeningWidth * 0.5f - world::kDoorFrameWidth;
        if (std::fabs(along - S * 0.5f) > halfOpening) return true;
        const Door* door = m_chunks.doorOnEdge(level, gx, gz, axis);
        return !door || door->openAmount() < 0.5f; // live state: an open door can be seen through
    }
    case world::EdgeType::Wall:
    default:
        return true;
    }
}

bool NavGrid::lineOfSight(int level, const glm::vec2& a, const glm::vec2& b) const {
    // Crossings of vertical grid lines x = k*S (west edges).
    if (a.x != b.x) {
        const float lo = std::min(a.x, b.x), hi = std::max(a.x, b.x);
        for (int k = static_cast<int>(std::ceil(lo / S)); k <= static_cast<int>(std::floor(hi / S)); ++k) {
            const float x = static_cast<float>(k) * S;
            if (x <= lo || x >= hi) continue;
            const float z = a.y + (x - a.x) / (b.x - a.x) * (b.y - a.y);
            const int gz = static_cast<int>(std::floor(z / S));
            if (edgeBlocksSight(level, k, gz, world::EdgeAxis::West, z - static_cast<float>(gz) * S)) return false;
        }
    }
    // Crossings of horizontal grid lines z = k*S (south edges).
    if (a.y != b.y) {
        const float lo = std::min(a.y, b.y), hi = std::max(a.y, b.y);
        for (int k = static_cast<int>(std::ceil(lo / S)); k <= static_cast<int>(std::floor(hi / S)); ++k) {
            const float z = static_cast<float>(k) * S;
            if (z <= lo || z >= hi) continue;
            const float x = a.x + (z - a.y) / (b.y - a.y) * (b.x - a.x);
            const int gx = static_cast<int>(std::floor(x / S));
            if (edgeBlocksSight(level, gx, k, world::EdgeAxis::South, x - static_cast<float>(gx) * S)) return false;
        }
    }
    return true;
}

float NavGrid::brightnessAt(int level, const glm::vec3& p) const {
    // The same attenuation the world shader applies, driven by each tube's
    // mean output instead of its instantaneous flicker.
    const float range = world::kLightRange;
    const ChunkCoord c = ChunkCoord::fromWorld(p.x, p.z, level);
    float sum = 0.0f;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const Chunk* chunk = m_chunks.chunkAt({c.x + dx, c.z + dz, level});
            if (!chunk) continue;
            for (const LightFixture& light : chunk->lights()) {
                const glm::vec3 d = light.center() - p;
                const float dist = glm::length(d);
                if (dist >= range || dist < 1e-3f) continue;
                if (m_generator.isLightBlocked(level, glm::vec2(light.center().x, light.center().z), glm::vec2(p.x, p.z))) continue;
                const float r = dist / range;
                const float window = std::clamp(1.0f - r * r * r * r, 0.0f, 1.0f);
                const float downward = std::clamp(d.y / dist * 5.0f, 0.0f, 1.0f);
                sum += light.averageIntensity() * window * window / (dist * dist + 0.8f) * downward;
            }
        }
    }
    return sum * cfg::kLightPower;
}

float NavGrid::cellBrightness(int level, const glm::ivec2& cell) const {
    const uint64_t key = rnd::hashCombine(cellKey(cell), static_cast<uint32_t>(level));
    auto it = m_brightness.find(key);
    if (it != m_brightness.end()) return it->second;

    const glm::vec3 p = cellCenter(cell, level) + glm::vec3(0.0f, 1.2f, 0.0f);
    const float b = brightnessAt(level, p);
    // Only cache once every chunk that can light the cell is present.
    const ChunkCoord c = ChunkCoord::fromWorld(p.x, p.z, level);
    bool complete = true;
    for (int dz = -1; dz <= 1 && complete; ++dz) {
        for (int dx = -1; dx <= 1 && complete; ++dx) complete = m_chunks.chunkAt({c.x + dx, c.z + dz, level}) != nullptr;
    }
    if (complete) {
        if (m_brightness.size() > kMaxCachedCells) m_brightness.clear();
        m_brightness.emplace(key, b);
    }
    return b;
}
