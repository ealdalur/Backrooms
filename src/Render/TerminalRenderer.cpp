// ---------------------------------------------------------------------------
// TerminalRenderer.cpp
// ---------------------------------------------------------------------------
#include "Render/TerminalRenderer.h"

#include "Render/BitmapFont.h"
#include "Render/ShaderSources.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kAtlasCols = 16; ///< Glyph cells per atlas row (8x8 texels each).
constexpr int kAtlasRows = (font::kGlyphCount + kAtlasCols - 1) / kAtlasCols;
constexpr int kAtlasW = kAtlasCols * 8;
constexpr int kAtlasH = kAtlasRows * 8;

const char* const kGlyphVertex = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;    // console pixels, origin top-left
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uTarget;
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = vec4(aPos.x / uTarget.x * 2.0 - 1.0, 1.0 - aPos.y / uTarget.y * 2.0, 0.0, 1.0);
}
)GLSL";

const char* const kGlyphFragment = R"GLSL(
#version 330 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uFont;
out vec4 oColor;
void main() {
    float ink = texture(uFont, vUV).r;
    oColor = vec4(vColor.rgb * ink, vColor.a * ink);
}
)GLSL";

const char* const kCrtFragment = R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 oColor;

uniform sampler2D uConsole;
uniform vec2  uConsoleSize;
uniform vec2  uResolution;
uniform float uTime;
uniform float uOpen;
uniform float uGlitch;
uniform float uBrightness;
uniform vec3  uPhosphor;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float hash11(float x) { return hash12(vec2(x, 17.31)); }

float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    // Position in units of the viewport height, centred.
    vec2  p     = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;
    float open  = uOpen * uOpen * (3.0 - 2.0 * uOpen);
    vec2  half  = vec2(0.62, 0.39) * mix(0.55, 1.0, open);  // tube half-size (16:10 raster)
    vec2  q     = p / half;
    vec2  qd    = q * (1.0 + 0.045 * dot(q, q));            // bulging tube face
    float tube  = sdRoundBox(qd, vec2(1.0), 0.12);           // < 0 on the glass
    float bezel = sdRoundBox(q, vec2(1.13, 1.20), 0.18);     // < 0 on the monitor

    if (bezel > 0.0) {
        // The room around the monitor, darkened (but still there).
        oColor = vec4(0.0, 0.0, 0.0, 0.55 * open);
        return;
    }
    if (tube > 0.0) {
        // Beige plastic bezel, lit from above, with a dark lip at the glass.
        vec3 beige = vec3(0.60, 0.56, 0.46) * (0.72 + 0.28 * (q.y * 0.5 + 0.5));
        beige *= mix(0.3, 1.0, smoothstep(0.0, 0.06, tube));
        beige *= 1.0 - 0.35 * smoothstep(-0.03, 0.0, bezel);
        beige += (hash12(gl_FragCoord.xy) - 0.5) * 0.025;
        oColor = vec4(beige, open);
        return;
    }

    vec2 uv = qd * 0.5 + 0.5;
    // Interference: some horizontal bands tear sideways during glitches.
    float band = floor(uv.y * 30.0) + floor(uTime * 24.0) * 3.1;
    uv.x += uGlitch * (hash11(band) - 0.5) * 0.09 * step(0.55, hash11(band + 7.0));
    uv.y += uGlitch * 0.01 * sin(uTime * 90.0);

    vec2 texel = 1.0 / uConsoleSize;
    vec3 c    = texture(uConsole, uv).rgb;
    vec3 glow = (texture(uConsole, uv + vec2(2.5 * texel.x, 0.0)).rgb + texture(uConsole, uv - vec2(2.5 * texel.x, 0.0)).rgb +
                 texture(uConsole, uv + vec2(0.0, 2.5 * texel.y)).rgb + texture(uConsole, uv - vec2(0.0, 2.5 * texel.y)).rgb) * 0.25;
    vec3 col = c * 1.05 + glow * 0.6 + uPhosphor * 0.03;     // lit raster background

    col *= 0.76 + 0.24 * sin(uv.y * uConsoleSize.y * 3.14159265); // scanlines (one per glyph pixel)
    col *= 0.93 + 0.07 * sin(gl_FragCoord.x * 2.0944);            // aperture grille
    col *= 0.96 + 0.04 * sin(uTime * 113.0);                      // refresh flicker
    col += (hash12(gl_FragCoord.xy + fract(uTime) * 91.7) - 0.5) * (0.03 + 0.12 * uGlitch);
    col *= mix(0.5, 1.0, smoothstep(0.0, -0.10, tube));           // darker toward the tube edge
    col *= uBrightness;
    // Reflection of the ceiling lights in the glass (independent of power).
    col += vec3(0.05, 0.05, 0.045) * smoothstep(0.9, 0.0, length(q - vec2(-0.45, 0.6)));
    oColor = vec4(col, open);
}
)GLSL";

