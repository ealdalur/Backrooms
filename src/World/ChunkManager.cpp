// ---------------------------------------------------------------------------
// ChunkManager.cpp
// ---------------------------------------------------------------------------
#include "World/ChunkManager.h"

#include "Core/Config.h"
#include "World/WorldGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

ChunkManager::ChunkManager(const WorldGenerator& generator, int loadRadius, int adjacentRadius, int buildBudget)
    : m_generator(generator), m_loadRadius(loadRadius), m_adjacentRadius(adjacentRadius), m_buildBudget(buildBudget) {}

int ChunkManager::radiusFor(int levelOffset) const {
    if (levelOffset == 0) return m_loadRadius;
    return std::abs(levelOffset) == 1 ? m_adjacentRadius : -1;
}

void ChunkManager::loadChunk(const ChunkCoord& c) {
    auto chunk = std::make_unique<Chunk>(m_generator.generate(c));
    // Restore doors the player opened, terminals switched on / off and messages heard here.
    for (Door& d : chunk->doors()) {
        auto it = m_doorMemory.find(d.id());
        if (it != m_doorMemory.end()) d.restoreState(it->second);
    }
    for (Terminal& t : chunk->terminals()) {
        auto it = m_terminalMemory.find(t.id());
        if (it != m_terminalMemory.end()) t.setPowered(it->second);
    }
    for (Phone& p : chunk->phones()) {
        auto it = m_phoneMemory.find(p.id());
        if (it != m_phoneMemory.end()) p.setMessageWaiting(it->second);
    }
    // Parts taken away stay gone; parts swapped in stay where they were left (with their charge).
    for (ItemSite& site : chunk->items()) {
        auto it = m_itemMemory.find(site.id);
        if (it == m_itemMemory.end()) continue;
        site.item = it->second;
        site.modified = true;
    }
    m_chunks.emplace(c, std::move(chunk));
    ++m_version;
}

void ChunkManager::rememberState(const Chunk& chunk) {
    for (const Door& d : chunk.doors()) {
        const int s = d.persistentState();
        if (s != 0) m_doorMemory[d.id()] = s;
        else        m_doorMemory.erase(d.id());
    }
    for (const Terminal& t : chunk.terminals()) m_terminalMemory[t.id()] = t.powered();
    for (const Phone& p : chunk.phones()) {
        if (p.hasMessage()) m_phoneMemory[p.id()] = p.messageWaiting();
    }
    for (const ItemSite& site : chunk.items()) {
        if (site.modified) m_itemMemory[site.id] = site.item;
    }
}

void ChunkManager::update(const glm::vec3& focus, int focusLevel, float dt, const AABB& playerBox, bool loadEverything) {
    const ChunkCoord center = ChunkCoord::fromWorld(focus.x, focus.z, focusLevel);

    // ---- Unload chunks beyond their storey's radius (+1 hysteresis to avoid thrashing).
    for (auto it = m_chunks.begin(); it != m_chunks.end();) {
        const int dl = it->first.level - focusLevel;
        const int dx = std::abs(it->first.x - center.x);
        const int dz = std::abs(it->first.z - center.z);
        const int keep = std::abs(dl) <= 1 ? radiusFor(dl) + 1 : -1;
        if (std::max(dx, dz) > keep) {
            rememberState(*it->second);
            it = m_chunks.erase(it);
            ++m_version;
        } else {
            ++it;
        }
    }

    // ---- Collect missing chunks, nearest first (the focus storey slightly ahead).
    std::vector<ChunkCoord> missing;
    for (int dl = -1; dl <= 1; ++dl) {
        const int r = radiusFor(dl);
        for (int dz = -r; dz <= r; ++dz) {
            for (int dx = -r; dx <= r; ++dx) {
                const ChunkCoord c{center.x + dx, center.z + dz, focusLevel + dl};
                if (m_chunks.find(c) == m_chunks.end()) missing.push_back(c);
            }
        }
    }
    auto priority = [&center, focusLevel](const ChunkCoord& c) {
        const int dx = c.x - center.x, dz = c.z - center.z;
        return dx * dx + dz * dz + 2 * std::abs(c.level - focusLevel);
    };
    std::sort(missing.begin(), missing.end(), [&](const ChunkCoord& a, const ChunkCoord& b) {
        const int pa = priority(a), pb = priority(b);
        return pa != pb ? pa < pb : a < b;
    });

    const size_t budget = loadEverything ? missing.size() : static_cast<size_t>(m_buildBudget);
    const size_t toBuild = std::min(budget, missing.size());
    for (size_t i = 0; i < toBuild; ++i) loadChunk(missing[i]);
    m_pending = missing.size() - toBuild;

    // ---- Animate doors and collect their events for the soundscape.
    m_doorEvents.clear();
    m_cabinetEvents.clear();
    for (auto& kv : m_chunks) {
        kv.second->update(dt, playerBox);
        for (Door& d : kv.second->doors()) {
            if (const uint8_t flags = d.takeEvents()) m_doorEvents.push_back({flags, d.doorwayCenter()});
        }
        for (FileCabinet& c : kv.second->cabinets()) {
            if (const uint8_t flags = c.takeEvents()) m_cabinetEvents.push_back({flags, c.frontCenter()});
        }
    }
}

