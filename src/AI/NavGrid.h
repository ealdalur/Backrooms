#pragma once
// ---------------------------------------------------------------------------
// NavGrid.h
// Navigation and perception queries for entities, on top of the world's own
// structure: the 5 m cell grid of each storey is the navigation graph, and
// two cells are linked when the edge between them is passable. Because edge
// types are a pure function of the seed, paths can be planned even across
// areas whose geometry is not loaded.
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
class WorldGenerator;

/// How an entity values routes.
struct NavProfile {
    float darkPreference = 0.0f; ///< Extra cost per unit of cell brightness (> 0 seeks darkness).
    float doorCost = 0.0f;       ///< Extra cost per door crossed.
    bool  passDoors = true;      ///< May route through door edges at all.
    /// Optional extra cost for entering a cell (e.g. "the player can see it").
    std::function<float(const glm::ivec2&)> cellCost;
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
