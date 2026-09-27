// ---------------------------------------------------------------------------
// LightGrid.cpp
// ---------------------------------------------------------------------------
#include "Render/LightGrid.h"

#include "Math/Noise.h"
#include "Render/Shader.h"
#include "World/Chunk.h"
#include "World/WorldConstants.h"
#include "World/WorldGenerator.h"

#include <algorithm>
#include <climits>
#include <cmath>

void LightGrid::init() {
    m_lightData.create(GL_RGBA32F);
    m_lightIntensity.create(GL_R32F);
    m_gridCells.create(GL_RG32UI);
    m_gridIndices.create(GL_R32UI);
    const uint8_t zero[2] = {0, 0};
    m_edgeMap.create(1, 1, GL_RG8UI, GL_RG_INTEGER, GL_UNSIGNED_BYTE, zero, GL_NEAREST, GL_CLAMP_TO_EDGE);
}

void LightGrid::rebuild(const std::vector<Chunk*>& chunks, const WorldGenerator& generator) {
    m_order.assign(chunks.begin(), chunks.end());
    if (chunks.empty()) {
        m_lightCount = 0;
        m_gridDims = glm::ivec2(0);
        m_edgeDims = glm::ivec2(0);
        m_levels = 0;
        return;
    }

    // ---- Region covered by the loaded chunks ------------------------------------
    int minCx = INT_MAX, minCz = INT_MAX, maxCx = INT_MIN, maxCz = INT_MIN;
    int minL = INT_MAX, maxL = INT_MIN;
    for (const Chunk* c : chunks) {
        minCx = std::min(minCx, c->coord().x);
        minCz = std::min(minCz, c->coord().z);
        maxCx = std::max(maxCx, c->coord().x);
        maxCz = std::max(maxCz, c->coord().z);
        minL = std::min(minL, c->coord().level);
        maxL = std::max(maxL, c->coord().level);
    }
    m_minLevel = minL;
    m_levels = maxL - minL + 1;
    const int gridPerChunk = static_cast<int>(world::kChunkSize / world::kLightGridCell + 0.5f);
    const glm::ivec2 gridMin(minCx * gridPerChunk, minCz * gridPerChunk);
    m_gridDims = glm::ivec2((maxCx - minCx + 1) * gridPerChunk, (maxCz - minCz + 1) * gridPerChunk);
    m_gridOrigin = glm::vec2(gridMin) * world::kLightGridCell;

    // ---- Global light list ---------------------------------------------------------
    std::vector<glm::vec4> lightData;
    int base = 0;
    for (Chunk* c : chunks) {
        c->setLightBase(base);
        for (const LightFixture& l : c->lights()) {
            lightData.emplace_back(l.center(), l.halfSize().x);
            lightData.emplace_back(l.halfSize().y, l.color());
            ++base;
        }
    }
    m_lightCount = static_cast<size_t>(base);
    m_lightData.upload(lightData.data(), lightData.size() * sizeof(glm::vec4), GL_STATIC_DRAW);

    // ---- Grid: counting sort of (cell, light) pairs ---------------------------------
    const size_t sliceCells = static_cast<size_t>(m_gridDims.x) * static_cast<size_t>(m_gridDims.y);
    const size_t cellCount = sliceCells * static_cast<size_t>(m_levels);
    std::vector<uint32_t> counts(cellCount, 0u);
    auto cellIndex = [&](const glm::ivec3& g) -> int {
        const int lx = g.x - gridMin.x, lz = g.y - gridMin.y, ls = g.z - m_minLevel;
        if (lx < 0 || lz < 0 || ls < 0 || lx >= m_gridDims.x || lz >= m_gridDims.y || ls >= m_levels) return -1;
        return (ls * m_gridDims.y + lz) * m_gridDims.x + lx;
    };
    for (const Chunk* c : chunks) {
        for (const LightFixture& l : c->lights()) {
            for (const glm::ivec3& g : l.visibleCells()) {
                const int i = cellIndex(g);
                if (i >= 0) ++counts[static_cast<size_t>(i)];
            }
        }
    }
    std::vector<uint32_t> cells(cellCount * 2); // (offset, count)
    uint32_t running = 0;
    for (size_t i = 0; i < cellCount; ++i) {
        cells[i * 2 + 0] = running;
        cells[i * 2 + 1] = counts[i];
        running += counts[i];
    }
    std::vector<uint32_t> indices(running);
    std::vector<uint32_t> fill(cellCount, 0u);
    uint32_t lightIndex = 0;
    for (const Chunk* c : chunks) {
        for (const LightFixture& l : c->lights()) {
            for (const glm::ivec3& g : l.visibleCells()) {
                const int i = cellIndex(g);
                if (i < 0) continue;
                const size_t ci = static_cast<size_t>(i);
                indices[cells[ci * 2] + fill[ci]++] = lightIndex;
            }
            ++lightIndex;
        }
    }
    m_gridCells.upload(cells.data(), cells.size() * sizeof(uint32_t), GL_STATIC_DRAW);
    m_gridIndices.upload(indices.data(), indices.size() * sizeof(uint32_t), GL_STATIC_DRAW);

    // ---- Edge map (one texel per world cell, +1 row/column for east/north
    //      edges), one slice of rows per storey ------------------------------------------
    const int N = world::kChunkCells;
    const glm::ivec2 cellMin(minCx * N, minCz * N);
    m_edgeDims = glm::ivec2((maxCx - minCx + 1) * N + 1, (maxCz - minCz + 1) * N + 1);
    m_edgeOrigin = glm::vec2(cellMin) * world::kCellSize;
    const size_t rowTexels = static_cast<size_t>(m_edgeDims.x);
    std::vector<uint8_t> edges(rowTexels * static_cast<size_t>(m_edgeDims.y) * static_cast<size_t>(m_levels) * 2);
    for (int s = 0; s < m_levels; ++s) {
        const int level = m_minLevel + s;
        for (int z = 0; z < m_edgeDims.y; ++z) {
            for (int x = 0; x < m_edgeDims.x; ++x) {
                const size_t row = static_cast<size_t>(s) * static_cast<size_t>(m_edgeDims.y) + static_cast<size_t>(z);
                const size_t i = (row * rowTexels + static_cast<size_t>(x)) * 2;
                edges[i + 0] = static_cast<uint8_t>(generator.edge(level, cellMin.x + x, cellMin.y + z, world::EdgeAxis::West));
                edges[i + 1] = static_cast<uint8_t>(generator.edge(level, cellMin.x + x, cellMin.y + z, world::EdgeAxis::South));
            }
        }
    }
    m_edgeMap.create(m_edgeDims.x, m_edgeDims.y * m_levels, GL_RG8UI, GL_RG_INTEGER, GL_UNSIGNED_BYTE, edges.data(),
                     GL_NEAREST, GL_CLAMP_TO_EDGE);

    m_intensity.assign(m_lightCount, 1.0f);
}

