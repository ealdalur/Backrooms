// ---------------------------------------------------------------------------
// MaterialLibrary.cpp
// Procedural material synthesis. Every generator writes a tileable square
// image using periodic noise so the textures repeat seamlessly.
// ---------------------------------------------------------------------------
#include "Render/MaterialLibrary.h"

#include "Math/Noise.h"
#include "Math/Random.h"
#include "Render/AtlasLayout.h"
#include "Render/BitmapFont.h"

#include <SDL3/SDL_video.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <vector>

#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#endif

namespace {

// ----- Small math helpers ------------------------------------------------------

inline float sat(float x) { return std::clamp(x, 0.0f, 1.0f); }
inline float smooth(float e0, float e1, float x) {
    const float t = sat((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}
inline float fract(float x) { return x - std::floor(x); }
inline float sq(float x) { return x * x; }
inline float gauss(float x, float center, float width) { return std::exp(-sq((x - center) / width)); }
inline float unit8(uint32_t h, int shift) { return static_cast<float>((h >> shift) & 255u) / 255.0f; }

/// Wraps a (possibly negative, fractional) texel coordinate into [0, n).
inline int wrapTexel(float coord, int n) {
    const int i = static_cast<int>(std::floor(coord)) % n;
    return i < 0 ? i + n : i;
}

/// CPU image pair for one material layer.
struct Canvas {
    int size = 0;
    std::vector<uint8_t> albedo;  // RGBA8, sRGB colour
    std::vector<uint8_t> surface; // RGBA8, (height, specular, emissive, 1)

    explicit Canvas(int n) : size(n), albedo(static_cast<size_t>(n) * n * 4), surface(static_cast<size_t>(n) * n * 4) {}

    void put(int x, int y, const glm::vec3& color, float height, float spec, float emissive, float opacity = 1.0f) {
        const size_t i = (static_cast<size_t>(y) * size + x) * 4;
        albedo[i + 0] = static_cast<uint8_t>(sat(color.r) * 255.0f + 0.5f);
        albedo[i + 1] = static_cast<uint8_t>(sat(color.g) * 255.0f + 0.5f);
        albedo[i + 2] = static_cast<uint8_t>(sat(color.b) * 255.0f + 0.5f);
        albedo[i + 3] = 255;
        surface[i + 0] = static_cast<uint8_t>(sat(height) * 255.0f + 0.5f);
        surface[i + 1] = static_cast<uint8_t>(sat(spec) * 255.0f + 0.5f);
        surface[i + 2] = static_cast<uint8_t>(sat(emissive) * 255.0f + 0.5f);
        surface[i + 3] = static_cast<uint8_t>(sat(opacity) * 255.0f + 0.5f);
    }
};

/// Iterates all texels, passing texel coords and normalised (u, v) in [0,1).
template <typename Fn>
void forEachTexel(Canvas& c, Fn&& fn) {
    const float inv = 1.0f / static_cast<float>(c.size);
    for (int y = 0; y < c.size; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) * inv;
        for (int x = 0; x < c.size; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) * inv;
            fn(x, y, u, v);
        }
    }
}

// ----- Wallpaper ------------------------------------------------------------------
// Monochromatic yellow paper with faint printed stripes and diamond motifs,
// vertical fibre grain, water blotches with darker tide lines and drip streaks.
void generateWallpaper(Canvas& c) {
    const glm::vec3 base(0.80f, 0.72f, 0.41f);
    const glm::vec3 stainTint(0.62f, 0.50f, 0.27f);
    forEachTexel(c, [&](int x, int y, float u, float v) {
        // Printed pattern: 10 vertical bands per repeat, thin lines between them
        // and staggered hollow diamonds centred in each band.
        const float band   = u * 10.0f;
        const float bf     = fract(band);
        const float line   = 1.0f - smooth(0.0f, 0.035f, std::min(bf, 1.0f - bf));
        const float stagger = (static_cast<int>(std::floor(band)) & 1) ? 0.5f : 0.0f;
        const float mu     = bf - 0.5f;
        const float mv     = fract(v * 10.0f + stagger) - 0.5f;
        const float dd     = std::fabs(mu) + std::fabs(mv);
        const float outline = (1.0f - smooth(0.17f, 0.20f, dd)) - (1.0f - smooth(0.11f, 0.14f, dd));

        // Vertical grain (stretched noise: high frequency across, low along).
        const float grain = noise::fbm(u * 64.0f, v * 4.0f, 64, 4, 3, 0x51A7u);
        const float fibre = noise::fbm(u * 256.0f, v * 256.0f, 256, 256, 2, 0x2F1Bu);

        // Small, faint blotches (large water stains are added in world space by
        // the shader so they never repeat with the texture).
        const float blotch = noise::fbm(u * 6.0f, v * 6.0f, 6, 6, 4, 0x77C3u);
        const float stain  = smooth(0.30f, 0.60f, blotch);
        const float tide   = gauss(blotch, 0.32f, 0.03f);

        // Drip streaks running down the wall.
        const float column = noise::fbm(u * 24.0f, v * 1.0f, 24, 1, 3, 0x9D11u);
        const float streak = smooth(0.35f, 0.65f, column) *
                             (0.5f + 0.5f * noise::perlin(u * 24.0f, v * 3.0f, 24, 3, 0x1234u));

        const float bright = 1.0f + 0.035f * grain + 0.02f * fibre - 0.05f * line + 0.035f * outline;
        glm::vec3 col = base * bright;
        col = glm::mix(col, stainTint * bright, stain * 0.18f);
        col *= 1.0f - 0.04f * tide - 0.05f * streak;

        const float height = 0.5f + 0.12f * grain + 0.15f * fibre + 0.18f * outline - 0.2f * line;
        const float spec   = 0.35f + 0.25f * stain;
        c.put(x, y, col, height, spec, 0.0f);
    });
}

// ----- Carpet -------------------------------------------------------------------
// Loop-pile office carpet: cellular tufts, per-fibre noise, wear, dirt and
// large damp patches that are darker, flattened and more specular (wet).
void generateCarpet(Canvas& c) {
    const glm::vec3 base(0.57f, 0.48f, 0.28f);
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const noise::Cellular cell = noise::worley(u * 200.0f, v * 200.0f, 200, 200, 0xCA7Eu);
        const float tuft    = 1.0f - smooth(0.0f, 0.75f, cell.f1);
        const float tuftVar = unit8(cell.cellId, 0) - 0.5f;
        const float fibre   = noise::white(x, y, 0x0F1Bu) - 0.5f;
        const float wear    = noise::fbm(u * 20.0f, v * 20.0f, 20, 20, 4, 0x3EA5u);
        const float dirt    = noise::fbm(u * 6.0f, v * 6.0f, 6, 6, 4, 0xD127u);
        const float damp    = noise::fbm(u * 4.0f, v * 4.0f, 4, 4, 4, 0xDA3Bu);
        const float wet     = smooth(0.25f, 0.5f, damp);
        const float tide    = gauss(damp, 0.26f, 0.03f);

        const float bright = 0.82f + 0.22f * tuft + 0.10f * tuftVar + 0.12f * fibre + 0.07f * wear;
        glm::vec3 col = base * bright;
        col = glm::mix(col, col * glm::vec3(0.62f, 0.58f, 0.52f), smooth(0.2f, 0.7f, dirt) * 0.4f);
        col = glm::mix(col, col * glm::vec3(0.62f, 0.56f, 0.46f), wet * 0.35f);
        col *= 1.0f - 0.06f * tide;

        const float height = (0.25f + 0.55f * tuft + 0.2f * (fibre + 0.5f)) * (1.0f - 0.3f * wet);
        const float spec   = 0.06f + 0.35f * wet;
        c.put(x, y, col, height, spec, 0.0f);
    });
}

