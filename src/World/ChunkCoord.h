#pragma once
// ---------------------------------------------------------------------------
// ChunkCoord.h
// Integer (x, z, level) address of a chunk on the infinite, multi-storey
// grid, plus hashing so it can key unordered containers.
// ---------------------------------------------------------------------------

#include "Math/Random.h"
#include "World/WorldConstants.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

struct ChunkCoord {
    int32_t x = 0;
    int32_t z = 0;
    int32_t level = 0; ///< Storey index (0 = the original floor, negative = below).

    bool operator==(const ChunkCoord& o) const { return x == o.x && z == o.z && level == o.level; }
    bool operator!=(const ChunkCoord& o) const { return !(*this == o); }
    /// Strict weak ordering (level, then row-major) for deterministic iteration.
    bool operator<(const ChunkCoord& o) const {
        if (level != o.level) return level < o.level;
        return z < o.z || (z == o.z && x < o.x);
    }

    /// Chunk containing a world-space position on a given level.
    static ChunkCoord fromWorld(float wx, float wz, int32_t lvl) {
        return {static_cast<int32_t>(std::floor(wx / world::kChunkSize)),
                static_cast<int32_t>(std::floor(wz / world::kChunkSize)), lvl};
    }

    /// World-space x/z of the chunk's minimum corner, and its floor height.
    float originX() const { return static_cast<float>(x) * world::kChunkSize; }
    float originZ() const { return static_cast<float>(z) * world::kChunkSize; }
    float originY() const { return world::levelFloorY(level); }
};

struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        const uint64_t h = rnd::hashCombine(static_cast<uint32_t>(c.x), static_cast<uint32_t>(c.z));
        return static_cast<size_t>(rnd::hashCombine(h, static_cast<uint32_t>(c.level)));
    }
};
