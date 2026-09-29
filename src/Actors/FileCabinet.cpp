// ---------------------------------------------------------------------------
// FileCabinet.cpp
// Procedural modelling and drawer animation of the filing cabinet.
// Dimensions in metres (0.46 x 1.02 x 0.61 body, three 312 mm drawers).
// ---------------------------------------------------------------------------
#include "Actors/FileCabinet.h"

#include "Math/Random.h"
#include "Render/MeshBuilder.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace {

constexpr float kHalfW = 0.23f, kTop = 1.02f, kBack = -0.31f, kFront = 0.30f;
constexpr float kSkin = 0.012f;          ///< Sheet steel.
constexpr float kBottom = 0.04f, kDrawerTop = 1.0f, kGap = 0.012f;
constexpr float kDrawerH = (kDrawerTop - kBottom - 2.0f * kGap) / 3.0f;
constexpr float kTrayDepth = 0.57f;      ///< Tray length behind the front panel.
constexpr float kTravel = 0.40f;         ///< How far a drawer rolls out.
constexpr float kOpenTime = 0.45f, kCloseTime = 0.32f;

inline float drawerBottom(int d) { return kBottom + static_cast<float>(d) * (kDrawerH + kGap); }
inline float ease(float t) { return t * t * (3.0f - 2.0f * t); }

void box(MeshData& m, const glm::vec3& mn, const glm::vec3& mx, MaterialId mat, uint8_t faces = mesh::FaceAll,
         const glm::mat4& t = glm::mat4(1.0f)) {
    mesh::BoxDesc d;
    d.min = mn;
    d.max = mx;
    d.material = mat;
    d.faces = faces;
    d.transform = t;
    mesh::addBox(m, d);
}

} // namespace

FileCabinet::FileCabinet(uint64_t id, const glm::mat4& model) : m_id(id), m_model(model) {}

MeshData FileCabinet::buildShellMesh() {
    MeshData m;
    const float in = kHalfW - kSkin;
    // Sides and back: painted outside, dark inside (where the drawers ride).
    box(m, {-kHalfW, 0.0f, kBack}, {-in, kTop, kFront}, MaterialId::GrayMetal, mesh::FaceAll & ~mesh::FacePosX);
    box(m, {in, 0.0f, kBack}, {kHalfW, kTop, kFront}, MaterialId::GrayMetal, mesh::FaceAll & ~mesh::FaceNegX);
    box(m, {-in, kBottom, kBack}, {in, kDrawerTop, kBack + kSkin}, MaterialId::GrayMetal, mesh::FaceNegZ);
    box(m, {-in, kBottom, kBack + kSkin}, {-in + 0.001f, kDrawerTop, kFront}, MaterialId::DarkPlastic, mesh::FacePosX);
    box(m, {in - 0.001f, kBottom, kBack + kSkin}, {in, kDrawerTop, kFront}, MaterialId::DarkPlastic, mesh::FaceNegX);
    box(m, {-in, kBottom, kBack + kSkin}, {in, kDrawerTop, kBack + kSkin + 0.001f}, MaterialId::DarkPlastic, mesh::FacePosZ);
    // Top and plinth.
    box(m, {-in, kDrawerTop, kBack + kSkin}, {in, kTop, kFront}, MaterialId::GrayMetal, mesh::FacePosY | mesh::FacePosZ);
    box(m, {-in, kDrawerTop - 0.001f, kBack + kSkin}, {in, kDrawerTop, kFront}, MaterialId::DarkPlastic, mesh::FaceNegY);
    box(m, {-in, 0.0f, kBack + kSkin}, {in, kBottom, kFront - 0.005f}, MaterialId::DarkPlastic,
        mesh::FacePosZ | mesh::FacePosY);
    // Rails between the drawers: a painted front edge, dark shelves behind.
    for (int d = 0; d < 2; ++d) {
        const float y0 = drawerBottom(d) + kDrawerH, y1 = y0 + kGap;
        box(m, {-in, y0, kFront - 0.01f}, {in, y1, kFront}, MaterialId::GrayMetal, mesh::FacePosZ | mesh::FacePosY | mesh::FaceNegY);
        box(m, {-in, y0, kBack + kSkin}, {in, y1, kFront - 0.01f}, MaterialId::DarkPlastic, mesh::FacePosY | mesh::FaceNegY);
    }
    return m;
}

