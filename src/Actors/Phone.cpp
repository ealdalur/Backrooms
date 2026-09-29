// ---------------------------------------------------------------------------
// Phone.cpp
// Procedural modelling of the desk telephone. Dimensions in metres.
// ---------------------------------------------------------------------------
#include "Actors/Phone.h"

#include "Render/MeshBuilder.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// Side profile of the base in the (z, y) plane, counter-clockwise: a flat
// plinth, the sloped keypad deck rising to the flat cradle hump at the back.
constexpr int kProfileCount = 7;
const glm::vec2 kProfile[kProfileCount] = {
    {-0.120f, 0.000f}, {0.120f, 0.000f}, {0.120f, 0.028f}, {0.113f, 0.036f}, // front edge (bevelled)
    {-0.030f, 0.078f}, {-0.112f, 0.078f}, {-0.120f, 0.070f},                // hump top, back bevel
};
constexpr float kHalfWidth = 0.105f;
// The keypad deck is the profile edge from kProfile[3] up to kProfile[4].
constexpr int kDeckLow = 3, kDeckHigh = 4;

// Keypad in deck space (x across, s down the slope from the hump, y out of the deck).
constexpr float kKeyPitchX = 0.028f, kKeyPitchS = 0.024f;
constexpr float kKeyFirstS = 0.022f;
constexpr float kKeyHalfW = 0.0105f, kKeyHalfD = 0.0085f;
constexpr float kKeyTop = 0.0065f;    ///< Keycap top above the deck.
constexpr float kKeyTravel = 0.0032f; ///< How far a key goes down.
constexpr float kPressTime = 0.14f;

// The handset rests across the hump.
constexpr float kCradleZ = -0.078f;
constexpr float kCupX = 0.080f;

// Texture atlas of the PhoneKeys material: 4 x 4 cells; keys in columns 0-2,
// the lamp and the number card in column 3.
constexpr float kCell = 0.25f;

void box(MeshData& m, const glm::mat4& t, const glm::vec3& mn, const glm::vec3& mx, MaterialId mat,
         uint8_t faces = mesh::FaceAll) {
    mesh::BoxDesc d;
    d.min = mn;
    d.max = mx;
    d.material = mat;
    d.faces = faces;
    d.transform = t;
    mesh::addBox(m, d);
}

/// Deck space -> phone space: origin where the deck meets the hump, +Z down
/// the slope towards the front, +Y the deck's outward normal.
glm::mat4 deckFrame() {
    const glm::vec2 lo = kProfile[kDeckLow], hi = kProfile[kDeckHigh];
    const glm::vec2 d = glm::normalize(lo - hi); // (z, y) down the slope
    glm::mat4 f(1.0f);
    f[1] = glm::vec4(0.0f, d.x, -d.y, 0.0f);     // outward normal (y = dz, z = -dy)
    f[2] = glm::vec4(0.0f, d.y, d.x, 0.0f);      // down the slope
    f[3] = glm::vec4(0.0f, hi.y, hi.x, 1.0f);
    return f;
}

glm::vec3 keyPosition(int key) {
    const int col = key % 3, row = key / 3;
    return {static_cast<float>(col - 1) * kKeyPitchX, 0.0f, kKeyFirstS + static_cast<float>(row) * kKeyPitchS};
}

/// A flat quad on a plane of `frame` at height y, spanning x0..x1 and s0..s1
/// (s0 at the top of the texture), textured with the atlas rectangle uv0..uv1.
void atlasQuad(MeshData& m, const glm::mat4& frame, float x0, float x1, float s0, float s1, float y,
               const glm::vec2& uv0, const glm::vec2& uv1) {
    const glm::vec3 local[4] = {{x0, y, s1}, {x1, y, s1}, {x1, y, s0}, {x0, y, s0}};
    glm::vec3 corners[4];
    for (int i = 0; i < 4; ++i) corners[i] = glm::vec3(frame * glm::vec4(local[i], 1.0f));
    const glm::vec2 uvs[4] = {{uv0.x, uv0.y}, {uv1.x, uv0.y}, {uv1.x, uv1.y}, {uv0.x, uv1.y}};
    mesh::addQuad(m, corners, glm::normalize(glm::vec3(frame[1])), uvs, MaterialId::PhoneKeys);
}

