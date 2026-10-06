// ---------------------------------------------------------------------------
// Renderer.cpp
// ---------------------------------------------------------------------------
#include "Render/Renderer.h"

#include "Actors/Door.h"
#include "Core/Config.h"
#include "Core/ConsoleLog.h"
#include "Render/ShaderSources.h"
#include "World/ChunkManager.h"
#include "World/WorldConstants.h"
#include "World/WorldGenerator.h"

namespace {
// Texture unit assignment for the world shader.
constexpr unsigned kUnitAlbedo    = 0;
constexpr unsigned kUnitSurface   = 1;
constexpr unsigned kUnitLightGrid = 2; // uses units 2..6

// Atmosphere (linear HDR). The fog colour doubles as the clear colour so the
// edge of the loaded world dissolves seamlessly into the haze.
struct Atmosphere {
    glm::vec3 ambient;     ///< Indirect light from above...
    glm::vec3 ambientDown; ///< ...and bounced up from the floor.
    glm::vec3 fog;
    float     fogDensity;
    glm::vec3 lightTint;   ///< Colour of the tubes.
};
// The Backrooms: humid, yellow, warm tubes.
const Atmosphere kBackrooms{{0.12f, 0.11f, 0.075f}, {0.42f, 0.37f, 0.22f}, {0.34f, 0.30f, 0.17f}, cfg::kFogDensity, {1.0f, 1.0f, 1.0f}};
// The office: conditioned air, grey carpet bounce, cold, bright tubes.
const Atmosphere kOffice{{0.11f, 0.12f, 0.13f}, {0.30f, 0.31f, 0.33f}, {0.30f, 0.32f, 0.35f}, 0.016f, {0.98f, 1.06f, 1.22f}};

const std::vector<LightDisturbance> kNoDisturbances;
} // namespace

bool Renderer::init(int width, int height) {
    m_width = width;
    m_height = height;

    if (!m_worldShader.build(shaders::kWorldVertex, shaders::kWorldFragment, "World")) return false;
    if (!m_materials.build(cfg::kTextureSize)) {
        con::line("RENDER", "Material generation failed", con::Level::Error);
        return false;
    }
    m_lightGrid.init();
    if (!m_post.init(width, height, cfg::kMsaaSamples)) return false;
    if (!m_hud.init()) return false;
    if (!m_entities.init()) return false;
    if (!m_lightning.init()) return false;
    if (!m_terminal.init()) {
        con::line("RENDER", "Terminal renderer initialisation failed", con::Level::Error);
        return false;
    }
    if (!m_title.init()) {
        con::line("RENDER", "Title screen initialisation failed", con::Level::Error);
        return false;
    }

    // Shared instanced meshes.
    for (int i = 0; i < kFurnitureTypeCount; ++i) {
        m_furnitureMeshes[static_cast<size_t>(i)].upload(Furniture::buildMesh(static_cast<FurnitureType>(i)), true);
    }
    m_doorMesh.upload(Door::buildMesh(), true);
    for (int i = 0; i < kTerminalLookCount; ++i) {
        m_terminalMeshes[static_cast<size_t>(i)].upload(Terminal::buildMesh(static_cast<TerminalLook>(i)), true);
    }
    for (int i = 0; i < kPhoneLookCount; ++i) {
        m_phoneMeshes[static_cast<size_t>(i)].upload(Phone::buildMesh(static_cast<PhoneLook>(i)), true);
    }
    for (int k = 0; k < Phone::kKeyCount; ++k) {
        m_phoneKeyMeshes[static_cast<size_t>(k)].upload(Phone::buildKeyMesh(k), true);
    }
    for (int i = 0; i < kPhoneLampCount; ++i) {
        m_phoneLampMeshes[static_cast<size_t>(i)].upload(Phone::buildLampMesh(static_cast<PhoneLamp>(i)), true);
    }
    m_cabinetMesh.upload(FileCabinet::buildShellMesh(), true);
    for (int v = 0; v < FileCabinet::kDrawerVariants; ++v) {
        m_drawerMeshes[static_cast<size_t>(v)].upload(FileCabinet::buildDrawerMesh(v), true);
    }
    for (int i = 0; i < kPartMeshCount; ++i) {
        m_partMeshes[static_cast<size_t>(i)].upload(tesla::buildMesh(static_cast<PartMesh>(i)), true);
    }

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
    m_worldShader.set("uLevelHeight", world::kLevelHeight);
    m_worldShader.set("uCeilingTile", world::kCeilingTileSize);
    m_worldShader.set("uLightRange", world::kLightRange);
    m_worldShader.set("uLightPower", cfg::kLightPower);
    m_worldShader.set("uAmbient", kBackrooms.ambient);
    m_worldShader.set("uAmbientDown", kBackrooms.ambientDown);
    m_worldShader.set("uFogColor", kBackrooms.fog);
    m_worldShader.set("uFogDensity", kBackrooms.fogDensity);
    m_worldShader.set("uLightTint", kBackrooms.lightTint);
    m_worldShader.set("uOffice", 0);
    m_worldShader.set("uDissolve", -1.0f);
    m_worldShader.set("uArcLight", glm::vec4(0.0f));
    glUseProgram(0);

    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        con::line("RENDER", con::format("OpenGL error during initialisation: {0x%x}", static_cast<unsigned>(err)), con::Level::Error);
        return false;
    }
    return true;
}

