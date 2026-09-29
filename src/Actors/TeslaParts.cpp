// ---------------------------------------------------------------------------
// TeslaParts.cpp
// Procedural modelling of the gun's parts. Dimensions in metres.
// ---------------------------------------------------------------------------
#include "Actors/TeslaParts.h"

#include "Render/MeshBuilder.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265f;

// ---- Battery: a brick along X.
constexpr float kBatHalfX = 0.07f, kBatHalfZ = 0.05f, kBatTop = 0.075f;

// ---- Driver: a steel box along Z with the primary on its front.
constexpr float kDrvHalfX = 0.05f, kDrvTop = 0.075f, kDrvBack = -0.11f, kDrvFront = 0.08f;
constexpr float kAxisY = 0.0375f;                  ///< Height of the coil axis (driver part space and gun space).
constexpr float kPrimaryRadius = 0.030f, kPrimaryTube = 0.0035f, kPrimaryTurns = 3.5f;
constexpr float kPrimaryStart = 0.092f, kPrimaryPitch = 0.012f;

// ---- Coil: a column along +Y.
constexpr float kCoilRadius = 0.018f, kCoilCap = 0.021f, kCoilTop = 0.268f;

// ---- Top load: a toroid lying flat.
constexpr float kTorusR = 0.05f, kTorusr = 0.02f;
constexpr float kSpikeBase = 0.028f, kSpikeTip = 0.105f;

// ---- Where the parts sit in the gun (gun space).
constexpr float kMountFront = 0.088f; ///< Coil base: just inside the primary.
constexpr float kTopLoadZ = kMountFront + 0.250f - 0.002f;

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

/// A flat quad from the TeslaLabels atlas: centre, in-plane right / up axes,
/// half size, atlas cell and a u offset (+2 = lit LED, +4 = blinking LED).
void label(MeshData& m, const glm::vec3& c, const glm::vec3& right, const glm::vec3& up, float hw, float hh, int ci, int cj,
           float uOffset = 0.0f) {
    const glm::vec3 corners[4] = {c - right * hw - up * hh, c + right * hw - up * hh, c + right * hw + up * hh,
                                  c - right * hw + up * hh};
    const float u0 = 0.25f * static_cast<float>(ci) + uOffset, u1 = u0 + 0.25f;
    const float v0 = 0.25f * static_cast<float>(cj), v1 = v0 + 0.25f;
    const glm::vec2 uvs[4] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    mesh::addQuad(m, corners, glm::normalize(glm::cross(right, up)), uvs, MaterialId::TeslaLabels);
}