// ----- Ceiling tiles ------------------------------------------------------------
// 4x4 acoustic tiles per repeat inside a protruding T-bar grid. Tiles carry
// pits, fissures, per-tile yellowing and occasional water stains with rings.
void generateCeiling(Canvas& c) {
    const float tileMetres = 0.625f;
    const float gridHalf   = 0.012f;
    const float bevelEnd   = 0.024f;
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const float tu = u * 4.0f, tv = v * 4.0f;
        const int   tx = static_cast<int>(std::floor(tu));
        const int   ty = static_cast<int>(std::floor(tv));
        const float fu = tu - static_cast<float>(tx);
        const float fv = tv - static_cast<float>(ty);
        const float edge = std::min(std::min(fu, 1.0f - fu), std::min(fv, 1.0f - fv)) * tileMetres;

        const float speck = noise::fbm(u * 300.0f, v * 300.0f, 300, 300, 2, 0x5EC4u);

        if (edge < gridHalf) {
            // Painted steel T-bar.
            c.put(x, y, glm::vec3(0.87f, 0.86f, 0.83f) * (0.97f + 0.03f * speck), 1.0f, 0.6f, 0.0f);
            return;
        }

        const uint32_t tileHash = rnd::hash2i(tx, ty, 0x7113u);
        const float tileBright = 0.95f + 0.08f * unit8(tileHash, 0);
        const float yellowing  = unit8(tileHash, 8) * 0.3f;
        glm::vec3 col = glm::mix(glm::vec3(0.84f, 0.82f, 0.76f), glm::vec3(0.80f, 0.73f, 0.55f), yellowing) * tileBright;

        const noise::Cellular w = noise::worley(u * 150.0f, v * 150.0f, 150, 150, 0x9175u);
        const float pit     = (1.0f - smooth(0.06f, 0.2f, w.f1)) * ((w.cellId & 3u) ? 1.0f : 0.3f);
        const float fissure = smooth(0.80f, 0.93f, noise::ridged(u * 40.0f, v * 40.0f, 40, 40, 3, 0xF155u));
        const float dirt    = noise::fbm(u * 3.0f, v * 3.0f, 3, 3, 4, 0xD1A7u);

        col *= (1.0f - 0.35f * pit - 0.18f * fissure + 0.03f * speck) * (0.95f + 0.05f * dirt);

        // Faint discolouration on a few tiles; distinct water-damaged tiles are
        // picked in world space by the shader so they do not repeat.
        if (unit8(tileHash, 16) < 0.25f) {
            const uint32_t sh = rnd::hash32(tileHash);
            const glm::vec2 centre(0.3f + 0.4f * unit8(sh, 0), 0.3f + 0.4f * unit8(sh, 8));
            const float radius = 0.18f + 0.22f * unit8(sh, 16);
            const float dist = glm::length(glm::vec2(fu, fv) - centre) / radius +
                               0.22f * noise::fbm(u * 10.0f, v * 10.0f, 10, 10, 3, 0x57A1u);
            const float inside = 1.0f - smooth(0.85f, 1.0f, dist);
            const float rings  = gauss(dist, 0.95f, 0.045f);
            col = glm::mix(col, col * glm::vec3(0.92f, 0.85f, 0.70f), inside * 0.35f);
            col *= 1.0f - 0.05f * rings;
        }

        float height = 0.55f - 0.3f * pit - 0.15f * fissure + 0.03f * speck;
        if (edge < bevelEnd) {
            // Bevelled tile edge sitting in the grid (slightly shadowed groove).
            const float t = (edge - gridHalf) / (bevelEnd - gridHalf);
            height = glm::mix(0.85f, height, t);
            col *= glm::mix(0.8f, 1.0f, t);
        }
        c.put(x, y, col, height, 0.15f, 0.0f);
    });
}

// ----- Wood laminate ------------------------------------------------------------
// Printed flat-sawn grain: distorted growth rings (along v), fibre streaks
// and pore dashes running along u, with glossy clear-coat specular.
void generateWood(Canvas& c) {
    const glm::vec3 light(0.60f, 0.45f, 0.31f);
    const glm::vec3 dark(0.47f, 0.33f, 0.21f);
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const float warp  = noise::fbm(u * 2.0f, v * 3.0f, 2, 3, 4, 0xA00Du);
        const float t     = v * 30.0f + warp * 2.5f + 0.6f * noise::perlin(u * 3.0f, v * 2.0f, 3, 2, 0xB00Du);
        const float ring  = fract(t);
        const float late  = smooth(0.6f, 0.92f, ring) * (1.0f - smooth(0.94f, 1.0f, ring));
        const float fibre = noise::fbm(u * 4.0f, v * 220.0f, 4, 220, 3, 0xC00Du);
        const float pores = smooth(0.55f, 0.8f, noise::perlin(u * 24.0f, v * 400.0f, 24, 400, 0xD00Du));
        const float board = noise::fbm(u * 1.0f, v * 1.0f, 1, 1, 3, 0xE00Du);

        glm::vec3 col = glm::mix(light, dark, sat(late * 0.55f + 0.25f * (fibre * 0.5f + 0.5f)));
        col *= (1.0f - 0.12f * pores) * (0.95f + 0.08f * board);

        const float height = 0.5f + 0.2f * fibre - 0.3f * pores;
        const float spec   = 0.75f - 0.35f * pores;
        c.put(x, y, col, height, spec, 0.0f);
    });
}

// ----- Gray metal -----------------------------------------------------------------
// Powder-coated steel: mottling, orange-peel relief, faint brushing, grime and
// a set of bright scratches rasterised with wrap-around (tileable).
void generateMetal(Canvas& c) {
    const int n = c.size;
    std::vector<float> scratch(static_cast<size_t>(n) * n, 0.0f);
    rnd::Rng rng(0x5C7A7C4ull);
    for (int s = 0; s < 90; ++s) {
        const float px = rng.nextFloat() * n, py = rng.nextFloat() * n;
        const float ang = rng.range(0.0f, 6.2831853f);
        const float len = rng.range(0.02f, 0.18f) * n;
        const float strength = rng.range(0.5f, 1.0f);
        const int steps = static_cast<int>(len * 2.0f);
        for (int i = 0; i < steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(std::max(steps - 1, 1));
            const float fade = std::sin(t * 3.14159265f); // tapered ends
            const int sx = wrapTexel(px + std::cos(ang) * len * t, n);
            const int sy = wrapTexel(py + std::sin(ang) * len * t, n);
            float& texel = scratch[static_cast<size_t>(sy) * n + sx];
            texel = std::max(texel, strength * fade);
        }
    }

    const glm::vec3 base(0.47f, 0.48f, 0.49f);
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const float mottle = noise::fbm(u * 5.0f, v * 5.0f, 5, 5, 4, 0x3E7Au);
        const float peel   = noise::fbm(u * 110.0f, v * 110.0f, 110, 110, 2, 0x0EE1u);
        const float brush  = noise::fbm(u * 2.0f, v * 180.0f, 2, 180, 2, 0xB125u);
        const float grime  = smooth(0.2f, 0.8f, noise::fbm(u * 3.0f, v * 3.0f, 3, 3, 3, 0x6213u));
        const float scr    = scratch[static_cast<size_t>(y) * n + x];

        glm::vec3 col = base * (1.0f + 0.05f * mottle + 0.015f * peel + 0.02f * brush) * (1.0f - 0.12f * grime);
        col = glm::mix(col, glm::vec3(0.62f, 0.63f, 0.64f), scr * 0.5f);

        const float height = 0.5f + 0.3f * peel - 0.3f * scr;
        const float spec   = 0.6f + 0.3f * scr - 0.3f * grime;
        c.put(x, y, col, height, spec, 0.0f);
    });
}

// ----- Fluorescent troffer panel ------------------------------------------------
// Mapped once per fixture (u across the short side, v along the long side):
// white steel frame, prismatic diffuser, brighter bands above the two tubes.
void generateLightPanel(Canvas& c) {
    const float borderU = 0.045f;
    const float borderV = 0.022f;
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const float du = std::min(u, 1.0f - u);
        const float dv = std::min(v, 1.0f - v);
        if (du < borderU || dv < borderV) {
            const float speck = noise::fbm(u * 64.0f, v * 64.0f, 64, 64, 2, 0xF7A3u);
            c.put(x, y, glm::vec3(0.88f, 0.88f, 0.86f) * (0.97f + 0.03f * speck), 1.0f, 0.4f, 0.0f);
            return;
        }
        // Prismatic diffuser: tiny pyramids.
        const float px = fract(u * 60.0f) - 0.5f;
        const float py = fract(v * 120.0f) - 0.5f;
        const float pyramid = 1.0f - std::max(std::fabs(px), std::fabs(py)) * 2.0f;

        const float tubes = 0.62f + 0.38f * (gauss(u, 0.32f, 0.11f) + gauss(u, 0.68f, 0.11f));
        const float edgeFade = smooth(0.0f, 0.08f, du - borderU) * 0.25f + 0.75f;
        const float endFade  = 0.75f + 0.25f * smooth(0.0f, 0.12f, dv);
        const float dust     = smooth(0.3f, 0.9f, noise::fbm(u * 6.0f, v * 6.0f, 6, 6, 4, 0xD057u)) *
                               (1.0f - smooth(0.05f, 0.2f, std::min(du, dv)));
        const float emissive = sat(tubes * edgeFade * endFade * (1.0f - 0.35f * dust));

        const glm::vec3 col = glm::vec3(0.96f, 0.96f, 0.94f) * (1.0f - 0.25f * dust);
        c.put(x, y, col, 0.3f + 0.2f * pyramid, 0.5f, emissive);
    });
}

// ----- Dark plastic / rubber ------------------------------------------------------
void generatePlastic(Canvas& c) {
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const float grain = noise::fbm(u * 200.0f, v * 200.0f, 200, 200, 2, 0x9A57u);
        const float scuff = smooth(0.4f, 0.9f, noise::fbm(u * 8.0f, v * 8.0f, 8, 8, 4, 0x5C0Fu));
        const glm::vec3 col = glm::vec3(0.10f, 0.095f, 0.09f) * (1.0f + 0.08f * grain) + glm::vec3(0.05f) * scuff;
        c.put(x, y, col, 0.5f + 0.3f * grain, 0.6f - 0.4f * scuff, 0.0f);
    });
}

