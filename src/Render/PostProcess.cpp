// ---------------------------------------------------------------------------
// PostProcess.cpp
// ---------------------------------------------------------------------------
#include "Render/PostProcess.h"

#include "Render/ShaderSources.h"

#include <algorithm>
#include <iostream>

namespace {
constexpr int kMaxBloomMips = 6;
} // namespace

PostProcess::~PostProcess() {
    destroyTargets();
    if (m_emptyVao) glDeleteVertexArrays(1, &m_emptyVao);
}

bool PostProcess::init(int width, int height, int msaaSamples) {
    if (!m_downShader.build(shaders::kFullscreenVertex, shaders::kBloomDownFragment, "BloomDown") ||
        !m_upShader.build(shaders::kFullscreenVertex, shaders::kBloomUpFragment, "BloomUp") ||
        !m_compositeShader.build(shaders::kFullscreenVertex, shaders::kCompositeFragment, "Composite")) {
        return false;
    }
    glGenVertexArrays(1, &m_emptyVao);

    GLint maxSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    m_samples = std::clamp(msaaSamples, 1, static_cast<int>(maxSamples));

    m_width = std::max(width, 1);
    m_height = std::max(height, 1);
    createTargets();

    glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);
    const GLenum msaaStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (msaaStatus != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[PostProcess] HDR framebuffer incomplete (0x" << std::hex << msaaStatus << std::dec << ")\n";
        return false;
    }
    return true;
}

void PostProcess::resize(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (width == m_width && height == m_height) return;
    m_width = width;
    m_height = height;
    destroyTargets();
    createTargets();
}

void PostProcess::createTargets() {
    // ---- Multisampled HDR scene target -------------------------------------------
    glGenFramebuffers(1, &m_msaaFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);

    glGenRenderbuffers(1, &m_msaaColor);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaColor);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, GL_RGBA16F, m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msaaColor);

    glGenRenderbuffers(1, &m_msaaDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, GL_DEPTH_COMPONENT24, m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_msaaDepth);

    // ---- Single-sample resolve target (sampled by bloom + composite) --------------
    m_resolveColor.create(m_width, m_height, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, nullptr, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &m_resolveFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_resolveColor.id(), 0);

    // ---- Bloom mip chain (half resolution and below) --------------------------------
    int w = m_width, h = m_height;
    for (int i = 0; i < kMaxBloomMips; ++i) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        BloomMip mip;
        mip.width = w;
        mip.height = h;
        mip.texture.create(w, h, GL_R11F_G11F_B10F, GL_RGB, GL_FLOAT, nullptr, GL_LINEAR, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &mip.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, mip.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mip.texture.id(), 0);
        m_mips.push_back(std::move(mip));
        if (w == 1 && h == 1) break;
    }

    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PostProcess::destroyTargets() {
    for (BloomMip& m : m_mips) {
        if (m.fbo) glDeleteFramebuffers(1, &m.fbo);
    }
    m_mips.clear();
    if (m_resolveFbo) glDeleteFramebuffers(1, &m_resolveFbo);
    if (m_msaaFbo) glDeleteFramebuffers(1, &m_msaaFbo);
    if (m_msaaColor) glDeleteRenderbuffers(1, &m_msaaColor);
    if (m_msaaDepth) glDeleteRenderbuffers(1, &m_msaaDepth);
    m_resolveFbo = m_msaaFbo = m_msaaColor = m_msaaDepth = 0;
}

void PostProcess::beginScene(const glm::vec3& clearColor) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);
    glViewport(0, 0, m_width, m_height);
    glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void PostProcess::present(float time, float exposure, float bloomStrength, float bloomThreshold,
                          float crosshairHighlight) {
    // ---- Resolve MSAA -> single-sample HDR texture ------------------------------------
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(m_emptyVao);

    // ---- Bloom downsample chain ------------------------------------------------------
    m_downShader.use();
    m_downShader.set("uSource", 0);
    m_downShader.set("uThreshold", bloomThreshold);
    m_downShader.set("uKnee", bloomThreshold * 0.5f);
    const Texture2D* source = &m_resolveColor;
    for (size_t i = 0; i < m_mips.size(); ++i) {
        BloomMip& mip = m_mips[i];
        glBindFramebuffer(GL_FRAMEBUFFER, mip.fbo);
        glViewport(0, 0, mip.width, mip.height);
        source->bind(0);
        m_downShader.set("uTexel", glm::vec2(1.0f / static_cast<float>(source->width()),
                                             1.0f / static_cast<float>(source->height())));
        m_downShader.set("uPrefilter", i == 0 ? 1 : 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        source = &mip.texture;
    }

    // ---- Bloom upsample: accumulate each level into the next larger one ------------------
    m_upShader.use();
    m_upShader.set("uSource", 0);
    m_upShader.set("uRadius", 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (size_t i = m_mips.size() - 1; i > 0; --i) {
        const BloomMip& small = m_mips[i];
        BloomMip& large = m_mips[i - 1];
        glBindFramebuffer(GL_FRAMEBUFFER, large.fbo);
        glViewport(0, 0, large.width, large.height);
        small.texture.bind(0);
        m_upShader.set("uTexel", glm::vec2(1.0f / static_cast<float>(small.width),
                                           1.0f / static_cast<float>(small.height)));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDisable(GL_BLEND);

    // ---- Composite to the back buffer -------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
    m_compositeShader.use();
    m_resolveColor.bind(0);
    m_mips.front().texture.bind(1);
    m_compositeShader.set("uScene", 0);
    m_compositeShader.set("uBloom", 1);
    m_compositeShader.set("uExposure", exposure);
    m_compositeShader.set("uBloomStrength", bloomStrength);
    m_compositeShader.set("uTime", time);
    m_compositeShader.set("uResolution", glm::vec2(static_cast<float>(m_width), static_cast<float>(m_height)));
    m_compositeShader.set("uCrosshairHighlight", crosshairHighlight);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}