void ChunkManager::reload(const ChunkCoord& c) {
    auto it = m_chunks.find(c);
    if (it == m_chunks.end()) return;
    rememberState(*it->second);
    m_chunks.erase(it);
    loadChunk(c);
}

void ChunkManager::clear() {
    m_chunks.clear();
    m_doorMemory.clear();
    m_terminalMemory.clear();
    m_phoneMemory.clear();
    m_itemMemory.clear();
    m_doorEvents.clear();
    m_cabinetEvents.clear();
    ++m_version;
}

bool ChunkManager::touchesGlitch(const AABB& box) const {
    const int level = world::levelOf(box.min.y);
    const ChunkCoord c0 = ChunkCoord::fromWorld(box.min.x - 1.0f, box.min.z - 1.0f, level);
    const ChunkCoord c1 = ChunkCoord::fromWorld(box.max.x + 1.0f, box.max.z + 1.0f, level);
    for (int z = c0.z; z <= c1.z; ++z) {
        for (int x = c0.x; x <= c1.x; ++x) {
            auto it = m_chunks.find({x, z, level});
            if (it == m_chunks.end()) continue;
            for (const AABB& g : it->second->glitchZones()) {
                if (box.min.x < g.max.x && box.max.x > g.min.x && box.min.y < g.max.y && box.max.y > g.min.y &&
                    box.min.z < g.max.z && box.max.z > g.min.z) {
                    return true;
                }
            }
        }
    }
    return false;
}

void ChunkManager::gatherColliders(const AABB& region, std::vector<AABB>& out) const {
    gatherColliders(region, out, true);
}

void ChunkManager::gatherColliders(const AABB& region, std::vector<AABB>& out, bool includeDoors) const {
    // A storey's chunk also owns stair geometry reaching into the storey
    // above, hence the extra level below the region.
    const int l0 = world::levelOf(region.min.y) - 1;
    const int l1 = world::levelOf(region.max.y);
    const ChunkCoord c0 = ChunkCoord::fromWorld(region.min.x - 1.0f, region.min.z - 1.0f, 0);
    const ChunkCoord c1 = ChunkCoord::fromWorld(region.max.x + 1.0f, region.max.z + 1.0f, 0);
    for (int l = l0; l <= l1; ++l) {
        for (int z = c0.z; z <= c1.z; ++z) {
            for (int x = c0.x; x <= c1.x; ++x) {
                auto it = m_chunks.find({x, z, l});
                if (it != m_chunks.end()) it->second->gatherColliders(region, out, includeDoors);
            }
        }
    }
}

Interactable ChunkManager::findInteractable(const glm::vec3& eye, const glm::vec3& forward) const {
    Interactable best;
    float bestScore = -1e9f;
    const float reach = cfg::kDoorInteractDistance;
    const int eyeLevel = world::levelOf(eye.y);
    const ChunkCoord c0 = ChunkCoord::fromWorld(eye.x - reach, eye.z - reach, 0);
    const ChunkCoord c1 = ChunkCoord::fromWorld(eye.x + reach, eye.z + reach, 0);

    // Score by how directly the player looks at the target and how close it is.
    auto consider = [&](const glm::vec3& target, float minFacing, auto&& assign) {
        const glm::vec3 toTarget = target - eye;
        const float dist = glm::length(toTarget);
        if (dist > reach || dist < 1e-4f) return;
        const float facing = glm::dot(toTarget / dist, forward);
        if (facing < minFacing && dist > 0.9f) return; // must look roughly at it unless very close
        const float score = facing * 2.0f - dist;
        if (score > bestScore) {
            bestScore = score;
            assign();
        }
    };

    for (int l = eyeLevel - 1; l <= eyeLevel; ++l) {
        for (int z = c0.z; z <= c1.z; ++z) {
            for (int x = c0.x; x <= c1.x; ++x) {
                auto it = m_chunks.find({x, z, l});
                if (it == m_chunks.end()) continue;
                for (Door& d : it->second->doors()) {
                    consider(d.center(), 0.35f, [&] { best = {Interactable::Kind::Door, &d, nullptr}; });
                }
                for (Terminal& t : it->second->terminals()) {
                    // Only from in front of the screen, and it must be looked at.
                    if (glm::dot(t.screenNormal(), eye - t.screenCenter()) <= 0.05f) continue;
                    consider(t.screenCenter(), 0.55f, [&] { best = {Interactable::Kind::Terminal, nullptr, &t}; });
                }
                for (Phone& p : it->second->phones()) {
                    consider(p.center(), 0.6f, [&] { best = {Interactable::Kind::Phone, nullptr, nullptr, &p}; });
                }
                for (FileCabinet& c : it->second->cabinets()) {
                    // Only from in front, where the drawers pull out.
                    if (glm::dot(c.frontNormal(), eye - c.frontCenter()) <= 0.05f) continue;
                    consider(c.frontCenter(), 0.5f, [&] {
                        best = Interactable{};
                        best.kind = Interactable::Kind::Cabinet;
                        best.cabinet = &c;
                    });
                }
                for (ItemSite& site : it->second->items()) {
                    if (!site.item || site.kind == SiteKind::Drawer) continue;
                    // A small thing: it must be looked at fairly directly (and then wins over the desk it is on).
                    const glm::vec3 at = glm::vec3(site.world * glm::vec4(0.0f, 0.05f, 0.0f, 1.0f));
                    consider(at, 0.8f, [&] {
                        best = Interactable{};
                        best.kind = Interactable::Kind::Item;
                        best.item = &site;
                    });
                }
            }
        }
    }
    return best;
}

