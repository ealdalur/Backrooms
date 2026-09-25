// ---------------------------------------------------------------------------
// Engine.cpp
// ---------------------------------------------------------------------------
#include "glad.h" // must precede any other OpenGL header

#include "Core/Engine.h"

#include "Actors/Player.h"
#include "Audio/Soundscape.h"
#include "Core/GpuSelection.h"
#include "Physics/Physics.h"
#include "Render/Renderer.h"
#include "World/ChunkManager.h"
#include "World/WorldConstants.h"
#include "World/WorldGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <utility>
#include <vector>

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

    m_world = std::make_unique<WorldGenerator>(m_options.seed);
    m_chunks = std::make_unique<ChunkManager>(*m_world, cfg::kChunkLoadRadius, cfg::kChunkBuildBudget);
    m_physics = std::make_unique<Physics>(0.0f, world::kCeilingHeight, cfg::kStepHeight);

    // Load the neighbourhood of the origin, find a free spot and spawn there.
    const BodyShape standing{cfg::kPlayerHalfWidth, cfg::kStandHeight};
    const glm::vec3 probe(world::kCellSize * 2.5f, 0.0f, world::kCellSize * 2.5f);
    m_chunks->update(probe, 0.0f, Physics::bodyBox(probe, standing), true);
    const glm::vec3 spawn = m_chunks->findSpawnPoint(standing, *m_physics);
    m_chunks->update(spawn, 0.0f, Physics::bodyBox(spawn, standing), true);
    m_player = std::make_unique<Player>(spawn, 0.0f);

    std::cout << "[Engine] World seed 0x" << std::hex << m_options.seed << std::dec << ", " << m_chunks->chunkCount()
              << " chunks loaded, spawn (" << spawn.x << ", " << spawn.z << ")\n"
              << "[Engine] Controls: WASD move, Shift run, Space jump, C crouch, E use door,\n"
              << "         hold RMB + move mouse to drive, +/- sensitivity, F11 fullscreen,\n"
              << "         F12 screenshot, P pause/resume, Esc quit.\n";

    setState(GameState::Running);
    return true;
}

void Engine::setState(GameState state) {
    m_state = state;
    const bool running = state == GameState::Running;
    SDL_SetWindowRelativeMouseMode(m_window, running);
    if (m_sound) m_sound->setPaused(!running);
    m_input.reset();
    m_titleDirty = true; // refresh the title immediately
}

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
            if (m_state == GameState::Running && m_options.screenshotPath.empty()) setState(GameState::Paused);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (m_state == GameState::Paused && e.button.button == SDL_BUTTON_LEFT) setState(GameState::Running);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.repeat) break;
            switch (e.key.scancode) {
            case SDL_SCANCODE_ESCAPE:
                m_quit = true; // exit immediately
                break;
            case SDL_SCANCODE_P:
                setState(m_state == GameState::Running ? GameState::Paused : GameState::Running);
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

void Engine::update(float dt) {
    if (m_state != GameState::Running) return;
    m_simTime += dt;

    // Doors: interact with the one in front of the player.
    if (m_input.keyPressed(SDL_SCANCODE_E)) {
        m_chunks->interact(m_player->eyePosition(), m_player->lookDirection(), m_player->feetPosition());
    }

    m_player->update(dt, m_input, m_settings, *m_chunks, *m_physics);
    m_chunks->update(m_player->feetPosition(), dt, m_player->bodyBox());
    // After both updates so this frame's footstep / door events are heard, and
    // with the renderer's clock so the tube buzz coincides with visible flicker.
    m_sound->update(dt, m_simTime, *m_player, *m_chunks, *m_world);

    // Crosshair ring fades in when a door is within reach.
    const bool canUse = m_chunks->findInteractableDoor(m_player->eyePosition(), m_player->lookDirection()) != nullptr;
    m_crosshairHighlight += ((canUse ? 1.0f : 0.0f) - m_crosshairHighlight) * (1.0f - std::exp(-12.0f * dt));
}

void Engine::render() {
    m_renderer->render(m_player->camera(), *m_chunks, *m_world, m_simTime, m_crosshairHighlight);
    m_renderer->drawHudText(m_fpsText); // drawn in-frame, so it is visible in fullscreen too
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
    const ChunkCoord c = ChunkCoord::fromWorld(p.x, p.z);
    const RenderStats& s = m_renderer->stats();
    char title[256];
    std::snprintf(title, sizeof(title),
                  "%s | %.0f FPS | chunk (%d, %d) | %zu chunks, %zu lights | sensitivity %.2f%s%s", cfg::kWindowTitle,
                  m_fps, c.x, c.z, m_chunks->chunkCount(), s.lights, m_settings.mouseSensitivity,
                  m_player->mouseDriveActive() ? " | MOUSE DRIVE" : "",
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
        render();

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
