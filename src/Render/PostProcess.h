#pragma once
// ---------------------------------------------------------------------------
// PostProcess.h
// HDR pipeline: the scene renders into a multisampled RGBA16F framebuffer,
// is resolved, run through a physically-motivated bloom mip chain (13-tap
// downsample / tent upsample) and composited to the back buffer with ACES
// tonemapping, vignette, film grain and the crosshair.
// ---------------------------------------------------------------------------

#include "Render/Shader.h"
#include "Render/Texture.h"

#include <vector>

class PostProcess {
public:
    PostProcess() = default;
    ~PostProcess();
    PostProcess(const PostProcess&) = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    /// Compiles the post shaders and allocates targets.
    bool init(int width, int height, int msaaSamples);

    /// Reallocates render targets for a new back-buffer size.
    void resize(int width, int height);

    /// Binds the HDR multisampled target and clears it.
    void beginScene(const glm::vec3& clearColor);

    /// Resolves, blooms and composites into the default framebuffer.
    void present(float time, float exposure, float bloomStrength, float bloomThreshold, float crosshairHighlight);

private:
    struct BloomMip {
        GLuint    fbo = 0;
        Texture2D texture;
        int       width = 0;
        int       height = 0;
    };

    void createTargets();
    void destroyTargets();

    int m_width = 0;
    int m_height = 0;
    int m_samples = 4;

    GLuint m_msaaFbo = 0;
    GLuint m_msaaColor = 0; ///< Multisampled RGBA16F renderbuffer.
    GLuint m_msaaDepth = 0; ///< Multisampled depth renderbuffer.

    GLuint    m_resolveFbo = 0;
    Texture2D m_resolveColor;

    std::vector<BloomMip> m_mips;

    Shader m_downShader;
    Shader m_upShader;
    Shader m_compositeShader;
    GLuint m_emptyVao = 0; ///< Core profile requires a bound VAO even without attributes.
};
