// ---------------------------------------------------------------------------
// OfficeFurniture.cpp
// Procedural modelling of the office's furniture, plants and desk clutter.
// All dimensions are in metres; local +Z is the front, +Y is up.
// ---------------------------------------------------------------------------
#include "Actors/OfficeFurniture.h"

#include "Math/Random.h"
#include "Render/MeshBuilder.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace officefurniture {
namespace {

constexpr float kPi = 3.14159265f;

void box(MeshData& m, const glm::vec3& mn, const glm::vec3& mx, MaterialId mat, uint8_t faces = mesh::FaceAll,
         const glm::mat4& transform = glm::mat4(1.0f)) {
    mesh::BoxDesc d;
    d.min = mn;
    d.max = mx;
    d.material = mat;
    d.faces = faces;
    d.transform = transform;
    mesh::addBox(m, d);
}

/// A five-star swivel base with casters and a gas lift up to `liftTop`.
void swivelBase(MeshData& m, float liftTop) {
    for (int i = 0; i < 5; ++i) {
        const glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(18.0f + 72.0f * static_cast<float>(i)), glm::vec3(0, 1, 0));
        box(m, {0.03f, 0.055f, -0.022f}, {0.34f, 0.09f, 0.022f}, MaterialId::GrayMetal, mesh::FaceAll, rot);
        mesh::addCylinder(m, glm::translate(rot, glm::vec3(0.32f, 0.0f, 0.0f)), 0.028f, 0.0f, 0.058f, 10, MaterialId::DarkPlastic);
    }
    mesh::addCylinder(m, glm::mat4(1.0f), 0.055f, 0.05f, 0.12f, 16, MaterialId::GrayMetal);
    mesh::addCylinder(m, glm::mat4(1.0f), 0.024f, 0.12f, liftTop, 12, MaterialId::GrayMetal);
}

/// A two-sided leaf quad: `base` at the stem, growing along `along` (its
/// length), `side` across (half its width); `uRange` picks the atlas half.
void leaf(MeshData& m, const glm::vec3& base, const glm::vec3& along, const glm::vec3& side, float u0, float u1) {
    const glm::vec3 n = glm::normalize(glm::cross(side, along));
    const glm::vec3 front[4] = {base - side, base + side, base + side + along, base - side + along};
    const glm::vec2 uv[4] = {{u0, 0.0f}, {u1, 0.0f}, {u1, 1.0f}, {u0, 1.0f}};
    mesh::addQuad(m, front, n, uv, MaterialId::Foliage);
    const glm::vec3 back[4] = {base + side, base - side, base - side + along, base + side + along};
    const glm::vec2 uvBack[4] = {{u1, 0.0f}, {u0, 0.0f}, {u0, 1.0f}, {u1, 1.0f}};
    mesh::addQuad(m, back, -n, uvBack, MaterialId::Foliage);
}

/// A round planter with soil showing at its rim.
void planter(MeshData& m, float radius, float height) {
    mesh::addCylinder(m, glm::mat4(1.0f), radius, 0.0f, height, 20, MaterialId::DarkPlastic);
    mesh::addTorus(m, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, height, 0.0f)), radius - 0.01f, 0.014f, 20, 6,
                   MaterialId::DarkPlastic);
    mesh::addCylinder(m, glm::mat4(1.0f), radius - 0.02f, height - 0.03f, height + 0.004f, 20, MaterialId::Concrete);
}