// ----- Woven fabric -----------------------------------------------------------------
// Plain weave: alternating warp/weft threads with heathered colour, fuzz and
// faint stains (cubicle panels and chair upholstery).
void generateFabric(Canvas& c) {
    const float threads = 160.0f;
    const glm::vec3 warpCol(0.34f, 0.37f, 0.43f);
    const glm::vec3 weftCol(0.30f, 0.33f, 0.38f);
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const float tu = u * threads, tv = v * threads;
        const int cu = static_cast<int>(std::floor(tu));
        const int cv = static_cast<int>(std::floor(tv));
        const bool warpOver = ((cu + cv) & 1) == 0;
        const float warpProfile = std::sin(3.14159265f * fract(tu));
        const float weftProfile = std::sin(3.14159265f * fract(tv));

        const float heatherU = noise::white(cu, 0, 0x7EA1u) - 0.5f;
        const float heatherV = noise::white(0, cv, 0x7EA2u) - 0.5f;
        const float fuzz  = noise::fbm(u * 300.0f, v * 300.0f, 300, 300, 2, 0xF022u);
        const float stain = smooth(0.35f, 0.8f, noise::fbm(u * 2.0f, v * 2.0f, 2, 2, 4, 0x57A2u));

        glm::vec3 col = warpOver ? warpCol * (1.0f + 0.10f * heatherU) : weftCol * (1.0f + 0.10f * heatherV);
        col *= (0.82f + 0.18f * (warpOver ? warpProfile : weftProfile)) * (1.0f + 0.04f * fuzz);
        col *= 1.0f - 0.12f * stain;

        const float height = warpOver ? 0.3f + 0.7f * warpProfile : 0.3f + 0.7f * weftProfile;
        c.put(x, y, col, height * 0.9f + 0.1f * (fuzz * 0.5f + 0.5f), 0.1f, 0.0f);
    });
}

// ----- Concrete -----------------------------------------------------------------
// Bare cast concrete: large mottling, exposed aggregate, air pores, form
// seams and grimy run-off stains (stairwell flights and slab edges).
void generateConcrete(Canvas& c) {
    const glm::vec3 base(0.50f, 0.49f, 0.46f);
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const float mottle = noise::fbm(u * 4.0f, v * 4.0f, 4, 4, 5, 0xC0C1u);
        const float fine   = noise::fbm(u * 140.0f, v * 140.0f, 140, 140, 2, 0xC0C2u);
        const noise::Cellular agg = noise::worley(u * 70.0f, v * 70.0f, 70, 70, 0xC0C3u);
        const float stone  = (1.0f - smooth(0.10f, 0.35f, agg.f1)) * ((agg.cellId & 7u) < 3u ? 1.0f : 0.0f);
        const noise::Cellular air = noise::worley(u * 110.0f, v * 110.0f, 110, 110, 0xC0C4u);
        const float pore   = (1.0f - smooth(0.02f, 0.09f, air.f1)) * ((air.cellId & 15u) == 0u ? 1.0f : 0.0f);
        const float stain  = smooth(0.25f, 0.75f, noise::fbm(u * 2.0f, v * 6.0f, 2, 6, 4, 0xC0C5u));
        const float seam   = gauss(fract(v * 2.0f), 0.5f, 0.004f); // formwork joint

        glm::vec3 col = base * (0.90f + 0.12f * mottle + 0.05f * fine);
        col = glm::mix(col, glm::vec3(0.62f, 0.60f, 0.56f), stone * 0.35f);
        col = glm::mix(col, col * glm::vec3(0.80f, 0.77f, 0.70f), stain * 0.45f);
        col *= (1.0f - 0.45f * pore) * (1.0f - 0.08f * seam);

        const float height = 0.5f + 0.12f * fine + 0.10f * stone - 0.45f * pore - 0.2f * seam;
        c.put(x, y, col, height, 0.18f + 0.15f * stain, 0.0f);
    });
}

// ----- Beige plastic ------------------------------------------------------------
// Textured ABS computer housing, unevenly yellowed by decades of fluorescent
// light, with ingrained grime.
void generateBeigePlastic(Canvas& c) {
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const noise::Cellular stipple = noise::worley(u * 180.0f, v * 180.0f, 180, 180, 0xBE16u);
        const float bump    = 1.0f - smooth(0.0f, 0.8f, stipple.f1);
        const float yellow  = smooth(0.2f, 0.8f, noise::fbm(u * 3.0f, v * 3.0f, 3, 3, 4, 0xBE17u));
        const float grime   = smooth(0.45f, 0.9f, noise::fbm(u * 9.0f, v * 9.0f, 9, 9, 4, 0xBE18u));
        glm::vec3 col = glm::mix(glm::vec3(0.80f, 0.76f, 0.65f), glm::vec3(0.78f, 0.68f, 0.47f), yellow * 0.7f);
        col *= (0.97f + 0.04f * bump) * (1.0f - 0.2f * grime);
        c.put(x, y, col, 0.4f + 0.3f * bump, 0.45f - 0.3f * grime, 0.0f);
    });
}

// ----- CRT screen -------------------------------------------------------------------
// Mapped once per screen (0..1). Dark smoked glass; the emissive mask is the
// visible raster area (rounded corners, soft edge) the shader draws text into.
void generateCrtScreen(Canvas& c) {
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const glm::vec2 q(std::fabs(u - 0.5f) * 2.0f, std::fabs(v - 0.5f) * 2.0f);
        // Rounded-rectangle raster (superellipse) inset from the bezel.
        const float r = std::pow(std::pow(q.x / 0.92f, 6.0f) + std::pow(q.y / 0.90f, 6.0f), 1.0f / 6.0f);
        const float raster = 1.0f - smooth(0.93f, 1.0f, r);
        const float smudge = smooth(0.4f, 0.9f, noise::fbm(u * 5.0f, v * 5.0f, 5, 5, 4, 0xC47Au));
        const glm::vec3 col = glm::vec3(0.030f, 0.036f, 0.033f) * (1.0f + 0.4f * smudge);
        c.put(x, y, col, 0.5f, 1.0f - 0.5f * smudge, raster);
    });
}

// ----- Flesh ------------------------------------------------------------------------
// Pale, bloodless skin: blotchy discolouration, a web of blue-grey veins,
// wrinkles and a clammy sheen.
void generateFlesh(Canvas& c) {
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const float blotch  = smooth(0.1f, 0.8f, noise::fbm(u * 5.0f, v * 5.0f, 5, 5, 5, 0xF1E5u));
        const float veins   = smooth(0.82f, 0.95f, noise::ridged(u * 7.0f, v * 7.0f, 7, 7, 4, 0xF1E6u));
        const float wrinkle = noise::fbm(u * 60.0f, v * 18.0f, 60, 18, 3, 0xF1E7u);
        const float pores   = noise::white(x, y, 0xF1E8u);
        glm::vec3 col = glm::vec3(0.74f, 0.71f, 0.67f);
        col = glm::mix(col, glm::vec3(0.66f, 0.54f, 0.56f), blotch * 0.35f);
        col = glm::mix(col, glm::vec3(0.40f, 0.47f, 0.58f), veins * 0.55f);
        col *= 0.94f + 0.06f * wrinkle - 0.04f * pores;
        c.put(x, y, col, 0.5f + 0.25f * wrinkle - 0.2f * veins, 0.55f + 0.3f * blotch, 0.0f);
    });
}

// ----- Stair sign ---------------------------------------------------------------------
// Mapped once per sign face. The top half reads "^ STAIRS" (the way up), the
// bottom half "v STAIRS" (the way down): white lettering from the built-in
// pixel font on a glowing green panel, inside a dark frame.
void generateStairSign(Canvas& c) {
    static const char* const kText[2] = {"v STAIRS", "^ STAIRS"}; // bottom band, top band
    const float signAspect = 0.6f / 0.2f;      // sign face: 0.6 m wide, 0.2 m tall
    const int   textCols = 8 * (font::kGlyphW + 1) - 1;
    const float pxU = 0.84f / static_cast<float>(textCols);
    const float pxV = pxU * signAspect;         // square font pixels, in band-height units
    const float u0 = 0.5f - 0.5f * pxU * static_cast<float>(textCols);
    const float vTop = 0.5f + 0.5f * pxV * static_cast<float>(font::kGlyphH);
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const int band = v < 0.5f ? 0 : 1;
        const float vv = v * 2.0f - static_cast<float>(band); // 0 at the band's bottom edge
        const float edge = std::min(std::min(u, 1.0f - u) * signAspect, std::min(vv, 1.0f - vv));
        if (edge < 0.05f) { // frame
            c.put(x, y, glm::vec3(0.06f, 0.07f, 0.06f), 0.5f, 0.3f, 0.0f);
            return;
        }
        bool ink = false;
        const int col = static_cast<int>(std::floor((u - u0) / pxU));
        const int row = static_cast<int>(std::floor((vTop - vv) / pxV));
        if (col >= 0 && col < textCols && row >= 0 && row < font::kGlyphH && col % (font::kGlyphW + 1) < font::kGlyphW) {
            const uint8_t* rows = font::glyph(kText[band][col / (font::kGlyphW + 1)]);
            ink = rows && (rows[row] & (0x10 >> (col % (font::kGlyphW + 1))));
        }
        const float grain = 0.97f + 0.03f * noise::white(x, y, 0x5160u);
        if (ink) c.put(x, y, glm::vec3(0.95f, 1.0f, 0.95f) * grain, 0.5f, 0.3f, 1.0f);
        else     c.put(x, y, glm::vec3(0.12f, 0.62f, 0.30f) * grain, 0.5f, 0.3f, 0.45f);
    });
}

