// ---------------------------------------------------------------------------
// Engine.cpp
// ---------------------------------------------------------------------------
#include "glad.h" // must precede any other OpenGL header

#include "Core/Engine.h"

#include "AI/EntityDirector.h"
#include "AI/NavGrid.h"
#include "Actors/Player.h"
#include "Audio/Soundscape.h"
#include "Core/GpuSelection.h"
#include "Gameplay/TerminalConsole.h"
#include "Math/Random.h"
#include "Physics/Physics.h"
#include "Render/Renderer.h"
#include "World/ChunkManager.h"
#include "World/Stairwell.h"
#include "World/WorldConstants.h"
#include "World/WorldGenerator.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

namespace {

constexpr float kPi = 3.14159265f;

/// Yaw that makes the camera look along direction `d` (see Camera::forward).
inline float yawToward(const glm::vec3& d) { return std::atan2(-d.x, -d.z); }
inline float pitchToward(const glm::vec3& d) { return std::asin(std::clamp(d.y / std::max(glm::length(d), 1e-4f), -1.0f, 1.0f)); }
/// Wraps an angle difference into [-pi, pi].
inline float wrapAngle(float a) { return std::remainder(a, 2.0f * kPi); }
inline float smooth01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

/// DOOM's controls from the real keyboard and mouse (classic and WASD layouts).
doom::Controls doomControls(const Input& in, float sensitivity) {
    doom::Controls c;
    c.forward = in.keyDown(SDL_SCANCODE_W) || in.keyDown(SDL_SCANCODE_UP);
    c.back = in.keyDown(SDL_SCANCODE_S) || in.keyDown(SDL_SCANCODE_DOWN);
    c.strafeLeft = in.keyDown(SDL_SCANCODE_A) || in.keyDown(SDL_SCANCODE_COMMA);
    c.strafeRight = in.keyDown(SDL_SCANCODE_D) || in.keyDown(SDL_SCANCODE_PERIOD);
    c.turnLeft = in.keyDown(SDL_SCANCODE_LEFT);
    c.turnRight = in.keyDown(SDL_SCANCODE_RIGHT);
    c.run = in.keyDown(SDL_SCANCODE_LSHIFT) || in.keyDown(SDL_SCANCODE_RSHIFT);
    c.fire = in.keyDown(SDL_SCANCODE_LCTRL) || in.keyDown(SDL_SCANCODE_RCTRL) || in.mouseDown(SDL_BUTTON_LEFT);
    c.use = in.keyDown(SDL_SCANCODE_SPACE) || in.keyDown(SDL_SCANCODE_E);
    c.turn = in.mouseDelta().x * cfg::kLookRadiansPerPixel * sensitivity;
    if (in.keyPressed(SDL_SCANCODE_1) || in.keyPressed(SDL_SCANCODE_2)) c.weapon = 2;
    if (in.keyPressed(SDL_SCANCODE_3)) c.weapon = 3;
    return c;
}

/// How each DOOM sound is played from the terminal's speaker (indexed by doom::Sfx).
struct DoomSfx {
    SoundId id;
    float   gain;
    bool    loud; ///< Gunfire and explosions: heard through the Backrooms.
};
const DoomSfx kDoomSfx[] = {
    {SoundId::DoomPistol, 0.5f, true},          {SoundId::DoomShotgun, 0.6f, true},
    {SoundId::DoomImpSight, 0.45f, false},      {SoundId::DoomTrooperSight, 0.45f, false},
    {SoundId::DoomMonsterPain, 0.4f, false},    {SoundId::DoomMonsterDeath, 0.45f, false},
    {SoundId::DoomClaw, 0.4f, false},           {SoundId::DoomFireball, 0.4f, false},
    {SoundId::DoomExplode, 0.55f, true},        {SoundId::DoomPlayerPain, 0.3f, false},
    {SoundId::DoomPlayerDeath, 0.5f, false},    {SoundId::DoomItemUp, 0.25f, false},
    {SoundId::DoomWeaponUp, 0.45f, false},      {SoundId::DoomDoor, 0.4f, false},
    {SoundId::DoomSwitch, 0.4f, false},
};
static_assert(sizeof(kDoomSfx) / sizeof(kDoomSfx[0]) == static_cast<size_t>(doom::Sfx::Switch) + 1, "one entry per doom::Sfx");

const char* stalkerStateName(Stalker::State s) {
    switch (s) {
    case Stalker::State::Lurking:  return "LURKING";
    case Stalker::State::Stalking: return "STALKING";
    case Stalker::State::Frozen:   return "FROZEN";
    case Stalker::State::Fleeing:  return "FLEEING";
    case Stalker::State::Lunging:  return "LUNGING";
    }
    return "?";
}

const char* wandererStateName(Wanderer::State s) {
    switch (s) {
    case Wanderer::State::Roaming:       return "ROAMING";
    case Wanderer::State::Investigating: return "INVESTIGATING";
    case Wanderer::State::Hunting:       return "HUNTING";
    case Wanderer::State::Searching:     return "SEARCHING";
    }
    return "?";
}

} // namespace

Engine::Engine(EngineOptions options) : m_options(std::move(options)) {}

Engine::~Engine() { shutdown(); }

bool Engine::createWindow() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL could not initialize! SDL Error: " << SDL_GetError() << '\n';
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    // The scene renders into an MSAA HDR framebuffer; the back buffer needs neither.
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);

    m_window = SDL_CreateWindow(cfg::kWindowTitle, cfg::kWindowWidth, cfg::kWindowHeight,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!m_window) {
        std::cerr << "Window could not be created! SDL Error: " << SDL_GetError() << '\n';
        return false;
    }

    m_context = SDL_GL_CreateContext(m_window);
    if (!m_context) {
        std::cerr << "OpenGL context could not be created! SDL Error: " << SDL_GetError() << '\n';
        return false;
    }
    SDL_GL_MakeCurrent(m_window, m_context);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        std::cerr << "Failed to initialize GLAD\n";
        return false;
    }

    // Prefer adaptive vsync, fall back to regular vsync.
    if (!SDL_GL_SetSwapInterval(-1)) SDL_GL_SetSwapInterval(1);

    SDL_GetWindowSizeInPixels(m_window, &m_pixelWidth, &m_pixelHeight);
    const char* glVendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* glRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    std::cout << "[Engine] OpenGL " << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << " on " << glRenderer
              << " (" << m_pixelWidth << "x" << m_pixelHeight << ")\n";
    gpu::reportActiveGpu(glVendor, glRenderer);
    return true;
}