// ----- Executive desk ------------------------------------------------------------------
// 1.9 x 0.9 m, 0.76 m high: a thick laminate top on two drawer pedestals with
// a full modesty panel facing the visitors (-Z); drawers face the occupant (+Z).
MeshData buildExecDesk() {
    MeshData m;
    const float hw = 0.95f, hd = 0.45f, top = 0.76f, topT = 0.05f;
    box(m, {-hw, top - topT, -hd}, {hw, top, hd}, MaterialId::WoodLaminate);
    for (float s : {-1.0f, 1.0f}) {
        const float x0 = s < 0 ? -hw + 0.02f : hw - 0.46f, x1 = x0 + 0.44f;
        box(m, {x0, 0.0f, -0.42f}, {x1, top - topT, 0.42f}, MaterialId::WoodLaminate, mesh::FaceAll & ~mesh::FacePosY);
        for (int d = 0; d < 3; ++d) {
            const float y0 = 0.05f + 0.22f * static_cast<float>(d), y1 = y0 + 0.2f;
            box(m, {x0 + 0.02f, y0, 0.42f}, {x1 - 0.02f, y1, 0.435f}, MaterialId::WoodLaminate, mesh::FaceAll & ~mesh::FaceNegZ);
            box(m, {x0 + 0.15f, y1 - 0.05f, 0.435f}, {x1 - 0.15f, y1 - 0.035f, 0.455f}, MaterialId::GrayMetal, mesh::FaceAll & ~mesh::FaceNegZ);
        }
    }
    box(m, {-hw + 0.46f, 0.18f, -0.43f}, {hw - 0.46f, top - topT, -0.40f}, MaterialId::WoodLaminate); // modesty panel
    return m;
}

// ----- Executive chair -------------------------------------------------------------------
// High-backed, thickly padded black leather, padded arms, a headrest.
MeshData buildExecChair() {
    MeshData m;
    swivelBase(m, 0.42f);
    box(m, {-0.24f, 0.41f, -0.24f}, {0.24f, 0.44f, 0.24f}, MaterialId::GrayMetal);
    box(m, {-0.28f, 0.44f, -0.26f}, {0.28f, 0.55f, 0.28f}, MaterialId::DarkPlastic);   // seat cushion
    box(m, {-0.04f, 0.42f, -0.33f}, {0.04f, 0.62f, -0.28f}, MaterialId::GrayMetal);    // spine
    box(m, {-0.27f, 0.58f, -0.37f}, {0.27f, 1.22f, -0.27f}, MaterialId::DarkPlastic);  // back
    box(m, {-0.22f, 1.10f, -0.36f}, {0.22f, 1.30f, -0.28f}, MaterialId::DarkPlastic);  // headrest
    for (float s : {-1.0f, 1.0f}) {
        const float x0 = s > 0 ? 0.27f : -0.32f;
        box(m, {x0, 0.50f, -0.02f}, {x0 + 0.05f, 0.68f, 0.03f}, MaterialId::GrayMetal);
        box(m, {x0 - 0.01f, 0.68f, -0.20f}, {x0 + 0.06f, 0.72f, 0.20f}, MaterialId::DarkPlastic);
    }
    return m;
}

// ----- Conference table ---------------------------------------------------------------------
// 3.6 x 1.2 m laminate top on two steel pedestals joined by a cable spine.
MeshData buildConferenceTable() {
    MeshData m;
    box(m, {-1.8f, 0.71f, -0.6f}, {1.8f, 0.76f, 0.6f}, MaterialId::WoodLaminate);
    for (float x : {-1.1f, 1.1f}) {
        box(m, {x - 0.08f, 0.02f, -0.30f}, {x + 0.08f, 0.71f, 0.30f}, MaterialId::GrayMetal, mesh::FaceSides);
        box(m, {x - 0.20f, 0.0f, -0.45f}, {x + 0.20f, 0.03f, 0.45f}, MaterialId::GrayMetal);
    }
    box(m, {-1.0f, 0.55f, -0.06f}, {1.0f, 0.68f, 0.06f}, MaterialId::GrayMetal);
    return m;
}

// ----- Breakroom table ------------------------------------------------------------------------
MeshData buildBreakTable() {
    MeshData m;
    mesh::addCylinder(m, glm::mat4(1.0f), 0.45f, 0.71f, 0.74f, 28, MaterialId::WoodLaminate);
    mesh::addCylinder(m, glm::mat4(1.0f), 0.04f, 0.03f, 0.71f, 12, MaterialId::GrayMetal, false);
    for (int i = 0; i < 2; ++i) {
        const glm::mat4 rot = glm::rotate(glm::mat4(1.0f), kPi * 0.5f * static_cast<float>(i), glm::vec3(0, 1, 0));
        box(m, {-0.30f, 0.0f, -0.03f}, {0.30f, 0.03f, 0.03f}, MaterialId::GrayMetal, mesh::FaceAll, rot);
    }
    return m;
}

