// ---------------------------------------------------------------------------
// EnginePuzzle.cpp
// The Engine's half of the way out of the Backrooms: carrying the puzzle
// chain's answers into the world (the exit chunk and its glitch room),
// moving the chain on when a terminal or a phone reports what the player
// found, the noclip through the glitching wall, the office on the other side
// - and the phones that ring by themselves, in both.
// ---------------------------------------------------------------------------
#include "Core/Engine.h"

#include "AI/EntityDirector.h"
#include "AI/NavGrid.h"
#include "Actors/Phone.h"
#include "Actors/Player.h"
#include "Audio/Soundscape.h"
#include "Audio/StorySounds.h"
#include "Core/ConsoleLog.h"
#include "Gameplay/PhoneCall.h"
#include "Gameplay/PuzzleChain.h"
#include "Gameplay/TerminalConsole.h"
#include "Gameplay/TeslaGun.h"
#include "Math/Random.h"
#include "Physics/Physics.h"
#include "Render/Renderer.h"
#include "World/ChunkManager.h"
#include "World/OfficeLayout.h"
#include "World/WorldGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <unordered_map>

namespace {

constexpr float kRingCycle = 6.0f;  ///< A ring of the bell every 6 s (2 s on, 4 s off)...
constexpr int   kRings     = 6;     ///< ...this many times, then it gives up.
constexpr float kBellGain  = 0.71f;
constexpr float kBellCarry = 4.0f;  ///< A bell carries: full level within 4 m, then 1/distance.
constexpr float kRingNear  = 3.0f;  ///< The phone that rings is no nearer than this...
constexpr float kRingFar   = 16.0f; ///< ...and no farther if it is in view...
constexpr float kRingFarWall = 8.0f;  ///< ...or than this behind one wall (never more): a bright bell loses a lot to a wall.

inline float smooth01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}
inline float yawToward(const glm::vec3& d) { return std::atan2(-d.x, -d.z); }
inline float pitchToward(const glm::vec3& d) { return std::asin(std::clamp(d.y / std::max(glm::length(d), 1e-4f), -1.0f, 1.0f)); }

} // namespace

// ---- The chain ---------------------------------------------------------------------------------

void Engine::advancePuzzle(PuzzleStage stage, const char* why) {
    if (m_puzzle->advance(stage)) con::spoiler("PUZZLE", con::format("{%s}: %s", PuzzleChain::stageName(stage), why));
}

void Engine::handlePuzzleEvents(const std::vector<PuzzleEvent>& events) {
    for (const PuzzleEvent& e : events) {
        switch (e.kind) {
        case PuzzleEvent::Kind::MemoryShown:
            advancePuzzle(PuzzleStage::MemoryFound, "7A9F scrolled past on a terminal");
            break;
        case PuzzleEvent::Kind::Pinged:
            if (m_office) break; // out here the maps are "here"
            if (m_puzzle->mapExit(e.from)) armExit(*m_puzzle->exitChunk());
            advancePuzzle(PuzzleStage::TargetMapped, "the host in the memory answered a ping");
            break;
        case PuzzleEvent::Kind::ExitMapped:
            advancePuzzle(PuzzleStage::ExitLocated, "a terminal's MAP shows the glitch room");
            break;
        }
    }
}

void Engine::armExit(const ChunkCoord& exit) {
    m_world->setExitChunk(exit);
    // Rebuild it if it is loaded - and its neighbours, whose writing may be on the other face of a wall that now glitches.
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) m_chunks->reload({exit.x + dx, exit.z + dz, exit.level});
    }
    if (const auto& cell = m_world->exitCell()) {
        con::spoiler("PUZZLE", con::format("The exit: chunk {(%d, %d)} on level {%d}, the room in cell {(%d, %d)}", exit.x, exit.z,
                                           exit.level, cell->x, cell->y));
    }
}

// ---- Noclip -------------------------------------------------------------------------------------