bool Engine::init() {
    if (!createWindow()) return false;

    m_renderer = std::make_unique<Renderer>();
    if (!m_renderer->init(m_pixelWidth, m_pixelHeight)) {
        std::cerr << "[Engine] Renderer initialisation failed\n";
        return false;
    }

    // Audio is optional: without a playback device the game runs silently.
    m_sound = std::make_unique<Soundscape>();
    m_sound->init();
    if (!m_options.dumpSoundsDir.empty()) m_sound->dumpSounds(m_options.dumpSoundsDir);

    m_world = std::make_unique<WorldGenerator>(m_options.seed);
    m_chunks = std::make_unique<ChunkManager>(*m_world, cfg::kChunkLoadRadius, cfg::kChunkAdjacentRadius,
                                              cfg::kChunkBuildBudget);
    m_physics = std::make_unique<Physics>(cfg::kStepHeight);
    m_entities = std::make_unique<EntityDirector>(*m_world, *m_chunks, m_options.seed);
    m_entities->setEnabled(!m_options.noEntities);

    // Load the neighbourhood of the origin, find a free spot and spawn there.
    const int level = m_options.startLevel;
    const glm::vec3 probe(world::kCellSize * 2.5f, world::levelFloorY(level), world::kCellSize * 2.5f);
    m_player = std::make_unique<Player>(probe, 0.0f);
    teleportPlayer(probe, level, 0.0f);
    const glm::vec3 spawn =
        m_chunks->findSpawnPoint(probe, level, {cfg::kPlayerHalfWidth, cfg::kStandHeight}, *m_physics);
    teleportPlayer(spawn, level, 0.0f);
    if (!m_options.demo.empty()) setupDemo();

    std::cout << "[Engine] World seed 0x" << std::hex << m_options.seed << std::dec << ", " << m_chunks->chunkCount()
              << " chunks loaded, spawn (" << m_player->feetPosition().x << ", " << m_player->feetPosition().z
              << ") on level " << m_focusLevel << "\n"
              << "[Engine] Controls: WASD move, mouse or arrow keys look, Shift run, Space jump, C crouch,\n"
              << "         E open doors / use terminals (Esc leaves a terminal), hold RMB + move mouse to drive,\n"
              << "         +/- sensitivity, F3 entity debug, F11 fullscreen, F12 screenshot, P pause, Esc quit.\n";

    setState(m_state);
    return true;
}

// ---- State ---------------------------------------------------------------------------------------

void Engine::setState(GameState state) {
    m_state = state;
    SDL_SetWindowRelativeMouseMode(m_window, state != GameState::Paused);
    if (m_sound) m_sound->setPaused(state == GameState::Paused);
    if (state == GameState::Terminal) SDL_StartTextInput(m_window);
    else SDL_StopTextInput(m_window);
    m_input.reset();
    m_titleDirty = true; // refresh the title immediately
}

void Engine::teleportPlayer(const glm::vec3& feet, int level, float yaw) {
    m_focusLevel = level;
    const BodyShape standing{cfg::kPlayerHalfWidth, cfg::kStandHeight};
    m_chunks->update(feet, level, 0.0f, Physics::bodyBox(feet, standing), true);
    m_player->teleport(feet, yaw);
}

void Engine::updateFocusLevel() {
    const float y = m_player->feetPosition().y;
    const float base = world::levelFloorY(m_focusLevel);
    if (y > base + cfg::kLevelSwitchBand * world::kLevelHeight) ++m_focusLevel;
    else if (y < base - cfg::kLevelSwitchBand * world::kLevelHeight) --m_focusLevel;
    else return;
    m_titleDirty = true;
    std::cout << "[Engine] Now on level " << m_focusLevel << "\n";
}

void Engine::showMessage(const std::string& text, float seconds) {
    m_message = text;
    m_messageTimer = seconds;
}

// ---- Events --------------------------------------------------------------------------------------

