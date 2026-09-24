#pragma once
// ---------------------------------------------------------------------------
// MaterialTypes.h
// GL-independent description of every procedural material. The material id
// doubles as the layer index inside the albedo / surface texture arrays and
// is stored per-vertex, so whole chunks render in a single draw call.
// ---------------------------------------------------------------------------

#include <glm/glm.hpp>
#include <cstdint>

enum class MaterialId : uint8_t {
    Wallpaper = 0, ///< Dingy mono-yellow wallpaper with vertical grain and stains.
    Carpet,        ///< Distressed, damp office carpet.
    CeilingTile,   ///< Stained acoustic ceiling tiles with T-bar grid.
    WoodLaminate,  ///< Artificial wood-grain laminate (desks, doors).
    GrayMetal,     ///< Dull gray painted steel (cabinets, frames).
    LightPanel,    ///< Fluorescent troffer diffuser (emissive).
    DarkPlastic,   ///< Black/dark rubber & plastic parts.
    Fabric,        ///< Woven office upholstery (chairs, cubicle partitions).
    Count
};

inline constexpr int kMaterialCount = static_cast<int>(MaterialId::Count);

/// Static shading/mapping parameters for a material.
struct MaterialInfo {
    const char* name;
    float       tileSize;      ///< World metres covered by one texture repeat.
    float       specular;      ///< Blinn-Phong specular intensity (scaled by surface mask).
    float       shininess;     ///< Blinn-Phong exponent.
    float       bumpScale;     ///< Height-map relief in metres (derivative bump mapping).
    float       emissiveGain;  ///< HDR multiplier for emissive texels.
};

/// Per-material constants. Tile sizes are chosen to divide the chunk size
/// (25 m) so that texture coordinates stay continuous across chunk seams
/// while being computed from small chunk-local values (no float drift).
inline const MaterialInfo& materialInfo(MaterialId id) {
    static const MaterialInfo kTable[kMaterialCount] = {
        //  name            tile   spec  shin   bump     emissive
        {"Wallpaper",     1.25f, 0.10f,  18.0f, 0.0012f, 0.0f},
        {"Carpet",        2.50f, 0.50f,  14.0f, 0.0040f, 0.0f},
        {"CeilingTile",   2.50f, 0.06f,  12.0f, 0.0040f, 0.0f},
        {"WoodLaminate",  1.25f, 0.45f,  64.0f, 0.0006f, 0.0f},
        {"GrayMetal",     1.00f, 0.55f,  72.0f, 0.0005f, 0.0f},
        {"LightPanel",    1.00f, 0.30f,  32.0f, 0.0008f, 7.5f},
        {"DarkPlastic",   0.50f, 0.30f,  28.0f, 0.0006f, 0.0f},
        {"Fabric",        0.40f, 0.04f,   8.0f, 0.0012f, 0.0f},
    };
    return kTable[static_cast<int>(id)];
}

/// Packs the shader-facing parameters of a material into a vec4:
/// x = specular intensity, y = shininess, z = bump scale, w = emissive gain.
inline glm::vec4 materialShaderParams(MaterialId id) {
    const MaterialInfo& m = materialInfo(id);
    return {m.specular, m.shininess, m.bumpScale, m.emissiveGain};
}
