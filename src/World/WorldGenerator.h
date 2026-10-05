#pragma once
// ---------------------------------------------------------------------------
// WorldGenerator.h
// Deterministic procedural generator for the infinite, multi-storey
// Backrooms grid.
//
// Topology: every storey ("level") is a grid of 5 m cells. Every cell edge
// is classified (Open / Wall / Archway / Door) by a pure function of the
// world seed, the level and the edge's global coordinates, so any chunk -
// and any system (lighting, AO, collision, AI) - sees exactly the same
// answer and revisited areas regenerate identically. Level 0 uses the world
// seed directly, so it is the same floor plan the single-storey generator
// produced (apart from cells that now hold stairwells).
//
// Connectivity guarantee: inside each chunk the passable interior edges
// contain a random spanning tree (randomised Kruskal), and every boundary
// between two chunks has at least one passable edge. Hence each whole
// infinite storey is connected. Stairwell cells are left out of the tree and
// attached as leaves through their entrance edge, so they never cut a
// chunk in two.
//
// Verticality: for every chunk column (cx, cz) and storey pair (L, L + 1) a
// stairwell may exist (a pure hash of the seed, cx, cz and L), and every
// storey of every chunk is guaranteed at least one, going up or down.
// Stairwells rising from even and odd storeys use different cell columns, so
// the two stairwells a storey may hold (one going up, one coming down) never
// collide.
//
// Writing on the walls: a few solid wall faces per chunk carry something
// written in marker, pen, pencil, spray paint or blood (World/Decals) - and
// very rarely the puzzle's phone number (setWallClue), alone.
//
// The exit: once the puzzle chain has located it (setExitChunk), one room of
// that chunk - the one with the most walls - becomes the glitch room. Its
// walls are built from loose tiles of the Glitch material that jitter,
// blink out and smear (the world shader animates them), and they are not
// solid: walking into one is the way out (ChunkBlueprint::glitchZones).
// That chunk is also guaranteed a terminal, whose MAP shows the room.
//
// Realms: after the escape the generator builds the office instead
// (setRealm, World/OfficeLayout) - the same grid, light blue paint.
// ---------------------------------------------------------------------------

#include "Render/MaterialTypes.h"
#include "World/Chunk.h"
#include "World/ChunkCoord.h"
#include "World/Stairwell.h"
#include "World/WorldConstants.h"

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

class WorldGenerator {
public:
    explicit WorldGenerator(uint64_t worldSeed);

    uint64_t worldSeed() const { return m_seed; }

    /// Switches between the Backrooms and the office (forgets cached layouts;
    /// loaded chunks must be rebuilt).
    void setRealm(world::Realm realm);
    world::Realm realm() const { return m_realm; }

    /// The phone number written - rarely - on the walls ("" for none).
    void setWallClue(const std::string& text) { m_wallClue = text; }

    /// Makes `c` the exit chunk (or none). Loaded copies must be rebuilt.
    void setExitChunk(const std::optional<ChunkCoord>& c);
    const std::optional<ChunkCoord>& exitChunk() const { return m_exitChunk; }
    /// The glitch room of the exit chunk: global cell (x, y) and level (z).
    const std::optional<glm::ivec3>& exitCell() const { return m_exitCell; }

    /// Seed of a whole storey (level 0 uses the world seed itself).
    uint64_t levelSeed(int level) const;

    /// Seed of a chunk: drives its furniture, light placement and flicker.
    uint64_t chunkSeed(const ChunkCoord& c) const;

    /// The stairwell connecting storeys `lowerLevel` and `lowerLevel + 1` in
    /// chunk column (cx, cz), if there is one.
    std::optional<stairs::Placement> stairwell(int lowerLevel, int cx, int cz) const;

    /// What occupies global cell (gx, gz) on `level`.
    world::CellRole cellRole(int level, int gx, int gz) const;

    /// Classification of a global cell edge (see EdgeAxis for conventions).
    world::EdgeType edge(int level, int gx, int gz, world::EdgeAxis axis) const;

    /// True if any edge incident to the global grid vertex is not Open.
    bool vertexHasWall(int level, int gx, int gz) const;

    /// True if a free-standing structural pillar stands on the vertex.
    bool vertexHasPillar(int level, int gx, int gz) const;

    /// True if the horizontal segment a->b (world x/z) on `level` crosses a
    /// wall, closed door frame or the solid part of an archway. Used for
    /// light and sound occlusion.
    bool isLightBlocked(int level, const glm::vec2& a, const glm::vec2& b) const;

    /// How many such walls the segment a->b crosses (counting stops at `stopAt`).
    int wallsBetween(int level, const glm::vec2& a, const glm::vec2& b, int stopAt = 1 << 30) const;

    /// Generates all CPU-side content of a chunk.
    ChunkBlueprint generate(const ChunkCoord& c) const;

private:
    static constexpr int kN = world::kChunkCells;

    /// Edge classification for all edges owned by one chunk (the west and
    /// south edges of each of its cells, index = lz * N + lx) plus the role
    /// of each cell.
    struct ChunkLayout {
        std::array<world::EdgeType, kN * kN> west;
        std::array<world::EdgeType, kN * kN> south;
        std::array<world::CellRole, kN * kN> roles;
    };

    const ChunkLayout& layout(const ChunkCoord& c) const;
    ChunkLayout buildLayout(const ChunkCoord& c) const;
    void trimCache() const;

    void buildShell(ChunkBlueprint& bp, const glm::vec3& origin) const;
    void buildEdge(ChunkBlueprint& bp, const glm::vec3& origin, int gx, int gz, world::EdgeAxis axis) const;
    void buildVertex(ChunkBlueprint& bp, const glm::vec3& origin, int gx, int gz) const;
    void placeLights(ChunkBlueprint& bp, const glm::vec3& origin) const;
    void placeFurniture(ChunkBlueprint& bp, const glm::vec3& origin) const;
    /// Things written on the walls of a Backrooms chunk.
    void scrawlWalls(ChunkBlueprint& bp, const glm::vec3& origin) const;
    /// One of the exit room's walls: loose glitching tiles over `box`, and no collider.
    void addGlitchWall(ChunkBlueprint& bp, const glm::vec3& origin, const AABB& box, world::EdgeAxis axis) const;

    bool isExitCell(int level, int gx, int gz) const;
    bool isGlitchEdge(int level, int gx, int gz, world::EdgeAxis axis) const;
    /// What walls are made of in the current realm.
    MaterialId wallMaterial() const;

    /// Adds one ceiling troffer (housing, emissive diffuser, light) centred
    /// at world (x, z). `shaftCell` (global cell, or nullptr) marks a fixture
    /// over a stairwell shaft, whose light also reaches the storey below.
    void addFixture(ChunkBlueprint& bp, const glm::vec3& origin, const glm::vec2& centre, bool alongX,
                    int& lightIndex, float unrest, const glm::ivec2* shaftCell) const;
    void computeLightVisibility(LightFixture& light, int level, const glm::ivec2* shaftCell) const;

    uint64_t m_seed;
    world::Realm m_realm = world::Realm::Backrooms;
    std::string m_wallClue;
    std::optional<ChunkCoord> m_exitChunk;
    std::optional<glm::ivec3> m_exitCell;
    int m_exitWalls = 0; ///< Solid walls round the exit room (0: it gets a free-standing glitch slab).
    mutable std::unordered_map<ChunkCoord, ChunkLayout, ChunkCoordHash> m_layoutCache;
};
