#pragma once
// ---------------------------------------------------------------------------
// Engine.h
// Owns the application lifecycle: SDL3 window + OpenGL 3.3 core context,
// the main loop (variable delta time handed to every simulation system),
// the game state machine and all top-level subsystems.
//
// States:
//   Running  - exploring.
//   Paused   - P / focus loss; the simulation stops.
//   Terminal - seated at a computer: the camera leans into the screen,
//              keyboard input goes to the console, and the world keeps
//              running around the player (the Stalker loves this).
//   Caught   - an entity reached the player: jumpscare, blackout, and the
//              player wakes up somewhere else.
// ---------------------------------------------------------------------------

#include "AI/NoiseEvent.h"
#include "Core/Config.h"
#include "Core/Input.h"
#include "Render/Camera.h"
#include "Render/EntityRenderer.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ChunkManager;
class EntityDirector;
enum class EntityKind : uint8_t;
class Physics;
class Player;
class Renderer;
class Soundscape;
class Terminal;
class TerminalConsole;
class WorldGenerator;
struct TerminalContext;

/// Start-up options (parsed from the command line in Main.cpp).
struct EngineOptions {
    uint64_t    seed = cfg::kDefaultWorldSeed;
    int         startLevel = 0;          ///< Storey to spawn on.
    std::string screenshotPath;          ///< If set: capture a BMP after a delay, then exit.
    float       screenshotDelay = 3.0f;  ///< Seconds of simulation before the capture.
    /// Developer scene set up at start: stairs, stairs-top, stairs-sign, climb, descend,
    /// stalker, ambush, caught, wanderer, terminal, idle (only logs entity activity).
    std::string demo;
    std::string demoInput;               ///< Typed into the terminal in the "terminal" scene ("upper" in "stairs-sign").
    bool        noEntities = false;      ///< Disable the anomalies.
    std::string dumpSoundsDir;           ///< If set: write every synthesised sound there as WAV.
};

class Engine {
public:
    explicit Engine(EngineOptions options);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /// Creates the window, GL context and every subsystem. False on failure.
    bool init();

    /// Runs the main loop until the user quits. Returns the process exit code.
    int run();

private:
    enum class GameState { Running, Paused, Terminal, Caught };

    bool createWindow();
    void processEvents();
    void handleTerminalKey(const SDL_KeyboardEvent& key);
    void update(float dt);
    void render(float dt);
    void setState(GameState state);
    void updateTitle(float dt);
    bool saveScreenshot(const std::string& path) const;
    void shutdown();

    /// Loads everything around `feet` on `level` synchronously and moves the player there.
    void teleportPlayer(const glm::vec3& feet, int level, float yaw);
    /// Sticky storey tracking: changes only once the feet are well past the
    /// half-way height, so walking on a stair never thrashes chunk streaming.
    void updateFocusLevel();

    // ---- Terminals --------------------------------------------------------------------
    void enterTerminal(Terminal& terminal);
    void leaveTerminal(bool powerOff);
    void updateTerminal(float dt);
    TerminalContext terminalContext() const;
    Terminal* activeTerminal() const;

    // ---- Entities / noise -----------------------------------------------------------------
    void collectNoise();
    void startCaught(EntityKind by);
    void updateCaught(float dt);

    /// The camera actually rendered: the player's, blended into a terminal
    /// close-up, or wrenched towards whatever caught them.
    Camera viewCamera() const;
    void drawHud();
    void showMessage(const std::string& text, float seconds);

    // ---- Developer scenes ----------------------------------------------------------------
    void setupDemo();
    void driveAutopilot(Input& input);
    void updateDemo(float dt);

    EngineOptions m_options;
    Settings      m_settings;
    Input         m_input;
    Input         m_idleInput;  ///< No keys held: drives the body while seated / caught.
    GameState     m_state = GameState::Running;
    GameState     m_resumeState = GameState::Running; ///< State to return to after a pause.
    bool          m_quit = false;

    SDL_Window*   m_window = nullptr;
    SDL_GLContext m_context = nullptr;
    int           m_pixelWidth = cfg::kWindowWidth;
    int           m_pixelHeight = cfg::kWindowHeight;
    bool          m_fullscreen = false;

    std::unique_ptr<WorldGenerator> m_world;
    std::unique_ptr<ChunkManager>   m_chunks;
    std::unique_ptr<Physics>        m_physics;
    std::unique_ptr<Player>         m_player;
    std::unique_ptr<Renderer>       m_renderer;
    std::unique_ptr<Soundscape>     m_sound;
    std::unique_ptr<EntityDirector> m_entities;
    EntityDrawList                  m_entityDraw;
    std::vector<NoiseEvent>         m_noises;

    int    m_focusLevel = 0;          ///< Storey the world streams around.
    double m_simTime = 0.0;           ///< Simulation clock (drives flicker, grain).
    float  m_crosshairHighlight = 0.0f;
    std::string m_prompt;             ///< Interaction hint under the crosshair.

    // Terminal session.
    std::unordered_map<uint64_t, std::unique_ptr<TerminalConsole>> m_consoles;
    TerminalConsole* m_console = nullptr;
    uint64_t  m_terminalId = 0;
    uint64_t  m_lastTerminalId = 0;
    float     m_terminalBlend = 0.0f; ///< 0 = player view, 1 = leaning into the screen.
    glm::vec3 m_terminalEye{0.0f};
    float     m_terminalYaw = 0.0f;
    float     m_terminalPitch = 0.0f;

    // Caught sequence.
    glm::vec3 m_caughtFace{0.0f};     ///< What the camera is wrenched towards.
    float     m_caughtTimer = 0.0f;
    bool      m_respawned = false;
    float     m_fade = 0.0f;
    std::string m_message;
    float     m_messageTimer = 0.0f;

    // HUD / diagnostics.
    float  m_titleTimer = 0.0f;       ///< Time accumulated in the current FPS measurement window.
    int    m_frameCounter = 0;        ///< Frames rendered in the current window.
    float  m_fps = 0.0f;              ///< Last measured average FPS.
    bool   m_titleDirty = true;       ///< Window title needs rebuilding.
    bool   m_debugHud = false;        ///< F3: entity states.
    std::string m_fpsText = "-- FPS"; ///< On-screen FPS meter (refreshed twice a second).
    int    m_screenshotIndex = 0;
    bool   m_screenshotRequested = false;

    // Developer scenes.
    std::vector<glm::vec3> m_autopilot;  ///< Remaining waypoints of a scripted walk.
    size_t m_autopilotIndex = 0;
    float  m_demoTime = 0.0f;
    int    m_demoStep = 0;
    std::string m_lastEntityStates;
};