glm::vec3 paletteColor(uint8_t color, bool amber) {
    const glm::vec3 phosphor = amber ? glm::vec3(1.0f, 0.66f, 0.20f) : glm::vec3(0.35f, 1.0f, 0.50f);
    switch (color) {
    case TerminalScreen::Dim:     return phosphor * 0.42f;
    case TerminalScreen::Bright:  return glm::min(phosphor + glm::vec3(0.25f), glm::vec3(1.0f));
    case TerminalScreen::Alert:   return {1.0f, 0.28f, 0.22f};
    case TerminalScreen::Anomaly: return {0.92f, 0.94f, 1.0f};
    case TerminalScreen::Input:   return phosphor;
    case TerminalScreen::Normal:
    default:                      return phosphor * 0.8f;
    }
}

} // namespace

TerminalRenderer::~TerminalRenderer() {
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_emptyVao) glDeleteVertexArrays(1, &m_emptyVao);
}

bool TerminalRenderer::init() {
    if (!m_glyphShader.build(kGlyphVertex, kGlyphFragment, "TerminalGlyphs")) return false;
    if (!m_crtShader.build(shaders::kFullscreenVertex, kCrtFragment, "TerminalCrt")) return false;

    // ---- Font atlas: every glyph in an 8x8 cell, R8 ----------------------------------
    std::vector<uint8_t> atlas(static_cast<size_t>(kAtlasW) * kAtlasH, 0);
    for (int i = 0; i < font::kGlyphCount; ++i) {
        const uint8_t* rows = font::glyph(static_cast<char>(font::kFirstChar + i));
        const int cx = (i % kAtlasCols) * 8, cy = (i / kAtlasCols) * 8;
        for (int r = 0; r < font::kGlyphH; ++r) {
            for (int c = 0; c < font::kGlyphW; ++c) {
                if (rows[r] & (0x10 >> c)) atlas[static_cast<size_t>((cy + r) * kAtlasW + cx + c)] = 255;
            }
        }
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    m_font.create(kAtlasW, kAtlasH, GL_R8, GL_RED, GL_UNSIGNED_BYTE, atlas.data(), GL_NEAREST, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    // ---- Console render target ---------------------------------------------------------
    const int w = TerminalScreen::kCols * kCellW, h = TerminalScreen::kRows * kCellH;
    m_console.create(w, h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, nullptr, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_console.id(), 0);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) return false;

    // ---- Glyph quad stream: (x, y, u, v, r, g, b, a) ---------------------------------------
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const GLsizei stride = 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(4 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenVertexArrays(1, &m_emptyVao);
    return glGetError() == GL_NO_ERROR;
}

void TerminalRenderer::renderConsole(const TerminalScreen& screen, float time, float dt) {
    const float tw = static_cast<float>(TerminalScreen::kCols * kCellW);
    const float th = static_cast<float>(TerminalScreen::kRows * kCellH);

    m_vertices.clear();
    auto quad = [this](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                       const glm::vec4& c) {
        const float q[6][8] = {{x0, y0, u0, v0, c.r, c.g, c.b, c.a}, {x1, y0, u1, v0, c.r, c.g, c.b, c.a},
                               {x1, y1, u1, v1, c.r, c.g, c.b, c.a}, {x0, y0, u0, v0, c.r, c.g, c.b, c.a},
                               {x1, y1, u1, v1, c.r, c.g, c.b, c.a}, {x0, y1, u0, v1, c.r, c.g, c.b, c.a}};
        m_vertices.insert(m_vertices.end(), &q[0][0], &q[0][0] + 48);
    };
    auto glyphUV = [](char ch, float& u0, float& v0, float& u1, float& v1) {
        const int i = static_cast<unsigned char>(ch) - font::kFirstChar;
        const int cx = (i % kAtlasCols) * 8, cy = (i / kAtlasCols) * 8;
        u0 = static_cast<float>(cx) / kAtlasW;
        u1 = static_cast<float>(cx + font::kGlyphW) / kAtlasW;
        v0 = static_cast<float>(cy) / kAtlasH;
        v1 = static_cast<float>(cy + font::kGlyphH) / kAtlasH;
    };

    // Fade the previous frame instead of clearing it: phosphor persistence.
    float bu0, bv0, bu1, bv1;
    glyphUV(font::kBlock, bu0, bv0, bu1, bv1);
    const float cu = (bu0 + bu1) * 0.5f, cv = (bv0 + bv1) * 0.5f; // a solid texel
    const float decay = m_consoleValid ? 1.0f - std::exp(-dt / 0.025f) : 1.0f;
    quad(0.0f, 0.0f, tw, th, cu, cv, cu, cv, glm::vec4(0.0f, 0.0f, 0.0f, decay));
    const size_t fadeVerts = m_vertices.size() / 8;

    const float ox = 1.0f, oy = 3.0f, px = 2.0f; // glyph offset in its cell, pixel size
    for (int row = 0; row < TerminalScreen::kRows; ++row) {
        for (int col = 0; col < TerminalScreen::kCols; ++col) {
            TerminalScreen::Cell cell = screen.at(col, row);
            const bool cursorHere = screen.cursorVisible && col == screen.cursorCol && row == screen.cursorRow &&
                                    std::fmod(time, 1.0f) < 0.55f;
            if (cursorHere) cell = {font::kBlock, TerminalScreen::Input};
            if (cell.ch == ' ' || !font::glyph(cell.ch)) continue;
            float u0, v0, u1, v1;
            glyphUV(cell.ch, u0, v0, u1, v1);
            const float x0 = static_cast<float>(col * kCellW) + ox;
            const float y0 = static_cast<float>(row * kCellH) + oy;
            const glm::vec3 c = paletteColor(cell.color, screen.amber);
            quad(x0, y0, x0 + font::kGlyphW * px, y0 + font::kGlyphH * px, u0, v0, u1, v1, glm::vec4(c, 1.0f));
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, static_cast<GLsizei>(tw), static_cast<GLsizei>(th));
    if (!m_consoleValid) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        m_consoleValid = true;
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vertices.size() * sizeof(float)), m_vertices.data(),
                 GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_glyphShader.use();
    m_glyphShader.set("uTarget", glm::vec2(tw, th));
    m_glyphShader.set("uFont", 0);
    m_font.bind(0);
    glBindVertexArray(m_vao);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // fade quad
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(fadeVerts));
    // Glyphs re-excite the phosphor: MAX keeps steady text at its colour
    // (adding would accumulate it to white) while moving text leaves trails.
    glBlendEquation(GL_MAX);
    glDrawArrays(GL_TRIANGLES, static_cast<GLint>(fadeVerts), static_cast<GLsizei>(m_vertices.size() / 8 - fadeVerts));
    glBlendEquation(GL_FUNC_ADD);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

void TerminalRenderer::draw(const TerminalScreen& screen, float time, float dt, float openAmount, int width,
                            int height) {
    if (!m_vao || openAmount <= 0.0f) return;
    renderConsole(screen, time, dt);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_crtShader.use();
    m_console.bind(0);
    m_crtShader.set("uConsole", 0);
    m_crtShader.set("uConsoleSize", glm::vec2(static_cast<float>(m_console.width()), static_cast<float>(m_console.height())));
    m_crtShader.set("uResolution", glm::vec2(static_cast<float>(width), static_cast<float>(height)));
    m_crtShader.set("uTime", time);
    m_crtShader.set("uOpen", std::clamp(openAmount, 0.0f, 1.0f));
    m_crtShader.set("uGlitch", screen.glitch);
    m_crtShader.set("uBrightness", screen.brightness);
    m_crtShader.set("uPhosphor", screen.amber ? glm::vec3(1.0f, 0.66f, 0.2f) : glm::vec3(0.35f, 1.0f, 0.5f));
    glBindVertexArray(m_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}