// ----- Phone keys ---------------------------------------------------------------------
// Atlas of a desk phone's printed parts in 4 x 4 cells (v up): the twelve
// keycaps in columns 0-2 (top keypad row in the top cell row) - cream plastic
// with a black digit and its letters beneath, grubby where fingers press -
// then in column 3 the red message lamp lens (top), the paper number card and
// the lamp's "MSG" label.
// Each part is mapped onto a rectangle of its own aspect, so font pixels are
// stretched here to come out square on the model.
void generatePhoneKeys(Canvas& c) {
    static const char kDigits[] = "123456789*0#";
    static const char* const kLetters[12] = {"", "ABC", "DEF", "GHI", "JKL", "MNO", "PRS", "TUV", "WXY", "", "OPER", ""};
    static const char* const kCard[2] = {"DIAL 9 FOR", "OUTSIDE LINE"};
    const int advance = font::kGlyphW + 1;
    // Is the font pixel under (cu, cv) lit, for `s` centred on cx with its top edge at `top`?
    auto ink = [&](const char* s, float cu, float cv, float cx, float top, float pu, float pv) {
        const int n = static_cast<int>(std::strlen(s));
        const float x0 = cx - 0.5f * pu * static_cast<float>(n * advance - 1);
        const int col = static_cast<int>(std::floor((cu - x0) / pu));
        const int row = static_cast<int>(std::floor((top - cv) / pv));
        if (col < 0 || row < 0 || row >= font::kGlyphH || col >= n * advance - 1 || col % advance >= font::kGlyphW) return false;
        const uint8_t* rows = font::glyph(s[col / advance]);
        return rows && (rows[row] & (0x10 >> (col % advance))) != 0;
    };
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const int ci = std::min(3, static_cast<int>(u * 4.0f)), cj = std::min(3, static_cast<int>(v * 4.0f));
        const float cu = u * 4.0f - static_cast<float>(ci), cv = v * 4.0f - static_cast<float>(cj);
        const float grain = 0.97f + 0.03f * noise::white(x, y, 0xB0B0u);
        if (ci < 3) {
            // Keycap (21 x 17 mm): rounded, dished, handled for decades.
            const int key = ci + (3 - cj) * 3;
            const float aspect = 0.021f / 0.017f;
            const float edge = smooth(0.0f, 0.14f, std::min(std::min(cu, 1.0f - cu) * aspect, std::min(cv, 1.0f - cv)));
            const float dish = sq(cu - 0.5f) + sq(cv - 0.5f);
            const float grime = gauss(cu, 0.5f, 0.3f) * gauss(cv, 0.5f, 0.3f) *
                                smooth(0.3f, 0.8f, noise::fbm(u * 24.0f, v * 24.0f, 24, 24, 3, 0xB0B1u + static_cast<uint32_t>(key)));
            glm::vec3 col = glm::vec3(0.86f, 0.84f, 0.78f) * (0.70f + 0.30f * edge) * grain;
            col = glm::mix(col, col * glm::vec3(0.72f, 0.66f, 0.55f), grime * 0.6f);
            const char digit[2] = {kDigits[key], '\0'};
            const float pv = 0.50f / static_cast<float>(font::kGlyphH), pv2 = 0.19f / static_cast<float>(font::kGlyphH);
            if (ink(digit, cu, cv, 0.5f, 0.88f, pv / aspect, pv) || ink(kLetters[key], cu, cv, 0.5f, 0.30f, pv2 / aspect, pv2)) {
                col = glm::vec3(0.07f, 0.07f, 0.075f) * (1.0f + 0.3f * grime);
            }
            c.put(x, y, col, 0.35f + 0.4f * edge - 0.6f * dish, 0.45f - 0.25f * grime, 0.0f);
        } else if (cj == 3) {
            // Message lamp lens: ribbed, deep red plastic (it only looks bright when lit), in a dark rim.
            const float aspect = 0.020f / 0.014f;
            const float edge = std::min(std::min(cu, 1.0f - cu) * aspect, std::min(cv, 1.0f - cv));
            const float lens = smooth(0.06f, 0.12f, edge);
            const float ribs = 0.85f + 0.15f * std::sin(cu * 90.0f);
            const float hot = gauss(cu, 0.5f, 0.35f) * gauss(cv, 0.5f, 0.45f);
            const glm::vec3 col = glm::mix(glm::vec3(0.05f), glm::vec3(0.32f, 0.035f, 0.025f) * ribs * (0.6f + 0.4f * hot), lens); // deep red when unlit: the glow brightens it
            c.put(x, y, col * grain, 0.5f + 0.2f * lens, 0.9f, lens * (0.55f + 0.45f * hot));
        } else if (cj == 2) {
            // Number card (84 x 26 mm) under its window: yellowed paper, typed in blue-black.
            const float aspect = 0.084f / 0.026f;
            const float stain = smooth(0.45f, 0.85f, noise::fbm(u * 10.0f, v * 10.0f, 10, 10, 4, 0xCA2Du));
            glm::vec3 col = glm::mix(glm::vec3(0.90f, 0.87f, 0.76f), glm::vec3(0.80f, 0.70f, 0.50f), stain * 0.6f) * grain;
            const float pu = 0.88f / static_cast<float>(12 * advance - 1), pv = pu * aspect;
            if (ink(kCard[0], cu, cv, 0.5f, 0.84f, pu, pv) || ink(kCard[1], cu, cv, 0.5f, 0.42f, pu, pv)) {
                col = glm::vec3(0.10f, 0.11f, 0.20f);
            }
            c.put(x, y, col, 0.5f, 0.45f, 0.0f); // a little glossy: it sits under clear plastic
        } else if (cj == 1) {
            // "MSG" label (28 x 12 mm) beside the lamp: a printed sticker.
            const float aspect = 0.028f / 0.012f;
            glm::vec3 col = glm::vec3(0.88f, 0.86f, 0.78f) * grain;
            const float pu = 0.78f / static_cast<float>(3 * advance - 1), pv = pu * aspect;
            if (ink("MSG", cu, cv, 0.5f, 0.5f + 0.5f * pv * static_cast<float>(font::kGlyphH), pu, pv)) {
                col = glm::vec3(0.55f, 0.06f, 0.05f); // printed in red, like the lamp
            }
            c.put(x, y, col, 0.5f, 0.4f, 0.0f);
        } else {
            c.put(x, y, glm::vec3(0.1f) * grain, 0.5f, 0.3f, 0.0f); // unused
        }
    });
}

// ----- Copper -------------------------------------------------------------------------
// Enamelled magnet wire wound on a coil former: 64 round turns per repeat
// along v (1 mm wire at the material's 64 mm tile), each a bright crown with
// dark grooves between turns, a varnish tint that drifts from salmon to deep
// brown and the odd tarnished patch. The same texture on thick tubing reads
// as polished copper pipe with faint ribs.
void generateCopper(Canvas& c) {
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const float turn = fract(v * 64.0f);
        const float profile = std::sqrt(std::max(0.0f, 1.0f - sq(turn * 2.0f - 1.0f))); // round wire cross-section
        const float groove = 1.0f - smooth(0.0f, 0.25f, profile);
        const float tint = noise::fbm(u * 3.0f, v * 3.0f, 3, 3, 4, 0xC0A1u);
        const float tarnish = smooth(0.62f, 0.8f, noise::fbm(u * 8.0f, v * 8.0f, 8, 8, 4, 0xC0A2u));
        const float streak = noise::fbm(u * 96.0f, v * 2.0f, 96, 2, 2, 0xC0A3u);
        glm::vec3 col = glm::mix(glm::vec3(0.80f, 0.42f, 0.24f), glm::vec3(0.55f, 0.24f, 0.11f), tint);
        col = glm::mix(col, glm::vec3(0.32f, 0.22f, 0.14f), tarnish * 0.6f);
        col *= (0.78f + 0.22f * profile) * (0.95f + 0.05f * streak);
        col *= 1.0f - 0.35f * groove;
        c.put(x, y, col, 0.15f + 0.75f * profile, (0.35f + 0.65f * profile) * (1.0f - 0.6f * tarnish), 0.0f);
    });
}

