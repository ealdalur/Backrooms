#pragma once
// ---------------------------------------------------------------------------
// EntityRenderer.h
// Draws the procedurally posed entities. Their bodies are rebuilt on the CPU
// every frame (see Actors/CreatureRig) and streamed into small dynamic meshes:
//   * lit geometry (the Wanderer) is drawn with the world shader, so the
//     fluorescent lights, AO and fog treat it like any other object;
//   * the Stalker's body and its sprites (soot motes, eye glints, the embers
//     of a vaporising body) use the dedicated shadow shader.
// A body that is being vaporised is streamed separately and drawn with the
// dissolve uniforms set (noise-threshold discard with glowing edges, eaten
// away from the head down), through whichever shader it normally uses.
// ---------------------------------------------------------------------------

#include "Render/Mesh.h"
#include "Render/Shader.h"

#include <glm/glm.hpp>

/// Per-frame entity geometry, all in world space.
struct EntityDrawList {
    enum SpriteKind : int { Smoke = 1, Eye = 2, Ember = 3 };

    /// A body being vaporised.
    struct Dissolving {
        MeshData  mesh;
        float     amount = -1.0f;  ///< 0..1 vaporised so far (< 0: none this frame).
        glm::vec3 feet{0.0f};      ///< Base of the body...
        float     height = 2.0f;   ///< ...and its height: it goes from the head down.
    };

    MeshData   lit;           ///< World-shaded bodies.
    MeshData   shadow;        ///< Shadow-shaded bodies.
    MeshData   sprites;       ///< Camera-facing quads for the shadow shader.
    Dissolving litDissolve;   ///< A world-shaded body vaporising.
    Dissolving shadowDissolve; ///< A shadow-shaded body vaporising.

    void clear() {
        lit.clear();
        shadow.clear();
        sprites.clear();
        litDissolve.mesh.clear();
        litDissolve.amount = -1.0f;
        shadowDissolve.mesh.clear();
        shadowDissolve.amount = -1.0f;
    }

    /// Appends a camera-facing quad of half-size `radius`. `opacity` is
    /// used by smoke motes.
    void addSprite(const glm::vec3& centre, float radius, SpriteKind kind, float opacity, const glm::vec3& camRight,
                   const glm::vec3& camUp);
};

class EntityRenderer {
public:
    bool init();

    /// Draws `list.lit` (and a vaporising lit body) with the bound, fully
    /// configured world shader.
    void drawLit(const EntityDrawList& list, const Shader& worldShader);

    /// Draws the shadow bodies and sprites with the shadow shader.
    void drawShadow(const EntityDrawList& list, const glm::mat4& viewProj, const glm::vec3& cameraPos, float time,
                    const glm::vec3& fogColor, float fogDensity);

private:
    Shader  m_shadowShader;
    GpuMesh m_litMesh;
    GpuMesh m_shadowMesh;
    GpuMesh m_spriteMesh;
    GpuMesh m_litDissolveMesh;
    GpuMesh m_shadowDissolveMesh;
};