/// A cylinder along an arbitrary axis from `a` to `b`.
void cylinderBetween(MeshData& m, const glm::vec3& a, const glm::vec3& b, float radius, int segments, MaterialId mat,
                     bool caps = true) {
    const glm::vec3 d = b - a;
    const float len = glm::length(d);
    if (len < 1e-6f) return;
    const glm::vec3 y = d / len;
    const glm::vec3 ref = std::fabs(y.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 x = glm::normalize(glm::cross(y, ref));
    const glm::vec3 z = glm::cross(x, y);
    glm::mat4 t(1.0f);
    t[0] = glm::vec4(x, 0.0f);
    t[1] = glm::vec4(y, 0.0f);
    t[2] = glm::vec4(z, 0.0f);
    t[3] = glm::vec4(a, 1.0f);
    mesh::addCylinder(m, t, radius, 0.0f, len, segments, mat, caps);
}

// ----- Battery ------------------------------------------------------------------------
MeshData buildBattery(int bars) {
    MeshData m;
    const float hx = kBatHalfX, hz = kBatHalfZ, top = kBatTop;
    box(m, {-hx, 0.004f, -hz}, {hx, top, hz}, MaterialId::DarkPlastic);
    // Grip ribs on the ends and a latch ridge along the top.
    for (float s : {-1.0f, 1.0f}) {
        for (int i = 0; i < 4; ++i) {
            const float y = 0.018f + 0.013f * static_cast<float>(i);
            const float x0 = s > 0.0f ? hx : -hx - 0.003f;
            box(m, {x0, y, -hz + 0.012f}, {x0 + 0.003f, y + 0.005f, hz - 0.012f}, MaterialId::DarkPlastic, mesh::FaceAll);
        }
        for (float z : {-hz + 0.012f, hz - 0.012f}) { // rubber feet
            box(m, {s * (hx - 0.012f) - 0.008f, 0.0f, z - 0.008f}, {s * (hx - 0.012f) + 0.008f, 0.004f, z + 0.008f},
                MaterialId::DarkPlastic, mesh::FaceSides | mesh::FaceNegY);
        }
    }
    box(m, {-hx + 0.01f, top, -0.008f}, {hx - 0.01f, top + 0.004f, 0.008f}, MaterialId::GrayMetal);
    // Terminal studs: positive and negative.
    for (float x : {-0.042f, 0.042f}) {
        mesh::addCylinder(m, glm::translate(glm::mat4(1.0f), glm::vec3(x, top, 0.024f)), 0.009f, 0.0f, 0.004f, 12,
                          MaterialId::DarkPlastic);
        mesh::addCylinder(m, glm::translate(glm::mat4(1.0f), glm::vec3(x, top, 0.024f)), 0.0045f, 0.004f, 0.012f, 10,
                          MaterialId::Copper);
    }
    // Front: the label and, above it, the charge gauge.
    const glm::vec3 right(1, 0, 0), up(0, 1, 0);
    label(m, {0.0f, 0.033f, hz + 0.0006f}, right, up, 0.06f, 0.0225f, 0, 3);
    box(m, {-0.034f, 0.0585f, hz}, {0.034f, 0.0705f, hz + 0.001f}, MaterialId::DarkPlastic, mesh::FaceAll & ~mesh::FaceNegZ);
    for (int i = 0; i < 4; ++i) {
        const float x = -0.024f + 0.016f * static_cast<float>(i);
        const bool lit = i < bars && bars >= 2;
        const bool warn = bars == 1 && i == 0;
        label(m, {x, 0.0645f, hz + 0.0016f}, right, up, 0.006f, 0.004f, 3, warn ? 2 : 3, warn ? 4.0f : lit ? 2.0f : 0.0f);
    }
    return m;
}

// ----- Driver box + primary -------------------------------------------------------------
MeshData buildDriver() {
    MeshData m;
    const float hx = kDrvHalfX;
    box(m, {-hx, 0.0f, kDrvBack}, {hx, kDrvTop, kDrvFront}, MaterialId::GrayMetal);
    // Folded lid seam and corner screws.
    box(m, {-hx - 0.001f, kDrvTop - 0.008f, kDrvBack + 0.004f}, {hx + 0.001f, kDrvTop - 0.006f, kDrvFront - 0.004f},
        MaterialId::DarkPlastic, mesh::FaceSides);
    for (float x : {-hx + 0.008f, hx - 0.008f}) {
        for (float z : {kDrvBack + 0.008f, kDrvFront - 0.008f}) {
            mesh::addCylinder(m, glm::translate(glm::mat4(1.0f), glm::vec3(x, kDrvTop, z)), 0.003f, 0.0f, 0.0012f, 8,
                              MaterialId::DarkPlastic);
        }
    }
    // -X side: cooling vents. +X side: the panel (it faces the player in the gun).
    for (int i = 0; i < 6; ++i) {
        const float z = kDrvBack + 0.03f + 0.022f * static_cast<float>(i);
        box(m, {-hx - 0.0012f, 0.015f, z}, {-hx, 0.060f, z + 0.01f}, MaterialId::DarkPlastic, mesh::FaceAll & ~mesh::FacePosX);
    }
    label(m, {hx + 0.0006f, 0.038f, -0.03f}, glm::vec3(0, 0, -1), glm::vec3(0, 1, 0), 0.04f, 0.025f, 2, 3);
    // Top: the warning sticker, a toggle switch and a status lamp.
    label(m, {0.0f, kDrvTop + 0.0006f, -0.02f}, glm::vec3(1, 0, 0), glm::vec3(0, 0, -1), 0.03f, 0.015f, 1, 3);
    mesh::addCylinder(m, glm::translate(glm::mat4(1.0f), glm::vec3(0.02f, kDrvTop, -0.085f)), 0.008f, 0.0f, 0.006f, 12,
                      MaterialId::DarkPlastic);
    cylinderBetween(m, {0.02f, kDrvTop + 0.006f, -0.085f}, {0.02f, kDrvTop + 0.024f, -0.078f}, 0.0022f, 8, MaterialId::GrayMetal);
    label(m, {-0.022f, kDrvTop + 0.0008f, -0.085f}, glm::vec3(1, 0, 0), glm::vec3(0, 0, -1), 0.006f, 0.004f, 3, 3, 2.0f);
    // A cable gland in the back plate.
    cylinderBetween(m, {0.0f, 0.03f, kDrvBack}, {0.0f, 0.03f, kDrvBack - 0.006f}, 0.007f, 10, MaterialId::DarkPlastic);

    // Front: a mounting plate, four comb standoffs and the primary helix.
    const glm::vec3 axis(0.0f, kAxisY, 0.0f);
    cylinderBetween(m, {0.0f, kAxisY, kDrvFront}, {0.0f, kAxisY, kDrvFront + 0.008f}, 0.036f, 20, MaterialId::DarkPlastic);
    const float length = kPrimaryTurns * kPrimaryPitch;
    for (int k = 0; k < 4; ++k) {
        const float a = kPi * 0.25f + kPi * 0.5f * static_cast<float>(k);
        const glm::vec3 radial(std::cos(a), std::sin(a), 0.0f);
        const glm::vec3 p0 = axis + radial * (kPrimaryRadius + 0.0045f) + glm::vec3(0.0f, 0.0f, kDrvFront + 0.008f);
        cylinderBetween(m, p0, p0 + glm::vec3(0.0f, 0.0f, length + 0.01f), 0.0028f, 6, MaterialId::DarkPlastic);
    }
    const int segments = static_cast<int>(kPrimaryTurns * 20.0f);
    glm::vec3 prev(0.0f);
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float a = t * kPrimaryTurns * 2.0f * kPi;
        const glm::vec3 p = axis + glm::vec3(std::cos(a) * kPrimaryRadius, std::sin(a) * kPrimaryRadius, kPrimaryStart + t * length);
        if (i > 0) mesh::addLimb(m, prev, p, kPrimaryTube, kPrimaryTube, MaterialId::Copper, 8, 2);
        prev = p;
    }
    // Tap lead from the box into the first turn.
    mesh::addLimb(m, axis + glm::vec3(kPrimaryRadius, 0.0f, kPrimaryStart), glm::vec3(kPrimaryRadius, kAxisY - 0.012f, kDrvFront),
                  0.0025f, 0.0025f, MaterialId::Copper, 8, 2);
    return m;
}

