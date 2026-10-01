// ---------------------------------------------------------------------------
// Engine.cpp
// ---------------------------------------------------------------------------
#include "glad.h" // must precede any other OpenGL header

#include "Core/Engine.h"

#include "AI/EntityDirector.h"
#include "AI/NavGrid.h"
#include "Actors/FileCabinet.h"
#include "Actors/Phone.h"
#include "Actors/Player.h"
#include "Audio/Soundscape.h"
#include "Core/GpuSelection.h"
#include "Gameplay/PhoneCall.h"
#include "Gameplay/TerminalConsole.h"
#include "Gameplay/TeslaGun.h"
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

/// DOOM's controls from the real keyboard and mouse (WASD or arrows; E or click fires, Space uses,
/// right button + mouse strafes).
doom::Controls doomControls(const Input& in, float sensitivity) {
    doom::Controls c;
    c.forward = in.keyDown(SDL_SCANCODE_W) || in.keyDown(SDL_SCANCODE_UP);
    c.back = in.keyDown(SDL_SCANCODE_S) || in.keyDown(SDL_SCANCODE_DOWN);
    c.strafeLeft = in.keyDown(SDL_SCANCODE_A) || in.keyDown(SDL_SCANCODE_COMMA);
    c.strafeRight = in.keyDown(SDL_SCANCODE_D) || in.keyDown(SDL_SCANCODE_PERIOD);
    c.turnLeft = in.keyDown(SDL_SCANCODE_LEFT);
    c.turnRight = in.keyDown(SDL_SCANCODE_RIGHT);
    c.run = in.keyDown(SDL_SCANCODE_LSHIFT) || in.keyDown(SDL_SCANCODE_RSHIFT);
    c.fire = in.keyDown(SDL_SCANCODE_E) || in.mouseDown(SDL_BUTTON_LEFT);
    c.use = in.keyDown(SDL_SCANCODE_SPACE);
    // Holding the right button turns sideways mouse movement into strafing.
    if (in.mouseDown(SDL_BUTTON_RIGHT)) c.strafe = in.mouseDelta().x * sensitivity;
    else c.turn = in.mouseDelta().x * cfg::kLookRadiansPerPixel * sensitivity;
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

/// Whether an open door's panel stands in the way from its doorway to `way`
/// (a point beyond the doorway): on the same side of the doorway as that point.
bool doorBlocksWay(const Door& door, const glm::vec3& way) {
    const glm::vec3 c = door.doorwayCenter();
    const glm::vec2 panel(door.center().x - c.x, door.center().z - c.z);
    const glm::vec2 to(way.x - c.x, way.z - c.z);
    return glm::dot(panel, to) > 0.0f;
}

/// Splits text into lines of at most `width` characters at spaces.
std::vector<std::string> wrapText(const std::string& text, size_t width) {
    std::vector<std::string> lines;
    std::string line;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        const std::string word = text.substr(i, j - i);
        if (!line.empty() && line.size() + 1 + word.size() > width) {
            lines.push_back(line);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
        i = j + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

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

    m_window = SDL_CreateWindow(cfg::kWindowTitle, m_options.windowWidth, m_options.windowHeight,
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

    m_pointerCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);

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
    m_gun = std::make_unique<TeslaGun>(rnd::hashCombine(m_options.seed, 0x7E51'A600ull));

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
              << "         E open doors / use terminals / pick up phones / search cabinets / take parts (Esc leaves),\n"
              << "         R assemble the Tesla gun, LMB or F fire it, hold RMB + move mouse to drive,\n"
              << "         +/- sensitivity, F3 entity debug, F11 fullscreen, F12 screenshot, P pause, Esc quit.\n";

    setState(m_state);
    return true;
}

// ---- State ---------------------------------------------------------------------------------------

void Engine::setState(GameState state) {
    m_state = state;
    // Holding a phone, a mouse pointer presses its keys.
    SDL_SetWindowRelativeMouseMode(m_window, state != GameState::Paused && state != GameState::Phone);
    if (state != GameState::Phone) SDL_SetCursor(SDL_GetDefaultCursor());
    if (m_sound) m_sound->setPaused(state == GameState::Paused);
    if (state == GameState::Terminal || state == GameState::Phone) SDL_StartTextInput(m_window);
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
            else if (m_state == GameState::Cabinet && e.button.button == SDL_BUTTON_LEFT) m_cabinetUse = true;
            else if (m_state == GameState::Phone && e.button.button == SDL_BUTTON_LEFT) {
                const int key = phoneKeyAt(e.button.x, e.button.y);
                if (key == Phone::kLampButton) m_phoneKeys += Phone::kMessageChar;
                else if (key >= 0) m_phoneKeys += Phone::kKeyChars[key];
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (m_state == GameState::Cabinet) m_cabinetMove += e.wheel.y > 0.0f ? 1 : e.wheel.y < 0.0f ? -1 : 0;
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (m_state == GameState::Terminal && m_console) m_console->type(e.text.text);
            if (m_state == GameState::Phone) {
                for (const char* c = e.text.text; *c; ++c) {
                    if (Phone::keyIndex(*c) >= 0) m_phoneKeys += *c;
                    else if (*c == 'm' || *c == 'M') m_phoneKeys += Phone::kMessageChar;
                }
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            if (m_state == GameState::Terminal) {
                handleTerminalKey(e.key);
                break;
            }
            if (m_state == GameState::Phone) {
                handlePhoneKey(e.key);
                break;
            }
            if (m_state == GameState::Cabinet) {
                handleCabinetKey(e.key);
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

void Engine::handlePhoneKey(const SDL_KeyboardEvent& key) {
    // Digits, * and # (and M) arrive as text input; here only hanging up and a few globals.
    switch (key.scancode) {
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_E:
        if (!key.repeat) m_phoneHangUp = true;
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

// ---- Phones ------------------------------------------------------------------------------------

Phone* Engine::activePhone() const { return m_phoneId ? m_chunks->phoneById(m_phoneId) : nullptr; }

void Engine::enterPhone(Phone& phone) {
    // The handset comes up to the ear; a fresh call every time.
    phone.setOffHook(true);
    m_phoneId = phone.id();
    const uint64_t session = rnd::hashCombine(phone.id(), static_cast<uint64_t>(m_simTime * 1000.0) + static_cast<uint64_t>(++m_phonePickups));
    PhoneMailbox mailbox; // which message it holds, and how the system counts, are fixed per phone
    const uint64_t box = rnd::hashCombine(phone.id(), 0x3E55'A6E5ull);
    if (phone.messageWaiting()) mailbox.message = static_cast<int>(box % static_cast<uint64_t>(phonesfx::kMessageCount));
    mailbox.miscounts = (box >> 32) % 5u == 0u;
    m_call = std::make_unique<PhoneCall>(m_sound->bank(), rnd::hashCombine(m_options.seed, 0x9403'CA11ull), session, mailbox);
    m_phoneKeys.clear();
    m_phoneHangUp = false;
    m_phoneHover = -1;
    m_phoneNoiseTimer = 0.0f;
    m_sound->playEffect(SoundId::PhonePickup, phone.center(), 0.5f, *m_world);
    m_noises.push_back({phone.center(), cfg::kNoiseTyping, NoiseKind::Machine});

    // Lean over the keypad.
    m_phoneEye = phone.viewPoint();
    const glm::vec3 look = phone.viewTarget() - m_phoneEye;
    m_phoneYaw = yawToward(look);
    m_phonePitch = pitchToward(look);
    setState(GameState::Phone);
    int ww = 0, wh = 0;
    SDL_GetWindowSize(m_window, &ww, &wh);
    SDL_WarpMouseInWindow(m_window, 0.5f * static_cast<float>(ww), 0.62f * static_cast<float>(wh));
}

void Engine::leavePhone() {
    if (Phone* phone = activePhone()) {
        phone->setOffHook(false);
        phone->setLampLit(false); // a message cut off stays waiting
        m_sound->playEffect(SoundId::PhoneHangup, phone->center(), 0.55f, *m_world);
        m_noises.push_back({phone->center(), cfg::kNoiseTyping * 1.4f, NoiseKind::Machine});
    }
    m_sound->stopEarpiece();
    m_call.reset();
    m_phoneKeys.clear();
    m_phoneHangUp = false;
    if (m_state == GameState::Phone) setState(GameState::Running);
    // m_phoneId stays set while the view blends back.
}

int Engine::phoneKeyAt(float windowX, float windowY) const {
    const Phone* phone = activePhone();
    if (!phone || m_state != GameState::Phone) return -1;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(m_window, &ww, &wh);
    const float W = static_cast<float>(m_pixelWidth), H = static_cast<float>(m_pixelHeight);
    const glm::vec2 p(windowX * W / static_cast<float>(std::max(1, ww)), windowY * H / static_cast<float>(std::max(1, wh)));
    const Camera cam = viewCamera();
    const glm::mat4 viewProj = cam.projectionMatrix(W / std::max(H, 1.0f)) * cam.viewMatrix();
    // Is the pointer inside this world-space quad, projected onto the screen?
    auto under = [&](const glm::vec3 corners[4]) {
        glm::vec2 s[4];
        for (int i = 0; i < 4; ++i) {
            const glm::vec4 clip = viewProj * glm::vec4(corners[i], 1.0f);
            if (clip.w <= 1e-4f) return false;
            const glm::vec2 ndc = glm::vec2(clip) / clip.w;
            s[i] = glm::vec2((ndc.x * 0.5f + 0.5f) * W, (0.5f - 0.5f * ndc.y) * H);
        }
        int positive = 0, negative = 0;
        for (int i = 0; i < 4; ++i) {
            const glm::vec2 e = s[(i + 1) % 4] - s[i], d = p - s[i];
            const float cross = e.x * d.y - e.y * d.x;
            positive += cross > 0.0f;
            negative += cross < 0.0f;
        }
        return positive == 0 || negative == 0;
    };
    glm::vec3 corners[4];
    // Each key's share of the keypad...
    for (int k = 0; k < Phone::kKeyCount; ++k) {
        phone->keyHitCorners(k, corners);
        if (under(corners)) return k;
    }
    // ...and the message lamp.
    phone->lampHitCorners(corners);
    return under(corners) ? Phone::kLampButton : -1;
}

void Engine::updatePhone(float dt) {
    const bool holding = m_state == GameState::Phone;
    const float step = dt / cfg::kTerminalOpenTime;
    m_phoneBlend = std::clamp(m_phoneBlend + (holding ? step : -step), 0.0f, 1.0f);
    if (!holding) {
        if (m_phoneBlend <= 0.0f) m_phoneId = 0;
        return;
    }
    Phone* phone = activePhone();
    if (!phone || !m_call || m_phoneHangUp) { // hung up (or its chunk went away)
        leavePhone();
        return;
    }
    const glm::vec3 at = phone->center();
    for (char c : m_phoneKeys) {
        if (c == Phone::kMessageChar) {
            phone->press(Phone::kLampButton);
            m_call->pressMessage();
        } else {
            phone->press(Phone::keyIndex(c));
            m_call->press(c);
        }
        m_noises.push_back({at, cfg::kNoiseTyping * 0.6f, NoiseKind::Typing}); // the Wanderer hears you dialling
    }
    m_phoneKeys.clear();
    phone->update(dt);

    PhoneContext ctx;
    ctx.stalkerBehind = m_entities->stalkerBehindPlayer();
    ctx.wandererDistance = m_entities->wandererDistance();
    m_call->update(dt, ctx);
    const bool log = m_options.demo == "phone"; // scripted verification of the line's behaviour
    for (const PhoneSoundEvent& e : m_call->takeSounds()) {
        if (e.id == SoundId::Count) m_sound->stopEarpieceSounds(); // the line moved on
        else if (e.earpiece) m_sound->playEarpiece(e.id, e.variant, e.gain, e.delay);
        else m_sound->playEffect(e.id, at, e.gain, *m_world);
        if (log) std::printf("[Phone] t=%.2f sound %d variant %d (+%.2fs)\n", m_demoTime, static_cast<int>(e.id), e.variant, e.delay);
    }
    // The lamp burns while its message plays, and goes out once it has been heard.
    phone->setLampLit(m_call->messagePlaying());
    if (m_call->messageHeard()) phone->setMessageWaiting(false);
    if (m_call->hungUp()) { // hung up on: the player's handset goes back down too
        if (log) std::printf("[Phone] t=%.2f the other end hung up\n", m_demoTime);
        leavePhone();
        return;
    }
    if (log) {
        const std::string state = "tone " + std::to_string(static_cast<int>(m_call->tone())) + " \"" +
                                  (m_call->captionAlpha() > 0.0f ? m_call->caption() : std::string()) + "\"";
        if (state != m_lastPhoneLog) std::printf("[Phone] t=%.2f %s\n", m_demoTime, state.c_str());
        m_lastPhoneLog = state;
    }
    m_sound->setPhoneLine(m_call->tone(), m_call->toneGain(), m_call->lineGain());
    // Left off the hook, the howler screams out of the earpiece - and carries.
    if (m_call->tone() == SoundId::PhoneHowler && (m_phoneNoiseTimer -= dt) <= 0.0f) {
        m_noises.push_back({at, cfg::kNoiseMachine * 0.8f, NoiseKind::Machine});
        m_phoneNoiseTimer = 1.5f;
    }

    // The pointer becomes a hand over a key.
    float mx = 0.0f, my = 0.0f;
    SDL_GetMouseState(&mx, &my);
    const int hover = phoneKeyAt(mx, my);
    if ((hover >= 0) != (m_phoneHover >= 0)) SDL_SetCursor(hover >= 0 && m_pointerCursor ? m_pointerCursor : SDL_GetDefaultCursor());
    m_phoneHover = hover;
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
    // Filing-cabinet drawers rumbling out and slamming shut: heard by the player and by the Wanderer.
    for (const CabinetEvent& c : m_chunks->cabinetEvents()) {
        if (c.flags & FileCabinet::kEventOpen) m_sound->playEffect(SoundId::CabinetOpen, c.position, 0.5f, *m_world);
        if (c.flags & FileCabinet::kEventClose) m_sound->playEffect(SoundId::CabinetClose, c.position, 0.55f, *m_world);
        m_noises.push_back({c.position, cfg::kNoiseCabinet * ((c.flags & FileCabinet::kEventClose) ? 1.0f : 0.7f), NoiseKind::Door});
    }
}

void Engine::startCaught(EntityKind by) {
    if (m_state == GameState::Terminal) leaveTerminal(false);
    if (m_state == GameState::Phone) leavePhone();
    if (m_state == GameState::Cabinet) leaveCabinet();
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
        } else if (target.kind == Interactable::Kind::Phone) {
            m_prompt = "E  PICK UP PHONE";
            if (m_input.keyPressed(SDL_SCANCODE_E)) enterPhone(*target.phone);
        } else if (target.kind == Interactable::Kind::Cabinet) {
            m_prompt = "E  SEARCH FILING CABINET";
            if (m_input.keyPressed(SDL_SCANCODE_E)) enterCabinet(*target.cabinet, FileCabinet::kDrawers - 1);
        } else if (target.kind == Interactable::Kind::Item) {
            m_prompt = "E  " + itemPrompt(*target.item);
            if (m_input.keyPressed(SDL_SCANCODE_E)) takeItem(*target.item);
        }
    }

    // Seated, on the phone or caught, the body just stands there.
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
    updatePhone(dt);
    updateCabinet(dt);
    collectNoise();
    updateGun(dt);

    // ---- Anomalies: they perceive what the player actually sees.
    const bool blind = m_state == GameState::Terminal || m_state == GameState::Phone || m_state == GameState::Cabinet ||
                       (m_state == GameState::Caught && m_fade > 0.5f);
    const float aspect = static_cast<float>(m_pixelWidth) / static_cast<float>(std::max(1, m_pixelHeight));
    m_entities->update(dt, viewCamera(), aspect, m_player->feetPosition(), m_focusLevel, blind, *m_chunks, *m_physics,
                       m_noises);
    if (const auto by = m_entities->takeCatch(); by && m_state != GameState::Caught) startCaught(*by);
    if (const auto gone = m_entities->takeVaporised()) {
        showMessage(*gone == EntityKind::Stalker ? "THE STALKER IS GONE. FOR GOOD." : "THE WANDERER IS GONE. FOR GOOD.", 5.0f);
    }
    if (m_state == GameState::Caught) updateCaught(dt);

    // After every update so this frame's footstep / door / entity events are
    // heard, and with the renderer's clock so the tube buzz matches the flicker.
    m_sound->update(dt, m_simTime, *m_player, *m_chunks, *m_world);
    m_sound->updateEntities(dt, m_entities->audioState(), m_entities->sounds(), m_entities->fear(), *m_world);

    // Crosshair ring fades in when a door, terminal, phone, cabinet or part is within reach.
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
    if (m_phoneBlend > 0.0f) {
        const float t = smooth01(m_phoneBlend);
        cam.position = glm::mix(cam.position, m_phoneEye, t);
        cam.yaw += wrapAngle(m_phoneYaw - cam.yaw) * t;
        cam.pitch += (m_phonePitch - cam.pitch) * t;
        cam.fovYDegrees += (52.0f - cam.fovYDegrees) * t;
    }
    if (m_cabinetBlend > 0.0f) {
        const float t = smooth01(m_cabinetBlend);
        cam.position = glm::mix(cam.position, m_cabinetEye, t);
        cam.yaw += wrapAngle(m_cabinetYaw - cam.yaw) * t;
        cam.pitch += (m_cabinetPitch - cam.pitch) * t;
        cam.fovYDegrees += (58.0f - cam.fovYDegrees) * t;
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
        const char* hint = m_console->doomActive() ? "<ESC> Quit  <W><A><S><D> Move  MOUSE Turn  <E>/CLICK Fire  <SPACE> Use  <2> <3> Weapons"
                           : m_console->commandMode() ? "<ESC> Leave     <ENTER> Run Command     Type HELP for Commands"
                                                      : "<ESC> Leave     <ENTER> Command Prompt";
        hud.text(hint, w * 0.5f, h - 40.0f * s, TextOverlay::Align::Center, 2.0f,
                 glm::vec4(0.8f, 0.8f, 0.75f, 0.6f * m_terminalBlend), true);
    }
    if (m_state == GameState::Phone && m_call) {
        const glm::vec4 guide(0.8f, 0.8f, 0.75f, 0.6f * m_phoneBlend);
        hud.text("<ESC>/<E> Hang Up    <0-9> <*> <#> or CLICK the Keys to Dial    <M> or the Red Light: Messages",
                 w * 0.5f, h - 40.0f * s, TextOverlay::Align::Center, 2.0f, guide, true);
        if (!m_call->dialed().empty()) {
            hud.text(m_call->dialed(), w * 0.5f, h - 76.0f * s, TextOverlay::Align::Center, 3.0f,
                     glm::vec4(0.95f, 0.93f, 0.85f, 0.8f * m_phoneBlend), true);
        }
        // What is heard on the line: the operator steady, the voices faint and unsteady.
        if (const float a = m_call->captionAlpha(); a > 0.0f) {
            const bool voice = m_call->captionAnomalous();
            const float t = static_cast<float>(m_simTime);
            const float flicker = voice ? 0.7f + 0.3f * std::sin(t * 23.0f) * std::sin(t * 7.1f) : 1.0f;
            const glm::vec4 ink = voice ? glm::vec4(0.85f, 0.5f, 0.45f, 0.85f * a * flicker) : glm::vec4(0.9f, 0.9f, 0.85f, 0.85f * a);
            const std::vector<std::string> lines = wrapText(voice ? "... " + m_call->caption() + " ..." : m_call->caption(), 56);
            for (size_t i = 0; i < lines.size(); ++i) {
                hud.text(lines[i], w * 0.5f, h * 0.12f + static_cast<float>(i) * 22.0f * s, TextOverlay::Align::Center, 2.0f, ink,
                         true);
            }
        }
    }
    if (m_cabinetBlend > 0.0f && m_cabinetId) {
        // What is in the drawer, and the keys.
        const glm::vec4 guide(0.8f, 0.8f, 0.75f, 0.6f * m_cabinetBlend);
        const ItemSite* site = cabinetSite();
        const bool part = site && site->item;
        static const char* const kDrawerNames[FileCabinet::kDrawers] = {"BOTTOM DRAWER", "MIDDLE DRAWER", "TOP DRAWER"};
        const std::string what = std::string(kDrawerNames[m_cabinetDrawer]) + (part ? ":  " + std::string(partName(site->item->type)) : ":  FILES");
        hud.text(what, w * 0.5f, h * 0.1f, TextOverlay::Align::Center, 2.0f,
                 glm::vec4(0.95f, 0.93f, 0.85f, (part ? 0.9f : 0.6f) * m_cabinetBlend), true);
        // E takes what is in the drawer; with nothing to take, it closes the cabinet like Esc.
        const std::string keys = part ? "<ESC> Close    <W>/<S> Drawer    <E> " + itemPrompt(*site)
                                      : std::string("<ESC>/<E> Close    <W>/<S> Drawer");
        hud.text(keys, w * 0.5f, h - 40.0f * s, TextOverlay::Align::Center, 2.0f, guide, true);
    }
    drawInventory();
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

    buildViewModel(cam);

    FrameParams frame;
    frame.camera = cam;
    frame.time = m_simTime;
    frame.crosshair = 1.0f - std::max(m_phoneBlend, m_cabinetBlend);
    frame.crosshairHighlight = m_state == GameState::Running ? m_crosshairHighlight : 0.0f;
    frame.fear = m_entities->fear();
    frame.fade = m_fade;
    frame.lightDisturbances = &m_entities->lightDisturbances();
    frame.entities = &m_entityDraw;
    frame.bolts = &m_gun->bolts();
    frame.glows = &m_gun->glows();
    frame.muzzleGlow = m_gun->muzzleGlow();
    frame.arcLight = m_gun->light();
    frame.viewModel = &m_viewModel;
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
    m_gun.reset();
    m_consoles.clear();
    m_call.reset();
    m_player.reset();
    m_chunks.reset();
    m_physics.reset();
    m_world.reset();

    if (m_context) {
        SDL_GL_DestroyContext(m_context);
        m_context = nullptr;
    }
    if (m_pointerCursor) {
        SDL_DestroyCursor(m_pointerCursor);
        m_pointerCursor = nullptr;
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

    if (demo == "stairs" || demo == "stairs-top" || demo == "stairs-sign" || demo == "climb" || demo == "descend" ||
        demo == "stairs-chase") {
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
                    // so they swing out of the way of the route - except, with
                    // --type door, the one the walk starts at: the player opens
                    // that one from outside, as a player would.
                    const int side = stairs::entranceSide(s->rotation);
                    const int ex = gx + (side == 1 ? 1 : 0), ez = gz + (side == 3 ? 1 : 0);
                    const world::EdgeAxis axis = side < 2 ? world::EdgeAxis::West : world::EdgeAxis::South;
                    const glm::vec3 inside((static_cast<float>(gx) + 0.5f) * world::kCellSize, 0.0f,
                                           (static_cast<float>(gz) + 0.5f) * world::kCellSize);
                    const bool viaDoor = m_options.demoInput == "door" && (demo == "climb" || demo == "descend");
                    const int startLevel = demo == "descend" ? level + 1 : level;
                    for (int l : {level, level + 1}) {
                        if (viaDoor && l == startLevel) continue;
                        if (Door* d = m_chunks->doorOnEdge(l, ex, ez, axis)) {
                            d->toggle(inside);                       // picks the outward swing...
                            d->restoreState(d->persistentState());   // ...and snaps fully open
                            d->takeEvents();
                        }
                    }
                    if (viaDoor) { // walk up to the shut door and open it first
                        m_autopilot.push_back({demo == "descend" ? route.back() : route.front(), true, startLevel, ex, ez, axis});
                    }
                    if (demo == "climb" || demo == "stairs-chase") {
                        for (auto it = route.begin() + 1; it != route.end(); ++it) m_autopilot.push_back({*it});
                        m_autopilotIndex = 0;
                    }
                    if (demo == "stairs-chase") {
                        // Something follows, from a few rooms back; the doors will be shut behind the player.
                        // "wanderer-far": out of earshot, so it never hears the player go (the fallback brings it).
                        const bool far = m_options.demoInput == "wanderer-far";
                        const bool wanderer = far || m_options.demoInput == "wanderer";
                        m_chaser = wanderer ? EntityKind::Wanderer : EntityKind::Stalker;
                        m_entities->setEnabled(wanderer ? EntityKind::Stalker : EntityKind::Wanderer, false);
                        glm::vec3 out = route[0] - route[1];
                        out.y = 0.0f;
                        out = glm::normalize(out);
                        glm::vec3 spot = route[0];
                        for (float d = far ? 30.0f : wanderer ? 7.0f : 10.0f; d >= 3.0f; d -= 0.5f) {
                            const glm::vec3 p = route[0] + out * d;
                            if (m_physics->isFree(Physics::bodyBox(p + glm::vec3(0.0f, 0.01f, 0.0f), {0.3f, 2.2f}), *m_chunks)) {
                                spot = p;
                                break;
                            }
                        }
                        m_entities->spawnAt(m_chaser, spot, level, std::atan2(-out.x, -out.z), true);
                        for (int k = 0; k < 2; ++k) m_chaseDoors[k] = {level + k, ex, ez, axis, false};
                        std::printf("[Demo] The %s starts %.1fm from the stairwell\n", wanderer ? "Wanderer" : "Stalker",
                                    glm::length(spot - route[0]));
                    } else if (demo == "descend") {
                        for (auto it = route.rbegin() + 1; it != route.rend(); ++it) m_autopilot.push_back({*it});
                        m_autopilotIndex = 0;
                    }
                    if (viaDoor) {
                        // Report where the door ends up once open: against the far side of the lobby?
                        m_chaseDoors[0] = {startLevel, ex, ez, axis, true};
                        m_chaseWay = route[demo == "descend" ? 8 : 2];
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
    } else if (demo == "phone") {
        // Nearest phone on this storey: pick it up (--type: then dial; 'h' hangs up,
        // 'M' presses the message lamp - and picks a phone with a message waiting).
        const bool wantMessage = m_options.demoInput.find(Phone::kMessageChar) != std::string::npos;
        Phone* best = nullptr;
        float bestDist = 1e9f;
        for (Chunk* chunk : m_chunks->sortedChunksMutable()) {
            if (chunk->coord().level != m_focusLevel) continue;
            for (Phone& p : chunk->phones()) {
                if (wantMessage && !p.messageWaiting()) continue;
                const float d = glm::length(p.center() - feet);
                if (d < bestDist) {
                    bestDist = d;
                    best = &p;
                }
            }
        }
        if (!best) {
            std::cerr << "[Demo] No phone loaded\n";
            return;
        }
        const uint64_t id = best->id();
        const glm::vec3 at = best->center();
        const glm::vec3 front = glm::normalize(glm::vec3(best->modelMatrix() * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
        glm::vec3 stand = at + front * 0.6f;
        stand.y = world::levelFloorY(m_focusLevel);
        teleportPlayer(stand, m_focusLevel, yawToward(at - stand));
        m_player->setViewAngles(yawToward(at - stand), pitchToward(at - m_player->eyePosition()));
        // Re-find it: teleporting may have reloaded its chunk.
        if (Phone* p = m_chunks->phoneById(id)) enterPhone(*p);
        m_phoneBlend = 1.0f;
    } else if (demo == "cabinet") {
        // The nearest filing cabinet with a part in a drawer (else the nearest
        // one at all): stand in front of it and search that drawer.
        const FileCabinet* best = nullptr;
        int bestDrawer = FileCabinet::kDrawers - 1;
        float bestDist = 1e9f;
        bool bestHasPart = false;
        for (Chunk* chunk : m_chunks->sortedChunksMutable()) {
            if (chunk->coord().level != m_focusLevel) continue;
            for (size_t i = 0; i < chunk->cabinets().size(); ++i) {
                const FileCabinet& c = chunk->cabinets()[i];
                int drawer = FileCabinet::kDrawers - 1;
                bool hasPart = false;
                for (int d = 0; d < FileCabinet::kDrawers && !hasPart; ++d) {
                    const ItemSite* site = chunk->drawerSite(static_cast<int>(i), d);
                    if (site && site->item) {
                        hasPart = true;
                        drawer = d;
                    }
                }
                const float dist = glm::length(c.frontCenter() - feet);
                if ((hasPart && !bestHasPart) || (hasPart == bestHasPart && dist < bestDist)) {
                    best = &c;
                    bestDist = dist;
                    bestDrawer = drawer;
                    bestHasPart = hasPart;
                }
            }
        }
        if (!best) {
            std::cerr << "[Demo] No filing cabinet loaded\n";
            return;
        }
        const uint64_t id = best->id();
        glm::vec3 stand = best->frontCenter() + best->frontNormal() * 1.1f;
        stand.y = world::levelFloorY(m_focusLevel);
        teleportPlayer(stand, m_focusLevel, yawToward(best->frontCenter() - stand));
        std::printf("[Demo] Filing cabinet %.1fm away, %s\n", bestDist, bestHasPart ? "with a part" : "no parts nearby");
        if (FileCabinet* c = m_chunks->cabinetById(id)) enterCabinet(*c, bestDrawer); // re-found: teleporting may reload
        m_cabinetBlend = 1.0f;
        m_demoCabinet = id;
        m_demoDrawer = bestDrawer;
        m_demoReturn = m_player->feetPosition();
        if (m_options.demoInput == "swap-persist") {
            // Hold a nearly flat part of the same type, so taking this one swaps it in.
            if (const ItemSite* site = m_chunks->drawerSite(id, bestDrawer); site && site->item) {
                Item flat;
                flat.id = 0xF1A7ull;
                flat.type = site->item->type;
                flat.charge = 0.07f;
                m_inventory.give(flat);
                std::printf("[Demo] Drawer holds %s at %.2f; holding one at %.2f\n", partName(site->item->type), site->item->charge,
                            flat.charge);
            }
        }
    } else if (demo == "stalker-stare") {
        // It stands a few metres ahead; the player keeps turning to face it wherever it is.
        m_entities->setEnabled(EntityKind::Wanderer, false);
        const glm::vec3 p = freeSpotAlong(fwd, 6.0f, 3.5f);
        m_entities->spawnAt(EntityKind::Stalker, p, m_focusLevel, std::atan2(-fwd.x, -fwd.z));
    } else if (demo == "stalker-door") {
        // The nearest door on this storey, shut: the player stands on one side,
        // looking away; the Stalker hunts them from the other.
        int bestX = 0, bestZ = 0;
        world::EdgeAxis bestAxis = world::EdgeAxis::West;
        float bestDist = 1e9f;
        const glm::ivec2 c = NavGrid::cellOf(feet);
        for (int z = c.y - 6; z <= c.y + 6; ++z) {
            for (int x = c.x - 6; x <= c.x + 6; ++x) {
                for (world::EdgeAxis a : {world::EdgeAxis::West, world::EdgeAxis::South}) {
                    const Door* d = m_chunks->doorOnEdge(m_focusLevel, x, z, a);
                    if (!d || m_world->cellRole(m_focusLevel, x, z) != world::CellRole::Room) continue;
                    const glm::ivec2 other = a == world::EdgeAxis::West ? glm::ivec2(x - 1, z) : glm::ivec2(x, z - 1);
                    if (m_world->cellRole(m_focusLevel, other.x, other.y) != world::CellRole::Room) continue;
                    const float dist = glm::length(d->doorwayCenter() - feet);
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestX = x;
                        bestZ = z;
                        bestAxis = a;
                    }
                }
            }
        }
        const Door* door = m_chunks->doorOnEdge(m_focusLevel, bestX, bestZ, bestAxis);
        if (!door) {
            std::cerr << "[Demo] No door loaded\n";
            return;
        }
        const glm::vec3 n = bestAxis == world::EdgeAxis::West ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 centre = door->doorwayCenter();
        centre.y = world::levelFloorY(m_focusLevel);
        const glm::vec3 stand = centre + n * 1.8f, lair = centre - n * 4.5f;
        teleportPlayer(stand, m_focusLevel, yawToward(n)); // facing away from the door
        m_entities->setEnabled(EntityKind::Wanderer, false);
        m_entities->spawnAt(EntityKind::Stalker, lair, m_focusLevel, std::atan2(n.x, n.z), true);
        m_chaser = EntityKind::Stalker;
        m_chaseDoors[0] = {m_focusLevel, bestX, bestZ, bestAxis, true};
        std::printf("[Demo] Door %.1fm away; the Stalker waits 4.5m behind it\n", bestDist);
    } else if (demo == "part") {
        // The nearest part lying out in the open (a desk top or a chair seat): walk up and look at it.
        const ItemSite* best = nullptr;
        float bestDist = 1e9f;
        int counts[3] = {0, 0, 0};
        int desks = 0, chairs = 0, cabinets = 0, chunks = 0;
        for (Chunk* chunk : m_chunks->sortedChunksMutable()) {
            if (chunk->coord().level != m_focusLevel) continue;
            ++chunks;
            cabinets += static_cast<int>(chunk->cabinets().size());
            for (const FurnitureInstance& f : chunk->furniture()) {
                desks += f.type == FurnitureType::Desk ? 1 : 0;
                chairs += f.type == FurnitureType::Chair ? 1 : 0;
            }
            for (const ItemSite& site : chunk->items()) {
                if (!site.item) continue;
                ++counts[static_cast<int>(site.kind)];
                if (site.kind == SiteKind::Drawer) continue;
                const float d = glm::length(glm::vec3(site.world[3]) - feet);
                if (d < bestDist) {
                    bestDist = d;
                    best = &site;
                }
            }
        }
        std::printf("[Demo] Parts on this storey's %d loaded chunks: %d on %d desks, %d on %d chairs, %d in %d cabinets\n", chunks,
                    counts[0], desks, counts[1], chairs, counts[2], cabinets);
        if (!best) {
            std::cerr << "[Demo] No loose part loaded\n";
            return;
        }
        const glm::vec3 at(best->world * glm::vec4(tesla::centerLocal(best->item->type), 1.0f));
        std::printf("[Demo] %s on a %s, %.1fm away\n", partName(best->item->type), best->kind == SiteKind::Desk ? "desk" : "chair", bestDist);
        // Approach from a side where nothing (a chair's backrest, a monitor) hides it.
        glm::vec3 chosen(0.0f);
        bool found = false, clear = false;
        for (int k = 0; k < 16 && !clear; ++k) {
            const float a = static_cast<float>(k) * 0.3926991f;
            glm::vec3 stand = at + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * 1.1f;
            stand.y = world::levelFloorY(m_focusLevel);
            if (!m_physics->isFree(Physics::bodyBox(stand + glm::vec3(0.0f, 0.01f, 0.0f), m_player->shape()), *m_chunks)) continue;
            const glm::vec3 eye = stand + glm::vec3(0.0f, cfg::kStandHeight - cfg::kEyeBelowTop, 0.0f);
            const glm::vec3 to = at + glm::vec3(0.0f, 0.04f, 0.0f) - eye;
            float t = 0.0f;
            clear = !m_physics->raycast(eye, glm::normalize(to), glm::length(to) - 0.12f, *m_chunks, t);
            if (clear || !found) chosen = stand;
            found = true;
        }
        if (found) {
            teleportPlayer(chosen, m_focusLevel, yawToward(at - chosen));
            m_player->setViewAngles(yawToward(at - chosen), pitchToward(at - m_player->eyePosition()));
        }
    } else if (demo == "assemble" || demo == "tesla" || demo == "tesla-stalker") {
        // Every part in hand ("assemble": then put together; "tesla": the gun
        // ready, and a target standing a few metres ahead).
        const float charge = m_options.demoInput.empty() ? 1.0f : std::clamp(static_cast<float>(std::atof(m_options.demoInput.c_str())) / 100.0f, 0.0f, 1.0f);
        for (int t = 0; t < kPartTypeCount; ++t) {
            Item item;
            item.id = rnd::hashCombine(0xDE30ull, static_cast<uint64_t>(t));
            item.type = static_cast<PartType>(t);
            item.charge = item.type == PartType::Battery ? charge : 1.0f;
            m_inventory.give(item);
        }
        if (demo != "assemble") {
            m_inventory.assemble();
            m_gunRaise = 1.0f;
            const bool stalker = demo == "tesla-stalker";
            m_entities->setEnabled(stalker ? EntityKind::Wanderer : EntityKind::Stalker, false);
            const glm::vec3 p = freeSpotAlong(fwd, 4.5f, 2.5f);
            m_entities->spawnAt(stalker ? EntityKind::Stalker : EntityKind::Wanderer, p, m_focusLevel, std::atan2(-fwd.x, -fwd.z));
        }
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
    if (m_options.demo == "phone" && m_state == GameState::Phone && m_demoTime > 2.5f &&
        m_demoStep < static_cast<int>(m_options.demoInput.size()) && m_demoTime > 2.5f + 0.3f * static_cast<float>(m_demoStep)) {
        const char c = m_options.demoInput[static_cast<size_t>(m_demoStep++)]; // dial, one key at a time
        if (c == 'h') m_phoneHangUp = true;                                    // ...or hang up
        else m_phoneKeys += c;
    }
    if (m_options.demo == "cabinet" && m_demoStep == 0 && m_demoTime > 1.5f &&
        (m_options.demoInput == "take" || m_options.demoInput == "swap-persist")) {
        m_cabinetUse = true; // take (or swap) what is in the drawer
        m_demoStep = 1;
    }
    if ((m_options.demo == "climb" || m_options.demo == "descend") && m_options.demoInput == "door" && m_chaseDoors[0].shut) {
        const ChaseDoor& cd = m_chaseDoors[0];
        const Door* d = m_chunks->doorOnEdge(cd.level, cd.gx, cd.gz, cd.axis);
        if (d && d->state() == Door::State::Open && m_autopilot.size() > 1) {
            // m_autopilot[1]: just inside the doorway, in the stairwell.
            const glm::vec3 panel = d->center() - d->doorwayCenter(), inside = m_autopilot[1].pos - d->doorwayCenter();
            std::printf("[Demo] t=%.2f the player opened the stairwell door from outside: it swung %s, %s\n", m_demoTime,
                        glm::dot(glm::vec2(panel.x, panel.z), glm::vec2(inside.x, inside.z)) > 0.0f ? "in, away from them" : "OUT, towards them",
                        doorBlocksWay(*d, m_chaseWay) ? "BLOCKING the way to the stairs" : "clear of the way to the stairs");
            m_chaseDoors[0].shut = false;
        }
    }
    if (m_options.demo == "stalker-stare") {
        const Stalker& st = m_entities->stalker();
        if (st.active()) { // the head follows it (through the walls, too: only a clear view counts as seen)
            const glm::vec3 d = st.feet() + glm::vec3(0.0f, 1.2f, 0.0f) - m_player->eyePosition();
            m_player->setViewAngles(yawToward(d), pitchToward(d));
        }
        static bool wasRetreating = false;
        if (st.retreating() != wasRetreating) {
            std::printf("[Stare] t=%.2f %s, %.1fm from the player\n", m_demoTime,
                        st.retreating() ? "third stare-down: it bolts far away" : "retreat over: it lies low",
                        glm::length(st.feet() - m_player->feetPosition()));
            wasRetreating = st.retreating();
        }
        static int lastStares = 0;
        if (st.stareDowns() != lastStares) {
            std::printf("[Stare] t=%.2f stare-downs: %d\n", m_demoTime, st.stareDowns());
            lastStares = st.stareDowns();
        }
    }
    if (m_options.demo == "stairs-chase" || m_options.demo == "stalker-door") {
        // Shut each stairwell entrance behind the player: whatever follows must open it.
        if (m_options.demo == "stairs-chase") {
            for (int k = 0; k < 2; ++k) {
                ChaseDoor& cd = m_chaseDoors[k];
                const bool past = k == 0 ? m_autopilotIndex >= 3 : m_autopilotIndex >= m_autopilot.size();
                if (cd.shut || !past) continue;
                if (Door* d = m_chunks->doorOnEdge(cd.level, cd.gx, cd.gz, cd.axis); d && d->state() == Door::State::Open) {
                    d->toggle(m_player->feetPosition());
                    std::printf("[Demo] t=%.2f the level %d stairwell door swings shut behind the player\n", m_demoTime, cd.level);
                }
                cd.shut = true;
            }
        }
        const bool tick = std::floor(m_demoTime * 2.0f) != std::floor((m_demoTime - dt) * 2.0f);
        const Agent& chaser = m_chaser == EntityKind::Stalker ? static_cast<const Agent&>(m_entities->stalker()) : m_entities->wanderer();
        static float lastY = 0.0f, lastVisualY = 0.0f, jumpY = 0.0f, jumpVisualY = 0.0f;
        if (chaser.active()) {
            if (m_demoTime > dt) {
                jumpY = std::max(jumpY, std::fabs(chaser.feet().y - lastY));
                jumpVisualY = std::max(jumpVisualY, std::fabs(chaser.visualFeet().y - lastVisualY));
            }
            lastY = chaser.feet().y;
            lastVisualY = chaser.visualFeet().y;
        }
        if (tick && chaser.active() && m_options.demo == "stairs-chase") {
            std::printf("[Smooth] t=%.2f biggest height change in one frame: body %.3f m, drawn %.3f m (drawn height %.2f)\n",
                        m_demoTime, jumpY, jumpVisualY, chaser.visualFeet().y);
            jumpY = jumpVisualY = 0.0f;
        }
        if (tick && chaser.active()) {
            std::string doors;
            for (int k = 0; k < (m_options.demo == "stairs-chase" ? 2 : 1); ++k) {
                const Door* d = m_chunks->doorOnEdge(m_chaseDoors[k].level, m_chaseDoors[k].gx, m_chaseDoors[k].gz, m_chaseDoors[k].axis);
                char one[40];
                std::snprintf(one, sizeof(one), " L%d %s", m_chaseDoors[k].level, !d ? "none" : d->openAmount() > 0.99f ? "open" : d->openAmount() > 0.0f ? "moving" : "shut");
                doors += one;
                if (d && d->openAmount() > 0.5f && m_options.demo == "stairs-chase" && m_autopilot.size() > 7) {
                    // Is the open panel in the way to the stairs (the foot of flight A below, the head of flight B above)?
                    doors += doorBlocksWay(*d, m_autopilot[k == 0 ? 1 : 7].pos) ? " (BLOCKS)" : " (clear)";
                }
            }
            const glm::vec3 e = chaser.feet(), p = m_player->feetPosition();
            std::printf("[Chase] t=%.2f %s L%d (%.1f %.2f %.1f) %s%s | player L%d (%.1f %.2f %.1f) %.1fm | doors%s\n", m_demoTime,
                        m_chaser == EntityKind::Stalker ? "stalker" : "wanderer", chaser.level(), e.x, e.y, e.z,
                        m_chaser == EntityKind::Stalker ? stalkerStateName(m_entities->stalker().state()) : wandererStateName(m_entities->wanderer().state()),
                        chaser.usingStairs() ? " (stairs)" : "", m_focusLevel, p.x, p.y, p.z, glm::length(e - p), doors.c_str());
        }
    }
    if (m_options.demo == "cabinet" && m_options.demoInput == "use-key") {
        // The use key, pressed for real: at once (the drawer is still rolling out, so it
        // waits and then takes the part), then again with the drawer empty (it closes).
        auto pressE = [this](const char* why) {
            SDL_KeyboardEvent key{};
            key.scancode = SDL_SCANCODE_E;
            handleCabinetKey(key);
            std::printf("[Demo] t=%.2f E pressed (%s)\n", m_demoTime, why);
        };
        if (m_demoStep == 0 && m_demoTime > 0.05f) {
            pressE("drawer still rolling out");
            m_demoStep = 1;
        } else if (m_demoStep == 1 && m_demoTime > 2.5f) {
            pressE("drawer now empty");
            m_demoStep = 2;
        }
        static GameState lastState = GameState::Cabinet;
        if (m_state != lastState) {
            std::printf("[Demo] t=%.2f state %s\n", m_demoTime, m_state == GameState::Cabinet ? "Cabinet" : m_state == GameState::Running ? "Running" : "other");
            lastState = m_state;
        }
    }
    if (m_options.demo == "cabinet" && m_options.demoInput == "swap-persist") {
        auto report = [this](const char* when) {
            const ItemSite* site = m_chunks->drawerSite(m_demoCabinet, m_demoDrawer);
            if (!m_chunks->cabinetById(m_demoCabinet)) std::printf("[Demo] %s: the cabinet's chunk is not loaded\n", when);
            else if (!site || !site->item) std::printf("[Demo] %s: the drawer is empty\n", when);
            else std::printf("[Demo] %s: the drawer holds %s at %.2f (id %llx)\n", when, partName(site->item->type), site->item->charge,
                             static_cast<unsigned long long>(site->item->id));
        };
        if (m_demoStep == 1 && m_demoTime > 2.5f) {
            report("After the swap");
            m_cabinetLeave = true;
            m_demoStep = 2;
        } else if (m_demoStep == 2 && m_demoTime > 3.0f) {
            const glm::vec3 far = m_demoReturn + glm::vec3(300.0f, 0.0f, 0.0f);
            teleportPlayer(far, m_focusLevel, 0.0f);
            teleportPlayer(m_chunks->findSpawnPoint(far, m_focusLevel, m_player->shape(), *m_physics), m_focusLevel, 0.0f);
            report("300 m away");
            m_demoStep = 3;
        } else if (m_demoStep == 3 && m_demoTime > 4.0f) {
            teleportPlayer(m_demoReturn, m_focusLevel, 0.0f);
            report("Back again");
            m_demoStep = 4;
        }
    }
    if (m_options.demo == "assemble" && m_demoStep == 0 && m_demoTime > 0.8f) {
        if (beginAssembly()) std::printf("[Demo] Assembling the gun\n");
        m_demoStep = 1;
    }
    if (m_options.demo == "tesla" || m_options.demo == "tesla-stalker") {
        // Aim at it and hold the trigger until nothing is left of it.
        const bool stalker = m_options.demo == "tesla-stalker";
        const Agent& target = stalker ? static_cast<const Agent&>(m_entities->stalker()) : m_entities->wanderer();
        if (target.active() && !target.dying()) {
            const glm::vec3 chest = target.feet() + glm::vec3(0.0f, 1.3f, 0.0f);
            const glm::vec3 d = chest - m_player->eyePosition();
            m_player->setViewAngles(yawToward(d), pitchToward(d));
        }
        m_demoTrigger = m_demoTime > 1.0f && target.active() && !target.dying();
        const bool tick = std::floor(m_demoTime * 4.0f) != std::floor((m_demoTime - dt) * 4.0f);
        if (tick && (target.active() || m_demoStep == 0)) {
            std::printf("[Demo] t=%.2f %s health %.2f shock %.2f dissolve %.2f | charge %.2f discharging %d connected %d\n", m_demoTime,
                        stalker ? "stalker" : "wanderer", target.health(), target.shock(), target.dissolve(), m_inventory.charge(),
                        m_gun->discharging() ? 1 : 0, m_gun->connected() ? 1 : 0);
        }
        if (!target.active() && m_demoStep == 0 && m_entities->gone(stalker ? EntityKind::Stalker : EntityKind::Wanderer)) {
            std::printf("[Demo] t=%.2f the %s is gone for good\n", m_demoTime, stalker ? "stalker" : "wanderer");
            m_demoStep = 1;
        }
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