void Engine::checkNoclip() {
    if (m_state != GameState::Running || m_office || !m_world->exitCell()) return;
    if (m_chunks->touchesGlitch(m_player->bodyBox())) startNoclip();
}

void Engine::startNoclip() {
    const glm::vec3 look = m_player->lookDirection();
    m_noclipDir = glm::length(glm::vec2(look.x, look.z)) > 1e-3f ? glm::normalize(glm::vec3(look.x, 0.0f, look.z)) : glm::vec3(0.0f, 0.0f, -1.0f);
    m_noclipTimer = 0.0f;
    m_noclipSwitched = false;
    m_monologueStarted = false;
    stopRinging();
    m_sound->playInHead(SoundId::NoclipTear, 0.38f); // loud enough to tear reality, not the player's ears
    setState(GameState::Noclip);
    con::spoiler("PUZZLE", con::format("Noclip: through the wall at {(%.1f, %.1f)} on level {%d}", m_player->feetPosition().x,
                                       m_player->feetPosition().z, m_focusLevel));
}

void Engine::updateNoclip(float dt) {
    m_noclipTimer += dt;
    const float t = m_noclipTimer;
    if (!m_noclipSwitched) {
        // Reality tears, the camera slides into the wall, the screen goes black.
        m_glitch = smooth01(t / 0.6f);
        m_fade = smooth01((t - 0.9f) / 1.0f);
        if (t >= 2.1f) {
            enterOffice();
            m_noclipSwitched = true;
        }
        return;
    }
    // ...and comes back: a cubicle, an office, the hum of the lights.
    m_glitch = 1.0f - smooth01((t - 2.3f) / 1.8f);
    m_fade = 1.0f - smooth01((t - 2.8f) / 1.8f);
    if (!m_monologueStarted && t >= 4.0f) {
        m_sound->playInHead(SoundId::Monologue, 0.9f);
        say(storysfx::monologueText(), m_sound->bank().duration(SoundId::Monologue, 0) + 1.5f);
        m_monologueStarted = true;
    }
    if (t >= 4.7f) {
        m_glitch = 0.0f;
        m_fade = 0.0f;
        setState(GameState::Running);
        advancePuzzle(PuzzleStage::Escaped, "noclipped out of the Backrooms");
    }
}

void Engine::enterOffice() {
    // Behind the black screen, the world is swapped for the office.
    m_office = true;
    m_world->setRealm(world::Realm::Office);
    m_chunks->clear();
    m_entities->setEnabled(false); // nobody is here
    m_consoles.clear();
    m_console = nullptr;
    m_terminalId = m_lastTerminalId = 0;
    m_terminalBlend = 0.0f;
    m_renderer->resetTerminal();
    m_call.reset();
    m_phoneId = 0;
    m_phoneBlend = 0.0f;
    m_cabinetId = 0;
    m_cabinetBlend = 0.0f;
    m_sound->stopEarpiece();
    m_sound->setMonitorHum(false, glm::vec3(0.0f), *m_world);
    m_sound->setTerminalMusic(false, glm::vec3(0.0f), *m_world);
    m_autopilot.clear();
    m_autopilotIndex = 0;
    stopRinging();
    m_nextRing = 30.0f; // the phones here ring too

    glm::vec3 feet;
    float yaw = 0.0f;
    office::spawn(feet, yaw);
    teleportPlayer(feet, office::kLevel, yaw);
    if (!m_physics->isFree(Physics::bodyBox(feet + glm::vec3(0.0f, 0.01f, 0.0f), m_player->shape()), *m_chunks)) {
        teleportPlayer(m_chunks->findSpawnPoint(feet, office::kLevel, m_player->shape(), *m_physics), office::kLevel, yaw);
    }
    m_player->setViewAngles(yaw, glm::radians(-14.0f)); // looking down at the desk
    con::spoiler("PUZZLE", con::format("The office: {%zu} chunks, the player at {(%.2f, %.2f)}", m_chunks->chunkCount(),
                                       m_player->feetPosition().x, m_player->feetPosition().z));
}