// ----- Ficus --------------------------------------------------------------------------------------
// A weeping fig in a black planter: a slender trunk forking into a few
// branches, each ending in a cloud of glossy leaves that droop at the edges.
MeshData buildFicus() {
    MeshData m;
    planter(m, 0.21f, 0.36f);
    rnd::Rng rng(0xF1C05ull);
    mesh::addCylinder(m, glm::mat4(1.0f), 0.022f, 0.34f, 1.05f, 8, MaterialId::WoodLaminate, false);
    const glm::vec3 clusters[5] = {{0.0f, 1.55f, 0.0f}, {0.2f, 1.25f, 0.08f}, {-0.18f, 1.32f, -0.1f}, {0.06f, 1.0f, -0.2f}, {-0.1f, 0.95f, 0.18f}};
    for (const glm::vec3& c : clusters) {
        // A branch from the trunk to the cluster.
        const glm::vec3 from(0.0f, 0.9f + 0.1f * c.y, 0.0f);
        mesh::addLimb(m, from, c, 0.012f, 0.008f, MaterialId::WoodLaminate, 6, 1);
        for (int i = 0; i < 46; ++i) {
            // Leaves scattered over a squashed sphere, hanging outward and down.
            const float a = rng.range(0.0f, 2.0f * kPi), z = rng.range(-0.8f, 1.0f);
            const float r = std::sqrt(1.0f - z * z);
            const glm::vec3 dir(r * std::cos(a), z * 0.7f, r * std::sin(a));
            const glm::vec3 base = c + dir * rng.range(0.12f, 0.27f);
            glm::vec3 grow = glm::normalize(dir + glm::vec3(rng.range(-0.3f, 0.3f), -0.6f, rng.range(-0.3f, 0.3f)));
            glm::vec3 side = glm::normalize(glm::cross(grow, glm::vec3(rng.range(-1.0f, 1.0f), 1.0f, rng.range(-1.0f, 1.0f))));
            const float len = rng.range(0.07f, 0.11f);
            leaf(m, base, grow * len, side * (len * 0.24f), 0.0f, 0.5f);
        }
    }
    return m;
}

// ----- Fern -----------------------------------------------------------------------------------------
// Fronds arching out of a wide, low planter.
MeshData buildFern() {
    MeshData m;
    planter(m, 0.24f, 0.32f);
    rnd::Rng rng(0xFE2Bull);
    const int fronds = 16;
    for (int f = 0; f < fronds; ++f) {
        const float a = 2.0f * kPi * (static_cast<float>(f) + rng.range(-0.3f, 0.3f)) / static_cast<float>(fronds);
        const glm::vec3 out(std::cos(a), 0.0f, std::sin(a));
        const glm::vec3 side = glm::vec3(-out.z, 0.0f, out.x) * 0.075f;
        const float rise = rng.range(0.9f, 1.4f), length = rng.range(0.45f, 0.65f);
        // Four segments, each bending further down: a frond arching under its own weight.
        glm::vec3 p(0.0f, 0.33f, 0.0f);
        const int segments = 4;
        for (int s = 0; s < segments; ++s) {
            const float t = static_cast<float>(s) / segments;
            const float pitch = rise * (1.0f - 1.6f * t);
            const glm::vec3 dir = glm::normalize(out + glm::vec3(0.0f, pitch, 0.0f)) * (length / segments);
            // Each segment shows its slice of the frond texture (v from stem to tip).
            const glm::vec3 n = glm::normalize(glm::cross(side, dir));
            const glm::vec3 q = p + dir;
            const float v0 = t, v1 = t + 1.0f / segments;
            const glm::vec3 front[4] = {p - side, p + side, q + side, q - side};
            const glm::vec2 uv[4] = {{0.5f, v0}, {1.0f, v0}, {1.0f, v1}, {0.5f, v1}};
            mesh::addQuad(m, front, n, uv, MaterialId::Foliage);
            const glm::vec3 back[4] = {p + side, p - side, q - side, q + side};
            const glm::vec2 uvBack[4] = {{1.0f, v0}, {0.5f, v0}, {0.5f, v1}, {1.0f, v1}};
            mesh::addQuad(m, back, -n, uvBack, MaterialId::Foliage);
            p = q;
        }
    }
    return m;
}