// ----- Coil ---------------------------------------------------------------------------------
MeshData buildCoil() {
    MeshData m;
    const glm::mat4 I(1.0f);
    mesh::addCylinder(m, I, kCoilCap, 0.0f, 0.012f, 20, MaterialId::DarkPlastic);
    mesh::addCylinder(m, I, kCoilRadius, 0.012f, 0.232f, 24, MaterialId::Copper, false);
    mesh::addCylinder(m, I, kCoilRadius + 0.0003f, 0.232f, 0.242f, 24, MaterialId::BeigePlastic, false);
    mesh::addCylinder(m, I, kCoilCap, 0.242f, 0.250f, 20, MaterialId::DarkPlastic);
    mesh::addCylinder(m, I, 0.004f, 0.250f, kCoilTop, 8, MaterialId::GrayMetal);
    // The bottom lead of the winding, taped down the former.
    box(m, {kCoilRadius - 0.0005f, 0.012f, -0.002f}, {kCoilRadius + 0.0012f, 0.05f, 0.002f}, MaterialId::Copper);
    return m;
}

// ----- Top load -------------------------------------------------------------------------
MeshData buildTopLoad() {
    MeshData m;
    const glm::mat4 I(1.0f);
    mesh::addTorus(m, glm::translate(I, glm::vec3(0.0f, kTorusr, 0.0f)), kTorusR, kTorusr, 36, 16, MaterialId::Aluminum);
    mesh::addCylinder(m, I, kTorusR - kTorusr + 0.004f, 0.012f, kSpikeBase, 28, MaterialId::Aluminum);
    mesh::addCylinder(m, I, 0.006f, 0.0f, 0.012f, 10, MaterialId::GrayMetal); // socket onto the coil's terminal
    // The breakout spike: a collar and a long, needle-sharp cone.
    mesh::addCylinder(m, I, 0.009f, kSpikeBase, kSpikeBase + 0.006f, 12, MaterialId::GrayMetal);
    mesh::addLimb(m, {0.0f, kSpikeBase + 0.004f, 0.0f}, {0.0f, kSpikeTip, 0.0f}, 0.0055f, 0.0006f, MaterialId::Aluminum, 10, 2);
    return m;
}

