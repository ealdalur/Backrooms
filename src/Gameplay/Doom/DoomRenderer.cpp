// ---------------------------------------------------------------------------
// DoomRenderer.cpp
// ---------------------------------------------------------------------------
#include "Gameplay/Doom/DoomRenderer.h"

#include "Render/BitmapFont.h"
#include "Render/TerminalRenderer.h"

#include <algorithm>
#include <cmath>

namespace doom {
namespace {

constexpr float kHalfW = kScreenW * 0.5f;
constexpr float kProj = kHalfW;          ///< Pixels per unit at distance 1 (90 degree field of view).
constexpr float kHorizon = kViewH * 0.5f;
constexpr float kHalfPi = 1.5707963f;

FlatTex animatedFlat(FlatTex f, int tic) {
    if (f == FlatTex::Nukage0) return static_cast<FlatTex>(static_cast<int>(FlatTex::Nukage0) + (tic / 8) % 3);
    return f;
}

} // namespace

SoftwareRenderer::SoftwareRenderer() : m_assets(DoomAssets::get()), m_fb(static_cast<size_t>(kScreenW * kScreenH), 0) {}

int SoftwareRenderer::lightIndex(int light, float distance, int extra) const {
    // Darker sectors start further down the colormaps; everything fades with distance.
    const float base = static_cast<float>(255 - std::clamp(light, 0, 255)) / 255.0f * 26.0f;
    const int index = static_cast<int>(base + distance * 1.7f - 3.0f) - extra * 4;
    return std::clamp(index, 0, kColormaps - 1);
}

void SoftwareRenderer::drawView(const Level& level, const ViewParams& view, std::vector<ViewSprite>& sprites) {
    m_levelW = level.width();
    m_cellLight.resize(static_cast<size_t>(level.width() * level.height()));
    for (int y = 0; y < level.height(); ++y)
        for (int x = 0; x < level.width(); ++x) m_cellLight[static_cast<size_t>(y * m_levelW + x)] = level.lightAt(x, y, view.tic);
    castFlats(level, view);
    castWalls(level, view);
    drawSprites(view, sprites);
}

void SoftwareRenderer::castFlats(const Level& level, const ViewParams& view) {
    const glm::vec2 dir(std::cos(view.angle), std::sin(view.angle));
    const glm::vec2 right(-dir.y, dir.x);
    const Image& sky = m_assets.sky();

    // Sky columns: 256 texels per quarter turn, wrapping.
    std::array<int, kScreenW> skyCol;
    for (int x = 0; x < kScreenW; ++x) {
        const float camX = (2.0f * (static_cast<float>(x) + 0.5f)) / kScreenW - 1.0f;
        const float a = view.angle + std::atan(camX);
        skyCol[static_cast<size_t>(x)] = static_cast<int>(std::floor(a / kHalfPi * 256.0f)) & 255;
    }

    for (int y = 0; y < kViewH; ++y) {
        const float dy = static_cast<float>(y) + 0.5f - kHorizon;
        const bool isFloor = dy > 0.0f;
        const float rowDist = isFloor ? view.eyeZ * kProj / dy : (1.0f - view.eyeZ) * kProj / -dy;
        const glm::vec2 step = right * (rowDist * 2.0f / kScreenW);
        glm::vec2 p = view.pos + dir * rowDist - right * (rowDist * (1.0f - 1.0f / kScreenW));
        const int skyRow = std::clamp(y + sky.h - static_cast<int>(kHorizon), 0, sky.h - 1);
        uint8_t* out = &m_fb[static_cast<size_t>(y * kScreenW)];
        for (int x = 0; x < kScreenW; ++x, p += step) {
            const int cx = static_cast<int>(std::floor(p.x)), cy = static_cast<int>(std::floor(p.y));
            if (!level.inside(cx, cy)) {
                out[x] = 0;
                continue;
            }
            const Tile& t = level.at(cx, cy);
            if (!isFloor && t.sky) {
                out[x] = sky.at(skyCol[static_cast<size_t>(x)], skyRow);
                continue;
            }
            const Image& flat = m_assets.flat(animatedFlat(isFloor ? t.floor : t.ceil, view.tic));
            const int u = static_cast<int>(std::floor(p.x * kTexSize)) & (kTexSize - 1);
            const int v = static_cast<int>(std::floor(p.y * kTexSize)) & (kTexSize - 1);
            const int li = lightIndex(m_cellLight[static_cast<size_t>(cy * m_levelW + cx)], rowDist, view.extraLight);
            out[x] = m_assets.colormap(li)[flat.at(u, v)];
        }
    }
}

void SoftwareRenderer::castWalls(const Level& level, const ViewParams& view) {
    const glm::vec2 dir(std::cos(view.angle), std::sin(view.angle));
    const glm::vec2 right(-dir.y, dir.x);

    for (int x = 0; x < kScreenW; ++x) {
        const float camX = (2.0f * (static_cast<float>(x) + 0.5f)) / kScreenW - 1.0f;
        const glm::vec2 rd = dir + right * camX;
        int mx = static_cast<int>(std::floor(view.pos.x)), my = static_cast<int>(std::floor(view.pos.y));
        const float deltaX = std::fabs(rd.x) < 1e-6f ? 1e30f : std::fabs(1.0f / rd.x);
        const float deltaY = std::fabs(rd.y) < 1e-6f ? 1e30f : std::fabs(1.0f / rd.y);
        const int stepX = rd.x < 0.0f ? -1 : 1, stepY = rd.y < 0.0f ? -1 : 1;
        float sideX = (rd.x < 0.0f ? view.pos.x - static_cast<float>(mx) : static_cast<float>(mx) + 1.0f - view.pos.x) * deltaX;
        float sideY = (rd.y < 0.0f ? view.pos.y - static_cast<float>(my) : static_cast<float>(my) + 1.0f - view.pos.y) * deltaY;
        int prevX = mx, prevY = my; // the open cell the ray is travelling through
        float clipTop = 0.0f;
        const float clipBottom = static_cast<float>(kViewH);
        m_depth[static_cast<size_t>(x)] = 1e9f;
        m_doorDepth[static_cast<size_t>(x)] = 1e9f;
        m_doorBottom[static_cast<size_t>(x)] = -1.0f;

        for (int i = 0; i < 256; ++i) {
            int side;
            if (sideX < sideY) {
                sideX += deltaX;
                mx += stepX;
                side = 0;
            } else {
                sideY += deltaY;
                my += stepY;
                side = 1;
            }
            if (!level.inside(mx, my)) break;
            const Tile& t = level.at(mx, my);
            if (t.kind == Tile::Empty) {
                prevX = mx;
                prevY = my;
                continue;
            }
            const float d = std::max(side == 0 ? sideX - deltaX : sideY - deltaY, 1e-3f);
            float wallX = side == 0 ? view.pos.y + d * rd.y : view.pos.x + d * rd.x;
            wallX -= std::floor(wallX);
            int u = std::min(kTexSize - 1, static_cast<int>(wallX * kTexSize));
            if ((side == 0 && rd.x < 0.0f) || (side == 1 && rd.y > 0.0f)) u = kTexSize - 1 - u; // never mirrored

            WallTex tex = t.kind == Tile::Wall && !t.overrideTex ? level.at(prevX, prevY).theme : t.wallTex;
            if (tex == WallTex::Computer && (view.tic / 18) % 2) tex = WallTex::ComputerBlink;
            const Image& img = m_assets.wall(tex);
            const int li = std::clamp(lightIndex(m_cellLight[static_cast<size_t>(prevY * m_levelW + prevX)], d, view.extraLight) +
                                          (side == 0 ? -1 : 1), 0, kColormaps - 1);
            const uint8_t* cmap = m_assets.colormap(li);

            const float scale = kProj / d;
            const float top = kHorizon - (1.0f - view.eyeZ) * scale;
            const float open = t.kind == Tile::Door ? level.doors[static_cast<size_t>(t.door)].open : 0.0f;
            const float bottom = kHorizon + (view.eyeZ - open) * scale; // a door's lower edge rises with it

            // Texel row at screen row y: height above the floor h = eyeZ + (horizon - y) / scale,
            // v = (1 - h + open) * 64 (a raised door's texture rises with it).
            const int y0 = std::max(static_cast<int>(std::ceil(std::max(top, clipTop) - 0.5f)), 0);
            const int y1 = std::min(static_cast<int>(std::ceil(std::min(bottom, clipBottom) - 0.5f)), kViewH);
            const float dv = kTexSize / scale;
            float v = (1.0f - view.eyeZ - (kHorizon - (static_cast<float>(y0) + 0.5f)) / scale + open) * kTexSize;
            for (int y = y0; y < y1; ++y, v += dv) {
                const int tv = std::clamp(static_cast<int>(v), 0, kTexSize - 1);
                m_fb[static_cast<size_t>(y * kScreenW + x)] = cmap[img.at(u, tv)];
            }

            if (t.kind == Tile::Door && open > 0.02f) {
                // See under it: the rest of the column only shows below the slab.
                if (m_doorDepth[static_cast<size_t>(x)] > 1e8f) {
                    m_doorDepth[static_cast<size_t>(x)] = d;
                    m_doorBottom[static_cast<size_t>(x)] = bottom;
                }
                clipTop = std::max(clipTop, bottom);
                prevX = mx;
                prevY = my;
                if (clipTop >= clipBottom) {
                    m_depth[static_cast<size_t>(x)] = d;
                    break;
                }
                continue;
            }
            m_depth[static_cast<size_t>(x)] = d;
            break;
        }
    }
}

void SoftwareRenderer::drawSprites(const ViewParams& view, std::vector<ViewSprite>& sprites) {
    const glm::vec2 dir(std::cos(view.angle), std::sin(view.angle));
    const glm::vec2 right(-dir.y, dir.x);
    std::sort(sprites.begin(), sprites.end(), [&](const ViewSprite& a, const ViewSprite& b) {
        return glm::dot(a.pos - view.pos, dir) > glm::dot(b.pos - view.pos, dir);
    });
    for (const ViewSprite& s : sprites) {
        const glm::vec2 rel = s.pos - view.pos;
        const float depth = glm::dot(rel, dir);
        if (depth < 0.12f) continue;
        const float lateral = glm::dot(rel, right);
        const Image& img = m_assets.sprite(s.sprite);
        const float scale = kProj / depth;
        const float texel = scale / kSpriteTexelsPerUnit; // screen pixels per sprite texel
        const float sx = kHalfW + lateral * scale;
        const float w = static_cast<float>(img.w) * texel, h = static_cast<float>(img.h) * texel;
        const float left = sx - w * 0.5f;
        const float bottom = kHorizon + (view.eyeZ - s.z) * scale, top = bottom - h;
        const int cx = static_cast<int>(std::floor(s.pos.x)), cy = static_cast<int>(std::floor(s.pos.y));
        const int cell = std::clamp(cy, 0, static_cast<int>(m_cellLight.size()) / std::max(1, m_levelW) - 1) * m_levelW +
                         std::clamp(cx, 0, m_levelW - 1);
        const int li = img.fullbright ? 0 : lightIndex(m_cellLight[static_cast<size_t>(cell)], depth, view.extraLight);
        const uint8_t* cmap = m_assets.colormap(li);

        const int x0 = std::max(0, static_cast<int>(std::ceil(left - 0.5f)));
        const int x1 = std::min(kScreenW, static_cast<int>(std::ceil(left + w - 0.5f)));
        for (int x = x0; x < x1; ++x) {
            if (depth >= m_depth[static_cast<size_t>(x)]) continue;
            const int tx = std::clamp(static_cast<int>((static_cast<float>(x) + 0.5f - left) / texel), 0, img.w - 1);
            int y0 = std::max(0, static_cast<int>(std::ceil(top - 0.5f)));
            const int y1 = std::min(kViewH, static_cast<int>(std::ceil(bottom - 0.5f)));
            if (m_doorDepth[static_cast<size_t>(x)] < depth) y0 = std::max(y0, static_cast<int>(std::ceil(m_doorBottom[static_cast<size_t>(x)] - 0.5f)));
            for (int y = y0; y < y1; ++y) {
                const int ty = std::clamp(static_cast<int>((static_cast<float>(y) + 0.5f - top) / texel), 0, img.h - 1);
                const uint8_t p = img.at(tx, ty);
                if (p != kTransparent) m_fb[static_cast<size_t>(y * kScreenW + x)] = cmap[p];
            }
        }
    }
}

void SoftwareRenderer::drawImage(const Image& img, int x, int y, int light, int clipBottom) {
    const uint8_t* cmap = m_assets.colormap(img.fullbright ? 0 : std::clamp(light, 0, kColormaps - 1));
    for (int j = 0; j < img.h; ++j) {
        const int sy = y + j;
        if (sy < 0 || sy >= std::min(clipBottom, kScreenH)) continue;
        for (int i = 0; i < img.w; ++i) {
            const int sx = x + i;
            if (sx < 0 || sx >= kScreenW) continue;
            const uint8_t p = img.at(i, j);
            if (p != kTransparent) m_fb[static_cast<size_t>(sy * kScreenW + sx)] = cmap[p];
        }
    }
}

void SoftwareRenderer::tileFlat(const Image& flat, int x0, int y0, int x1, int y1, int light) {
    const uint8_t* cmap = m_assets.colormap(std::clamp(light, 0, kColormaps - 1));
    for (int y = std::max(0, y0); y < std::min(kScreenH, y1); ++y)
        for (int x = std::max(0, x0); x < std::min(kScreenW, x1); ++x)
            m_fb[static_cast<size_t>(y * kScreenW + x)] = cmap[flat.at(x % flat.w, y % flat.h)];
}

int SoftwareRenderer::drawText(int x, int y, const std::string& text, uint8_t color, int scale) {
    const uint8_t shadow = pal(Grey, 0);
    for (int pass = 0; pass < 2; ++pass) {
        const int off = pass == 0 ? scale : 0;
        int cx = x;
        for (char ch : text) {
            if (const uint8_t* rows = font::glyph(ch)) {
                for (int r = 0; r < font::kGlyphH; ++r)
                    for (int c = 0; c < font::kGlyphW; ++c) {
                        if (!(rows[r] & (0x10 >> c))) continue;
                        for (int dy = 0; dy < scale; ++dy)
                            for (int dx = 0; dx < scale; ++dx) {
                                const int px = cx + c * scale + dx + off, py = y + r * scale + dy + off;
                                if (px >= 0 && py >= 0 && px < kScreenW && py < kScreenH)
                                    m_fb[static_cast<size_t>(py * kScreenW + px)] = pass == 0 ? shadow : color;
                            }
                    }
            }
            cx += 6 * scale;
        }
    }
    return textWidth(text, scale);
}

void SoftwareRenderer::drawBigNumber(int right, int y, int value, bool percent) {
    std::string s = std::to_string(std::max(0, value));
    if (percent) s += '%';
    int x = right;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        const Image* g = m_assets.bigGlyph(*it);
        if (!g) continue;
        x -= g->w;
        drawImage(*g, x, y);
    }
}

