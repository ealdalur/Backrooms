#pragma once
// ---------------------------------------------------------------------------
// Renderer.h
// Top-level render subsystem. Owns shaders, procedural materials, shared
// furniture/door/terminal/phone/cabinet/gun-part meshes, the clustered light
// grid, the entity renderer, the discharge renderer, the terminal CRT overlay
// and the HDR post chain, and draws the loaded world from the player's
// camera each frame - then the gun in the player's hands over it.
// ---------------------------------------------------------------------------

#include "Actors/FileCabinet.h"
#include "Actors/Furniture.h"
#include "Actors/Phone.h"
#include "Actors/Terminal.h"
#include "Actors/TeslaParts.h"
#include "Gameplay/Lightning.h"
#include "Gameplay/TeslaGun.h"
#include "Render/Camera.h"
#include "Render/EntityRenderer.h"
#include "Render/Frustum.h"
#include "Render/LightGrid.h"
#include "Render/LightningRenderer.h"
#include "Render/MaterialLibrary.h"
#include "Render/Mesh.h"
#include "Render/PostProcess.h"
#include "Render/Shader.h"
#include "Render/TerminalRenderer.h"
#include "Render/TextOverlay.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class ChunkManager;
class WorldGenerator;

/// Per-frame statistics for the window title / diagnostics.
struct RenderStats {
    size_t chunksDrawn = 0;
    size_t furnitureDrawn = 0;
    size_t doorsDrawn = 0;
    size_t terminalsDrawn = 0;
    size_t phonesDrawn = 0;
    size_t cabinetsDrawn = 0;
    size_t itemsDrawn = 0;
    size_t lights = 0;
};


/// Everything that varies per frame besides the world itself.
struct FrameParams {
    Camera camera;
    double time = 0.0;
    float  crosshair = 1.0f;                                  ///< 0..1, visibility of the crosshair.
    float  crosshairHighlight = 0.0f;                         ///< 0..1, shows the "interactable" ring.
    float  fear = 0.0f;                                        ///< 0..1, drives the dread post effects.
    float  fade = 0.0f;                                        ///< 0..1, fade to black.
    float  dim = 0.0f;                                         ///< 0..1, the game held under a dialog (blurred, darkened).
    const std::vector<LightDisturbance>* lightDisturbances = nullptr;
    const EntityDrawList*                entities = nullptr;
    // The Tesla gun.
    const std::vector<Bolt>*          bolts = nullptr;     ///< Discharges and spark streaks.
    const std::vector<Glow>*          glows = nullptr;     ///< Where arcs land.
    Glow                              muzzleGlow{glm::vec3(0.0f), 0.0f, 0.0f, glm::vec3(0.0f)}; ///< Corona round the spike.
    ArcLight                          arcLight;            ///< The light the discharge throws.
    const std::vector<ViewModelPart>* viewModel = nullptr; ///< Drawn last, over everything (depth cleared).
};

class Renderer {
public:
    /// Creates all GPU resources. Must be called with a current GL context.
    bool init(int width, int height);

    /// Handles back-buffer size changes.
    void resize(int width, int height);

    /// Renders one frame of the world into the default framebuffer.
    void render(const FrameParams& frame, ChunkManager& chunks, const WorldGenerator& generator);

    /// Draws a terminal's console (or its graphics-mode picture) as a CRT overlay. Call after render().
    void drawTerminal(const TerminalScreen& screen, const TerminalGraphics* graphics, float time, float dt, float openAmount);
    /// Clears the CRT persistence buffer (a different terminal is being used).
    void resetTerminal();

    /// HUD text overlay (FPS meter, prompts, messages). Queue with hud(),
    /// then call flushHud() after everything else, before swapping.
    TextOverlay& hud() { return m_hud; }
    void flushHud();

    const RenderStats& stats() const { return m_stats; }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    Shader           m_worldShader;
    MaterialLibrary  m_materials;
    LightGrid        m_lightGrid;
    PostProcess      m_post;
    TextOverlay      m_hud;
    Frustum          m_frustum;
    EntityRenderer    m_entities;
    LightningRenderer m_lightning;
    TerminalRenderer  m_terminal;

    std::array<GpuMesh, kFurnitureTypeCount>                m_furnitureMeshes;
    std::array<std::vector<glm::mat4>, kFurnitureTypeCount> m_furnitureInstances;
    GpuMesh                                                 m_doorMesh;
    std::vector<glm::mat4>                                  m_doorInstances;
    std::array<GpuMesh, kTerminalLookCount>                 m_terminalMeshes;
    std::array<std::vector<glm::mat4>, kTerminalLookCount>  m_terminalInstances;
    std::array<GpuMesh, kPhoneLookCount>                    m_phoneMeshes;
    std::array<std::vector<glm::mat4>, kPhoneLookCount>     m_phoneInstances;
    std::array<GpuMesh, Phone::kKeyCount>                   m_phoneKeyMeshes;    ///< One per key: each carries its own label.
    std::array<std::vector<glm::mat4>, Phone::kKeyCount>    m_phoneKeyInstances;
    std::array<GpuMesh, kPhoneLampCount>                    m_phoneLampMeshes;   ///< One per lamp state.
    std::array<std::vector<glm::mat4>, kPhoneLampCount>     m_phoneLampInstances;
    GpuMesh                                                 m_cabinetMesh;
    std::vector<glm::mat4>                                  m_cabinetInstances;
    std::array<GpuMesh, FileCabinet::kDrawerVariants>                m_drawerMeshes;
    std::array<std::vector<glm::mat4>, FileCabinet::kDrawerVariants> m_drawerInstances;
    std::array<GpuMesh, kPartMeshCount>                     m_partMeshes;
    std::array<std::vector<glm::mat4>, kPartMeshCount>      m_partInstances;
    std::vector<glm::mat4>                                  m_single; ///< Scratch: one view-model instance.

    uint64_t    m_lightTopology = ~0ull; ///< ChunkManager version the light grid was built for.
    int         m_width = 1;
    int         m_height = 1;
    RenderStats m_stats;
};
