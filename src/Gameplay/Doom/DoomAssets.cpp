// ---------------------------------------------------------------------------
// DoomAssets.cpp
// Painting happens on a float RGB "canvas" with a coverage mask; finished
// pictures are gritted, edge-darkened and ordered-dithered into the palette.
// Canvas coordinates: x right, y down, +z towards the viewer. Shapes are lit
// from the upper left, the way the original's hand-painted sprites were.
// ---------------------------------------------------------------------------
#include "Gameplay/Doom/DoomAssets.h"

#include "Math/Noise.h"
#include "Math/Random.h"
#include "Render/BitmapFont.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>

namespace doom {
namespace {

using glm::vec2;
using glm::vec3;

constexpr float kPi = 3.14159265f;

// ============================================================================
// Canvas and primitives
// ============================================================================

struct Canvas {
    int w, h;
    std::vector<vec3>    rgb;
    std::vector<uint8_t> cover;

    Canvas(int width, int height)
        : w(width), h(height), rgb(static_cast<size_t>(width * height), vec3(0.0f)),
          cover(static_cast<size_t>(width * height), 0) {}

    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    size_t idx(int x, int y) const { return static_cast<size_t>(y * w + x); }
    bool opaque(int x, int y) const { return inside(x, y) && cover[idx(x, y)] != 0; }
    vec3& at(int x, int y) { return rgb[idx(x, y)]; }
    void put(int x, int y, const vec3& c) {
        if (!inside(x, y)) return;
        rgb[idx(x, y)] = c;
        cover[idx(x, y)] = 1;
    }
    /// Fills every pixel from a function of its coordinates.
    void fill(const std::function<vec3(int, int)>& f) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) put(x, y, f(x, y));
    }
};

/// Diffuse lighting from the upper left, with a floor of ambient.
float shade(const vec3& n) {
    static const vec3 kLight = glm::normalize(vec3(-0.45f, -0.55f, 0.7f));
    return 0.3f + 0.85f * std::max(0.0f, glm::dot(n, kLight));
}

float white(int x, int y, uint32_t seed) { return noise::white(x, y, seed); }

/// Periodic fBm over a `size`-texel tile, `cells` lattice cells across.
float fbmTile(float x, float y, int size, int cells, int octaves, uint32_t seed) {
    const float s = static_cast<float>(cells) / static_cast<float>(size);
    return noise::fbm(x * s, y * s, cells, cells, octaves, seed);
}
float fbmTile(int x, int y, int size, int cells, int octaves, uint32_t seed) {
    return fbmTile(static_cast<float>(x), static_cast<float>(y), size, cells, octaves, seed);
}

/// A lit ellipsoid ("blob"): heads, bodies, hands. `mottle` blotches the skin.
void ellipse(Canvas& cv, vec2 c, vec2 r, const vec3& col, uint32_t seed, float grit = 0.2f, float mottle = 0.0f) {
    for (int y = static_cast<int>(std::floor(c.y - r.y)); y <= static_cast<int>(std::ceil(c.y + r.y)); ++y) {
        for (int x = static_cast<int>(std::floor(c.x - r.x)); x <= static_cast<int>(std::ceil(c.x + r.x)); ++x) {
            const float u = (static_cast<float>(x) + 0.5f - c.x) / r.x, v = (static_cast<float>(y) + 0.5f - c.y) / r.y;
            const float d2 = u * u + v * v;
            if (d2 > 1.0f) continue;
            float k = shade(vec3(u, v, std::sqrt(1.0f - d2))) * (1.0f + grit * (white(x, y, seed) - 0.5f));
            if (mottle > 0.0f) k *= 1.0f - mottle * std::max(0.0f, noise::fbm(x * 0.35f, y * 0.35f, 64, 64, 2, seed + 7));
            cv.put(x, y, col * k);
        }
    }
}

/// A lit, tapered cylinder from a (radius ra) to b (radius rb): limbs, barrels.
void capsule(Canvas& cv, vec2 a, vec2 b, float ra, float rb, const vec3& col, uint32_t seed, float grit = 0.2f) {
    const float rmax = std::max(ra, rb);
    const int x0 = static_cast<int>(std::floor(std::min(a.x, b.x) - rmax)), x1 = static_cast<int>(std::ceil(std::max(a.x, b.x) + rmax));
    const int y0 = static_cast<int>(std::floor(std::min(a.y, b.y) - rmax)), y1 = static_cast<int>(std::ceil(std::max(a.y, b.y) + rmax));
    const vec2 ab = b - a;
    const float len2 = std::max(glm::dot(ab, ab), 1e-6f);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const vec2 p(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
            const float t = std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f);
            const float r = ra + (rb - ra) * t;
            const vec2 o = (p - (a + ab * t)) / r;
            const float d2 = glm::dot(o, o);
            if (d2 > 1.0f) continue;
            cv.put(x, y, col * shade(vec3(o.x, o.y, std::sqrt(1.0f - d2))) * (1.0f + grit * (white(x, y, seed) - 0.5f)));
        }
    }
}

/// Scanline polygon fill; the colour may vary per pixel.
void polygon(Canvas& cv, const std::vector<vec2>& pts, const std::function<vec3(int, int)>& colorAt) {
    float ymin = 1e9f, ymax = -1e9f;
    for (const vec2& p : pts) {
        ymin = std::min(ymin, p.y);
        ymax = std::max(ymax, p.y);
    }
    std::vector<float> xs;
    for (int y = static_cast<int>(std::floor(ymin)); y <= static_cast<int>(std::ceil(ymax)); ++y) {
        const float yc = static_cast<float>(y) + 0.5f;
        xs.clear();
        for (size_t i = 0; i < pts.size(); ++i) {
            const vec2 a = pts[i], b = pts[(i + 1) % pts.size()];
            if ((a.y <= yc && b.y > yc) || (b.y <= yc && a.y > yc)) xs.push_back(a.x + (yc - a.y) / (b.y - a.y) * (b.x - a.x));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t i = 0; i + 1 < xs.size(); i += 2) {
            for (int x = static_cast<int>(std::ceil(xs[i] - 0.5f)); x <= static_cast<int>(std::floor(xs[i + 1] - 0.5f)); ++x) {
                cv.put(x, y, colorAt(x, y));
            }
        }
    }
}

void polygon(Canvas& cv, const std::vector<vec2>& pts, const vec3& col, uint32_t seed, float grit = 0.2f) {
    polygon(cv, pts, [&](int x, int y) { return col * (1.0f + grit * (white(x, y, seed) - 0.5f)); });
}

void rect(Canvas& cv, int x0, int y0, int x1, int y1, const vec3& col) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) cv.put(x, y, col);
}

/// Text in the built-in 5x7 font.
void text(Canvas& cv, int x, int y, const char* s, const vec3& col, int scale = 1) {
    for (; *s; ++s, x += 6 * scale) {
        const uint8_t* rows = font::glyph(*s);
        if (!rows) continue;
        for (int r = 0; r < font::kGlyphH; ++r)
            for (int c = 0; c < font::kGlyphW; ++c)
                if (rows[r] & (0x10 >> c)) rect(cv, x + c * scale, y + r * scale, x + c * scale + scale - 1, y + r * scale + scale - 1, col);
    }
}

/// Darkens the outermost opaque pixels: sprites read against any background.
void darkenEdges(Canvas& cv, float k = 0.5f) {
    std::vector<size_t> edge;
    for (int y = 0; y < cv.h; ++y)
        for (int x = 0; x < cv.w; ++x)
            if (cv.opaque(x, y) && (!cv.opaque(x - 1, y) || !cv.opaque(x + 1, y) || !cv.opaque(x, y - 1) || !cv.opaque(x, y + 1)))
                edge.push_back(cv.idx(x, y));
    for (size_t i : edge) cv.rgb[i] *= k;
}

/// Multiplies in per-pixel grit (the hand-dithered look of scanned clay).
void grit(Canvas& cv, float amount, uint32_t seed) {
    for (int y = 0; y < cv.h; ++y)
        for (int x = 0; x < cv.w; ++x) cv.at(x, y) *= 1.0f + amount * (white(x, y, seed) - 0.5f);
}

/// A splash of blood: a few overlapping dark and bright red drops.
void bloodSplat(Canvas& cv, vec2 c, float r, uint32_t seed) {
    rnd::Rng rng(seed);
    for (int i = 0; i < 6; ++i) {
        const vec2 p = c + vec2(rng.range(-r, r), rng.range(-r * 0.5f, r * 0.5f));
        const float s = rng.range(0.25f, 0.6f) * r;
        ellipse(cv, p, vec2(s, s * rng.range(0.5f, 1.0f)), vec3(rng.range(0.45f, 0.8f), 0.04f, 0.03f), seed + i, 0.3f);
    }
}

/// Fire: a noisy radial gradient through white, yellow, orange and red.
void fireBall(Canvas& cv, vec2 c, float r, float ring, float smoke, uint32_t seed) {
    for (int y = 0; y < cv.h; ++y) {
        for (int x = 0; x < cv.w; ++x) {
            const float d = glm::length(vec2(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f) - c) / r;
            const float n = noise::fbm(x * 0.3f, y * 0.3f, 64, 64, 3, seed);
            float heat = 1.0f - d + 0.45f * n;
            if (ring > 0.0f) heat -= ring * std::max(0.0f, 0.6f - d); // hollowing out as it burns away
            if (heat < 0.12f) continue;
            vec3 col = heat > 0.85f ? vec3(1.0f, 1.0f, 0.8f) : heat > 0.6f ? vec3(1.0f, 0.9f, 0.3f)
                     : heat > 0.38f ? vec3(1.0f, 0.55f, 0.1f) : vec3(0.8f, 0.15f, 0.05f);
            col = glm::mix(col, vec3(0.3f, 0.12f, 0.08f), smoke * (1.0f - heat));
            cv.put(x, y, col);
        }
    }
}

