// ---------------------------------------------------------------------------
// TitleRenderer.cpp
// ---------------------------------------------------------------------------
#include "Render/TitleRenderer.h"

#include "Render/BitmapFont.h"
#include "Render/ShaderSources.h"
#include "Render/TextOverlay.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kReach = 0.75f;         ///< Cap heights round the letters the field covers (the halo's reach).
constexpr float kTexelsPerUnit = 112.0f; ///< Field resolution, per cap height.
constexpr float kLetterGap = 0.24f;     ///< Between one letter's box and the next.
constexpr int   kFaultyLetter = 6;      ///< The second O.
constexpr float kTypeStart = 1.2f;     ///< When the tagline starts typing itself out...
constexpr float kTypeRate = 38.0f;      ///< ...at this many characters a second.
const char* const kTagline = "MONO-YELLOW  /  MOIST CARPET  /  HUM-BUZZ";

// ---- The lettering -------------------------------------------------------------------------

struct Segment {
    glm::vec2 a, b;
};

/// One letter: its tubes' centrelines, in cap heights (x from its left edge, y up from the baseline).
struct Letter {
    float                width = 0.0f;
    std::vector<Segment> segments;

    void line(std::initializer_list<glm::vec2> points) {
        const glm::vec2* p = points.begin();
        for (size_t i = 1; i < points.size(); ++i) segments.push_back({p[i - 1], p[i]});
    }
    /// An elliptic arc from angle a0 to a1 (degrees, anticlockwise from +x; a1 < a0 runs clockwise).
    void arc(glm::vec2 c, float rx, float ry, float a0, float a1) {
        const int n = std::max(8, static_cast<int>(std::fabs(a1 - a0) / 7.5f));
        glm::vec2 prev = c + glm::vec2(rx * std::cos(glm::radians(a0)), ry * std::sin(glm::radians(a0)));
        for (int i = 1; i <= n; ++i) {
            const float a = glm::radians(a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(n));
            const glm::vec2 p = c + glm::vec2(rx * std::cos(a), ry * std::sin(a));
            segments.push_back({prev, p});
            prev = p;
        }
    }
};

/// A rounded geometric sans, as a sign-maker would bend it out of glass.
std::vector<Letter> buildWord() {
    Letter B{0.62f, {}};
    B.line({{0.0f, 0.0f}, {0.0f, 1.0f}, {0.32f, 1.0f}});
    B.arc({0.32f, 0.76f}, 0.24f, 0.24f, 90.0f, -90.0f);
    B.line({{0.0f, 0.52f}, {0.36f, 0.52f}});
    B.arc({0.36f, 0.26f}, 0.26f, 0.26f, 90.0f, -90.0f);
    B.line({{0.36f, 0.0f}, {0.0f, 0.0f}});

    Letter A{0.70f, {}};
    A.line({{0.0f, 0.0f}, {0.35f, 1.0f}, {0.70f, 0.0f}});
    A.line({{0.117f, 0.335f}, {0.583f, 0.335f}});

    Letter C{0.74f, {}};
    C.arc({0.42f, 0.5f}, 0.42f, 0.5f, 42.0f, 318.0f);

    Letter K{0.62f, {}};
    K.line({{0.0f, 0.0f}, {0.0f, 1.0f}});
    K.line({{0.60f, 1.0f}, {0.0f, 0.42f}});
    K.line({{0.20f, 0.613f}, {0.62f, 0.0f}});

    Letter R{0.62f, {}};
    R.line({{0.0f, 0.0f}, {0.0f, 1.0f}, {0.32f, 1.0f}});
    R.arc({0.32f, 0.74f}, 0.26f, 0.26f, 90.0f, -90.0f);
    R.line({{0.32f, 0.48f}, {0.0f, 0.48f}});
    R.line({{0.30f, 0.48f}, {0.62f, 0.0f}});

    Letter O{0.84f, {}};
    O.arc({0.42f, 0.5f}, 0.42f, 0.5f, 0.0f, 360.0f);

    Letter M{0.82f, {}};
    M.line({{0.0f, 0.0f}, {0.0f, 1.0f}, {0.41f, 0.36f}, {0.82f, 1.0f}, {0.82f, 0.0f}});

    Letter S{0.62f, {}};
    S.arc({0.31f, 0.755f}, 0.29f, 0.245f, 28.0f, 270.0f);
    S.arc({0.31f, 0.255f}, 0.31f, 0.255f, 90.0f, -152.0f);

    return {B, A, C, K, R, O, O, M, S};
}

float segmentDistance(const glm::vec2& p, const Segment& s) {
    const glm::vec2 ab = s.b - s.a;
    const float t = std::clamp(glm::dot(p - s.a, ab) / std::max(glm::dot(ab, ab), 1e-8f), 0.0f, 1.0f);
    return glm::length(p - (s.a + ab * t));
}