Door* ChunkManager::doorOnEdge(int level, int gx, int gz, world::EdgeAxis axis) {
    const int n = world::kChunkCells;
    auto it = m_chunks.find({world::floorDiv(gx, n), world::floorDiv(gz, n), level});
    return it != m_chunks.end() ? it->second->doorOnEdge(gx, gz, axis) : nullptr;
}

const Door* ChunkManager::doorOnEdge(int level, int gx, int gz, world::EdgeAxis axis) const {
    return const_cast<ChunkManager*>(this)->doorOnEdge(level, gx, gz, axis);
}

Terminal* ChunkManager::terminalById(uint64_t id) {
    for (auto& kv : m_chunks) {
        for (Terminal& t : kv.second->terminals()) {
            if (t.id() == id) return &t;
        }
    }
    return nullptr;
}

Phone* ChunkManager::phoneById(uint64_t id) {
    for (auto& kv : m_chunks) {
        for (Phone& p : kv.second->phones()) {
            if (p.id() == id) return &p;
        }
    }
    return nullptr;
}

FileCabinet* ChunkManager::cabinetById(uint64_t id) {
    for (auto& kv : m_chunks) {
        for (FileCabinet& c : kv.second->cabinets()) {
            if (c.id() == id) return &c;
        }
    }
    return nullptr;
}

ItemSite* ChunkManager::drawerSite(uint64_t cabinetId, int drawer) {
    for (auto& kv : m_chunks) {
        std::vector<FileCabinet>& cabinets = kv.second->cabinets();
        for (size_t i = 0; i < cabinets.size(); ++i) {
            if (cabinets[i].id() == cabinetId) return kv.second->drawerSite(static_cast<int>(i), drawer);
        }
    }
    return nullptr;
}

glm::vec3 ChunkManager::findSpawnPoint(const glm::vec3& near, int level, const BodyShape& shape,
                                       const Physics& physics) const {
    // Spiral outward over cell centres (and cell quarter points) until free.
    const float S = world::kCellSize;
    const float y = world::levelFloorY(level);
    const int bx = static_cast<int>(std::floor(near.x / S));
    const int bz = static_cast<int>(std::floor(near.z / S));
    const glm::vec2 offsets[5] = {{0.5f, 0.5f}, {0.25f, 0.25f}, {0.75f, 0.25f}, {0.75f, 0.75f}, {0.25f, 0.75f}};
    for (int ring = 0; ring < 8; ++ring) {
        for (int dz = -ring; dz <= ring; ++dz) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) != ring) continue;
                if (m_generator.cellRole(level, bx + dx, bz + dz) != world::CellRole::Room) continue;
                for (const glm::vec2& o : offsets) {
                    const glm::vec3 p((static_cast<float>(bx + dx) + o.x) * S, y,
                                      (static_cast<float>(bz + dz) + o.y) * S);
                    if (!isLoadedAt(p, level)) continue;
                    // Probe a hair above the floor: resting contact is not an obstruction.
                    const AABB probe = Physics::bodyBox(p + glm::vec3(0.0f, 0.01f, 0.0f), shape).expanded(glm::vec3(0.05f, 0.0f, 0.05f));
                    if (physics.isFree(probe, *this)) return p;
                }
            }
        }
    }
    return glm::vec3((static_cast<float>(bx) + 0.5f) * S, y, (static_cast<float>(bz) + 0.5f) * S);
}

const Chunk* ChunkManager::chunkAt(const ChunkCoord& c) const {
    auto it = m_chunks.find(c);
    return it != m_chunks.end() ? it->second.get() : nullptr;
}

bool ChunkManager::isLoadedAt(const glm::vec3& p, int level) const {
    return m_chunks.find(ChunkCoord::fromWorld(p.x, p.z, level)) != m_chunks.end();
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