void Engine::processEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        m_input.handleEvent(e);
        switch (e.type) {
        case SDL_EVENT_QUIT:
            m_quit = true;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            m_pixelWidth = std::max(1, static_cast<int>(e.window.data1));
            m_pixelHeight = std::max(1, static_cast<int>(e.window.data2));
            m_renderer->resize(m_pixelWidth, m_pixelHeight);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (m_state != GameState::Paused && m_options.screenshotPath.empty()) {
                m_resumeState = m_state;
                setState(GameState::Paused);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (m_state == GameState::Paused && e.button.button == SDL_BUTTON_LEFT) setState(m_resumeState);
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (m_state == GameState::Terminal && m_console) m_console->type(e.text.text);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (m_state == GameState::Terminal) {
                handleTerminalKey(e.key);
                break;
            }
            if (e.key.repeat) break;
            switch (e.key.scancode) {
            case SDL_SCANCODE_ESCAPE:
                m_quit = true; // exit immediately
                break;
            case SDL_SCANCODE_P:
                if (m_state == GameState::Paused) {
                    setState(m_resumeState);
                } else {
                    m_resumeState = m_state;
                    setState(GameState::Paused);
                }
                break;
            case SDL_SCANCODE_F3:
                m_debugHud = !m_debugHud;
                break;
            case SDL_SCANCODE_F11:
                m_fullscreen = !m_fullscreen;
                SDL_SetWindowFullscreen(m_window, m_fullscreen);
                break;
            case SDL_SCANCODE_F12:
                m_screenshotRequested = true;
                break;
            case SDL_SCANCODE_EQUALS:
            case SDL_SCANCODE_KP_PLUS:
                m_settings.mouseSensitivity = std::min(m_settings.mouseSensitivity * 1.1f, 8.0f);
                m_titleDirty = true;
                break;
            case SDL_SCANCODE_MINUS:
            case SDL_SCANCODE_KP_MINUS:
                m_settings.mouseSensitivity = std::max(m_settings.mouseSensitivity / 1.1f, 0.1f);
                m_titleDirty = true;
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }
}

void Engine::handleTerminalKey(const SDL_KeyboardEvent& key) {
    // While seated, the keyboard belongs to the console (characters arrive as
    // text input events); only editing keys and a few globals are handled here.
    // While DOOM runs it owns the keyboard (its held keys are sampled every
    // frame in updateTerminal); ESC then quits the game, not the terminal.
    if (!m_console) return;
    const bool game = m_console->doomActive();
    if (game && !key.repeat && key.scancode != SDL_SCANCODE_ESCAPE) m_console->keyClick();
    switch (key.scancode) {
    case SDL_SCANCODE_ESCAPE:
        if (!key.repeat && !m_console->escape()) leaveTerminal(false);
        break;
    case SDL_SCANCODE_BACKSPACE:
        if (!game) m_console->backspace();
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        if (!key.repeat && !game) m_console->submit(terminalContext());
        break;
    case SDL_SCANCODE_UP:
        if (!game) m_console->historyUp();
        break;
    case SDL_SCANCODE_DOWN:
        if (!game) m_console->historyDown();
        break;
    case SDL_SCANCODE_F11:
        if (!key.repeat) {
            m_fullscreen = !m_fullscreen;
            SDL_SetWindowFullscreen(m_window, m_fullscreen);
        }
        break;
    case SDL_SCANCODE_F12:
        m_screenshotRequested = true;
        break;
    default:
        break;
    }
}

// ---- Terminals -----------------------------------------------------------------------------------

Terminal* Engine::activeTerminal() const { return m_terminalId ? m_chunks->terminalById(m_terminalId) : nullptr; }

TerminalContext Engine::terminalContext() const {
    TerminalContext ctx;
    ctx.level = m_focusLevel;
    ctx.playerFeet = m_player->feetPosition();
    ctx.stalkerDistance = m_entities->stalkerDistance();
    ctx.stalkerBehind = m_entities->stalkerBehindPlayer();
    ctx.wandererDistance = m_entities->wandererDistance();
    ctx.world = m_world.get();
    return ctx;
}

void Engine::enterTerminal(Terminal& terminal) {
    auto it = m_consoles.find(terminal.id());
    if (it == m_consoles.end()) {
        if (m_consoles.size() >= 8) m_consoles.clear(); // forget old sessions
        it = m_consoles.emplace(terminal.id(), std::make_unique<TerminalConsole>(terminal.id(), terminal.amber(), m_focusLevel)).first;
    }
    m_console = it->second.get();
    m_console->open(terminal.powered());
    terminal.setPowered(true);
    if (terminal.id() != m_lastTerminalId) m_renderer->resetTerminal();
    m_terminalId = m_lastTerminalId = terminal.id();

    // Lean in: the camera settles square in front of the screen.
    m_terminalEye = terminal.viewPoint();
    const glm::vec3 look = terminal.screenCenter() - m_terminalEye;
    m_terminalYaw = yawToward(look);
    m_terminalPitch = pitchToward(look);
    setState(GameState::Terminal);
}

void Engine::leaveTerminal(bool powerOff) {
    if (Terminal* t = activeTerminal()) {
        if (powerOff) t->setPowered(false);
    }
    m_sound->setMonitorHum(false, glm::vec3(0.0f), *m_world);
    m_sound->setTerminalMusic(false, glm::vec3(0.0f), *m_world);
    if (m_state == GameState::Terminal) setState(GameState::Running);
    // m_console stays set while the view blends back, so the screen fades out.
}

void Engine::updateTerminal(float dt) {
    const bool seated = m_state == GameState::Terminal;
    const float step = dt / cfg::kTerminalOpenTime;
    m_terminalBlend = std::clamp(m_terminalBlend + (seated ? step : -step), 0.0f, 1.0f);
    if (!m_console) return;
    if (!seated) {
        if (m_terminalBlend <= 0.0f) {
            m_console = nullptr;
            m_terminalId = 0;
        }
        return;
    }

    Terminal* terminal = activeTerminal();
    if (!terminal) { // its chunk went away (should not happen while seated)
        leaveTerminal(false);
        return;
    }
    TerminalContext ctx = terminalContext();
    if (m_console->doomActive()) ctx.doom = m_options.demo == "doom" ? demoDoomControls() : doomControls(m_input, m_settings.mouseSensitivity);
    m_console->update(dt, ctx);
    const glm::vec3 at = terminal->screenCenter();
    for (TerminalSound s : m_console->takeSounds()) {
        switch (s) {
        case TerminalSound::Key:
            m_sound->playEffect(SoundId::TerminalKey, at + glm::vec3(0.0f, -0.28f, 0.0f), 0.35f, *m_world);
            m_noises.push_back({at, cfg::kNoiseTyping, NoiseKind::Typing}); // the Wanderer hears you typing
            break;
        case TerminalSound::GhostKey:
            m_sound->playEffect(SoundId::TerminalKey, at + glm::vec3(0.0f, -0.28f, 0.0f), 0.2f, *m_world);
            break;
        case TerminalSound::Beep:
            m_sound->playEffect(SoundId::TerminalBeep, at, 0.22f, *m_world);
            m_noises.push_back({at, cfg::kNoiseMachine, NoiseKind::Machine});
            break;
        case TerminalSound::Glitch:
            m_sound->playEffect(SoundId::TerminalGlitch, at, 0.3f, *m_world);
            break;
        case TerminalSound::Boot:
            m_sound->playEffect(SoundId::TerminalBoot, at, 0.45f, *m_world);
            m_noises.push_back({at, cfg::kNoiseMachine, NoiseKind::Machine});
            break;
        case TerminalSound::PowerDown:
            m_sound->playEffect(SoundId::TerminalOff, at, 0.4f, *m_world);
            break;
        }
    }
    // DOOM plays through the terminal's speaker - and the shooting carries.
    doom::Game* game = m_console->doom();
    if (game) {
        for (const doom::SoundEvent& e : game->takeSounds()) {
            const DoomSfx& sfx = kDoomSfx[static_cast<size_t>(e.id)];
            m_sound->playEffect(sfx.id, at, sfx.gain * e.gain, *m_world);
            if (sfx.loud) m_noises.push_back({at, cfg::kNoiseDoomGunfire * std::max(0.5f, e.gain), NoiseKind::Machine});
        }
        if ((m_doomNoiseTimer -= dt) <= 0.0f) {
            m_noises.push_back({at, cfg::kNoiseDoomMusic, NoiseKind::Machine});
            m_doomNoiseTimer = 1.5f;
        }
    }
    m_sound->setTerminalMusic(game != nullptr, at, *m_world);
    m_sound->setMonitorHum(true, at, *m_world);
    if (m_console->exitRequested()) {
        const bool off = m_console->powerOffRequested();
        m_console->clearRequests();
        leaveTerminal(off);
    }
}

// ---- Noise and entities ------------------------------------------------------------------------

void Engine::collectNoise() {
    for (const PlayerEvent& e : m_player->events()) {
        const glm::vec3 at = m_player->feetPosition();
        switch (e.type) {
        case PlayerEvent::Type::Footstep:
            // Running is loud; walking carries; crouch-walking barely registers.
            m_noises.push_back({at, cfg::kNoiseFootstepRun * std::max(0.08f, e.intensity), NoiseKind::Footstep});
            break;
        case PlayerEvent::Type::Jump:
            m_noises.push_back({at, 8.0f, NoiseKind::Landing});
            break;
        case PlayerEvent::Type::Land:
            m_noises.push_back({at, cfg::kNoiseLanding * (0.4f + 0.6f * e.intensity), NoiseKind::Landing});
            break;
        }
    }
    for (const DoorEvent& d : m_chunks->doorEvents()) {
        const bool loud = (d.flags & (Door::kEventUnlatch | Door::kEventShut)) != 0;
        m_noises.push_back({d.position, cfg::kNoiseDoor * (loud ? 1.0f : 0.6f), NoiseKind::Door});
    }
}

void Engine::startCaught(EntityKind by) {
    if (m_state == GameState::Terminal) leaveTerminal(false);
    // It looms up in front of the player; the camera is wrenched to its face.
    m_caughtFace = m_entities->confront(by, m_player->feetPosition(), m_player->lookDirection());
    m_caughtTimer = 0.0f;
    m_respawned = false;
    m_sound->playSting();
    setState(GameState::Caught);
    std::cout << "[Engine] Caught by the " << (by == EntityKind::Stalker ? "Stalker" : "Wanderer") << "\n";
}

void Engine::updateCaught(float dt) {
    m_caughtTimer += dt;
    if (!m_respawned) {
        m_fade = smooth01((m_caughtTimer - 0.7f) / 0.9f);
        if (m_caughtTimer > 3.0f) {
            // You wake up somewhere else. Sometimes a whole storey away.
            rnd::Rng rng(rnd::hashCombine(m_options.seed, static_cast<uint64_t>(m_simTime * 1000.0)));
            int level = m_focusLevel;
            if (rng.chance(0.4f)) level += rng.chance(0.5f) ? 1 : -1;
            const float angle = rng.range(0.0f, 2.0f * kPi);
            const float dist = rng.range(120.0f, 220.0f);
            const glm::vec3 near = glm::vec3(m_player->feetPosition().x + std::cos(angle) * dist, world::levelFloorY(level),
                                             m_player->feetPosition().z + std::sin(angle) * dist);
            teleportPlayer(near, level, rng.range(0.0f, 2.0f * kPi));
            teleportPlayer(m_chunks->findSpawnPoint(near, level, m_player->shape(), *m_physics), level, m_player->yaw());
            m_entities->scatter();
            if (m_options.demo == "explore") { // keep exploring from wherever the player woke up
                m_autopilot = exploreRoute(m_player->feetPosition(), level, 600, rng.next());
                m_autopilotIndex = 0;
            }
            char msg[48];
            std::snprintf(msg, sizeof(msg), "YOU WAKE UP ON LEVEL %d", level);
            showMessage(msg, 5.0f);
            m_respawned = true;
        }
        return;
    }
    m_fade = 1.0f - smooth01((m_caughtTimer - 3.2f) / 1.4f);
    if (m_caughtTimer > 4.6f) {
        m_fade = 0.0f;
        setState(GameState::Running);
    }
}

// ---- Simulation --------------------------------------------------------------------------------------

void Engine::update(float dt) {
    if (m_state == GameState::Paused) return;
    m_simTime += dt;
    m_lastDt = dt;
    m_noises.clear();
    m_messageTimer = std::max(0.0f, m_messageTimer - dt);
    updateDemo(dt);

    m_prompt.clear();
    bool canUse = false;
    if (m_state == GameState::Running) {
        driveAutopilot(m_input);
        const Interactable target = m_chunks->findInteractable(m_player->eyePosition(), m_player->lookDirection());
        canUse = static_cast<bool>(target);
        if (target.kind == Interactable::Kind::Door) {
            const bool shut = target.door->state() == Door::State::Closed || target.door->state() == Door::State::Closing;
            m_prompt = shut ? "E  OPEN DOOR" : "E  CLOSE DOOR";
            if (m_input.keyPressed(SDL_SCANCODE_E)) target.door->toggle(m_player->feetPosition());
        } else if (target.kind == Interactable::Kind::Terminal) {
            m_prompt = target.terminal->powered() ? "E  USE TERMINAL" : "E  SWITCH ON TERMINAL";
            if (m_input.keyPressed(SDL_SCANCODE_E)) enterTerminal(*target.terminal);
        }
    }

    // Seated or caught, the body just stands there.
    const Input& bodyInput = m_state == GameState::Running ? m_input : m_idleInput;
    m_player->update(dt, bodyInput, m_settings, *m_chunks, *m_physics);
    updateFocusLevel();
    m_chunks->update(m_player->feetPosition(), m_focusLevel, dt, m_player->bodyBox());
    // Fell out of the world (e.g. down a shaft whose floor never loaded): start over nearby.
    if (m_player->feetPosition().y < world::levelFloorY(m_focusLevel) - world::kLevelHeight - 2.0f) {
        std::cerr << "[Engine] Fell out of the world, respawning\n";
        const glm::vec3 near(m_player->feetPosition().x, world::levelFloorY(m_focusLevel), m_player->feetPosition().z);
        teleportPlayer(m_chunks->findSpawnPoint(near, m_focusLevel, m_player->shape(), *m_physics), m_focusLevel,
                       m_player->yaw());
    }

    updateTerminal(dt);
    collectNoise();

    // ---- Anomalies: they perceive what the player actually sees.
    const bool blind = m_state == GameState::Terminal || (m_state == GameState::Caught && m_fade > 0.5f);
    const float aspect = static_cast<float>(m_pixelWidth) / static_cast<float>(std::max(1, m_pixelHeight));
    m_entities->update(dt, viewCamera(), aspect, m_player->feetPosition(), m_focusLevel, blind, *m_chunks, *m_physics,
                       m_noises);
    if (const auto by = m_entities->takeCatch(); by && m_state != GameState::Caught) startCaught(*by);
    if (m_state == GameState::Caught) updateCaught(dt);

    // After every update so this frame's footstep / door / entity events are
    // heard, and with the renderer's clock so the tube buzz matches the flicker.
    m_sound->update(dt, m_simTime, *m_player, *m_chunks, *m_world);
    m_sound->updateEntities(dt, m_entities->audioState(), m_entities->sounds(), m_entities->fear(), *m_world);

    // Crosshair ring fades in when a door or terminal is within reach.
    m_crosshairHighlight += ((canUse ? 1.0f : 0.0f) - m_crosshairHighlight) * (1.0f - std::exp(-12.0f * dt));
}

// ---- Rendering ---------------------------------------------------------------------------------------

Camera Engine::viewCamera() const {
    Camera cam = m_player->camera();
    if (m_terminalBlend > 0.0f) {
        const float t = smooth01(m_terminalBlend);
        cam.position = glm::mix(cam.position, m_terminalEye, t);
        cam.yaw += wrapAngle(m_terminalYaw - cam.yaw) * t;
        cam.pitch += (m_terminalPitch - cam.pitch) * t;
        cam.fovYDegrees += (55.0f - cam.fovYDegrees) * t;
    }
    if (m_state == GameState::Caught && !m_respawned) {
        // The jumpscare: the head is wrenched round to face it, and shakes.
        const glm::vec3 d = m_caughtFace - cam.position;
        const float t = smooth01(m_caughtTimer / 0.12f);
        cam.yaw += wrapAngle(yawToward(d) - cam.yaw) * t;
        cam.pitch += (pitchToward(d) - cam.pitch) * t;
        const float shake = 0.03f * std::max(0.0f, 1.0f - m_caughtTimer);
        cam.yaw += shake * std::sin(m_caughtTimer * 91.0f);
        cam.pitch += shake * std::sin(m_caughtTimer * 73.0f + 1.3f);
        cam.fovYDegrees -= 12.0f * t;
    }
    return cam;
}

void Engine::drawHud() {
    TextOverlay& hud = m_renderer->hud();
    const float s = hud.pixelScale();
    const float w = static_cast<float>(m_pixelWidth), h = static_cast<float>(m_pixelHeight);
    const glm::vec4 ink(1.0f, 1.0f, 0.92f, 0.9f);

    hud.text(m_fpsText, w - 10.0f * s, 10.0f * s, TextOverlay::Align::Right, 1.0f, ink, true);
    char level[32];
    std::snprintf(level, sizeof(level), "LEVEL %d", m_focusLevel);
    hud.text(level, 10.0f * s, 10.0f * s, TextOverlay::Align::Left, 1.0f, ink * glm::vec4(1, 1, 1, 1.0f - m_fade), true);

    if (!m_prompt.empty() && m_state == GameState::Running) {
        hud.text(m_prompt, w * 0.5f, h * 0.5f + 20.0f * s, TextOverlay::Align::Center, 1.0f,
                 glm::vec4(1.0f, 1.0f, 0.92f, 0.75f * m_crosshairHighlight), true);
    }
    if (m_terminalBlend > 0.0f && m_console) {
        // The terminal's key guide, at twice the size of the other HUD text.
        const char* hint = m_console->doomActive() ? "<ESC> Quit  <W><A><S><D> Move  MOUSE Turn  <CTRL>/CLICK Fire  <SPACE> Use  <2> <3> Weapons"
                           : m_console->commandMode() ? "<ESC> Leave     <ENTER> Run Command     Type HELP for Commands"
                                                      : "<ESC> Leave     <ENTER> Command Prompt";
        hud.text(hint, w * 0.5f, h - 40.0f * s, TextOverlay::Align::Center, 2.0f,
                 glm::vec4(0.8f, 0.8f, 0.75f, 0.6f * m_terminalBlend), true);
    }
    if (m_messageTimer > 0.0f && !m_message.empty()) {
        const float a = std::min(1.0f, m_messageTimer / 1.0f) * std::min(1.0f, (5.0f - m_messageTimer) / 0.8f + 0.2f);
        hud.text(m_message, w * 0.5f, h * 0.62f, TextOverlay::Align::Center, 2.0f, glm::vec4(0.95f, 0.93f, 0.85f, a));
    }
    if (m_debugHud) {
        const Stalker& st = m_entities->stalker();
        const Wanderer& wa = m_entities->wanderer();
        char line[160];
        std::snprintf(line, sizeof(line), "STALKER %s %s %.1fM   WANDERER %s %.1fM %.2f   FEAR %.2f",
                      st.active() ? stalkerStateName(st.state()) : "ABSENT", st.seen() ? "SEEN" : "UNSEEN",
                      m_entities->stalkerDistance(), wa.active() ? wandererStateName(wa.state()) : "ABSENT",
                      m_entities->wandererDistance(), wa.agitation(), m_entities->fear());
        hud.text(line, 10.0f * s, 24.0f * s, TextOverlay::Align::Left, 1.0f, glm::vec4(0.7f, 1.0f, 0.7f, 0.9f), true);
    }
}

void Engine::render(float dt) {
    const Camera cam = viewCamera();
    m_entityDraw.clear();
    m_entities->buildDrawList(m_entityDraw, cam);

    FrameParams frame;
    frame.camera = cam;
    frame.time = m_simTime;
    frame.crosshairHighlight = m_state == GameState::Running ? m_crosshairHighlight : 0.0f;
    frame.fear = m_entities->fear();
    frame.fade = m_fade;
    frame.lightDisturbances = &m_entities->lightDisturbances();
    frame.entities = &m_entityDraw;
    m_renderer->render(frame, *m_chunks, *m_world);

    if (m_console && m_terminalBlend > 0.0f) {
        m_renderer->drawTerminal(m_console->screen(), m_console->graphics(), static_cast<float>(m_simTime), dt, m_terminalBlend);
    }
    drawHud();
    m_renderer->flushHud();
}

void Engine::updateTitle(float dt) {
    m_titleTimer += dt;
    ++m_frameCounter;
    if (m_titleTimer >= 0.5f) {
        // Average over the whole interval: steadier and easier to read than per-frame values.
        m_fps = static_cast<float>(m_frameCounter) / m_titleTimer;
        char meter[32];
        std::snprintf(meter, sizeof(meter), "%.0f FPS  %.1f ms", m_fps, 1000.0f / std::max(m_fps, 1e-3f));
        m_fpsText = meter;
        m_frameCounter = 0;
        m_titleTimer = 0.0f;
        m_titleDirty = true;
    }
    if (!m_titleDirty) return;
    m_titleDirty = false;

    const glm::vec3 p = m_player->feetPosition();
    const ChunkCoord c = ChunkCoord::fromWorld(p.x, p.z, m_focusLevel);
    const RenderStats& s = m_renderer->stats();
    char title[256];
    std::snprintf(title, sizeof(title),
                  "%s | %.0f FPS | level %d chunk (%d, %d) | %zu chunks, %zu lights | sensitivity %.2f%s%s",
                  cfg::kWindowTitle, m_fps, c.level, c.x, c.z, m_chunks->chunkCount(), s.lights,
                  m_settings.mouseSensitivity, m_player->mouseDriveActive() ? " | MOUSE DRIVE" : "",
                  m_state == GameState::Paused ? " | PAUSED - press P or click to resume, Esc to quit" : "");
    SDL_SetWindowTitle(m_window, title);
}

bool Engine::saveScreenshot(const std::string& path) const {
    const int w = m_pixelWidth, h = m_pixelHeight;
    const size_t pitch = static_cast<size_t>(w) * 3;
    std::vector<uint8_t> pixels(pitch * static_cast<size_t>(h));
    std::vector<uint8_t> flipped(pixels.size());

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    // OpenGL's origin is bottom-left; images are stored top-down.
    for (int y = 0; y < h; ++y) {
        std::copy_n(pixels.data() + static_cast<size_t>(h - 1 - y) * pitch, pitch,
                    flipped.data() + static_cast<size_t>(y) * pitch);
    }

    SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGB24, flipped.data(), static_cast<int>(pitch));
    if (!surface) return false;
    const bool ok = SDL_SaveBMP(surface, path.c_str());
    SDL_DestroySurface(surface);
    std::cout << (ok ? "[Engine] Saved screenshot " : "[Engine] Failed to save screenshot ") << path << '\n';
    return ok;
}

