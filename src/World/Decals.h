#pragma once
// ---------------------------------------------------------------------------
// Decals.h
// Words on surfaces, as geometry: every character becomes one small quad
// mapped onto its cell of the Writing atlas (Render/AtlasLayout.h), lying a
// few millimetres in front of the wall it is written on. The ink - felt-tip,
// ballpoint, pencil, spray paint, blood, crisp print, glowing sign lettering,
// dry-erase marker - travels in the quad's u coordinate; the world shader
// turns the glyph's distance fields into strokes of that ink (drawn with
// alpha-to-coverage, so the wall shows between them).
//
// Handwriting is never quite straight: each character gets its own small
// tilt, size and baseline wobble from a seed, and a block of text can lean.
//
// Also here: the Backrooms' scrawl - what someone wrote on a wall, in what
// and how large (mostly despair and gibberish; very rarely, a phone number
// and nothing else) - and plain sign panels mapped onto a Signage atlas cell.
// ---------------------------------------------------------------------------

#include "Math/Random.h"
#include "Render/AtlasLayout.h"
#include "Render/MaterialTypes.h"
#include "Render/Mesh.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace decals {

using atlas::Ink;

enum class Align : uint8_t { Left, Center, Right };

/// How a block of text is set.
struct TextLayout {
    float charHeight = 0.1f;   ///< Height of a capital (7 font pixels), metres.
    Align align = Align::Left;
    float lineSpacing = 1.7f;  ///< Baseline to baseline, in character heights.
    float wobble = 0.0f;       ///< 0 = print; 1 = an ordinary hand; more = shakier.
    float slant = 0.0f;        ///< Tilt of the whole block (radians, counter-clockwise).
};

/// Width (metres) of one line of text at `charHeight`.
float lineWidth(const std::string& line, float charHeight);

/// Splits `text` at '\n' and word-wraps each paragraph to `maxChars` columns.
std::vector<std::string> wrap(const std::string& text, int maxChars);

/// The largest character height (up to `maxHeight`) at which `lines` fit in `width` x `height`.
float fitHeight(const std::vector<std::string>& lines, float width, float height, float maxHeight, float lineSpacing = 1.7f);

/// Appends `lines`. `anchor` lies on the first line's baseline - at its left
/// end, centre or right end depending on the alignment; `right` and `up`
/// (unit, perpendicular) span the surface, whose front faces cross(right, up).
void addText(MeshData& mesh, const std::vector<std::string>& lines, const glm::vec3& anchor, const glm::vec3& right,
             const glm::vec3& up, Ink ink, const TextLayout& layout, uint64_t seed);

/// A flat panel (`size` = width, height; `depth` thick, its back at `back`
/// centred) whose front face is mapped onto a Signage cell; the rim is
/// `rim`. With `twoSided` the back face shows the cell too (hanging signs).
void addPanel(MeshData& mesh, const glm::vec3& back, const glm::vec3& right, const glm::vec3& up, const glm::vec2& size,
              float depth, atlas::Sign cell, MaterialId rim, bool twoSided = false);

/// Something written on a Backrooms wall.
struct Scrawl {
    std::vector<std::string> lines;
    Ink   ink = Ink::Marker;
    float charHeight = 0.1f;
    float wobble = 1.0f;
    bool  clue = false;     ///< The phone number.
    bool  doomNote = false; ///< The note about DOOM on the terminals.
};

/// What gets written (and how), given the room left on the wall. `clue` is
/// the phone number, written - rarely - on its own, without a word of explanation.
Scrawl scrawl(rnd::Rng& rng, const std::string& clue, float maxWidth);

} // namespace decals
