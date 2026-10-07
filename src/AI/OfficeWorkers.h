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
//
// They are still flesh and shadow, though: the Tesla gun works on them as it
// did in the dark (the same vitals, AI/Agent - convulsing, shrieking, then
// vaporising at their desks). HR gets to hear about it.
// ---------------------------------------------------------------------------

#include "AI/Agent.h"
#include "Actors/CreatureRig.h"
#include "Audio/SoundBank.h"
#include "Gameplay/Shockable.h"
#include "Math/Random.h"

#include <glm/glm.hpp>
#include <optional>
#include <vector>

class Camera;
class ICollisionWorld;
class Physics;
struct EntityDrawList;

class OfficeWorkers : public IShockable {
public:
    /// A sound for the Engine to play where it happens.
    struct Sound {
        SoundId   id;
        int       variant;     ///< < 0: any.
        glm::vec3 at;
        float     gain;
        float     refDistance;
        float     range;       ///< Not heard at all beyond this (m): they have to be found, not heard from the far end of the floor.
    };

    enum Worker : int { Wanderer = 0, Stalker = 1 };

    /// `world` and `physics`: what the gun's arcs check their line of sight with.
    OfficeWorkers(uint64_t seed, const ICollisionWorld& world, const Physics& physics);

    /// Sits them down at their desks.
    void clockIn();
    /// Anyone still at work (neither vaporised nor burning away).
    bool staffed() const { return (m_wanderer.active() && !m_wanderer.dying()) || (m_stalker.active() && !m_stalker.dying()); }

    /// Typing, grumbling, snarling - or convulsing under the arc, or burning away.
    void update(float dt, std::vector<Sound>& sounds);

    /// Appends the bodies: the Wanderer world-lit, the Stalker a shadow shedding soot (or either vaporising).
    void buildDrawList(EntityDrawList& list, const Camera& camera) const;

    /// One of them still at work (for the developer scenes) - and the middle of its body.
    bool atDesk(Worker w) const { return body(w).active() && !body(w).dying(); }
    glm::vec3 centre(Worker w) const { return body(w).rig.bounds().center(); }

    /// Who has just been vaporised, once each (for HR).
    std::optional<Worker> takeVaporised();

    // ---- IShockable: the Tesla gun's targets in the office (ids: Worker).
    int arcTarget(const glm::vec3& from, const glm::vec3& aim, float reach, float cosCone, glm::vec3& point) const override;
    int shockTest(const glm::vec3& a, const glm::vec3& b, float radius, glm::vec3& hit) const override;
    void applyShock(int target, float damage, const glm::vec3& at) override;

private:
    /// One of them at its desk: an Agent for the vitals (shock, pain, the vaporisation), never for walking.
    class Body : public Agent {
    public:
        using Agent::VitalSigns;
        Body() : Agent(0.3f, 2.3f) {}
        const CreatureRig& body() const override { return rig; }
        VitalSigns vitals(float dt) { return updateVitals(dt); }
        /// Twitching with the current, or shuddering as it burns away.
        void shudder() {
            convulse(rig);
            convulse(mouth);
        }
        void embers(EntityDrawList& list, const glm::vec3& camRight, const glm::vec3& camUp) const { buildEmbers(list, camRight, camUp); }
        void setVulnerability(float v) { m_vulnerability = v; }
        /// Neither shocked nor dying: at work.
        bool atWork() const { return !shocked() && !dying(); }

        glm::mat4   desk{1.0f};
        glm::vec3   seat{0.0f};   ///< The chair, on the floor.
        float       facing = 0.0f; ///< Yaw towards the desk.
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

    const Body& body(Worker w) const { return w == Wanderer ? m_wanderer : m_stalker; }
    void place(Body& body, Worker worker);
    /// The shrieks and the end, from its vitals.
    void vitals(Body& b, Worker worker, float dt, std::vector<Sound>& sounds);
    /// Nothing solid between `a` and `b` (an arc can jump there).
    bool clearLine(const glm::vec3& a, const glm::vec3& b) const;
    void poseWanderer(Body& b);
    void poseStalker(Body& b);
    /// Clicks of its keyboard while it types.
    void keystrokes(Body& b, float dt, float rate, float gain, std::vector<Sound>& sounds);

    const ICollisionWorld& m_world;
    const Physics&         m_physics;
    rnd::Rng m_rng;
    Body     m_wanderer;
    Body     m_stalker;
    std::vector<Worker> m_vaporised;
    int      m_lastLine = -1;
    glm::vec3 m_eyes[2]{};
    std::vector<Mote> m_motes;
    float    m_moteTimer = 0.0f;
};