int Engine::run() {
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    uint64_t last = SDL_GetPerformanceCounter();
    double runTime = 0.0;

    while (!m_quit) {
        const uint64_t now = SDL_GetPerformanceCounter();
        // Clamp dt so hitches (window drags, breakpoints) never explode the physics.
        const float dt = static_cast<float>(std::min(static_cast<double>(now - last) / frequency, 0.1));
        last = now;
        runTime += dt;

        m_input.beginFrame();
        processEvents();
        update(dt);
        render(dt);

        if (m_screenshotRequested) {
            char name[64];
            std::snprintf(name, sizeof(name), "backrooms_%03d.bmp", m_screenshotIndex++);
            saveScreenshot(name);
            m_screenshotRequested = false;
        }
        if (!m_options.screenshotPath.empty() && runTime >= m_options.screenshotDelay) {
            saveScreenshot(m_options.screenshotPath);
            m_quit = true;
        }

        SDL_GL_SwapWindow(m_window);
        updateTitle(dt);
    }
    return 0;
}

void Engine::shutdown() {
    // GPU resources must be released while the context is still alive.
    m_sound.reset(); // stops the audio thread before anything it reads goes away
    m_renderer.reset();
    m_entities.reset();
    m_consoles.clear();
    m_player.reset();
    m_chunks.reset();
    m_physics.reset();
    m_world.reset();

    if (m_context) {
        SDL_GL_DestroyContext(m_context);
        m_context = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
}

// ---- Developer scenes ------------------------------------------------------------------------------

void Engine::setupDemo() {
    const std::string& demo = m_options.demo;
    const glm::vec3 feet = m_player->feetPosition();
    const glm::vec3 fwd = glm::normalize(glm::vec3(m_player->lookDirection().x, 0.0f, m_player->lookDirection().z));

    if (demo == "stairs" || demo == "stairs-top" || demo == "stairs-sign" || demo == "climb" || demo == "descend") {
        // Nearest stairwell rising from the start storey (spiral over chunk columns).
        const int level = m_focusLevel;
        const ChunkCoord home = ChunkCoord::fromWorld(feet.x, feet.z, level);
        for (int ring = 0; ring < 12; ++ring) {
            for (int dz = -ring; dz <= ring; ++dz) {
                for (int dx = -ring; dx <= ring; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dz)) != ring) continue;
                    const int cx = home.x + dx, cz = home.z + dz;
                    const auto s = m_world->stairwell(level, cx, cz);
                    if (!s) continue;
                    const int gx = cx * world::kChunkCells + s->lx, gz = cz * world::kChunkCells + s->lz;
                    const std::vector<glm::vec3> route = stairs::climbRoute(gx, gz, level, s->rotation);
                    const glm::mat4 toWorld = stairs::cellTransform(gx, gz, level, s->rotation);
                    std::cout << "[Demo] Stairwell at cell (" << gx << ", " << gz << ") level " << level
                              << ", rotation " << s->rotation << "\n";
                    if (demo == "stairs-sign") {
                        // A few metres back from the (closed) entrance, as a player would come across it.
                        const int signLevel = level + (m_options.demoInput == "upper" ? 1 : 0);
                        const glm::mat4 at = stairs::cellTransform(gx, gz, signLevel, s->rotation);
                        const glm::vec3 feetPos(at * glm::vec4(world::kCellSize * 0.5f, 0.0f, -4.0f, 1.0f));
                        const glm::vec3 door(at * glm::vec4(world::kCellSize * 0.5f, 0.0f, 0.0f, 1.0f));
                        teleportPlayer(feetPos, signLevel, yawToward(door - feetPos));
                        m_player->setViewAngles(yawToward(door - feetPos), glm::radians(8.0f));
                        return;
                    }
                    if (demo == "stairs-top") {
                        // Upper lobby, at the head of flight B, looking down the shaft.
                        const glm::vec3 top(toWorld * glm::vec4(3.4f, world::kLevelHeight, 0.9f, 1.0f));
                        const glm::vec3 ahead(toWorld * glm::vec4(2.4f, world::kLevelHeight, 3.5f, 1.0f));
                        teleportPlayer(top, level + 1, yawToward(ahead - top));
                        m_player->setViewAngles(yawToward(ahead - top), glm::radians(-42.0f));
                    } else if (demo == "descend") {
                        const size_t n = route.size();
                        teleportPlayer(route[n - 1], level + 1, yawToward(route[n - 2] - route[n - 1]));
                    } else {
                        teleportPlayer(route[0], level, yawToward(route[1] - route[0]));
                    }
                    // Open the entrances on both storeys, from inside the stairwell
                    // so they swing out of the way of the route.
                    const int side = stairs::entranceSide(s->rotation);
                    const int ex = gx + (side == 1 ? 1 : 0), ez = gz + (side == 3 ? 1 : 0);
                    const world::EdgeAxis axis = side < 2 ? world::EdgeAxis::West : world::EdgeAxis::South;
                    const glm::vec3 inside((static_cast<float>(gx) + 0.5f) * world::kCellSize, 0.0f,
                                           (static_cast<float>(gz) + 0.5f) * world::kCellSize);
                    for (int l : {level, level + 1}) {
                        if (Door* d = m_chunks->doorOnEdge(l, ex, ez, axis)) {
                            d->toggle(inside);                       // picks the outward swing...
                            d->restoreState(d->persistentState());   // ...and snaps fully open
                            d->takeEvents();
                        }
                    }
                    if (demo == "climb") {
                        for (auto it = route.begin() + 1; it != route.end(); ++it) m_autopilot.push_back({*it});
                        m_autopilotIndex = 0;
                    } else if (demo == "descend") {
                        for (auto it = route.rbegin() + 1; it != route.rend(); ++it) m_autopilot.push_back({*it});
                        m_autopilotIndex = 0;
                    }
                    return;
                }
            }
        }
        std::cerr << "[Demo] No stairwell found near the spawn\n";
        return;
    }

    // Entity scenes: the first free spot along a direction from the player.
    auto freeSpotAlong = [&](const glm::vec3& dir, float from, float to) {
        for (float d = from; d >= to; d -= 0.5f) {
            const glm::vec3 p = feet + dir * d;
            if (m_physics->isFree(Physics::bodyBox(p + glm::vec3(0.0f, 0.01f, 0.0f), {0.3f, 2.2f}), *m_chunks)) return p;
        }
        return feet + dir * to;
    };
    if (demo == "stalker") {
        const glm::vec3 p = freeSpotAlong(fwd, 5.5f, 3.0f);
        m_entities->spawnAt(EntityKind::Stalker, p, m_focusLevel, std::atan2(-fwd.x, -fwd.z));
    } else if (demo == "ambush" || demo == "caught") {
        // It starts behind the player; in "ambush" the view whips round after
        // a moment, in "caught" the player never looks.
        const glm::vec3 p = freeSpotAlong(-fwd, 14.0f, 6.0f);
        m_entities->spawnAt(EntityKind::Stalker, p, m_focusLevel, std::atan2(fwd.x, fwd.z), true);
    } else if (demo == "wanderer") {
        const glm::vec3 p = freeSpotAlong(fwd, 4.5f, 2.5f);
        m_entities->spawnAt(EntityKind::Wanderer, p, m_focusLevel, std::atan2(-fwd.x, -fwd.z));
    } else if (demo == "terminal" || demo == "doom") {
        // Nearest terminal on this storey: sit down at it ("doom": and play).
        if (demo == "doom") m_entities->setEnabled(EntityKind::Stalker, false);
        Terminal* best = nullptr;
        float bestDist = 1e9f;
        for (Chunk* chunk : m_chunks->sortedChunksMutable()) {
            if (chunk->coord().level != m_focusLevel) continue;
            for (Terminal& t : chunk->terminals()) {
                const float d = glm::length(t.center() - feet);
                if (d < bestDist) {
                    bestDist = d;
                    best = &t;
                }
            }
        }
        if (!best) {
            std::cerr << "[Demo] No terminal loaded\n";
            return;
        }
        glm::vec3 seat = best->viewPoint() + best->screenNormal() * 0.3f;
        seat.y = world::levelFloorY(m_focusLevel);
        const glm::vec3 look = best->screenCenter() - seat;
        teleportPlayer(seat, m_focusLevel, yawToward(look));
        // Re-find it: teleporting may have reloaded its chunk.
        if (Terminal* t = m_chunks->terminalById(best->id())) enterTerminal(*t);
        m_terminalBlend = 1.0f;
    } else if (demo == "explore") {
        // A long walk through the rooms (Stalker off: a catch would teleport
        // the player); for measuring how often the Wanderer is met.
        m_entities->setEnabled(EntityKind::Stalker, false);
        const uint64_t routeSeed = m_options.demoInput.empty() ? 1 : std::strtoull(m_options.demoInput.c_str(), nullptr, 10);
        m_autopilot = exploreRoute(feet, m_focusLevel, 600, routeSeed);
        m_autopilotIndex = 0;
    } else if (demo != "idle") { // "idle": nothing staged, entity activity is just logged
        std::cerr << "[Demo] Unknown demo '" << demo << "'\n";
    }
}

