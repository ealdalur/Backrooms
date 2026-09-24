#pragma once
// ---------------------------------------------------------------------------
// Input.h
// Per-frame snapshot of keyboard and mouse state fed by SDL3 events, with
// edge detection ("pressed this frame") and accumulated relative motion.
// ---------------------------------------------------------------------------

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>

class Input {
public:
    /// Clears per-frame data (edges and mouse delta). Call before polling events.
    void beginFrame();

    /// Feeds one SDL event into the state.
    void handleEvent(const SDL_Event& e);

    /// Releases everything (e.g. when the window loses focus).
    void reset();

    bool keyDown(SDL_Scancode sc) const { return m_keys[static_cast<size_t>(sc)]; }
    bool keyPressed(SDL_Scancode sc) const { return m_pressed[static_cast<size_t>(sc)]; }

    /// SDL button index (SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, ...).
    bool mouseDown(int button) const { return (m_buttons & (1u << button)) != 0; }
    bool mousePressed(int button) const { return (m_buttonsPressed & (1u << button)) != 0; }

    /// Relative mouse motion (pixels) accumulated over this frame.
    glm::vec2 mouseDelta() const { return m_mouseDelta; }

private:
    std::array<bool, SDL_SCANCODE_COUNT> m_keys{};
    std::array<bool, SDL_SCANCODE_COUNT> m_pressed{};
    uint32_t  m_buttons = 0;
    uint32_t  m_buttonsPressed = 0;
    glm::vec2 m_mouseDelta{0.0f};
};
