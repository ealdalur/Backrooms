#pragma once
// ---------------------------------------------------------------------------
// Noise.h
// Periodic (tileable) procedural noise primitives used to synthesise the
// material textures at start-up, plus 1D value noise used by light flicker.
//
// All 2D functions take an integer period (px, py) in lattice units: the
// output repeats exactly every px units in x and py units in y, which makes
// textures generated from them seamlessly tileable.
// ---------------------------------------------------------------------------

#include <cstdint>

namespace noise {

/// Periodic 2D gradient (Perlin) noise. Output roughly in [-1, 1].
float perlin(float x, float y, int px, int py, uint32_t seed);

/// Fractal Brownian motion built from periodic Perlin noise.
/// Each octave doubles frequency AND period, so tiling is preserved.
/// Output roughly in [-1, 1].
float fbm(float x, float y, int px, int py, int octaves, uint32_t seed, float gain = 0.5f);

/// Ridged multifractal variant (sharp creases); output in [0, 1].
float ridged(float x, float y, int px, int py, int octaves, uint32_t seed);

/// Result of a cellular (Worley) noise query.
struct Cellular {
    float    f1;     ///< Distance to the closest feature point.
    float    f2;     ///< Distance to the second closest feature point.
    uint32_t cellId; ///< Hash identifying the closest feature's cell (periodic).
};

/// Periodic cellular noise with one jittered feature point per lattice cell.
Cellular worley(float x, float y, int px, int py, uint32_t seed);

/// Smooth 1D value noise in [0, 1], used for temporal signals (flicker).
float value1D(double t, uint64_t seed);

/// Per-pixel white noise in [0, 1] that is tileable by construction.
float white(int x, int y, uint32_t seed);

} // namespace noise