doom::Controls Engine::demoDoomControls() const {
    // A scripted player: start the game, look round the first room, walk in and shoot.
    doom::Controls c;
    const float t = m_demoTime;
    c.fire = (t > 9.0f && t < 9.2f) || (t > 14.0f && std::fmod(t, 0.9f) < 0.15f);
    c.turnRight = t > 11.0f && t < 12.2f;
    c.forward = t > 12.2f && t < 14.5f;
    c.use = t > 14.5f && t < 14.6f;
    c.turnLeft = t > 15.5f && std::fmod(t, 3.0f) < 0.6f;
    return c;
}

void Engine::updateDemo(float dt) {
    if (m_options.demo.empty()) return;
    m_demoTime += dt;
    if (m_options.demo == "ambush" && m_demoStep == 0 && m_entities->stalker().active() &&
        m_entities->stalkerDistance() < 7.0f) {
        // Whip round towards it (whatever the layout between).
        const glm::vec3 to = m_entities->stalker().feet() - m_player->feetPosition();
        m_player->setViewAngles(yawToward(to), 0.0f);
        m_demoStep = 1;
        std::cout << "[Demo] Turning round\n";
    }
    if (m_options.demo == "wanderer" && m_demoStep == 0 && m_demoTime > 1.0f) {
        // Make a noise: open the nearest door. The blind thing should come for it.
        Door* nearest = nullptr;
        float best = 8.0f;
        for (Chunk* chunk : m_chunks->sortedChunksMutable()) {
            if (chunk->coord().level != m_focusLevel) continue;
            for (Door& d : chunk->doors()) {
                const float dist = glm::length(d.doorwayCenter() - m_player->eyePosition());
                if (dist < best) {
                    best = dist;
                    nearest = &d;
                }
            }
        }
        if (nearest) {
            nearest->toggle(m_player->feetPosition());
            std::printf("[Demo] Opening a door %.1fm away\n", best);
        }
        m_demoStep = 1;
    }
    if (m_options.demo == "terminal" && m_demoStep == 0 && m_demoTime > 1.5f && m_console && !m_options.demoInput.empty()) {
        m_console->type(m_options.demoInput.c_str());
        m_console->submit(terminalContext());
        m_demoStep = 1;
    }
    if (m_options.demo == "doom" && m_demoStep == 0 && m_demoTime > 1.5f && m_console) {
        m_console->type("doom");
        m_console->submit(terminalContext());
        m_demoStep = 1;
    }
    // Log entity state changes (scripted verification of the behaviours).
    const Stalker& st = m_entities->stalker();
    const Wanderer& wa = m_entities->wanderer();
    char states[128];
    std::snprintf(states, sizeof(states), "stalker %s%s / wanderer %s", st.active() ? stalkerStateName(st.state()) : "absent",
                  st.active() && st.seen() ? " (seen)" : "", wa.active() ? wandererStateName(wa.state()) : "absent");
    const bool tick = std::floor(m_demoTime * 2.0f) != std::floor((m_demoTime - dt) * 2.0f); // twice a second
    if (states != m_lastEntityStates || (tick && (st.active() || wa.active()))) {
        m_lastEntityStates = states;
        std::printf("[Demo] t=%.2f %s, stalker %.1fm (stuck %.1fs), wanderer %.1fm (stuck %.1fs, agitation %.2f), "
                    "player (%.1f, %.1f)\n",
                    m_demoTime, states, m_entities->stalkerDistance(), st.stuckTime(), m_entities->wandererDistance(),
                    wa.stuckTime(), wa.agitation(), m_player->feetPosition().x, m_player->feetPosition().z);
    }
}