void Engine::say(const std::string& line, float seconds) {
    m_dialogue = line;
    m_dialogueTimer = seconds;
}

// ---- Phones ringing ------------------------------------------------------------------------------

void Engine::stopRinging() {
    if (m_ringingPhone) {
        if (Phone* phone = m_chunks->phoneById(m_ringingPhone)) phone->setRinging(false); // its lamp goes dark
    }
    m_ringingPhone = 0;
    m_ringTime = 0.0f;
}

ArcLight Engine::ringLight() const {
    ArcLight light;
    const Phone* phone = m_ringingPhone ? static_cast<const ChunkManager&>(*m_chunks).phoneById(m_ringingPhone) : nullptr;
    if (!phone) return light;
    // The same flash as the lamp itself (the world shader's ringing lamp): fast
    // during each 2 s ring of the cadence, a faint glow between rings.
    const float blink = m_ringTime * 10.0f - std::floor(m_ringTime * 10.0f);
    const float flash = std::fmod(m_ringTime, kRingCycle) < 2.0f ? (blink >= 0.45f ? 1.0f : 0.0f) : 0.15f;
    light.position = glm::vec3(phone->lampMatrix()[3]) + glm::vec3(0.0f, 0.06f, 0.0f);
    light.color = glm::vec3(1.0f, 0.12f, 0.06f);
    light.intensity = 0.45f * flash; // a little lamp: it tints the desk round it, no more
    light.range = 1.75f;
    return light;
}

void Engine::updateRinger(float dt) {
    if (m_ringingPhone) {
        Phone* phone = m_chunks->phoneById(m_ringingPhone);
        if (!phone || phone->offHook()) {
            stopRinging();
            return;
        }
        phone->setRinging(true); // (every frame: its chunk may have been rebuilt)
        const float before = m_ringTime;
        m_ringTime += dt;
        if (before == 0.0f || std::floor(before / kRingCycle) != std::floor(m_ringTime / kRingCycle)) {
            if (m_ringTime < kRingCycle * kRings) {
                m_sound->playEffect(SoundId::PhoneBell, phone->center(), kBellGain, *m_world, kBellCarry);
                m_noises.push_back({phone->center(), cfg::kNoiseMachine * 1.4f, NoiseKind::Machine}); // so does the Wanderer
            } else {
                stopRinging(); // nobody answered
            }
        }
        return;
    }
    // (Not in the scripted scenes, whose timing it would upset - but for the ones that ask for it.)
    if (!m_options.demo.empty() && m_options.demo != "ringer" && m_options.demo != "office") return;
    if (m_state != GameState::Running && m_state != GameState::Terminal && m_state != GameState::Cabinet) return;
    if ((m_nextRing -= dt) > 0.0f) return;
    rnd::Rng rng(rnd::hashCombine(m_options.seed, static_cast<uint64_t>(m_simTime * 1000.0) ^ 0xB311ull));
    m_nextRing = rng.range(70.0f, 160.0f);
    // A phone the player can hear, but not right in front of them: in view if
    // there is one (the same room, or through an opening), else one wall away.
    const Phone* pick[2] = {nullptr, nullptr}; // by the walls in the way
    int seen[2] = {0, 0};
    const glm::vec3 eye = m_player->eyePosition();
    for (Chunk* chunk : m_chunks->sortedChunksMutable()) {
        if (chunk->coord().level != m_focusLevel) continue;
        for (const Phone& p : chunk->phones()) {
            const float d = glm::length(p.center() - eye);
            if (d < kRingNear || d > kRingFar || p.offHook() || p.id() == m_phoneId) continue;
            const int walls = m_world->wallsBetween(m_focusLevel, glm::vec2(eye.x, eye.z), glm::vec2(p.center().x, p.center().z), 2);
            if (walls > 1 || (walls == 1 && d > kRingFarWall)) continue;
            if (rng.rangeInt(0, seen[walls]++) == 0) pick[walls] = &p; // uniform among them
        }
    }
    const int walls = pick[0] ? 0 : 1;
    if (!pick[walls]) {
        m_nextRing = 20.0f;
        return;
    }
    m_ringingPhone = pick[walls]->id();
    m_ringTime = 0.0f;
    if (!m_options.demo.empty()) {
        std::printf("[Demo] t=%.2f a phone %.1fm away starts ringing (%s; id %llx)\n", m_demoTime, glm::length(pick[walls]->center() - eye),
                    walls == 0 ? "in view" : "one wall away", static_cast<unsigned long long>(m_ringingPhone));
    }
}

