// ---------------------------------------------------------------------------
// DoomGame.cpp
// Units: one map cell = 1 (about 100 of the original's map units), one tic =
// 1/35 s. Damage and health use the original's numbers.
// ---------------------------------------------------------------------------
#include "Gameplay/Doom/DoomGame.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>

namespace doom {
namespace {

constexpr float kTic = 1.0f / 35.0f;
constexpr float kPi = 3.14159265f;
constexpr float kPlayerRadius = 0.22f;
constexpr float kEyeZ = 0.41f;
constexpr float kFriction = 0.90625f;   ///< Momentum kept per tic (the original's FRICTION).
constexpr float kWalkThrust = 0.0082f;  ///< Top speed = thrust / (1 - friction): ~3 cells/s walking...
constexpr float kRunThrust = 0.0158f;   ///< ...and ~6 running.
constexpr float kMaxMomentum = kRunThrust / (1.0f - kFriction);
constexpr float kUseRange = 1.3f;
constexpr int   kDoorTravelTics = 20;
constexpr int   kDoorWaitTics = 150;

/// The eight chase directions, 45 degrees apart (0 = east, turning right).
const glm::vec2 kDirs[8] = {{1.0f, 0.0f},  {0.7071f, 0.7071f},  {0.0f, 1.0f},  {-0.7071f, 0.7071f},
                            {-1.0f, 0.0f}, {-0.7071f, -0.7071f}, {0.0f, -1.0f}, {0.7071f, -0.7071f}};

float wrapAngle(float a) { return std::remainder(a, 2.0f * kPi); }
glm::vec2 heading(float angle) { return {std::cos(angle), std::sin(angle)}; }
float angleTo(const glm::vec2& from, const glm::vec2& to) { return std::atan2(to.y - from.y, to.x - from.x); }

bool isPickup(ThingType t) { return t >= ThingType::Shotgun && t <= ThingType::Armor; }

} // namespace

Game::Game(uint64_t seed) : m_seed(seed), m_rng(rnd::hashCombine(seed, 0xD0Bull)) {
    m_renderer = std::make_unique<SoftwareRenderer>();
    render();
}

Game::~Game() = default;

std::vector<SoundEvent> Game::takeSounds() {
    std::vector<SoundEvent> out;
    out.swap(m_sounds);
    return out;
}

void Game::message(const std::string& text, bool anomaly) { m_message = {text, anomaly ? 35 * 6 : 35 * 4, anomaly}; }

void Game::say(const std::string& text) { message(text, false); }

void Game::sound(Sfx id, const glm::vec2& at) {
    // The original's attenuation: full volume close by, silent ~16 cells away.
    const float d = glm::length(at - m_player.pos);
    const float gain = std::clamp(1.0f - (d - 1.5f) / 16.0f, 0.0f, 1.0f);
    if (gain > 0.03f) m_sounds.push_back({id, gain});
}

void Game::sound2D(Sfx id, float gain) { m_sounds.push_back({id, gain}); }

void Game::skipTitle() {
    if (m_mode == Mode::Title) startGame();
}

// ============================================================================
// Flow
// ============================================================================

void Game::update(float dt, const Controls& in) {
    // Presses between tics must not be lost.
    if (in.fire && !m_prevFire) m_fireLatch = true;
    if (in.use && !m_prevUse) m_useLatch = true;
    m_prevFire = in.fire;
    m_prevUse = in.use;
    if (in.weapon) m_weaponLatch = in.weapon;
    m_turn += in.turn;

    m_accum += std::min(dt, 0.25f);
    int ran = 0;
    while (m_accum >= kTic) {
        m_accum -= kTic;
        tic(in);
        ++ran;
    }
    if (ran > 0) render(); // 35 frames a second, like the original
}

void Game::tic(const Controls& in) {
    ++m_tic;
    if (m_message.tics > 0) --m_message.tics;
    if (m_melting) {
        // The screen melts down column by column; the game waits.
        bool done = true;
        for (int& y : m_meltOffsets) {
            if (y < 0) {
                ++y;
                done = false;
            } else if (y < kScreenH) {
                y = std::min(kScreenH, y + (y < 16 ? y + 1 : 8));
                done = false;
            }
        }
        m_melting = !done;
        m_turn = 0.0f;
        return;
    }
    switch (m_mode) {
    case Mode::Title:
        if (m_fireLatch || m_useLatch) startGame();
        m_fireLatch = m_useLatch = false;
        break;
    case Mode::Level:
        ticLevel(in);
        break;
    case Mode::Intermission:
        ticIntermission();
        break;
    }
}

void Game::startGame() {
    beginMelt();
    loadLevel(1, true);
}

void Game::beginMelt() {
    m_meltFrom = m_renderer->frame();
    // Columns (in pairs, as in the original) start at staggered heights.
    int y = -static_cast<int>(m_rng.next() % 16);
    for (int x = 0; x < kScreenW; x += 2) {
        y = std::clamp(y + static_cast<int>(m_rng.next() % 3) - 1, -15, 0);
        m_meltOffsets[static_cast<size_t>(x)] = m_meltOffsets[static_cast<size_t>(x + 1)] = y;
    }
    m_melting = true;
}

void Game::loadLevel(int map, bool pistolStart) {
    m_map = map;
    m_level = Level::generate(rnd::hashCombine(m_seed, static_cast<uint64_t>(map)), map);
    m_things.clear();
    m_pending.clear();
    for (const Spawn& s : m_level.spawns) {
        Thing& t = spawn(s.type, s.pos);
        t.angle = m_rng.range(-kPi, kPi);
    }
    m_things.swap(m_pending);
    m_alert.assign(static_cast<size_t>(m_level.width() * m_level.height()), -1000);

    Player& p = m_player;
    if (pistolStart) {
        p = Player{};
    } else {
        // Health, armour, weapons and ammo carry over; everything else resets.
        Player next;
        next.health = p.health;
        next.armor = p.armor;
        next.bullets = p.bullets;
        next.shells = p.shells;
        next.hasShotgun = p.hasShotgun;
        next.weapon = next.pending = p.weapon;
        p = next;
    }
    p.pos = m_level.start;
    p.angle = m_level.startAngle;
    m_levelTics = 0;
    m_exitTics = -1;
    m_mode = Mode::Level;
    m_fireLatch = m_useLatch = false;
    say(m_level.name);
}

void Game::ticLevel(const Controls& in) {
    ++m_levelTics;
    ticDoors();
    ticPlayer(in);
    for (size_t i = 0; i < m_things.size(); ++i) {
        if (!m_things[i].removed) ticThing(m_things[i]);
    }
    m_things.erase(std::remove_if(m_things.begin(), m_things.end(), [](const Thing& t) { return t.removed; }), m_things.end());
    m_things.insert(m_things.end(), m_pending.begin(), m_pending.end());
    m_pending.clear();
    m_fireLatch = m_useLatch = false;

    if (m_exitTics > 0 && --m_exitTics == 0) {
        const int seconds = m_levelTics / 35;
        m_interTarget = {m_level.monsterCount ? m_player.kills * 100 / m_level.monsterCount : 100,
                         m_level.itemCount ? m_player.items * 100 / m_level.itemCount : 100, seconds};
        m_interShown = {0, 0, 0};
        m_interTics = 0;
        beginMelt();
        m_mode = Mode::Intermission;
    }
}

void Game::ticIntermission() {
    ++m_interTics;
    bool counting = false;
    for (size_t i = 0; i < 3; ++i) {
        if (m_interShown[i] >= m_interTarget[i]) continue;
        counting = true;
        if (m_interTics > 20 && m_interTics % 2 == 0) m_interShown[i] = std::min(m_interTarget[i], m_interShown[i] + (i == 2 ? 3 : 2));
        break;
    }
    if (counting && m_interTics > 20 && m_interTics % 4 == 0) sound2D(Sfx::Pistol, 0.5f);
    if (m_fireLatch || m_useLatch) {
        if (counting) {
            m_interShown = m_interTarget;
        } else {
            beginMelt();
            loadLevel(m_map + 1, false);
        }
    }
    m_fireLatch = m_useLatch = false;
}

// ============================================================================
// Player
// ============================================================================

void Game::ticPlayer(const Controls& in) {
    Player& p = m_player;
    if (p.damageCount > 0) --p.damageCount;
    if (p.bonusCount > 0) --p.bonusCount;
    if (p.extraLight > 0) --p.extraLight;
    if (p.painSoundTics > 0) --p.painSoundTics;
    updateFace();

    if (p.dead) {
        // Down on the floor; the weapon drops out of sight. USE restarts the map.
        p.viewZ = std::max(0.08f, p.viewZ - 0.012f);
        p.drop = std::min(1.0f, p.drop + 0.1f);
        ++p.deadTics;
        glm::vec2 before = p.pos;
        tryMove(p.pos, p.mom, kPlayerRadius, -1, true);
        p.mom = (p.pos - before) * kFriction;
        m_turn = 0.0f;
        if (p.deadTics > 35 && (m_useLatch || m_fireLatch)) {
            beginMelt();
            loadLevel(m_map, true);
        }
        return;
    }

    const float keyTurn = in.run ? 0.085f : 0.055f;
    p.angle = wrapAngle(p.angle + m_turn + (in.turnRight ? keyTurn : 0.0f) - (in.turnLeft ? keyTurn : 0.0f));
    m_turn = 0.0f;
    const glm::vec2 dir = heading(p.angle), right(-dir.y, dir.x);
    const float thrust = in.run ? kRunThrust : kWalkThrust;
    const float fwd = (in.forward ? 1.0f : 0.0f) - (in.back ? 1.0f : 0.0f);
    const float side = (in.strafeRight ? 1.0f : 0.0f) - (in.strafeLeft ? 1.0f : 0.0f);
    p.mom += dir * (fwd * thrust) + right * (side * thrust * 0.9f);

    const glm::vec2 before = p.pos;
    if (!tryMove(p.pos, p.mom, kPlayerRadius, -1, true)) p.mom = p.pos - before; // slid along a wall
    p.mom *= kFriction;
    if (glm::length(p.mom) < 0.0005f) p.mom = glm::vec2(0.0f);

    const float speed = std::min(1.0f, glm::length(p.mom) / kMaxMomentum);
    p.bob = std::min(1.0f, speed * speed * 1.6f + speed * 0.3f);
    p.viewZ = kEyeZ + 0.014f * p.bob * std::sin(static_cast<float>(m_levelTics) * 2.0f * kPi / 20.0f);

    // Nukage burns through the boots.
    if (m_levelTics % 32 == 0 && m_level.isNukage(static_cast<int>(std::floor(p.pos.x)), static_cast<int>(std::floor(p.pos.y)))) {
        damagePlayer(5, p.pos);
    }
    pickups();
    if (m_useLatch) use();
    ticWeapon(in);
}

void Game::ticWeapon(const Controls& in) {
    Player& p = m_player;
    if (m_weaponLatch == 2) p.pending = Weapon::Pistol;
    if (m_weaponLatch == 3 && p.hasShotgun) p.pending = Weapon::Shotgun;
    m_weaponLatch = 0;
    ++p.fireTic;
    if (p.weaponTics > 0) --p.weaponTics;

    if (p.pending != p.weapon) {
        // Lower the old weapon, then raise the new one.
        if (p.weaponTics > 0) return;
        p.drop += 0.15f;
        if (p.drop >= 1.0f) {
            p.drop = 1.0f;
            p.weapon = p.pending;
        }
        return;
    }
    if (p.drop > 0.0f) {
        p.drop = std::max(0.0f, p.drop - 0.15f);
        return;
    }
    if (p.weaponTics > 0) return;
    if (!(in.fire || m_fireLatch)) {
        p.refire = 0;
        return;
    }
    // Out of ammo: switch to whatever still has some.
    if (p.weapon == Weapon::Shotgun && p.shells <= 0) {
        if (p.bullets > 0) p.pending = Weapon::Pistol;
        return;
    }
    if (p.weapon == Weapon::Pistol && p.bullets <= 0) {
        if (p.hasShotgun && p.shells > 0) p.pending = Weapon::Shotgun;
        return;
    }
    fire();
}

void Game::fire() {
    Player& p = m_player;
    p.fireTic = 0;
    p.extraLight = 3;
    if (p.weapon == Weapon::Pistol) {
        --p.bullets;
        sound2D(Sfx::Pistol);
        const float spread = p.refire ? m_rng.range(-0.09f, 0.09f) : 0.0f; // the first shot is dead on
        hitscan(p.pos, p.angle + spread, 5 * m_rng.rangeInt(1, 3), -1);
        p.weaponTics = 14;
    } else {
        --p.shells;
        sound2D(Sfx::Shotgun);
        for (int i = 0; i < 7; ++i) hitscan(p.pos, p.angle + m_rng.range(-0.1f, 0.1f), 5 * m_rng.rangeInt(1, 3), -1);
        p.weaponTics = 37;
    }
    ++p.refire;
    noiseAlert();
}

void Game::use() {
    // The first thing within reach along the view: a door, the switch, or a wall ("oof").
    const Player& p = m_player;
    const glm::vec2 dir = heading(p.angle);
    int mx = static_cast<int>(std::floor(p.pos.x)), my = static_cast<int>(std::floor(p.pos.y));
    const float dx = std::fabs(dir.x) < 1e-6f ? 1e30f : std::fabs(1.0f / dir.x), dy = std::fabs(dir.y) < 1e-6f ? 1e30f : std::fabs(1.0f / dir.y);
    const int sx = dir.x < 0.0f ? -1 : 1, sy = dir.y < 0.0f ? -1 : 1;
    float tx = (dir.x < 0.0f ? p.pos.x - static_cast<float>(mx) : static_cast<float>(mx) + 1.0f - p.pos.x) * dx;
    float ty = (dir.y < 0.0f ? p.pos.y - static_cast<float>(my) : static_cast<float>(my) + 1.0f - p.pos.y) * dy;
    for (int i = 0; i < 4; ++i) {
        float t;
        if (tx < ty) {
            t = tx;
            tx += dx;
            mx += sx;
        } else {
            t = ty;
            ty += dy;
            my += sy;
        }
        if (t > kUseRange) return;
        Tile& tile = m_level.at(mx, my);
        if (tile.kind == Tile::Empty) continue;
        if (tile.kind == Tile::Door) {
            toggleDoor(tile.door, true);
        } else if (tile.kind == Tile::Switch && tile.wallTex == WallTex::Switch && m_exitTics < 0) {
            tile.wallTex = WallTex::SwitchOn;
            sound2D(Sfx::Switch);
            m_exitTics = 35;
        } else {
            sound2D(Sfx::PlayerPain, 0.5f);
        }
        return;
    }
}

void Game::pickups() {
    Player& p = m_player;
    for (Thing& t : m_things) {
        if (t.removed || !isPickup(t.type) || glm::length(t.pos - p.pos) > 0.42f) continue;
        bool weapon = false;
        const char* msg = nullptr;
        switch (t.type) {
        case ThingType::Shotgun:
            if (!p.hasShotgun) {
                p.hasShotgun = true;
                p.pending = Weapon::Shotgun;
                p.grinTics = 70;
                msg = "YOU GOT THE SHOTGUN!";
            } else if (p.shells < 50) {
                msg = "YOU GOT THE SHOTGUN!";
            }
            if (msg) {
                p.shells = std::min(50, p.shells + 8);
                weapon = true;
            }
            break;
        case ThingType::Clip:
            if (p.bullets < 200) {
                p.bullets = std::min(200, p.bullets + (t.dropped ? 5 : 10));
                msg = "PICKED UP A CLIP.";
            }
            break;
        case ThingType::Shells:
            if (p.shells < 50) {
                p.shells = std::min(50, p.shells + 4);
                msg = "PICKED UP 4 SHOTGUN SHELLS.";
            }
            break;
        case ThingType::Medikit:
            if (p.health < 100) {
                msg = p.health < 25 ? "PICKED UP A MEDIKIT THAT YOU REALLY NEED!" : "PICKED UP A MEDIKIT.";
                p.health = std::min(100, p.health + 25);
            }
            break;
        case ThingType::Stimpack:
            if (p.health < 100) {
                p.health = std::min(100, p.health + 10);
                msg = "PICKED UP A STIMPACK.";
            }
            break;
        case ThingType::Armor:
            if (p.armor < 100) {
                p.armor = 100;
                msg = "PICKED UP THE ARMOR.";
            }
            break;
        default:
            break;
        }
        if (!msg) continue; // not needed: leave it
        t.removed = true;
        p.bonusCount += 6;
        sound2D(weapon ? Sfx::WeaponUp : Sfx::ItemUp);
        say(msg);
        if (!t.dropped) ++p.items;
    }
}

void Game::damagePlayer(int amount, const glm::vec2& from) {
    Player& p = m_player;
    if (p.dead || amount <= 0) return;
    if (p.armor > 0) {
        const int saved = std::min(amount / 3, p.armor); // green armour takes a third
        p.armor -= saved;
        amount -= saved;
    }
    p.health -= amount;
    p.damageCount = std::min(100, p.damageCount + amount);
    if (amount >= 20) p.ouchTics = 35;
    if (glm::length(from - p.pos) > 0.05f) {
        const float rel = wrapAngle(angleTo(p.pos, from) - p.angle);
        if (std::fabs(rel) > 0.35f) {
            p.faceLook = rel > 0.0f ? 2 : 0; // the face turns towards whoever hurt it
            p.hurtLookTics = 35;
        }
    }
    if (p.health <= 0) {
        p.health = 0;
        p.dead = true;
        p.deadTics = 0;
        sound2D(Sfx::PlayerDeath);
    } else if (p.painSoundTics == 0) {
        sound2D(Sfx::PlayerPain);
        p.painSoundTics = 12;
    }
}

void Game::updateFace() {
    Player& p = m_player;
    if (p.grinTics > 0) --p.grinTics;
    if (p.ouchTics > 0) --p.ouchTics;
    if (p.hurtLookTics > 0) {
        --p.hurtLookTics;
        return;
    }
    if (--p.faceTics <= 0) {
        const float r = m_rng.nextFloat();
        p.faceLook = r < 0.5f ? 1 : r < 0.75f ? 0 : 2;
        p.faceTics = m_rng.rangeInt(17, 50);
    }
}

// ============================================================================
// World
// ============================================================================

void Game::toggleDoor(int index, bool byPlayer) {
    Door& d = m_level.doors[static_cast<size_t>(index)];
    const glm::vec2 at(static_cast<float>(d.x) + 0.5f, static_cast<float>(d.y) + 0.5f);
    switch (d.state) {
    case Door::State::Closed:
    case Door::State::Closing:
        d.state = Door::State::Opening;
        sound(Sfx::Door, at);
        break;
    case Door::State::Open:
    case Door::State::Opening:
        if (byPlayer) {
            d.state = Door::State::Closing;
            sound(Sfx::Door, at);
        }
        break;
    }
}

void Game::ticDoors() {
    for (Door& d : m_level.doors) {
        const glm::vec2 at(static_cast<float>(d.x) + 0.5f, static_cast<float>(d.y) + 0.5f);
        auto occupied = [&]() {
            auto inCell = [&](const glm::vec2& p, float r) {
                const float cx = std::clamp(p.x, static_cast<float>(d.x), static_cast<float>(d.x + 1));
                const float cy = std::clamp(p.y, static_cast<float>(d.y), static_cast<float>(d.y + 1));
                return glm::length(p - glm::vec2(cx, cy)) < r;
            };
            if (inCell(m_player.pos, kPlayerRadius)) return true;
            for (const Thing& t : m_things)
                if (t.solid && !t.removed && inCell(t.pos, t.radius)) return true;
            return false;
        };
        switch (d.state) {
        case Door::State::Opening:
            d.open += 1.0f / kDoorTravelTics;
            if (d.open >= 1.0f) {
                d.open = 1.0f;
                d.state = Door::State::Open;
                d.wait = kDoorWaitTics;
            }
            break;
        case Door::State::Open:
            if (--d.wait <= 0) {
                if (occupied()) {
                    d.wait = 35;
                } else {
                    d.state = Door::State::Closing;
                    sound(Sfx::Door, at);
                }
            }
            break;
        case Door::State::Closing:
            if (occupied()) { // something in the way: back up
                d.state = Door::State::Opening;
                sound(Sfx::Door, at);
                break;
            }
            d.open -= 1.0f / kDoorTravelTics;
            if (d.open <= 0.0f) {
                d.open = 0.0f;
                d.state = Door::State::Closed;
            }
            break;
        case Door::State::Closed:
            break;
        }
    }
}

Game::Thing& Game::spawn(ThingType type, const glm::vec2& at) {
    Thing t;
    t.type = type;
    t.pos = at;
    t.id = m_nextId++;
    switch (type) {
    case ThingType::Imp:
        t.radius = 0.2f;
        t.health = 60;
        t.solid = t.shootable = true;
        t.state = Thing::State::Idle;
        break;
    case ThingType::Trooper:
        t.radius = 0.2f;
        t.health = 20;
        t.solid = t.shootable = true;
        t.state = Thing::State::Idle;
        break;
    case ThingType::Barrel:
        t.radius = 0.16f;
        t.health = 20;
        t.solid = t.shootable = true;
        break;
    case ThingType::Lamp:
        t.radius = 0.12f;
        t.solid = true;
        break;
    case ThingType::Fireball:
        t.radius = 0.1f;
        break;
    default:
        t.radius = 0.2f;
        break;
    }
    m_pending.push_back(t);
    return m_pending.back();
}

void Game::spawnEffect(ThingType type, const glm::vec2& at, float z) { spawn(type, at).z = z; }

void Game::ticThing(Thing& t) {
    ++t.tics;
    switch (t.type) {
    case ThingType::Imp:
    case ThingType::Trooper:
        ticMonster(t);
        break;
    case ThingType::Barrel:
        if (t.state == Thing::State::Dying) {
            if (t.tics == 5) {
                t.solid = false;
                radiusDamage(t.pos, 128, 1.28f, t.byPlayer);
            }
            if (t.tics >= 15) t.removed = true;
        }
        break;
    case ThingType::Fireball: {
        t.pos += t.vel;
        bool hit = m_level.blocksSight(static_cast<int>(std::floor(t.pos.x)), static_cast<int>(std::floor(t.pos.y)));
        if (!hit && !m_player.dead && glm::length(t.pos - m_player.pos) < t.radius + kPlayerRadius) {
            damagePlayer(3 * m_rng.rangeInt(1, 8), t.pos - t.vel * 4.0f);
            hit = true;
        }
        for (Thing& o : m_things) {
            if (hit) break;
            if (!o.shootable || !o.alive() || o.removed || o.id == t.owner) continue;
            if (glm::length(o.pos - t.pos) < t.radius + o.radius) {
                damageThing(o, 3 * m_rng.rangeInt(1, 8), false);
                hit = true;
            }
        }
        if (hit) {
            t.pos -= t.vel;
            t.vel = glm::vec2(0.0f);
            t.type = ThingType::Explosion;
            t.tics = 0;
            t.z = std::max(0.0f, t.z - 0.1f);
            sound(Sfx::Explode, t.pos);
        } else if (t.tics > 35 * 8) {
            t.removed = true;
        }
        break;
    }
    case ThingType::Explosion:
        if (t.tics >= 15) t.removed = true;
        break;
    case ThingType::Puff:
        t.z += 0.004f;
        if (t.tics >= 12) t.removed = true;
        break;
    case ThingType::Blood:
        t.z = std::max(0.0f, t.z - 0.008f);
        if (t.tics >= 15) t.removed = true;
        break;
    default:
        break;
    }
}

void Game::wake(Thing& t) {
    t.state = Thing::State::Chase;
    t.tics = 0;
    t.reaction = 8;
    t.moveCount = 0;
    sound(t.type == ThingType::Imp ? Sfx::ImpSight : Sfx::TrooperSight, t.pos);
}

void Game::ticMonster(Thing& t) {
    const Player& p = m_player;
    const float d = glm::length(p.pos - t.pos);
    const float meleeRange = t.radius + kPlayerRadius + 0.3f;
    switch (t.state) {
    case Thing::State::Idle: {
        if ((m_levelTics + t.id) % 4 != 0 || p.dead) break;
        const int cx = static_cast<int>(std::floor(t.pos.x)), cy = static_cast<int>(std::floor(t.pos.y));
        const bool heard = m_alert[static_cast<size_t>(cy * m_level.width() + cx)] >= m_levelTics - 4;
        const bool facing = std::fabs(wrapAngle(angleTo(t.pos, p.pos) - t.angle)) < 1.75f;
        const bool seen = d < 24.0f && (facing || d < 1.6f) && canSee(t.pos, p.pos);
        if (heard || seen) wake(t);
        break;
    }
    case Thing::State::Chase:
        if (t.reaction > 0) --t.reaction;
        if (t.cooldown > 0) --t.cooldown;
        t.angle = angleTo(t.pos, p.pos);
        if (!p.dead && t.type == ThingType::Imp && d < meleeRange && canSee(t.pos, p.pos)) {
            t.state = Thing::State::Attack;
            t.tics = 0;
            break;
        }
        if (--t.moveCount < 0 || !stepMonster(t)) {
            // At the end of each run it may attack (the original's missile range check).
            if (!p.dead && t.reaction == 0 && t.cooldown == 0 && canSee(t.pos, p.pos)) {
                const float dist = std::clamp(d * 100.0f - (t.type == ThingType::Imp ? 64.0f : 128.0f), 0.0f, 200.0f);
                if (m_rng.nextFloat() * 256.0f >= dist) {
                    t.state = Thing::State::Attack;
                    t.tics = 0;
                    break;
                }
            }
            newChaseDir(t);
        }
        break;
    case Thing::State::Attack:
        t.angle = angleTo(t.pos, p.pos);
        if (t.type == ThingType::Imp) {
            if (t.tics == 8) {
                if (d < meleeRange && !p.dead) {
                    sound(Sfx::Claw, t.pos);
                    damagePlayer(3 * m_rng.rangeInt(1, 8), t.pos);
                } else {
                    const glm::vec2 dir = heading(t.angle);
                    Thing& ball = spawn(ThingType::Fireball, t.pos + dir * 0.25f);
                    ball.vel = dir * 0.1f;
                    ball.z = 0.3f;
                    ball.owner = t.id;
                    sound(Sfx::Fireball, t.pos);
                }
            }
            if (t.tics >= 16) {
                t.state = Thing::State::Chase;
                t.cooldown = m_rng.rangeInt(20, 50);
            }
        } else {
            if (t.tics == 10) {
                sound(Sfx::Pistol, t.pos);
                const float spread = (m_rng.nextFloat() - m_rng.nextFloat()) * 0.39f;
                hitscan(t.pos, t.angle + spread, 3 * m_rng.rangeInt(1, 5), t.id);
            }
            if (t.tics >= 18) {
                t.state = Thing::State::Chase;
                t.cooldown = m_rng.rangeInt(15, 40);
            }
        }
        break;
    case Thing::State::Pain:
        if (t.tics >= 6) {
            t.state = Thing::State::Chase;
            t.tics = 0;
        }
        break;
    case Thing::State::Dying:
        if (t.tics >= 25) t.state = Thing::State::Dead;
        break;
    default:
        break;
    }
}

bool Game::stepMonster(Thing& t) {
    if (t.moveDir < 0) return false;
    const float speed = t.type == ThingType::Imp ? 0.028f : 0.024f;
    const glm::vec2 dir = kDirs[t.moveDir];
    const glm::vec2 next = t.pos + dir * speed;
    if (positionFree(next, t.radius, t.id, false)) {
        t.pos = next;
        return true;
    }
    // A shut door in the way: open it and wait.
    const glm::vec2 ahead = t.pos + dir * (t.radius + 0.15f);
    const Tile& tile = m_level.at(static_cast<int>(std::floor(ahead.x)), static_cast<int>(std::floor(ahead.y)));
    if (tile.kind == Tile::Door) {
        const Door& door = m_level.doors[static_cast<size_t>(tile.door)];
        if (door.state == Door::State::Closed || door.state == Door::State::Closing) toggleDoor(tile.door, false);
        return true;
    }
    return false;
}

void Game::newChaseDir(Thing& t) {
    // The original's P_NewChaseDir: head for the player along the diagonal,
    // then the major axis, then keep going, then anything but back.
    const glm::vec2 delta = m_player.pos - t.pos;
    const int old = t.moveDir, turnaround = old >= 0 ? (old + 4) % 8 : -1;
    int dx = delta.x > 0.3f ? 0 : delta.x < -0.3f ? 4 : -1;
    int dy = delta.y > 0.3f ? 2 : delta.y < -0.3f ? 6 : -1;
    auto tryDir = [&](int dir) {
        if (dir < 0) return false;
        t.moveDir = dir;
        if (!stepMonster(t)) return false;
        t.moveCount = m_rng.rangeInt(0, 15) * 3;
        return true;
    };
    if (dx >= 0 && dy >= 0) {
        const int diag = dx == 0 ? (dy == 2 ? 1 : 7) : (dy == 2 ? 3 : 5);
        if (diag != turnaround && tryDir(diag)) return;
    }
    if (m_rng.chance(0.3f) || std::fabs(delta.y) > std::fabs(delta.x)) std::swap(dx, dy);
    if (dx == turnaround) dx = -1;
    if (dy == turnaround) dy = -1;
    if (tryDir(dx) || tryDir(dy)) return;
    if (old >= 0 && old != turnaround && tryDir(old)) return;
    const bool up = m_rng.chance(0.5f);
    for (int i = 0; i < 8; ++i) {
        const int dir = up ? i : 7 - i;
        if (dir != turnaround && tryDir(dir)) return;
    }
    if (tryDir(turnaround)) return;
    t.moveDir = -1;
}

void Game::damageThing(Thing& t, int amount, bool byPlayer) {
    if (!t.shootable || t.health <= 0 || t.removed) return;
    t.health -= amount;
    if (t.type == ThingType::Barrel) {
        if (t.health <= 0) {
            t.state = Thing::State::Dying;
            t.tics = 0;
            t.byPlayer = byPlayer;
            sound(Sfx::Explode, t.pos);
        }
        return;
    }
    if (t.health <= 0) {
        t.state = Thing::State::Dying;
        t.tics = 0;
        t.solid = false;
        sound(Sfx::MonsterDeath, t.pos);
        if (byPlayer) ++m_player.kills;
        if (t.type == ThingType::Trooper) spawn(ThingType::Clip, t.pos).dropped = true;
        return;
    }
    if (t.state == Thing::State::Idle) wake(t);
    if (m_rng.nextFloat() * 256.0f < 200.0f) {
        t.state = Thing::State::Pain;
        t.tics = 0;
        sound(Sfx::MonsterPain, t.pos);
    }
}

void Game::radiusDamage(const glm::vec2& at, int amount, float range, bool byPlayer) {
    for (Thing& t : m_things) {
        if (!t.shootable || !t.alive() || t.removed) continue;
        const float d = std::max(0.0f, glm::length(t.pos - at) - t.radius);
        if (d < range && canSee(at, t.pos)) damageThing(t, static_cast<int>(static_cast<float>(amount) * (1.0f - d / range)), byPlayer);
    }
    const float d = std::max(0.0f, glm::length(m_player.pos - at) - kPlayerRadius);
    if (d < range && canSee(at, m_player.pos)) damagePlayer(static_cast<int>(static_cast<float>(amount) * (1.0f - d / range)), at);
}

void Game::hitscan(const glm::vec2& from, float angle, int damage, int shooter) {
    const glm::vec2 dir = heading(angle);
    float best = castRay(from, dir, 32.0f);
    Thing* hit = nullptr;
    bool hitPlayer = false;
    auto test = [&](const glm::vec2& pos, float radius) {
        const glm::vec2 rel = pos - from;
        const float along = glm::dot(rel, dir);
        if (along <= 0.0f || along > best) return -1.0f;
        const float perp2 = glm::dot(rel, rel) - along * along, r2 = radius * radius;
        if (perp2 > r2) return -1.0f;
        return along - std::sqrt(r2 - perp2);
    };
    for (Thing& t : m_things) {
        if (!t.shootable || !t.alive() || t.removed || t.id == shooter) continue;
        const float e = test(t.pos, t.radius + 0.04f);
        if (e >= 0.0f && e < best) {
            best = e;
            hit = &t;
        }
    }
    if (shooter != -1 && !m_player.dead) {
        const float e = test(m_player.pos, kPlayerRadius);
        if (e >= 0.0f && e < best) {
            best = e;
            hit = nullptr;
            hitPlayer = true;
        }
    }
    const glm::vec2 point = from + dir * best;
    if (hitPlayer) {
        damagePlayer(damage, from);
    } else if (hit) {
        const bool barrel = hit->type == ThingType::Barrel;
        damageThing(*hit, damage, shooter == -1);
        spawnEffect(barrel ? ThingType::Puff : ThingType::Blood, point - dir * 0.05f, 0.28f + m_rng.range(-0.08f, 0.08f));
    } else {
        spawnEffect(ThingType::Puff, point - dir * 0.04f, m_rng.range(0.2f, 0.55f));
    }
}

void Game::noiseAlert() {
    // Gunfire carries through every open cell and open door nearby.
    const int w = m_level.width();
    const glm::ivec2 start(static_cast<int>(std::floor(m_player.pos.x)), static_cast<int>(std::floor(m_player.pos.y)));
    std::vector<int> depth(m_alert.size(), -1);
    std::deque<glm::ivec2> queue{start};
    depth[static_cast<size_t>(start.y * w + start.x)] = 0;
    while (!queue.empty()) {
        const glm::ivec2 c = queue.front();
        queue.pop_front();
        m_alert[static_cast<size_t>(c.y * w + c.x)] = m_levelTics;
        const int dc = depth[static_cast<size_t>(c.y * w + c.x)];
        if (dc >= 40) continue;
        const glm::ivec2 nbs[4] = {{c.x + 1, c.y}, {c.x - 1, c.y}, {c.x, c.y + 1}, {c.x, c.y - 1}};
        for (const glm::ivec2& n : nbs) {
            if (!m_level.inside(n.x, n.y) || m_level.blocksSight(n.x, n.y) || depth[static_cast<size_t>(n.y * w + n.x)] >= 0) continue;
            depth[static_cast<size_t>(n.y * w + n.x)] = dc + 1;
            queue.push_back(n);
        }
    }
}

// ============================================================================
// Queries
// ============================================================================

bool Game::positionFree(const glm::vec2& p, float radius, int selfId, bool isPlayer) const {
    const int x0 = static_cast<int>(std::floor(p.x - radius)), x1 = static_cast<int>(std::floor(p.x + radius));
    const int y0 = static_cast<int>(std::floor(p.y - radius)), y1 = static_cast<int>(std::floor(p.y + radius));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            if (!m_level.blocksMove(x, y)) continue;
            const glm::vec2 closest(std::clamp(p.x, static_cast<float>(x), static_cast<float>(x + 1)),
                                    std::clamp(p.y, static_cast<float>(y), static_cast<float>(y + 1)));
            if (glm::dot(p - closest, p - closest) < radius * radius) return false;
        }
    for (const Thing& t : m_things) {
        if (!t.solid || t.removed || t.id == selfId) continue;
        const float r = radius + t.radius;
        if (glm::dot(p - t.pos, p - t.pos) < r * r) return false;
    }
    if (!isPlayer && !m_player.dead) {
        const float r = radius + kPlayerRadius;
        if (glm::dot(p - m_player.pos, p - m_player.pos) < r * r) return false;
    }
    return true;
}

