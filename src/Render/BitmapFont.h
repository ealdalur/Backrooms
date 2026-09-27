#pragma once
// ---------------------------------------------------------------------------
// BitmapFont.h
// Built-in 5x7 pixel font covering printable ASCII (32..126), plus code 127
// as a solid block (the terminal cursor). Shared by the HUD text overlay and
// the terminal's CRT console. Rows are stored top to bottom, one byte each,
// bit 4 = leftmost column.
// ---------------------------------------------------------------------------

#include <cstdint>

namespace font {

inline constexpr int  kGlyphW    = 5;
inline constexpr int  kGlyphH    = 7;
inline constexpr char kFirstChar = 32;
inline constexpr char kLastChar  = 127;
inline constexpr int  kGlyphCount = kLastChar - kFirstChar + 1;
inline constexpr char kBlock     = 127;

/// Row bitmap of a character, or nullptr if the font has no such glyph.
const uint8_t* glyph(char c);

} // namespace font