void Renderer::resize(int width, int height) {
    m_width = width > 0 ? width : 1;
    m_height = height > 0 ? height : 1;
    m_post.resize(m_width, m_height);
}

void Renderer::render(const FrameParams& frame, ChunkManager& chunks, const WorldGenerator& generator) {
    m_stats = RenderStats{};
    const Camera& camera = frame.camera;

    // ---- Keep light clustering in sync with the loaded chunk set --------------------
    std::vector<Chunk*> ordered = chunks.sortedChunksMutable();
    if (chunks.topologyVersion() != m_lightTopology) {
        m_lightGrid.rebuild(ordered, generator);
        m_lightTopology = chunks.topologyVersion();
    }
    m_lightGrid.updateIntensities(frame.time, frame.lightDisturbances ? *frame.lightDisturbances : kNoDisturbances);
    m_stats.lights = m_lightGrid.lightCount();

    // ---- Camera ---------------------------------------------------------------------
    const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    const glm::mat4 viewProj = camera.projectionMatrix(aspect) * camera.viewMatrix();
    m_frustum.update(viewProj);

    // ---- Scene pass (HDR, MSAA) --------------------------------------------------------
    const Atmosphere& air = frame.office ? kOffice : kBackrooms;
    m_post.beginScene(air.fog);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    m_worldShader.use();
    m_worldShader.set("uViewProj", viewProj);
    m_worldShader.set("uCameraPos", camera.position);
    m_worldShader.set("uTime", static_cast<float>(frame.time));
    m_worldShader.set("uArcLight", glm::vec4(frame.arcLight.position, frame.arcLight.intensity));
    m_worldShader.set("uArcColor", frame.arcLight.color);
    m_worldShader.set("uArcRange", std::max(frame.arcLight.range, 0.1f));
    m_worldShader.set("uAmbient", air.ambient);
    m_worldShader.set("uAmbientDown", air.ambientDown);
    m_worldShader.set("uFogColor", air.fog);
    m_worldShader.set("uFogDensity", air.fogDensity);
    m_worldShader.set("uLightTint", air.lightTint);
    m_worldShader.set("uOffice", frame.office ? 1 : 0);
    m_worldShader.set("uRingTime", frame.ringTime);
    // Writing on the walls, leaves and the glitching walls are cut out by
    // their coverage (written to alpha): smooth edges from the MSAA samples, no sorting.
    glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
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
    for (auto& list : m_terminalInstances) list.clear();
    for (auto& list : m_phoneInstances) list.clear();
    for (auto& list : m_phoneKeyInstances) list.clear();
    for (auto& list : m_phoneLampInstances) list.clear();
    for (auto& list : m_drawerInstances) list.clear();
    for (auto& list : m_partInstances) list.clear();
    m_doorInstances.clear();
    m_cabinetInstances.clear();

    for (const Chunk* chunk : ordered) {
        if (!m_frustum.isVisible(chunk->bounds())) continue;
        m_worldShader.set("uLightBase", chunk->lightBase());
        chunk->staticMesh().draw();
        ++m_stats.chunksDrawn;

        for (const FurnitureInstance& f : chunk->furniture()) {
            m_furnitureInstances[static_cast<size_t>(f.type)].push_back(f.model);
        }
        for (const Door& d : chunk->doors()) m_doorInstances.push_back(d.modelMatrix());
        for (const Terminal& t : chunk->terminals()) {
            m_terminalInstances[static_cast<size_t>(t.look())].push_back(t.modelMatrix());
        }
        for (const Phone& p : chunk->phones()) {
            m_phoneInstances[static_cast<size_t>(p.look())].push_back(p.modelMatrix());
            for (int k = 0; k < Phone::kKeyCount; ++k) m_phoneKeyInstances[static_cast<size_t>(k)].push_back(p.keyMatrix(k));
            m_phoneLampInstances[static_cast<size_t>(p.lamp())].push_back(p.lampMatrix());
        }
        const std::vector<FileCabinet>& cabinets = chunk->cabinets();
        for (const FileCabinet& c : cabinets) {
            m_cabinetInstances.push_back(c.modelMatrix());
            for (int d = 0; d < FileCabinet::kDrawers; ++d) {
                m_drawerInstances[static_cast<size_t>(c.drawerVariant(d))].push_back(c.drawerMatrix(d));
            }
        }
        for (const ItemSite& site : chunk->items()) {
            if (!site.item) continue;
            // Parts in a shut drawer cannot be seen.
            if (site.kind == SiteKind::Drawer && cabinets[static_cast<size_t>(site.cabinet)].drawerOpen(site.drawer) <= 0.0f) continue;
            m_partInstances[static_cast<size_t>(tesla::meshFor(*site.item))].push_back(site.world);
        }
    }

    // ---- Instanced furniture, doors, terminals and phones ------------------------------------------
    m_worldShader.set("uLightBase", 0);
    for (size_t t = 0; t < m_furnitureMeshes.size(); ++t) {
        m_furnitureMeshes[t].setInstances(m_furnitureInstances[t]);
        m_furnitureMeshes[t].drawInstanced();
        m_stats.furnitureDrawn += m_furnitureInstances[t].size();
    }
    m_doorMesh.setInstances(m_doorInstances);
    m_doorMesh.drawInstanced();
    m_stats.doorsDrawn = m_doorInstances.size();
    for (size_t t = 0; t < m_terminalMeshes.size(); ++t) {
        m_terminalMeshes[t].setInstances(m_terminalInstances[t]);
        m_terminalMeshes[t].drawInstanced();
        m_stats.terminalsDrawn += m_terminalInstances[t].size();
    }
    for (size_t t = 0; t < m_phoneMeshes.size(); ++t) {
        m_phoneMeshes[t].setInstances(m_phoneInstances[t]);
        m_phoneMeshes[t].drawInstanced();
        m_stats.phonesDrawn += m_phoneInstances[t].size();
    }
    for (size_t k = 0; k < m_phoneKeyMeshes.size(); ++k) {
        m_phoneKeyMeshes[k].setInstances(m_phoneKeyInstances[k]);
        m_phoneKeyMeshes[k].drawInstanced();
    }
    for (size_t l = 0; l < m_phoneLampMeshes.size(); ++l) {
        m_phoneLampMeshes[l].setInstances(m_phoneLampInstances[l]);
        m_phoneLampMeshes[l].drawInstanced();
    }
    m_cabinetMesh.setInstances(m_cabinetInstances);
    m_cabinetMesh.drawInstanced();
    m_stats.cabinetsDrawn = m_cabinetInstances.size();
    for (size_t v = 0; v < m_drawerMeshes.size(); ++v) {
        m_drawerMeshes[v].setInstances(m_drawerInstances[v]);
        m_drawerMeshes[v].drawInstanced();
    }
    for (size_t t = 0; t < m_partMeshes.size(); ++t) {
        if (m_partInstances[t].empty()) continue;
        m_partMeshes[t].setInstances(m_partInstances[t]);
        m_partMeshes[t].drawInstanced();
        m_stats.itemsDrawn += m_partInstances[t].size();
    }

    // ---- Entities: lit bodies through the world shader, then the shadow creature ----------
    if (frame.entities) {
        m_entities.drawLit(*frame.entities, m_worldShader);
        glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        m_entities.drawShadow(*frame.entities, viewProj, camera.position, static_cast<float>(frame.time), air.fog,
                              air.fogDensity);
    }
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glBindVertexArray(0);

    // ---- The Tesla gun: its discharges out in the world, then the gun itself on top ------------
    const glm::vec3 camRight = camera.right();
    const glm::vec3 camUp = glm::normalize(glm::cross(camRight, camera.forward()));
    static const std::vector<Bolt> kNoBolts;
    static const std::vector<Glow> kNoGlows;
    if (frame.bolts || frame.glows) {
        m_lightning.draw(frame.bolts ? *frame.bolts : kNoBolts, frame.glows ? *frame.glows : kNoGlows, viewProj,
                         camera.position, camRight, camUp, air.fogDensity);
    }
    if (frame.viewModel && !frame.viewModel->empty()) {
        // In the hands: never clipped by the wall the player stands against,
        // but lit by the room's own lights (it is drawn at its world position).
        glClear(GL_DEPTH_BUFFER_BIT);
        m_worldShader.use();
        for (const ViewModelPart& part : *frame.viewModel) {
            m_single.assign(1, part.model);
            GpuMesh& mesh = m_partMeshes[static_cast<size_t>(part.mesh)];
            mesh.setInstances(m_single);
            mesh.drawInstanced();
        }
        glBindVertexArray(0);
    }
    if (frame.muzzleGlow.intensity > 0.0f) {
        m_lightning.draw(kNoBolts, std::vector<Glow>{frame.muzzleGlow}, viewProj, camera.position, camRight, camUp,
                         air.fogDensity, false);
    }

    // ---- Post-processing to the back buffer -----------------------------------------------
    m_post.present(static_cast<float>(frame.time), cfg::kExposure, cfg::kBloomStrength, cfg::kBloomThreshold,
                   frame.crosshair, frame.crosshairHighlight, frame.fear, frame.fade, frame.dim, frame.glitch);
    m_hud.begin(m_width, m_height);
}

void Renderer::drawTerminal(const TerminalScreen& screen, const TerminalGraphics* graphics, float time, float dt,
                            float openAmount) {
    m_terminal.draw(screen, graphics, time, dt, openAmount, m_width, m_height);
}

void Renderer::resetTerminal() { m_terminal.reset(); }

void Renderer::drawTitle(float time, float fade) { m_title.draw(time, fade, m_width, m_height, m_hud); }

void Renderer::flushHud() { m_hud.flush(); }
