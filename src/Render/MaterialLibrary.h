#pragma once
// ---------------------------------------------------------------------------
// MaterialLibrary.h
// Synthesises every material procedurally at start-up (noise / fractal math,
// no files on disk) and uploads them into two texture arrays:
//
//   Albedo array  (sRGB8_A8) : rgb = base colour
//   Surface array (RGBA8)    : r = height (bump), g = specular mask,
//                              b = emissive mask, a = unused
//
// Layer index == MaterialId, so a single draw call can mix materials.
// ---------------------------------------------------------------------------

#include "Render/MaterialTypes.h"
#include "Render/Texture.h"

#include <array>
#include <glm/glm.hpp>

class MaterialLibrary {
public:
    /// Generates all layers (in parallel) and uploads them. `size` must be a
    /// power of two. Returns false if texture allocation fails.
    bool build(int size);

    /// Binds the albedo and surface arrays to the given texture units.
    void bind(GLuint albedoUnit, GLuint surfaceUnit) const;

    /// Shader parameter block (see materialShaderParams) for every material.
    const std::array<glm::vec4, kMaterialCount>& shaderParams() const { return m_params; }

private:
    TextureArray2D m_albedo;
    TextureArray2D m_surface;
    std::array<glm::vec4, kMaterialCount> m_params{};
};
