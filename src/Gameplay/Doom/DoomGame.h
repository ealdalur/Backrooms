#pragma once
// ---------------------------------------------------------------------------
// DoomGame.h
// "Doom runs on everything": the Easter-egg game behind the hidden DOOM
// command of the Backrooms terminals. A self-contained sub-engine: it owns
// its world (a generated DoomLevel), simulation and software renderer, and
// talks to the outside only through three narrow channels:
//   in:  Controls (held keys and mouse turn, sampled by the Engine each frame);
//   out: a TerminalGraphics frame (the terminal's CRT shows it instead of the
//        text grid) and SoundEvents (played from the terminal's speaker, so
//        the Backrooms hear the shooting too).
//
// The simulation runs at Doom's fixed 35 Hz tic rate and a new frame is only
// drawn after a tic, so it looks and feels like the 1993 original: title
// screen -> melt wipe -> level -> exit switch -> intermission tally -> next
// map, each generated from the seed. Player: momentum and friction, wall
// sliding, view and weapon bob, pistol and a pump shotgun to find, hitscan
// with spread, armour, the status-bar face, pain / pickup palette flashes.
// Monsters (imps throwing fireballs and clawing, zombie troopers with
// rifles) sleep until they see you or hear gunfire flood through open
// doors, then chase in Doom's eight-direction style, open doors, flinch in
// pain and die in gory stages. Barrels explode with splash damage; nukage
// burns.
// ---------------------------------------------------------------------------

