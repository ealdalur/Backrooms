#pragma once
// ---------------------------------------------------------------------------
// Lightning.h
// Procedural electrical discharges. A bolt is a polyline built by recursive
// midpoint displacement - each segment's midpoint is pushed sideways by a
// random amount proportional to the segment's length, so the path is
// jagged at every scale, like a real stepped leader - with forks sprouting
// at random points along it (thinner, dimmer, shorter, and forking again
// themselves). Bolts live for a few hundredths of a second; streaming a new
// set many times a second gives the flicker and crawl of a live arc.
//
// The same polylines are drawn (Render/LightningRenderer) and collided with
// the entities' bodies (Gameplay/TeslaGun), so what hits is what is seen.
// ---------------------------------------------------------------------------

#include "Math/Random.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <vector>

/// One discharge channel.
struct Bolt {
    std::vector<glm::vec3> points;
    float     width = 0.01f;     ///< Half-width of the white-hot core at the root (m).
    float     taper = 0.35f;     ///< Width at the far end, as a fraction of the root width.
    float     intensity = 1.0f;  ///< HDR brightness of the core.
    glm::vec3 color{0.55f, 0.65f, 1.0f}; ///< Tint of the glow around the core.
    float     age = 0.0f;
    float     life = 0.08f;
};

/// A ball of glow: the corona round the spike, the spot where an arc lands.
struct Glow {
    glm::vec3 position;
    float     radius;
    float     intensity;
    glm::vec3 color;
};

namespace lightning {

/// Shape of a strike.
struct StrikeParams {
    float width = 0.012f;
    float intensity = 1.5f;
    float life = 0.08f;
    float roughness = 0.22f;   ///< Sideways displacement per unit segment length.
    int   levels = 5;          ///< Subdivisions: 2^levels segments.
    float forkChance = 0.12f;  ///< Per interior point of the main channel.
    int   forkDepth = 2;       ///< Forks of forks.
    glm::vec3 color{0.55f, 0.65f, 1.0f};
};

/// Jagged path from `a` to `b` (both kept exactly), replacing `out`.
void jagged(std::vector<glm::vec3>& out, const glm::vec3& a, const glm::vec3& b, float roughness, int levels, rnd::Rng& rng);

/// Appends a bolt from `a` to `b` and its forks to `out`. Returns the index
/// of the main channel in `out`.
size_t strike(std::vector<Bolt>& out, const glm::vec3& a, const glm::vec3& b, const StrikeParams& params, rnd::Rng& rng);

/// A random unit direction within `maxAngle` radians of the unit `axis`
/// (uniform over the spherical cap).
glm::vec3 coneDirection(const glm::vec3& axis, float maxAngle, rnd::Rng& rng);

} // namespace lightning