// ----- Aluminium ----------------------------------------------------------------------
// Spun and polished sheet: fine circumferential brushing (u around), soft
// milky oxidation patches and a few fingerprints' worth of haze.
void generateAluminum(Canvas& c) {
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const float brush = noise::fbm(u * 2.0f, v * 180.0f, 2, 180, 3, 0xA1A1u);
        const float haze = smooth(0.5f, 0.85f, noise::fbm(u * 5.0f, v * 5.0f, 5, 5, 4, 0xA1A2u));
        const float speck = noise::white(x, y, 0xA1A3u);
        glm::vec3 col = glm::vec3(0.78f, 0.80f, 0.83f) * (0.90f + 0.10f * brush - 0.03f * speck);
        col = glm::mix(col, glm::vec3(0.70f, 0.71f, 0.70f), haze * 0.4f);
        c.put(x, y, col, 0.5f + 0.2f * brush, 0.85f - 0.45f * haze, 0.0f);
    });
}

// ----- Manila ---------------------------------------------------------------------------
// Buff file-folder card: short paper fibres, faint mottling and old stains.
void generateManila(Canvas& c) {
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const float mottle = noise::fbm(u * 6.0f, v * 6.0f, 6, 6, 4, 0x3A11u);
        const float fibre = noise::fbm(u * 200.0f, v * 60.0f, 200, 60, 2, 0x3A12u);
        const float stain = smooth(0.66f, 0.8f, noise::fbm(u * 4.0f, v * 4.0f, 4, 4, 4, 0x3A13u));
        glm::vec3 col = glm::vec3(0.85f, 0.74f, 0.50f) * (0.93f + 0.07f * mottle + 0.04f * fibre);
        col = glm::mix(col, glm::vec3(0.66f, 0.52f, 0.30f), stain * 0.5f);
        c.put(x, y, col, 0.5f + 0.2f * fibre, 0.2f, 0.0f);
    });
}

// ----- Tesla labels --------------------------------------------------------------------
// Atlas of the gun parts' printed and electronic details in 4 x 4 cells (v up):
//   (0,3) battery label: "LI-ION" over "36V 5AH" in black on a yellow band;
//   (1,3) "DANGER / HIGH VOLTAGE" sticker: black on yellow in a black border;
//   (2,3) driver panel: "DRSSTC" / "DRIVER" in white on dark grey;
//   (0,2) circuit board: green solder mask, copper traces and pads;
//   (3,3) green gauge LED lens, (3,2) red LED lens - both emissive where
//         the mesh asks for it (see the world shader's MAT_TESLA).
void generateTeslaLabels(Canvas& c) {
    const int advance = font::kGlyphW + 1;
    auto ink = [&](const char* s, float cu, float cv, float cx, float top, float pu, float pv) {
        const int n = static_cast<int>(std::strlen(s));
        const float x0 = cx - 0.5f * pu * static_cast<float>(n * advance - 1);
        const int col = static_cast<int>(std::floor((cu - x0) / pu));
        const int row = static_cast<int>(std::floor((top - cv) / pv));
        if (col < 0 || row < 0 || row >= font::kGlyphH || col >= n * advance - 1 || col % advance >= font::kGlyphW) return false;
        const uint8_t* rows = font::glyph(s[col / advance]);
        return rows && (rows[row] & (0x10 >> (col % advance))) != 0;
    };
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const int ci = std::min(3, static_cast<int>(u * 4.0f)), cj = std::min(3, static_cast<int>(v * 4.0f));
        const float cu = u * 4.0f - static_cast<float>(ci), cv = v * 4.0f - static_cast<float>(cj);
        const float grain = 0.96f + 0.04f * noise::white(x, y, 0x7E51u);
        const float wear = smooth(0.55f, 0.85f, noise::fbm(u * 12.0f, v * 12.0f, 12, 12, 4, 0x7E52u));
        if (ci == 0 && cj == 3) {
            // Battery label (120 x 45 mm).
            const float aspect = 0.12f / 0.045f;
            const float edge = std::min(std::min(cu, 1.0f - cu) * aspect, std::min(cv, 1.0f - cv));
            glm::vec3 col = glm::vec3(0.93f, 0.72f, 0.08f);
            if (cv < 0.22f) col = glm::vec3(0.06f);                   // black band along the bottom
            if (edge < 0.04f) col = glm::vec3(0.05f);                  // printed border
            const float pv = 0.36f / static_cast<float>(font::kGlyphH), pv2 = 0.18f / static_cast<float>(font::kGlyphH);
            if (ink("LI-ION", cu, cv, 0.5f, 0.90f, pv / aspect, pv)) col = glm::vec3(0.05f);
            if (ink("36V 5AH", cu, cv, 0.5f, 0.20f, pv2 / aspect, pv2)) col = glm::vec3(0.93f, 0.72f, 0.08f);
            col = glm::mix(col, col * 0.7f, wear * 0.5f);
            c.put(x, y, col * grain, 0.5f, 0.5f - 0.3f * wear, 0.0f);
        } else if (ci == 1 && cj == 3) {
            // Warning sticker (60 x 30 mm).
            const float aspect = 2.0f;
            const float edge = std::min(std::min(cu, 1.0f - cu) * aspect, std::min(cv, 1.0f - cv));
            glm::vec3 col = edge < 0.07f ? glm::vec3(0.05f) : glm::vec3(0.95f, 0.80f, 0.10f);
            const float pv = 0.26f / static_cast<float>(font::kGlyphH), pv2 = 0.2f / static_cast<float>(font::kGlyphH);
            if (ink("DANGER", cu, cv, 0.5f, 0.84f, pv / aspect, pv) || ink("HIGH VOLTAGE", cu, cv, 0.5f, 0.40f, pv2 / aspect, pv2)) {
                col = glm::vec3(0.05f);
            }
            c.put(x, y, col * grain * (1.0f - 0.25f * wear), 0.5f, 0.45f, 0.0f);
        } else if (ci == 2 && cj == 3) {
            // Driver panel (80 x 50 mm).
            const float aspect = 1.6f;
            glm::vec3 col = glm::vec3(0.16f, 0.17f, 0.18f) * (0.9f + 0.1f * wear);
            const float pv = 0.24f / static_cast<float>(font::kGlyphH);
            if (ink("DRSSTC", cu, cv, 0.5f, 0.82f, pv / aspect, pv) || ink("DRIVER", cu, cv, 0.5f, 0.46f, pv / aspect, pv)) {
                col = glm::vec3(0.85f, 0.85f, 0.80f);
            }
            if (sq((cu - 0.2f) * aspect) + sq(cv - 0.14f) < 0.004f) col = glm::vec3(0.5f, 0.05f, 0.04f); // screw-terminal dots
            if (sq((cu - 0.8f) * aspect) + sq(cv - 0.14f) < 0.004f) col = glm::vec3(0.05f, 0.05f, 0.05f);
            c.put(x, y, col * grain, 0.5f, 0.35f, 0.0f);
        } else if (ci == 0 && cj == 2) {
            // Circuit board: traces on a periodic lattice, pads at their ends.
            const float tx = fract(cu * 14.0f), ty = fract(cv * 14.0f);
            const uint32_t h = rnd::hash2i(static_cast<int>(cu * 14.0f), static_cast<int>(cv * 14.0f), 0x9CB1u);
            const bool horiz = (h & 1u) != 0u, vert = (h & 2u) != 0u, pad = (h & 12u) == 12u;
            bool copper = (horiz && std::fabs(ty - 0.5f) < 0.08f) || (vert && std::fabs(tx - 0.5f) < 0.08f);
            if (pad && sq(tx - 0.5f) + sq(ty - 0.5f) < 0.06f) copper = true;
            glm::vec3 col = copper ? glm::vec3(0.72f, 0.60f, 0.30f) : glm::vec3(0.07f, 0.28f, 0.12f);
            c.put(x, y, col * grain, copper ? 0.6f : 0.45f, copper ? 0.8f : 0.5f, 0.0f);
        } else if (ci == 3 && (cj == 3 || cj == 2)) {
            // Gauge LED lens: a domed rectangle in a black bezel.
            const float aspect = 0.012f / 0.008f;
            const float edge = std::min(std::min(cu, 1.0f - cu) * aspect, std::min(cv, 1.0f - cv));
            const float lens = smooth(0.08f, 0.16f, edge);
            const float hot = gauss(cu, 0.5f, 0.3f) * gauss(cv, 0.5f, 0.35f);
            const glm::vec3 tint = cj == 3 ? glm::vec3(0.12f, 0.55f, 0.16f) : glm::vec3(0.55f, 0.07f, 0.04f);
            const glm::vec3 col = glm::mix(glm::vec3(0.03f), tint * (0.6f + 0.4f * hot), lens);
            c.put(x, y, col, 0.5f + 0.3f * lens, 0.9f, lens * (0.6f + 0.4f * hot));
        } else {
            c.put(x, y, glm::vec3(0.08f) * grain, 0.5f, 0.3f, 0.0f); // unused
        }
    });
}