// ============================================================================
// Quantisation
// ============================================================================

const int kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

Image toImage(const Canvas& cv, const DoomAssets& a, float dither = 1.0f, bool fullbright = false) {
    Image img;
    img.w = cv.w;
    img.h = cv.h;
    img.fullbright = fullbright;
    img.px.resize(static_cast<size_t>(cv.w * cv.h));
    for (int y = 0; y < cv.h; ++y) {
        for (int x = 0; x < cv.w; ++x) {
            const size_t i = cv.idx(x, y);
            if (!cv.cover[i]) {
                img.px[i] = kTransparent;
                continue;
            }
            const float d = dither * (static_cast<float>(kBayer4[y & 3][x & 3]) / 16.0f - 0.47f) / 18.0f;
            const vec3 c = cv.rgb[i] + d;
            img.px[i] = a.nearest(c.r, c.g, c.b);
        }
    }
    return img;
}

// ============================================================================
// Walls (64 x 64, tile horizontally; the wall is one texture tall)
// ============================================================================

vec3 bevel(vec3 c, int u, int period) {
    const int m = u % period;
    if (m == 0) return c * 0.35f;             // the seam
    if (m == 1) return c * 1.25f;             // lit lip below / right of it
    if (m == period - 1) return c * 0.7f;     // shadowed lip above / left of it
    return c;
}

Canvas wallTech(uint32_t seed) {
    // Tan tech panelling: three rows of panels, rivets, grime streaks, a baseboard.
    Canvas cv(64, 64);
    cv.fill([&](int x, int y) {
        const float n = fbmTile(x, y, 64, 8, 4, seed);
        const float streak = std::max(0.0f, fbmTile(static_cast<float>(x), y * 0.12f, 64, 16, 2, seed + 3));
        vec3 c = vec3(0.64f, 0.56f, 0.42f) * (0.85f + 0.25f * n) * (1.0f - 0.5f * streak);
        if (y >= 57) return bevel(vec3(0.30f, 0.25f, 0.18f) * (0.9f + 0.2f * n), y - 57, 7);
        c = bevel(bevel(c, y, 19), x, 32);
        const int px = x % 32, py = y % 19;
        if ((px == 4 || px == 27) && (py == 4 || py == 15)) c = vec3(0.85f, 0.8f, 0.7f);
        if ((px == 4 || px == 27) && (py == 5 || py == 16)) c *= 0.5f;
        return c;
    });
    return cv;
}

Canvas wallBrown(uint32_t seed) {
    // Brown brick: four courses, staggered, each brick its own tone.
    Canvas cv(64, 64);
    cv.fill([&](int x, int y) {
        const int row = y / 16, bx = (x + (row % 2) * 16) % 64, brick = bx / 32 + row * 2;
        const int u = bx % 32, v = y % 16;
        const float tone = 0.8f + 0.35f * rnd::toUnit(rnd::hashCoords(seed, brick, row));
        const float n = fbmTile(x, y, 64, 8, 4, seed);
        vec3 c = vec3(0.52f, 0.36f, 0.22f) * tone * (0.8f + 0.3f * n);
        const noise::Cellular w = noise::worley(x / 8.0f, y / 8.0f, 8, 8, seed + 1);
        if (w.f2 - w.f1 < 0.05f) c *= 0.65f; // cracks
        if (u < 2 || v < 2) return vec3(0.16f, 0.13f, 0.1f) * (0.8f + 0.3f * n); // mortar
        if (u == 2 || v == 2) c *= 1.2f;
        if (u == 31 || v == 15) c *= 0.7f;
        return c;
    });
    return cv;
}

Canvas wallStone(uint32_t seed) {
    // Grey rock: cellular slabs with dark cracks between them.
    Canvas cv(64, 64);
    cv.fill([&](int x, int y) {
        const noise::Cellular w = noise::worley(x / 12.0f, y / 12.0f, 5, 5, seed);
        const float n = fbmTile(x, y, 64, 8, 5, seed + 1);
        const float tone = 0.85f + 0.3f * rnd::toUnit(rnd::splitmix64(w.cellId));
        vec3 c = vec3(0.50f, 0.48f, 0.44f) * tone * (0.75f + 0.35f * n);
        const float edge = w.f2 - w.f1;
        if (edge < 0.06f) c *= 0.35f;
        else if (edge < 0.12f) c *= 0.8f;
        return c;
    });
    return cv;
}

Canvas wallMetal(uint32_t seed) {
    // Rusted steel: vertical ribs, a riveted band, rust blooms and scratches.
    Canvas cv(64, 64);
    cv.fill([&](int x, int y) {
        const float n = fbmTile(x, y, 64, 8, 4, seed);
        const float rust = std::clamp(fbmTile(x, y, 64, 4, 4, seed + 5) * 2.0f - 0.1f, 0.0f, 1.0f);
        vec3 c = glm::mix(vec3(0.36f, 0.37f, 0.38f), vec3(0.45f, 0.24f, 0.12f), rust) * (0.8f + 0.3f * n);
        c = bevel(c, x, 16);
        if (y >= 29 && y <= 34) {
            c = bevel(vec3(0.30f, 0.30f, 0.31f) * (0.9f + 0.2f * n), y - 29, 6);
            if (y == 31 && x % 8 == 4) c = vec3(0.7f, 0.7f, 0.68f);
        }
        if (white(x / 2, y * 3, seed) > 0.985f) c *= 1.5f; // scratches
        return c;
    });
    return cv;
}

Canvas wallMarble(uint32_t seed) {
    // Green marble in a carved frame, veined, with blood running down it.
    Canvas cv(64, 64);
    rnd::Rng rng(seed);
    int drip[64] = {};
    for (int i = 0; i < 5; ++i) {
        const int x = rng.rangeInt(6, 57), len = rng.rangeInt(10, 40);
        for (int k = 0; k < 2; ++k) drip[std::min(63, x + k)] = std::max(drip[std::min(63, x + k)], len - k * 6);
    }
    cv.fill([&](int x, int y) {
        const float n = fbmTile(x, y, 64, 8, 4, seed);
        const float vein = noise::ridged(x / 16.0f, y / 16.0f, 4, 4, 5, seed + 2);
        vec3 c = vec3(0.30f, 0.42f, 0.34f) * (0.8f + 0.3f * n);
        if (vein > 0.82f) c = glm::mix(c, vec3(0.75f, 0.8f, 0.72f), (vein - 0.82f) * 4.0f);
        const int e = std::min(std::min(x, 63 - x), std::min(y, 63 - y));
        if (e < 4) c = vec3(0.24f, 0.26f, 0.22f) * (e == 0 ? 0.6f : e == 3 ? 0.7f : 1.1f) * (0.9f + 0.2f * n);
        if (y < drip[x]) c = vec3(0.5f + 0.2f * n, 0.04f, 0.03f);
        return c;
    });
    return cv;
}

Canvas wallHell(uint32_t seed) {
    // Dark red rock split by glowing cracks.
    Canvas cv(64, 64);
    cv.fill([&](int x, int y) {
        const noise::Cellular w = noise::worley(x / 10.0f, y / 10.0f, 6, 6, seed);
        const float n = fbmTile(x, y, 64, 8, 5, seed + 1);
        vec3 c = vec3(0.34f, 0.12f, 0.07f) * (0.7f + 0.45f * n);
        const float edge = w.f2 - w.f1;
        if (edge < 0.09f) c = glm::mix(vec3(1.0f, 0.75f, 0.2f), vec3(0.8f, 0.2f, 0.04f), edge / 0.09f);
        return c;
    });
    return cv;
}

Canvas wallComputer(uint32_t seed, bool blink) {
    // A bank of computers: a text display, rows of status lights, vents.
    Canvas cv(64, 64);
    rnd::Rng rng(seed + (blink ? 1u : 0u));
    rnd::Rng layout(seed);
    cv.fill([&](int x, int y) {
        const float n = fbmTile(x, y, 64, 8, 3, seed);
        vec3 c = vec3(0.22f, 0.23f, 0.25f) * (0.85f + 0.3f * n);
        c = bevel(c, x, 64);
        if (y == 0) c *= 0.4f;
        if (y >= 52 && y <= 60 && x >= 8 && x <= 55) c *= (y % 2 == 0) ? 0.35f : 1.1f; // vents
        return c;
    });
    rect(cv, 5, 5, 58, 28, vec3(0.1f, 0.1f, 0.11f));
    rect(cv, 6, 6, 57, 27, vec3(0.02f, 0.06f, 0.03f));
    for (int row = 0; row < 5; ++row) {
        int x = 8;
        while (x < 54) {
            const int len = layout.rangeInt(2, 9);
            if (layout.chance(0.75f)) rect(cv, x, 8 + row * 4, std::min(55, x + len), 9 + row * 4, vec3(0.2f, 0.85f, 0.35f));
            x += len + 2;
        }
    }
    const vec3 lights[] = {vec3(1.0f, 0.15f, 0.1f), vec3(0.2f, 1.0f, 0.3f), vec3(1.0f, 0.85f, 0.2f)};
    for (int row = 0; row < 3; ++row) {
        for (int i = 0; i < 9; ++i) {
            const vec3 col = lights[layout.next() % 3];
            const bool on = rng.chance(0.6f);
            rect(cv, 8 + i * 6, 33 + row * 6, 9 + i * 6, 34 + row * 6, on ? col : col * 0.25f);
        }
    }
    return cv;
}

