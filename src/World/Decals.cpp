// ---------------------------------------------------------------------------
// Decals.cpp
// ---------------------------------------------------------------------------
#include "World/Decals.h"

#include "Render/MeshBuilder.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace decals {
namespace {

constexpr float kFontRows = 7.0f; ///< A capital is 7 font pixels tall.

/// Share of the phone number among everything written on the walls: tuned (with
/// the "clue-survey" scene) so that exploring walks past a copy about every 5
/// minutes - once every ~180 rooms, at the ~1.65 s a room the explore walk takes.
constexpr float kClueShare = 0.092f;

template <size_t N>
const char* pick(rnd::Rng& rng, const char* const (&options)[N]) {
    return options[rng.next() % N];
}

// Despair, warnings and tallies...
const char* const kMessages[] = {
    "HELP ME",
    "help",
    "I WAS HERE",
    "i was here 1997",
    "DON'T TRUST THE LIGHTS",
    "IT HEARS YOU",
    "NO EXIT",
    "KEEP WALKING",
    "the carpet is always wet",
    "DAY 3",
    "DAY 41",
    "DAY ???",
    "STOP COUNTING THE ROOMS",
    "it's behind you",
    "I CAN'T FIND MY WAY BACK",
    "sorry mom",
    "THEY LIVE IN THE WALLS",
    "DON'T LOOK UP",
    "HMMMMMMMMMMMMMMMMM",
    "SAME ROOM SAME ROOM SAME ROOM",
    "WAKE UP",
    "WAKE UP WAKE UP WAKE UP",
    "you are not the first",
    "i saw it smile",
    "DO NOT RUN",
    "all the doors are the same",
    "the hum is talking",
    "WHERE DID EVERYONE GO",
    "it moves when you blink",
    "<-- NOT THIS WAY",
    "THIS WAY -->",
    "turn back",
    "I MISS THE SUN",
    "how long has it been",
    "don't answer the phones",
    "the screens lie",
    "IT WAS NEVER A DREAM",
    "count your steps",
    "level 0 forever",
    "LET ME OUT",
    "it only moves when you can't see it",
    "|||| |||| |||| |||| ||",
    "|||| |||| |",
    "THE WALLS ARE YELLOW BECAUSE",
    "i'm still here. are you?",
};

/// ...and gibberish: letters that never were a word.
std::string gibberish(rnd::Rng& rng) {
    static const char kLetters[] = "AEIOUXZQKRTHWMVNYG";
    std::string s;
    const int words = rng.rangeInt(1, 3);
    for (int w = 0; w < words; ++w) {
        if (w) s += ' ';
        const int len = rng.rangeInt(3, 8);
        const char repeat = kLetters[rng.next() % (sizeof(kLetters) - 1)];
        for (int i = 0; i < len; ++i) s += rng.chance(0.3f) ? repeat : kLetters[rng.next() % (sizeof(kLetters) - 1)];
    }
    if (rng.chance(0.5f)) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

} // namespace

float lineWidth(const std::string& line, float charHeight) {
    if (line.empty()) return 0.0f;
    const float px = charHeight / kFontRows;
    return (static_cast<float>(line.size()) * atlas::kAdvance - 1.0f) * px;
}

std::vector<std::string> wrap(const std::string& text, int maxChars) {
    std::vector<std::string> lines;
    maxChars = std::max(maxChars, 1);
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string para = text.substr(start, end - start);
        std::string line;
        size_t i = 0;
        while (i < para.size()) {
            size_t j = para.find(' ', i);
            if (j == std::string::npos) j = para.size();
            std::string word = para.substr(i, j - i);
            while (static_cast<int>(word.size()) > maxChars) { // a word too long for a line is broken
                if (!line.empty()) {
                    lines.push_back(line);
                    line.clear();
                }
                lines.push_back(word.substr(0, static_cast<size_t>(maxChars)));
                word = word.substr(static_cast<size_t>(maxChars));
            }
            if (!line.empty() && static_cast<int>(line.size() + 1 + word.size()) > maxChars) {
                lines.push_back(line);
                line.clear();
            }
            if (!word.empty()) line += (line.empty() ? "" : " ") + word;
            i = j + 1;
        }
        lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

float fitHeight(const std::vector<std::string>& lines, float width, float height, float maxHeight, float lineSpacing) {
    size_t longest = 1;
    for (const std::string& l : lines) longest = std::max(longest, l.size());
    const float byWidth = width * kFontRows / (static_cast<float>(longest) * atlas::kAdvance - 1.0f);
    const float n = static_cast<float>(std::max<size_t>(lines.size(), 1));
    const float byHeight = height / (1.0f + (n - 1.0f) * lineSpacing);
    return std::max(0.005f, std::min({maxHeight, byWidth, byHeight}));
}

void addText(MeshData& mesh, const std::vector<std::string>& lines, const glm::vec3& anchor, const glm::vec3& right,
             const glm::vec3& up, Ink ink, const TextLayout& layout, uint64_t seed) {
    rnd::Rng rng(seed);
    const glm::vec3 normal = glm::normalize(glm::cross(right, up));
    const float px = layout.charHeight / kFontRows;
    const float inkU = atlas::kInkStride * static_cast<float>(ink);
    // The whole block leans by `slant` around its anchor.
    const float cs = std::cos(layout.slant), sn = std::sin(layout.slant);
    const glm::vec3 blockRight = right * cs + up * sn;
    const glm::vec3 blockUp = up * cs - right * sn;
    const float w = layout.wobble;

    for (size_t li = 0; li < lines.size(); ++li) {
        const std::string& line = lines[li];
        const float width = lineWidth(line, layout.charHeight);
        float x0 = 0.0f;
        if (layout.align == Align::Center) x0 = -0.5f * width;
        else if (layout.align == Align::Right) x0 = -width;
        const float baseline = -static_cast<float>(li) * layout.lineSpacing * layout.charHeight;
        float drift = 0.0f; // a hand's baseline wanders slowly along the line
        for (size_t ci = 0; ci < line.size(); ++ci) {
            const char ch = line[ci];
            drift += rng.range(-0.18f, 0.18f) * w * px;
            if (ch == ' ') continue;
            // Per-character tilt, size and placement: an unsteady hand.
            const float angle = rng.range(-0.07f, 0.07f) * w;
            const float scale = 1.0f + rng.range(-0.07f, 0.07f) * w;
            const float jx = rng.range(-0.25f, 0.25f) * w * px, jy = rng.range(-0.3f, 0.3f) * w * px + drift;
            const float penX = x0 + static_cast<float>(ci) * atlas::kAdvance * px;
            // The glyph's own frame: centred on its 5 x 7 body, then wobbled.
            const glm::vec2 centre(penX + 2.5f * px + jx, baseline + 3.5f * px + jy);
            const float ca = std::cos(angle), sa = std::sin(angle);
            const glm::vec3 gr = (blockRight * ca + blockUp * sa) * (px * scale);
            const glm::vec3 gu = (blockUp * ca - blockRight * sa) * (px * scale);
            const glm::vec3 c = anchor + blockRight * centre.x + blockUp * centre.y;
            // Cell corners relative to the glyph body centre (font pixels).
            const float l = -atlas::kGlyphX - 2.5f, r = atlas::kCellW - atlas::kGlyphX - 2.5f;
            const float b = -atlas::kBaseY - 3.5f, t = atlas::kCellH - atlas::kBaseY - 3.5f;
            const glm::vec3 corners[4] = {c + gr * l + gu * b, c + gr * r + gu * b, c + gr * r + gu * t, c + gr * l + gu * t};
            const glm::vec4 cell = atlas::glyphCell(ch);
            const glm::vec2 uvs[4] = {{cell.x + inkU, cell.y}, {cell.z + inkU, cell.y}, {cell.z + inkU, cell.w}, {cell.x + inkU, cell.w}};
            mesh::addQuad(mesh, corners, normal, uvs, MaterialId::Writing);
        }
    }
}

namespace {
/// A quad wound counter-clockwise as seen from the side `normal` points to, whatever order its corners come in.
void addFacing(MeshData& mesh, glm::vec3 corners[4], const glm::vec3& normal, glm::vec2 uvs[4], MaterialId material) {
    if (glm::dot(glm::cross(corners[1] - corners[0], corners[2] - corners[1]), normal) < 0.0f) {
        std::swap(corners[1], corners[3]);
        std::swap(uvs[1], uvs[3]);
    }
    mesh::addQuad(mesh, corners, normal, uvs, material);
}
} // namespace

void addPanel(MeshData& mesh, const glm::vec3& back, const glm::vec3& right, const glm::vec3& up, const glm::vec2& size,
              float depth, atlas::Sign cell, MaterialId rim, bool twoSided) {
    const glm::vec3 n = glm::normalize(glm::cross(right, up));
    const glm::vec3 hr = right * (0.5f * size.x), hu = up * (0.5f * size.y);
    const glm::vec3 front = back + n * depth;
    const glm::vec4 uv = atlas::signCell(cell);
    glm::vec3 face[4] = {front - hr - hu, front + hr - hu, front + hr + hu, front - hr + hu};
    glm::vec2 faceUV[4] = {{uv.x, uv.y}, {uv.z, uv.y}, {uv.z, uv.w}, {uv.x, uv.w}};
    addFacing(mesh, face, n, faceUV, MaterialId::Signage);
    if (twoSided) {
        // Seen from behind, the cell reads the right way round too.
        glm::vec3 rear[4] = {back + hr - hu, back - hr - hu, back - hr + hu, back + hr + hu};
        glm::vec2 rearUV[4] = {{uv.x, uv.y}, {uv.z, uv.y}, {uv.z, uv.w}, {uv.x, uv.w}};
        addFacing(mesh, rear, -n, rearUV, MaterialId::Signage);
    }
    // The rim: four thin sides.
    const glm::vec3 edges[4][2] = {{-hr + hu, hr + hu}, {-hr - hu, hr - hu}, {hr - hu, hr + hu}, {-hr - hu, -hr + hu}};
    const glm::vec3 normals[4] = {up, -up, right, -right};
    for (int i = 0; i < 4; ++i) {
        glm::vec3 side[4] = {back + edges[i][0], back + edges[i][1], front + edges[i][1], front + edges[i][0]};
        glm::vec2 small[4] = {{0, 0}, {0.05f, 0}, {0.05f, 0.05f}, {0, 0.05f}};
        addFacing(mesh, side, normals[i], small, rim);
    }
}

Scrawl scrawl(rnd::Rng& rng, const std::string& clue, float maxWidth) {
    Scrawl s;
    std::string text;
    if (!clue.empty() && rng.chance(kClueShare)) {
        // Just the number. No name, no explanation.
        s.clue = true;
        text = clue;
        s.ink = rng.chance(0.6f) ? Ink::Marker : Ink::Pen;
        s.charHeight = s.ink == Ink::Marker ? rng.range(0.09f, 0.12f) : rng.range(0.065f, 0.08f);
        s.wobble = 0.8f;
    } else {
        text = rng.chance(0.15f) ? gibberish(rng) : std::string(pick(rng, kMessages));
        const float r = rng.nextFloat();
        s.ink = r < 0.28f ? Ink::Marker : r < 0.40f ? Ink::Pen : r < 0.52f ? Ink::Pencil : r < 0.63f ? Ink::SprayRed
              : r < 0.74f ? Ink::SprayBlack : Ink::Blood;
        switch (s.ink) {
        case Ink::Pen:        s.charHeight = rng.range(0.05f, 0.075f); s.wobble = 1.0f; break;
        case Ink::Pencil:     s.charHeight = rng.range(0.05f, 0.08f);  s.wobble = 1.1f; break;
        case Ink::SprayRed:
        case Ink::SprayBlack: s.charHeight = rng.range(0.18f, 0.34f);  s.wobble = 1.4f; break;
        case Ink::Blood:      s.charHeight = rng.range(0.12f, 0.24f);  s.wobble = 1.8f; break;
        case Ink::Marker:
        default:              s.charHeight = rng.range(0.08f, 0.14f);  s.wobble = 1.0f; break;
        }
    }
    const float advance = atlas::kAdvance * s.charHeight / kFontRows;
    s.lines = wrap(text, std::max(4, static_cast<int>(maxWidth / advance)));
    return s;
}

} // namespace decals