void SoftwareRenderer::meltOver(const std::vector<uint8_t>& from, const std::array<int, kScreenW>& offsets) {
    for (int x = 0; x < kScreenW; ++x) {
        const int off = std::max(0, offsets[static_cast<size_t>(x)]);
        for (int y = off; y < kScreenH; ++y) m_fb[static_cast<size_t>(y * kScreenW + x)] = from[static_cast<size_t>((y - off) * kScreenW + x)];
    }
}

void SoftwareRenderer::present(TerminalGraphics& out, float red, float yellow) const {
    // Pain and pickups tint the whole palette, as the original swapped PLAYPALs.
    std::array<std::array<uint8_t, 4>, 256> lut;
    const auto& palette = m_assets.palette();
    for (size_t i = 0; i < 256; ++i) {
        glm::vec3 c(palette[i][0], palette[i][1], palette[i][2]);
        c = glm::mix(c, glm::vec3(255.0f, 0.0f, 0.0f), std::clamp(red, 0.0f, 1.0f));
        c = glm::mix(c, glm::vec3(215.0f, 186.0f, 69.0f), std::clamp(yellow, 0.0f, 1.0f));
        lut[i] = {static_cast<uint8_t>(c.r), static_cast<uint8_t>(c.g), static_cast<uint8_t>(c.b), 255};
    }
    out.width = kScreenW;
    out.height = kScreenH;
    out.rgba.resize(static_cast<size_t>(kScreenW * kScreenH * 4));
    for (size_t i = 0; i < m_fb.size(); ++i) std::copy(lut[m_fb[i]].begin(), lut[m_fb[i]].end(), out.rgba.begin() + static_cast<std::ptrdiff_t>(i * 4));
    ++out.version;
}

} // namespace doom