void LightGrid::updateIntensities(double time, const std::vector<LightDisturbance>& disturbances) {
    size_t i = 0;
    for (const Chunk* c : m_order) {
        for (const LightFixture& l : c->lights()) {
            if (i >= m_intensity.size()) break;
            float v = l.intensity(time);
            for (const LightDisturbance& d : disturbances) {
                const glm::vec3 delta = l.center() - d.position;
                if (std::fabs(delta.y) > world::kLevelHeight) continue; // another storey
                const float dist = glm::length(delta);
                if (dist >= d.radius) continue;
                // Closer tubes are drained harder: they sag and stutter out in
                // irregular, per-tube bursts.
                const float t = (1.0f - dist / d.radius) * d.strength;
                const float gate = noise::value1D(time * 11.0 + static_cast<double>(l.seed() % 997u), l.seed() ^ 0x5A1Cull);
                v *= gate < t * 0.75f ? 0.06f : 1.0f - 0.55f * t;
            }
            m_intensity[i++] = v;
        }
    }
    m_lightIntensity.upload(m_intensity.data(), m_intensity.size() * sizeof(float), GL_STREAM_DRAW);
}

void LightGrid::bind(const Shader& shader, unsigned firstUnit) const {
    m_lightData.bind(firstUnit + 0);
    m_lightIntensity.bind(firstUnit + 1);
    m_gridCells.bind(firstUnit + 2);
    m_gridIndices.bind(firstUnit + 3);
    m_edgeMap.bind(firstUnit + 4);

    shader.set("uLightData", static_cast<int>(firstUnit + 0));
    shader.set("uLightIntensity", static_cast<int>(firstUnit + 1));
    shader.set("uGridCells", static_cast<int>(firstUnit + 2));
    shader.set("uGridIndices", static_cast<int>(firstUnit + 3));
    shader.set("uEdgeMap", static_cast<int>(firstUnit + 4));

    shader.set("uGridOrigin", m_gridOrigin);
    shader.set("uGridCellSize", world::kLightGridCell);
    shader.set("uGridDims", m_gridDims);
    shader.set("uEdgeOrigin", m_edgeOrigin);
    shader.set("uEdgeDims", m_edgeDims);
    shader.set("uGridMinLevel", m_minLevel);
    shader.set("uGridLevels", m_levels);
}
