#pragma once
// ---------------------------------------------------------------------------
// TextOverlay.h
// Minimal screen-space text for HUD readouts (FPS meter, storey indicator,
// interaction prompts, messages). Uses the built-in 5x7 pixel font: every
// lit font pixel becomes a solid quad, drawn at an integer scale so the text
// stays crisp at any resolution. Text is queued during the frame and drawn
// in one batch by flush().
// ---------------------------------------------------------------------------

#include "Render/Shader.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

class TextOverlay {
public:
    enum class Align { Left, Center, Right };

    TextOverlay() = default;
    ~TextOverlay();
    TextOverlay(const TextOverlay&) = delete;
    TextOverlay& operator=(const TextOverlay&) = delete;

    bool init();

    /// Starts a new batch for a back buffer of the given size.
    void begin(int width, int height);

    /// Integer pixel scale for the current back buffer: 1x at 1080p, 2x at
    /// 2160p, never below 1x (the smallest size at which the font stays crisp).
    float pixelScale() const { return m_scale; }

    /// Queues `text` with its top edge at `y` pixels (origin top-left);
    /// `x` is the left edge, centre or right edge depending on `align`.
    /// `sizeMul` multiplies the pixel scale. With `backing`, a translucent
    /// box is drawn behind it. Drop-shadowed.
    void text(const std::string& text, float x, float y, Align align, float sizeMul, const glm::vec4& color,
              bool backing = false);

    /// Draws everything queued since begin() into the default framebuffer.
    void flush();

private:
    struct Vertex {
        float x, y;
        float r, g, b, a;
    };
    void pushRect(float x0, float y0, float x1, float y1, const glm::vec4& c);

    Shader              m_shader;
    GLuint              m_vao = 0;
    GLuint              m_vbo = 0;
    std::vector<Vertex> m_vertices;
    int                 m_width = 1;
    int                 m_height = 1;
    float               m_scale = 1.0f;
};
