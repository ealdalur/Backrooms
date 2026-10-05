#pragma once
// ---------------------------------------------------------------------------
// TerminalRenderer.h
// Draws a terminal console as a CRT monitor over the scene while the player
// uses a computer. Two passes:
//   1. Console pass: glyphs from the built-in 5x7 font are drawn (2x, crisp)
//      into an offscreen texture. The previous frame is faded rather than
//      cleared, which gives the phosphor persistence trails of a real tube.
//   2. CRT pass: a full-screen triangle composites that texture onto a
//      curved (barrel-distorted) tube face with scanlines, an aperture
//      grille, glow, noise, flicker, glitch tearing and a beige bezel. The
//      room stays faintly visible - and dangerous - around the monitor.
// In graphics mode (a program such as DOOM owns the screen) the console pass
// is skipped: the program's framebuffer is uploaded when it changes and the
// CRT pass draws it instead, crisp pixels with one scanline per pixel row.
// ---------------------------------------------------------------------------

#include "Render/Shader.h"
#include "Render/Texture.h"

#include <array>
#include <cstdint>
#include <vector>

/// A console's visible text grid (written by Gameplay/TerminalConsole).
struct TerminalScreen {
    static constexpr int kCols = 64;
    static constexpr int kRows = 24;

    /// Palette entries; the phosphor colour tints all but Alert / Anomaly / Beacon.
    /// Beacon blinks (magenta, unlike anything else on the tube): the MAP's exit room.
    enum Color : uint8_t { Normal = 0, Dim, Bright, Alert, Anomaly, Input, Beacon };

    struct Cell {
        char    ch = ' ';
        uint8_t color = Normal;
    };

    std::array<Cell, kCols * kRows> cells{};
    int   cursorCol = 0;
    int   cursorRow = 0;
    bool  cursorVisible = true;
    bool  amber = false;      ///< Amber (P3) instead of green (P1) phosphor.
    float glitch = 0.0f;      ///< 0..1 tearing / interference strength.
    float brightness = 1.0f;  ///< 0..1 tube brightness (warm-up, power-off collapse).

    Cell& at(int col, int row) { return cells[static_cast<size_t>(row * kCols + col)]; }
    const Cell& at(int col, int row) const { return cells[static_cast<size_t>(row * kCols + col)]; }
};

/// A console's graphics mode: a small colour framebuffer (written by a
/// program running on the terminal, e.g. Gameplay/Doom) shown on the tube
/// in place of the text grid.
struct TerminalGraphics {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba; ///< width * height RGBA8 pixels, rows top to bottom.
    uint32_t version = 0;      ///< Bumped on every new picture (the renderer re-uploads then).
};

class TerminalRenderer {
public:
    TerminalRenderer() = default;
    ~TerminalRenderer();
    TerminalRenderer(const TerminalRenderer&) = delete;
    TerminalRenderer& operator=(const TerminalRenderer&) = delete;

    bool init();

    /// Composites `screen` over the default framebuffer - or `graphics`, if a
    /// program has the terminal in graphics mode (tube effects still come from `screen`).
    /// @param dt          Frame time (phosphor decay).
    /// @param openAmount  0..1 zoom / fade of the monitor into view.
    void draw(const TerminalScreen& screen, const TerminalGraphics* graphics, float time, float dt, float openAmount,
              int width, int height);

    /// Forgets the persistence buffer (call when a new session starts).
    void reset() { m_consoleValid = false; }

private:
    void renderConsole(const TerminalScreen& screen, float time, float dt);

    static constexpr int kCellW = 12; ///< Console pixels per character cell (glyph drawn at 2x).
    static constexpr int kCellH = 20;

    Shader    m_glyphShader;
    Shader    m_crtShader;
    Texture2D m_font;
    Texture2D m_console;
    Texture2D m_graphics;             ///< Graphics-mode picture.
    uint32_t  m_graphicsVersion = 0;  ///< Version of the picture in m_graphics.
    GLuint    m_fbo = 0;
    GLuint    m_vao = 0;
    GLuint    m_vbo = 0;
    GLuint    m_emptyVao = 0;
    bool      m_consoleValid = false;
    std::vector<float> m_vertices;
};
