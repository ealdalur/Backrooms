#pragma once
// ---------------------------------------------------------------------------
// NavGrid.h
// Navigation and perception queries for entities, on top of the world's own
// structure: the 5 m cell grid of each storey is the navigation graph, and
// two cells are linked when the edge between them is passable. Because edge
// types are a pure function of the seed, paths can be planned even across
// areas whose geometry is not loaded. Storeys are joined by the stairwells:
// findStairLink() finds the nearest one and its walking route, and agents
// chain a path to its entrance, the climb, and a path on the next storey.
//
// Also provides:
//   * line of sight on a storey (walls, the solid parts of archways and doors
//     according to their *live* swing state);
//   * a darkness map: the long-run light level at any point, computed from
//     the actual fixtures (their mean flicker output, range and occlusion),
//     which the Stalker uses to hunt through the shadows.
// ---------------------------------------------------------------------------

#include "World/WorldConstants.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

class ChunkManager;
class Door;
class WorldGenerator;

/// How an entity values routes.
struct NavProfile {
    float darkPreference = 0.0f; ///< Extra cost per unit of cell brightness (> 0 seeks darkness).
    float doorCost = 0.0f;       ///< Extra cost per door crossed.
    bool  passDoors = true;      ///< May route through door edges at all.
    bool  avoidClosedDoors = false; ///< ...but not through a door that is shut right now (no time to open it).
    /// Optional extra cost for entering a cell (e.g. "the player can see it").
    std::function<float(const glm::ivec2&)> cellCost;
};

/// A stairwell joining storeys `lower` and `lower + 1`, as the entities use it.
struct StairLink {
    int                    lower = 0;
    glm::ivec2             cell{0};    ///< The stairwell's cell (the same on both storeys).
    glm::ivec2             outside{0}; ///< The room cell in front of its entrances (the same on both storeys).
    std::vector<glm::vec3> route;      ///< Walking route: outside the lower entrance -> outside the upper one.
};

class NavGrid {
public:
    NavGrid(const WorldGenerator& generator, const ChunkManager& chunks);

    static glm::ivec2 cellOf(const glm::vec3& p);
    /// Centre of a cell at floor height on `level`.
    static glm::vec3 cellCenter(const glm::ivec2& cell, int level);

    /// Cells entities may stand in: ordinary rooms whose chunk is loaded.
    bool walkable(int level, const glm::ivec2& cell) const;

    /// Classification of a global edge on a storey.
    world::EdgeType edgeType(int level, int gx, int gz, world::EdgeAxis axis) const;

    /// The global edge between two 4-adjacent cells. False if not adjacent.
    static bool edgeBetween(const glm::ivec2& a, const glm::ivec2& b, int& gx, int& gz, world::EdgeAxis& axis);

    /// Whether an entity with `profile` can step from `a` to adjacent `b`.
    bool canStep(int level, const glm::ivec2& a, const glm::ivec2& b, const NavProfile& profile) const;

    /// Point where a path crosses from cell `a` into adjacent `b` (edge
    /// midpoint - where doors and archways are - at floor height).
    glm::vec3 crossing(int level, const glm::ivec2& a, const glm::ivec2& b) const;

    /// A* over cells. On success `out` holds start..goal. If the goal cannot
    /// be reached within the search budget, `out` leads to the explored cell
    /// closest to it and false is returned.
    bool findPath(int level, const glm::ivec2& start, const glm::ivec2& goal, const NavProfile& profile,
                  std::vector<glm::ivec2>& out, int maxExpansions = 900) const;

    /// Horizontal line of sight between two points on `level`.
    bool lineOfSight(int level, const glm::vec2& a, const glm::vec2& b) const;

    /// The nearest stairwell from `level` to `level + dir` (dir = +1 / -1)
    /// whose entrance can be walked to on both storeys, searching chunk
    /// columns within `radius` chunks of `near`. False if there is none.
    bool findStairLink(int level, int dir, const glm::ivec2& near, StairLink& out, int radius = 2) const;

    /// True if `cell` on `level` is part of a stairwell.
    bool isStairwell(int level, const glm::ivec2& cell) const;

    /// The live door (if any) on the edge between two adjacent cells.
    const Door* doorBetween(int level, const glm::ivec2& a, const glm::ivec2& b) const;

    /// Long-run light level at a point (roughly 0 = pitch dark, >1 = under a tube).
    float brightnessAt(int level, const glm::vec3& p) const;
    /// Cached brightnessAt() of a cell centre.
    float cellBrightness(int level, const glm::ivec2& cell) const;

private:
    bool edgeBlocksSight(int level, int gx, int gz, world::EdgeAxis axis, float along) const;

    const WorldGenerator& m_generator;
    const ChunkManager&   m_chunks;
    mutable std::unordered_map<uint64_t, float> m_brightness; ///< (level, cell) -> brightness
};