std::vector<Engine::AutopilotPoint> Engine::exploreRoute(const glm::vec3& from, int level, int steps,
                                                         uint64_t seed) const {
    // A random walk over the cell graph that prefers rooms it has not been
    // through yet, passing each opening square-on (as the entities do).
    rnd::Rng rng(seed);
    glm::ivec2 cell = NavGrid::cellOf(from), prev(INT_MAX, INT_MAX);
    std::unordered_map<uint64_t, int> visits;
    auto key = [](const glm::ivec2& c) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32) | static_cast<uint32_t>(c.y);
    };
    const glm::ivec2 dirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    std::vector<AutopilotPoint> route;
    for (int i = 0; i < steps; ++i) {
        std::vector<glm::ivec2> options;
        int fewest = INT_MAX;
        for (const glm::ivec2& dir : dirs) {
            const glm::ivec2 nb = cell + dir;
            int gx, gz;
            world::EdgeAxis axis;
            NavGrid::edgeBetween(cell, nb, gx, gz, axis);
            if (m_world->edge(level, gx, gz, axis) == world::EdgeType::Wall) continue;
            if (m_world->cellRole(level, nb.x, nb.y) != world::CellRole::Room) continue;
            const int v = visits[key(nb)] + (nb == prev ? 2 : 0); // rather not turn straight back
            if (v < fewest) options.clear();
            if (v <= fewest) {
                fewest = v;
                options.push_back(nb);
            }
        }
        if (options.empty()) break;
        const glm::ivec2 next = options[rng.next() % options.size()];
        int gx, gz;
        world::EdgeAxis axis;
        NavGrid::edgeBetween(cell, next, gx, gz, axis);
        const float S = world::kCellSize, y = world::levelFloorY(level);
        const glm::vec3 mid = axis == world::EdgeAxis::West
                                  ? glm::vec3(static_cast<float>(gx) * S, y, (static_cast<float>(gz) + 0.5f) * S)
                                  : glm::vec3((static_cast<float>(gx) + 0.5f) * S, y, static_cast<float>(gz) * S);
        const glm::vec3 through(static_cast<float>(next.x - cell.x), 0.0f, static_cast<float>(next.y - cell.y));
        const bool door = m_world->edge(level, gx, gz, axis) == world::EdgeType::Door;
        route.push_back({mid - through * 0.9f, door, level, gx, gz, axis});
        route.push_back({mid + through * 0.9f, false, level, gx, gz, axis});
        prev = cell;
        cell = next;
        ++visits[key(cell)];
    }
    return route;
}

