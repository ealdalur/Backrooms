#pragma once
// ---------------------------------------------------------------------------
// OfficeWorkers.h
// Who else made it out of the Backrooms: the Wanderer and the Stalker, at
// work in two neighbouring cubicles in the far corner of the office (see
// World/OfficeLayout's workerDesk) - an Easter egg for a player who roams.
//
// Not the AI: just the two bodies, built like the real ones (AI/Wanderer,
// AI/Stalker) but sat in their chairs, hands on their keyboards:
//   * the Wanderer types in fits and starts, its long eyeless head lolling
//     over the cubicle walls, and every so often sags back and grumbles
//     about work in its broken voice ("Oh man, work sucks");
//   * the Stalker hammers away at its keyboard with its claws, hunched and
//     twitching, and snarls now and then.
// They do not hunt any more. They have deadlines. Both always made it out,
// whatever became of them in the Backrooms.
// ---------------------------------------------------------------------------

#include "Actors/CreatureRig.h"
#include "Audio/SoundBank.h"
#include "Math/Random.h"

#include <glm/glm.hpp>
#include <vector>

class Camera;
struct EntityDrawList;

class OfficeWorkers {
public:
    /// A sound for the Engine to play where it happens.
    struct Sound {
        SoundId   id;
        int       variant;     ///< < 0: any.
        glm::vec3 at;
        float     gain;
        float     refDistance;
    };

    explicit OfficeWorkers(uint64_t seed);

    /// Sits them down at their desks.
    void clockIn();
    bool working() const { return m_wanderer.present || m_stalker.present; }

    /// Typing, grumbling, snarling.
    void update(float dt, std::vector<Sound>& sounds);

    /// Appends the bodies: the Wanderer world-lit, the Stalker a shadow shedding soot.
    void buildDrawList(EntityDrawList& list, const Camera& camera) const;

private:
    struct Body {
        bool        present = false;
        glm::mat4   desk{1.0f};
        glm::vec3   seat{0.0f};   ///< The chair, on the floor.
        float       yaw = 0.0f;   ///< Facing the desk.
        glm::vec3   keys{0.0f};   ///< The keyboard.
        glm::vec3   head{0.0f};   ///< Where its voice comes from.
        float       time = 0.0f;  ///< Animation clock.
        bool        typing = true;
        float       typeTimer = 0.0f; ///< Until it stops / starts typing.
        float       keyTimer = 0.0f;  ///< Until the next keystroke is heard.
        float       voiceTimer = 0.0f;///< Until it grumbles / snarls.
        float       talking = 0.0f;   ///< > 0 while the Wanderer is saying something.
        float       slump = 0.0f;     ///< 0..1: sagged back in the chair.
        CreatureRig rig;
        CreatureRig mouth;
    };
    struct Mote {
        glm::vec3 pos, vel;
        float     age, life, size;
    };

    void place(Body& body, int worker);
    void poseWanderer(Body& b);
    void poseStalker(Body& b);
    /// Clicks of its keyboard while it types.
    void keystrokes(Body& b, float dt, float rate, float gain, std::vector<Sound>& sounds);

    rnd::Rng m_rng;
    Body     m_wanderer;
    Body     m_stalker;
    int      m_lastLine = -1;
    glm::vec3 m_eyes[2]{};
    std::vector<Mote> m_motes;
    float    m_moteTimer = 0.0f;
};
