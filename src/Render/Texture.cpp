// ---------------------------------------------------------------------------
// Texture.cpp
// ---------------------------------------------------------------------------
#include "Render/Texture.h"

#include <algorithm>
#include <utility>

// Anisotropic filtering is core only in GL 4.6; on 3.3 it is exposed through
// the (near universal) EXT/ARB extension which shares these enum values.
#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#endif

// ----- Texture2D --------------------------------------------------------------

Texture2D::~Texture2D() { release(); }

Texture2D::Texture2D(Texture2D&& o) noexcept { *this = std::move(o); }

Texture2D& Texture2D::operator=(Texture2D&& o) noexcept {
    if (this != &o) {
        release();
        m_id     = std::exchange(o.m_id, 0);
        m_width  = std::exchange(o.m_width, 0);
        m_height = std::exchange(o.m_height, 0);
    }
    return *this;
}

void Texture2D::release() {
    if (m_id) glDeleteTextures(1, &m_id);
    m_id = 0;
}

void Texture2D::create(int width, int height, GLenum internalFormat, GLenum format, GLenum type,
                       const void* data, GLenum filter, GLenum wrap) {
    if (!m_id) glGenTextures(1, &m_id);
    m_width  = width;
    m_height = height;
    glBindTexture(GL_TEXTURE_2D, m_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), width, height, 0, format, type, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(wrap));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(wrap));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture2D::bind(GLuint unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_id);
}

// ----- TextureArray2D ---------------------------------------------------------

TextureArray2D::~TextureArray2D() {
    if (m_id) glDeleteTextures(1, &m_id);
}

void TextureArray2D::create(int size, int layers, GLenum internalFormat) {
    if (!m_id) glGenTextures(1, &m_id);
    m_size   = size;
    m_layers = layers;
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_id);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, static_cast<GLint>(internalFormat), size, size, layers, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void TextureArray2D::uploadLayer(int layer, const uint8_t* rgba) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, m_size, m_size, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void TextureArray2D::finalize(float maxAnisotropy) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_id);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (maxAnisotropy > 1.0f) {
        glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, maxAnisotropy);
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void TextureArray2D::bind(GLuint unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_id);
}

// ----- TextureBuffer ------------------------------------------------------------

TextureBuffer::~TextureBuffer() {
    if (m_texture) glDeleteTextures(1, &m_texture);
    if (m_buffer) glDeleteBuffers(1, &m_buffer);
}

void TextureBuffer::create(GLenum internalFormat) {
    m_format = internalFormat;
    if (!m_buffer) glGenBuffers(1, &m_buffer);
    if (!m_texture) glGenTextures(1, &m_texture);
    upload(nullptr, 0, GL_DYNAMIC_DRAW);
}

void TextureBuffer::upload(const void* data, size_t bytes, GLenum usage) {
    static const uint8_t kDummy[16] = {};
    const void* src  = bytes > 0 ? data : kDummy;
    const size_t size = std::max<size_t>(bytes, sizeof(kDummy));

    glBindBuffer(GL_TEXTURE_BUFFER, m_buffer);
    if (size > m_capacity) {
        // Grow and re-attach: a TBO view must be re-specified after reallocation.
        glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(size), src, usage);
        m_capacity = size;
        glBindTexture(GL_TEXTURE_BUFFER, m_texture);
        glTexBuffer(GL_TEXTURE_BUFFER, m_format, m_buffer);
        glBindTexture(GL_TEXTURE_BUFFER, 0);
    } else {
        glBufferSubData(GL_TEXTURE_BUFFER, 0, static_cast<GLsizeiptr>(size), src);
    }
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

void TextureBuffer::bind(GLuint unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_BUFFER, m_texture);
}