// ----- Writing ---------------------------------------------------------------------------
// Distance fields of every glyph of the built-in font (see Render/AtlasLayout.h):
// the pen strokes of a hand that joins the font's pixels (a little unsteadily),
// the drips that run down from the strokes' lower ends, and the crisp pixel
// squares themselves. Stored in the surface layer; the albedo is unused.
void generateWriting(Canvas& c) {
    struct Seg {
        glm::vec2 a, b;
    };
    auto segDistance = [](const glm::vec2& p, const Seg& s) {
        const glm::vec2 ab = s.b - s.a;
        const float len2 = glm::dot(ab, ab);
        const float t = len2 > 1e-6f ? sat(glm::dot(p - s.a, ab) / len2) : 0.0f;
        return glm::length(p - (s.a + ab * t));
    };
    const int count = atlas::kGlyphCols * atlas::kGlyphRows;
    std::vector<std::vector<Seg>> strokes(static_cast<size_t>(count)), drips(static_cast<size_t>(count));
    std::vector<std::vector<glm::vec2>> squares(static_cast<size_t>(count)); // lower-left corners of lit pixels
    for (int i = 0; i < count; ++i) {
        const uint8_t* rows = font::glyph(static_cast<char>(32 + i));
        if (!rows || 32 + i == ' ') continue;
        auto lit = [rows](int col, int row) {
            return col >= 0 && col < font::kGlyphW && row >= 0 && row < font::kGlyphH && (rows[row] & (0x10 >> col)) != 0;
        };
        // Pixel centre (cell space, y up), nudged as an unsteady hand would place it.
        auto point = [i](int col, int row) {
            const uint32_t h = rnd::hash2i(i * 8 + col, row, 0x3A17u);
            return glm::vec2(atlas::kGlyphX + static_cast<float>(col) + 0.5f + (unit8(h, 0) - 0.5f) * 0.28f,
                             atlas::kBaseY + static_cast<float>(font::kGlyphH - row) - 0.5f + (unit8(h, 8) - 0.5f) * 0.28f);
        };
        std::vector<Seg>& s = strokes[static_cast<size_t>(i)];
        for (int row = 0; row < font::kGlyphH; ++row) {
            for (int col = 0; col < font::kGlyphW; ++col) {
                if (!lit(col, row)) continue;
                squares[static_cast<size_t>(i)].emplace_back(atlas::kGlyphX + static_cast<float>(col),
                                                             atlas::kBaseY + static_cast<float>(font::kGlyphH - 1 - row));
                const glm::vec2 p = point(col, row);
                bool joined = false;
                if (lit(col + 1, row)) { s.push_back({p, point(col + 1, row)}); joined = true; }
                if (lit(col, row + 1)) { s.push_back({p, point(col, row + 1)}); joined = true; }
                // Diagonals only where no orthogonal neighbour already turns the corner.
                if (lit(col + 1, row + 1) && !lit(col + 1, row) && !lit(col, row + 1)) { s.push_back({p, point(col + 1, row + 1)}); joined = true; }
                if (lit(col - 1, row + 1) && !lit(col - 1, row) && !lit(col, row + 1)) { s.push_back({p, point(col - 1, row + 1)}); joined = true; }
                const bool fromAbove = lit(col, row - 1) || lit(col - 1, row - 1) || lit(col + 1, row - 1) || lit(col - 1, row);
                if (!joined && !fromAbove) s.push_back({p, p}); // a lone dot
                // The lower ends of strokes drip (when the ink is wet enough).
                const uint32_t h = rnd::hash2i(i * 8 + col, row, 0xD819u);
                if (!lit(col, row + 1) && unit8(h, 0) < 0.45f) {
                    const float len = 0.8f + 4.2f * unit8(h, 8) * unit8(h, 16);
                    drips[static_cast<size_t>(i)].push_back({p, p - glm::vec2(0.0f, std::min(len, p.y - 0.35f))});
                }
            }
        }
    }
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const int col = std::min(atlas::kGlyphCols - 1, static_cast<int>(u * atlas::kGlyphCols));
        const int row = std::min(atlas::kGlyphRows - 1, static_cast<int>(v * atlas::kGlyphRows));
        const size_t i = static_cast<size_t>(row * atlas::kGlyphCols + col);
        const glm::vec2 p((u * atlas::kGlyphCols - static_cast<float>(col)) * atlas::kCellW,
                          (v * atlas::kGlyphRows - static_cast<float>(row)) * atlas::kCellH);
        float hand = atlas::kMaxDistance, drip = atlas::kMaxDistance, print = atlas::kMaxDistance;
        for (const Seg& s : strokes[i]) hand = std::min(hand, segDistance(p, s));
        for (const Seg& s : drips[i]) {
            // A drip thins as it runs and ends in a bead.
            const float along = sat((s.a.y - p.y) / std::max(s.a.y - s.b.y, 1e-3f));
            drip = std::min(drip, segDistance(p, s) + 0.06f * along);
            drip = std::min(drip, std::max(0.0f, glm::length(p - s.b) - 0.07f));
        }
        for (const glm::vec2& q : squares[i]) {
            const glm::vec2 d = glm::max(glm::max(q - p, p - (q + glm::vec2(1.0f))), glm::vec2(0.0f));
            print = std::min(print, glm::length(d));
        }
        c.put(x, y, glm::vec3(1.0f), hand / atlas::kMaxDistance, drip / atlas::kMaxDistance, print / atlas::kMaxDistance);
    });
}

// ----- Glitch ----------------------------------------------------------------------------
// Digital corruption: nested blocks of saturated colour (four scales), torn
// scanlines and rows of dead pixels, for the exit room's walls to break into.
void generateGlitch(Canvas& c) {
    const glm::vec3 palette[6] = {{1.0f, 0.05f, 0.85f}, {0.05f, 1.0f, 1.0f}, {0.35f, 1.0f, 0.15f},
                                  {1.0f, 1.0f, 1.0f},   {0.02f, 0.02f, 0.03f}, {0.15f, 0.2f, 1.0f}};
    forEachTexel(c, [&](int x, int y, float u, float v) {
        glm::vec3 col(0.05f);
        float glow = 0.0f;
        for (int level = 0; level < 4; ++level) {
            const int cells = 4 << level;
            const uint32_t h = rnd::hash2i(static_cast<int>(u * cells), static_cast<int>(v * cells), 0x6117u + static_cast<uint32_t>(level));
            if ((h & 7u) < 3u) {
                col = palette[(h >> 4) % 6u];
                glow = ((h >> 8) & 3u) == 0u ? 1.0f : 0.35f;
            }
        }
        // Torn rows: horizontally smeared bands of one colour.
        const int band = static_cast<int>(v * 128.0f);
        const uint32_t bh = rnd::hash2i(band, 0, 0x7EA2u);
        if ((bh & 15u) == 0u) {
            col = palette[(bh >> 4) % 6u] * (0.6f + 0.4f * unit8(bh, 12));
            glow = 0.8f;
        }
        const float scan = 0.7f + 0.3f * (fract(v * 512.0f) < 0.5f ? 1.0f : 0.0f);
        const float dead = noise::white(x / 4, y, 0x6D3Au) > 0.985f ? 1.0f : 0.0f;
        col = glm::mix(col * scan, glm::vec3(1.0f), dead);
        c.put(x, y, col, 0.5f, 0.6f, std::max(glow * scan, dead));
    });
}

// ----- Office paint ---------------------------------------------------------------------
// Light blue eggshell latex over drywall: the roller's stipple, faint lap marks
// where the roller was reloaded, and the barest scuffing.
void generateOfficePaint(Canvas& c) {
    const glm::vec3 base(0.60f, 0.73f, 0.86f);
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const float stipple = noise::fbm(u * 220.0f, v * 220.0f, 220, 220, 2, 0x0FA1u);
        const float roller = noise::fbm(u * 3.0f, v * 12.0f, 3, 12, 3, 0x0FA2u);
        const float laps = noise::fbm(u * 2.0f, v * 2.0f, 2, 2, 4, 0x0FA3u);
        const float scuff = smooth(0.72f, 0.9f, noise::fbm(u * 9.0f, v * 9.0f, 9, 9, 4, 0x0FA4u));
        glm::vec3 col = base * (1.0f + 0.018f * stipple + 0.015f * roller + 0.02f * laps);
        col = glm::mix(col, col * glm::vec3(0.88f, 0.88f, 0.9f), scuff * 0.25f);
        c.put(x, y, col, 0.5f + 0.25f * stipple, 0.45f - 0.15f * scuff, 0.0f);
    });
}

