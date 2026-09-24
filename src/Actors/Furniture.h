#pragma once
// ---------------------------------------------------------------------------
// Furniture.h
// Procedurally modelled office furniture. Each type has one shared indexed
// mesh (rendered with instancing) and a set of local-space collision boxes.
// Instances store their placement and derived world-space colliders.
// ---------------------------------------------------------------------------

#include "Physics/AABB.h"
#include "Render/Mesh.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

enum class FurnitureType : uint8_t {
    Desk = 0,    ///< Steel desk with laminate top and drawer pedestal.
    Chair,       ///< Five-star swivel task chair.
    FileCabinet, ///< Three-drawer vertical filing cabinet.
    Partition,   ///< Fabric cubicle partition panel.
    Count
};

inline constexpr int kFurnitureTypeCount = static_cast<int>(FurnitureType::Count);

/// One placed piece of furniture.
struct FurnitureInstance {
    FurnitureType type = FurnitureType::Desk;
    glm::vec3     position{0.0f}; ///< Floor-level centre of the footprint.
    float         yaw = 0.0f;     ///< Rotation about +Y (radians); local +Z is the "front".
    glm::mat4     model{1.0f};    ///< Cached model matrix.
};

class Furniture {
public:
    /// Creates an instance with its model matrix computed.
    static FurnitureInstance makeInstance(FurnitureType type, const glm::vec3& position, float yaw);

    /// Builds the local-space mesh of a furniture type (origin at floor centre).
    static MeshData buildMesh(FurnitureType type);

    /// Local-space collision boxes of a furniture type.
    static const std::vector<AABB>& localColliders(FurnitureType type);

    /// Half extents of the type's footprint (x = width/2, y = depth/2), used by placement.
    static glm::vec2 footprintHalfExtents(FurnitureType type);

    /// Appends the world-space colliders of an instance (AABB of each rotated box).
    static void appendWorldColliders(const FurnitureInstance& instance, std::vector<AABB>& out);
};
