// ---------------------------------------------------------------------------
// EntityRenderer.cpp
// ---------------------------------------------------------------------------
#include "Render/EntityRenderer.h"

#include "Render/ShaderSources.h"

void EntityDrawList::addSprite(const glm::vec3& centre, float radius, SpriteKind kind, float opacity,
                               const glm::vec3& camRight, const glm::vec3& camUp) {
    const uint32_t base = static_cast<uint32_t>(sprites.vertices.size());
    const glm::vec3 r = camRight * radius, u = camUp * radius;
    const glm::vec3 n = glm::normalize(glm::cross(camRight, camUp));
    const glm::vec2 corners[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (const glm::vec2& c : corners) {
        sprites.vertices.push_back({centre + r * c.x + u * c.y, n, c, static_cast<float>(kind), opacity});
    }
    sprites.indices.insert(sprites.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

bool EntityRenderer::init() {
    return m_shadowShader.build(shaders::kShadowVertex, shaders::kShadowFragment, "Shadow");
}

void EntityRenderer::drawLit(const EntityDrawList& list) {
    m_litMesh.stream(list.lit);
    if (!list.lit.empty()) m_litMesh.draw();
}

void EntityRenderer::drawShadow(const EntityDrawList& list, const glm::mat4& viewProj, const glm::vec3& cameraPos,
                                float time, const glm::vec3& fogColor, float fogDensity) {
    m_shadowMesh.stream(list.shadow);
    m_spriteMesh.stream(list.sprites);
    if (list.shadow.empty() && list.sprites.empty()) return;

    m_shadowShader.use();
    m_shadowShader.set("uViewProj", viewProj);
    m_shadowShader.set("uCameraPos", cameraPos);
    m_shadowShader.set("uTime", time);
    m_shadowShader.set("uFogColor", fogColor);
    m_shadowShader.set("uFogDensity", fogDensity);
    if (!list.shadow.empty()) m_shadowMesh.draw();
    if (!list.sprites.empty()) {
        glDisable(GL_CULL_FACE); // sprites are single quads
        m_spriteMesh.draw();
        glEnable(GL_CULL_FACE);
    }
    glBindVertexArray(0);
}
