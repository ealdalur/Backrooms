#pragma once
// ---------------------------------------------------------------------------
// AtlasLayout.h
// Layout of the two atlas materials shared by the texture synthesis
// (MaterialLibrary) and the geometry that maps onto them (World/Decals,
// the office's signs and posters).
//
// Writing (MaterialId::Writing): one cell per printable ASCII character,
// 16 x 8 cells (v up: row 0 at the bottom). Each cell holds three distance
// fields of its glyph, measured in font pixels of the built-in 5x7 font:
//   surface.r  distance to the glyph's pen strokes (lit pixels joined into
//              a slightly wobbly skeleton: handwriting),
//   surface.g  distance to drips running down from the strokes' lower ends
//              (spray paint and blood),
//   surface.b  distance to the glyph's pixel squares (crisp print).
// The world shader turns the fields into ink: the stroke width, softness,
// colour and texture depend on the ink, which a quad carries in its u
// coordinate (u + kInkStride * ink; the texture repeats, so the offset never
// changes what is sampled).
//
// Signage (MaterialId::Signage): 4 x 4 cells of sign and poster backings.
// ---------------------------------------------------------------------------

#include <glm/glm.hpp>

namespace atlas {

// ---- Writing --------------------------------------------------------------------
inline constexpr int   kGlyphCols   = 16;
inline constexpr int   kGlyphRows   = 8;
inline constexpr float kCellW       = 6.4f;  ///< Cell width in font pixels.
inline constexpr float kCellH       = 12.8f; ///< Cell height in font pixels.
inline constexpr float kGlyphX      = 0.7f;  ///< Left edge of the 5-pixel glyph in its cell.
inline constexpr float kBaseY       = 5.3f;  ///< Bottom edge of the 7-pixel glyph (drips hang below it).
inline constexpr float kAdvance     = 6.0f;  ///< Pen advance per character (font pixels).
inline constexpr float kMaxDistance = 2.0f;  ///< Distances are stored as d / kMaxDistance.
inline constexpr float kInkStride   = 2.0f;  ///< u offset per ink.

/// Ink a glyph quad is drawn in (the world shader's MAT_WRITING styles).
enum class Ink : int {
    Marker = 0,   ///< Black felt-tip.
    Pen,          ///< Blue ballpoint.
    Pencil,       ///< Grainy graphite.
    SprayRed,     ///< Red spray paint: soft overspray, drips.
    SprayBlack,
    Blood,        ///< Smeared, dark, dripping.
    Print,        ///< Crisp near-black print (notices, posters).
    PrintWhite,
    PrintGlowRed, ///< Glowing red sign lettering (EXIT).
    DryEraseBlue, ///< Whiteboard marker.
    DryEraseRed,
    PrintRed,
    Count
};

/// UV rectangle (u0, v0, u1, v1) of a character's cell; v grows upward.
inline glm::vec4 glyphCell(char c) {
    int i = static_cast<unsigned char>(c) - 32;
    if (i < 0 || i >= kGlyphCols * kGlyphRows) i = 0;
    const float u0 = static_cast<float>(i % kGlyphCols) / kGlyphCols;
    const float v0 = static_cast<float>(i / kGlyphCols) / kGlyphRows;
    return {u0, v0, u0 + 1.0f / kGlyphCols, v0 + 1.0f / kGlyphRows};
}

// ---- Signage --------------------------------------------------------------------
enum class Sign : int {
    // Top row (cj = 3).
    Paper = 0,      ///< White notice paper with a thin printed border.
    SafetyYellow,   ///< Yellow safety notice with a black header band.
    PosterMountain, ///< Motivational poster: sunset over mountains, black caption band.
    PosterSea,      ///< Motivational poster: a breaking wave, black caption band.
    // Second row (cj = 2).
    Whiteboard,     ///< Glossy white, ghosts of erased marker.
    PlacardDark,    ///< Charcoal name / room plate.
    PlacardBlue,    ///< Corporate blue plate.
    ExitFace,       ///< Near-black exit sign face (the lettering glows).
    // Third row (cj = 1).
    StickyNote,     ///< Yellow sticky note.
    PosterBlue,     ///< Blue gradient poster (team spirit), black frame.
    Brass,          ///< Brushed brass plate.
    Chart,          ///< Gridded paper with a bar chart that only goes up.
    Count
};

/// UV rectangle (u0, v0, u1, v1) of a signage cell, inset by `inset` (cell fraction).
inline glm::vec4 signCell(Sign s, float inset = 0.004f) {
    const int i = static_cast<int>(s);
    const int ci = i % 4, cj = 3 - i / 4;
    const float u0 = static_cast<float>(ci) * 0.25f, v0 = static_cast<float>(cj) * 0.25f;
    const float d = inset * 0.25f;
    return {u0 + d, v0 + d, u0 + 0.25f - d, v0 + 0.25f - d};
}

} // namespace atlas