Canvas wallDoor(uint32_t seed) {
    // A steel door: riveted plates, a centre seam, hazard stripes at the foot.
    Canvas cv(64, 64);
    cv.fill([&](int x, int y) {
        const float n = fbmTile(x, y, 64, 8, 4, seed);
        vec3 c = vec3(0.50f, 0.50f, 0.52f) * (0.8f + 0.3f * n);
        c = bevel(c, y, 13);
        if (x == 31) c *= 0.3f;
        if (x == 32) c *= 1.2f;
        if (x < 4 || x > 59) c = bevel(vec3(0.3f, 0.3f, 0.32f) * (0.9f + 0.2f * n), x < 4 ? x : 63 - x, 4);
        if ((x == 7 || x == 56 || x == 27 || x == 36) && y % 13 == 6) c = vec3(0.85f, 0.85f, 0.8f);
        if (y >= 54) c = ((x + y) / 5) % 2 ? vec3(0.9f, 0.75f, 0.1f) * (0.85f + 0.2f * n) : vec3(0.08f, 0.07f, 0.05f);
        if (y == 53) c *= 0.4f;
        return c;
    });
    return cv;
}

Canvas wallSwitch(uint32_t seed, bool on) {
    // Tech panelling with the exit switch box and an EXIT sign.
    Canvas cv = wallTech(seed);
    rect(cv, 16, 3, 47, 14, vec3(0.08f, 0.06f, 0.05f));
    text(cv, 20, 5, "EXIT", vec3(1.0f, 0.15f, 0.08f));
    rect(cv, 22, 20, 41, 47, vec3(0.12f, 0.12f, 0.13f));
    rect(cv, 23, 21, 40, 46, vec3(0.3f, 0.3f, 0.32f));
    rect(cv, 24, 22, 39, 22, vec3(0.45f, 0.45f, 0.47f));
    rect(cv, 29, 26, 34, 42, vec3(0.08f, 0.08f, 0.08f)); // the lever's slot
    if (on) rect(cv, 30, 34, 33, 41, vec3(0.75f, 0.72f, 0.65f));
    else rect(cv, 30, 27, 33, 34, vec3(0.75f, 0.72f, 0.65f));
    rect(cv, 36, 43, 38, 45, on ? vec3(0.2f, 1.0f, 0.3f) : vec3(1.0f, 0.15f, 0.1f));
    return cv;
}

// ============================================================================
// Flats (64 x 64, tile both ways)
// ============================================================================

Canvas flatTiles(uint32_t seed) {
    Canvas cv(64, 64);
    cv.fill([&](int x, int y) {
        const int tx = x / 16, ty = y / 16, u = x % 16, v = y % 16;
        const float tone = 0.85f + 0.25f * rnd::toUnit(rnd::hashCoords(seed, tx, ty));
        const float n = fbmTile(x, y, 64, 8, 4, seed);
        vec3 c = vec3(0.46f, 0.43f, 0.38f) * tone * (0.8f + 0.3f * n);
        if (u == 0 || v == 0) return vec3(0.15f, 0.13f, 0.11f);
        if (u == 1 || v == 1) c *= 1.15f;
        if (u == 15 || v == 15) c *= 0.75f;
        return c;
    });
    return cv;
}

Canvas flatPlate(uint32_t seed) {
    // Diamond tread plate in riveted sheets.
    Canvas cv(64, 64);
    cv.fill([&](int x, int y) {
        const float n = fbmTile(x, y, 64, 8, 4, seed);
        const float rust = std::clamp(fbmTile(x, y, 64, 4, 3, seed + 4) * 2.5f - 0.3f, 0.0f, 1.0f);
        vec3 c = glm::mix(vec3(0.40f, 0.41f, 0.43f), vec3(0.42f, 0.26f, 0.14f), rust * 0.7f) * (0.8f + 0.3f * n);
        const int u = (x + ((y / 8) % 2) * 4) % 8, v = y % 8;
        if ((u == 2 && v >= 2 && v <= 4) || (u == 3 && v >= 3 && v <= 5)) c *= u == 2 ? 1.35f : 0.7f; // raised ribs
        c = bevel(bevel(c, x, 32), y, 32);
        if ((x % 32 == 3 || x % 32 == 28) && (y % 32 == 3 || y % 32 == 28)) c = vec3(0.7f, 0.7f, 0.68f);
        return c;
    });
    return cv;
}

Canvas flatDirt(uint32_t seed) {
    Canvas cv(64, 64);
    cv.fill([&](int x, int y) {
        const float n = fbmTile(x, y, 64, 8, 5, seed);
        const noise::Cellular w = noise::worley(x / 4.0f, y / 4.0f, 16, 16, seed + 1);
        vec3 c = vec3(0.40f, 0.30f, 0.20f) * (0.7f + 0.4f * n);
        if (w.f1 < 0.18f) c *= 0.75f + 0.8f * rnd::toUnit(rnd::splitmix64(w.cellId));
        return c;
    });
    return cv;
}

Canvas flatHellRock(uint32_t seed) {
    Canvas cv(64, 64);
    cv.fill([&](int x, int y) {
        const noise::Cellular w = noise::worley(x / 16.0f, y / 16.0f, 4, 4, seed);
        const float n = fbmTile(x, y, 64, 8, 5, seed + 1);
        vec3 c = vec3(0.30f, 0.11f, 0.07f) * (0.7f + 0.45f * n);
        if (w.f2 - w.f1 < 0.05f) c = vec3(0.9f, 0.35f, 0.06f);
        return c;
    });
    return cv;
}

Canvas flatNukage(uint32_t seed, int frame) {
    // Glowing green slime; frames shift the swirls (by whole texels, so it still tiles).
    Canvas cv(64, 64);
    cv.fill([&](int x, int y) {
        const int sx = (x + frame * 21) % 64, sy = (y + frame * 11) % 64;
        const float n = fbmTile(static_cast<float>(sx), static_cast<float>(sy), 64, 8, 4, seed);
        const float m = fbmTile(static_cast<float>(x), static_cast<float>((y + 64 - frame * 7) % 64), 64, 4, 3, seed + 1);
        vec3 c = vec3(0.22f, 0.7f, 0.18f) * (0.75f + 0.4f * n + 0.2f * m);
        if (n + m > 0.55f) c = vec3(0.6f, 1.0f, 0.4f);
        return c;
    });
    return cv;
}

Canvas flatCeiling(uint32_t seed, bool light) {
    Canvas cv(64, 64);
    cv.fill([&](int x, int y) {
        const float n = fbmTile(x, y, 64, 8, 4, seed);
        const float stain = std::max(0.0f, fbmTile(x, y, 64, 4, 3, seed + 2) - 0.2f);
        vec3 c = vec3(0.52f, 0.50f, 0.46f) * (0.85f + 0.2f * n) * (1.0f - stain);
        return bevel(bevel(c, x, 32), y, 32);
    });
    if (light) {
        rect(cv, 11, 11, 52, 52, vec3(0.2f, 0.2f, 0.2f));
        for (int y = 13; y <= 50; ++y)
            for (int x = 13; x <= 50; ++x) {
                const bool grid = (x - 13) % 13 == 0 || (y - 13) % 13 == 0;
                cv.put(x, y, grid ? vec3(0.6f, 0.6f, 0.55f) : vec3(1.0f, 0.98f, 0.9f));
            }
    }
    return cv;
}

Canvas paintSky(int w, int h, uint32_t seed) {
    // A blood-red sky over black mountains; wraps horizontally.
    Canvas cv(w, h);
    const int cells = w / 32;
    cv.fill([&](int x, int y) {
        const float t = static_cast<float>(y) / static_cast<float>(h - 1);
        const float cloud = noise::fbm(x / 32.0f, y / 16.0f, cells, 64, 5, seed);
        vec3 c = glm::mix(vec3(0.25f, 0.03f, 0.02f), vec3(0.9f, 0.38f, 0.1f), t * t) * (0.8f + 0.45f * cloud);
        const float ridge = 0.62f + 0.22f * noise::fbm(x / 40.0f, 0.5f, std::max(1, w / 40), 8, 5, seed + 9);
        const float my = ridge * static_cast<float>(h);
        if (static_cast<float>(y) > my) {
            c = vec3(0.09f, 0.03f, 0.02f) * (0.8f + 0.3f * noise::fbm(x / 8.0f, y / 8.0f, std::max(1, w / 8), 64, 3, seed + 3));
            if (static_cast<float>(y) < my + 2.0f) c = vec3(0.45f, 0.1f, 0.04f);
        }
        return c;
    });
    return cv;
}

// ============================================================================
// Monsters
// ============================================================================

struct Body {
    vec3 skin, torso, legs, boots, eye;
    bool imp;
    uint32_t seed;
};

enum class Arms { Down, Raised, Throw, Aim };

struct Pose {
    float walk = 0.0f;      ///< Walk cycle phase, 0..1.
    Arms  arms = Arms::Down;
    vec2  head{0.0f};       ///< Head offset (pain recoil).
    bool  mouthOpen = false;
    bool  flash = false;    ///< Muzzle flash (trooper firing).
};

