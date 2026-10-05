#pragma once
// ---------------------------------------------------------------------------
// Chunk.h
// One 25 x 25 m square of one storey of the infinite Backrooms. A chunk is
// produced as a CPU-only ChunkBlueprint by the WorldGenerator (pure function
// of the world seed and chunk coordinate) and then turned into a live Chunk
// that owns GPU geometry, collision data, doors, terminals, phones, filing
// cabinets, furniture, item sites (where the gun's parts rest) and light
// fixtures. A chunk that holds the lower half of a stairwell also owns
// the stair geometry reaching up into the storey above.
// ---------------------------------------------------------------------------

#include "Actors/Door.h"
#include "Actors/FileCabinet.h"
#include "Actors/Furniture.h"
#include "Actors/Phone.h"
#include "Actors/Terminal.h"
#include "Gameplay/Items.h"
#include "Physics/AABB.h"
#include "Render/Mesh.h"
#include "World/ChunkCoord.h"
#include "World/LightFixture.h"
#include "World/WorldConstants.h"

#include <cstdint>
#include <vector>

/// Placement of a door inside a door-frame edge.
struct DoorPlacement {
    uint64_t        id;        ///< Deterministic global id (hash of the edge and level).
    glm::vec3       hinge;     ///< Hinge axis position at floor level.
    glm::vec3       closedDir; ///< Hinge -> latch direction when closed.
    int32_t         gx, gz;    ///< Global edge coordinate the door hangs in...
    world::EdgeAxis axis;      ///< ...and its orientation.
};

/// Placement of a retro computer on a desk.
struct TerminalPlacement {
    uint64_t  id;
    glm::mat4 model;
    bool      powered;
};

/// Placement of a telephone on a desk.
struct PhonePlacement {
    uint64_t  id;
    glm::mat4 model;
};

/// Placement of a filing cabinet.
struct CabinetPlacement {
    uint64_t  id;
    glm::mat4 model;
};

/// Where something is written on a wall (developer scenes find it).
struct ClueSpot {
    glm::vec3 position; ///< Middle of the writing.
    glm::vec3 normal;   ///< The way the wall faces.
    bool      clue;     ///< It is the phone number.
    bool      doomNote; ///< It is the note about DOOM.
};

/// Everything the generator produces for a chunk (no GL objects).
struct ChunkBlueprint {
    ChunkCoord                     coord;
    uint64_t                       seed = 0;
    MeshData                       staticMesh;   ///< Architecture + fixtures (world space).
    std::vector<AABB>              colliders;    ///< Static solid obstacles (incl. floor / ceiling slabs).
    std::vector<FurnitureInstance> furniture;
    std::vector<DoorPlacement>     doors;
    std::vector<TerminalPlacement> terminals;
    std::vector<PhonePlacement>    phones;
    std::vector<CabinetPlacement>  cabinets;
    std::vector<ItemSite>          items;        ///< Drawer sites refer to `cabinets` by index.
    std::vector<LightFixture>      lights;
    std::vector<AABB>              glitchZones;  ///< The exit room's glitching walls: not solid - walking into one is the way out.
    std::vector<ClueSpot>          clues;        ///< Everything written on the walls.
    AABB                           bounds;
};

class Chunk {
public:
    /// Takes ownership of a blueprint and uploads its static mesh to the GPU.
    explicit Chunk(ChunkBlueprint&& blueprint);

    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    /// Advances door and drawer animations (items in drawers slide with them).
    void update(float dt, const AABB& playerBox);

    /// Appends static colliders, furniture / terminal / phone / cabinet colliders and (unless
    /// `includeDoors` is false) door colliders that overlap `region`.
    void gatherColliders(const AABB& region, std::vector<AABB>& out, bool includeDoors = true) const;

    /// The door hanging in global edge (gx, gz, axis), or nullptr.
    Door* doorOnEdge(int gx, int gz, world::EdgeAxis axis);
    const Door* doorOnEdge(int gx, int gz, world::EdgeAxis axis) const;

    const ChunkCoord& coord() const { return m_coord; }
    uint64_t seed() const { return m_seed; }
    const AABB& bounds() const { return m_bounds; }
    const GpuMesh& staticMesh() const { return m_staticMesh; }
    const std::vector<FurnitureInstance>& furniture() const { return m_furniture; }
    const std::vector<LightFixture>& lights() const { return m_lights; }
    std::vector<Door>& doors() { return m_doors; }
    const std::vector<Door>& doors() const { return m_doors; }
    std::vector<Terminal>& terminals() { return m_terminals; }
    const std::vector<Terminal>& terminals() const { return m_terminals; }
    std::vector<Phone>& phones() { return m_phones; }
    const std::vector<Phone>& phones() const { return m_phones; }
    std::vector<FileCabinet>& cabinets() { return m_cabinets; }
    const std::vector<FileCabinet>& cabinets() const { return m_cabinets; }
    std::vector<ItemSite>& items() { return m_items; }
    const std::vector<ItemSite>& items() const { return m_items; }
    const std::vector<AABB>& glitchZones() const { return m_glitchZones; }
    const std::vector<ClueSpot>& clues() const { return m_clues; }

    /// The item site in a drawer of one of this chunk's cabinets, or nullptr.
    ItemSite* drawerSite(int cabinet, int drawer);

    /// Offset of this chunk's first light in the renderer's global light list.
    int lightBase() const { return m_lightBase; }
    void setLightBase(int base) { m_lightBase = base; }

private:
    /// Packs a global edge coordinate into a lookup key.
    static uint64_t edgeKey(int gx, int gz, world::EdgeAxis axis);
    /// Recomputes the world transform of an item site.
    void placeSite(ItemSite& site) const;

    ChunkCoord                     m_coord;
    uint64_t                       m_seed;
    GpuMesh                        m_staticMesh;
    std::vector<AABB>              m_colliders;
    std::vector<AABB>              m_furnitureColliders;
    std::vector<FurnitureInstance> m_furniture;
    std::vector<Door>              m_doors;
    std::vector<uint64_t>          m_doorEdges; ///< edgeKey() of each door (parallel to m_doors).
    std::vector<Terminal>          m_terminals;
    std::vector<Phone>             m_phones;
    std::vector<FileCabinet>       m_cabinets;
    std::vector<ItemSite>          m_items;
    std::vector<LightFixture>      m_lights;
    std::vector<AABB>              m_glitchZones;
    std::vector<ClueSpot>          m_clues;
    AABB                           m_bounds;
    int                            m_lightBase = 0;
};
