// ---------------------------------------------------------------------------
// TextOverlay.cpp
// ---------------------------------------------------------------------------
#include "Render/TextOverlay.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

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

constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;

/// 5x7 glyphs, one byte per row, bit 4 = leftmost column.
struct Glyph {
    char    c;
    uint8_t rows[kGlyphH];
};
const Glyph kFont[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'m', {0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11}},
    {'s', {0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
};

/// Glyph bitmap for a character; unknown characters (and space) are blank.
const uint8_t* glyphRows(char c) {
    for (const Glyph& g : kFont) {
        if (g.c == c) return g.rows;
    }
    return nullptr;
}

struct OverlayVertex {
    float x, y;
    float r, g, b, a;
};

void pushRect(std::vector<OverlayVertex>& v, float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    const OverlayVertex q[6] = {{x0, y0, r, g, b, a}, {x1, y0, r, g, b, a}, {x1, y1, r, g, b, a},
                                {x0, y0, r, g, b, a}, {x1, y1, r, g, b, a}, {x0, y1, r, g, b, a}};
    v.insert(v.end(), q, q + 6);
}

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
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void TextOverlay::rebuild(const std::string& text, int width, int height) {
    // Integer pixel scale: 1x at 1080p, 2x at 2160p, never below 1x (the
    // smallest size at which the 5x7 font stays crisp).
    const float s = static_cast<float>(std::max(1, static_cast<int>(std::lround(height / 1080.0))));
    const float advance = (kGlyphW + 1) * s;
    const float margin = 10.0f * s;
    const float pad = 3.0f * s;

    const float textW = static_cast<float>(text.size()) * advance - s;
    const float x0 = static_cast<float>(width) - margin - textW;
    const float y0 = margin;

    std::vector<OverlayVertex> verts;
    verts.reserve(text.size() * kGlyphW * kGlyphH * 12 + 6);
    pushRect(verts, x0 - pad, y0 - pad, x0 + textW + pad, y0 + kGlyphH * s + pad, 0.0f, 0.0f, 0.0f, 0.45f);

    // Two passes: 1-pixel drop shadow, then the text itself.
    for (int pass = 0; pass < 2; ++pass) {
        const float off = pass == 0 ? s : 0.0f;
        const float shade = pass == 0 ? 0.0f : 1.0f;
        for (size_t i = 0; i < text.size(); ++i) {
            const uint8_t* rows = glyphRows(text[i]);
            if (!rows) continue;
            const float gx = x0 + static_cast<float>(i) * advance + off;
            for (int row = 0; row < kGlyphH; ++row) {
                for (int col = 0; col < kGlyphW; ++col) {
                    if (!(rows[row] & (0x10 >> col))) continue;
                    const float px = gx + static_cast<float>(col) * s;
                    const float py = y0 + static_cast<float>(row) * s + off;
                    pushRect(verts, px, py, px + s, py + s, shade, shade, shade * 0.92f, 0.9f);
                }
            }
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(OverlayVertex)), verts.data(),
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_vertexCount = static_cast<GLsizei>(verts.size());
    m_cachedText = text;
    m_cachedWidth = width;
    m_cachedHeight = height;
}

void TextOverlay::drawTopRight(const std::string& text, int width, int height) {
    if (!m_vao || text.empty()) return;
    if (text != m_cachedText || width != m_cachedWidth || height != m_cachedHeight) rebuild(text, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shader.use();
    m_shader.set("uScreen", glm::vec2(static_cast<float>(width), static_cast<float>(height)));
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}