// ----- Water cooler -----------------------------------------------------------------------------------
// A white cabinet with hot and cold taps over a drip tray, its big blue bottle upturned on top.
MeshData buildWaterCooler() {
    MeshData m;
    box(m, {-0.16f, 0.0f, -0.16f}, {0.16f, 0.95f, 0.16f}, MaterialId::BeigePlastic);
    box(m, {-0.12f, 0.55f, 0.16f}, {0.12f, 0.80f, 0.18f}, MaterialId::DarkPlastic, mesh::FaceAll & ~mesh::FaceNegZ); // tap recess
    for (float x : {-0.05f, 0.05f}) box(m, {x - 0.015f, 0.70f, 0.18f}, {x + 0.015f, 0.74f, 0.21f}, MaterialId::GrayMetal);
    box(m, {-0.11f, 0.56f, 0.16f}, {0.11f, 0.58f, 0.24f}, MaterialId::GrayMetal); // drip tray
    mesh::addCylinder(m, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.95f, 0.0f)), 0.035f, 0.0f, 0.04f, 12, MaterialId::WaterBottle);
    mesh::addCylinder(m, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.95f, 0.0f)), 0.135f, 0.05f, 0.44f, 24, MaterialId::WaterBottle);
    return m;
}

// ----- Trash can -------------------------------------------------------------------------------------------
MeshData buildTrashCan() {
    MeshData m;
    mesh::addCylinder(m, glm::mat4(1.0f), 0.17f, 0.0f, 0.42f, 20, MaterialId::GrayMetal);
    mesh::addCylinder(m, glm::mat4(1.0f), 0.18f, 0.42f, 0.47f, 20, MaterialId::DarkPlastic);
    box(m, {-0.08f, 0.47f, -0.02f}, {0.08f, 0.49f, 0.02f}, MaterialId::DarkPlastic); // the swing flap's lip
    return m;
}

// ----- Paper stack ------------------------------------------------------------------------------------------
// Three stacks of different heights, none of them square.
MeshData buildPaperStack() {
    MeshData m;
    const float heights[3] = {0.045f, 0.018f, 0.03f};
    const float angles[3] = {0.05f, -0.09f, 0.16f};
    float y = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const glm::mat4 t = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.01f * static_cast<float>(i), y, -0.008f * static_cast<float>(i))),
                                        angles[i], glm::vec3(0, 1, 0));
        box(m, {-0.105f, 0.0f, -0.148f}, {0.105f, heights[i], 0.148f}, MaterialId::OfficePaper, mesh::FaceAll & ~mesh::FaceNegY, t);
        y += heights[i];
    }
    return m;
}

// ----- Mug ----------------------------------------------------------------------------------------------------
// A cream mug with a handle, cold coffee inside.
MeshData buildMug() {
    MeshData m;
    mesh::addCylinder(m, glm::mat4(1.0f), 0.042f, 0.0f, 0.10f, 16, MaterialId::BeigePlastic, false);
    mesh::addCylinder(m, glm::mat4(1.0f), 0.038f, 0.0f, 0.085f, 16, MaterialId::DarkPlastic);
    mesh::addTorus(m, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.10f, 0.0f)), 0.040f, 0.0035f, 16, 4, MaterialId::BeigePlastic);
    const glm::mat4 handle = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.045f, 0.052f, 0.0f)), kPi * 0.5f, glm::vec3(1, 0, 0));
    mesh::addTorus(m, handle, 0.026f, 0.0065f, 12, 6, MaterialId::BeigePlastic);
    return m;
}