/// Integer hash -> [0, 1).
float hash01(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return static_cast<float>(x & 0xFFFFFFu) / 16777216.0f;
}
float hash01(int a, int b) { return hash01(static_cast<uint32_t>(a) * 0x9E3779B1u ^ static_cast<uint32_t>(b) * 0x85EBCA77u); }

float smooth01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

/// How brightly letter `i`'s tube burns at `time` (0 = dark).
float tubeLight(int i, float time, float fade) {
    // Each one strikes on by itself, left to right but not in step.
    const float strike = 0.22f + 0.075f * static_cast<float>(i) + 0.2f * hash01(i, 1);
    if (time < strike) return 0.0f;
    const float s = time - strike;
    float light = 1.0f;
    if (s < 0.5f) {
        // The starter's stutter: blinks, mostly dark at first, mostly lit by the end.
        const int blink = static_cast<int>(s * 32.0f);
        light = hash01(i * 131 + blink, 2) < 0.3f + 1.4f * s ? 1.0f : 0.06f;
    }
    // Mains flicker: never quite steady.
    light *= 0.95f + 0.05f * hash01(static_cast<int>(time * 55.0f), i + 10);
    if (i == kFaultyLetter && s >= 0.5f) {
        // The dying tube: drops out now and then, in short bursts.
        const int beat = static_cast<int>(time * 13.0f);
        if (hash01(beat, 7) > 0.84f) light *= 0.1f + 0.2f * hash01(beat, 8);
    }
    if (fade > 0.0f) {
        // Going out unevenly, with a last few flickers.
        light *= 1.0f - smooth01(fade * 1.35f - 0.35f * hash01(i, 3));
        if (hash01(static_cast<int>(time * 30.0f), i + 40) < 0.45f * fade) light *= 0.25f;
    }
    return light;
}

const char* const kTitleFragment = R"GLSL(
#version 330 core
out vec4 oColor;

uniform sampler2D uField;     // r, g: distances (cap heights) to the nearest two letters; b, a: which letters
uniform vec2  uFieldSize;     // texels
uniform vec4  uRect;          // the field on screen: x0, y0, x1, y1 (pixels, origin bottom-left)
uniform float uUnitPx;        // pixels per cap height
uniform vec4  uLetters[3];    // each letter's tube brightness, 0..1
uniform float uSignLight;     // their average: the light the sign throws round itself
uniform float uGlass;         // 0..1, the unlit glass showing
uniform float uTear;          // 0..1, VHS tracking trouble
uniform vec3  uLine;          // the underline tube: centre x, y and half-length (pixels)
uniform float uLineLight;
uniform float uTime;
uniform float uAlpha;

const float kTube  = 0.058;   // tube radius (cap heights)
const float kReach = 0.75;    // the field's distances stop here
const vec3  kCore  = vec3(1.0, 0.98, 0.90);   // white-hot gas
const vec3  kHalo  = vec3(1.0, 0.76, 0.28);   // mono-yellow glow

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float letterLight(float id) {
    int k = int(id + 0.5);
    return uLetters[k / 4][k % 4];
}

// The light of the tubes at field coordinate uv; the glass of the nearest one (coverage, shade).
vec3 tubes(vec2 uv, out vec2 glass) {
    vec4  f  = texture(uField, uv);
    ivec2 tc = clamp(ivec2(uv * uFieldSize), ivec2(0), ivec2(uFieldSize) - 1);
    vec2  id = texelFetch(uField, tc, 0).ba;
    float aa = 0.9 / uUnitPx;
    vec3  light = vec3(0.0);
    glass = vec2(0.0);
    for (int j = 0; j < 2; ++j) {
        float d = j == 0 ? f.r : f.g;
        float b = letterLight(j == 0 ? id.x : id.y);
        float core = 1.0 - smoothstep(kTube - aa, kTube + aa, d);
        float x = clamp(d / kTube, 0.0, 1.0);
        float profile = sqrt(1.0 - x * x);                  // round tube: hottest down its middle
        float outside = max(d - kTube, 0.0);
        float halo = (0.62 * exp(-outside * 15.0) + 0.24 * exp(-outside * 4.5)) * (1.0 - smoothstep(0.0, kReach - kTube, outside));
        light += b * (core * mix(kHalo, kCore, profile) * (0.75 + 1.25 * profile) + halo * kHalo * (1.0 - core));
        if (j == 0) glass = vec2(core, 0.35 + 0.65 * pow(profile, 5.0));
    }
    return light;
}

