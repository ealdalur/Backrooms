// ---------------------------------------------------------------------------
// MaterialLibrary.cpp
// Procedural material synthesis. Every generator writes a tileable square
// image using periodic noise so the textures repeat seamlessly.
// ---------------------------------------------------------------------------
#include "Render/MaterialLibrary.h"

#include "Math/Noise.h"
#include "Math/Random.h"

#include <SDL3/SDL_video.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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

    void put(int x, int y, const glm::vec3& color, float height, float spec, float emissive) {
        const size_t i = (static_cast<size_t>(y) * size + x) * 4;
        albedo[i + 0] = static_cast<uint8_t>(sat(color.r) * 255.0f + 0.5f);
        albedo[i + 1] = static_cast<uint8_t>(sat(color.g) * 255.0f + 0.5f);
        albedo[i + 2] = static_cast<uint8_t>(sat(color.b) * 255.0f + 0.5f);
        albedo[i + 3] = 255;
        surface[i + 0] = static_cast<uint8_t>(sat(height) * 255.0f + 0.5f);
        surface[i + 1] = static_cast<uint8_t>(sat(spec) * 255.0f + 0.5f);
        surface[i + 2] = static_cast<uint8_t>(sat(emissive) * 255.0f + 0.5f);
        surface[i + 3] = 255;
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

} // namespace

bool MaterialLibrary::build(int size) {
    const auto start = std::chrono::steady_clock::now();

    // Generator table indexed by MaterialId.
    using Generator = void (*)(Canvas&);
    const Generator generators[kMaterialCount] = {
        generateWallpaper, generateCarpet, generateCeiling, generateWood,
        generateMetal,     generateLightPanel, generatePlastic, generateFabric,
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
