// ---------------------------------------------------------------------------
// Door.cpp
// ---------------------------------------------------------------------------
#include "Actors/Door.h"

#include "Core/Config.h"
#include "Render/MeshBuilder.h"
#include "World/WorldConstants.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kOpenAngle = 1.57079633f; ///< 90 degrees: open panel lies flat against its stop.
constexpr float kPanelX0   = 0.004f; ///< Hinge-side gap between jamb and panel.
constexpr float kHandleY   = 1.0f;   ///< Lever height above the floor.
constexpr float kContactSlop   = 0.01f; ///< Touching the player is not "blocking" the door.

/// Smooth ease-in-out (cubic) for a natural swing: accelerates, then settles.
inline float easeInOut(float t) {
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}

/// Door-local AABB of the panel slab.
AABB localPanel() {
    const float d = world::kDoorThickness * 0.5f;
    return {glm::vec3(0.0f, world::kDoorFloorGap, -d),
            glm::vec3(kPanelX0 + world::kDoorPanelWidth, world::kDoorFloorGap + world::kDoorPanelHeight, d)};
}
} // namespace

Door::Door(uint64_t id, const glm::vec3& hinge, const glm::vec3& closedDir)
    : m_id(id),
      m_hinge(hinge),
      m_axis(glm::normalize(closedDir)),
      m_normal(glm::normalize(glm::cross(m_axis, glm::vec3(0.0f, 1.0f, 0.0f)))) {}

MeshData Door::buildMesh() {
    using namespace mesh;
    MeshData m;
    const float W  = world::kDoorPanelWidth;
    const float H  = world::kDoorPanelHeight;
    const float y0 = world::kDoorFloorGap;
    const float d  = world::kDoorThickness * 0.5f;

    // Panel: wood laminate with vertical grain (swapped UVs).
    BoxDesc panel;
    panel.min = glm::vec3(kPanelX0, y0, -d);
    panel.max = glm::vec3(kPanelX0 + W, y0 + H, d);
    panel.material = MaterialId::WoodLaminate;
    panel.swapUV = true;
    addBox(m, panel);

    const float handleX = kPanelX0 + W - 0.065f;
    for (int side = -1; side <= 1; side += 2) {
        const float s = static_cast<float>(side);
        const float face = d * s;

        // Stainless kick plate near the floor.
        BoxDesc kick;
        kick.min = glm::vec3(kPanelX0 + 0.03f, y0 + 0.02f, side > 0 ? face : face - 0.0015f);
        kick.max = glm::vec3(kPanelX0 + W - 0.03f, y0 + 0.22f, side > 0 ? face + 0.0015f : face);
        kick.material = MaterialId::GrayMetal;
        kick.faces = static_cast<uint8_t>(FaceAll & ~(side > 0 ? FaceNegZ : FacePosZ));
        addBox(m, kick);

        // Lever handle: round rose, short stem, lever bar pointing to the hinge.
        const glm::mat4 toFace =
            glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(handleX, kHandleY, face)),
                        glm::radians(90.0f) * s, glm::vec3(1.0f, 0.0f, 0.0f));
        addCylinder(m, toFace, 0.028f, 0.0f, 0.010f, 20, MaterialId::GrayMetal);
        addCylinder(m, toFace, 0.009f, 0.010f, 0.050f, 12, MaterialId::GrayMetal, false);

        BoxDesc lever;
        const float zNear = face + s * 0.043f;
        const float zFar  = face + s * 0.061f;
        lever.min = glm::vec3(handleX - 0.125f, kHandleY - 0.011f, std::min(zNear, zFar));
        lever.max = glm::vec3(handleX + 0.012f, kHandleY + 0.011f, std::max(zNear, zFar));
        lever.material = MaterialId::GrayMetal;
        addBox(m, lever);
    }

    // Three hinge knuckles on the hinge edge.
    for (float hy : {0.25f, 1.05f, 1.85f}) {
        const glm::mat4 knuckle = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, hy - 0.05f, 0.0f));
        addCylinder(m, knuckle, 0.009f, 0.0f, 0.10f, 10, MaterialId::GrayMetal);
    }
    return m;
}

