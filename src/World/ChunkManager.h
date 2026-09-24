#pragma once
// ---------------------------------------------------------------------------
// ChunkManager.h
// Streams chunks of the infinite grid in and out around the player, keeps
// door states alive across unload/reload, answers collision queries and
// routes door interaction.
// ---------------------------------------------------------------------------

#include "Physics/Physics.h"
#include "World/Chunk.h"
#include "World/ChunkCoord.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class WorldGenerator;

class ChunkManager : public ICollisionWorld {
public:
    ChunkManager(const WorldGenerator& generator, int loadRadius, int buildBudget);

    /// Loads/unloads chunks around `focus` and animates doors.
    /// @param loadEverything  Ignore the per-frame build budget (initial load).
    void update(const glm::vec3& focus, float dt, const AABB& playerBox, bool loadEverything = false);

    // ICollisionWorld
    void gatherColliders(const AABB& region, std::vector<AABB>& out) const override;

    /// Door the player is looking at / standing next to, or nullptr.
    const Door* findInteractableDoor(const glm::vec3& eye, const glm::vec3& forward) const;

    /// Toggles the best interactable door. Returns true if one was toggled.
    bool interact(const glm::vec3& eye, const glm::vec3& forward, const glm::vec3& playerFeet);

    /// Finds a collision-free standing position near the world origin.
    glm::vec3 findSpawnPoint(const BodyShape& shape, const Physics& physics) const;

    /// Loaded chunks in deterministic (row-major coordinate) order.
    std::vector<const Chunk*> sortedChunks() const;
    std::vector<Chunk*> sortedChunksMutable();

    /// The loaded chunk at `c`, or nullptr.
    const Chunk* chunkAt(const ChunkCoord& c) const;

    /// Incremented whenever the set of loaded chunks changes.
    uint64_t topologyVersion() const { return m_version; }
    size_t chunkCount() const { return m_chunks.size(); }
    size_t pendingCount() const { return m_pending; }

    /// Door events (unlatch / swing / shut) raised during the last update().
    const std::vector<DoorEvent>& doorEvents() const { return m_doorEvents; }

private:
    Door* findDoorInternal(const glm::vec3& eye, const glm::vec3& forward) const;
    void loadChunk(const ChunkCoord& c);

    const WorldGenerator& m_generator;
    int m_loadRadius;
    int m_buildBudget;
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> m_chunks;
    std::unordered_map<uint64_t, int> m_doorMemory; ///< Door id -> persisted state.
    std::vector<DoorEvent> m_doorEvents;
    uint64_t m_version = 0;
    size_t m_pending = 0;
};
