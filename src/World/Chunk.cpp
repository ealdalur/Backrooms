// ---------------------------------------------------------------------------
// Chunk.cpp
// ---------------------------------------------------------------------------
#include "World/Chunk.h"

#include <utility>

Chunk::Chunk(ChunkBlueprint&& bp)
    : m_coord(bp.coord),
      m_seed(bp.seed),
      m_colliders(std::move(bp.colliders)),
      m_furniture(std::move(bp.furniture)),
      m_lights(std::move(bp.lights)),
      m_bounds(bp.bounds) {
    m_staticMesh.upload(bp.staticMesh);

    // Furniture and terminals never move, so their world colliders are computed once.
    for (const FurnitureInstance& f : m_furniture) {
        Furniture::appendWorldColliders(f, m_furnitureColliders);
    }
    m_terminals.reserve(bp.terminals.size());
    for (const TerminalPlacement& t : bp.terminals) {
        m_terminals.emplace_back(t.id, t.model, t.powered);
        for (const AABB& local : Terminal::localColliders()) m_furnitureColliders.push_back(local.transformed(t.model));
    }

    m_doors.reserve(bp.doors.size());
    m_doorEdges.reserve(bp.doors.size());
    for (const DoorPlacement& d : bp.doors) {
        m_doors.emplace_back(d.id, d.hinge, d.closedDir);
        m_doorEdges.push_back(edgeKey(d.gx, d.gz, d.axis));
    }
}

uint64_t Chunk::edgeKey(int gx, int gz, world::EdgeAxis axis) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(gx)) << 32) ^
           (static_cast<uint64_t>(static_cast<uint32_t>(gz)) << 1) ^ static_cast<uint64_t>(axis);
}

void Chunk::update(float dt, const AABB& playerBox) {
    for (Door& door : m_doors) door.update(dt, playerBox);
}

void Chunk::gatherColliders(const AABB& region, std::vector<AABB>& out, bool includeDoors) const {
    // Touching counts here (not strict overlap) so resting contacts are kept.
    auto touches = [&region](const AABB& b) {
        return b.min.x <= region.max.x && b.max.x >= region.min.x &&
               b.min.y <= region.max.y && b.max.y >= region.min.y &&
               b.min.z <= region.max.z && b.max.z >= region.min.z;
    };
    for (const AABB& c : m_colliders) {
        if (touches(c)) out.push_back(c);
    }
    for (const AABB& c : m_furnitureColliders) {
        if (touches(c)) out.push_back(c);
    }
    if (!includeDoors) return;
    for (const Door& d : m_doors) {
        if (touches(d.bounds())) d.appendColliders(out);
    }
}

Door* Chunk::doorOnEdge(int gx, int gz, world::EdgeAxis axis) {
    const uint64_t key = edgeKey(gx, gz, axis);
    for (size_t i = 0; i < m_doorEdges.size(); ++i) {
        if (m_doorEdges[i] == key) return &m_doors[i];
    }
    return nullptr;
}

const Door* Chunk::doorOnEdge(int gx, int gz, world::EdgeAxis axis) const {
    return const_cast<Chunk*>(this)->doorOnEdge(gx, gz, axis);
}