bool Game::tryMove(glm::vec2& pos, const glm::vec2& delta, float radius, int selfId, bool isPlayer) const {
    if (positionFree(pos + delta, radius, selfId, isPlayer)) {
        pos += delta;
        return true;
    }
    // Slide along whatever is in the way.
    const glm::vec2 alongX(pos.x + delta.x, pos.y), alongY(pos.x, pos.y + delta.y);
    if (std::fabs(delta.x) > 1e-6f && positionFree(alongX, radius, selfId, isPlayer)) pos = alongX;
    else if (std::fabs(delta.y) > 1e-6f && positionFree(alongY, radius, selfId, isPlayer)) pos = alongY;
    return false;
}

float Game::castRay(const glm::vec2& from, const glm::vec2& dir, float maxDist) const {
    int mx = static_cast<int>(std::floor(from.x)), my = static_cast<int>(std::floor(from.y));
    const float dx = std::fabs(dir.x) < 1e-6f ? 1e30f : std::fabs(1.0f / dir.x), dy = std::fabs(dir.y) < 1e-6f ? 1e30f : std::fabs(1.0f / dir.y);
    const int sx = dir.x < 0.0f ? -1 : 1, sy = dir.y < 0.0f ? -1 : 1;
    float tx = (dir.x < 0.0f ? from.x - static_cast<float>(mx) : static_cast<float>(mx) + 1.0f - from.x) * dx;
    float ty = (dir.y < 0.0f ? from.y - static_cast<float>(my) : static_cast<float>(my) + 1.0f - from.y) * dy;
    while (true) {
        float t;
        if (tx < ty) {
            t = tx;
            tx += dx;
            mx += sx;
        } else {
            t = ty;
            ty += dy;
            my += sy;
        }
        if (t >= maxDist) return maxDist;
        if (m_level.blocksSight(mx, my)) return t;
    }
}