// ----- Frame (gun space) -------------------------------------------------------------------
MeshData buildFrame() {
    MeshData m;
    // Pistol grip under the back of the driver box, raked back.
    const glm::mat4 grip = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -0.078f)), glm::radians(-16.0f),
                                       glm::vec3(1, 0, 0));
    box(m, {-0.017f, -0.115f, -0.024f}, {0.017f, 0.0f, 0.024f}, MaterialId::DarkPlastic, mesh::FaceAll, grip);
    for (int i = 0; i < 4; ++i) { // finger grooves
        const float y = -0.095f + 0.022f * static_cast<float>(i);
        box(m, {-0.015f, y, 0.024f}, {0.015f, y + 0.008f, 0.028f}, MaterialId::DarkPlastic, mesh::FaceAll, grip);
    }
    box(m, {-0.019f, -0.122f, -0.028f}, {0.019f, -0.112f, 0.028f}, MaterialId::DarkPlastic, mesh::FaceAll, grip); // butt
    // Trigger and guard.
    box(m, {-0.004f, -0.034f, -0.046f}, {0.004f, -0.004f, -0.040f}, MaterialId::GrayMetal);
    box(m, {-0.006f, -0.050f, -0.060f}, {0.006f, -0.045f, -0.005f}, MaterialId::DarkPlastic);
    box(m, {-0.006f, -0.050f, -0.010f}, {0.006f, 0.0f, -0.005f}, MaterialId::DarkPlastic);
    // Rail under the coil and two cradles holding it.
    box(m, {-0.009f, 0.004f, kDrvFront}, {0.009f, 0.010f, kMountFront + 0.23f}, MaterialId::GrayMetal);
    for (float z : {kMountFront + 0.09f, kMountFront + 0.20f}) {
        box(m, {-0.004f, 0.010f, z - 0.004f}, {0.004f, kAxisY - kCoilCap + 0.002f, z + 0.004f}, MaterialId::GrayMetal);
        box(m, {-0.024f, kAxisY - kCoilCap - 0.002f, z - 0.004f}, {0.024f, kAxisY - kCoilCap + 0.002f, z + 0.004f},
            MaterialId::GrayMetal);
    }
    // The ground strap: a braided lead from the rail back to the box.
    mesh::addLimb(m, {0.0f, 0.007f, kDrvFront + 0.01f}, {0.012f, 0.004f, kDrvFront - 0.02f}, 0.0025f, 0.0025f,
                  MaterialId::Copper, 6, 2);
    return m;
}

} // namespace

