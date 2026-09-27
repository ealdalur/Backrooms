#pragma once
// ---------------------------------------------------------------------------
// EntityRenderer.h
// Draws the procedurally posed entities. Their bodies are rebuilt on the CPU
// every frame (see Actors/CreatureRig) and streamed into small dynamic meshes:
//   * lit geometry (the Wanderer) is drawn with the world shader, so the
//     fluorescent lights, AO and fog treat it like any other object;
//   * the Stalker's body and its sprites (soot motes, eye glints) use the
//     dedicated shadow shader.
// ---------------------------------------------------------------------------

#include "Render/Mesh.h"
#include "Render/Shader.h"

#include <glm/glm.hpp>

/// Per-frame entity geometry, all in world space.
struct EntityDrawList {
    enum SpriteKind : int { Smoke = 1, Eye = 2 };

    MeshData lit;     ///< World-shaded bodies.
    MeshData shadow;  ///< Shadow-shaded bodies.
    MeshData sprites; ///< Camera-facing quads for the shadow shader.

    void clear() {
        lit.clear();
        shadow.clear();
        sprites.clear();
    }

    /// Appends a camera-facing quad of half-size `radius`. `opacity` is
    /// used by smoke motes.
    void addSprite(const glm::vec3& centre, float radius, SpriteKind kind, float opacity, const glm::vec3& camRight,
                   const glm::vec3& camUp);
};

class EntityRenderer {
public:
    bool init();

    /// Draws `list.lit` with the currently bound, fully configured world shader.
    void drawLit(const EntityDrawList& list);

    /// Draws the shadow bodies and sprites with the shadow shader.
    void drawShadow(const EntityDrawList& list, const glm::mat4& viewProj, const glm::vec3& cameraPos, float time,
                    const glm::vec3& fogColor, float fogDensity);

private:
    Shader  m_shadowShader;
    GpuMesh m_litMesh;
    GpuMesh m_shadowMesh;
    GpuMesh m_spriteMesh;
};
