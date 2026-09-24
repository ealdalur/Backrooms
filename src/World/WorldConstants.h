#pragma once
// ---------------------------------------------------------------------------
// WorldConstants.h
// Physical dimensions of the Backrooms architecture. The world is a regular
// grid of square cells; walls live on cell edges; chunks group NxN cells.
// ---------------------------------------------------------------------------

#include <cstdint>

namespace world {

inline constexpr float kCellSize      = 5.0f;                    ///< Metres per cell edge.
inline constexpr int   kChunkCells    = 5;                       ///< Cells per chunk edge.
inline constexpr float kChunkSize     = kCellSize * kChunkCells; ///< 25 m.
inline constexpr float kCeilingHeight = 2.9f;
inline constexpr float kWallThickness = 0.20f;
inline constexpr float kWallHalf      = kWallThickness * 0.5f;

// Openings -----------------------------------------------------------------
inline constexpr float kArchWidth         = 2.0f;   ///< Clear width of an archway.
inline constexpr float kArchHeight        = 2.35f;
inline constexpr float kDoorOpeningWidth  = 1.0f;   ///< Rough opening incl. frame.
inline constexpr float kDoorOpeningHeight = 2.15f;
inline constexpr float kDoorFrameWidth    = 0.05f;  ///< Jamb face width.
inline constexpr float kDoorFrameProtrude = 0.012f; ///< Jamb depth beyond wall faces.
inline constexpr float kDoorThickness     = 0.045f;
inline constexpr float kDoorPanelWidth    = kDoorOpeningWidth - 2.0f * kDoorFrameWidth - 0.008f;
inline constexpr float kDoorPanelHeight   = kDoorOpeningHeight - kDoorFrameWidth - 0.012f;
inline constexpr float kDoorFloorGap      = 0.01f;

// Pillars / ceiling ---------------------------------------------------------
inline constexpr float kPillarSize      = 0.70f;  ///< Free-standing structural pillars.
inline constexpr float kPillarChance    = 0.16f;  ///< Probability at wall-free vertices.
inline constexpr float kCeilingTileSize = 0.625f; ///< 8 tiles per cell edge.
inline constexpr float kFixtureWidth    = 0.60f;  ///< Troffer short side.
inline constexpr float kFixtureLength   = 1.225f; ///< Troffer long side.
inline constexpr float kFixtureDepth    = 0.025f; ///< Protrusion below the ceiling.

// Lighting ------------------------------------------------------------------
inline constexpr float kLightRange    = 7.5f;             ///< Hard cut-off radius (m).
inline constexpr float kLightGridCell = kCellSize * 0.5f; ///< World-space light grid resolution.

/// Wall-edge classification shared by generation, collision and the shader.
enum class EdgeType : uint8_t {
    Open    = 0, ///< No wall at all (open-plan).
    Wall    = 1, ///< Solid wall.
    Archway = 2, ///< Wall with a wide, doorless rectangular opening.
    Door    = 3, ///< Wall with a framed, interactable door.
};

/// Which cell edge a global edge coordinate refers to.
///  West : the line x = gx * cellSize spanning z in [gz, gz+1) * cellSize.
///  South: the line z = gz * cellSize spanning x in [gx, gx+1) * cellSize.
enum class EdgeAxis : uint8_t { West = 0, South = 1 };

/// Integer division rounding toward negative infinity.
inline constexpr int floorDiv(int a, int b) {
    return (a >= 0) ? (a / b) : -(((-a) + b - 1) / b);
}

/// Non-negative modulo matching floorDiv.
inline constexpr int floorMod(int a, int b) {
    return a - floorDiv(a, b) * b;
}

} // namespace world