namespace tesla {

MeshData buildMesh(PartMesh mesh) {
    switch (mesh) {
    case PartMesh::Battery0:
    case PartMesh::Battery1:
    case PartMesh::Battery2:
    case PartMesh::Battery3:
    case PartMesh::Battery4:      return buildBattery(static_cast<int>(mesh) - static_cast<int>(PartMesh::Battery0));
    case PartMesh::Driver:        return buildDriver();
    case PartMesh::Coil:          return buildCoil();
    case PartMesh::TopLoad:       return buildTopLoad();
    case PartMesh::Frame:         return buildFrame();
    default:                      return {};
    }
}

PartMesh meshFor(const Item& item) {
    switch (item.type) {
    case PartType::Battery:
        return static_cast<PartMesh>(static_cast<int>(PartMesh::Battery0) + batteryBars(item.charge));
    case PartType::Driver:        return PartMesh::Driver;
    case PartType::Coil:          return PartMesh::Coil;
    default:                      return PartMesh::TopLoad;
    }
}

glm::mat4 mountTransform(PartType type) {
    const glm::mat4 I(1.0f);
    const glm::vec3 x(1, 0, 0), y(0, 1, 0);
    switch (type) {
    case PartType::Battery:
        // Under the front of the box; its label and gauge face +X (towards the player).
        return glm::rotate(glm::translate(I, glm::vec3(0.0f, -kBatTop - 0.002f, 0.07f)), glm::radians(90.0f), y);
    case PartType::Driver:
        return I;
    case PartType::Coil:
        return glm::rotate(glm::translate(I, glm::vec3(0.0f, kAxisY, kMountFront)), glm::radians(90.0f), x);
    default:
        return glm::rotate(glm::translate(I, glm::vec3(0.0f, kAxisY, kTopLoadZ)), glm::radians(90.0f), x);
    }
}

glm::mat4 restTransform(PartType type, SiteKind site, float spin) {
    const glm::mat4 I(1.0f);
    const glm::vec3 y(0, 1, 0), z(0, 0, 1);
    if (site == SiteKind::Drawer) {
        // Laid in lengthwise across the drawer (drawer space x), in front of the folders.
        switch (type) {
        case PartType::Battery:
            return glm::rotate(I, spin * 0.1f, y);
        case PartType::Driver: // box along x, the primary pointing to the side
            return glm::translate(glm::rotate(I, glm::radians(90.0f) + spin * 0.1f, y), glm::vec3(0.0f, 0.0f, -0.02f));
        case PartType::Coil: // on its side
            return glm::rotate(glm::translate(I, glm::vec3(0.125f, kCoilCap, 0.0f)), glm::radians(90.0f), z);
        default:
            return glm::rotate(I, spin, y);
        }
    }
    if (type == PartType::Coil && site == SiteKind::Chair) {
        return glm::rotate(glm::rotate(I, spin, y) * glm::translate(I, glm::vec3(0.12f, kCoilCap, 0.0f)), glm::radians(90.0f), z);
    }
    return glm::rotate(I, spin, y);
}

glm::vec3 spikeTipLocal() { return {0.0f, kSpikeTip, 0.0f}; }

glm::vec3 spikeTipGun() { return glm::vec3(mountTransform(PartType::TopLoad) * glm::vec4(spikeTipLocal(), 1.0f)); }

glm::vec3 centerLocal(PartType type) {
    switch (type) {
    case PartType::Battery:       return {0.0f, 0.04f, 0.0f};
    case PartType::Driver:        return {0.0f, 0.04f, 0.0f};
    case PartType::Coil:          return {0.0f, 0.13f, 0.0f};
    default:                      return {0.0f, 0.03f, 0.0f};
    }
}

} // namespace tesla
