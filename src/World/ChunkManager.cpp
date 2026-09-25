// ---------------------------------------------------------------------------
// ChunkManager.cpp
// ---------------------------------------------------------------------------
#include "World/ChunkManager.h"

#include "Core/Config.h"
#include "World/WorldGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

ChunkManager::ChunkManager(const WorldGenerator& generator, int loadRadius, int buildBudget)
    : m_generator(generator), m_loadRadius(loadRadius), m_buildBudget(buildBudget) {}

void ChunkManager::loadChunk(const ChunkCoord& c) {
    auto chunk = std::make_unique<Chunk>(m_generator.generate(c));
    // Restore doors the player previously opened in this chunk.
    for (Door& d : chunk->doors()) {
        auto it = m_doorMemory.find(d.id());
        if (it != m_doorMemory.end()) d.restoreState(it->second);
    }
    m_chunks.emplace(c, std::move(chunk));
    ++m_version;
}

void ChunkManager::update(const glm::vec3& focus, float dt, const AABB& playerBox, bool loadEverything) {
    const ChunkCoord center = ChunkCoord::fromWorld(focus.x, focus.z);

    // ---- Unload chunks beyond the radius (+1 hysteresis to avoid thrashing).
    for (auto it = m_chunks.begin(); it != m_chunks.end();) {
        const int dx = std::abs(it->first.x - center.x);
        const int dz = std::abs(it->first.z - center.z);
        if (std::max(dx, dz) > m_loadRadius + 1) {
            for (const Door& d : it->second->doors()) {
                const int s = d.persistentState();
                if (s != 0) m_doorMemory[d.id()] = s;
                else        m_doorMemory.erase(d.id());
            }
            it = m_chunks.erase(it);
            ++m_version;
        } else {
            ++it;
        }
    }

    // ---- Collect missing chunks, nearest first.
    std::vector<ChunkCoord> missing;
    for (int dz = -m_loadRadius; dz <= m_loadRadius; ++dz) {
        for (int dx = -m_loadRadius; dx <= m_loadRadius; ++dx) {
            const ChunkCoord c{center.x + dx, center.z + dz};
            if (m_chunks.find(c) == m_chunks.end()) missing.push_back(c);
        }
    }
    std::sort(missing.begin(), missing.end(), [&center](const ChunkCoord& a, const ChunkCoord& b) {
        const int da = (a.x - center.x) * (a.x - center.x) + (a.z - center.z) * (a.z - center.z);
        const int db = (b.x - center.x) * (b.x - center.x) + (b.z - center.z) * (b.z - center.z);
        return da != db ? da < db : a < b;
    });

    const size_t budget = loadEverything ? missing.size() : static_cast<size_t>(m_buildBudget);
    const size_t toBuild = std::min(budget, missing.size());
    for (size_t i = 0; i < toBuild; ++i) loadChunk(missing[i]);
    m_pending = missing.size() - toBuild;

    // ---- Animate doors and collect their events for the soundscape.
    m_doorEvents.clear();
    for (auto& kv : m_chunks) {
        kv.second->update(dt, playerBox);
        for (Door& d : kv.second->doors()) {
            if (const uint8_t flags = d.takeEvents()) m_doorEvents.push_back({flags, d.doorwayCenter()});
        }
    }
}

void ChunkManager::gatherColliders(const AABB& region, std::vector<AABB>& out) const {
    const ChunkCoord c0 = ChunkCoord::fromWorld(region.min.x - 1.0f, region.min.z - 1.0f);
    const ChunkCoord c1 = ChunkCoord::fromWorld(region.max.x + 1.0f, region.max.z + 1.0f);
    for (int z = c0.z; z <= c1.z; ++z) {
        for (int x = c0.x; x <= c1.x; ++x) {
            auto it = m_chunks.find({x, z});
            if (it != m_chunks.end()) it->second->gatherColliders(region, out);
        }
    }
}

Door* ChunkManager::findDoorInternal(const glm::vec3& eye, const glm::vec3& forward) const {
    Door* best = nullptr;
    float bestScore = -1e9f;
    const ChunkCoord c0 = ChunkCoord::fromWorld(eye.x - cfg::kDoorInteractDistance, eye.z - cfg::kDoorInteractDistance);
    const ChunkCoord c1 = ChunkCoord::fromWorld(eye.x + cfg::kDoorInteractDistance, eye.z + cfg::kDoorInteractDistance);
    for (int z = c0.z; z <= c1.z; ++z) {
        for (int x = c0.x; x <= c1.x; ++x) {
            auto it = m_chunks.find({x, z});
            if (it == m_chunks.end()) continue;
            for (Door& d : it->second->doors()) {
                // Score by how directly the player looks at the panel centre and how close it is.
                const glm::vec3 target = d.center();
                const glm::vec3 toDoor = target - eye;
                const float dist = glm::length(toDoor);
                if (dist > cfg::kDoorInteractDistance || dist < 1e-4f) continue;
                const float facing = glm::dot(toDoor / dist, forward);
                if (facing < 0.35f && dist > 0.9f) continue; // must look roughly at it unless very close
                const float score = facing * 2.0f - dist;
                if (score > bestScore) {
                    bestScore = score;
                    best = &d;
                }
            }
        }
    }
    return best;
}

const Door* ChunkManager::findInteractableDoor(const glm::vec3& eye, const glm::vec3& forward) const {
    return findDoorInternal(eye, forward);
}

bool ChunkManager::interact(const glm::vec3& eye, const glm::vec3& forward, const glm::vec3& playerFeet) {
    Door* d = findDoorInternal(eye, forward);
    if (!d) return false;
    d->toggle(playerFeet);
    return true;
}

glm::vec3 ChunkManager::findSpawnPoint(const BodyShape& shape, const Physics& physics) const {
    // Spiral outward over cell centres (and cell quarter points) until free.
    const float S = world::kCellSize;
    const glm::vec2 offsets[5] = {{0.5f, 0.5f}, {0.25f, 0.25f}, {0.75f, 0.25f}, {0.75f, 0.75f}, {0.25f, 0.75f}};
    for (int ring = 0; ring < 8; ++ring) {
        for (int dz = -ring; dz <= ring; ++dz) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) != ring) continue;
                for (const glm::vec2& o : offsets) {
                    const glm::vec3 p((static_cast<float>(dx) + o.x) * S + S * 2.0f, 0.0f,
                                      (static_cast<float>(dz) + o.y) * S + S * 2.0f);
                    if (physics.isFree(Physics::bodyBox(p, shape).expanded(0.05f), *this)) return p;
                }
            }
        }
    }
    return glm::vec3(S * 2.5f, 0.0f, S * 2.5f);
}

const Chunk* ChunkManager::chunkAt(const ChunkCoord& c) const {
    auto it = m_chunks.find(c);
    return it != m_chunks.end() ? it->second.get() : nullptr;
}

std::vector<const Chunk*> ChunkManager::sortedChunks() const {
    std::vector<const Chunk*> out;
    out.reserve(m_chunks.size());
    for (const auto& kv : m_chunks) out.push_back(kv.second.get());
    std::sort(out.begin(), out.end(), [](const Chunk* a, const Chunk* b) { return a->coord() < b->coord(); });
    return out;
}

std::vector<Chunk*> ChunkManager::sortedChunksMutable() {
    std::vector<Chunk*> out;
    out.reserve(m_chunks.size());
    for (auto& kv : m_chunks) out.push_back(kv.second.get());
    std::sort(out.begin(), out.end(), [](const Chunk* a, const Chunk* b) { return a->coord() < b->coord(); });
    return out;
}
