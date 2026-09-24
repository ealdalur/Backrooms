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

    // Furniture never moves, so its world colliders are computed once.
    for (const FurnitureInstance& f : m_furniture) {
        Furniture::appendWorldColliders(f, m_furnitureColliders);
    }

    m_doors.reserve(bp.doors.size());
    for (const DoorPlacement& d : bp.doors) {
        m_doors.emplace_back(d.id, d.hinge, d.closedDir);
    }
}

void Chunk::update(float dt, const AABB& playerBox) {
    for (Door& door : m_doors) door.update(dt, playerBox);
}

void Chunk::gatherColliders(const AABB& region, std::vector<AABB>& out) const {
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
    for (const Door& d : m_doors) {
        if (touches(d.bounds())) d.appendColliders(out);
    }
}
