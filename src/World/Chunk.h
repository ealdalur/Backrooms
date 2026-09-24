#pragma once
// ---------------------------------------------------------------------------
// Chunk.h
// One 25 x 25 m square of the infinite Backrooms. A chunk is produced as a
// CPU-only ChunkBlueprint by the WorldGenerator (pure function of the world
// seed and chunk coordinate) and then turned into a live Chunk that owns GPU
// geometry, collision data, doors, furniture and light fixtures.
// ---------------------------------------------------------------------------

#include "Actors/Door.h"
#include "Actors/Furniture.h"
#include "Physics/AABB.h"
#include "Render/Mesh.h"
#include "World/ChunkCoord.h"
#include "World/LightFixture.h"

#include <cstdint>
#include <vector>

/// Placement of a door inside a door-frame edge.
struct DoorPlacement {
    uint64_t  id;        ///< Deterministic global id (hash of the edge).
    glm::vec3 hinge;     ///< Hinge axis position at floor level.
    glm::vec3 closedDir; ///< Hinge -> latch direction when closed.
};

/// Everything the generator produces for a chunk (no GL objects).
struct ChunkBlueprint {
    ChunkCoord                     coord;
    uint64_t                       seed = 0;
    MeshData                       staticMesh;   ///< Architecture + fixtures (world space).
    std::vector<AABB>              colliders;    ///< Static solid obstacles.
    std::vector<FurnitureInstance> furniture;
    std::vector<DoorPlacement>     doors;
    std::vector<LightFixture>      lights;
    AABB                           bounds;
};

class Chunk {
public:
    /// Takes ownership of a blueprint and uploads its static mesh to the GPU.
    explicit Chunk(ChunkBlueprint&& blueprint);

    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    /// Advances door animations.
    void update(float dt, const AABB& playerBox);

    /// Appends static colliders, furniture colliders and door colliders
    /// that overlap `region`.
    void gatherColliders(const AABB& region, std::vector<AABB>& out) const;

    const ChunkCoord& coord() const { return m_coord; }
    uint64_t seed() const { return m_seed; }
    const AABB& bounds() const { return m_bounds; }
    const GpuMesh& staticMesh() const { return m_staticMesh; }
    const std::vector<FurnitureInstance>& furniture() const { return m_furniture; }
    const std::vector<LightFixture>& lights() const { return m_lights; }
    std::vector<Door>& doors() { return m_doors; }
    const std::vector<Door>& doors() const { return m_doors; }

    /// Offset of this chunk's first light in the renderer's global light list.
    int lightBase() const { return m_lightBase; }
    void setLightBase(int base) { m_lightBase = base; }

private:
    ChunkCoord                     m_coord;
    uint64_t                       m_seed;
    GpuMesh                        m_staticMesh;
    std::vector<AABB>              m_colliders;
    std::vector<AABB>              m_furnitureColliders;
    std::vector<FurnitureInstance> m_furniture;
    std::vector<Door>              m_doors;
    std::vector<LightFixture>      m_lights;
    AABB                           m_bounds;
    int                            m_lightBase = 0;
};
