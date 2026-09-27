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

    /// Palette entries; the phosphor colour tints all but Alert / Anomaly.
    enum Color : uint8_t { Normal = 0, Dim, Bright, Alert, Anomaly, Input };

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

class TerminalRenderer {
public:
    TerminalRenderer() = default;
    ~TerminalRenderer();
    TerminalRenderer(const TerminalRenderer&) = delete;
    TerminalRenderer& operator=(const TerminalRenderer&) = delete;

    bool init();

    /// Composites `screen` over the default framebuffer.
    /// @param dt          Frame time (phosphor decay).
    /// @param openAmount  0..1 zoom / fade of the monitor into view.
    void draw(const TerminalScreen& screen, float time, float dt, float openAmount, int width, int height);

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
    GLuint    m_fbo = 0;
    GLuint    m_vao = 0;
    GLuint    m_vbo = 0;
    GLuint    m_emptyVao = 0;
    bool      m_consoleValid = false;
    std::vector<float> m_vertices;
};