bool Game::canSee(const glm::vec2& a, const glm::vec2& b) const {
    const glm::vec2 d = b - a;
    const float len = glm::length(d);
    if (len < 1e-3f) return true;
    return castRay(a, d / len, len) >= len;
}

// ============================================================================
// Drawing
// ============================================================================

Spr Game::spriteFor(const Thing& t) const {
    switch (t.type) {
    case ThingType::Imp:
    case ThingType::Trooper: {
        const Spr base = t.type == ThingType::Imp ? Spr::ImpWalk : Spr::TrooperWalk;
        switch (t.state) {
        case Thing::State::Idle:   return base + (m_levelTics / 10 + t.id) % 2;
        case Thing::State::Chase:  return base + (m_levelTics / 6 + t.id) % 4;
        case Thing::State::Attack:
            if (t.type == ThingType::Imp) return base + (t.tics < 8 ? 4 : 5);
            return base + (t.tics >= 10 && t.tics < 14 ? 5 : 4);
        case Thing::State::Pain:   return base + 6;
        case Thing::State::Dying:  return base + 7 + std::min(4, t.tics / 5);
        default:                   return base + 11;
        }
    }
    case ThingType::Barrel:
        return t.state == Thing::State::Dying ? Spr::BarrelBoom + std::min(2, t.tics / 5) : Spr::Barrel + (m_levelTics / 12) % 2;
    case ThingType::Lamp:      return Spr::Lamp;
    case ThingType::Shotgun:   return Spr::ShotgunPickup;
    case ThingType::Clip:      return Spr::Clip;
    case ThingType::Shells:    return Spr::Shells;
    case ThingType::Medikit:   return Spr::Medikit;
    case ThingType::Stimpack:  return Spr::Stimpack;
    case ThingType::Armor:     return Spr::Armor;
    case ThingType::Fireball:  return Spr::Fireball + (t.tics / 4) % 2;
    case ThingType::Explosion: return Spr::FireBoom + std::min(2, t.tics / 5);
    case ThingType::Puff:      return Spr::Puff + std::min(2, t.tics / 4);
    case ThingType::Blood:     return Spr::Blood + std::min(2, t.tics / 5);
    default:                   return Spr::Puff;
    }
}