/// A 48 x 64 humanoid facing the viewer, feet on the bottom row.
Canvas paintHumanoid(const Body& b, const Pose& p) {
    Canvas cv(48, 64);
    const uint32_t s = b.seed;
    const float swing = std::sin(p.walk * 2.0f * kPi);
    const float liftL = std::max(0.0f, swing), liftR = std::max(0.0f, -swing);
    const vec3 bone(0.9f, 0.86f, 0.72f);
    // The imp is wiry and long-limbed; the trooper is a stocky man in fatigues.
    const float hipX = b.imp ? 4.0f : 5.0f, thigh = b.imp ? 2.8f : 3.8f, shin = b.imp ? 2.1f : 3.0f;
    const float shoulderX = b.imp ? 8.0f : 10.0f, upperArm = b.imp ? 2.4f : 3.4f, foreArm = b.imp ? 1.9f : 2.8f;

    auto leg = [&](float side, float lift) {
        const vec2 hip(24.0f + side * hipX, 38.0f), knee(24.0f + side * (hipX + 1.0f + lift * 1.5f), 48.0f - lift * 3.0f);
        const vec2 foot(24.0f + side * (hipX + 0.5f), 59.0f - lift * 4.0f);
        capsule(cv, hip, knee, thigh, thigh * 0.8f, b.legs, s);
        capsule(cv, knee, foot, shin, shin * 0.8f, b.legs * 0.9f, s + 1);
        ellipse(cv, foot + vec2(0.0f, 1.5f), b.imp ? vec2(3.0f, 1.8f) : vec2(3.8f, 2.2f), b.boots, s + 2);
        if (b.imp) { // talons
            for (int k = -1; k <= 1; ++k) cv.put(static_cast<int>(foot.x) + k * 2, static_cast<int>(foot.y + 3.5f), bone);
        }
    };
    leg(-1.0f, liftL);
    leg(1.0f, liftR);

    // Torso.
    if (b.imp) {
        // Broad shoulders tapering to a narrow waist, ribs showing through the hide.
        ellipse(cv, {24.0f, 36.0f}, {4.8f, 3.2f}, b.legs, s + 3, 0.2f, 0.3f);
        capsule(cv, {24.0f, 23.0f}, {24.0f, 33.0f}, 7.0f, 4.2f, b.torso, s + 4, 0.25f);
        for (int y = 25; y <= 30; y += 2)
            for (int x = 20; x <= 28; ++x)
                if (x != 24 && std::abs(x - 24) >= 2 && cv.opaque(x, y)) cv.at(x, y) *= 0.62f;
        for (int y = 24; y <= 32; ++y)
            if (cv.opaque(24, y)) cv.at(24, y) *= 0.75f; // breastbone
    } else {
        capsule(cv, {24.0f, 36.0f}, {24.0f, 36.5f}, 7.0f, 7.0f, b.legs, s + 3);
        capsule(cv, {24.0f, 25.0f}, {24.0f, 33.0f}, 9.5f, 8.5f, b.torso, s + 4, 0.25f);
        rect(cv, 17, 35, 31, 36, vec3(0.2f, 0.14f, 0.08f));
        rect(cv, 23, 35, 25, 36, vec3(0.8f, 0.7f, 0.3f));
    }

    // Arms (shoulder, elbow, hand per side; +1 = the viewer's right).
    auto arm = [&](float side, vec2 elbow, vec2 hand) {
        const vec2 shoulder(24.0f + side * shoulderX, 22.0f);
        capsule(cv, shoulder, elbow, upperArm, upperArm * 0.82f, b.imp ? b.torso : b.torso * 0.95f, s + 5);
        capsule(cv, elbow, hand, foreArm, foreArm * 0.8f, b.skin, s + 6);
        ellipse(cv, hand, vec2(foreArm * 0.95f), b.skin * 0.95f, s + 7);
        if (b.imp) {
            cv.put(static_cast<int>(hand.x) - 1, static_cast<int>(hand.y) + 3, bone);
            cv.put(static_cast<int>(hand.x) + 1, static_cast<int>(hand.y) + 3, bone);
        }
    };
    auto rifleDiagonal = [&]() {
        capsule(cv, {14.0f, 38.0f}, {34.0f, 20.0f}, 1.7f, 1.4f, vec3(0.2f, 0.2f, 0.22f), s + 8);
        capsule(cv, {12.0f, 40.0f}, {17.0f, 36.0f}, 2.4f, 2.0f, vec3(0.4f, 0.25f, 0.12f), s + 9);
    };
    switch (p.arms) {
    case Arms::Down:
        if (b.imp) {
            arm(-1.0f, {13.0f, 31.0f}, {14.0f, 40.0f + swing * 2.0f});
            arm(1.0f, {35.0f, 31.0f}, {34.0f, 40.0f - swing * 2.0f});
        } else {
            rifleDiagonal();
            arm(-1.0f, {13.0f, 31.0f}, {17.0f, 35.0f});
            arm(1.0f, {35.0f, 29.0f}, {31.0f, 25.0f});
        }
        break;
    case Arms::Raised:
        arm(-1.0f, {14.0f, 13.0f}, {17.0f, 5.0f});
        arm(1.0f, {34.0f, 13.0f}, {31.0f, 5.0f});
        break;
    case Arms::Throw:
        arm(-1.0f, {13.0f, 31.0f}, {14.0f, 40.0f});
        arm(1.0f, {33.0f, 27.0f}, {28.0f, 24.0f});
        break;
    case Arms::Aim:
        arm(-1.0f, {15.0f, 31.0f}, {21.0f, 28.0f});
        arm(1.0f, {33.0f, 31.0f}, {27.0f, 28.0f});
        ellipse(cv, {24.0f, 26.0f}, {3.2f, 3.2f}, vec3(0.22f, 0.22f, 0.24f), s + 8);
        cv.put(24, 26, vec3(0.02f));
        break;
    }

    // Head.
    const vec2 hc = vec2(24.0f, b.imp ? 13.0f : 12.5f) + p.head;
    if (b.imp) {
        // Bone spikes on the shoulders and brow.
        for (float side : {-1.0f, 1.0f}) {
            for (int k = 0; k < 3; ++k) {
                const float x = 24.0f + side * (6.5f + static_cast<float>(k) * 2.2f), y = 20.0f + static_cast<float>(k);
                polygon(cv, {{x - 1.0f, y + 1.0f}, {x + 1.0f, y + 1.0f}, {x + side * 1.5f, y - 3.5f}}, bone, s + 10);
            }
        }
        ellipse(cv, hc, {5.0f, 6.2f}, b.skin, s + 11, 0.25f, 0.4f);
        rect(cv, static_cast<int>(hc.x) - 4, static_cast<int>(hc.y) - 3, static_cast<int>(hc.x) + 3, static_cast<int>(hc.y) - 2, b.skin * 0.55f);
    } else {
        ellipse(cv, hc, {5.5f, 6.5f}, b.skin, s + 11, 0.25f, 0.2f);
        for (int y = static_cast<int>(hc.y) - 7; y < static_cast<int>(hc.y) - 3; ++y)
            for (int x = static_cast<int>(hc.x) - 5; x <= static_cast<int>(hc.x) + 5; ++x)
                if (cv.opaque(x, y)) cv.at(x, y) = vec3(0.12f, 0.1f, 0.08f); // cropped hair
    }
    const int ex = static_cast<int>(hc.x), ey = static_cast<int>(hc.y);
    for (int side : {-3, 2}) {
        cv.put(ex + side, ey - 1, b.eye);
        cv.put(ex + side + 1, ey - 1, b.eye * (b.imp ? 1.0f : 0.7f));
    }
    const int mouthH = p.mouthOpen ? 3 : 1;
    rect(cv, ex - 3, ey + 3, ex + 2, ey + 2 + mouthH, vec3(0.1f, 0.02f, 0.02f));
    if (b.imp) {
        for (int x = ex - 3; x <= ex + 2; x += 2) cv.put(x, ey + 3, bone);
    } else {
        cv.put(ex - 1, ey + 5, vec3(0.5f, 0.05f, 0.03f)); // a zombie's bloody chin
        cv.put(ex, ey + 6, vec3(0.5f, 0.05f, 0.03f));
    }

    if (p.arms == Arms::Throw) {
        Canvas fire(12, 12);
        fireBall(fire, {6.0f, 6.0f}, 5.5f, 0.0f, 0.0f, s + 12);
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 12; ++x)
                if (fire.opaque(x, y)) cv.put(22 + x, 15 + y, fire.at(x, y));
    }
    if (p.flash) {
        for (int y = 17; y <= 35; ++y)
            for (int x = 15; x <= 33; ++x) {
                const float dx = static_cast<float>(x) - 24.0f, dy = static_cast<float>(y) - 26.0f;
                const float a = std::atan2(dy, dx), r = 3.5f + 5.5f * std::pow(std::fabs(std::cos(3.0f * a)), 3.0f);
                const float d = std::sqrt(dx * dx + dy * dy) / r;
                if (d < 1.0f) cv.put(x, y, d < 0.45f ? vec3(1.0f, 1.0f, 0.85f) : vec3(1.0f, 0.8f, 0.25f));
            }
    }
    return cv;
}

/// A collapsing body: `src` rotated about its feet, squashed and widened.
Canvas collapse(const Canvas& src, float angle, float sx, float sy, float dark) {
    Canvas cv(src.w, src.h);
    const vec2 pivot(24.0f, 62.0f);
    const float c = std::cos(angle), s = std::sin(angle);
    for (int y = 0; y < cv.h; ++y) {
        for (int x = 0; x < cv.w; ++x) {
            vec2 d(static_cast<float>(x) + 0.5f - pivot.x, static_cast<float>(y) + 0.5f - pivot.y);
            d = vec2(d.x / sx, d.y / sy);
            const vec2 q(c * d.x + s * d.y, -s * d.x + c * d.y);
            const int ix = static_cast<int>(std::floor(pivot.x + q.x)), iy = static_cast<int>(std::floor(pivot.y + q.y));
            if (src.opaque(ix, iy)) cv.put(x, y, src.rgb[src.idx(ix, iy)] * dark);
        }
    }
    return cv;
}

