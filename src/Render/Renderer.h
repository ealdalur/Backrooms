#pragma once
// ---------------------------------------------------------------------------
// Renderer.h
// Top-level render subsystem. Owns shaders, procedural materials, shared
// furniture/door meshes, the clustered light grid and the HDR post chain,
// and draws the loaded world from the player's camera each frame.
// ---------------------------------------------------------------------------

#include "Actors/Furniture.h"
#include "Render/Camera.h"
#include "Render/Frustum.h"
#include "Render/LightGrid.h"
#include "Render/MaterialLibrary.h"
#include "Render/Mesh.h"
#include "Render/PostProcess.h"
#include "Render/Shader.h"
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
    size_t lights = 0;
};

class Renderer {
public:
    /// Creates all GPU resources. Must be called with a current GL context.
    bool init(int width, int height);

    /// Handles back-buffer size changes.
    void resize(int width, int height);

    /// Renders one frame of the world into the default framebuffer.
    /// @param crosshairHighlight 0..1, shows the "interactable" ring.
    void render(const Camera& camera, ChunkManager& chunks, const WorldGenerator& generator, double time,
                float crosshairHighlight);

    /// Draws HUD text (e.g. the FPS meter) in the top-right corner of the
    /// back buffer. Call after render(), before swapping.
    void drawHudText(const std::string& text);

    const RenderStats& stats() const { return m_stats; }

private:
    Shader          m_worldShader;
    MaterialLibrary m_materials;
    LightGrid       m_lightGrid;
    PostProcess     m_post;
    TextOverlay     m_hud;
    Frustum         m_frustum;

    std::array<GpuMesh, kFurnitureTypeCount>                m_furnitureMeshes;
    std::array<std::vector<glm::mat4>, kFurnitureTypeCount> m_furnitureInstances;
    GpuMesh                                                 m_doorMesh;
    std::vector<glm::mat4>                                  m_doorInstances;

    uint64_t    m_lightTopology = ~0ull; ///< ChunkManager version the light grid was built for.
    int         m_width = 1;
    int         m_height = 1;
    RenderStats m_stats;
};
