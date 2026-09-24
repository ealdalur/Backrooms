#pragma once
// ---------------------------------------------------------------------------
// Texture.h
// Thin RAII wrappers over OpenGL texture objects:
//   * Texture2D       - render targets and small lookup textures.
//   * TextureArray2D  - the procedural material stacks (one layer per material).
//   * TextureBuffer   - buffer textures (TBOs) feeding light data to shaders.
// ---------------------------------------------------------------------------

#include "glad.h"

#include <cstddef>
#include <cstdint>

/// Owns a GL_TEXTURE_2D.
class Texture2D {
public:
    Texture2D() = default;
    ~Texture2D();
    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;
    Texture2D(Texture2D&& o) noexcept;
    Texture2D& operator=(Texture2D&& o) noexcept;

    /// (Re)allocates storage and optionally uploads `data`.
    void create(int width, int height, GLenum internalFormat, GLenum format, GLenum type,
                const void* data, GLenum filter, GLenum wrap);

    void bind(GLuint unit) const;
    GLuint id() const { return m_id; }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    void release();
    GLuint m_id = 0;
    int m_width = 0;
    int m_height = 0;
};

/// Owns a GL_TEXTURE_2D_ARRAY of RGBA8-class layers.
class TextureArray2D {
public:
    TextureArray2D() = default;
    ~TextureArray2D();
    TextureArray2D(const TextureArray2D&) = delete;
    TextureArray2D& operator=(const TextureArray2D&) = delete;

    /// Allocates `layers` square layers of `size` texels.
    void create(int size, int layers, GLenum internalFormat);

    /// Uploads one tightly packed RGBA8 layer.
    void uploadLayer(int layer, const uint8_t* rgba);

    /// Builds the mip chain and configures trilinear + anisotropic filtering.
    void finalize(float maxAnisotropy);

    void bind(GLuint unit) const;
    GLuint id() const { return m_id; }

private:
    GLuint m_id = 0;
    int m_size = 0;
    int m_layers = 0;
};

/// Owns a buffer object exposed to shaders as a samplerBuffer/usamplerBuffer.
class TextureBuffer {
public:
    TextureBuffer() = default;
    ~TextureBuffer();
    TextureBuffer(const TextureBuffer&) = delete;
    TextureBuffer& operator=(const TextureBuffer&) = delete;

    /// Creates the buffer + texture view with the given texel format
    /// (e.g. GL_RGBA32F, GL_R32F, GL_RG32UI, GL_R32UI).
    void create(GLenum internalFormat);

    /// Replaces the buffer contents. Empty uploads keep a 16-byte dummy so
    /// the texture view always references valid storage.
    void upload(const void* data, size_t bytes, GLenum usage);

    void bind(GLuint unit) const;

private:
    GLuint m_buffer = 0;
    GLuint m_texture = 0;
    GLenum m_format = GL_R32F;
    size_t m_capacity = 0;
};
