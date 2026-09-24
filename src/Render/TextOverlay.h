#pragma once
// ---------------------------------------------------------------------------
// TextOverlay.h
// Minimal screen-space text for HUD readouts (the FPS meter). Uses a tiny
// built-in 5x7 pixel font: every lit font pixel becomes a solid quad, drawn
// at an integer scale so the text stays crisp at any resolution. Geometry is
// rebuilt only when the text or the back-buffer size changes.
// ---------------------------------------------------------------------------

#include "Render/Shader.h"

#include <string>

class TextOverlay {
public:
    TextOverlay() = default;
    ~TextOverlay();
    TextOverlay(const TextOverlay&) = delete;
    TextOverlay& operator=(const TextOverlay&) = delete;

    bool init();

    /// Draws `text` right-aligned in the top-right corner of the default
    /// framebuffer (drop-shadowed, on a translucent backing box).
    void drawTopRight(const std::string& text, int width, int height);

private:
    void rebuild(const std::string& text, int width, int height);

    Shader      m_shader;
    GLuint      m_vao = 0;
    GLuint      m_vbo = 0;
    GLsizei     m_vertexCount = 0;
    std::string m_cachedText;
    int         m_cachedWidth = 0;
    int         m_cachedHeight = 0;
};
