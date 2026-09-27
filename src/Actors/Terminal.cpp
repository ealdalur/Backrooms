// ---------------------------------------------------------------------------
// Terminal.cpp
// Procedural modelling of the retro computer. Dimensions in metres.
// ---------------------------------------------------------------------------
#include "Actors/Terminal.h"

#include "Render/MeshBuilder.h"

namespace {

// Screen glass rectangle on the monitor front (local space).
constexpr float kScreenX0 = -0.16f, kScreenX1 = 0.16f;
constexpr float kScreenY0 = 0.17f,  kScreenY1 = 0.43f;
constexpr float kScreenZ  = 0.131f;

void box(MeshData& m, const glm::vec3& mn, const glm::vec3& mx, MaterialId mat, uint8_t faces = mesh::FaceAll) {
    mesh::BoxDesc d;
    d.min = mn;
    d.max = mx;
    d.material = mat;
    d.faces = faces;
    mesh::addBox(m, d);
}

} // namespace

Terminal::Terminal(uint64_t id, const glm::mat4& model, bool powered) : m_id(id), m_model(model), m_powered(powered) {}

MeshData Terminal::buildMesh(TerminalLook look) {
    using namespace mesh;
    MeshData m;
    const MaterialId beige = MaterialId::BeigePlastic;
    const MaterialId dark = MaterialId::DarkPlastic;
    const uint8_t noBottom = FaceAll & ~FaceNegY;

    // ---- Desktop system unit with a 5.25" floppy slot and power button ----------------
    box(m, {-0.23f, 0.0f, -0.22f}, {0.23f, 0.11f, 0.17f}, beige, noBottom);
    box(m, {0.03f, 0.048f, 0.17f}, {0.19f, 0.064f, 0.176f}, dark, FaceAll & ~FaceNegZ);     // drive slot
    box(m, {0.04f, 0.030f, 0.17f}, {0.18f, 0.036f, 0.173f}, dark, FaceAll & ~FaceNegZ);     // drive latch
    box(m, {-0.20f, 0.030f, 0.17f}, {-0.16f, 0.070f, 0.182f}, dark, FaceAll & ~FaceNegZ);   // power button
    for (int i = 0; i < 6; ++i) {                                                            // vent slots
        const float x = -0.12f + 0.02f * static_cast<float>(i);
        box(m, {x, 0.03f, 0.17f}, {x + 0.008f, 0.08f, 0.172f}, dark, FaceAll & ~FaceNegZ);
    }

    // ---- CRT monitor: swivel foot, front case, tapered tube housing -------------------
    box(m, {-0.09f, 0.11f, -0.12f}, {0.09f, 0.13f, 0.06f}, beige, FaceSides | FacePosY);
    box(m, {-0.20f, 0.13f, -0.12f}, {0.20f, 0.47f, 0.13f}, beige);
    box(m, {-0.16f, 0.16f, -0.24f}, {0.16f, 0.44f, -0.12f}, beige, FaceAll & ~FacePosZ);
    box(m, {-0.11f, 0.20f, -0.32f}, {0.11f, 0.40f, -0.24f}, beige, FaceAll & ~FacePosZ);

    // Raised bezel framing the recessed glass.
    box(m, {-0.18f, 0.43f, 0.13f}, {0.18f, 0.45f, 0.14f}, beige, FaceAll & ~FaceNegZ);
    box(m, {-0.18f, 0.15f, 0.13f}, {0.18f, 0.17f, 0.14f}, beige, FaceAll & ~FaceNegZ);
    box(m, {-0.18f, 0.17f, 0.13f}, {-0.16f, 0.43f, 0.14f}, beige, FaceAll & ~FaceNegZ);
    box(m, {0.16f, 0.17f, 0.13f}, {0.18f, 0.43f, 0.14f}, beige, FaceAll & ~FaceNegZ);
    box(m, {0.11f, 0.153f, 0.14f}, {0.15f, 0.165f, 0.146f}, dark, FaceAll & ~FaceNegZ);     // brightness knob

    // Screen glass (UV 0..1 across the raster; amber shifted by +2 in u).
    {
        const glm::vec3 corners[4] = {{kScreenX0, kScreenY0, kScreenZ}, {kScreenX1, kScreenY0, kScreenZ},
                                      {kScreenX1, kScreenY1, kScreenZ}, {kScreenX0, kScreenY1, kScreenZ}};
        const float u0 = look == TerminalLook::Amber ? 2.0f : 0.0f;
        const glm::vec2 uvs[4] = {{u0, 0.0f}, {u0 + 1.0f, 0.0f}, {u0 + 1.0f, 1.0f}, {u0, 1.0f}};
        const MaterialId glass = look == TerminalLook::Off ? MaterialId::DarkPlastic : MaterialId::CrtScreen;
        addQuad(m, corners, glm::vec3(0.0f, 0.0f, 1.0f), uvs, glass);
    }

    // ---- Keyboard: 4 rows of 15 keycaps plus a space bar ----------------------------------
    box(m, {-0.22f, 0.0f, 0.20f}, {0.22f, 0.024f, 0.36f}, beige, noBottom);
    const float pitch = 0.0275f, cap = 0.023f;
    for (int row = 0; row < 4; ++row) {
        const float z0 = 0.215f + pitch * static_cast<float>(row);
        const float stagger = 0.006f * static_cast<float>(row);
        for (int col = 0; col < 15; ++col) {
            const float x0 = -0.207f + stagger + pitch * static_cast<float>(col);
            if (x0 + cap > 0.21f) break;
            box(m, {x0, 0.024f, z0}, {x0 + cap, 0.036f, z0 + cap}, beige, FaceSides | FacePosY);
        }
    }
    box(m, {-0.09f, 0.024f, 0.325f}, {0.09f, 0.036f, 0.348f}, beige, FaceSides | FacePosY);
    return m;
}

const std::vector<AABB>& Terminal::localColliders() {
    static const std::vector<AABB> kColliders = {
        AABB({-0.23f, 0.0f, -0.32f}, {0.23f, 0.47f, 0.17f}), // system unit + monitor
        AABB({-0.22f, 0.0f, 0.20f}, {0.22f, 0.036f, 0.36f}), // keyboard
    };
    return kColliders;
}

glm::vec3 Terminal::screenCenter() const {
    const glm::vec3 local(0.5f * (kScreenX0 + kScreenX1), 0.5f * (kScreenY0 + kScreenY1), kScreenZ);
    return glm::vec3(m_model * glm::vec4(local, 1.0f));
}

glm::vec3 Terminal::screenNormal() const {
    return glm::normalize(glm::vec3(m_model * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
}

glm::vec3 Terminal::viewPoint() const {
    return screenCenter() + screenNormal() * 0.55f + glm::vec3(0.0f, 0.04f, 0.0f);
}

glm::vec3 Terminal::center() const { return glm::vec3(m_model * glm::vec4(0.0f, 0.25f, 0.0f, 1.0f)); }