/// The base: the profile extruded across the width, with planar UVs.
void addBody(MeshData& m, MaterialId mat) {
    const float invTile = 1.0f / materialInfo(mat).tileSize;
    const float fm = static_cast<float>(mat);
    // Sloped and flat faces around the profile (the bottom rests on the desk).
    float along = 0.0f;
    for (int i = 0; i < kProfileCount; ++i) {
        const glm::vec2 a = kProfile[i], b = kProfile[(i + 1) % kProfileCount];
        const glm::vec2 e = b - a;
        const float len = glm::length(e);
        if (i != 0) {
            const glm::vec3 corners[4] = {{-kHalfWidth, a.y, a.x}, {kHalfWidth, a.y, a.x},
                                          {kHalfWidth, b.y, b.x}, {-kHalfWidth, b.y, b.x}};
            const glm::vec2 uvs[4] = {{-kHalfWidth * invTile, along * invTile}, {kHalfWidth * invTile, along * invTile},
                                      {kHalfWidth * invTile, (along + len) * invTile},
                                      {-kHalfWidth * invTile, (along + len) * invTile}};
            mesh::addQuad(m, corners, glm::normalize(glm::vec3(0.0f, -e.x, e.y)), uvs, mat);
        }
        along += len;
    }
    // The two side walls: triangle fans over the (convex) profile.
    for (int side = 0; side < 2; ++side) {
        const float x = side == 0 ? -kHalfWidth : kHalfWidth;
        const glm::vec3 n(side == 0 ? -1.0f : 1.0f, 0.0f, 0.0f);
        const uint32_t base = static_cast<uint32_t>(m.vertices.size());
        for (const glm::vec2& p : kProfile) {
            m.vertices.push_back({glm::vec3(x, p.y, p.x), n, glm::vec2(p.x, p.y) * invTile, fm, -1.0f});
        }
        for (uint32_t i = 1; i + 1 < static_cast<uint32_t>(kProfileCount); ++i) {
            // Counter-clockwise in (z, y) faces -X; the +X wall is wound the other way.
            if (side == 0) m.indices.insert(m.indices.end(), {base, base + i, base + i + 1});
            else           m.indices.insert(m.indices.end(), {base, base + i + 1, base + i});
        }
    }
}

glm::vec3 bezier(const glm::vec3 p[4], float t) {
    const float u = 1.0f - t;
    return u * u * u * p[0] + 3.0f * u * u * t * p[1] + 3.0f * u * t * t * p[2] + t * t * t * p[3];
}

