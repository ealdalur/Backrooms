#pragma once
// ---------------------------------------------------------------------------
// FileCabinet.h
// A three-drawer steel filing cabinet whose drawers really open. The body is
// a hollow shell (one shared instanced mesh); each drawer is a separate
// instanced mesh - a tray with its front, pull and label holder, full of
// hanging manila folders in one of a few arrangements - drawn with a matrix
// that slides it out on its runners.
//
// The player searches a cabinet from a close-up view (see Engine's Cabinet
// state): one drawer at a time rolls open, and whatever part lies in it
// (an ItemSite owned by the chunk, in drawer space) slides out with it.
//
// Local space: origin on the floor at the centre of the footprint, +Z the
// front (the side the drawers pull out of), +Y up. Same footprint and
// collider as the static cabinet it replaces.
// ---------------------------------------------------------------------------

#include "Physics/AABB.h"
#include "Render/Mesh.h"

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <vector>

class FileCabinet {
public:
    static constexpr int kDrawers = 3;
    static constexpr int kDrawerVariants = 3; ///< Folder arrangements.

    /// Event bits reported by takeEvents() (consumed by the soundscape).
    static constexpr uint8_t kEventOpen  = 1 << 0; ///< A drawer started rolling out.
    static constexpr uint8_t kEventClose = 1 << 1; ///< A drawer started rolling shut.

    /// @param id    Deterministic global id (hash of its cell and slot).
    /// @param model Rigid local -> world transform.
    FileCabinet(uint64_t id, const glm::mat4& model);

    /// The hollow body: sides, back, top, plinth and the rails between drawers.
    static MeshData buildShellMesh();
    /// One drawer (drawer space: origin at the bottom centre of its front face
    /// when shut, the tray running back along -Z).
    static MeshData buildDrawerMesh(int variant);
    /// Local-space collision boxes (the shut cabinet).
    static const std::vector<AABB>& localColliders();

    /// Where an item rests inside a drawer (item surface -> drawer space).
    static glm::mat4 itemSurface();

    uint64_t id() const { return m_id; }
    const glm::mat4& modelMatrix() const { return m_model; }
    /// Folder arrangement of a drawer (fixed per cabinet, from its id).
    int drawerVariant(int drawer) const;

    /// Rolls one drawer out and every other one shut (-1: all shut).
    void openDrawer(int drawer);
    int openedDrawer() const { return m_target; }
    /// 0 = shut .. 1 = fully out.
    float drawerOpen(int drawer) const { return m_open[static_cast<size_t>(drawer)]; }
    bool anyOpen() const;

    void update(float dt);
    /// Event bits since the last call.
    uint8_t takeEvents();

    /// Drawer space -> world transform of a drawer at its current opening.
    glm::mat4 drawerMatrix(int drawer) const;

    /// World-space centre of the front face (used to rank interaction targets).
    glm::vec3 frontCenter() const;
    /// Outward normal of the front face.
    glm::vec3 frontNormal() const;
    /// Where the camera sits while the player searches a drawer, and what it looks at.
    glm::vec3 viewPoint(int drawer) const;
    glm::vec3 viewTarget(int drawer) const;

private:
    uint64_t  m_id;
    glm::mat4 m_model;
    std::array<float, kDrawers> m_open{};
    int       m_target = -1;
    uint8_t   m_events = 0;
};
