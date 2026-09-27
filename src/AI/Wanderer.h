#pragma once
// ---------------------------------------------------------------------------
// Wanderer.h
// The blind entity. An emaciated, eyeless figure over two metres tall that
// navigates purely by ear:
//
//   Roaming       - shuffles aimlessly through the rooms, arms hanging.
//   Investigating - heard something: it stops, cocks its head, then walks
//                   to where the sound came from (with an error that grows
//                   with distance), arms reaching out ahead of it.
//   Hunting       - heard something loud or close: it rushes the source,
//                   re-targeting on every new noise.
//   Searching     - arrived and heard nothing more: it turns and gropes
//                   around the spot for a while, then gives up.
//
// Hearing is audio propagation on the world's structure: each NoiseEvent
// has an audible radius (footsteps scale with speed - crouch-walking is
// nearly silent - doors and landings are loud), halved when walls stand in
// between. It opens doors in its way (loudly). It never stops muttering:
// the Soundscape loops its broken phrases from its head position, growing
// louder as it closes in.
// ---------------------------------------------------------------------------

#include "AI/Agent.h"
#include "AI/NoiseEvent.h"
#include "AI/Perception.h"
#include "Actors/CreatureRig.h"
#include "Math/Random.h"

#include <vector>

class ChunkManager;
struct EntityDrawList;

class Wanderer : public Agent {
public:
    enum class State : uint8_t { Roaming, Investigating, Hunting, Searching };

    explicit Wanderer(uint64_t seed);

    void place(const glm::vec3& feet, int level, float yaw) override;

    /// Listens, moves, opens doors and animates. Returns true if it walked
    /// into the player this frame.
    bool update(float dt, const PlayerView& player, const NavGrid& nav, ChunkManager& chunks, const Physics& physics,
                const std::vector<NoiseEvent>& noises, std::vector<EntitySound>& sounds);

    /// The catch: it looms over the player, groping hands at their face.
    /// Returns where its face is.
    glm::vec3 confront(const glm::vec3& playerFeet, const glm::vec3& playerForward);

    State state() const { return m_state; }
    /// 0 = calm, 1 = frantic (drives voice level and speed of delivery).
    float agitation() const { return m_alert; }
    /// Where its voice comes from.
    glm::vec3 headPosition() const { return m_head; }

    /// Appends the (world-lit) body.
    void buildGeometry(EntityDrawList& list) const;

private:
    bool listen(const std::vector<NoiseEvent>& noises, const NavGrid& nav);
    void pickRoamTarget(const NavGrid& nav);
    /// Handles a closed door on the path; true while it is waiting for one.
    bool handleDoors(float dt, const NavGrid& nav, ChunkManager& chunks, const Physics& physics);
    void animate(float dt, std::vector<EntitySound>& sounds);
    void pose();

    rnd::Rng  m_rng;
    State     m_state = State::Roaming;
    glm::vec3 m_target{0.0f};     ///< Where it thinks the sound came from.
    float     m_alert = 0.0f;
    float     m_timer = 0.0f;
    float     m_replan = 0.0f;
    float     m_listenPause = 0.0f; ///< Freezes to listen after hearing something.
    float     m_doorWait = 0.0f;
    glm::vec3 m_heardDir{0.0f, 0.0f, 1.0f};

    // Animation.
    float     m_animTime = 0.0f;
    float     m_phase = 0.0f;
    float     m_stride = 0.0f;
    float     m_reach = 0.0f;     ///< 0 = arms hanging, 1 = groping ahead.
    float     m_duck = 0.0f;      ///< 1 = hunched down to fit through an opening.
    float     m_headCock = 0.0f;
    glm::vec3 m_head{0.0f};
    CreatureRig m_rig;
    CreatureRig m_mouth;
};