void Game::render() {
    switch (m_mode) {
    case Mode::Title:        drawTitle(); break;
    case Mode::Level:        drawLevel(); break;
    case Mode::Intermission: drawIntermission(); break;
    }
    if (m_message.tics > 0 && !m_message.text.empty()) {
        // Anomalous lines shiver on the screen.
        const int jitter = m_message.anomaly ? static_cast<int>(m_rng.next() % 3) - 1 : 0;
        m_renderer->drawText(2 + jitter, 2, m_message.text, m_message.anomaly ? pal(Grey, 15) : pal(Red, 12));
    }
    if (m_melting) m_renderer->meltOver(m_meltFrom, m_meltOffsets);
    const Player& p = m_player;
    const bool playing = m_mode == Mode::Level;
    const float red = playing ? static_cast<float>(std::min(8, (p.damageCount + 7) / 8)) / 8.0f * 0.55f : 0.0f;
    const float yellow = playing ? static_cast<float>(std::min(4, (p.bonusCount + 7) / 8)) / 4.0f * 0.22f : 0.0f;
    m_renderer->present(m_frame, red, yellow);
}

void Game::drawTitle() {
    SoftwareRenderer& r = *m_renderer;
    const DoomAssets& a = DoomAssets::get();
    r.drawImage(a.title(), 0, 0);
    auto centered = [&](int y, const std::string& s, uint8_t color, int scale = 1) {
        r.drawText((kScreenW - SoftwareRenderer::textWidth(s, scale)) / 2, y, s, color, scale);
    };
    if ((m_tic / 18) % 2 == 0) centered(136, "PRESS FIRE TO PLAY", pal(Yellow, 14));
    centered(150, "ESC TO QUIT", pal(Grey, 10));
    char seed[64];
    std::snprintf(seed, sizeof(seed), "NO WAD FOUND. SYNTHESIZED FROM SEED %08X", static_cast<unsigned>(m_seed & 0xFFFFFFFFu));
    centered(186, seed, pal(Grey, 7));
}

