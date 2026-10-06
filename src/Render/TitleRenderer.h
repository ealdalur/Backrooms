#pragma once
// ---------------------------------------------------------------------------
// TitleRenderer.h
// The title screen: "BACKROOMS" as a sign of fluorescent tubes hung over the
// game, which carries on (lights flickering, hum buzzing) blurred behind it.
//
// The lettering is a stroke font defined here - line segments and elliptic
// arcs, the centrelines of bent glass tubes - turned at start-up into a
// distance field (per texel: the distances to the two nearest letters, and
// which letters they are). A full-screen pass then lights it: each tube has
// a hot white core with a round cross-section and a mono-yellow halo, and
// strikes on by itself with a starter's stutter; unlit, the glass shows dark.
// One tube (the second O) is on its way out and keeps dropping. On top: VHS
// tracking slips, a slight colour split, scanlines, an underline tube that
// draws itself out and a typed-in tagline (queued on the HUD text overlay).
// ---------------------------------------------------------------------------

#include "Render/Shader.h"
#include "Render/Texture.h"

class TextOverlay;

class TitleRenderer {
public:
    TitleRenderer() = default;
    ~TitleRenderer();
    TitleRenderer(const TitleRenderer&) = delete;
    TitleRenderer& operator=(const TitleRenderer&) = delete;

    /// Builds the lettering's distance field and the shader.
    bool init();

    /// Draws the title over the default framebuffer and queues its tagline on `hud`.
    /// @param time  Seconds since the title came up (drives the tubes striking on).
    /// @param fade  0..1 as the title fades away.
    void draw(float time, float fade, int width, int height, TextOverlay& hud);

    static constexpr int kLetters = 9; ///< B A C K R O O M S

private:
    Shader    m_shader;
    Texture2D m_field;
    GLuint    m_vao = 0;
    float     m_wordWidth = 1.0f; ///< In cap heights.
};