/// A coiled cord: a helix wound around a cubic Bezier path, `pitch` metres per turn.
void addCoiledCord(MeshData& m, const glm::vec3 path[4], float pitch, MaterialId mat) {
    constexpr float kCoilRadius = 0.0058f, kWire = 0.0017f;
    constexpr int kSamples = 400, kSegmentsPerTurn = 6;
    // Arc-length table of the path.
    std::vector<glm::vec3> pts(kSamples + 1);
    std::vector<float> dist(kSamples + 1, 0.0f);
    for (int i = 0; i <= kSamples; ++i) {
        pts[static_cast<size_t>(i)] = bezier(path, static_cast<float>(i) / kSamples);
        pts[static_cast<size_t>(i)].y = std::max(pts[static_cast<size_t>(i)].y, kCoilRadius + kWire + 0.0005f);
        if (i > 0) dist[static_cast<size_t>(i)] = dist[static_cast<size_t>(i) - 1] + glm::length(pts[static_cast<size_t>(i)] - pts[static_cast<size_t>(i) - 1]);
    }
    const float length = dist.back();
    const int turns = std::max(4, static_cast<int>(length / pitch));
    const int segments = turns * kSegmentsPerTurn;

    auto at = [&](float s, glm::vec3& pos, glm::vec3& tangent) {
        const size_t i = static_cast<size_t>(std::upper_bound(dist.begin(), dist.end(), s) - dist.begin());
        const size_t hi = std::clamp<size_t>(i, 1, kSamples), lo = hi - 1;
        const float span = std::max(dist[hi] - dist[lo], 1e-6f);
        pos = glm::mix(pts[lo], pts[hi], std::clamp((s - dist[lo]) / span, 0.0f, 1.0f));
        tangent = glm::normalize(pts[hi] - pts[lo]);
    };

    // Parallel-transported frame along the path, so the coil never twists.
    glm::vec3 pos, t;
    at(0.0f, pos, t);
    glm::vec3 u = glm::normalize(glm::cross(t, std::fabs(t.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
    glm::vec3 prev(0.0f);
    for (int k = 0; k <= segments; ++k) {
        const float s = length * static_cast<float>(k) / static_cast<float>(segments);
        at(s, pos, t);
        u = glm::normalize(u - t * glm::dot(u, t));
        const glm::vec3 v = glm::cross(t, u);
        const float phi = 6.2831853f * static_cast<float>(k) / static_cast<float>(kSegmentsPerTurn);
        const glm::vec3 p = pos + kCoilRadius * (std::cos(phi) * u + std::sin(phi) * v);
        if (k > 0) mesh::addLimb(m, prev, p, kWire, kWire, mat, 4, 1);
        prev = p;
    }
}

} // namespace

Phone::Phone(uint64_t id, const glm::mat4& model) : m_id(id), m_model(model) {}

PhoneLook Phone::look() const {
    const int base = charcoal() ? static_cast<int>(PhoneLook::Charcoal) : static_cast<int>(PhoneLook::Beige);
    return static_cast<PhoneLook>(base + (m_offHook ? 1 : 0));
}

int Phone::keyIndex(char c) {
    const char* p = std::strchr(kKeyChars, c);
    return c != '\0' && p ? static_cast<int>(p - kKeyChars) : -1;
}

MeshData Phone::buildMesh(PhoneLook look) {
    MeshData m;
    const bool offHook = look == PhoneLook::BeigeOffHook || look == PhoneLook::CharcoalOffHook;
    const bool charcoal = look == PhoneLook::Charcoal || look == PhoneLook::CharcoalOffHook;
    const MaterialId body = charcoal ? MaterialId::DarkPlastic : MaterialId::BeigePlastic;
    const MaterialId dark = MaterialId::DarkPlastic;
    const glm::mat4 I(1.0f);
    const glm::mat4 deck = deckFrame();
    const uint8_t noBottom = mesh::FaceAll & ~mesh::FaceNegY;

    addBody(m, body);

    // ---- Keypad deck furniture: speaker grille, line buttons, the number card --------
    for (int i = 0; i < 6; ++i) {
        const float s = 0.030f + 0.011f * static_cast<float>(i);
        box(m, deck, {-0.086f, 0.0f, s}, {-0.050f, 0.0006f, s + 0.0035f}, dark, noBottom);
    }
    for (int i = 0; i < 3; ++i) {
        const float s = 0.024f + 0.028f * static_cast<float>(i);
        box(m, deck, {0.058f, -0.002f, s}, {0.080f, 0.0048f, s + 0.013f}, dark, mesh::FaceSides | mesh::FacePosY);
    }
    atlasQuad(m, deck, -0.042f, 0.042f, 0.112f, 0.138f, 0.0008f, {3 * kCell + 0.004f, 2 * kCell + 0.004f},
              {4 * kCell - 0.004f, 3 * kCell - 0.004f});
    box(m, deck, {-0.046f, 0.0f, 0.108f}, {0.046f, 0.0016f, 0.112f}, body, noBottom); // card window frame
    box(m, deck, {-0.046f, 0.0f, 0.138f}, {0.046f, 0.0016f, 0.142f}, body, noBottom);
    box(m, deck, {-0.046f, 0.0f, 0.112f}, {-0.042f, 0.0016f, 0.138f}, body, noBottom);
    box(m, deck, {0.042f, 0.0f, 0.112f}, {0.046f, 0.0016f, 0.138f}, body, noBottom);

    // ---- Cradle: two saddles on the hump; the hook switches spring up when lifted ------
    const float hump = kProfile[kDeckHigh].y;
    for (float sx : {-1.0f, 1.0f}) {
        box(m, I, {sx * kCupX - 0.022f, hump, kCradleZ - 0.026f}, {sx * kCupX + 0.022f, hump + 0.005f, kCradleZ + 0.026f},
            body, noBottom);
        if (offHook) {
            box(m, I, {sx * kCupX - 0.006f, hump + 0.005f, kCradleZ - 0.006f},
                {sx * kCupX + 0.006f, hump + 0.016f, kCradleZ + 0.006f}, dark, noBottom);
        }
    }

    // Message lamp in front of the cradle. The off-hook look shifts its UVs by
    // +2 in u: the shader lights it steadily ("line in use") instead of blinking.
    box(m, I, {0.070f, hump, -0.047f}, {0.090f, hump + 0.004f, -0.033f}, dark, mesh::FaceSides);
    {
        const float du = offHook ? 2.0f : 0.0f;
        atlasQuad(m, I, 0.070f, 0.090f, -0.047f, -0.033f, hump + 0.004f, {3 * kCell + 0.02f + du, 3 * kCell + 0.02f},
                  {4 * kCell - 0.02f + du, 4 * kCell - 0.02f});
    }

    // ---- Handset: earpiece and mouthpiece cups joined by an arched grip ----------------
    if (!offHook) {
        const float y0 = hump + 0.005f;
        for (float sx : {-1.0f, 1.0f}) {
            mesh::addCylinder(m, glm::translate(I, glm::vec3(sx * kCupX, 0.0f, kCradleZ)), 0.024f, y0, y0 + 0.016f, 16, body);
        }
        const glm::vec3 mid(0.0f, y0 + 0.028f, kCradleZ);
        mesh::addLimb(m, glm::vec3(-kCupX, y0 + 0.018f, kCradleZ), mid, 0.0135f, 0.0115f, body, 12, 3);
        mesh::addLimb(m, mid, glm::vec3(kCupX, y0 + 0.018f, kCradleZ), 0.0115f, 0.0135f, body, 12, 3);
    }

    // ---- The coiled cord: from the jack on the left side to the handset ---------------
    box(m, I, {-kHalfWidth - 0.004f, 0.010f, 0.024f}, {-kHalfWidth, 0.022f, 0.036f}, dark, mesh::FaceAll & ~mesh::FacePosX);
    const glm::vec3 jack(-kHalfWidth - 0.006f, 0.016f, 0.030f);
    if (!offHook) {
        // Lying in a loop on the desk, climbing up into the earpiece end.
        const glm::vec3 path[4] = {jack, {-0.215f, 0.004f, 0.085f}, {-0.205f, 0.004f, -0.135f},
                                   {-kCupX - 0.024f, hump + 0.013f, kCradleZ}};
        addCoiledCord(m, path, 0.0085f, body);
    } else {
        // Stretched up and away towards the listener's ear, out of view.
        const glm::vec3 path[4] = {jack, {-0.19f, 0.010f, 0.10f}, {-0.24f, 0.10f, 0.30f}, {-0.20f, 0.34f, 0.46f}};
        addCoiledCord(m, path, 0.021f, body);
    }
    return m;
}

MeshData Phone::buildKeyMesh(int key) {
    MeshData m;
    const glm::mat4 I(1.0f);
    // The cap reaches well below the deck so it never shows a gap when pressed.
    box(m, I, {-kKeyHalfW, -0.0045f, -kKeyHalfD}, {kKeyHalfW, kKeyTop, kKeyHalfD}, MaterialId::BeigePlastic, mesh::FaceSides);
    const int col = key % 3, row = key / 3;
    const glm::vec2 cell(static_cast<float>(col) * kCell, static_cast<float>(3 - row) * kCell);
    atlasQuad(m, I, -kKeyHalfW, kKeyHalfW, -kKeyHalfD, kKeyHalfD, kKeyTop, cell + glm::vec2(0.004f),
              cell + glm::vec2(kCell - 0.004f));
    return m;
}

const std::vector<AABB>& Phone::localColliders() {
    static const std::vector<AABB> kColliders = {AABB({-kHalfWidth, 0.0f, -0.12f}, {kHalfWidth, 0.11f, 0.12f})};
    return kColliders;
}

void Phone::press(int key) {
    if (key < 0 || key >= kKeyCount) return;
    m_pressedKey = key;
    m_pressTimer = kPressTime;
}

void Phone::update(float dt) {
    if (m_pressTimer > 0.0f && (m_pressTimer -= dt) <= 0.0f) m_pressedKey = -1;
}

glm::mat4 Phone::keyMatrix(int key) const {
    glm::vec3 p = keyPosition(key);
    if (key == m_pressedKey) p.y -= kKeyTravel;
    return m_model * deckFrame() * glm::translate(glm::mat4(1.0f), p);
}

void Phone::keyHitCorners(int key, glm::vec3 out[4]) const {
    const glm::mat4 toWorld = m_model * deckFrame();
    const glm::vec3 c = keyPosition(key);
    const float hx = kKeyPitchX * 0.5f, hs = kKeyPitchS * 0.5f;
    const glm::vec3 local[4] = {{c.x - hx, kKeyTop, c.z + hs}, {c.x + hx, kKeyTop, c.z + hs},
                                {c.x + hx, kKeyTop, c.z - hs}, {c.x - hx, kKeyTop, c.z - hs}};
    for (int i = 0; i < 4; ++i) out[i] = glm::vec3(toWorld * glm::vec4(local[i], 1.0f));
}

glm::vec3 Phone::center() const { return glm::vec3(m_model * glm::vec4(0.0f, 0.06f, 0.0f, 1.0f)); }

glm::vec3 Phone::viewPoint() const { return glm::vec3(m_model * glm::vec4(0.0f, 0.36f, 0.24f, 1.0f)); }

glm::vec3 Phone::viewTarget() const { return glm::vec3(m_model * glm::vec4(0.0f, 0.058f, 0.035f, 1.0f)); }