void Game::drawLevel() {
    SoftwareRenderer& r = *m_renderer;
    const DoomAssets& a = DoomAssets::get();
    const Player& p = m_player;

    m_sprites.clear();
    for (const Thing& t : m_things)
        if (!t.removed) m_sprites.push_back({t.pos, t.z, spriteFor(t)});
    ViewParams view;
    view.pos = p.pos;
    view.angle = p.angle;
    view.eyeZ = p.viewZ;
    view.tic = m_levelTics;
    view.extraLight = p.extraLight > 0 ? 1 : 0;
    r.drawView(m_level, view, m_sprites);

    // The weapon, bobbing as you walk, lit by the light where you stand.
    const Image* gun = nullptr;
    const Image* flash = nullptr;
    const bool firing = p.weaponTics > 0;
    if (p.weapon == Weapon::Pistol) {
        gun = &a.gun(firing && p.fireTic < 6 ? Gun::PistolFire : Gun::Pistol);
        if (firing && p.fireTic < 3) flash = &a.gun(Gun::PistolFlash);
    } else {
        const int f = p.fireTic;
        Gun g = Gun::Shotgun;
        if (firing) g = f < 5 ? Gun::ShotgunFire : f < 12 ? Gun::Shotgun : f < 20 ? Gun::ShotgunPump1 : f < 28 ? Gun::ShotgunPump2 : f < 33 ? Gun::ShotgunPump1 : Gun::Shotgun;
        gun = &a.gun(g);
        if (firing && f < 4) flash = &a.gun(Gun::ShotgunFlash);
    }
    const float phase = static_cast<float>(m_levelTics) * 2.0f * kPi / 40.0f;
    const float bob = firing ? 0.0f : p.bob;
    const int x = kScreenW / 2 - gun->w / 2 + static_cast<int>(std::cos(phase) * 8.0f * bob);
    const int y = kViewH - gun->h + 4 + static_cast<int>(std::fabs(std::sin(phase)) * 6.0f * bob + p.drop * static_cast<float>(gun->h));
    const int cellLight = m_level.lightAt(static_cast<int>(std::floor(p.pos.x)), static_cast<int>(std::floor(p.pos.y)), m_levelTics);
    const int light = std::clamp((255 - cellLight) / 10 - 2 - (p.extraLight > 0 ? 4 : 0), 0, kColormaps - 1);
    if (flash) {
        const int muzzleX = x + gun->w / 2, muzzleY = y + (p.weapon == Weapon::Pistol ? 6 : 1);
        r.drawImage(*flash, muzzleX - flash->w / 2, muzzleY - flash->h * 3 / 4, 0, kViewH);
    }
    r.drawImage(*gun, x, y, light, kViewH);

    if (p.dead && p.deadTics > 35) {
        const std::string s = "PRESS USE TO TRY AGAIN";
        r.drawText((kScreenW - SoftwareRenderer::textWidth(s)) / 2, kViewH / 2 - 20, s, pal(Red, 12));
    }
    drawStatusBar();
}

