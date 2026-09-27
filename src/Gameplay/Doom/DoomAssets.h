#pragma once
// ---------------------------------------------------------------------------
// DoomAssets.h
// Every picture of the terminal Doom clone, painted procedurally at start-up
// into 8-bit palettised images, the way the 1993 engine stored its art:
//   * A 256-colour palette of 16 hue ramps (browns, tans, blood reds, slime
//     greens...) and 32 light-diminishing colormaps built from it: darkening
//     remaps every colour to its nearest palette entry, so shadows band and
//     shift hue exactly like the original's COLORMAP lump.
//   * 64x64 wall textures and flats (tech panels, brown brick, grey rock,
//     rusted metal, marble, hell rock, blinking computers, doors, the exit
//     switch; floor tiles, tread plate, dirt, animated nukage, ceiling lights)
//     and a wrapping red sky - from periodic noise, so all of them tile.
//   * Monster sprites (an imp and a zombie trooper: walk, attack, pain and
//     death frames) painted from shaded ellipses and capsules, then gritted,
//     edge-darkened and dithered into the palette; projectiles, puffs, blood,
//     explosions, barrels, lamps and pickups.
//   * First-person pistol and shotgun (idle, recoil, pump) with muzzle
//     flashes, the status bar, its big red digits, the marine's face in every
//     state of injury, and the title screen with its logo.
// Built once (on a worker thread, see preload()) and shared read-only.
// ---------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <vector>

namespace doom {

inline constexpr int     kScreenW = 320;
inline constexpr int     kScreenH = 200;
inline constexpr int     kStatusH = 32;
inline constexpr int     kViewH = kScreenH - kStatusH;
inline constexpr int     kTexSize = 64;                 ///< Wall / flat texels per map unit.
inline constexpr float   kSpriteTexelsPerUnit = 80.0f;  ///< Sprite texel density in the world.
inline constexpr int     kColormaps = 32;               ///< Light levels: 0 = full bright, 31 = black.
inline constexpr uint8_t kTransparent = 255;            ///< Sprite holes (never a drawable colour).

/// Palette hue ramps: entry = ramp * 16 + shade, shade 0 darkest .. 15 brightest.
enum Ramp : uint8_t {
    Grey, Brown, Tan, Red, Green, Olive, Steel, Orange, Yellow, Skin, Rust, Blue, Slime, Purple, Stone, Umber
};
constexpr uint8_t pal(Ramp r, int shade) { return static_cast<uint8_t>(r * 16 + shade); }

/// A palettised picture, rows top to bottom.
struct Image {
    int  w = 0, h = 0;
    bool fullbright = false; ///< Drawn without light diminishing (fire, flashes).
    std::vector<uint8_t> px;
    uint8_t at(int x, int y) const { return px[static_cast<size_t>(y * w + x)]; }
};

enum class WallTex : uint8_t {
    Tech, Brown, Stone, Metal, Marble, Hell, Computer, ComputerBlink, Door, Switch, SwitchOn, Count
};
enum class FlatTex : uint8_t { Tiles, Plate, Dirt, HellRock, Nukage0, Nukage1, Nukage2, CeilTile, CeilLight, Count };

/// World sprites. Animation frames of one action are consecutive.
enum class Spr : uint16_t {
    ImpWalk, ImpWalk1, ImpWalk2, ImpWalk3, ImpAttack, ImpAttack1, ImpPain,
    ImpDeath, ImpDeath1, ImpDeath2, ImpDeath3, ImpDeath4,
    TrooperWalk, TrooperWalk1, TrooperWalk2, TrooperWalk3, TrooperAim, TrooperFire, TrooperPain,
    TrooperDeath, TrooperDeath1, TrooperDeath2, TrooperDeath3, TrooperDeath4,
    Fireball, Fireball1, FireBoom, FireBoom1, FireBoom2,
    Puff, Puff1, Puff2, Blood, Blood1, Blood2,
    Barrel, Barrel1, BarrelBoom, BarrelBoom1, BarrelBoom2,
    ShotgunPickup, Clip, Shells, Medikit, Stimpack, Armor, Lamp,
    Count
};
constexpr Spr operator+(Spr s, int frame) { return static_cast<Spr>(static_cast<int>(s) + frame); }

/// First-person weapon pictures (screen resolution).
enum class Gun : uint8_t { Pistol, PistolFire, PistolFlash, Shotgun, ShotgunFire, ShotgunPump1, ShotgunPump2, ShotgunFlash, Count };

/// The marine's face: health level 0 (healthy) .. 4 (near death).
inline constexpr int kFaceHealthLevels = 5;
constexpr int faceLook(int level, int look /*0 left, 1 ahead, 2 right*/) { return level * 3 + look; }
constexpr int faceGrin(int level) { return 15 + level; }
constexpr int faceOuch(int level) { return 20 + level; }
inline constexpr int kFaceDead = 25;
inline constexpr int kFaceCount = 26;

class DoomAssets {
public:
    /// Starts building the assets on a worker thread (idempotent).
    static void preload();
    /// The shared assets (waits for / performs the build on first use).
    static const DoomAssets& get();

    const std::array<std::array<uint8_t, 3>, 256>& palette() const { return m_palette; }
    const uint8_t* colormap(int level) const { return &m_colormaps[static_cast<size_t>(level) * 256]; }

    const Image& wall(WallTex t) const { return m_walls[static_cast<size_t>(t)]; }
    const Image& flat(FlatTex t) const { return m_flats[static_cast<size_t>(t)]; }
    const Image& sky() const { return m_sky; }
    const Image& sprite(Spr s) const { return m_sprites[static_cast<size_t>(s)]; }
    const Image& gun(Gun g) const { return m_guns[static_cast<size_t>(g)]; }
    const Image& face(int i) const { return m_faces[static_cast<size_t>(i)]; }
    const Image& statusBar() const { return m_statusBar; }
    const Image& title() const { return m_title; }
    /// Big status-bar digit ('0'-'9', '%', '-'), or nullptr.
    const Image* bigGlyph(char c) const;

    /// Nearest palette entry to a linear-ish RGB colour in [0, 1].
    uint8_t nearest(float r, float g, float b) const;

private:
    DoomAssets();
    void buildPalette();

    std::array<std::array<uint8_t, 3>, 256> m_palette{};
    std::vector<uint8_t> m_colormaps;  ///< kColormaps x 256.
    std::vector<uint8_t> m_lut;        ///< 32^3 RGB -> nearest palette entry.
    std::array<Image, static_cast<size_t>(WallTex::Count)> m_walls;
    std::array<Image, static_cast<size_t>(FlatTex::Count)> m_flats;
    std::array<Image, static_cast<size_t>(Spr::Count)>     m_sprites;
    std::array<Image, static_cast<size_t>(Gun::Count)>     m_guns;
    std::array<Image, kFaceCount>                          m_faces;
    std::array<Image, 12>                                  m_bigGlyphs; ///< 0-9, %, -
    Image m_sky, m_statusBar, m_title;

};

} // namespace doom
