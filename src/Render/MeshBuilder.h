#pragma once
// ---------------------------------------------------------------------------
// MeshBuilder.h
// Helpers that append procedural primitives (quads, boxes, cylinders) to a
// MeshData with correct CCW winding, normals and world-scaled planar UVs.
// Used for architecture, furniture, doors and light fixtures alike.
// ---------------------------------------------------------------------------

#include "Render/Mesh.h"
#include "Render/MaterialTypes.h"

#include <glm/glm.hpp>
#include <cstdint>

namespace mesh {

/// Bit mask selecting which faces of a box are emitted.
enum Face : uint8_t {
    FacePosX  = 1 << 0,
    FaceNegX  = 1 << 1,
    FacePosY  = 1 << 2,
    FaceNegY  = 1 << 3,
    FacePosZ  = 1 << 4,
    FaceNegZ  = 1 << 5,
    FaceSides = FacePosX | FaceNegX | FacePosZ | FaceNegZ,
    FaceAll   = FaceSides | FacePosY | FaceNegY,
};

/// Parameters for an (optionally transformed) axis-aligned box.
struct BoxDesc {
    glm::vec3  min{0.0f};                 ///< Local-space minimum corner.
    glm::vec3  max{1.0f};                 ///< Local-space maximum corner.
    MaterialId material = MaterialId::Wallpaper;
    uint8_t    faces    = FaceAll;        ///< Faces to emit (hidden faces can be skipped).
    glm::mat4  transform{1.0f};           ///< Rigid local -> mesh transform.
    glm::vec3  uvOrigin{0.0f};            ///< Subtracted from local positions before UV projection.
    bool       swapUV   = false;          ///< Rotate texture 90 degrees (e.g. vertical wood grain).
    float      tileSize = 0.0f;           ///< Metres per texture repeat; 0 -> material default.
    float      lightIndex = -1.0f;        ///< Emissive fixture index, -1 for none.
};

/// Appends a quad. Corners must be counter-clockwise when viewed from the
/// side the normal points to.
void addQuad(MeshData& mesh, const glm::vec3 corners[4], const glm::vec3& normal,
             const glm::vec2 uvs[4], MaterialId material, float lightIndex = -1.0f);

/// Appends a box with planar UVs derived from local positions (in metres
/// divided by the material tile size), so textures keep a constant texel
/// density regardless of the box size.
void addBox(MeshData& mesh, const BoxDesc& desc);

/// Appends a Y-aligned cylinder (local space) transformed by `transform`.
void addCylinder(MeshData& mesh, const glm::mat4& transform, float radius, float y0, float y1,
                 int segments, MaterialId material, bool caps = true);

} // namespace mesh
