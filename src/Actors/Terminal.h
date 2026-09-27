#pragma once
// ---------------------------------------------------------------------------
// Terminal.h
// A retro office computer (desktop unit, CRT monitor, keyboard) standing on
// a desk. Procedurally modelled like the furniture; rendered with one shared
// instanced mesh per screen look. The in-world screen shows live scrolling
// text drawn by the world shader; the interactive console the player types
// into lives in Gameplay/TerminalConsole.
//
// Local space: origin on the desk top at the centre of the unit, +Z is the
// front (the side the screen faces), +Y up.
// ---------------------------------------------------------------------------

#include "Physics/AABB.h"
#include "Render/Mesh.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

/// How a terminal's screen is drawn.
enum class TerminalLook : uint8_t {
    Off = 0,  ///< Powered down: dead black glass.
    Green,    ///< P1 green phosphor.
    Amber,    ///< P3 amber phosphor.
    Count
};

inline constexpr int kTerminalLookCount = static_cast<int>(TerminalLook::Count);

class Terminal {
public:
    /// @param id      Deterministic global id (hash of its cell and desk).
    /// @param model   Rigid local -> world transform.
    /// @param powered Initial power state (the generator powers most on).
    Terminal(uint64_t id, const glm::mat4& model, bool powered);

    /// Shared geometry for a screen look. The amber variant offsets its screen
    /// UVs by +2 in u; the world shader keys the phosphor colour off that.
    static MeshData buildMesh(TerminalLook look);

    /// Local-space collision boxes (unit + monitor, keyboard).
    static const std::vector<AABB>& localColliders();

    uint64_t id() const { return m_id; }
    const glm::mat4& modelMatrix() const { return m_model; }
    bool powered() const { return m_powered; }
    void setPowered(bool on) { m_powered = on; }

    /// Phosphor colour, fixed per terminal (derived from its id).
    bool amber() const { return (m_id >> 17) % 3u == 0u; }
    TerminalLook look() const { return !m_powered ? TerminalLook::Off : amber() ? TerminalLook::Amber : TerminalLook::Green; }

    /// World-space centre of the glass and its outward normal.
    glm::vec3 screenCenter() const;
    glm::vec3 screenNormal() const;

    /// Where the camera sits while the player uses the terminal.
    glm::vec3 viewPoint() const;

    /// World-space centre of the unit (used to rank interaction targets).
    glm::vec3 center() const;

private:
    uint64_t  m_id;
    glm::mat4 m_model;
    bool      m_powered;
};
