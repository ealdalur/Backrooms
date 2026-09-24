#pragma once
// ---------------------------------------------------------------------------
// Engine.h
// Owns the application lifecycle: SDL3 window + OpenGL 3.3 core context,
// the main loop (variable delta time handed to every simulation system),
// the game state machine (running / paused) and all top-level subsystems.
// ---------------------------------------------------------------------------

#include "Core/Config.h"
#include "Core/Input.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>

class ChunkManager;
class Physics;
class Player;
class Renderer;
class Soundscape;
class WorldGenerator;

/// Start-up options (parsed from the command line in Main.cpp).
struct EngineOptions {
    uint64_t    seed = cfg::kDefaultWorldSeed;
    std::string screenshotPath;          ///< If set: capture a BMP after a delay, then exit.
    float       screenshotDelay = 3.0f;  ///< Seconds of simulation before the capture.
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
    enum class GameState { Running, Paused };

    bool createWindow();
    void processEvents();
    void update(float dt);
    void render();
    void setState(GameState state);
    void updateTitle(float dt);
    bool saveScreenshot(const std::string& path) const;
    void shutdown();

    EngineOptions m_options;
    Settings      m_settings;
    Input         m_input;
    GameState     m_state = GameState::Running;
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

    double m_simTime = 0.0;           ///< Simulation clock (drives flicker, grain).
    float  m_crosshairHighlight = 0.0f;
    float  m_titleTimer = 0.0f;       ///< Time accumulated in the current FPS measurement window.
    int    m_frameCounter = 0;        ///< Frames rendered in the current window.
    float  m_fps = 0.0f;              ///< Last measured average FPS.
    bool   m_titleDirty = true;       ///< Window title needs rebuilding.
    std::string m_fpsText = "-- FPS"; ///< On-screen FPS meter (refreshed twice a second).
    int    m_screenshotIndex = 0;
    bool   m_screenshotRequested = false;
};
