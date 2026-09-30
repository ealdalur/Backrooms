#pragma once
// ---------------------------------------------------------------------------
// Stalker.h
// The dark creature. A tall, spindly shadow that only moves while the player
// is not watching it:
//
//   Lurking  - stands upright in a dark spot, head cocked towards the player,
//              waiting. Then it starts to hunt.
//   Stalking - drops onto all fours and scuttles towards the player faster
//              than they can run, routing through dark cells and around the
//              player's field of view so it arrives from behind; the last
//              few metres it creeps. It follows the player to other storeys
//              up and down the stairwells, and opens the doors in its way.
//   Frozen   - the instant it is seen it stops dead, mid-stride. Stare long
//              enough (or come close) and...
//   Fleeing  - ...it darts, very fast, to the nearest spot the player cannot
//              see that it can reach without stopping: behind a corner, a
//              wall, through a doorway that stands open. Stared down for the
//              third time, it bolts much farther - to a dark spot 10-20 m
//              from the player, out of their sight - and lies low there for
//              a long while before it hunts again.
//   Lunging  - within reach and unseen, it rushes the player.
//
// "Seen" is a real perception test: the body's bounds against the player's
// view frustum (frustum culling turned into an AI sense), line of sight past
// walls and live door states (in and around a stairwell, a true 3D sight
// line past the flights, landing and slabs), and darkness - in an unlit area
// it cannot be made out beyond a few metres. Closed doors stop it like
// anything else: it opens them. It drains the fluorescent tubes around it.
//
// The Tesla gun's arc stops it dead: it rears up screeching, convulsing, and
// the moment the arc lets go it darts for cover.
// ---------------------------------------------------------------------------

#include "AI/Agent.h"
#include "AI/Perception.h"
#include "Actors/CreatureRig.h"
#include "Math/Random.h"
#include "Render/LightGrid.h"

#include <vector>

class ChunkManager;
struct EntityDrawList;

class Stalker : public Agent {
public:
    enum class State : uint8_t { Lurking, Stalking, Frozen, Fleeing, Lunging };

    explicit Stalker(uint64_t seed);

    void place(const glm::vec3& feet, int level, float yaw) override;

    /// Advances perception, behaviour and animation. Returns true if it
    /// reached the player this frame.
    bool update(float dt, const PlayerView& player, const NavGrid& nav, ChunkManager& chunks, const Physics& physics,
                std::vector<EntitySound>& sounds);

    /// Cuts short its lurking: it starts hunting right away.
    void provoke();

    /// The catch: it rears up to its full height, face to face with the
    /// player. Returns where its face is.
    glm::vec3 confront(const glm::vec3& playerFeet, const glm::vec3& playerForward);

    State state() const { return m_state; }
    bool seen() const { return m_seen; }
    /// Times it has been stared down since it last bolted far away.
    int stareDowns() const { return m_flees; }
    /// Bolting far away after one stare-down too many.
    bool retreating() const { return m_retreating; }
    /// True when it is moving fast (skittering) rather than holding still.
    bool moving() const;

    /// Nearby tubes sag and stutter.
    LightDisturbance lightDisturbance() const;

    const CreatureRig& body() const override { return m_rig; }

    /// Appends the shadow body, soot motes and eye glints (or the vaporising body and its embers).
    void buildGeometry(EntityDrawList& list, const glm::vec3& camRight, const glm::vec3& camUp) const;

private:
    struct Mote {
        glm::vec3 pos, vel;
        float     age, life, size;
    };

    bool isSeenBy(const PlayerView& player, const NavGrid& nav, const ICollisionWorld& world, const Physics& physics) const;
    NavProfile huntProfile(const PlayerView& player, const NavGrid& nav) const;
    /// How it darts for cover: no stopping to open doors.
    static NavProfile fleeProfile();
    bool findCover(const PlayerView& player, const NavGrid& nav, glm::vec3& out) const;
    /// Where it bolts after one stare-down too many: a dark spot kStalkerRetreatMin..Max
    /// from the player, out of their sight, that it can dash to without stopping.
    bool findRetreat(const PlayerView& player, const NavGrid& nav, glm::vec3& out) const;
    /// Darts for cover. `stareDown`: the player's gaze drove it off (it counts those).
    void startFleeing(const PlayerView& player, const NavGrid& nav, std::vector<EntitySound>& sounds, bool stareDown = false);
    /// A straight rush at the player is possible (flat run on a storey, or a clear 3D line in a stairwell).
    bool canRush(const PlayerView& player, const NavGrid& nav, const ICollisionWorld& world, const Physics& physics) const;
    void freeze();
    void animate(float dt, const PlayerView& player);
    void pose();
    void updateMotes(float dt);

    rnd::Rng m_rng;
    State    m_state = State::Lurking;
    bool     m_seen = false;
    float    m_timer = 0.0f;     ///< State-specific countdown / count-up.
    float    m_exposure = 0.0f;  ///< How long it has been stared at recently.
    float    m_replan = 0.0f;
    float    m_skitter = 0.0f;
    int      m_coverTries = 0;
    int      m_flees = 0;          ///< Stare-downs since it last bolted far away.
    bool     m_retreating = false;  ///< The current dash is the long one.
    bool     m_patient = false;    ///< Lying low after fleeing: waits out its timer.
    bool     m_burnt = false;      ///< The arc just had it: it runs as soon as it lets go.

    // Animation state (frozen along with the creature while it is watched).
    float     m_animTime = 0.0f;
    float     m_phase = 0.0f;     ///< Gait cycle.
    float     m_crawl = 0.0f;     ///< 0 = upright, 1 = on all fours.
    float     m_stride = 0.0f;    ///< Gait amplitude (follows speed).
    glm::vec3 m_lookDir{0.0f, 0.0f, 1.0f};

    CreatureRig       m_rig;
    glm::vec3         m_eyes[2]{};
    std::vector<Mote> m_motes;
    float             m_moteTimer = 0.0f;
};