void Engine::updateGlitchHum(float dt) {
    // The glitch room fizzes and crackles: it can be heard before it is found.
    const auto& cell = m_world->exitCell();
    if (m_office || !cell || cell->z != m_focusLevel) return;
    const glm::vec3 centre((static_cast<float>(cell->x) + 0.5f) * world::kCellSize, world::levelFloorY(cell->z) + 1.4f,
                           (static_cast<float>(cell->y) + 0.5f) * world::kCellSize);
    const float dist = glm::length(centre - m_player->eyePosition());
    if (dist > 22.0f || (m_glitchHumTimer -= dt) > 0.0f) return;
    rnd::Rng rng(rnd::hashCombine(m_options.seed, static_cast<uint64_t>(m_simTime * 1000.0) ^ 0x6117ull));
    m_glitchHumTimer = rng.range(0.15f, 0.5f) + 0.05f * dist;
    const glm::vec3 at = centre + glm::vec3(rng.range(-2.0f, 2.0f), rng.range(-1.0f, 1.0f), rng.range(-2.0f, 2.0f));
    m_sound->playEffect(SoundId::TerminalGlitch, at, rng.range(0.25f, 0.45f), *m_world);
}

// ---- Developer scenes -----------------------------------------------------------------------------

void Engine::setupPuzzleDemo() {
    const std::string& demo = m_options.demo;
    const glm::vec3 feet = m_player->feetPosition();
    // Sits down at a terminal (found again by id after the teleport, which may rebuild its chunk).
    auto sitAt = [this](uint64_t id) {
        Terminal* t = m_chunks->terminalById(id);
        if (!t) return;
        glm::vec3 seat = t->viewPoint() + t->screenNormal() * 0.3f;
        seat.y = world::levelFloorY(m_focusLevel);
        teleportPlayer(seat, m_focusLevel, yawToward(t->screenCenter() - seat));
        if (Terminal* again = m_chunks->terminalById(id)) enterTerminal(*again);
        m_terminalBlend = 1.0f;
    };
    auto nearestTerminal = [&]() -> const Terminal* {
        const Terminal* best = nullptr;
        float bestDist = 1e9f;
        for (Chunk* chunk : m_chunks->sortedChunksMutable()) {
            if (chunk->coord().level != m_focusLevel) continue;
            for (const Terminal& t : chunk->terminals()) {
                const float d = glm::length(t.center() - feet);
                if (d < bestDist) {
                    bestDist = d;
                    best = &t;
                }
            }
        }
        return best;
    };

    if (demo == "clue") {
        // In front of the nearest phone number on a wall (--type <n>: the n-th nearest writing of any kind;
        // --type doom: the nearest note about DOOM).
        const bool doom = m_options.demoInput == "doom";
        const int nth = m_options.demoInput.empty() || doom ? -1 : std::atoi(m_options.demoInput.c_str());
        int doomNotes = 0;
        std::vector<std::pair<float, const ClueSpot*>> spots;
        int chunks = 0, writings = 0, numbers = 0;
        for (Chunk* chunk : m_chunks->sortedChunksMutable()) {
            if (chunk->coord().level != m_focusLevel) continue;
            ++chunks;
            for (const ClueSpot& c : chunk->clues()) {
                ++writings;
                numbers += c.clue ? 1 : 0;
                doomNotes += c.doomNote ? 1 : 0;
                if (doom ? !c.doomNote : nth < 0 && !c.clue) continue;
                spots.emplace_back(glm::length(c.position - feet), &c);
            }
        }
        std::sort(spots.begin(), spots.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::printf("[Demo] %d loaded chunks on this storey: %d writings, %d of them the number, %d the note about DOOM\n", chunks,
                    writings, numbers, doomNotes);
        const size_t index = static_cast<size_t>(std::max(nth, 0));
        const ClueSpot* best = index < spots.size() ? spots[index].second : nullptr;
        const float bestDist = best ? spots[index].first : 0.0f;
        if (!best) {
            std::cerr << "[Demo] No phone number written anywhere nearby\n";
            return;
        }
        const glm::vec3 at = best->position;
        glm::vec3 stand = at + best->normal * 1.2f;
        stand.y = world::levelFloorY(m_focusLevel);
        std::printf("[Demo] %s on a wall %.1fm away\n", doom ? "The note about DOOM is" : nth < 0 ? "The number is" : "That writing is", bestDist);
        teleportPlayer(stand, m_focusLevel, yawToward(at - stand));
        m_player->setViewAngles(yawToward(at - stand), pitchToward(at - m_player->eyePosition()));
    } else if (demo == "hexstream" || demo == "exit-map") {
        const Terminal* t = nearestTerminal();
        if (!t) {
            std::cerr << "[Demo] No terminal loaded\n";
            return;
        }
        const uint64_t id = t->id();
        if (demo == "hexstream") {
            advancePuzzle(PuzzleStage::NumberDialed, "(scene) the call was made");
        } else {
            // Its chunk becomes the exit.
            const ChunkCoord c = ChunkCoord::fromWorld(t->center().x, t->center().z, m_focusLevel);
            m_puzzle->advance(PuzzleStage::TargetMapped);
            armExit(c);
        }
        sitAt(id);
    } else if (demo == "glitch") {
        // This chunk becomes the exit; stand in the glitch room, facing one of its walls.
        armExit(ChunkCoord::fromWorld(feet.x, feet.z, m_focusLevel));
        const auto& cell = m_world->exitCell();
        if (!cell) {
            std::cerr << "[Demo] No room for the exit here\n";
            return;
        }
        const float S = world::kCellSize;
        const glm::vec3 centre((static_cast<float>(cell->x) + 0.5f) * S, world::levelFloorY(m_focusLevel), (static_cast<float>(cell->y) + 0.5f) * S);
        glm::vec3 wall = centre + glm::vec3(1.2f, 0.0f, 0.0f); // the free-standing slab, if there are no walls
        const struct { int gx, gz; world::EdgeAxis axis; glm::vec3 dir; } sides[4] = {
            {cell->x, cell->y, world::EdgeAxis::West, {-1, 0, 0}}, {cell->x + 1, cell->y, world::EdgeAxis::West, {1, 0, 0}},
            {cell->x, cell->y, world::EdgeAxis::South, {0, 0, -1}}, {cell->x, cell->y + 1, world::EdgeAxis::South, {0, 0, 1}}};
        for (const auto& side : sides) {
            if (m_world->edge(m_focusLevel, side.gx, side.gz, side.axis) == world::EdgeType::Wall) {
                wall = centre + side.dir * (S * 0.5f);
                break;
            }
        }
        const glm::vec3 toWall = glm::normalize(wall - centre);
        const glm::vec3 stand = wall - toWall * 2.2f;
        teleportPlayer(stand, m_focusLevel, yawToward(toWall));
        m_player->setViewAngles(yawToward(toWall), 0.0f);
        if (m_options.demoInput == "walk") m_autopilot.push_back({wall + toWall * 0.5f}); // straight into it
        m_autopilotIndex = 0;
    } else if (demo == "office") {
        // Straight through: the noclip from the moment the screen is black.
        startNoclip();
        m_noclipTimer = 2.05f;
    } else if (demo == "ringer") {
        m_nextRing = 1.0f;
    } else if (demo == "terminal-pause") {
        if (const Terminal* t = nearestTerminal()) sitAt(t->id());
        else std::cerr << "[Demo] No terminal loaded\n";
    } else if (demo == "clue-survey") {
        // How often does exploring walk past the number? The explore scene's
        // routes (a walk that prefers rooms it has not been through), walked
        // on paper: every copy of the number on the walls of a room entered
        // counts once. Then route 1 is walked for real, to time the pace.
        const int kN = world::kChunkCells;
        std::unordered_map<ChunkCoord, std::vector<glm::vec3>, ChunkCoordHash> numbers; // per chunk: where the number faces
        auto numbersIn = [&](const ChunkCoord& c) -> const std::vector<glm::vec3>& {
            auto it = numbers.find(c);
            if (it == numbers.end()) {
                std::vector<glm::vec3> found;
                for (const ClueSpot& s : m_world->generate(c).clues) {
                    if (s.clue) found.push_back(s.position + s.normal * 0.5f); // a point in the room it faces
                }
                it = numbers.emplace(c, std::move(found)).first;
            }
            return it->second;
        };
        const int routes = m_options.demoInput.empty() ? 40 : std::max(1, std::atoi(m_options.demoInput.c_str()));
        const int steps = 600;
        long rooms = 0;
        int passed = 0;
        for (int r = 1; r <= routes; ++r) {
            const std::vector<AutopilotPoint> route = exploreRoute(feet, m_focusLevel, steps, static_cast<uint64_t>(r));
            std::vector<glm::vec3> seen; // copies already walked past on this route
            for (size_t i = 1; i < route.size(); i += 2) { // each step's second point is in the room entered
                const glm::ivec2 cell = NavGrid::cellOf(route[i].pos);
                ++rooms;
                const ChunkCoord c{world::floorDiv(cell.x, kN), world::floorDiv(cell.y, kN), m_focusLevel};
                for (const glm::vec3& p : numbersIn(c)) {
                    if (NavGrid::cellOf(p) != cell) continue;
                    if (std::any_of(seen.begin(), seen.end(), [&](const glm::vec3& q) { return glm::length(q - p) < 0.01f; })) continue;
                    seen.push_back(p);
                    ++passed;
                }
            }
        }
        std::printf("[Survey] %d routes, %ld rooms entered, the number walked past %d times: once every %.1f rooms\n", routes, rooms,
                    passed, passed > 0 ? static_cast<double>(rooms) / passed : 0.0);
        m_autopilot = exploreRoute(feet, m_focusLevel, steps, 1);
        m_autopilotIndex = 0;
    }
}

void Engine::updatePuzzleDemo() {
    const std::string& demo = m_options.demo;
    auto dumpScreen = [this] {
        if (!m_console) return;
        const TerminalScreen& screen = m_console->screen();
        for (int r = 0; r < TerminalScreen::kRows; ++r) {
            std::string row, marks;
            for (int c = 0; c < TerminalScreen::kCols; ++c) {
                row += screen.at(c, r).ch;
                marks += screen.at(c, r).color == TerminalScreen::Beacon ? '*' : ' ';
            }
            row.erase(row.find_last_not_of(' ') + 1);
            marks.erase(marks.find_last_not_of(' ') + 1);
            std::printf("[Screen] |%s\n", row.c_str());
            if (!marks.empty()) std::printf("[Beacon] |%s\n", marks.c_str());
        }
    };
    auto command = [this](const char* text) {
        if (!m_console) return;
        m_console->type(text);
        m_console->submit(terminalContext());
    };
    if ((demo == "ringer" || demo == "office") && m_ringingPhone && (m_options.demoInput == "look" || m_options.demoInput == "look-far")) {
        // --type look / look-far: stand 3 m / 8 m in front of the ringing phone, looking at it.
        static uint64_t placed = 0;
        if (const Phone* phone = m_chunks->phoneById(m_ringingPhone)) {
            if (placed != m_ringingPhone) {
                const glm::vec3 front = glm::normalize(glm::vec3(phone->modelMatrix() * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
                const glm::vec3 out = glm::normalize(glm::vec3(front.x, 0.0f, front.z));
                // As far back as asked - but not out of the room (no wall between the player and the phone).
                glm::vec3 stand = phone->center() + out * 2.0f;
                for (float d = m_options.demoInput == "look" ? 3.0f : 8.0f; d > 2.0f; d -= 0.5f) {
                    const glm::vec3 p = phone->center() + out * d;
                    if (m_world->wallsBetween(m_focusLevel, glm::vec2(phone->center().x, phone->center().z), glm::vec2(p.x, p.z), 1) == 0) {
                        stand = p;
                        break;
                    }
                }
                stand.y = world::levelFloorY(m_focusLevel);
                m_player->teleport(stand, m_player->yaw());
                std::printf("[Demo] Looking at the ringing phone from %.1fm\n", glm::length(glm::vec2(stand.x - phone->center().x, stand.z - phone->center().z)));
                placed = m_ringingPhone;
            }
            const glm::vec3 d = phone->center() - m_player->eyePosition();
            m_player->setViewAngles(yawToward(d), pitchToward(d));
        }
    }
    if (demo == "terminal-pause") {
        // SPACE on the live log, ESC, sit down again, SPACE: does the stream come back?
        auto press = [this](SDL_Scancode code, const char* text) {
            SDL_KeyboardEvent key{};
            key.scancode = code;
            handleTerminalKey(key);
            if (text && m_console) m_console->type(text); // the text event a real key press sends along
        };
        auto screenHash = [this] {
            uint64_t h = 0;
            if (m_console) {
                for (const TerminalScreen::Cell& c : m_console->screen().cells) h = rnd::hashCombine(h, static_cast<uint64_t>(c.ch));
            }
            return h;
        };
        auto promptRow = [this] {
            std::string row;
            if (m_console) {
                for (int c = 0; c < TerminalScreen::kCols; ++c) row += m_console->screen().at(c, TerminalScreen::kRows - 1).ch;
            }
            row.erase(row.find_last_not_of(' ') + 1);
            return row;
        };
        static uint64_t before = 0;
        const bool paused = m_console && m_console->streamPaused();
        if (m_demoStep == 0 && m_demoTime > 2.0f) {
            press(SDL_SCANCODE_SPACE, " ");
            std::printf("[Demo] t=%.1f SPACE: paused %s\n", m_demoTime, m_console && m_console->streamPaused() ? "yes" : "no");
            m_demoStep = 1;
        } else if (m_demoStep == 1 && m_demoTime > 3.0f) {
            press(SDL_SCANCODE_ESCAPE, nullptr);
            std::printf("[Demo] t=%.1f ESC: %s\n", m_demoTime, m_state == GameState::Terminal ? "still seated" : "left the terminal");
            m_demoStep = 2;
        } else if (m_demoStep == 2 && m_demoTime > 4.5f) {
            if (Terminal* t = m_chunks->terminalById(m_lastTerminalId)) enterTerminal(*t);
            std::printf("[Demo] t=%.1f sat down again\n", m_demoTime);
            m_demoStep = 3;
        } else if (m_demoStep == 3 && m_demoTime > 6.0f) {
            std::printf("[Demo] t=%.1f back at it: paused %s, prompt row \"%s\"\n", m_demoTime, paused ? "yes" : "no", promptRow().c_str());
            press(SDL_SCANCODE_SPACE, " ");
            std::printf("[Demo] t=%.1f SPACE: paused %s\n", m_demoTime, m_console && m_console->streamPaused() ? "yes" : "no");
            before = screenHash();
            m_demoStep = 4;
        } else if (m_demoStep == 4 && m_demoTime > 9.0f) {
            std::printf("[Demo] t=%.1f three seconds later: the stream is %s; prompt row \"%s\"\n", m_demoTime,
                        screenHash() != before ? "moving" : "STUCK", promptRow().c_str());
            m_demoStep = 5;
        }
    }
    if (demo == "clue-survey") {
        // The pace of exploring: rooms walked through per minute (each step is two waypoints).
        static int reported = 0;
        if (m_demoTime >= 60.0f * static_cast<float>(reported + 1)) {
            ++reported;
            std::printf("[Survey] t=%.0fs: %zu rooms walked, %.2f s per room\n", m_demoTime, m_autopilotIndex / 2,
                        m_demoTime / std::max<float>(1.0f, static_cast<float>(m_autopilotIndex / 2)));
        }
    }
    if (demo == "hexstream") {
        static float streaming = -1.0f;
        if (m_demoStep == 0 && m_demoTime > 1.5f) {
            command("mode hex");
            m_demoStep = 1;
        } else if (m_demoStep == 1 && m_demoTime > 2.5f) {
            command("stream");
            streaming = m_demoTime;
            m_demoStep = 2;
        } else if (m_demoStep == 2 && m_puzzle->reached(PuzzleStage::MemoryFound)) {
            std::printf("[Demo] 7A9F came up after %.1fs of HEX streaming\n", m_demoTime - streaming);
            m_demoStep = 3;
            m_demoWait = m_demoTime + 0.3f;
        } else if (m_demoStep == 3 && m_demoTime > m_demoWait) {
            m_console->toggleStreamPause(); // SPACE: hold it there
            std::printf("[Demo] Stream paused: %s\n", m_console->streamPaused() ? "yes" : "NO");
            m_demoStep = 4;
        } else if (m_demoStep == 4 && m_demoTime > m_demoWait + 1.5f) {
            dumpScreen();
            m_screenshotRequested = true;
            if (!m_options.demoInput.empty()) { // and ping what is on the screen
                command(("ping " + m_puzzle->secrets().ipAddress).c_str());
            }
            m_demoStep = 5;
        } else if (m_demoStep == 5 && !m_options.demoInput.empty() && m_demoTime > m_demoWait + 9.0f) {
            dumpScreen();
            m_demoStep = 6;
        } else if (m_demoStep == 2 && m_demoTime - streaming > 180.0f) {
            std::printf("[Demo] 7A9F did not come up in 3 minutes\n");
            m_demoStep = 6;
        }
    } else if (demo == "exit-map") {
        if (m_demoStep == 0 && m_demoTime > 1.5f) {
            command("map");
            m_demoStep = 1;
        } else if (m_demoStep == 1 && m_demoTime > 4.5f) {
            dumpScreen();
            m_screenshotRequested = true;
            m_demoStep = 2;
        }
    } else if (demo == "glitch" || demo == "office") {
        static GameState last = GameState::Running;
        static bool officeLogged = false;
        if (m_state != last) {
            std::printf("[Demo] t=%.2f %s\n", m_demoTime, m_state == GameState::Noclip ? "noclipping" : m_state == GameState::Running ? "running" : "other");
            last = m_state;
        }
        if (m_office && m_state == GameState::Running && !officeLogged) {
            std::printf("[Demo] t=%.2f in the office; puzzle %s; entities left %d\n", m_demoTime, PuzzleChain::stageName(m_puzzle->stage()),
                        m_entities->entitiesRemaining());
            officeLogged = true;
            // "office" --type "<x> <z> <yaw deg> <pitch deg>": look round from there.
            float x = 0.0f, z = 0.0f, yaw = 0.0f, pitch = 0.0f;
            std::istringstream view(m_options.demoInput);
            if (demo == "office" && (view >> x >> z)) {
                view >> yaw >> pitch;
                teleportPlayer(glm::vec3(x, 0.0f, z), office::kLevel, glm::radians(yaw));
                m_player->setViewAngles(glm::radians(yaw), glm::radians(pitch));
            }
        }
    }
}