// ----- Document tray -----------------------------------------------------------------------------------------
// Two black trays on posts, the lower one with papers in it.
MeshData buildDocTray() {
    MeshData m;
    for (float y : {0.0f, 0.085f}) {
        box(m, {-0.13f, y, -0.17f}, {0.13f, y + 0.008f, 0.17f}, MaterialId::DarkPlastic);
        box(m, {-0.13f, y, -0.17f}, {-0.122f, y + 0.05f, 0.17f}, MaterialId::DarkPlastic);
        box(m, {0.122f, y, -0.17f}, {0.13f, y + 0.05f, 0.17f}, MaterialId::DarkPlastic);
        box(m, {-0.13f, y, -0.17f}, {0.13f, y + 0.05f, -0.162f}, MaterialId::DarkPlastic);
    }
    for (float x : {-0.12f, 0.12f}) {
        for (float z : {-0.16f, 0.16f}) box(m, {x - 0.005f, 0.008f, z - 0.005f}, {x + 0.005f, 0.085f, z + 0.005f}, MaterialId::GrayMetal);
    }
    box(m, {-0.105f, 0.008f, -0.148f}, {0.105f, 0.03f, 0.148f}, MaterialId::OfficePaper);
    return m;
}

} // namespace

MeshData buildMesh(FurnitureType type) {
    switch (type) {
    case FurnitureType::ExecDesk:        return buildExecDesk();
    case FurnitureType::ExecChair:       return buildExecChair();
    case FurnitureType::ConferenceTable: return buildConferenceTable();
    case FurnitureType::BreakTable:      return buildBreakTable();
    case FurnitureType::Ficus:           return buildFicus();
    case FurnitureType::Fern:            return buildFern();
    case FurnitureType::WaterCooler:     return buildWaterCooler();
    case FurnitureType::TrashCan:        return buildTrashCan();
    case FurnitureType::PaperStack:      return buildPaperStack();
    case FurnitureType::Mug:             return buildMug();
    case FurnitureType::DocTray:         return buildDocTray();
    default:                             return {};
    }
}

std::vector<AABB> colliders(FurnitureType type) {
    switch (type) {
    case FurnitureType::ExecDesk:        return {AABB({-0.95f, 0.0f, -0.45f}, {0.95f, 0.76f, 0.45f})};
    case FurnitureType::ExecChair:       return {AABB({-0.33f, 0.0f, -0.33f}, {0.33f, 0.55f, 0.33f}),
                                                 AABB({-0.27f, 0.55f, -0.38f}, {0.27f, 1.30f, -0.27f})};
    case FurnitureType::ConferenceTable: return {AABB({-1.8f, 0.0f, -0.6f}, {1.8f, 0.76f, 0.6f})};
    case FurnitureType::BreakTable:      return {AABB({-0.45f, 0.0f, -0.45f}, {0.45f, 0.74f, 0.45f})};
    case FurnitureType::Ficus:           return {AABB({-0.21f, 0.0f, -0.21f}, {0.21f, 1.6f, 0.21f})};
    case FurnitureType::Fern:            return {AABB({-0.24f, 0.0f, -0.24f}, {0.24f, 0.6f, 0.24f})};
    case FurnitureType::WaterCooler:     return {AABB({-0.16f, 0.0f, -0.16f}, {0.16f, 1.39f, 0.21f})};
    case FurnitureType::TrashCan:        return {AABB({-0.18f, 0.0f, -0.18f}, {0.18f, 0.49f, 0.18f})};
    default:                             return {};
    }
}

glm::vec2 footprint(FurnitureType type) {
    switch (type) {
    case FurnitureType::ExecDesk:        return {0.95f, 0.45f};
    case FurnitureType::ExecChair:       return {0.33f, 0.38f};
    case FurnitureType::ConferenceTable: return {1.8f, 0.6f};
    case FurnitureType::BreakTable:      return {0.45f, 0.45f};
    case FurnitureType::Ficus:           return {0.35f, 0.35f};
    case FurnitureType::Fern:            return {0.6f, 0.6f};
    case FurnitureType::WaterCooler:     return {0.16f, 0.21f};
    case FurnitureType::TrashCan:        return {0.18f, 0.18f};
    case FurnitureType::PaperStack:      return {0.11f, 0.15f};
    case FurnitureType::Mug:             return {0.07f, 0.05f};
    case FurnitureType::DocTray:         return {0.13f, 0.17f};
    default:                             return {0.5f, 0.5f};
    }
}

} // namespace officefurniture