void Game::drawStatusBar() {
    SoftwareRenderer& r = *m_renderer;
    const DoomAssets& a = DoomAssets::get();
    const Player& p = m_player;
    const int top = kViewH;
    r.drawImage(a.statusBar(), 0, top);
    r.drawBigNumber(47, top + 3, p.weapon == Weapon::Pistol ? p.bullets : p.shells, false);
    r.drawBigNumber(104, top + 3, p.health, true);
    r.drawBigNumber(232, top + 3, p.armor, true);

    // Arms: the slots you own light up.
    const char* slots = "234567";
    for (int i = 0; i < 6; ++i) {
        const bool owned = i == 0 || (i == 1 && p.hasShotgun);
        r.drawText(111 + (i % 3) * 10, top + 3 + (i / 3) * 9, std::string(1, slots[i]), owned ? pal(Yellow, 14) : pal(Grey, 6));
    }

    // The face.
    const int level = p.health >= 80 ? 0 : p.health >= 60 ? 1 : p.health >= 40 ? 2 : p.health >= 20 ? 3 : 4;
    int face = faceLook(level, p.faceLook);
    if (p.dead) face = kFaceDead;
    else if (p.ouchTics > 0) face = faceOuch(level);
    else if (p.grinTics > 0) face = faceGrin(level);
    r.drawImage(a.face(face), 148, top + 2);

    char line[32];
    std::snprintf(line, sizeof(line), "%3d/200", p.bullets);
    r.drawText(270, top + 5, line, pal(Yellow, 13));
    std::snprintf(line, sizeof(line), "%3d/ 50", p.shells);
    r.drawText(270, top + 13, line, pal(Yellow, 13));
}

