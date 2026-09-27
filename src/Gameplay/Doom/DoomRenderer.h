#pragma once
// ---------------------------------------------------------------------------
// DoomRenderer.h
// The software renderer of the terminal Doom clone. Everything is drawn in
// 8-bit palette indices into a 320x200 frame (VGA mode 13h), exactly one
// byte per pixel, then converted to RGBA once per frame for the terminal's
// CRT (see TerminalGraphics in Render/TerminalRenderer.h).
//
// The 3D view (320x168 above the status bar) is a Wolfenstein-style grid
// raycaster with Doom's touches:
//   * Floors and ceilings: every row is cast at its own distance, every pixel
//     looks up the cell under it for its flat, its light level and whether it
//     is open to the (wrapping, full-bright) sky.
//   * Walls: one DDA ray per column. A door rises into the ceiling; a ray
//     hitting a partly open door draws the slab and carries on underneath it,
//     so what lies beyond is seen through the gap (per-column clip window).
//   * Light: the sector's level and the distance pick one of 32 colormaps,
//     with "fake contrast" (east-west walls a shade lighter than north-south
//     ones) and the muzzle flash lighting everything up for a moment.
//   * Sprites: billboards sorted far to near, clipped per column against the
//     wall depth and the underside of any door slab in front of them.
// Screen-space helpers draw the weapon, status bar, text and the melt wipe.
// ---------------------------------------------------------------------------

#include "Gameplay/Doom/DoomAssets.h"
#include "Gameplay/Doom/DoomLevel.h"

#include <glm/glm.hpp>
#include <array>
#include <string>
#include <vector>

struct TerminalGraphics;

namespace doom {

/// A sprite to draw in the 3D view.
struct ViewSprite {
    glm::vec2 pos;
    float     z = 0.0f;  ///< Height of its bottom above the floor.
    Spr       sprite;
};

/// Where the 3D view is drawn from.
struct ViewParams {
    glm::vec2 pos;
    float     angle = 0.0f;
    float     eyeZ = 0.41f;   ///< Eye height (the wall is 1 unit tall).
    int       tic = 0;
    int       extraLight = 0; ///< Muzzle flash brightening.
};

class SoftwareRenderer {
public:
    SoftwareRenderer();

    void drawView(const Level& level, const ViewParams& view, std::vector<ViewSprite>& sprites);

    /// Blits a picture (transparent pixels skipped) through colormap `light`.
    void drawImage(const Image& img, int x, int y, int light = 0, int clipBottom = kScreenH);
    /// Covers a rectangle with a repeating flat (backgrounds).
    void tileFlat(const Image& flat, int x0, int y0, int x1, int y1, int light);
    /// Text in the 5x7 font, with a drop shadow. Returns the width drawn.
    int drawText(int x, int y, const std::string& text, uint8_t color, int scale = 1);
    static int textWidth(const std::string& text, int scale = 1) { return static_cast<int>(text.size()) * 6 * scale - scale; }
    /// Big red status-bar number, right-aligned to `right`.
    void drawBigNumber(int right, int y, int value, bool percent);

    /// Slides `from` down the screen column by column (Doom's melt wipe).
    void meltOver(const std::vector<uint8_t>& from, const std::array<int, kScreenW>& offsets);

    /// Converts the frame to RGBA; `red` / `yellow` tint for pain and pickups.
    void present(TerminalGraphics& out, float red, float yellow) const;

    const std::vector<uint8_t>& frame() const { return m_fb; }

private:
    void castFlats(const Level& level, const ViewParams& view);
    void castWalls(const Level& level, const ViewParams& view);
    void drawSprites(const ViewParams& view, std::vector<ViewSprite>& sprites);
    int  lightIndex(int light, float distance, int extra) const;

    const DoomAssets& m_assets;
    std::vector<uint8_t> m_fb;
    std::vector<int>     m_cellLight;  ///< This frame's light level per cell.
    int                  m_levelW = 0;
    std::array<float, kScreenW> m_depth{};       ///< Distance to the wall drawn in each column.
    std::array<float, kScreenW> m_doorDepth{};   ///< Nearest see-through door slab in each column...
    std::array<float, kScreenW> m_doorBottom{};  ///< ...and the screen row of its lower edge.
};

} // namespace doom