MeshData FileCabinet::buildDrawerMesh(int variant) {
    MeshData m;
    const float h = kDrawerH;
    // Front panel with its pull and a label holder holding a blank card.
    box(m, {-0.215f, 0.0f, 0.0f}, {0.215f, h, 0.012f}, MaterialId::GrayMetal);
    box(m, {-0.08f, h - 0.085f, 0.012f}, {0.08f, h - 0.065f, 0.032f}, MaterialId::DarkPlastic, mesh::FaceAll & ~mesh::FaceNegZ);
    box(m, {-0.045f, h - 0.055f, 0.012f}, {0.045f, h - 0.030f, 0.015f}, MaterialId::GrayMetal, mesh::FaceAll & ~mesh::FaceNegZ);
    box(m, {-0.038f, h - 0.050f, 0.015f}, {0.038f, h - 0.035f, 0.0158f}, MaterialId::Manila, mesh::FacePosZ);

    // The tray: bottom, sides topped with hanging rails, back.
    const float wall = 0.27f, back = -kTrayDepth;
    box(m, {-0.20f, 0.005f, back}, {0.20f, 0.012f, 0.0f}, MaterialId::GrayMetal, mesh::FacePosY | mesh::FaceNegY);
    for (float s : {-1.0f, 1.0f}) {
        const float x0 = s < 0.0f ? -0.20f : 0.192f;
        box(m, {x0, 0.005f, back}, {x0 + 0.008f, wall, 0.0f}, MaterialId::GrayMetal, mesh::FaceSides | mesh::FacePosY);
        const float r0 = s < 0.0f ? -0.200f : 0.188f;
        box(m, {r0, wall, back}, {r0 + 0.012f, wall + 0.008f, 0.0f}, MaterialId::GrayMetal, mesh::FaceAll & ~mesh::FaceNegY);
    }
    box(m, {-0.192f, 0.005f, back}, {0.192f, wall, back + 0.008f}, MaterialId::GrayMetal, mesh::FaceAll);

    // Hanging folders at the back: packed tight, sparse, or slumped forward.
    rnd::Rng rng(rnd::hashCombine(0xF01D'E850ull, static_cast<uint64_t>(variant)));
    const int count = variant == 0 ? 14 : variant == 1 ? 8 : 6;
    const float first = back + 0.02f, spacing = variant == 0 ? 0.021f : variant == 1 ? 0.028f : 0.035f;
    for (int i = 0; i < count; ++i) {
        const float z = first + spacing * static_cast<float>(i);
        const float tilt = variant == 2 ? glm::radians(10.0f + 4.0f * static_cast<float>(i)) : rng.range(-0.05f, 0.08f);
        const glm::mat4 hang = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, wall + 0.004f, z)), tilt, glm::vec3(1, 0, 0));
        box(m, {-0.197f, -0.004f, -0.0015f}, {0.197f, 0.002f, 0.0015f}, MaterialId::GrayMetal, mesh::FaceAll, hang); // hanger rod
        box(m, {-0.175f, -0.235f, -0.0015f}, {0.175f, -0.002f, 0.0015f}, MaterialId::Manila, mesh::FaceAll, hang);
        const float tab = -0.13f + 0.085f * static_cast<float>((i + variant) % 4);
        box(m, {tab - 0.025f, -0.002f, -0.001f}, {tab + 0.025f, 0.022f, 0.001f}, MaterialId::Manila, mesh::FaceAll, hang);
        if (rng.chance(0.3f)) { // papers sticking up out of the folder
            const float x = rng.range(-0.1f, 0.1f);
            box(m, {x - 0.1f, -0.2f, 0.0017f}, {x + 0.1f, rng.range(0.005f, 0.03f), 0.0025f}, MaterialId::Manila, mesh::FaceAll, hang);
        }
    }
    return m;
}

const std::vector<AABB>& FileCabinet::localColliders() {
    static const std::vector<AABB> kColliders = {AABB({-kHalfW, 0.0f, kBack}, {kHalfW, kTop, 0.335f})};
    return kColliders;
}

glm::mat4 FileCabinet::itemSurface() { return glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.012f, -0.15f)); }

int FileCabinet::drawerVariant(int drawer) const {
    return static_cast<int>(rnd::hashCombine(m_id, static_cast<uint64_t>(drawer)) % kDrawerVariants);
}

void FileCabinet::openDrawer(int drawer) {
    if (drawer == m_target) return;
    for (int d = 0; d < kDrawers; ++d) {
        if (d == drawer) m_events |= kEventOpen;
        else if (d == m_target && m_open[static_cast<size_t>(d)] > 0.0f) m_events |= kEventClose;
    }
    m_target = drawer;
}

bool FileCabinet::anyOpen() const {
    return std::any_of(m_open.begin(), m_open.end(), [](float o) { return o > 0.0f; });
}

void FileCabinet::update(float dt) {
    for (int d = 0; d < kDrawers; ++d) {
        float& o = m_open[static_cast<size_t>(d)];
        o = d == m_target ? std::min(1.0f, o + dt / kOpenTime) : std::max(0.0f, o - dt / kCloseTime);
    }
}

uint8_t FileCabinet::takeEvents() {
    const uint8_t e = m_events;
    m_events = 0;
    return e;
}

glm::mat4 FileCabinet::drawerMatrix(int drawer) const {
    const float out = kTravel * ease(m_open[static_cast<size_t>(drawer)]);
    return glm::translate(m_model, glm::vec3(0.0f, drawerBottom(drawer), kFront + out));
}

glm::vec3 FileCabinet::frontCenter() const { return glm::vec3(m_model * glm::vec4(0.0f, 0.55f, 0.32f, 1.0f)); }

glm::vec3 FileCabinet::frontNormal() const { return glm::normalize(glm::vec3(m_model * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f))); }

glm::vec3 FileCabinet::viewPoint(int drawer) const {
    const float d = static_cast<float>(std::clamp(drawer, 0, kDrawers - 1));
    // High and close, looking steeply down over the drawer front into it.
    const float y = drawerBottom(static_cast<int>(d)) + kDrawerH + 0.55f;
    return glm::vec3(m_model * glm::vec4(0.0f, y, kFront + kTravel + 0.22f, 1.0f));
}

glm::vec3 FileCabinet::viewTarget(int drawer) const {
    const int d = std::clamp(drawer, 0, kDrawers - 1);
    return glm::vec3(m_model * glm::vec4(0.0f, drawerBottom(d) + 0.05f, kFront + kTravel - 0.17f, 1.0f));
}