void main() {
    vec2 frag = gl_FragCoord.xy;
    // VHS tracking: now and then bands of rows slip sideways.
    float tick = floor(uTime * 24.0);
    float band = floor(frag.y / max(2.0, uUnitPx * 0.08));
    float slip = hash12(vec2(band, tick)) < 0.3 ? hash12(vec2(band * 1.7, tick + 9.0)) - 0.5 : 0.0;
    vec2  p = frag + vec2(slip * uTear * uUnitPx * 0.3, 0.0);

    vec2 size = uRect.zw - uRect.xy;
    vec2 uv = (p - uRect.xy) / size;
    vec2 split = vec2((1.2 + 7.0 * uTear) / size.x, 0.0);   // the colour channels drift apart
    vec2 glass, unused;
    vec3 l0 = tubes(uv - split, unused);
    vec3 l1 = tubes(uv, glass);
    vec3 l2 = tubes(uv + split, unused);
    vec3 light = vec3(l0.r, l1.g, l2.b);

    // The underline: one thin tube, drawn out from the middle.
    if (uLine.z > 0.0) {
        vec2  lp = p - uLine.xy;
        float ld = length(vec2(max(abs(lp.x) - uLine.z, 0.0), lp.y));
        float r = max(1.0, uUnitPx * 0.012);
        float core = 1.0 - smoothstep(r - 0.8, r + 0.8, ld);
        light += uLineLight * (core * kCore + exp(-ld / (uUnitPx * 0.04)) * 0.5 * kHalo * (1.0 - core));
    }

    // The sign lights the dark round it; a dark band behind keeps it readable over bright rooms.
    vec2  q = (frag - 0.5 * (uRect.xy + uRect.zw)) / (size * vec2(0.62, 0.8));
    float r2 = dot(q, q);
    light += uSignLight * 0.07 * exp(-r2 * 1.8) * kHalo;
    float backdrop = 0.5 * exp(-r2 * 2.4);

    light *= 0.88 + 0.12 * sin(frag.y * 3.14159265);        // scanlines
    light *= 1.0 + (hash12(frag + fract(uTime * 7.31) * vec2(113.1, 71.7)) - 0.5) * 0.12; // grain
    vec3 emit = 1.0 - exp(-light * 1.3);                     // the tubes' light piles up softly, never clips

    float glassA = glass.x * uGlass * 0.9;
    vec3  glassRgb = vec3(0.17, 0.155, 0.12) * glass.y * glassA * (1.0 - max(emit.r, max(emit.g, emit.b)));
    // Premultiplied: the light adds, the backdrop and the glass cover what is behind.
    oColor = vec4(emit + glassRgb, max(backdrop, glassA)) * uAlpha;
}
)GLSL";

} // namespace

TitleRenderer::~TitleRenderer() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

bool TitleRenderer::init() {
    if (!m_shader.build(shaders::kFullscreenVertex, kTitleFragment, "Title")) return false;
    glGenVertexArrays(1, &m_vao);

    // Lay the word out, then measure every texel's distance to its two nearest letters.
    std::vector<Letter> word = buildWord();
    std::vector<float> left(word.size());
    float x = 0.0f;
    for (size_t i = 0; i < word.size(); ++i) {
        left[i] = x;
        for (Segment& s : word[i].segments) {
            s.a.x += x;
            s.b.x += x;
        }
        x += word[i].width + kLetterGap;
    }
    m_wordWidth = x - kLetterGap;

    const int w = static_cast<int>(std::ceil((m_wordWidth + 2.0f * kReach) * kTexelsPerUnit));
    const int h = static_cast<int>(std::ceil((1.0f + 2.0f * kReach) * kTexelsPerUnit));
    std::vector<float> texels(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    auto fillRows = [&](int y0, int y1) {
        for (int ty = y0; ty < y1; ++ty) {
            for (int tx = 0; tx < w; ++tx) {
                const glm::vec2 p(-kReach + (static_cast<float>(tx) + 0.5f) / kTexelsPerUnit,
                                  -kReach + (static_cast<float>(ty) + 0.5f) / kTexelsPerUnit);
                float d1 = kReach, d2 = kReach;
                int i1 = 0, i2 = 0;
                for (size_t i = 0; i < word.size(); ++i) {
                    // Only letters within reach count.
                    if (p.x < left[i] - kReach || p.x > left[i] + word[i].width + kReach) continue;
                    float d = kReach;
                    for (const Segment& s : word[i].segments) d = std::min(d, segmentDistance(p, s));
                    if (d < d1) {
                        d2 = d1;
                        i2 = i1;
                        d1 = d;
                        i1 = static_cast<int>(i);
                    } else if (d < d2) {
                        d2 = d;
                        i2 = static_cast<int>(i);
                    }
                }
                float* t = &texels[(static_cast<size_t>(ty) * static_cast<size_t>(w) + static_cast<size_t>(tx)) * 4];
                t[0] = d1;
                t[1] = d2;
                t[2] = static_cast<float>(i1);
                t[3] = static_cast<float>(i2);
            }
        }
    };
    const int workers = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 8);
    std::vector<std::thread> threads;
    for (int k = 0; k < workers; ++k) threads.emplace_back(fillRows, h * k / workers, h * (k + 1) / workers);
    for (std::thread& t : threads) t.join();

    m_field.create(w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT, texels.data(), GL_LINEAR, GL_CLAMP_TO_EDGE);
    return glGetError() == GL_NO_ERROR;
}

