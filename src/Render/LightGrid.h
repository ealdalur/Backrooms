#pragma once
// ---------------------------------------------------------------------------
// LightGrid.h
// World-space light clustering. Lights only ever reach their own storey (and
// down a stairwell shaft), so light culling is done on a stack of 2D grids -
// one slice per loaded storey, 2.5 m cells aligned with the wall grid. Each
// grid cell stores the list of fixtures that can reach it *without passing
// through a wall* (visibility precomputed per light at generation), so
// fragments only evaluate a handful of relevant lights, walls do not leak
// light, and no light bleeds through the slab between storeys.
//
// Also owns the per-storey edge map texture used by the shader's analytic AO.
// ---------------------------------------------------------------------------

#include "Render/Texture.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class Chunk;
class Shader;
class WorldGenerator;

/// Something that disturbs nearby fluorescent tubes (e.g. the Stalker
/// draining them): lights within `radius` stutter and dim by up to `strength`.
struct LightDisturbance {
    glm::vec3 position;
    float     radius;
    float     strength; ///< 0..1
};

class LightGrid {
public:
    /// Creates the GL buffer objects.
    void init();

    /// Rebuilds the light list, grid and edge map for the given chunks
    /// (must be called whenever the loaded chunk set changes).
    void rebuild(const std::vector<Chunk*>& chunks, const WorldGenerator& generator);

    /// Evaluates every fixture's flicker at `time`, applies disturbances and
    /// streams the result.
    void updateIntensities(double time, const std::vector<LightDisturbance>& disturbances);

    /// Binds all textures (starting at `firstUnit`) and sets grid uniforms.
    /// Uses units firstUnit .. firstUnit+4.
    void bind(const Shader& shader, unsigned firstUnit) const;

    size_t lightCount() const { return m_lightCount; }

private:
    TextureBuffer m_lightData;      ///< RGBA32F, 2 texels per light.
    TextureBuffer m_lightIntensity; ///< R32F, 1 texel per light.
    TextureBuffer m_gridCells;      ///< RG32UI (offset, count) per cell, slice-major.
    TextureBuffer m_gridIndices;    ///< R32UI light indices.
    Texture2D     m_edgeMap;        ///< RG8UI (west, south) edge types per world cell; slices stacked in rows.

    std::vector<const Chunk*> m_order;     ///< Chunks in light-list order.
    std::vector<float>        m_intensity; ///< Scratch buffer for streaming.
    size_t     m_lightCount = 0;
    glm::vec2  m_gridOrigin{0.0f};
    glm::ivec2 m_gridDims{0};      ///< Cells per slice (x, z).
    glm::vec2  m_edgeOrigin{0.0f};
    glm::ivec2 m_edgeDims{0};      ///< Texels per slice (x, z).
    int        m_minLevel = 0;     ///< Storey of slice 0.
    int        m_levels = 0;       ///< Number of slices.
};
