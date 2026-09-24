// ---------------------------------------------------------------------------
// LightGrid.cpp
// ---------------------------------------------------------------------------
#include "Render/LightGrid.h"

#include "Render/Shader.h"
#include "World/Chunk.h"
#include "World/WorldConstants.h"
#include "World/WorldGenerator.h"

#include <algorithm>
#include <climits>

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
        return;
    }

    // ---- Region covered by the loaded chunks ------------------------------------
    int minCx = INT_MAX, minCz = INT_MAX, maxCx = INT_MIN, maxCz = INT_MIN;
    for (const Chunk* c : chunks) {
        minCx = std::min(minCx, c->coord().x);
        minCz = std::min(minCz, c->coord().z);
        maxCx = std::max(maxCx, c->coord().x);
        maxCz = std::max(maxCz, c->coord().z);
    }
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
    const size_t cellCount = static_cast<size_t>(m_gridDims.x) * static_cast<size_t>(m_gridDims.y);
    std::vector<uint32_t> counts(cellCount, 0u);
    auto cellIndex = [&](const glm::ivec2& g) -> int {
        const glm::ivec2 l = g - gridMin;
        if (l.x < 0 || l.y < 0 || l.x >= m_gridDims.x || l.y >= m_gridDims.y) return -1;
        return l.y * m_gridDims.x + l.x;
    };
    for (const Chunk* c : chunks) {
        for (const LightFixture& l : c->lights()) {
            for (const glm::ivec2& g : l.visibleCells()) {
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
            for (const glm::ivec2& g : l.visibleCells()) {
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

    // ---- Edge map (one texel per world cell, +1 row/column for east/north edges) ----
    const int N = world::kChunkCells;
    const glm::ivec2 cellMin(minCx * N, minCz * N);
    m_edgeDims = glm::ivec2((maxCx - minCx + 1) * N + 1, (maxCz - minCz + 1) * N + 1);
    m_edgeOrigin = glm::vec2(cellMin) * world::kCellSize;
    std::vector<uint8_t> edges(static_cast<size_t>(m_edgeDims.x) * static_cast<size_t>(m_edgeDims.y) * 2);
    for (int z = 0; z < m_edgeDims.y; ++z) {
        for (int x = 0; x < m_edgeDims.x; ++x) {
            const size_t i = (static_cast<size_t>(z) * static_cast<size_t>(m_edgeDims.x) + static_cast<size_t>(x)) * 2;
            edges[i + 0] = static_cast<uint8_t>(generator.edge(cellMin.x + x, cellMin.y + z, world::EdgeAxis::West));
            edges[i + 1] = static_cast<uint8_t>(generator.edge(cellMin.x + x, cellMin.y + z, world::EdgeAxis::South));
        }
    }
    m_edgeMap.create(m_edgeDims.x, m_edgeDims.y, GL_RG8UI, GL_RG_INTEGER, GL_UNSIGNED_BYTE, edges.data(),
                     GL_NEAREST, GL_CLAMP_TO_EDGE);

    m_intensity.assign(m_lightCount, 1.0f);
}

void LightGrid::updateIntensities(double time) {
    size_t i = 0;
    for (const Chunk* c : m_order) {
        for (const LightFixture& l : c->lights()) {
            if (i < m_intensity.size()) m_intensity[i] = l.intensity(time);
            ++i;
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
}
