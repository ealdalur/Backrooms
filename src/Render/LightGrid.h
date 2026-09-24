#pragma once
// ---------------------------------------------------------------------------
// LightGrid.h
// World-space light clustering. Because the Backrooms is a single storey,
// light culling is done on a 2D grid (2.5 m cells, aligned with the wall
// grid) covering the loaded chunks. Each grid cell stores the list of
// fixtures that can reach it *without passing through a wall* (visibility
// precomputed per light at generation), so fragments only evaluate a
// handful of relevant lights and walls do not leak light.
//
// Also owns the edge map texture used by the shader's analytic AO.
// ---------------------------------------------------------------------------

#include "Render/Texture.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class Chunk;
class Shader;
class WorldGenerator;

class LightGrid {
public:
    /// Creates the GL buffer objects.
    void init();

    /// Rebuilds the light list, grid and edge map for the given chunks
    /// (must be called whenever the loaded chunk set changes).
    void rebuild(const std::vector<Chunk*>& chunks, const WorldGenerator& generator);

    /// Evaluates every fixture's flicker at `time` and streams the result.
    void updateIntensities(double time);

    /// Binds all textures (starting at `firstUnit`) and sets grid uniforms.
    /// Uses units firstUnit .. firstUnit+4.
    void bind(const Shader& shader, unsigned firstUnit) const;

    size_t lightCount() const { return m_lightCount; }

private:
    TextureBuffer m_lightData;      ///< RGBA32F, 2 texels per light.
    TextureBuffer m_lightIntensity; ///< R32F, 1 texel per light.
    TextureBuffer m_gridCells;      ///< RG32UI (offset, count) per cell.
    TextureBuffer m_gridIndices;    ///< R32UI light indices.
    Texture2D     m_edgeMap;        ///< RG8UI (west, south) edge types per world cell.

    std::vector<const Chunk*> m_order;     ///< Chunks in light-list order.
    std::vector<float>        m_intensity; ///< Scratch buffer for streaming.
    size_t     m_lightCount = 0;
    glm::vec2  m_gridOrigin{0.0f};
    glm::ivec2 m_gridDims{0};
    glm::vec2  m_edgeOrigin{0.0f};
    glm::ivec2 m_edgeDims{0};
};
