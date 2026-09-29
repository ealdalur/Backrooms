#pragma once
// ---------------------------------------------------------------------------
// EntityDirector.h
// Owns the anomalies and paces the horror. Responsibilities:
//   * spawning: each entity first appears after a grace period, somewhere
//     suitable around the player (the Stalker in a dark spot the player
//     cannot see; the Wanderer out of earshot somewhere ahead of where the
//     player is heading, so the first thing they notice is its muttering);
//   * resurfacing: entities are not bound to one storey - if the player
//     leaves the area or changes level, they fade out and manifest again
//     near the player a little later ("they always find you");
//   * perception: builds the player's real view frustum for the Stalker;
//   * outputs for other systems: catches, light disturbances, entity sounds,
//     the voice position, fear (0..1) and hints for the terminal voice;
//   * the Tesla gun's targets (IShockable): where an arc jumps to, which body
//     a bolt passes through (segment-vs-capsule tests against the posed
//     limbs) and the damage it does. An entity burnt to nothing is gone for
//     good: it is never spawned or resurfaced again.
// ---------------------------------------------------------------------------

#include "AI/NavGrid.h"
#include "AI/NoiseEvent.h"
#include "AI/Perception.h"
#include "AI/Stalker.h"
#include "AI/Wanderer.h"
#include "Gameplay/Shockable.h"
#include "Math/Random.h"
#include "Render/Camera.h"
#include "Render/LightGrid.h"

#include <optional>
#include <vector>

class ChunkManager;
class Physics;
class WorldGenerator;
struct EntityDrawList;

enum class EntityKind : uint8_t { Stalker, Wanderer };

class EntityDirector : public IShockable {
public:
    EntityDirector(const WorldGenerator& generator, const ChunkManager& chunks, uint64_t seed);

    /// Advances both entities.
    /// @param camera     The player's current view (for the Stalker's "am I seen").
    /// @param aspect     Viewport aspect ratio.
    /// @param viewBlocked The player cannot see the room (terminal, blackout).
    void update(float dt, const Camera& camera, float aspect, const glm::vec3& playerFeet, int playerLevel,
                bool viewBlocked, ChunkManager& chunks, const Physics& physics, const std::vector<NoiseEvent>& noises);

    /// Which entity reached the player since the last call, if any.
    std::optional<EntityKind> takeCatch();

    /// Stages the catch: the entity looms face to face in front of the
    /// player and everything holds still until scatter(). Returns its face.
    glm::vec3 confront(EntityKind kind, const glm::vec3& playerFeet, const glm::vec3& playerForward);

    /// Both entities vanish and return after a while (after a catch).
    void scatter();

    /// Places an entity right away (developer scenes). A placed Stalker can
    /// be made to start hunting at once with `hunt`.
    void spawnAt(EntityKind kind, const glm::vec3& feet, int level, float yaw, bool hunt = false);

    // IShockable: target ids are 0 = the Stalker, 1 = the Wanderer.
    int arcTarget(const glm::vec3& from, const glm::vec3& aim, float reach, float cosCone, glm::vec3& point) const override;
    int shockTest(const glm::vec3& a, const glm::vec3& b, float radius, glm::vec3& hit) const override;
    void applyShock(int target, float damage, const glm::vec3& at) override;

    /// An entity that finished vaporising since the last call, if any.
    std::optional<EntityKind> takeVaporised();
    /// Whether an entity has been destroyed for good.
    bool gone(EntityKind kind) const { return kind == EntityKind::Stalker ? m_stalkerGone : m_wandererGone; }

    /// Disables / enables the anomalies entirely, or just one of them (a destroyed one stays gone).
    void setEnabled(bool enabled);
    void setEnabled(EntityKind kind, bool enabled);

    void buildDrawList(EntityDrawList& list, const Camera& camera) const;
    const std::vector<LightDisturbance>& lightDisturbances() const { return m_disturbances; }
    const std::vector<EntitySound>& sounds() const { return m_sounds; }
    EntityAudioState audioState() const;

    /// Smoothed dread, 0..1: an entity close by, or the Stalker being watched.
    float fear() const { return m_fear; }

    /// Distances for the terminal voice (< 0 when absent / elsewhere).
    float stalkerDistance() const;
    float wandererDistance() const;
    /// The Stalker is close and the player is not looking at it.
    bool stalkerBehindPlayer() const;

    const Stalker& stalker() const { return m_stalker; }
    const Wanderer& wanderer() const { return m_wanderer; }

private:
    /// Collision view that omits doors (the Stalker slips under them).
    class NoDoorWorld : public ICollisionWorld {
    public:
        explicit NoDoorWorld(const ChunkManager& chunks) : m_chunks(chunks) {}
        void gatherColliders(const AABB& region, std::vector<AABB>& out) const override;

    private:
        const ChunkManager& m_chunks;
    };

    /// Tries to find a spot `minDist`..`maxDist` from the player on their
    /// storey, out of their sight, preferring darkness if asked and the
    /// direction `ahead` (unit, horizontal) if given.
    bool findSpawn(const PlayerView& view, float minDist, float maxDist, bool preferDark, const Physics& physics,
                   const ChunkManager& chunks, glm::vec3& out, const glm::vec3* ahead = nullptr);
    /// Removes an entity that drifted out of reach (farther than
    /// `maxDistance`, on another storey, unloaded, stuck); true if it did.
    bool manageLifetime(Agent& agent, float& timer, const PlayerView& view, const ChunkManager& chunks, bool canVanish,
                        float maxDistance);

    NavGrid  m_nav;
    rnd::Rng m_rng;
    Stalker  m_stalker;
    Wanderer m_wanderer;
    bool     m_stalkerEnabled = true;
    bool     m_wandererEnabled = true;
    bool     m_holding = false; ///< A catch is being staged: entities hold still.
    bool     m_stalkerGone = false;  ///< Vaporised: never comes back.
    bool     m_wandererGone = false;
    std::vector<EntityKind> m_vaporised;
    float    m_stalkerTimer;   ///< Countdown to the next (re)appearance.
    bool     m_stalkerResurface = false; ///< Next appearance is a close-range resurfacing.
    float    m_wandererTimer;
    std::optional<EntityKind> m_catch;

    PlayerView                    m_view;
    glm::vec3                     m_lastFeet{0.0f};
    glm::vec3                     m_travel{0.0f, 0.0f, -1.0f}; ///< Which way the player is heading (smoothed).
    std::vector<LightDisturbance> m_disturbances;
    std::vector<EntitySound>      m_sounds;
    float                         m_fear = 0.0f;
};
