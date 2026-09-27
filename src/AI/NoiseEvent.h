#pragma once
// ---------------------------------------------------------------------------
// NoiseEvent.h
// A sound made in the world that entities may hear. The audible radius is
// for open space; walls in between halve it (see Wanderer hearing).
// ---------------------------------------------------------------------------

#include <glm/glm.hpp>
#include <cstdint>

enum class NoiseKind : uint8_t {
    Footstep, ///< Player footsteps (quiet when crouching, loud when running).
    Landing,  ///< Jump take-off / heavy landing.
    Door,     ///< A door unlatching, creaking or slamming.
    Typing,   ///< Keys on a terminal.
    Machine,  ///< Terminal beeps and boot noise.
};

struct NoiseEvent {
    glm::vec3 position;
    float     radius; ///< Audible distance in metres (open space).
    NoiseKind kind;
};