#include "Gameplay/Doom/DoomLevel.h"
#include "Gameplay/Doom/DoomRenderer.h"
#include "Math/Random.h"
#include "Render/TerminalRenderer.h"

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace doom {

/// Held keys / mouse for one frame (the Engine maps real input onto these).
struct Controls {
    bool  forward = false, back = false, strafeLeft = false, strafeRight = false;
    bool  turnLeft = false, turnRight = false, run = false, fire = false, use = false;
    float turn = 0.0f;  ///< Mouse turn this frame, radians (positive = right).
    int   weapon = 0;   ///< Weapon slot pressed this frame (2 pistol, 3 shotgun), 0 = none.
};

enum class Sfx : uint8_t {
    Pistol, Shotgun, ImpSight, TrooperSight, MonsterPain, MonsterDeath, Claw, Fireball, Explode,
    PlayerPain, PlayerDeath, ItemUp, WeaponUp, Door, Switch
};

struct SoundEvent {
    Sfx   id;
    float gain; ///< 0..1, already attenuated by in-game distance.
};

class Game {
public:
    explicit Game(uint64_t seed);
    ~Game();

    /// Advances the tic clock by `dt` seconds and redraws after new tics.
    void update(float dt, const Controls& controls);

    const TerminalGraphics& frame() const { return m_frame; }
    /// Shows a line in the HUD message area (anomaly: the voice breaking in).
    void message(const std::string& text, bool anomaly);
    std::vector<SoundEvent> takeSounds();

    /// Jumps from the title straight into the first map (scripted demos).
    void skipTitle();

private:
    enum class Mode : uint8_t { Title, Level, Intermission };
    enum class Weapon : uint8_t { Pistol, Shotgun };

    struct Thing {
        enum class State : uint8_t { Idle, Chase, Attack, Pain, Dying, Dead, Active };
        ThingType type;
        glm::vec2 pos{0.0f}, vel{0.0f};
        float     z = 0.0f;       ///< Height of its bottom (projectiles, puffs).
        float     radius = 0.2f;
        float     angle = 0.0f;
        int       health = 0;
        State     state = State::Active;
        int       tics = 0;       ///< Tics spent in the current state.
        int       moveDir = -1;   ///< 0..7 (45 degree steps), -1 = none.
        int       moveCount = 0;
        int       reaction = 0;
        int       cooldown = 0;
        int       id = 0;
        int       owner = -1;     ///< Projectiles: id of the shooter (-1 player).
        bool      solid = false;
        bool      shootable = false;
        bool      dropped = false;  ///< Dropped by a monster (half ammo, not tallied).
        bool      byPlayer = false; ///< Barrels: the player set it off (kills count).
        bool      removed = false;
        bool monster() const { return type == ThingType::Imp || type == ThingType::Trooper; }
        bool alive() const { return health > 0; }
    };

    struct Player {
        glm::vec2 pos{0.0f}, mom{0.0f};
        float  angle = 0.0f;
        float  viewZ = 0.41f;
        float  bob = 0.0f;
        int    health = 100, armor = 0;
        int    bullets = 50, shells = 0;
        bool   hasShotgun = false;
        Weapon weapon = Weapon::Pistol, pending = Weapon::Pistol;
        float  drop = 0.0f;      ///< 0 = weapon up, 1 = lowered out of view.
        int    weaponTics = 0;   ///< Refire lockout.
        int    fireTic = 1000;   ///< Tics since the last shot (animation).
        int    refire = 0;
        int    damageCount = 0, bonusCount = 0, extraLight = 0;
        int    painSoundTics = 0;
        bool   dead = false;
        int    deadTics = 0;
        // Face.
        int    faceLook = 1, faceTics = 0, grinTics = 0, ouchTics = 0, hurtLookTics = 0;
        // Tally.
        int    kills = 0, items = 0;
    };

    struct Message {
        std::string text;
        int  tics = 0;
        bool anomaly = false;
    };

    // ---- Flow -------------------------------------------------------------------------
    void tic(const Controls& in);
    void startGame();
    void loadLevel(int map, bool pistolStart);
    void beginMelt();
    void render();
    void drawTitle();
    void drawLevel();
    void drawStatusBar();
    void drawIntermission();

    // ---- Player -----------------------------------------------------------------------
    void ticPlayer(const Controls& in);
    void ticWeapon(const Controls& in);
    void fire();
    void use();
    void pickups();
    void damagePlayer(int amount, const glm::vec2& from);
    void updateFace();

    // ---- World ------------------------------------------------------------------------
    void ticDoors();
    void ticThing(Thing& t);
    void ticMonster(Thing& t);
    void wake(Thing& t);
    bool stepMonster(Thing& t);
    void newChaseDir(Thing& t);
    void damageThing(Thing& t, int amount, bool byPlayer);
    void radiusDamage(const glm::vec2& at, int amount, float range, bool byPlayer);
    void hitscan(const glm::vec2& from, float angle, int damage, int shooter);
    void spawnEffect(ThingType type, const glm::vec2& at, float z);
    /// Queues a new thing (valid until the next spawn) for the end of the tic.
    Thing& spawn(ThingType type, const glm::vec2& at);
    Spr spriteFor(const Thing& t) const;
    void ticLevel(const Controls& in);
    void ticIntermission();
    void noiseAlert();
    void toggleDoor(int door, bool byPlayer);

    // ---- Queries ----------------------------------------------------------------------
    bool positionFree(const glm::vec2& p, float radius, int selfId, bool isPlayer) const;
    bool tryMove(glm::vec2& pos, const glm::vec2& delta, float radius, int selfId, bool isPlayer) const;
    /// Distance along `dir` to the first cell that blocks sight (max `maxDist`).
    float castRay(const glm::vec2& from, const glm::vec2& dir, float maxDist) const;
    bool canSee(const glm::vec2& a, const glm::vec2& b) const;
    void sound(Sfx id, const glm::vec2& at);
    void sound2D(Sfx id, float gain = 1.0f);
    void say(const std::string& text);

    uint64_t m_seed;
    rnd::Rng m_rng;
    Mode     m_mode = Mode::Title;
    int      m_map = 1;
    int      m_tic = 0;
    int      m_levelTics = 0;
    float    m_accum = 0.0f;
    float    m_turn = 0.0f;       ///< Mouse turn accumulated until the next tic.
    bool     m_fireLatch = false; ///< Presses between tics are not lost.
    bool     m_useLatch = false;
    bool     m_prevFire = false, m_prevUse = false;
    int      m_weaponLatch = 0;
    int      m_nextId = 1;

    Level              m_level;
    std::vector<Thing> m_things;
    std::vector<Thing> m_pending;           ///< Spawned during a tic, joins m_things after it.
    Player             m_player;
    Message            m_message;
    int                m_exitTics = -1;     ///< Counting down after the exit switch.
    std::vector<int>   m_alert;             ///< Tic each cell last heard gunfire.
    int                m_interTics = 0;
    std::array<int, 3> m_interShown{};      ///< Kills %, items %, seconds counted up so far.
    std::array<int, 3> m_interTarget{};

    bool                        m_melting = false;
    std::vector<uint8_t>        m_meltFrom;
    std::array<int, kScreenW>   m_meltOffsets{};

    std::unique_ptr<SoftwareRenderer> m_renderer;
    TerminalGraphics                  m_frame;
    std::vector<ViewSprite>           m_sprites; ///< Scratch, reused every frame.
    std::vector<SoundEvent>           m_sounds;
};

} // namespace doom