void Engine::driveAutopilot(Input& input) {
    if (m_autopilotIndex >= m_autopilot.size()) return;
    const bool verbose = m_options.demo != "explore"; // long walks only report problems
    const glm::vec3 feet = m_player->feetPosition();
    const AutopilotPoint& point = m_autopilot[m_autopilotIndex];
    glm::vec3 d = point.pos - feet;
    d.y = 0.0f;
    if (glm::length(d) < 0.3f) {
        if (verbose) {
            std::printf("[Autopilot] Waypoint %zu reached at (%.2f, %.2f, %.2f), level %d\n", m_autopilotIndex, feet.x,
                        feet.y, feet.z, m_focusLevel);
        }
        ++m_autopilotIndex;
        m_autopilotStuck = 0.0f;
        m_autopilotWait = 0.0f;
        m_autopilotTries = 0;
        if (m_autopilotIndex >= m_autopilot.size()) {
            std::printf("[Autopilot] Route complete: feet height %.2f, level %d\n", feet.y, m_focusLevel);
            input.setKeyDown(SDL_SCANCODE_W, false);
        }
        return;
    }

    // Doors: open the one ahead like a player would, then wait for it to swing clear.
    if (point.door) {
        Door* door = m_chunks->doorOnEdge(point.level, point.gx, point.gz, point.axis);
        if (door && door->state() == Door::State::Closed && glm::length(d) < 2.5f) door->toggle(feet);
    } else if (m_autopilotIndex > 0 && m_autopilot[m_autopilotIndex - 1].door) {
        const AutopilotPoint& at = m_autopilot[m_autopilotIndex - 1];
        Door* door = m_chunks->doorOnEdge(at.level, at.gx, at.gz, at.axis);
        if (door && door->state() == Door::State::Closed) door->toggle(feet); // its approach was skipped
        if (door && door->state() != Door::State::Open && m_autopilotWait < 2.0f) {
            m_autopilotWait += m_lastDt;
            input.setKeyDown(SDL_SCANCODE_W, false);
            return;
        }
    }

    // Snagged on furniture: sidestep, alternating sides, as the entities do.
    // If that keeps failing, jump to the waypoint (reported, so a soak test
    // can tell how often the scripted walk needed help).
    m_autopilotStuck = m_player->horizontalSpeed() < 0.3f ? m_autopilotStuck + m_lastDt : 0.0f;
    if (m_autopilotStuck > 0.5f) {
        m_autopilotStuck = 0.0f;
        m_autopilotDetour = 0.6f;
        m_autopilotSide = -m_autopilotSide;
        if (++m_autopilotTries > 4) {
            std::printf("[Autopilot] Stuck near (%.2f, %.2f), jumping to waypoint %zu\n", feet.x, feet.z, m_autopilotIndex);
            m_player->teleport(point.pos, m_player->yaw());
            m_autopilotTries = 0;
            return;
        }
    }
    if (m_autopilotDetour > 0.0f) {
        m_autopilotDetour -= m_lastDt;
        const float a = 1.2f * m_autopilotSide;
        d = glm::vec3(d.x * std::cos(a) - d.z * std::sin(a), 0.0f, d.x * std::sin(a) + d.z * std::cos(a));
    }
    m_player->setViewAngles(yawToward(d), 0.0f);
    input.setKeyDown(SDL_SCANCODE_W, true);
}
