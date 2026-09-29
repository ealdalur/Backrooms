// ---------------------------------------------------------------------------
// LightningRenderer.cpp
// ---------------------------------------------------------------------------
#include "Render/LightningRenderer.h"

#include "Render/ShaderSources.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kGlowSpread = 4.0f; ///< Ribbon half-width as a multiple of the core's.
constexpr float kRibbon = 0.0f, kBall = 1.0f;
} // namespace

bool LightningRenderer::init() { return m_shader.build(shaders::kLightningVertex, shaders::kLightningFragment, "Lightning"); }

void LightningRenderer::addRibbon(const Bolt& bolt, const glm::vec3& cameraPos, const glm::vec3& camRight) {
    const std::vector<glm::vec3>& p = bolt.points;
    if (p.size() < 2) return;
    // A fresh bolt flashes, then fades over its short life.
    const float fade = std::max(0.0f, 1.0f - bolt.age / bolt.life);
    const float bright = bolt.intensity * std::sqrt(fade);
    if (bright <= 0.001f) return;

    const uint32_t base = static_cast<uint32_t>(m_data.vertices.size());
    const size_t n = p.size();
    for (size_t i = 0; i < n; ++i) {
        const glm::vec3 tangent = p[std::min(i + 1, n - 1)] - p[i > 0 ? i - 1 : 0];
        const glm::vec3 toCamera = cameraPos - p[i];
        glm::vec3 side = glm::cross(tangent, toCamera);
        const float len = glm::length(side);
        side = len > 1e-6f ? side / len : camRight; // bolt pointing straight at the camera
        const float t = static_cast<float>(i) / static_cast<float>(n - 1);
        const float halfWidth = bolt.width * glm::mix(1.0f, bolt.taper, t) * kGlowSpread;
        for (float s : {-1.0f, 1.0f}) {
            m_data.vertices.push_back({p[i] + side * (halfWidth * s), bolt.color, glm::vec2(s, t), bright, kRibbon});
        }
    }
    for (uint32_t i = 0; i + 1 < static_cast<uint32_t>(n); ++i) {
        const uint32_t a = base + 2 * i;
        m_data.indices.insert(m_data.indices.end(), {a, a + 1, a + 3, a, a + 3, a + 2});
    }
}

void LightningRenderer::addGlow(const Glow& glow, const glm::vec3& camRight, const glm::vec3& camUp) {
    if (glow.intensity <= 0.001f || glow.radius <= 0.0f) return;
    const uint32_t base = static_cast<uint32_t>(m_data.vertices.size());
    const glm::vec2 corners[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (const glm::vec2& c : corners) {
        const glm::vec3 pos = glow.position + (camRight * c.x + camUp * c.y) * glow.radius;
        m_data.vertices.push_back({pos, glow.color, c, glow.intensity, kBall});
    }
    m_data.indices.insert(m_data.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void LightningRenderer::draw(const std::vector<Bolt>& bolts, const std::vector<Glow>& glows, const glm::mat4& viewProj,
                             const glm::vec3& cameraPos, const glm::vec3& camRight, const glm::vec3& camUp,
                             float fogDensity, bool depthTest) {
    m_data.clear();
    for (const Bolt& b : bolts) addRibbon(b, cameraPos, camRight);
    for (const Glow& g : glows) addGlow(g, camRight, camUp);
    if (m_data.empty()) return;
    m_mesh.stream(m_data);

    m_shader.use();
    m_shader.set("uViewProj", viewProj);
    m_shader.set("uCameraPos", cameraPos);
    m_shader.set("uFogDensity", fogDensity);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    if (!depthTest) glDisable(GL_DEPTH_TEST);
    m_mesh.draw();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}