/// Walk (4), attack (2), pain and death (5) frames for one monster.
std::vector<Canvas> paintMonster(const Body& b) {
    std::vector<Canvas> frames;
    for (int i = 0; i < 4; ++i) {
        Pose p;
        p.walk = static_cast<float>(i) / 4.0f + 0.125f;
        frames.push_back(paintHumanoid(b, p));
    }
    Pose attack0, attack1, pain;
    if (b.imp) {
        attack0.arms = Arms::Raised;
        attack1.arms = Arms::Throw;
        attack1.mouthOpen = true;
    } else {
        attack0.arms = Arms::Aim;
        attack1.arms = Arms::Aim;
        attack1.flash = true;
    }
    pain.head = {1.5f, -1.0f};
    pain.mouthOpen = true;
    frames.push_back(paintHumanoid(b, attack0));
    frames.push_back(paintHumanoid(b, attack1));
    Canvas hurt = paintHumanoid(b, pain);
    bloodSplat(hurt, {24.0f, 22.0f}, 4.0f, b.seed + 20);
    frames.push_back(hurt);

    const float angles[] = {-0.12f, 0.35f, 0.9f, 1.3f, 1.45f};
    const float squash[] = {0.95f, 0.8f, 0.55f, 0.36f, 0.24f};
    const float widen[] = {1.0f, 1.05f, 1.15f, 1.3f, 1.45f};
    for (int k = 0; k < 5; ++k) {
        Canvas d = collapse(hurt, angles[k] * (b.imp ? 1.0f : -1.0f), widen[k], squash[k], 1.0f - 0.07f * static_cast<float>(k));
        bloodSplat(d, {24.0f, 62.0f - 30.0f * squash[k]}, 3.0f + 1.5f * static_cast<float>(k), b.seed + 30 + k);
        if (k >= 2) {
            // A spreading pool beneath it.
            ellipse(d, {24.0f, 61.5f}, {6.0f + 3.0f * static_cast<float>(k), 1.5f + 0.5f * static_cast<float>(k)},
                    vec3(0.45f, 0.03f, 0.02f), b.seed + 40 + k, 0.3f);
        }
        frames.push_back(std::move(d));
    }
    return frames;
}

// ============================================================================
// Weapons
// ============================================================================

const vec3 kSteel(0.30f, 0.30f, 0.32f), kWood(0.46f, 0.28f, 0.14f), kHand(0.85f, 0.62f, 0.48f), kSleeve(0.25f, 0.38f, 0.18f);

/// Steel shaded like a cylinder across `x0..x1`.
std::function<vec3(int, int)> cylinderShade(const vec3& base, float x0, float x1, uint32_t seed) {
    return [=](int x, int y) {
        const float u = std::clamp((static_cast<float>(x) + 0.5f - (x0 + x1) * 0.5f) / ((x1 - x0) * 0.5f), -1.0f, 1.0f);
        return base * shade(vec3(u, -0.2f, std::sqrt(std::max(0.0f, 1.0f - u * u)))) * (1.0f + 0.15f * (white(x, y, seed) - 0.5f));
    };
}

Canvas paintPistol(float slideBack, uint32_t seed) {
    // Seen from behind, as in the original: held in the right hand and angled
    // in towards the centre - the blued slide running away from you, its back
    // with the rear sight posts and hammer, the fist round the grip with the
    // thumb along its side and the finger on the trigger guard.
    Canvas cv(64, 64);
    const float sb = slideBack;
    const vec3 blued(0.30f, 0.32f, 0.38f);
    polygon(cv, {{24.5f, 10.0f + sb}, {31.5f, 10.0f + sb}, {40.0f, 36.0f}, {26.0f, 36.0f}}, cylinderShade(blued * 1.15f, 25.0f, 38.0f, seed));
    polygon(cv, {{27.5f, 12.0f + sb}, {28.5f, 12.0f + sb}, {33.5f, 35.0f}, {32.5f, 35.0f}}, blued * 2.0f, seed);   // top highlight
    rect(cv, 26, static_cast<int>(7 + sb), 27, static_cast<int>(10 + sb), blued * 0.6f);                         // front sight
    polygon(cv, {{25.0f, 36.0f}, {41.0f, 36.0f}, {41.0f, 44.0f}, {25.0f, 44.0f}}, cylinderShade(blued * 0.7f, 25.0f, 41.0f, seed + 1));
    rect(cv, 26, 33, 28, 36, blued * 0.55f);                                                                     // rear sight posts
    rect(cv, 38, 33, 40, 36, blued * 0.55f);
    rect(cv, 25, 36, 41, 36, blued * 1.4f);                                                                      // edge of the slide
    rect(cv, 32, 44, 34, 47, blued * 0.4f);                                                                      // hammer
    polygon(cv, {{27.0f, 44.0f}, {39.0f, 44.0f}, {38.0f, 49.0f}, {28.0f, 49.0f}}, blued * 0.45f, seed);         // frame
    capsule(cv, {42.0f, 62.0f}, {48.0f, 74.0f}, 10.0f, 12.0f, kSleeve, seed + 2);
    ellipse(cv, {35.0f, 55.0f}, {13.0f, 10.0f}, kHand, seed + 3, 0.15f);
    for (int k = 0; k < 3; ++k) rect(cv, 39, 51 + k * 4, 46 - k, 51 + k * 4, kHand * 0.6f);                      // finger creases
    capsule(cv, {27.0f, 49.5f}, {36.0f, 50.5f}, 2.2f, 2.0f, kHand * 0.95f, seed + 4, 0.15f);                     // trigger finger
    capsule(cv, {23.0f, 52.0f}, {27.0f, 43.0f}, 3.2f, 2.6f, kHand, seed + 5, 0.15f);                             // thumb
    return cv;
}

Canvas paintShotgun(float pump, float drop, uint32_t seed) {
    // The pump gun pointing into the view, as the original draws it: barrel
    // and magazine tube side by side running up the middle of the screen, the
    // grooved wooden fore-end round both, and the receiver with rounded
    // shoulders and a shell in its ejection port - broad and squat, filling
    // the bottom of the view. Hands only show while working the pump.
    // Laid out on a 96 x 72 grid, stretched to kx x ky on the canvas.
    const float kx = 1.5f, ky = 1.05f;
    Canvas cv(static_cast<int>(96 * kx), static_cast<int>(72 * ky));
    auto P = [&](float x, float y) { return vec2(x * kx, y * ky); };
    auto R = [&](float x0, float y0, float x1, float y1, const vec3& c) {
        rect(cv, static_cast<int>(x0 * kx), static_cast<int>(y0 * ky), static_cast<int>(x1 * kx), static_cast<int>(y1 * ky), c);
    };
    const float o = drop, pp = pump + drop;
    polygon(cv, {P(48.0f, 3.0f + o), P(51.0f, 3.0f + o), P(55.0f, 46.0f + o), P(48.5f, 46.0f + o)}, cylinderShade(kSteel * 0.75f, 48.5f * kx, 55.0f * kx, seed));
    polygon(cv, {P(44.5f, o), P(48.0f, o), P(48.5f, 46.0f + o), P(41.0f, 46.0f + o)}, cylinderShade(kSteel * 1.15f, 41.0f * kx, 48.5f * kx, seed + 1));
    polygon(cv, {P(47.8f, 3.0f + o), P(48.2f, 3.0f + o), P(48.8f, 46.0f + o), P(48.2f, 46.0f + o)}, vec3(0.04f), seed); // the seam between them
    polygon(cv, {P(45.5f, 2.0f + o), P(46.1f, 2.0f + o), P(44.6f, 44.0f + o), P(44.0f, 44.0f + o)}, kSteel * 2.0f, seed); // highlight down the barrel
    ellipse(cv, P(46.2f, 1.0f + o), vec2(2.0f * kx, 1.0f * ky), vec3(0.03f), seed);                                   // muzzle
    R(46.0f, o, 46.4f, o, vec3(0.85f, 0.85f, 0.8f));                                                                   // bead sight
    polygon(cv, {P(40.5f, 20.0f + pp), P(56.5f, 20.0f + pp), P(60.0f, 38.0f + pp), P(37.0f, 38.0f + pp)},
            [&](int x, int y) {
                const vec3 c = cylinderShade(kWood, 37.0f * kx, 60.0f * kx, seed + 2)(x, y);
                return (y - static_cast<int>((20 + pp) * ky)) % 3 == 2 ? c * 0.5f : c; // grooves
            });
    // The receiver: a rounded block, broader than the tubes, running off the bottom.
    ellipse(cv, P(48.0f, 52.0f + o), vec2(16.0f * kx, 9.0f * ky), kSteel * 0.85f, seed + 3, 0.15f);
    polygon(cv, {P(32.0f, 52.0f + o), P(64.0f, 52.0f + o), P(66.0f, 72.0f), P(30.0f, 72.0f)}, cylinderShade(kSteel * 0.85f, 30.0f * kx, 66.0f * kx, seed + 3));
    polygon(cv, {P(41.0f, 45.0f + o), P(55.0f, 45.0f + o), P(56.0f, 48.0f + o), P(40.0f, 48.0f + o)}, kSteel * 0.5f, seed); // where the tubes enter it
    R(53.0f, 54.0f + o, 60.0f, 60.0f + o, vec3(0.04f));                                                                // ejection port...
    R(54.0f, 56.0f + o, 58.0f, 58.0f + o, vec3(0.7f, 0.1f, 0.08f));                                                    // ...with a shell in it
    R(58.7f, 56.0f + o, 59.3f, 58.0f + o, vec3(0.9f, 0.75f, 0.3f));
    if (pump > 0.0f) {
        capsule(cv, P(30.0f, 42.0f + pp), P(18.0f, 76.0f), 8.0f * kx, 11.0f * kx, kSleeve, seed + 4);
        ellipse(cv, P(37.0f, 30.0f + pp), vec2(7.0f * kx, 8.5f * ky), kHand, seed + 5, 0.15f);
        for (int k = 0; k < 3; ++k) R(32.0f, 26.0f + pp + k * 4.0f, 36.0f, 26.0f + pp + k * 4.0f, kHand * 0.6f);
    }
    return cv;
}

