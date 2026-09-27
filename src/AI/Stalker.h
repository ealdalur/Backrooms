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
//              few metres it creeps.
//   Frozen   - the instant it is seen it stops dead, mid-stride. Stare long
//              enough (or come close) and...
//   Fleeing  - ...it darts, very fast, to the nearest spot the player cannot
//              see: behind a corner, a wall, a doorway.
//   Lunging  - within reach and unseen, it rushes the player.
//
// "Seen" is a real perception test: the body's bounds against the player's
// view frustum (frustum culling turned into an AI sense), line of sight past
// walls and live door states, and darkness - in an unlit area it cannot be
// made out beyond a few metres. It slips under closed doors and drains the
// fluorescent tubes around it.
// ---------------------------------------------------------------------------

#include "AI/Agent.h"
#include "AI/Perception.h"
#include "Actors/CreatureRig.h"
#include "Math/Random.h"
#include "Render/LightGrid.h"

#include <vector>

struct EntityDrawList;

class Stalker : public Agent {
public:
    enum class State : uint8_t { Lurking, Stalking, Frozen, Fleeing, Lunging };

    explicit Stalker(uint64_t seed);

    void place(const glm::vec3& feet, int level, float yaw) override;

    /// Advances perception, behaviour and animation. Returns true if it
    /// reached the player this frame.
    bool update(float dt, const PlayerView& player, const NavGrid& nav, const ICollisionWorld& world,
                const Physics& physics, std::vector<EntitySound>& sounds);

    /// Cuts short its lurking: it starts hunting right away.
    void provoke();

    /// The catch: it rears up to its full height, face to face with the
    /// player. Returns where its face is.
    glm::vec3 confront(const glm::vec3& playerFeet, const glm::vec3& playerForward);

    State state() const { return m_state; }
    bool seen() const { return m_seen; }
    /// Stared down too often here: it wants to vanish and resurface elsewhere.
    bool wantsToResurface() const { return m_flees >= 3; }
    /// True when it is moving fast (skittering) rather than holding still.
    bool moving() const;

    /// Nearby tubes sag and stutter.
    LightDisturbance lightDisturbance() const;

    /// Appends the shadow body, soot motes and eye glints.
    void buildGeometry(EntityDrawList& list, const glm::vec3& camRight, const glm::vec3& camUp) const;

private:
    struct Mote {
        glm::vec3 pos, vel;
        float     age, life, size;
    };

    bool isSeenBy(const PlayerView& player, const NavGrid& nav) const;
    NavProfile huntProfile(const PlayerView& player, const NavGrid& nav) const;
    bool findCover(const PlayerView& player, const NavGrid& nav, glm::vec3& out) const;
    void startFleeing(const PlayerView& player, const NavGrid& nav, std::vector<EntitySound>& sounds);
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
    int      m_flees = 0;          ///< Times it has been driven into cover since it appeared.
    bool     m_patient = false;    ///< Lying low after fleeing: waits out its timer.

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