void TitleRenderer::draw(float time, float fade, int width, int height, TextOverlay& hud) {
    if (!m_vao) return;
    const float W = static_cast<float>(width), H = static_cast<float>(height);
    const float alpha = 1.0f - smooth01(fade);
    if (alpha <= 0.0f) return;

    // Layout: the word across two thirds of the screen, a little above the middle.
    const float unit = std::min(0.66f * W / m_wordWidth, 0.17f * H);
    const float cx = 0.5f * W;
    const float baseline = H - (0.42f * H + 0.5f * unit); // origin bottom-left
    const float x0 = cx - (0.5f * m_wordWidth + kReach) * unit;
    const float x1 = cx + (0.5f * m_wordWidth + kReach) * unit;
    const float y0 = baseline - kReach * unit, y1 = baseline + (1.0f + kReach) * unit;

    glm::vec4 lights[3] = {glm::vec4(0.0f), glm::vec4(0.0f), glm::vec4(0.0f)};
    float total = 0.0f;
    for (int i = 0; i < kLetters; ++i) {
        lights[i / 4][i % 4] = tubeLight(i, time, fade);
        total += lights[i / 4][i % 4];
    }
    // Tracking trouble: as the tubes strike on, in brief fits after, and as it all fades away.
    const int fit = static_cast<int>(time * 5.0f);
    float tear = 0.55f * (1.0f - smooth01((time - 0.3f) / 0.9f)) * smooth01(time / 0.3f);
    if (time > 1.5f && hash01(fit, 99) > 0.92f) tear = std::max(tear, 0.45f);
    tear = std::max(tear, 0.9f * fade);
    const float grow = smooth01((time - 1.0f) / 0.55f);
    const float underlineY = baseline - 0.36f * unit;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    m_shader.use();
    m_field.bind(0);
    m_shader.set("uField", 0);
    m_shader.set("uFieldSize", glm::vec2(static_cast<float>(m_field.width()), static_cast<float>(m_field.height())));
    m_shader.set("uRect", glm::vec4(x0, y0, x1, y1));
    m_shader.set("uUnitPx", unit);
    m_shader.setArray("uLetters", lights, 3);
    m_shader.set("uSignLight", total / static_cast<float>(kLetters));
    m_shader.set("uGlass", smooth01(time / 0.6f));
    m_shader.set("uTear", tear);
    m_shader.set("uLine", glm::vec3(cx, underlineY, 0.5f * m_wordWidth * unit * grow));
    m_shader.set("uLineLight", 0.8f * grow * (0.94f + 0.06f * hash01(static_cast<int>(time * 50.0f), 77)));
    m_shader.set("uTime", time);
    m_shader.set("uAlpha", alpha);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    // The tagline types itself out under the line, a cursor blinking at its end.
    const std::string tagline(kTagline);
    const size_t typed = std::min(tagline.size(), static_cast<size_t>(std::max(0.0f, (time - kTypeStart) * kTypeRate)));
    if (time > kTypeStart - 0.4f) {
        const float size = std::max(1.0f, unit * 0.14f / static_cast<float>(font::kGlyphH)) / hud.pixelScale();
        const float s = std::max(1.0f, std::round(hud.pixelScale() * size));
        const float textY = H - underlineY + 0.2f * unit;
        const float fullW = static_cast<float>(tagline.size()) * (font::kGlyphW + 1) * s - s;
        const float left = std::round(cx - 0.5f * fullW); // fixed, so the text does not slide as it types
        const glm::vec4 ink(1.0f, 0.88f, 0.56f, 0.85f * alpha);
        hud.text(tagline.substr(0, typed), left, textY, TextOverlay::Align::Left, size, ink);
        if (std::fmod(time, 0.5f) < 0.3f && time < kTypeStart + static_cast<float>(tagline.size()) / kTypeRate + 1.2f) {
            const float cursorX = left + static_cast<float>(typed) * (font::kGlyphW + 1) * s;
            hud.text(std::string(1, font::kBlock), cursorX, textY, TextOverlay::Align::Left, size, ink);
        }
    }
}
