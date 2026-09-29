#pragma once
// ---------------------------------------------------------------------------
// LightningRenderer.h
// Draws the Tesla gun's discharges into the HDR scene. Every bolt polyline
// is expanded on the CPU into a camera-facing ribbon (each point pushed out
// sideways, perpendicular to both the bolt and the view ray, so the strip
// always shows its full width), tapering from the root to the tip and
// fading over the bolt's short life; glows are camera-facing quads. Both
// are blended additively (no sorting needed), depth-tested against the
// world but not written, so walls hide what is behind them and the bloom
// pass turns the white-hot cores into dazzling streaks.
// ---------------------------------------------------------------------------

#include "Gameplay/Lightning.h"
#include "Render/Mesh.h"
#include "Render/Shader.h"

#include <glm/glm.hpp>
#include <vector>

class LightningRenderer {
public:
    bool init();

    /// Draws bolts and glows. With `depthTest` off they show through
    /// everything (the corona round the spike, over the gun itself).
    void draw(const std::vector<Bolt>& bolts, const std::vector<Glow>& glows, const glm::mat4& viewProj,
              const glm::vec3& cameraPos, const glm::vec3& camRight, const glm::vec3& camUp, float fogDensity,
              bool depthTest = true);

private:
    void addRibbon(const Bolt& bolt, const glm::vec3& cameraPos, const glm::vec3& camRight);
    void addGlow(const Glow& glow, const glm::vec3& camRight, const glm::vec3& camUp);

    Shader   m_shader;
    GpuMesh  m_mesh;
    MeshData m_data; ///< Reused every frame.
};
