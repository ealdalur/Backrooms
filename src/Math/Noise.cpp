// ---------------------------------------------------------------------------
// Noise.cpp
// Implementation of the tileable noise primitives declared in Noise.h.
// ---------------------------------------------------------------------------
#include "Math/Noise.h"
#include "Math/Random.h"

#include <cmath>
#include <algorithm>

namespace noise {
namespace {

/// Wraps a lattice coordinate into [0, period).
inline int wrap(int i, int period) {
    const int m = i % period;
    return m < 0 ? m + period : m;
}

/// Quintic smootherstep fade curve (C2 continuous).
inline float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

/// 16 evenly distributed unit gradients; looked up by hash (no trig per sample).
struct GradientTable {
    float x[16];
    float y[16];
    GradientTable() {
        for (int i = 0; i < 16; ++i) {
            const float a = static_cast<float>(i) * (6.28318530718f / 16.0f) + 0.19634954f;
            x[i] = std::cos(a);
            y[i] = std::sin(a);
        }
    }
};
const GradientTable& gradients() {
    static const GradientTable table;
    return table;
}

/// Dot product between the lattice gradient at (ix, iy) and offset (dx, dy).
inline float gradDot(int ix, int iy, float dx, float dy, uint32_t seed) {
    const GradientTable& g = gradients();
    const uint32_t h = rnd::hash2i(ix, iy, seed) & 15u;
    return g.x[h] * dx + g.y[h] * dy;
}

} // namespace

float perlin(float x, float y, int px, int py, uint32_t seed) {
    px = std::max(px, 1);
    py = std::max(py, 1);

    const float fx0 = std::floor(x);
    const float fy0 = std::floor(y);
    const int   x0  = static_cast<int>(fx0);
    const int   y0  = static_cast<int>(fy0);
    const float dx  = x - fx0;
    const float dy  = y - fy0;

    const int ix0 = wrap(x0, px), ix1 = wrap(x0 + 1, px);
    const int iy0 = wrap(y0, py), iy1 = wrap(y0 + 1, py);

    const float n00 = gradDot(ix0, iy0, dx,        dy,        seed);
    const float n10 = gradDot(ix1, iy0, dx - 1.0f, dy,        seed);
    const float n01 = gradDot(ix0, iy1, dx,        dy - 1.0f, seed);
    const float n11 = gradDot(ix1, iy1, dx - 1.0f, dy - 1.0f, seed);

    const float u = fade(dx);
    const float v = fade(dy);
    // 2D Perlin peaks at ~0.707; rescale to use the full [-1, 1] range.
    return lerp(lerp(n00, n10, u), lerp(n01, n11, u), v) * 1.41421356f;
}

float fbm(float x, float y, int px, int py, int octaves, uint32_t seed, float gain) {
    float sum = 0.0f;
    float amp = 1.0f;
    float norm = 0.0f;
    float freq = 1.0f;
    int   perX = px;
    int   perY = py;
    for (int o = 0; o < octaves; ++o) {
        sum  += amp * perlin(x * freq, y * freq, perX, perY, seed + static_cast<uint32_t>(o) * 0x9E3779B9u);
        norm += amp;
        amp  *= gain;
        freq *= 2.0f;
        perX *= 2;
        perY *= 2;
    }
    return sum / norm;
}

float ridged(float x, float y, int px, int py, int octaves, uint32_t seed) {
    float sum = 0.0f;
    float amp = 0.5f;
    float norm = 0.0f;
    float freq = 1.0f;
    int   perX = px;
    int   perY = py;
    for (int o = 0; o < octaves; ++o) {
        float n = 1.0f - std::fabs(perlin(x * freq, y * freq, perX, perY, seed + static_cast<uint32_t>(o) * 0x632BE5ABu));
        n *= n;
        sum  += amp * n;
        norm += amp;
        amp  *= 0.5f;
        freq *= 2.0f;
        perX *= 2;
        perY *= 2;
    }
    return sum / norm;
}

Cellular worley(float x, float y, int px, int py, uint32_t seed) {
    px = std::max(px, 1);
    py = std::max(py, 1);

    const int cx = static_cast<int>(std::floor(x));
    const int cy = static_cast<int>(std::floor(y));

    Cellular result{1e9f, 1e9f, 0u};
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int lx = cx + ox;
            const int ly = cy + oy;
            // Feature points are hashed from WRAPPED cell ids -> periodic result.
            const uint32_t h  = rnd::hash2i(wrap(lx, px), wrap(ly, py), seed);
            const float    fx = static_cast<float>(lx) + static_cast<float>(h & 0xFFFFu) / 65535.0f;
            const float    fy = static_cast<float>(ly) + static_cast<float>(h >> 16) / 65535.0f;
            const float    d  = std::sqrt((fx - x) * (fx - x) + (fy - y) * (fy - y));
            if (d < result.f1) {
                result.f2     = result.f1;
                result.f1     = d;
                result.cellId = rnd::hash32(h ^ 0xA511E9B3u);
            } else if (d < result.f2) {
                result.f2 = d;
            }
        }
    }
    return result;
}

float value1D(double t, uint64_t seed) {
    const double fl = std::floor(t);
    const int64_t i = static_cast<int64_t>(fl);
    const float   f = static_cast<float>(t - fl);
    const float   a = rnd::toUnit(rnd::hashCombine(seed, static_cast<uint64_t>(i)));
    const float   b = rnd::toUnit(rnd::hashCombine(seed, static_cast<uint64_t>(i + 1)));
    const float   s = f * f * (3.0f - 2.0f * f);
    return a + (b - a) * s;
}

float white(int x, int y, uint32_t seed) {
    return static_cast<float>(rnd::hash2i(x, y, seed) >> 8) * (1.0f / 16777216.0f);
}

} // namespace noise
