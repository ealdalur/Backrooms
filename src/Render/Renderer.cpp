// ---------------------------------------------------------------------------
// Renderer.cpp
// ---------------------------------------------------------------------------
#include "Render/Renderer.h"

#include "Actors/Door.h"
#include "Core/Config.h"
#include "Render/ShaderSources.h"
#include "World/ChunkManager.h"
#include "World/WorldConstants.h"
#include "World/WorldGenerator.h"

#include <iostream>

namespace {
// Texture unit assignment for the world shader.
constexpr unsigned kUnitAlbedo    = 0;
constexpr unsigned kUnitSurface   = 1;
constexpr unsigned kUnitLightGrid = 2; // uses units 2..6

// Atmosphere (linear HDR). The fog colour doubles as the clear colour so the
// edge of the loaded world dissolves seamlessly into the haze.
const glm::vec3 kAmbient(0.12f, 0.11f, 0.075f);
const glm::vec3 kAmbientDown(0.42f, 0.37f, 0.22f);
const glm::vec3 kFogColor(0.34f, 0.30f, 0.17f);
} // namespace

bool Renderer::init(int width, int height) {
    m_width = width;
    m_height = height;

    if (!m_worldShader.build(shaders::kWorldVertex, shaders::kWorldFragment, "World")) return false;
    if (!m_materials.build(cfg::kTextureSize)) {
        std::cerr << "[Renderer] Material generation failed\n";
        return false;
    }
    m_lightGrid.init();
    if (!m_post.init(width, height, cfg::kMsaaSamples)) return false;
    if (!m_hud.init()) return false;

    // Shared instanced meshes.
    for (int i = 0; i < kFurnitureTypeCount; ++i) {
        m_furnitureMeshes[static_cast<size_t>(i)].upload(Furniture::buildMesh(static_cast<FurnitureType>(i)), true);
    }
    m_doorMesh.upload(Door::buildMesh(), true);

    // Constant world shader state.
    m_worldShader.use();
    m_worldShader.set("uAlbedo", static_cast<int>(kUnitAlbedo));
    m_worldShader.set("uSurface", static_cast<int>(kUnitSurface));
    m_worldShader.setArray("uMaterialParams", m_materials.shaderParams().data(), kMaterialCount);
    m_worldShader.set("uCellSize", world::kCellSize);
    m_worldShader.set("uWallHalf", world::kWallHalf);
    m_worldShader.set("uArchHalfWidth", world::kArchWidth * 0.5f);
    m_worldShader.set("uDoorHalfWidth", world::kDoorOpeningWidth * 0.5f);
    m_worldShader.set("uCeilingHeight", world::kCeilingHeight);
    m_worldShader.set("uCeilingTile", world::kCeilingTileSize);
    m_worldShader.set("uLightRange", world::kLightRange);
    m_worldShader.set("uLightPower", cfg::kLightPower);
    m_worldShader.set("uAmbient", kAmbient);
    m_worldShader.set("uAmbientDown", kAmbientDown);
    m_worldShader.set("uFogColor", kFogColor);
    m_worldShader.set("uFogDensity", cfg::kFogDensity);
    glUseProgram(0);

    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "[Renderer] OpenGL error during initialisation: 0x" << std::hex << err << std::dec << '\n';
        return false;
    }
    return true;
}

void Renderer::resize(int width, int height) {
    m_width = width > 0 ? width : 1;
    m_height = height > 0 ? height : 1;
    m_post.resize(m_width, m_height);
}

void Renderer::render(const Camera& camera, ChunkManager& chunks, const WorldGenerator& generator, double time,
                      float crosshairHighlight) {
    m_stats = RenderStats{};

    // ---- Keep light clustering in sync with the loaded chunk set --------------------
    std::vector<Chunk*> ordered = chunks.sortedChunksMutable();
    if (chunks.topologyVersion() != m_lightTopology) {
        m_lightGrid.rebuild(ordered, generator);
        m_lightTopology = chunks.topologyVersion();
    }
    m_lightGrid.updateIntensities(time);
    m_stats.lights = m_lightGrid.lightCount();

    // ---- Camera ---------------------------------------------------------------------
    const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    const glm::mat4 viewProj = camera.projectionMatrix(aspect) * camera.viewMatrix();
    m_frustum.update(viewProj);

    // ---- Scene pass (HDR, MSAA) --------------------------------------------------------
    m_post.beginScene(kFogColor);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    m_worldShader.use();
    m_worldShader.set("uViewProj", viewProj);
    m_worldShader.set("uCameraPos", camera.position);
    m_materials.bind(kUnitAlbedo, kUnitSurface);
    m_lightGrid.bind(m_worldShader, kUnitLightGrid);

    // Static chunk meshes have no instance buffer: when attributes 4..7 are
    // disabled in the bound VAO, GL feeds the current generic attribute value,
    // which we set to the identity matrix (one shader for both paths).
    const glm::mat4 identity(1.0f);
    for (GLuint c = 0; c < 4; ++c) {
        glVertexAttrib4f(GpuMesh::kInstanceAttribute + c, identity[c][0], identity[c][1], identity[c][2], identity[c][3]);
    }

    for (auto& list : m_furnitureInstances) list.clear();
    m_doorInstances.clear();

    for (const Chunk* chunk : ordered) {
        if (!m_frustum.isVisible(chunk->bounds())) continue;
        m_worldShader.set("uLightBase", chunk->lightBase());
        chunk->staticMesh().draw();
        ++m_stats.chunksDrawn;

        for (const FurnitureInstance& f : chunk->furniture()) {
            m_furnitureInstances[static_cast<size_t>(f.type)].push_back(f.model);
        }
        for (const Door& d : chunk->doors()) m_doorInstances.push_back(d.modelMatrix());
    }

    // ---- Instanced furniture and doors ----------------------------------------------------
    m_worldShader.set("uLightBase", 0);
    for (size_t t = 0; t < m_furnitureMeshes.size(); ++t) {
        m_furnitureMeshes[t].setInstances(m_furnitureInstances[t]);
        m_furnitureMeshes[t].drawInstanced();
        m_stats.furnitureDrawn += m_furnitureInstances[t].size();
    }
    m_doorMesh.setInstances(m_doorInstances);
    m_doorMesh.drawInstanced();
    m_stats.doorsDrawn = m_doorInstances.size();
    glBindVertexArray(0);

    // ---- Post-processing to the back buffer -----------------------------------------------
    m_post.present(static_cast<float>(time), cfg::kExposure, cfg::kBloomStrength, cfg::kBloomThreshold,
                   crosshairHighlight);
}

void Renderer::drawHudText(const std::string& text) {
    m_hud.drawTopRight(text, m_width, m_height);
}
