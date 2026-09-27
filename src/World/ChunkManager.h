#pragma once
// ---------------------------------------------------------------------------
// ChunkManager.h
// Streams chunks of the infinite, multi-storey grid in and out around the
// player: a full radius on the player's storey and a small one on the
// storeys directly above and below (enough that a stairwell, and whatever
// is visible through its entrances, is always loaded on both ends). Keeps
// door and terminal states alive across unload/reload, answers collision
// queries and finds interaction targets.
// ---------------------------------------------------------------------------

#include "Physics/Physics.h"
#include "World/Chunk.h"
#include "World/ChunkCoord.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class WorldGenerator;

/// Something the player can use with the interact key.
struct Interactable {
    enum class Kind : uint8_t { None, Door, Terminal };
    Kind      kind = Kind::None;
    Door*     door = nullptr;
    Terminal* terminal = nullptr;

    explicit operator bool() const { return kind != Kind::None; }
};

class ChunkManager : public ICollisionWorld {
public:
    /// @param loadRadius     Chebyshev radius (chunks) loaded on the focus storey.
    /// @param adjacentRadius Radius loaded on the storeys directly above and below.
    /// @param buildBudget    Max chunks built per update (unless loading everything).
    ChunkManager(const WorldGenerator& generator, int loadRadius, int adjacentRadius, int buildBudget);

    /// Loads/unloads chunks around `focus` on storey `focusLevel` and animates doors.
    /// @param loadEverything  Ignore the per-frame build budget (initial load, teleports).
    void update(const glm::vec3& focus, int focusLevel, float dt, const AABB& playerBox, bool loadEverything = false);

    // ICollisionWorld
    void gatherColliders(const AABB& region, std::vector<AABB>& out) const override;
    /// Same, optionally skipping doors (for things that slip under them).
    void gatherColliders(const AABB& region, std::vector<AABB>& out, bool includeDoors) const;

    /// Best door or terminal in front of / next to the eye, or none.
    Interactable findInteractable(const glm::vec3& eye, const glm::vec3& forward) const;

    /// The door hanging in a global edge on a storey, if its chunk is loaded.
    Door* doorOnEdge(int level, int gx, int gz, world::EdgeAxis axis);
    const Door* doorOnEdge(int level, int gx, int gz, world::EdgeAxis axis) const;

    /// A loaded terminal by id, or nullptr.
    Terminal* terminalById(uint64_t id);

    /// Finds a collision-free standing position on `level` near `near`
    /// (spiralling outward over cell centres and quarter points).
    glm::vec3 findSpawnPoint(const glm::vec3& near, int level, const BodyShape& shape, const Physics& physics) const;

    /// Loaded chunks in deterministic (level, row-major) order.
    std::vector<const Chunk*> sortedChunks() const;
    std::vector<Chunk*> sortedChunksMutable();

    /// The loaded chunk at `c`, or nullptr.
    const Chunk* chunkAt(const ChunkCoord& c) const;
    /// True if the chunk under world x/z on `level` is loaded.
    bool isLoadedAt(const glm::vec3& p, int level) const;

    /// Incremented whenever the set of loaded chunks changes.
    uint64_t topologyVersion() const { return m_version; }
    size_t chunkCount() const { return m_chunks.size(); }
    size_t pendingCount() const { return m_pending; }

    /// Door events (unlatch / swing / shut) raised during the last update().
    const std::vector<DoorEvent>& doorEvents() const { return m_doorEvents; }

private:
    void loadChunk(const ChunkCoord& c);
    void rememberState(const Chunk& chunk);
    int radiusFor(int levelOffset) const;

    const WorldGenerator& m_generator;
    int m_loadRadius;
    int m_adjacentRadius;
    int m_buildBudget;
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> m_chunks;
    std::unordered_map<uint64_t, int>  m_doorMemory;     ///< Door id -> persisted state.
    std::unordered_map<uint64_t, bool> m_terminalMemory; ///< Terminal id -> powered.
    std::vector<DoorEvent> m_doorEvents;
    uint64_t m_version = 0;
    size_t m_pending = 0;
};