Canvas paintFlash(int w, int h, int spikes, uint32_t seed) {
    Canvas cv(w, h);
    const vec2 c(static_cast<float>(w) * 0.5f, static_cast<float>(h) * 0.6f);
    const float rmax = static_cast<float>(std::min(w, h)) * 0.5f;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const vec2 d(static_cast<float>(x) + 0.5f - c.x, static_cast<float>(y) + 0.5f - c.y);
            const float a = std::atan2(d.y, d.x);
            const float r = rmax * (0.45f + 0.55f * std::pow(std::fabs(std::cos(0.5f * spikes * a)), 4.0f)) *
                            (0.85f + 0.3f * white(static_cast<int>(a * 10.0f), 0, seed));
            const float t = glm::length(d) / r;
            if (t >= 1.0f) continue;
            cv.put(x, y, t < 0.35f ? vec3(1.0f, 1.0f, 0.9f) : t < 0.7f ? vec3(1.0f, 0.88f, 0.3f) : vec3(1.0f, 0.5f, 0.1f));
        }
    return cv;
}

// ============================================================================
// Items and props
// ============================================================================

Canvas paintBarrel(uint32_t seed, int frame) {
    Canvas cv(22, 30);
    for (int y = 3; y < 30; ++y)
        for (int x = 1; x < 21; ++x) {
            const float u = (static_cast<float>(x) + 0.5f - 11.0f) / 10.0f;
            vec3 c = vec3(0.40f, 0.44f, 0.38f) * shade(vec3(u, 0.0f, std::sqrt(std::max(0.0f, 1.0f - u * u))));
            if (y == 9 || y == 10 || y == 22 || y == 23) c *= 0.6f;
            c *= 0.85f + 0.3f * white(x, y, seed);
            cv.put(x, y, c);
        }
    ellipse(cv, {11.0f, 4.0f}, {10.0f, 3.0f}, vec3(0.3f, 0.32f, 0.3f), seed);
    ellipse(cv, {11.0f, 4.0f}, {8.0f, 2.0f}, frame ? vec3(0.55f, 1.0f, 0.4f) : vec3(0.35f, 0.9f, 0.25f), seed + 1, 0.4f);
    rnd::Rng rng(seed);
    for (int i = 0; i < 3; ++i) {
        const int x = rng.rangeInt(4, 17), len = rng.rangeInt(3, 9);
        rect(cv, x, 5, x, 5 + len, vec3(0.35f, 0.85f, 0.25f));
    }
    return cv;
}

Canvas paintShotgunPickup(uint32_t seed) {
    Canvas cv(42, 12);
    capsule(cv, {3.0f, 4.0f}, {32.0f, 4.0f}, 1.6f, 1.6f, kSteel, seed);
    capsule(cv, {7.0f, 6.5f}, {27.0f, 6.5f}, 1.3f, 1.3f, kSteel * 0.8f, seed + 1);
    capsule(cv, {14.0f, 6.5f}, {24.0f, 6.5f}, 2.3f, 2.3f, kWood, seed + 2);
    rect(cv, 27, 3, 32, 8, kSteel * 0.7f);
    polygon(cv, {{32.0f, 4.0f}, {41.0f, 6.0f}, {41.0f, 11.0f}, {31.0f, 8.5f}}, kWood * 0.9f, seed + 3);
    return cv;
}

Canvas paintBox(int w, int h, const vec3& body, uint32_t seed, int kind /*0 plain, 1 cross, 2 shells, 3 clip*/) {
    Canvas cv(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w) * 2.0f - 1.0f;
            vec3 c = body * (0.75f + 0.35f * (1.0f - u * u) - 0.2f * static_cast<float>(y) / static_cast<float>(h));
            cv.put(x, y, c * (0.9f + 0.2f * white(x, y, seed)));
        }
    const vec3 red(0.85f, 0.08f, 0.06f), brass(0.95f, 0.8f, 0.35f);
    if (kind == 1) {
        rect(cv, w / 2 - 1, 2, w / 2, h - 3, red);
        rect(cv, w / 2 - 4, h / 2 - 1, w / 2 + 3, h / 2, red);
    } else if (kind == 2) {
        for (int x = 2; x < w - 2; x += 3) rect(cv, x, 1, x + 1, 2, brass);
    } else if (kind == 3) {
        rect(cv, 2, 0, w - 3, 1, brass);
    }
    return cv;
}

Canvas paintArmor(uint32_t seed) {
    Canvas cv(26, 18);
    const vec3 green(0.22f, 0.62f, 0.22f);
    ellipse(cv, {13.0f, 10.0f}, {9.0f, 7.5f}, green, seed, 0.25f);
    ellipse(cv, {5.0f, 4.0f}, {4.0f, 3.5f}, green, seed + 1, 0.25f);
    ellipse(cv, {21.0f, 4.0f}, {4.0f, 3.5f}, green, seed + 2, 0.25f);
    rect(cv, 13, 4, 13, 16, green * 0.4f);
    ellipse(cv, {13.0f, 2.0f}, {4.0f, 2.0f}, vec3(0.05f), seed); // the neck hole
    return cv;
}

Canvas paintLamp(uint32_t seed) {
    Canvas cv(12, 52);
    capsule(cv, {6.0f, 14.0f}, {6.0f, 49.0f}, 1.4f, 1.8f, kSteel, seed);
    ellipse(cv, {6.0f, 50.0f}, {5.5f, 2.0f}, kSteel * 0.8f, seed + 1);
    capsule(cv, {6.0f, 3.0f}, {6.0f, 12.0f}, 3.6f, 3.6f, vec3(0.85f, 0.95f, 1.0f), seed + 2, 0.05f);
    rect(cv, 3, 13, 9, 14, kSteel * 0.6f);
    return cv;
}

// ============================================================================
// Status bar, face, digits, title
// ============================================================================

void panel(Canvas& cv, int x0, int y0, int x1, int y1) {
    for (int x = x0; x <= x1; ++x) {
        cv.at(x, y0) *= 1.5f;
        cv.at(x, y1) *= 0.45f;
    }
    for (int y = y0; y <= y1; ++y) {
        cv.at(x0, y) *= 1.5f;
        cv.at(x1, y) *= 0.45f;
    }
}

Canvas paintStatusBar(uint32_t seed) {
    Canvas cv(kScreenW, kStatusH);
    cv.fill([&](int x, int y) {
        const float n = noise::fbm(x / 8.0f, y / 8.0f, 40, 4, 4, seed);
        return vec3(0.40f, 0.38f, 0.34f) * (0.75f + 0.35f * n);
    });
    const int edges[] = {0, 52, 106, 142, 178, 236, 319};
    for (int i = 0; i + 1 < 7; ++i) panel(cv, edges[i], 0, edges[i + 1] - (i + 2 < 7 ? 1 : 0), kStatusH - 1);
    rect(cv, 144, 2, 176, 29, vec3(0.08f, 0.07f, 0.06f));
    const vec3 label(0.82f, 0.8f, 0.74f);
    text(cv, 14, 23, "AMMO", label);
    text(cv, 62, 23, "HEALTH", label);
    text(cv, 113, 23, "ARMS", label);
    text(cv, 192, 23, "ARMOR", label);
    text(cv, 241, 5, "BULL", label);
    text(cv, 241, 13, "SHEL", label);
    return cv;
}

Canvas paintBigGlyph(char ch) {
    Canvas cv(11, 15);
    const uint8_t* rows = font::glyph(ch);
    for (int pass = 0; pass < 2; ++pass) {
        for (int r = 0; r < font::kGlyphH; ++r)
            for (int c = 0; c < font::kGlyphW; ++c) {
                if (!(rows[r] & (0x10 >> c))) continue;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        const int x = c * 2 + dx + (pass == 0 ? 1 : 0), y = r * 2 + dy + (pass == 0 ? 1 : 0);
                        const float t = static_cast<float>(y) / 14.0f;
                        cv.put(x, y, pass == 0 ? vec3(0.1f, 0.02f, 0.02f)
                                               : glm::mix(vec3(1.0f, 0.3f, 0.18f), vec3(0.55f, 0.04f, 0.03f), t) * (dx == 0 ? 1.15f : 1.0f));
                    }
            }
    }
    return cv;
}

