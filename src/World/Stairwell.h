#pragma once
// ---------------------------------------------------------------------------
// Stairwell.h
// Geometry of the enclosed switchback stairwells that connect storey L to
// storey L + 1. A stairwell fills one grid cell on both storeys; the world
// generator walls that cell off (except for one entrance edge on each
// storey), cuts the shaft hole through the slab between them, and calls
// build() to add the stair itself to the lower storey's chunk.
//
// Local frame (before rotation): x, z in [0, cell size] across the cell,
// y = 0 at the lower floor; the entrance is on the local south edge (z = 0).
//
//     z = S  +-------------------------------+
//            |            landing            |  y = 1.7
//     zTop   |------------+-----+------------|
//            |  flight A  | div |  flight B  |
//            |  (up, +z)  | ider|  (up, -z)  |
//     zL     |------------+-----+------------|
//            |        lobby (entrance)       |  y = 0 below, 3.4 above
//     z = 0  +--------------[  ]-------------+
//            x = 0                         x = S
// ---------------------------------------------------------------------------

#include "Physics/AABB.h"
#include "Render/Mesh.h"
#include "World/WorldConstants.h"

#include <glm/glm.hpp>
#include <vector>

namespace stairs {

/// Where a stairwell sits inside its chunk and how it is turned. A pure
/// function of the world seed, the chunk and the storey pair (see
/// WorldGenerator::stairwell).
struct Placement {
    int             lx = 1, lz = 1;  ///< Local cell in the chunk (never on a chunk border).
    int             rotation = 0;    ///< Quarter turns (counter-clockwise seen from above).
    world::EdgeType lowerEntrance = world::EdgeType::Door;   ///< Opening on storey L.
    world::EdgeType upperEntrance = world::EdgeType::Archway; ///< Opening on storey L + 1.
};

/// Cell side the entrance ends up on: 0 = west, 1 = east, 2 = south, 3 = north
/// (the same convention as the furniture placement code).
int entranceSide(int rotation);

/// Rigid local -> world transform of the stairwell in global cell (gx, gz).
glm::mat4 cellTransform(int gx, int gz, int lowerLevel, int rotation);

/// World x/z rectangle (x0, z0, x1, z1) of the shaft hole through the slab.
glm::vec4 holeRect(int gx, int gz, int rotation);

/// Appends flights, landing, divider wall, slab edges, the upper guard wall
/// and handrails (world space) plus their colliders.
void build(MeshData& mesh, std::vector<AABB>& colliders, int gx, int gz, int lowerLevel, int rotation);

/// Appends the illuminated sign above the entrance, on the outside wall of
/// storey `level` (the stairwell's lower storey reads "^ STAIRS", its upper
/// storey "v STAIRS"), so stairwells can be spotted down a corridor.
void addEntranceSign(MeshData& mesh, int gx, int gz, int level, int rotation, bool up);

/// A ceiling fixture position: centre of the emitter (x/z, world) and
/// whether the tube's long side runs along world X.
struct FixtureSpot {
    glm::vec2 center;
    bool      alongX;
};
/// Fixture over the lower storey's entrance lobby.
FixtureSpot lobbyFixture(int gx, int gz, int rotation);
/// Fixture on the upper storey's ceiling over the landing; it lights the
/// whole shaft, down to the lower storey.
FixtureSpot shaftFixture(int gx, int gz, int rotation);

/// Walking route (feet positions) from outside the lower entrance, up both
/// flights, to outside the upper entrance. Used by the stair autopilot test.
std::vector<glm::vec3> climbRoute(int gx, int gz, int lowerLevel, int rotation);

} // namespace stairs
