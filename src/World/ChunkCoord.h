#pragma once
// ---------------------------------------------------------------------------
// ChunkCoord.h
// Integer (x, z) address of a chunk on the infinite grid, plus hashing so it
// can key unordered containers.
// ---------------------------------------------------------------------------

#include "Math/Random.h"
#include "World/WorldConstants.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

struct ChunkCoord {
    int32_t x = 0;
    int32_t z = 0;

    bool operator==(const ChunkCoord& o) const { return x == o.x && z == o.z; }
    bool operator!=(const ChunkCoord& o) const { return !(*this == o); }
    /// Strict weak ordering (row-major) for deterministic iteration.
    bool operator<(const ChunkCoord& o) const { return z < o.z || (z == o.z && x < o.x); }

    /// Chunk containing a world-space position.
    static ChunkCoord fromWorld(float wx, float wz) {
        return {static_cast<int32_t>(std::floor(wx / world::kChunkSize)),
                static_cast<int32_t>(std::floor(wz / world::kChunkSize))};
    }

    /// World-space x/z of the chunk's minimum corner.
    float originX() const { return static_cast<float>(x) * world::kChunkSize; }
    float originZ() const { return static_cast<float>(z) * world::kChunkSize; }
};

struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        return static_cast<size_t>(rnd::hashCombine(static_cast<uint32_t>(c.x), static_cast<uint32_t>(c.z)));
    }
};