void Game::drawIntermission() {
    SoftwareRenderer& r = *m_renderer;
    const DoomAssets& a = DoomAssets::get();
    r.tileFlat(a.flat(FlatTex::Plate), 0, 0, kScreenW, kScreenH, 16);
    auto centered = [&](int y, const std::string& s, uint8_t color, int scale) {
        r.drawText((kScreenW - SoftwareRenderer::textWidth(s, scale)) / 2, y, s, color, scale);
    };
    centered(18, m_level.name, pal(Red, 13), 2);
    centered(40, "FINISHED", pal(Red, 13), 2);
    const char* labels[] = {"KILLS", "ITEMS", "TIME"};
    for (int i = 0; i < 3; ++i) {
        const int y = 80 + i * 26;
        r.drawText(56, y, labels[i], pal(Red, 12), 2);
        if (i < 2) {
            r.drawBigNumber(264, y - 1, m_interShown[static_cast<size_t>(i)], true);
        } else {
            char t[16];
            std::snprintf(t, sizeof(t), "%d:%02d", m_interShown[2] / 60, m_interShown[2] % 60);
            r.drawText(264 - SoftwareRenderer::textWidth(t, 2), y, t, pal(Red, 12), 2);
        }
    }
    if (m_interShown == m_interTarget && (m_tic / 18) % 2 == 0) centered(170, "PRESS FIRE TO CONTINUE", pal(Yellow, 14), 1);
}

} // namespace doom