/// expression: 0 normal, 1 grin, 2 ouch, 3 dead.
Canvas paintFace(int level, int look, int expression, uint32_t seed) {
    // The marine: a flat-top crop, heavy brows over narrowed eyes, high
    // cheekbones and a square, cleft jaw - all hard planes, not a round face.
    Canvas cv(24, 29);
    const bool dead = expression == 3, grin = expression == 1, ouch = expression == 2;
    const vec3 skin = dead ? vec3(0.62f, 0.56f, 0.5f) : vec3(0.88f, 0.62f, 0.44f);
    const vec3 dark(0.2f, 0.08f, 0.05f);

    // Ears, then the head as an angular outline, shaded in planes.
    rect(cv, 0, 11, 1, 17, skin * 0.62f);
    rect(cv, 22, 11, 23, 17, skin * 0.5f);
    polygon(cv, {{5.0f, 2.0f}, {19.0f, 2.0f}, {21.5f, 5.0f}, {22.0f, 11.0f}, {21.5f, 18.0f}, {19.5f, 23.0f},
                 {15.5f, 27.5f}, {8.5f, 27.5f}, {4.5f, 23.0f}, {2.5f, 18.0f}, {2.0f, 11.0f}, {2.5f, 5.0f}},
            [&](int x, int y) {
                const float u = (static_cast<float>(x) + 0.5f - 12.0f) / 10.0f, v = (static_cast<float>(y) + 0.5f) / 28.0f;
                float k = (1.0f - 0.45f * u * u) * (1.05f - 0.25f * v) * (1.0f - 0.12f * u); // lit from the upper left
                const float au = std::fabs(u);
                if (y >= 14 && y <= 16 && au > 0.4f && au < 0.75f) k *= 1.12f;  // cheekbones
                if (y >= 18 && y <= 21 && au > 0.45f && au < 0.85f) k *= 0.78f; // hollow cheeks
                if (y >= 24) k *= 0.85f;                                         // under the jaw
                return skin * k * (1.0f + 0.12f * (white(x, y, seed) - 0.5f));
            });

    // Flat-top hair and short sideburns.
    const vec3 hair(0.30f, 0.18f, 0.08f);
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 24; ++x) {
            if (!cv.opaque(x, y) || (x < 2 || x > 21)) continue;
            const bool top = static_cast<float>(y) < 5.0f + 1.5f * white(x, 0, seed + 2);
            const bool side = (x < 5 || x > 18) && y < 10;
            if (top || side) cv.at(x, y) = hair * (0.8f + 0.4f * white(x, y, seed + 3));
        }

    // Brows: thick, dipping towards the nose (raised when shocked).
    const int browY = ouch ? 8 : 9;
    for (int e = 0; e < 2; ++e) {
        const int x0 = e == 0 ? 5 : 14;
        for (int i = 0; i < 6; ++i) {
            const bool inner = e == 0 ? i >= 4 : i <= 1;
            rect(cv, x0 + i, browY + (inner && !ouch ? 1 : 0), x0 + i, browY + (inner && !ouch ? 1 : 0), dark * 1.2f);
            if (!ouch) cv.put(x0 + i, browY + (inner ? 2 : 1), skin * 0.55f); // the brow's shadow
        }
    }

    // Eyes.
    for (int e = 0; e < 2; ++e) {
        const int cx = e == 0 ? 6 : 15, ey = 12;
        if (dead) {
            rect(cv, cx, ey, cx + 2, ey, dark);
            continue;
        }
        rect(cv, cx, ey - (ouch ? 1 : 0), cx + 2, ey, vec3(0.95f, 0.93f, 0.88f));
        const int px = cx + (grin ? 1 : look);
        rect(cv, px, ey - (ouch ? 1 : 0), px, ey, vec3(0.12f, 0.18f, 0.32f));
        rect(cv, cx, ey + 1, cx + 2, ey + 1, skin * 0.7f); // bags under them
    }

    // Nose: lit on the left, shadowed on the right, with nostrils.
    rect(cv, 11, 12, 11, 17, skin * 1.1f);
    rect(cv, 13, 12, 13, 17, skin * 0.68f);
    rect(cv, 10, 18, 14, 18, skin * 0.72f);
    cv.put(11, 18, dark);
    cv.put(13, 18, dark);

    // Mouth and chin.
    const vec3 lips(0.45f, 0.18f, 0.12f);
    if (grin) {
        rect(cv, 7, 21, 17, 23, dark);
        rect(cv, 8, 22, 16, 22, vec3(0.95f, 0.93f, 0.85f));
        cv.put(6, 20, skin * 0.6f);
        cv.put(18, 20, skin * 0.6f);
    } else if (ouch || dead) {
        rect(cv, 10, 21, 14, 24, vec3(0.1f, 0.02f, 0.02f));
        rect(cv, 10, 21, 14, 21, lips);
    } else {
        rect(cv, 9, 21, 15, 21, skin * 0.75f); // upper lip shadow
        rect(cv, 9, 22, 15, 22, lips);
        cv.put(8, 23, skin * 0.6f); // a hard set to the mouth
        cv.put(16, 23, skin * 0.6f);
    }
    rect(cv, 12, 25, 12, 26, skin * 0.65f); // cleft chin

    // Injuries: more blood and bruising the lower the health.
    rnd::Rng rng(seed + static_cast<uint32_t>(level) * 97u);
    const vec3 blood(0.6f, 0.04f, 0.03f);
    const int streaks = dead ? 7 : level * 2;
    for (int i = 0; i < streaks; ++i) {
        const int x = rng.rangeInt(4, 19), y0 = rng.rangeInt(3, 10), len = rng.rangeInt(3, 10);
        for (int y = y0; y < y0 + len; ++y)
            if (cv.opaque(x, y)) cv.at(x, y) = blood * rng.range(0.75f, 1.15f);
    }
    if (level >= 2 || dead) rect(cv, 12, 19, 12, 20 + std::min(level, 3), blood); // bloody nose
    for (int i = 0; i < level * 3; ++i) {
        const int x = rng.rangeInt(3, 20), y = rng.rangeInt(6, 26);
        if (cv.opaque(x, y)) cv.at(x, y) *= vec3(0.55f, 0.45f, 0.55f); // bruises
    }
    darkenEdges(cv, 0.6f);
    return cv;
}

Canvas paintTitle(uint32_t seed) {
    Canvas cv = paintSky(kScreenW, kScreenH, seed);
    // The logo: the font blown up into bevelled metal blocks, with a dark outline.
    const char* word = "DOOM";
    const int scale = 8, gap = 6;
    const int wordW = 4 * font::kGlyphW * scale + 3 * gap, x0 = (kScreenW - wordW) / 2, y0 = 22;
    Canvas logo(kScreenW, kScreenH);
    for (int i = 0; i < 4; ++i) {
        const uint8_t* rows = font::glyph(word[i]);
        const int gx = x0 + i * (font::kGlyphW * scale + gap);
        for (int r = 0; r < font::kGlyphH; ++r)
            for (int c = 0; c < font::kGlyphW; ++c) {
                if (!(rows[r] & (0x10 >> c))) continue;
                for (int dy = 0; dy < scale; ++dy)
                    for (int dx = 0; dx < scale; ++dx) {
                        const int x = gx + c * scale + dx, y = y0 + r * scale + dy;
                        const float t = static_cast<float>(y - y0) / static_cast<float>(font::kGlyphH * scale);
                        vec3 col = glm::mix(vec3(1.0f, 0.86f, 0.5f), vec3(0.62f, 0.2f, 0.05f), t);
                        col *= 0.85f + 0.3f * white(x, y, seed + 4);
                        const bool up = r == 0 || !(rows[r - 1] & (0x10 >> c)), down = r == font::kGlyphH - 1 || !(rows[r + 1] & (0x10 >> c));
                        const bool left = c == 0 || !(rows[r] & (0x10 >> (c - 1))), right = c == font::kGlyphW - 1 || !(rows[r] & (0x10 >> (c + 1)));
                        if ((up && dy < 2) || (left && dx < 2)) col *= 1.3f;
                        if ((down && dy >= scale - 2) || (right && dx >= scale - 2)) col *= 0.5f;
                        logo.put(x, y, col);
                    }
            }
    }
    for (int y = 0; y < kScreenH; ++y)
        for (int x = 0; x < kScreenW; ++x) {
            if (logo.opaque(x, y)) cv.put(x, y, logo.at(x, y));
            else if (logo.opaque(x - 2, y - 2) || logo.opaque(x + 2, y + 2) || logo.opaque(x + 2, y - 2) || logo.opaque(x - 2, y + 2))
                cv.put(x, y, vec3(0.05f, 0.01f, 0.01f));
        }
    return cv;
}

} // namespace

// ============================================================================
// DoomAssets
// ============================================================================

namespace {
std::mutex                    g_buildMutex;
std::shared_future<void>      g_build;
std::unique_ptr<DoomAssets>   g_assets;
} // namespace

void DoomAssets::preload() {
    std::lock_guard<std::mutex> lock(g_buildMutex);
    if (g_build.valid()) return;
    g_build = std::async(std::launch::async, [] { g_assets.reset(new DoomAssets()); }).share();
}

const DoomAssets& DoomAssets::get() {
    preload();
    g_build.wait();
    return *g_assets;
}

const Image* DoomAssets::bigGlyph(char c) const {
    if (c >= '0' && c <= '9') return &m_bigGlyphs[static_cast<size_t>(c - '0')];
    if (c == '%') return &m_bigGlyphs[10];
    if (c == '-') return &m_bigGlyphs[11];
    return nullptr;
}

uint8_t DoomAssets::nearest(float r, float g, float b) const {
    auto bin = [](float v) { return std::clamp(static_cast<int>(v * 32.0f), 0, 31); };
    return m_lut[static_cast<size_t>((bin(r) * 32 + bin(g)) * 32 + bin(b))];
}

void DoomAssets::buildPalette() {
    static const vec3 kBases[16] = {
        {0.96f, 0.96f, 0.96f}, {0.62f, 0.42f, 0.24f}, {0.88f, 0.76f, 0.56f}, {1.00f, 0.12f, 0.08f},
        {0.36f, 0.85f, 0.28f}, {0.62f, 0.60f, 0.30f}, {0.55f, 0.62f, 0.74f}, {1.00f, 0.58f, 0.12f},
        {1.00f, 0.96f, 0.40f}, {1.00f, 0.76f, 0.60f}, {0.66f, 0.30f, 0.16f}, {0.30f, 0.42f, 1.00f},
        {0.40f, 1.00f, 0.55f}, {0.66f, 0.30f, 0.72f}, {0.72f, 0.68f, 0.60f}, {0.42f, 0.34f, 0.24f},
    };
    std::array<vec3, 256> pf;
    for (int r = 0; r < 16; ++r)
        for (int s = 0; s < 16; ++s) {
            const float t = static_cast<float>(s + 1) / 16.0f;
            vec3 c = kBases[r] * std::pow(t, 1.1f);
            if (r != Grey && s >= 12) c = glm::mix(c, vec3(1.0f), static_cast<float>(s - 11) * 0.06f);
            pf[static_cast<size_t>(r * 16 + s)] = glm::clamp(c, vec3(0.0f), vec3(1.0f));
        }
    for (size_t i = 0; i < 256; ++i)
        for (int k = 0; k < 3; ++k) m_palette[i][static_cast<size_t>(k)] = static_cast<uint8_t>(std::lround(pf[i][k] * 255.0f));

    auto nearestExact = [&](const vec3& c) {
        float best = 1e9f;
        uint8_t bestIndex = 0;
        for (int i = 0; i < 255; ++i) { // 255 is the transparent key
            const vec3 d = c - pf[static_cast<size_t>(i)];
            const float e = 2.0f * d.r * d.r + 4.0f * d.g * d.g + 3.0f * d.b * d.b;
            if (e < best) {
                best = e;
                bestIndex = static_cast<uint8_t>(i);
            }
        }
        return bestIndex;
    };
    m_lut.resize(32 * 32 * 32);
    for (int r = 0; r < 32; ++r)
        for (int g = 0; g < 32; ++g)
            for (int b = 0; b < 32; ++b)
                m_lut[static_cast<size_t>((r * 32 + g) * 32 + b)] =
                    nearestExact(vec3(static_cast<float>(r) + 0.5f, static_cast<float>(g) + 0.5f, static_cast<float>(b) + 0.5f) / 32.0f);

    // Light diminishing: each level darkens every colour and remaps it to the
    // nearest entry, so shadows band and drift towards the browns.
    m_colormaps.resize(static_cast<size_t>(kColormaps) * 256);
    for (int level = 0; level < kColormaps; ++level) {
        const float f = 1.0f - static_cast<float>(level) / static_cast<float>(kColormaps);
        for (int i = 0; i < 256; ++i) {
            m_colormaps[static_cast<size_t>(level * 256 + i)] =
                level == 0 || i == kTransparent ? static_cast<uint8_t>(i) : nearestExact(pf[static_cast<size_t>(i)] * f);
        }
    }
}

