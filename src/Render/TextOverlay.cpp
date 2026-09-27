// ---------------------------------------------------------------------------
// TextOverlay.cpp
// ---------------------------------------------------------------------------
#include "Render/TextOverlay.h"

#include "Render/BitmapFont.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

const char* const kOverlayVertex = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;    // pixels, origin top-left
layout(location = 1) in vec4 aColor;
uniform vec2 uScreen;
out vec4 vColor;
void main() {
    vColor = aColor;
    vec2 ndc = aPos / uScreen * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)GLSL";

const char* const kOverlayFragment = R"GLSL(
#version 330 core
in vec4 vColor;
out vec4 oColor;
void main() { oColor = vColor; }
)GLSL";

} // namespace

TextOverlay::~TextOverlay() {
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

bool TextOverlay::init() {
    if (!m_shader.build(kOverlayVertex, kOverlayFragment, "TextOverlay")) return false;
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(2 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void TextOverlay::begin(int width, int height) {
    m_width = std::max(1, width);
    m_height = std::max(1, height);
    m_scale = static_cast<float>(std::max(1, static_cast<int>(std::lround(m_height / 1080.0))));
    m_vertices.clear();
}

void TextOverlay::pushRect(float x0, float y0, float x1, float y1, const glm::vec4& c) {
    const Vertex q[6] = {{x0, y0, c.r, c.g, c.b, c.a}, {x1, y0, c.r, c.g, c.b, c.a}, {x1, y1, c.r, c.g, c.b, c.a},
                         {x0, y0, c.r, c.g, c.b, c.a}, {x1, y1, c.r, c.g, c.b, c.a}, {x0, y1, c.r, c.g, c.b, c.a}};
    m_vertices.insert(m_vertices.end(), q, q + 6);
}

void TextOverlay::text(const std::string& text, float x, float y, Align align, float sizeMul, const glm::vec4& color,
                       bool backing) {
    if (text.empty() || color.a <= 0.0f) return;
    const float s = std::max(1.0f, std::round(m_scale * sizeMul));
    const float advance = (font::kGlyphW + 1) * s;
    const float textW = static_cast<float>(text.size()) * advance - s;
    float x0 = x;
    if (align == Align::Center) x0 = std::round(x - textW * 0.5f);
    else if (align == Align::Right) x0 = x - textW;
    const float y0 = std::round(y);

    if (backing) {
        const float pad = 3.0f * s;
        pushRect(x0 - pad, y0 - pad, x0 + textW + pad, y0 + font::kGlyphH * s + pad, glm::vec4(0.0f, 0.0f, 0.0f, 0.45f * color.a));
    }

    // Two passes: 1-pixel drop shadow, then the text itself.
    for (int pass = 0; pass < 2; ++pass) {
        const float off = pass == 0 ? s : 0.0f;
        const glm::vec4 c = pass == 0 ? glm::vec4(0.0f, 0.0f, 0.0f, color.a * 0.8f) : color;
        for (size_t i = 0; i < text.size(); ++i) {
            const uint8_t* rows = font::glyph(text[i]);
            if (!rows) continue;
            const float gx = x0 + static_cast<float>(i) * advance + off;
            for (int row = 0; row < font::kGlyphH; ++row) {
                for (int col = 0; col < font::kGlyphW; ++col) {
                    if (!(rows[row] & (0x10 >> col))) continue;
                    const float px = gx + static_cast<float>(col) * s;
                    const float py = y0 + static_cast<float>(row) * s + off;
                    pushRect(px, py, px + s, py + s, c);
                }
            }
        }
    }
}

void TextOverlay::flush() {
    if (!m_vao || m_vertices.empty()) return;

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vertices.size() * sizeof(Vertex)), m_vertices.data(),
                 GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shader.use();
    m_shader.set("uScreen", glm::vec2(static_cast<float>(m_width), static_cast<float>(m_height)));
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertices.size()));
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    m_vertices.clear();
}
