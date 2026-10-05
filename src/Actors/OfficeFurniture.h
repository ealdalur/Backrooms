#pragma once
// ---------------------------------------------------------------------------
// OfficeFurniture.h
// Procedural models of the "real world" office's own furniture and clutter
// (executive desks and chairs, tables, plants, the water cooler, bins and the
// things on desks). The office reuses the Backrooms' desks, chairs,
// terminals and phones as they are; these are what it adds. Each model is a
// shared instanced mesh like the rest of Actors/Furniture (local +Z is the
// front, the origin is at floor level in the middle of the footprint).
// ---------------------------------------------------------------------------

#include "Actors/Furniture.h"
#include "Physics/AABB.h"
#include "Render/Mesh.h"

#include <vector>

namespace officefurniture {

/// The mesh of one of the office's own furniture types (empty for any other).
MeshData buildMesh(FurnitureType type);

/// Its local collision boxes (none for desk clutter).
std::vector<AABB> colliders(FurnitureType type);

/// Half extents of its footprint (x = width / 2, y = depth / 2).
glm::vec2 footprint(FurnitureType type);

} // namespace officefurniture