DoomAssets::DoomAssets() {
    const auto start = std::chrono::steady_clock::now();
    buildPalette();
    const uint32_t seed = 0xD0037u;

    // Walls.
    auto setWall = [&](WallTex t, const Canvas& cv) { m_walls[static_cast<size_t>(t)] = toImage(cv, *this, 0.7f); };
    setWall(WallTex::Tech, wallTech(seed + 1));
    setWall(WallTex::Brown, wallBrown(seed + 2));
    setWall(WallTex::Stone, wallStone(seed + 3));
    setWall(WallTex::Metal, wallMetal(seed + 4));
    setWall(WallTex::Marble, wallMarble(seed + 5));
    setWall(WallTex::Hell, wallHell(seed + 6));
    setWall(WallTex::Computer, wallComputer(seed + 7, false));
    setWall(WallTex::ComputerBlink, wallComputer(seed + 7, true));
    setWall(WallTex::Door, wallDoor(seed + 8));
    setWall(WallTex::Switch, wallSwitch(seed + 1, false));
    setWall(WallTex::SwitchOn, wallSwitch(seed + 1, true));

    // Flats.
    auto setFlat = [&](FlatTex t, const Canvas& cv) { m_flats[static_cast<size_t>(t)] = toImage(cv, *this, 0.7f); };
    setFlat(FlatTex::Tiles, flatTiles(seed + 11));
    setFlat(FlatTex::Plate, flatPlate(seed + 12));
    setFlat(FlatTex::Dirt, flatDirt(seed + 13));
    setFlat(FlatTex::HellRock, flatHellRock(seed + 14));
    for (int k = 0; k < 3; ++k) setFlat(static_cast<FlatTex>(static_cast<int>(FlatTex::Nukage0) + k), flatNukage(seed + 15, k));
    setFlat(FlatTex::CeilTile, flatCeiling(seed + 16, false));
    setFlat(FlatTex::CeilLight, flatCeiling(seed + 16, true));
    m_sky = toImage(paintSky(256, 128, seed + 17), *this, 0.5f);

    // Monsters.
    auto setSprite = [&](Spr s, Canvas cv, bool fullbright = false, bool edges = true) {
        if (edges) darkenEdges(cv, 0.5f);
        m_sprites[static_cast<size_t>(s)] = toImage(cv, *this, 1.0f, fullbright);
    };
    const Body imp{{0.58f, 0.38f, 0.24f}, {0.55f, 0.36f, 0.22f}, {0.42f, 0.27f, 0.16f}, {0.35f, 0.22f, 0.14f},
                   {1.0f, 0.8f, 0.2f}, true, seed + 20};
    const Body trooper{{0.66f, 0.62f, 0.50f}, {0.42f, 0.44f, 0.26f}, {0.30f, 0.30f, 0.20f}, {0.12f, 0.1f, 0.08f},
                       {0.85f, 0.12f, 0.06f}, false, seed + 21};
    std::vector<Canvas> impFrames = paintMonster(imp), trooperFrames = paintMonster(trooper);
    for (int i = 0; i < 12; ++i) {
        setSprite(Spr::ImpWalk + i, impFrames[static_cast<size_t>(i)], i == 5);
        setSprite(Spr::TrooperWalk + i, trooperFrames[static_cast<size_t>(i)], i == 5);
    }

    // Effects.
    for (int k = 0; k < 2; ++k) {
        Canvas cv(16, 16);
        fireBall(cv, {8.0f, 8.0f}, 7.0f, 0.0f, 0.0f, seed + 30 + k);
        setSprite(Spr::Fireball + k, cv, true, false);
    }
    for (int k = 0; k < 3; ++k) {
        const int size = 24 + 6 * k;
        Canvas cv(size, size);
        fireBall(cv, {size * 0.5f, size * 0.5f}, size * 0.48f, 0.4f * k, 0.3f * k, seed + 32 + k);
        setSprite(Spr::FireBoom + k, cv, true, false);
        const int big = 40 + 8 * k;
        Canvas bb(big, big);
        fireBall(bb, {big * 0.5f, big * 0.55f}, big * 0.48f, 0.35f * k, 0.35f * k, seed + 35 + k);
        setSprite(Spr::BarrelBoom + k, bb, true, false);
    }
    for (int k = 0; k < 3; ++k) {
        Canvas puff(12, 12), blood(10, 10);
        const float r = 5.0f - static_cast<float>(k) * 1.2f;
        ellipse(puff, {6.0f, 6.0f - k}, {r, r}, vec3(0.6f, 0.58f, 0.55f) * (1.0f - 0.15f * k), seed + 40 + k, 0.4f);
        if (k == 0) ellipse(puff, {6.0f, 6.0f}, {2.0f, 2.0f}, vec3(1.0f, 0.9f, 0.4f), seed, 0.0f);
        setSprite(Spr::Puff + k, puff, k == 0, false);
        bloodSplat(blood, {5.0f, 4.0f + 2.0f * k}, 3.0f - 0.6f * k, seed + 44 + k);
        setSprite(Spr::Blood + k, blood, false, false);
    }

    // Props and pickups.
    setSprite(Spr::Barrel, paintBarrel(seed + 50, 0));
    setSprite(Spr::Barrel1, paintBarrel(seed + 50, 1));
    setSprite(Spr::ShotgunPickup, paintShotgunPickup(seed + 51));
    setSprite(Spr::Clip, paintBox(8, 12, vec3(0.3f, 0.3f, 0.28f), seed + 52, 3));
    setSprite(Spr::Shells, paintBox(18, 11, vec3(0.75f, 0.12f, 0.08f), seed + 53, 2));
    setSprite(Spr::Medikit, paintBox(22, 15, vec3(0.88f, 0.88f, 0.84f), seed + 54, 1));
    setSprite(Spr::Stimpack, paintBox(11, 12, vec3(0.88f, 0.88f, 0.84f), seed + 55, 1));
    setSprite(Spr::Armor, paintArmor(seed + 56));
    setSprite(Spr::Lamp, paintLamp(seed + 57), true);

    // First-person weapons.
    auto setGun = [&](Gun g, Canvas cv, bool fullbright = false) {
        darkenEdges(cv, 0.55f);
        m_guns[static_cast<size_t>(g)] = toImage(cv, *this, 1.0f, fullbright);
    };
    setGun(Gun::Pistol, paintPistol(0.0f, seed + 60));
    setGun(Gun::PistolFire, paintPistol(4.0f, seed + 60));
    setGun(Gun::PistolFlash, paintFlash(26, 22, 6, seed + 61), true);
    setGun(Gun::Shotgun, paintShotgun(0.0f, 0.0f, seed + 62));
    setGun(Gun::ShotgunFire, paintShotgun(0.0f, 3.0f, seed + 62));
    setGun(Gun::ShotgunPump1, paintShotgun(7.0f, 9.0f, seed + 62));
    setGun(Gun::ShotgunPump2, paintShotgun(14.0f, 15.0f, seed + 62));
    setGun(Gun::ShotgunFlash, paintFlash(40, 30, 8, seed + 63), true);

    // HUD.
    m_statusBar = toImage(paintStatusBar(seed + 70), *this, 0.6f);
    const char glyphs[] = "0123456789%-";
    for (size_t i = 0; i < 12; ++i) m_bigGlyphs[i] = toImage(paintBigGlyph(glyphs[i]), *this, 0.0f);
    for (int level = 0; level < kFaceHealthLevels; ++level) {
        for (int look = 0; look < 3; ++look) m_faces[static_cast<size_t>(faceLook(level, look))] = toImage(paintFace(level, look, 0, seed + 80), *this, 0.4f);
        m_faces[static_cast<size_t>(faceGrin(level))] = toImage(paintFace(level, 1, 1, seed + 80), *this, 0.4f);
        m_faces[static_cast<size_t>(faceOuch(level))] = toImage(paintFace(level, 1, 2, seed + 80), *this, 0.4f);
    }
    m_faces[kFaceDead] = toImage(paintFace(4, 1, 3, seed + 80), *this, 0.4f);
    m_title = toImage(paintTitle(seed + 90), *this, 0.8f);

    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "[Doom] Painted palette, " << static_cast<int>(WallTex::Count) << " walls, " << static_cast<int>(FlatTex::Count)
              << " flats, " << static_cast<int>(Spr::Count) << " sprites in " << static_cast<int>(ms) << " ms\n";
}

} // namespace doom
