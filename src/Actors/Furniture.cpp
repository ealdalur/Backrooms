// ---------------------------------------------------------------------------
// Furniture.cpp
// Procedural modelling of desks, chairs, filing cabinets and partitions.
// All dimensions are in metres; local +Z is the front, +Y is up.
// ---------------------------------------------------------------------------
#include "Actors/Furniture.h"

#include "Render/MeshBuilder.h"

#include <glm/gtc/matrix_transform.hpp>
#include <array>

namespace {

using mesh::BoxDesc;

/// Convenience: append an untransformed box.
void box(MeshData& m, const glm::vec3& mn, const glm::vec3& mx, MaterialId mat,
         uint8_t faces = mesh::FaceAll, bool swapUV = false) {
    BoxDesc d;
    d.min = mn;
    d.max = mx;
    d.material = mat;
    d.faces = faces;
    d.swapUV = swapUV;
    mesh::addBox(m, d);
}

// ----- Desk -----------------------------------------------------------------------
// 1.5 x 0.75 x 0.75 m: laminate top, steel modesty panel and left leg panel,
// three-drawer pedestal on the right with drawer fronts and pulls.
MeshData buildDesk() {
    MeshData m;
    const float hw = 0.75f, hd = 0.375f, top = 0.75f, topT = 0.03f;

    box(m, {-hw, top - topT, -hd}, {hw, top, hd}, MaterialId::WoodLaminate);           // top
    box(m, {-0.74f, 0.0f, -0.35f}, {-0.71f, top - topT, 0.35f}, MaterialId::GrayMetal); // left panel
    box(m, {-0.71f, 0.30f, -0.34f}, {0.30f, top - topT - 0.02f, -0.32f}, MaterialId::GrayMetal); // modesty
    box(m, {0.30f, 0.0f, -0.34f}, {0.74f, top - topT, 0.34f}, MaterialId::GrayMetal);   // pedestal

    // Drawer fronts (proud of the pedestal face) with dark plastic pulls.
    const float ranges[3][2] = {{0.05f, 0.28f}, {0.30f, 0.50f}, {0.52f, 0.70f}};
    for (const auto& r : ranges) {
        box(m, {0.315f, r[0], 0.34f}, {0.725f, r[1], 0.352f}, MaterialId::GrayMetal, mesh::FaceAll & ~mesh::FaceNegZ);
        const float pullY = r[1] - 0.045f;
        box(m, {0.47f, pullY - 0.010f, 0.352f}, {0.57f, pullY + 0.010f, 0.372f}, MaterialId::DarkPlastic,
            mesh::FaceAll & ~mesh::FaceNegZ);
    }
    // Rubber floor glides.
    for (float x : {-0.725f, 0.72f}) {
        for (float z : {-0.33f, 0.33f}) {
            box(m, {x - 0.015f, 0.0f, z - 0.015f}, {x + 0.015f, 0.008f, z + 0.015f}, MaterialId::DarkPlastic,
                mesh::FaceSides | mesh::FacePosY);
        }
    }
    return m;
}

// ----- Chair ----------------------------------------------------------------------
// Five-star base with casters, gas lift, fabric seat and backrest, armrests.
MeshData buildChair() {
    MeshData m;
    for (int i = 0; i < 5; ++i) {
        const float a = glm::radians(18.0f + 72.0f * static_cast<float>(i));
        const glm::mat4 rot = glm::rotate(glm::mat4(1.0f), a, glm::vec3(0, 1, 0));
        BoxDesc leg;
        leg.min = {0.03f, 0.055f, -0.02f};
        leg.max = {0.32f, 0.085f, 0.02f};
        leg.material = MaterialId::DarkPlastic;
        leg.transform = rot;
        mesh::addBox(m, leg);
        // Caster wheel at the end of each leg.
        const glm::mat4 caster = glm::translate(rot, glm::vec3(0.30f, 0.0f, 0.0f));
        mesh::addCylinder(m, caster, 0.026f, 0.0f, 0.055f, 10, MaterialId::DarkPlastic);
    }
    mesh::addCylinder(m, glm::mat4(1.0f), 0.05f, 0.05f, 0.11f, 16, MaterialId::DarkPlastic);   // hub
    mesh::addCylinder(m, glm::mat4(1.0f), 0.022f, 0.11f, 0.41f, 12, MaterialId::GrayMetal);    // gas lift

    box(m, {-0.20f, 0.40f, -0.20f}, {0.20f, 0.42f, 0.20f}, MaterialId::DarkPlastic);          // seat pan
    box(m, {-0.245f, 0.42f, -0.23f}, {0.245f, 0.50f, 0.25f}, MaterialId::Fabric);            // cushion

    // Backrest support spine and backrest.
    box(m, {-0.03f, 0.40f, -0.29f}, {0.03f, 0.44f, -0.20f}, MaterialId::DarkPlastic);
    box(m, {-0.03f, 0.44f, -0.30f}, {0.03f, 0.64f, -0.26f}, MaterialId::DarkPlastic);
    box(m, {-0.225f, 0.58f, -0.33f}, {0.225f, 1.02f, -0.26f}, MaterialId::Fabric);

    // Armrests.
    for (float s : {-1.0f, 1.0f}) {
        const float x0 = s > 0 ? 0.245f : -0.265f;
        box(m, {x0, 0.45f, -0.03f}, {x0 + 0.02f, 0.64f, 0.03f}, MaterialId::DarkPlastic);
        const float px0 = s > 0 ? 0.23f : -0.28f;
        box(m, {px0, 0.64f, -0.12f}, {px0 + 0.05f, 0.67f, 0.14f}, MaterialId::DarkPlastic);
    }
    return m;
}

// ----- File cabinet ---------------------------------------------------------------
// 0.46 x 1.02 x 0.62 m steel body with three drawers, pulls and label holders.
MeshData buildCabinet() {
    MeshData m;
    box(m, {-0.23f, 0.0f, -0.31f}, {0.23f, 1.02f, 0.30f}, MaterialId::GrayMetal, mesh::FaceAll & ~mesh::FaceNegY);
    const float bottom = 0.04f, topY = 1.0f, gap = 0.012f;
    const float h = (topY - bottom - 2.0f * gap) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        const float y0 = bottom + static_cast<float>(i) * (h + gap);
        const float y1 = y0 + h;
        box(m, {-0.215f, y0, 0.30f}, {0.215f, y1, 0.312f}, MaterialId::GrayMetal, mesh::FaceAll & ~mesh::FaceNegZ);
        // Recessed-look pull and a small label holder above it.
        box(m, {-0.08f, y1 - 0.085f, 0.312f}, {0.08f, y1 - 0.065f, 0.332f}, MaterialId::DarkPlastic,
            mesh::FaceAll & ~mesh::FaceNegZ);
        box(m, {-0.045f, y1 - 0.055f, 0.312f}, {0.045f, y1 - 0.030f, 0.316f}, MaterialId::GrayMetal,
            mesh::FaceAll & ~mesh::FaceNegZ);
    }
    // Plinth.
    box(m, {-0.22f, 0.0f, -0.30f}, {0.22f, 0.04f, 0.305f}, MaterialId::DarkPlastic, mesh::FaceSides);
    return m;
}