// ----- Office carpet ---------------------------------------------------------------------
// 50 cm grey loop-pile carpet tiles (5 x 5 per repeat), laid quarter-turned
// so the pile direction alternates; charcoal and blue flecks; dark seams.
void generateOfficeCarpet(Canvas& c) {
    const glm::vec3 base(0.37f, 0.38f, 0.40f);
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const float tu = u * 5.0f, tv = v * 5.0f;
        const int ti = static_cast<int>(tu), tj = static_cast<int>(tv);
        const float fu = fract(tu), fv = fract(tv);
        const bool turned = ((ti + tj) & 1) != 0;
        // Loop rows run along the tile's pile direction.
        const float across = turned ? fu : fv;
        const float rows = std::sin(across * 3.14159265f * 90.0f);
        const float loops = noise::fbm((turned ? u : v) * 400.0f, (turned ? v : u) * 40.0f, 400, 40, 2, 0x0C41u);
        const uint32_t fleck = rnd::hash2i(x / 2, y / 2, 0x0C42u);
        const float tileShade = 0.96f + 0.06f * unit8(rnd::hash2i(ti, tj, 0x0C43u), 0);
        const float seam = 1.0f - smooth(0.0f, 0.012f, std::min(std::min(fu, 1.0f - fu), std::min(fv, 1.0f - fv)));
        const float wear = noise::fbm(u * 4.0f, v * 4.0f, 4, 4, 4, 0x0C44u);
        glm::vec3 col = base * tileShade * (0.9f + 0.06f * rows + 0.08f * loops + 0.04f * wear);
        if ((fleck & 63u) == 0u) col = glm::vec3(0.15f, 0.15f, 0.17f);
        else if ((fleck & 255u) == 1u) col = glm::vec3(0.22f, 0.30f, 0.48f);
        col *= 1.0f - 0.35f * seam;
        c.put(x, y, col, 0.45f + 0.25f * rows * 0.5f + 0.2f * loops - 0.3f * seam, 0.06f, 0.0f);
    });
}

// ----- Office fabric ---------------------------------------------------------------------
// Cubicle partition cloth: a fine, dingy beige-grey basket weave with faint
// coffee stains and a fuzz of lint.
void generateOfficeFabric(Canvas& c) {
    const float threads = 220.0f;
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const float tu = u * threads, tv = v * threads;
        const int cu = static_cast<int>(std::floor(tu / 2.0f)), cv = static_cast<int>(std::floor(tv / 2.0f));
        const bool warpOver = ((cu + cv) & 1) == 0; // basket weave: pairs of threads
        const float profile = std::sin(3.14159265f * fract(warpOver ? tu : tv));
        const float heather = noise::white(warpOver ? static_cast<int>(tu) : 0, warpOver ? 0 : static_cast<int>(tv), 0x0FAB1u) - 0.5f;
        const float lint = noise::fbm(u * 300.0f, v * 300.0f, 300, 300, 2, 0x0FAB2u);
        const float stain = smooth(0.62f, 0.8f, noise::fbm(u * 3.0f, v * 3.0f, 3, 3, 4, 0x0FAB3u));
        glm::vec3 col = glm::vec3(0.55f, 0.52f, 0.46f) * (0.84f + 0.16f * profile) * (1.0f + 0.07f * heather + 0.04f * lint);
        col = glm::mix(col, col * glm::vec3(0.8f, 0.72f, 0.6f), stain * 0.4f);
        c.put(x, y, col, 0.3f + 0.6f * profile + 0.1f * lint, 0.08f, 0.0f);
    });
}

// ----- Signage ----------------------------------------------------------------------------
// Backings for the office's posters, notices, whiteboards and signs (see
// Render/AtlasLayout.h for the cells). The words go on top as glyph quads.
void generateSignage(Canvas& c) {
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const int ci = std::min(3, static_cast<int>(u * 4.0f)), cj = std::min(3, static_cast<int>(v * 4.0f));
        const float cu = u * 4.0f - static_cast<float>(ci), cv = v * 4.0f - static_cast<float>(cj);
        const auto sign = static_cast<atlas::Sign>((3 - cj) * 4 + ci);
        const float grain = 0.97f + 0.03f * noise::white(x, y, 0x5160Au);
        const float edge = std::min(std::min(cu, 1.0f - cu), std::min(cv, 1.0f - cv));
        const float fibre = noise::fbm(u * 160.0f, v * 160.0f, 160, 160, 2, 0x5160Bu);
        glm::vec3 col(0.9f);
        float spec = 0.2f, emissive = 0.0f;
        // A photograph-like landscape: sky gradient over layered ridges.
        auto landscape = [&](float pu, float pv, bool sea) {
            if (sea) {
                const glm::vec3 sky = glm::mix(glm::vec3(0.55f, 0.75f, 0.95f), glm::vec3(0.15f, 0.35f, 0.75f), pv);
                const float wave = 0.42f + 0.12f * std::sin(pu * 7.0f + 1.3f) * smooth(0.0f, 0.8f, pu) +
                                   0.05f * noise::fbm(pu * 6.0f, 0.0f, 6, 1, 3, 0x5EA1u);
                if (pv < wave) {
                    const float foam = smooth(wave - 0.04f, wave, pv);
                    return glm::mix(glm::vec3(0.05f, 0.25f, 0.42f) * (0.8f + 0.4f * pv), glm::vec3(0.92f), foam);
                }
                return sky;
            }
            glm::vec3 col = glm::mix(glm::vec3(1.0f, 0.62f, 0.25f), glm::vec3(0.35f, 0.18f, 0.45f), sat(pv * 1.2f));
            col += glm::vec3(1.0f, 0.85f, 0.5f) * gauss(glm::length(glm::vec2(pu - 0.62f, pv - 0.38f)), 0.0f, 0.09f);
            for (int k = 0; k < 3; ++k) {
                const float ridge = 0.25f + 0.1f * static_cast<float>(2 - k) +
                                    0.14f * noise::fbm(pu * (3.0f + k), static_cast<float>(k), 64, 64, 4, 0x3015u + static_cast<uint32_t>(k));
                if (pv < ridge) col = glm::mix(glm::vec3(0.08f, 0.06f, 0.12f), glm::vec3(0.3f, 0.18f, 0.3f), 0.6f - 0.25f * static_cast<float>(k));
            }
            return col;
        };
        switch (sign) {
        case atlas::Sign::Paper:
            col = glm::vec3(0.93f, 0.93f, 0.91f) * (0.98f + 0.02f * fibre);
            if (std::fabs(edge - 0.05f) < 0.006f) col = glm::vec3(0.25f);
            break;
        case atlas::Sign::SafetyYellow:
            col = glm::vec3(0.96f, 0.80f, 0.12f);
            if (cv > 0.74f && cv < 0.94f) col = glm::vec3(0.05f);
            if (edge < 0.04f) col = glm::vec3(0.05f);
            break;
        case atlas::Sign::PosterMountain:
        case atlas::Sign::PosterSea: {
            col = glm::vec3(0.03f);
            const float pu = (cu - 0.08f) / 0.84f, pv = (cv - 0.30f) / 0.62f;
            if (pu > 0.0f && pu < 1.0f && pv > 0.0f && pv < 1.0f) col = landscape(pu, pv, sign == atlas::Sign::PosterSea);
            else if (edge > 0.03f && std::fabs(cv - 0.27f) > 0.012f) col = glm::vec3(0.02f); // the caption band
            spec = 0.5f;
            break;
        }
        case atlas::Sign::Whiteboard: {
            const float ghost = smooth(0.55f, 0.75f, noise::fbm(u * 14.0f, v * 6.0f, 14, 6, 4, 0x3B0Au));
            col = glm::mix(glm::vec3(0.94f, 0.95f, 0.96f), glm::vec3(0.78f, 0.80f, 0.86f), ghost * 0.35f);
            spec = 0.9f;
            break;
        }
        case atlas::Sign::PlacardDark:
            col = glm::vec3(0.12f, 0.12f, 0.13f) * (0.95f + 0.08f * noise::fbm(u * 2.0f, v * 200.0f, 2, 200, 2, 0x91Au));
            if (std::fabs(edge - 0.06f) < 0.008f) col = glm::vec3(0.55f);
            spec = 0.5f;
            break;
        case atlas::Sign::PlacardBlue:
            col = glm::mix(glm::vec3(0.10f, 0.22f, 0.50f), glm::vec3(0.06f, 0.14f, 0.36f), cv);
            if (std::fabs(edge - 0.06f) < 0.008f) col = glm::vec3(0.85f);
            spec = 0.45f;
            break;
        case atlas::Sign::ExitFace:
            col = glm::vec3(0.05f, 0.05f, 0.055f);
            if (edge < 0.05f) col = glm::vec3(0.85f, 0.85f, 0.82f); // white housing rim
            spec = 0.5f;
            break;
        case atlas::Sign::StickyNote:
            col = glm::vec3(0.98f, 0.90f, 0.38f) * (0.92f + 0.08f * cv) * (0.98f + 0.02f * fibre);
            break;
        case atlas::Sign::PosterBlue:
            col = glm::mix(glm::vec3(0.05f, 0.12f, 0.35f), glm::vec3(0.25f, 0.55f, 0.85f), cv);
            col += glm::vec3(0.3f, 0.35f, 0.4f) * gauss(cu - cv * 0.5f, 0.3f, 0.05f); // a lens flare streak
            if (edge < 0.06f) col = glm::vec3(0.03f);
            spec = 0.5f;
            break;
        case atlas::Sign::Brass:
            col = glm::vec3(0.72f, 0.56f, 0.26f) * (0.9f + 0.12f * noise::fbm(u * 2.0f, v * 300.0f, 2, 300, 2, 0xB4A5u));
            spec = 0.85f;
            break;
        case atlas::Sign::Chart: {
            col = glm::vec3(0.95f, 0.95f, 0.93f);
            if (fract(cu * 12.0f) < 0.05f || fract(cv * 12.0f) < 0.05f) col = glm::vec3(0.75f, 0.82f, 0.9f);
            const int bar = static_cast<int>(cu * 6.0f);
            const float top = 0.15f + 0.13f * static_cast<float>(bar);
            if (fract(cu * 6.0f) > 0.2f && fract(cu * 6.0f) < 0.8f && cv > 0.1f && cv < top && bar < 6) {
                col = glm::vec3(0.15f, 0.45f, 0.25f);
            }
            break;
        }
        default:
            col = glm::vec3(0.5f);
            break;
        }
        c.put(x, y, col * grain, 0.5f, spec, emissive);
    });
}

