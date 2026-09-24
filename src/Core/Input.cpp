// ---------------------------------------------------------------------------
// Input.cpp
// ---------------------------------------------------------------------------
#include "Core/Input.h"

void Input::beginFrame() {
    m_pressed.fill(false);
    m_buttonsPressed = 0;
    m_mouseDelta = glm::vec2(0.0f);
}

void Input::reset() {
    m_keys.fill(false);
    m_pressed.fill(false);
    m_buttons = 0;
    m_buttonsPressed = 0;
    m_mouseDelta = glm::vec2(0.0f);
}

void Input::handleEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
        if (e.key.scancode < SDL_SCANCODE_COUNT) {
            if (!e.key.repeat) m_pressed[static_cast<size_t>(e.key.scancode)] = true;
            m_keys[static_cast<size_t>(e.key.scancode)] = true;
        }
        break;
    case SDL_EVENT_KEY_UP:
        if (e.key.scancode < SDL_SCANCODE_COUNT) m_keys[static_cast<size_t>(e.key.scancode)] = false;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        m_mouseDelta += glm::vec2(e.motion.xrel, e.motion.yrel);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button < 32) {
            m_buttons |= 1u << e.button.button;
            m_buttonsPressed |= 1u << e.button.button;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button < 32) m_buttons &= ~(1u << e.button.button);
        break;
    default:
        break;
    }
}