// ----- Partition ------------------------------------------------------------------
// 1.5 m long, 1.4 m tall, 6 cm thick fabric panel in a steel frame (local X = length).
MeshData buildPartition() {
    MeshData m;
    box(m, {-0.72f, 0.04f, -0.025f}, {0.72f, 1.36f, 0.025f}, MaterialId::Fabric, mesh::FacePosZ | mesh::FaceNegZ);
    box(m, {-0.75f, 1.36f, -0.03f}, {0.75f, 1.40f, 0.03f}, MaterialId::GrayMetal);                  // top cap
    box(m, {-0.75f, 0.0f, -0.03f}, {-0.72f, 1.36f, 0.03f}, MaterialId::GrayMetal, mesh::FaceSides); // end posts
    box(m, {0.72f, 0.0f, -0.03f}, {0.75f, 1.36f, 0.03f}, MaterialId::GrayMetal, mesh::FaceSides);
    box(m, {-0.72f, 0.0f, -0.03f}, {0.72f, 0.04f, 0.03f}, MaterialId::DarkPlastic, mesh::FacePosZ | mesh::FaceNegZ);
    return m;
}

/// Local collision boxes per type (conservative, simple shapes).
const std::array<std::vector<AABB>, kFurnitureTypeCount>& colliderTable() {
    static const std::array<std::vector<AABB>, kFurnitureTypeCount> table = {{
        // Desk: one solid block the player can jump onto.
        {AABB({-0.75f, 0.0f, -0.375f}, {0.75f, 0.75f, 0.375f})},
        // Chair: seat block (standable) + backrest.
        {AABB({-0.30f, 0.0f, -0.30f}, {0.30f, 0.50f, 0.30f}),
         AABB({-0.23f, 0.50f, -0.34f}, {0.23f, 1.02f, -0.25f})},
        // File cabinet.
        {AABB({-0.23f, 0.0f, -0.31f}, {0.23f, 1.02f, 0.335f})},
        // Partition.
        {AABB({-0.75f, 0.0f, -0.03f}, {0.75f, 1.40f, 0.03f})},
    }};
    return table;
}

} // namespace

FurnitureInstance Furniture::makeInstance(FurnitureType type, const glm::vec3& position, float yaw) {
    FurnitureInstance inst;
    inst.type = type;
    inst.position = position;
    inst.yaw = yaw;
    inst.model = glm::rotate(glm::translate(glm::mat4(1.0f), position), yaw, glm::vec3(0, 1, 0));
    return inst;
}

MeshData Furniture::buildMesh(FurnitureType type) {
    switch (type) {
    case FurnitureType::Desk:        return buildDesk();
    case FurnitureType::Chair:       return buildChair();
    case FurnitureType::FileCabinet: return buildCabinet();
    case FurnitureType::Partition:   return buildPartition();
    default:                         return {};
    }
}

const std::vector<AABB>& Furniture::localColliders(FurnitureType type) {
    return colliderTable()[static_cast<size_t>(type)];
}

glm::vec2 Furniture::footprintHalfExtents(FurnitureType type) {
    switch (type) {
    case FurnitureType::Desk:        return {0.75f, 0.375f};
    case FurnitureType::Chair:       return {0.33f, 0.33f};
    case FurnitureType::FileCabinet: return {0.23f, 0.335f};
    case FurnitureType::Partition:   return {0.75f, 0.03f};
    default:                         return {0.5f, 0.5f};
    }
}

void Furniture::appendWorldColliders(const FurnitureInstance& instance, std::vector<AABB>& out) {
    for (const AABB& local : localColliders(instance.type)) {
        out.push_back(local.transformed(instance.model));
    }
}