// ----- Foliage -------------------------------------------------------------------------------
// Left half: a glossy ficus leaf (u across, v from stem to tip); right half: a
// fern frond with alternating leaflets. The surface alpha is the outline.
void generateFoliage(Canvas& c) {
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const bool fern = u >= 0.5f;
        const float lx = (fern ? u - 0.75f : u - 0.25f) * 4.0f; // -1..1 across the half
        const float ly = v;
        const float vein = noise::fbm(u * 40.0f, v * 40.0f, 40, 40, 3, 0xF011u);
        float opacity = 0.0f;
        glm::vec3 col;
        if (!fern) {
            // Elliptic, drawn out to a pointed tip.
            const float width = 0.92f * std::pow(std::sin(3.14159265f * sat(ly * 0.96f + 0.02f)), 0.75f) * (1.0f - 0.35f * ly);
            const float d = std::fabs(lx) - width;
            opacity = 1.0f - smooth(-0.04f, 0.0f, d);
            const float midrib = gauss(lx, 0.0f, 0.035f);
            const float side = gauss(fract((ly - std::fabs(lx) * 0.45f) * 9.0f), 0.5f, 0.06f) * (1.0f - midrib);
            col = glm::mix(glm::vec3(0.10f, 0.26f, 0.07f), glm::vec3(0.18f, 0.36f, 0.10f), vein * 0.5f + 0.5f * ly);
            col = glm::mix(col, glm::vec3(0.40f, 0.55f, 0.25f), midrib * 0.8f + side * 0.25f);
            c.put(x, y, col, 0.5f + 0.2f * midrib, 0.75f, 0.0f, opacity);
            return;
        }
        // Fern: a rachis and paired leaflets angled towards the tip.
        const float rachis = 1.0f - smooth(0.025f, 0.045f, std::fabs(lx));
        const float reach = 0.9f * std::pow(sat(1.0f - ly), 0.6f) * smooth(0.0f, 0.12f, ly);
        const float ax = std::fabs(lx);
        const float slot = (ly - ax * 0.35f) * 22.0f;
        const float k = fract(slot);
        const float lobe = 0.5f - std::fabs(k - 0.5f);
        const float leaflet = (ax < reach && ly > 0.05f) ? smooth(0.05f, 0.18f, lobe * (1.0f - ax / std::max(reach, 1e-3f) * 0.6f)) : 0.0f;
        opacity = std::max(rachis * (ly < 0.98f ? 1.0f : 0.0f), leaflet);
        col = glm::mix(glm::vec3(0.16f, 0.36f, 0.09f), glm::vec3(0.30f, 0.50f, 0.14f), vein * 0.6f + 0.4f * ax);
        col = glm::mix(col, glm::vec3(0.28f, 0.34f, 0.12f), rachis * 0.7f);
        c.put(x, y, col, 0.5f, 0.35f, 0.0f, opacity);
    });
}

// ----- Office paper --------------------------------------------------------------------------
// Bright copier paper: fine fibres; on the sides of a stack, the faint lines of the sheets.
void generateOfficePaper(Canvas& c) {
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const float fibre = noise::fbm(u * 200.0f, v * 200.0f, 200, 200, 2, 0x9A9Eu);
        const float sheets = 0.97f + 0.03f * std::sin(v * 3.14159265f * 600.0f);
        const glm::vec3 col = glm::vec3(0.93f, 0.93f, 0.91f) * (0.98f + 0.02f * fibre) * sheets;
        c.put(x, y, col, 0.5f + 0.1f * fibre, 0.15f, 0.0f);
    });
}

// ----- Water bottle ----------------------------------------------------------------------------
// The water cooler's polycarbonate bottle: clear blue, brightened where light
// would pass through the water, with moulded ribs.
void generateWaterBottle(Canvas& c) {
    forEachTexel(c, [&](int x, int y, float u, float v) {
        const float ribs = 0.5f + 0.5f * std::sin(v * 3.14159265f * 24.0f);
        const float caustic = noise::fbm(u * 6.0f, v * 6.0f, 6, 6, 4, 0xB077u);
        const glm::vec3 col = glm::mix(glm::vec3(0.30f, 0.52f, 0.80f), glm::vec3(0.55f, 0.75f, 0.95f), 0.4f * caustic + 0.2f * ribs);
        c.put(x, y, col, 0.4f + 0.3f * ribs, 0.95f, 0.0f);
    });
}

} // namespace

bool MaterialLibrary::build(int size) {
    const auto start = std::chrono::steady_clock::now();

    // Generator table indexed by MaterialId.
    using Generator = void (*)(Canvas&);
    const Generator generators[kMaterialCount] = {
        generateWallpaper, generateCarpet, generateCeiling, generateWood,
        generateMetal,     generateLightPanel, generatePlastic, generateFabric,
        generateConcrete,  generateBeigePlastic, generateCrtScreen, generateFlesh,
        generateStairSign, generatePhoneKeys, generateCopper, generateAluminum,
        generateManila,    generateTeslaLabels, generateWriting, generateGlitch,
        generateOfficePaint, generateOfficeCarpet, generateOfficeFabric, generateSignage,
        generateFoliage,   generateOfficePaper, generateWaterBottle,
    };

    // Synthesise every layer concurrently: each generator is independent and
    // writes only its own canvas.
    std::vector<Canvas> canvases;
    canvases.reserve(kMaterialCount);
    for (int i = 0; i < kMaterialCount; ++i) canvases.emplace_back(size);

    std::vector<std::future<void>> jobs;
    jobs.reserve(kMaterialCount);
    for (int i = 0; i < kMaterialCount; ++i) {
        jobs.push_back(std::async(std::launch::async, generators[i], std::ref(canvases[static_cast<size_t>(i)])));
    }
    for (auto& job : jobs) job.get();

    m_albedo.create(size, kMaterialCount, GL_SRGB8_ALPHA8);
    m_surface.create(size, kMaterialCount, GL_RGBA8);
    for (int i = 0; i < kMaterialCount; ++i) {
        m_albedo.uploadLayer(i, canvases[static_cast<size_t>(i)].albedo.data());
        m_surface.uploadLayer(i, canvases[static_cast<size_t>(i)].surface.data());
    }

    float maxAniso = 1.0f;
    if (SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic") ||
        SDL_GL_ExtensionSupported("GL_ARB_texture_filter_anisotropic")) {
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);
        maxAniso = std::min(maxAniso, 16.0f);
    }
    m_albedo.finalize(maxAniso);
    m_surface.finalize(maxAniso);

    for (int i = 0; i < kMaterialCount; ++i) {
        m_params[static_cast<size_t>(i)] = materialShaderParams(static_cast<MaterialId>(i));
    }

    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "[Materials] Generated " << kMaterialCount << " procedural materials at " << size << "x" << size
              << " in " << static_cast<int>(ms) << " ms (anisotropy " << maxAniso << "x)\n";
    return glGetError() == GL_NO_ERROR;
}

void MaterialLibrary::bind(GLuint albedoUnit, GLuint surfaceUnit) const {
    m_albedo.bind(albedoUnit);
    m_surface.bind(surfaceUnit);
}