void Door::toggle(const glm::vec3& playerPos) {
    if (m_progress <= 0.0f && !m_opening) {
        // Swing away from the player: pick the side the player is NOT on.
        const glm::vec3 closedCenter = m_hinge + m_axis * (world::kDoorPanelWidth * 0.5f);
        const float facing = glm::dot(m_normal, closedCenter - playerPos);
        m_side = facing >= 0.0f ? 1 : -1;
        m_opening = true;
        m_events |= kEventUnlatch | kEventSwing;
    } else {
        m_opening = !m_opening; // reverse direction (also mid-swing)
        m_events |= kEventSwing;
    }
}

void Door::update(float dt, const AABB& playerBox) {
    const float target = m_opening ? 1.0f : 0.0f;
    if (m_progress == target) return;

    const float step = dt / cfg::kDoorSwingDuration;
    const float next = m_opening ? std::min(1.0f, m_progress + step) : std::max(0.0f, m_progress - step);

    // Hold still only if the panel would genuinely hit the player at its next
    // pose. A door swinging away from the player moves out of their space, so
    // standing close to it (or following it through the frame) never blocks
    // it. Resting contact is ignored via a small slop, and if the panel
    // somehow already overlaps the player it is allowed to move on.
    const AABB body = playerBox.expanded(-kContactSlop);
    auto hitsPlayer = [this, &body](float progress) {
        AABB boxes[kMaxColliders];
        const int n = collidersAt(angleAt(progress), boxes);
        for (int i = 0; i < n; ++i) {
            if (boxes[i].intersects(body)) return true;
        }
        return false;
    };
    if (hitsPlayer(next) && !hitsPlayer(m_progress)) return;

    m_progress = next;
    if (m_progress <= 0.0f) {
        m_side = 0;
        m_events |= kEventShut;
    }
}

float Door::angleAt(float progress) const {
    return static_cast<float>(m_side) * kOpenAngle * easeInOut(progress);
}

glm::mat4 Door::modelFor(float a) const {
    // Rotate the closed direction towards the swing normal by angle a.
    const glm::vec3 x = m_axis * std::cos(a) + m_normal * std::sin(a);
    const glm::vec3 y(0.0f, 1.0f, 0.0f);
    const glm::vec3 z = glm::cross(x, y);
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(x, 0.0f);
    m[1] = glm::vec4(y, 0.0f);
    m[2] = glm::vec4(z, 0.0f);
    m[3] = glm::vec4(m_hinge, 1.0f);
    return m;
}

glm::mat4 Door::modelMatrix() const { return modelFor(angle()); }

AABB Door::bounds() const { return localPanel().transformed(modelMatrix()); }

int Door::collidersAt(float a, AABB* out) const {
    const glm::mat4 m = modelFor(a);
    const AABB panel = localPanel();

    // Closed or fully open: the panel is axis-aligned, one box is exact.
    if (std::fabs(std::sin(a) * std::cos(a)) < 1e-3f) {
        out[0] = panel.transformed(m);
        return 1;
    }

    // Mid-swing: a single AABB of the diagonal panel would be a large
    // invisible square. Slice the panel along its width instead, so the
    // boxes hug the actual slab.
    const float x0 = panel.min.x;
    const float len = panel.max.x - panel.min.x;
    for (int i = 0; i < kMaxColliders; ++i) {
        AABB slice = panel;
        slice.min.x = x0 + len * static_cast<float>(i) / static_cast<float>(kMaxColliders);
        slice.max.x = x0 + len * static_cast<float>(i + 1) / static_cast<float>(kMaxColliders);
        out[i] = slice.transformed(m);
    }
    return kMaxColliders;
}

void Door::appendColliders(std::vector<AABB>& out) const {
    AABB boxes[kMaxColliders];
    const int n = collidersAt(angle(), boxes);
    out.insert(out.end(), boxes, boxes + n);
}

glm::vec3 Door::center() const {
    return glm::vec3(modelMatrix() * glm::vec4(localPanel().center(), 1.0f));
}

Door::State Door::state() const {
    if (m_opening) return m_progress >= 1.0f ? State::Open : State::Opening;
    return m_progress <= 0.0f ? State::Closed : State::Closing;
}

int Door::persistentState() const { return m_opening ? m_side : 0; }

void Door::restoreState(int side) {
    if (side == 0) {
        m_opening = false;
        m_progress = 0.0f;
        m_side = 0;
    } else {
        m_opening = true;
        m_progress = 1.0f;
        m_side = side > 0 ? 1 : -1;
    }
}
