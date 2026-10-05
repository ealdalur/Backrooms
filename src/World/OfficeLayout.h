#pragma once
// ---------------------------------------------------------------------------
// OfficeLayout.h
// The "real world": the corporate office the player noclips out into. It is
// built on the very same grid as the Backrooms - 5 m cells, walls on cell
// edges, the same doors, ceiling, light fixtures, desks, chairs, terminals
// and phones - so every system (collision, lighting, ambient occlusion, the
// terminals' MAP) works there unchanged. Only the paint is different.
//
// One storey (level 0), kWidth x kDepth cells (90 x 60 m), walled in:
//
//   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
//   |CS CS|OF|OF|..|..|..|..|..|..|..|CR CR|OF|..|OF|CS CS|  <- north: offices, conference room
//   +-----+==+==+--+--+--+--+--+--+--+==+--+==+--+==+-----+
//   |OF=  corridor ring                               =OF|
//   |OF=  ..cubicles..  | aisle |  ..cubicles..  |    =OF|
//   |EX.  (2 x 2 pods per cell, aisles between)       =OF|
//   |OF=                                              .EX|  <- exit lobbies
//   |OF=  corridor ring                               =OF|
//   +-----+==+==+--+--+--+--+.....+--+--+--+--+--+--+-----+
//   |CS CS|OF|OF|OF|OF|OF|BR BR|OF|..|..|..|..|..|..|CS CS|  <- south: offices, breakroom
//
//   CS corner suites (two cells), OF executive offices (one cell, a door to
//   the corridor), BR the breakroom (open archways), CR conference room B,
//   EX exit lobbies, each with an emergency exit that does not open.
// Inside the ring: a field of cubicle pods (four cubicles each, dingy fabric
// partitions) split by aisles, with plants, water coolers and bins along the
// walkways. Nobody is here. The terminals and phones work just like the ones
// in the Backrooms. That is the joke.
// ---------------------------------------------------------------------------

#include "World/Chunk.h"
#include "World/WorldConstants.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace office {

inline constexpr int kWidth = 18; ///< Cells along x.
inline constexpr int kDepth = 12; ///< Cells along z.
inline constexpr int kLevel = 0;  ///< The only storey.

enum class Zone : uint8_t {
    Outside,
    Office,      ///< An executive office.
    CornerSuite, ///< A two-cell corner office.
    Breakroom,
    Conference,
    ExitLobby,   ///< Where an emergency exit is.
    Corridor,    ///< The ring walkway inside the perimeter rooms.
    Aisle,       ///< Walkways through the cubicle field.
    Cubicles,    ///< One pod of four cubicles.
};

/// What occupies global cell (gx, gz) of the office storey.
Zone zone(int gx, int gz);

/// Classification of a global cell edge on `level` (see world::EdgeAxis).
world::EdgeType edge(int level, int gx, int gz, world::EdgeAxis axis);

/// Ceiling fixtures of a cell: emitter centres, cell-local (x, z), long side along z.
std::vector<glm::vec2> fixtures(int gx, int gz);

/// Builds a chunk's floor plan contents: cubicle partitions, furniture,
/// terminals, phones, filing cabinets, plants, clutter, signs and posters.
/// `seed` keys every random choice and id.
void furnish(ChunkBlueprint& bp, uint64_t seed);

/// Where the player arrives: standing in a cubicle, looking at its desk.
void spawn(glm::vec3& feet, float& yaw);

} // namespace office
