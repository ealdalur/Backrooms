#pragma once
// ---------------------------------------------------------------------------
// WorldGenerator.h
// Deterministic procedural generator for the infinite Backrooms grid.
//
// Topology: the world is a grid of 5 m cells. Every cell edge is classified
// (Open / Wall / Archway / Door) by a pure function of the world seed and the
// edge's global coordinates, so any chunk - and any system (lighting, AO,
// collision) - sees exactly the same answer and revisited areas regenerate
// identically.
//
// Connectivity guarantee: inside each chunk the passable interior edges
// contain a random spanning tree (randomised Kruskal), and every boundary
// between two chunks has at least one passable edge. Hence the whole
// infinite map is connected.
// ---------------------------------------------------------------------------

#include "World/Chunk.h"
#include "World/ChunkCoord.h"
#include "World/WorldConstants.h"

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <unordered_map>

class WorldGenerator {
public:
    explicit WorldGenerator(uint64_t worldSeed);

    uint64_t worldSeed() const { return m_seed; }

    /// Seed of a chunk: drives its furniture, light placement and flicker.
    uint64_t chunkSeed(const ChunkCoord& c) const;

    /// Classification of a global cell edge (see EdgeAxis for conventions).
    world::EdgeType edge(int gx, int gz, world::EdgeAxis axis) const;

    /// True if any edge incident to the global grid vertex is not Open.
    bool vertexHasWall(int gx, int gz) const;

    /// True if a free-standing structural pillar stands on the vertex.
    bool vertexHasPillar(int gx, int gz) const;

    /// True if the horizontal segment a->b (world x/z) crosses a wall, closed
    /// door frame or the solid part of an archway. Used for light occlusion.
    bool isLightBlocked(const glm::vec2& a, const glm::vec2& b) const;

    /// Generates all CPU-side content of a chunk.
    ChunkBlueprint generate(const ChunkCoord& c) const;

private:
    static constexpr int kN = world::kChunkCells;

    /// Edge classification for all edges owned by one chunk: the west and
    /// south edges of each of its cells (index = lz * N + lx).
    struct ChunkLayout {
        std::array<world::EdgeType, kN * kN> west;
        std::array<world::EdgeType, kN * kN> south;
    };

    const ChunkLayout& layout(const ChunkCoord& c) const;
    ChunkLayout buildLayout(const ChunkCoord& c) const;
    void trimCache() const;

    void buildShell(ChunkBlueprint& bp, const glm::vec3& origin) const;
    void buildEdge(ChunkBlueprint& bp, const glm::vec3& origin, int gx, int gz, world::EdgeAxis axis) const;
    void buildVertex(ChunkBlueprint& bp, const glm::vec3& origin, int gx, int gz) const;
    void placeLights(ChunkBlueprint& bp, const glm::vec3& origin) const;
    void placeFurniture(ChunkBlueprint& bp, const glm::vec3& origin) const;
    void computeLightVisibility(LightFixture& light) const;

    uint64_t m_seed;
    mutable std::unordered_map<ChunkCoord, ChunkLayout, ChunkCoordHash> m_layoutCache;
};
