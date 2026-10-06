#pragma once
// ---------------------------------------------------------------------------
// Engine.h
// Owns the application lifecycle: SDL3 window + OpenGL 3.3 core context,
// the main loop (variable delta time handed to every simulation system),
// the game state machine and all top-level subsystems.
//
// States:
//   Title    - the start: "BACKROOMS" strikes on in fluorescent tubes over
//              the start point (Render/TitleRenderer), holds, and fades away.
//              The rooms go on behind it - the lights flicker, the hum plays -
//              but the player's controls, the entities and the timers that set
//              things off (the phones ringing) wait until it has gone.
//   Running  - exploring.
//   Paused   - P / focus loss; the simulation stops behind a blurred,
//              darkened screen reading "Paused" (P or a click resumes).
//   Terminal - seated at a computer: the camera leans into the screen,
//              keyboard input goes to the console, and the world keeps
//              running around the player (the Stalker loves this).
//   Phone    - holding a desk phone's handset: the camera leans over its
//              keypad, a mouse pointer appears to press the keys and the
//              message lamp (the digit keys and M work too), and the
//              earpiece plays the line.
//   Cabinet  - searching a filing cabinet: the camera leans over the open
//              drawer, W / S (or the wheel) roll another drawer out, E (or a
//              click) takes - or swaps for - the part inside; with nothing in
//              the drawer to take, E closes the cabinet, as Esc always does.
//              Head down in a drawer, the player sees nothing else.
//
// Running, the player scavenges the four parts of the Tesla coil gun (from
// desks, chairs and cabinet drawers; one of each type can be carried and a
// duplicate is swapped in place), puts them together with R, and fires with
// the left button or F (see Core/EngineTesla.cpp).
//   Caught   - an entity reached the player: jumpscare, blackout, and the
//              player wakes up somewhere else.
//   QuitPrompt - Esc pressed where it would quit the game: everything holds
//              still behind a blurred, darkened screen asking "Are you sure
//              you want to quit the game?" - Esc again quits, any other key
//              (or a click) carries on where the player was.
//   Noclip   - the player walked into the glitch room's wall: the picture
//              tears apart, the camera slides through the wall, everything
//              goes black - and fades back in on a cubicle in an office
//              (Core/EnginePuzzle.cpp). Then they are Running again, there.
//
// The puzzle chain (Gameplay/PuzzleChain) runs through it all: the phone
// number on a wall, the call, the memory at 7A9F on a terminal's hex stream,
// the ping and its offset, the glitch room on the MAP, the way out. The
// Engine carries the chain's answers to the world generator (the clue on the
// walls, the exit chunk), the phones (the clue's number) and the terminals,
// and moves it on when they report what the player found.
// ---------------------------------------------------------------------------

#include "AI/NoiseEvent.h"
#include "Actors/TeslaParts.h"
#include "Core/Config.h"
#include "Core/Input.h"
#include "Gameplay/Inventory.h"
#include "Render/Camera.h"
#include "Render/EntityRenderer.h"
#include "World/ChunkCoord.h"
#include "World/WorldConstants.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ChunkManager;
class EntityDirector;
class PuzzleChain;
struct ArcLight;
struct PuzzleEvent;
enum class PuzzleStage : uint8_t;
enum class EntityKind : uint8_t;
class FileCabinet;
class Phone;
class PhoneCall;
class Physics;
class Player;
class Renderer;
class Soundscape;
class Terminal;
class TerminalConsole;
class TeslaGun;
class WorldGenerator;
struct ItemSite;
struct TerminalContext;
namespace doom { struct Controls; }

/// Start-up options (parsed from the command line in Main.cpp).
struct EngineOptions {
    uint64_t    seed = cfg::kDefaultWorldSeed;
    int         startLevel = 0;          ///< Storey to spawn on.
    std::string screenshotPath;          ///< If set: capture a BMP after a delay, then exit.
    float       screenshotDelay = 3.0f;  ///< Seconds of simulation before the capture.
    /// Developer scene set up at start: stairs, stairs-top, stairs-sign, climb, descend (--type door:
    /// the player opens the shut entrance from outside first),
    /// stalker, ambush, caught, wanderer, terminal, doom (DOOM on the nearest terminal,
    /// scripted play), phone (picks up the nearest phone), explore (a long scripted walk,
    /// Stalker off; --type <n> picks the route), idle (only logs entity activity),
    /// cabinet (searches the nearest cabinet holding a part; --type take: and takes it;
    /// --type swap-persist: swaps a nearly flat part of the same type in, walks away until
    /// the chunk unloads, comes back and checks the drawer; --type use-key: presses E to take
    /// the part, then again to close),
    /// part (walks up to the nearest part lying on a desk or chair),
    /// stairs-chase (climbs the nearest stairwell, shutting its doors behind; the Stalker -
    /// or with --type wanderer the Wanderer - has to follow on foot; --type wanderer-far: out of
    /// earshot, so only the fallback brings it), stalker-door (the
    /// Stalker hunts the player from behind a closed door), stalker-stare (the player keeps
    /// turning to face the Stalker: stared down three times, it bolts far away),
    /// quit-prompt (P, P, Esc, another key, Esc, a held Esc, Esc: checks the pause screen and the quit prompt),
    /// assemble (all four parts, put together), tesla / tesla-stalker (the gun,
    /// fired at the Wanderer / the Stalker; --type <percent> sets the battery).
    /// Developer scenes for the way out: clue (in front of the nearest phone
    /// number on a wall), hexstream (the call already made: sits at a terminal
    /// streaming HEX and reports when 7A9F comes up), exit-map (the nearest
    /// terminal's chunk becomes the exit; MAP), glitch (the current chunk
    /// becomes the exit; stands at the glitch room - --type walk: walks into
    /// its wall), office (straight to the office), ringer (a nearby phone rings),
    /// clue-survey (rooms of exploring between copies of the number; --type <routes>),
    /// terminal-pause (SPACE on a terminal's log, ESC, sit down again, SPACE: the stream must come back).
    std::string demo;
    std::string demoInput;               ///< Typed into the terminal in the "terminal" scene, dialled in "phone" ('h' hangs up, 'M' messages; "upper" in "stairs-sign").
    bool        noEntities = false;      ///< Disable the anomalies.
    bool        noTitle = false;         ///< Straight into the game, without the title (always so in a developer scene).
    bool        verbose = false;         ///< Developer log: also print what gives the game's secrets away.
    std::string puzzleStage;             ///< Start the puzzle chain this far along: dialed, memory, mapped.
    int         windowWidth = cfg::kWindowWidth;   ///< Initial window size (logical pixels).
    int         windowHeight = cfg::kWindowHeight;
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
    enum class GameState { Title, Running, Paused, Terminal, Phone, Cabinet, Caught, QuitPrompt, Noclip };

    /// A point of a scripted walk. `door` marks the approach to a door that
    /// has to be opened first (the edge it hangs in follows).
    struct AutopilotPoint {
        glm::vec3       pos;
        bool            door = false;
        int             level = 0, gx = 0, gz = 0;
        world::EdgeAxis axis = world::EdgeAxis::West;
    };

    bool createWindow();
    void processEvents();
    void handleTerminalKey(const SDL_KeyboardEvent& key);
    void update(float dt);
    /// Under the title: only the rooms' own life goes on; then the game starts.
    void updateTitle(float dt);
    void render(float dt);
    void setState(GameState state);
    /// Measures the frame rate for the HUD's meter.
    void updateFps(float dt);
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

    // ---- Phones -----------------------------------------------------------------------
    void enterPhone(Phone& phone);
    void leavePhone();
    void updatePhone(float dt);
    void handlePhoneKey(const SDL_KeyboardEvent& key);
    Phone* activePhone() const;
    /// The key (or Phone::kLampButton) of the phone in use under a window position (logical pixels), or -1.
    int phoneKeyAt(float windowX, float windowY) const;

    // ---- Filing cabinets (Core/EngineTesla.cpp) ----------------------------------------------
    void enterCabinet(FileCabinet& cabinet, int drawer);
    void leaveCabinet();
    void updateCabinet(float dt);
    void handleCabinetKey(const SDL_KeyboardEvent& key);
    FileCabinet* activeCabinet() const;
    /// The part in the open drawer of the cabinet being searched, if any.
    ItemSite* cabinetSite() const;

    // ---- Parts and the Tesla gun (Core/EngineTesla.cpp) --------------------------------------
    /// Takes the part at a site (swapping in the held one of its type).
    void takeItem(ItemSite& site);
    /// Starts putting the gun together (all four parts held). False if it cannot.
    bool beginAssembly();
    /// "TAKE BATTERY PACK 74%" / "SWAP ..." for a site's part.
    std::string itemPrompt(const ItemSite& site) const;
    /// Assembly, raising / lowering the gun, firing, and their sounds and noise.
    void updateGun(float dt);
    /// Gun space -> world, in the player's hands for camera `cam` (raised,
    /// dipped for a swap, shaking while it fires).
    glm::mat4 gunTransform(const Camera& cam) const;
    void buildViewModel(const Camera& cam);
    void drawInventory();

    // ---- The puzzle chain and the way out (Core/EnginePuzzle.cpp) -------------------------------
    /// Moves the chain on (and says so in the log).
    void advancePuzzle(PuzzleStage stage, const char* why);
    void handlePuzzleEvents(const std::vector<PuzzleEvent>& events);
    /// Makes `exit` the exit chunk (the generator rebuilds it with the glitch room).
    void armExit(const ChunkCoord& exit);
    /// Walking into a glitching wall starts the noclip.
    void checkNoclip();
    void startNoclip();
    void updateNoclip(float dt);
    /// Black screen: swaps the Backrooms for the office and puts the player in a cubicle.
    void enterOffice();
    /// Now and then a phone near the player rings (picked up, someone is on the line).
    void updateRinger(float dt);
    void stopRinging();
    /// The red light a ringing phone's flashing lamp throws on the desk around it (off when none rings).
    ArcLight ringLight() const;
    /// The glitch room crackles and hisses while the player is near it.
    void updateGlitchHum(float dt);
    /// A line of speech captioned at the bottom of the screen.
    void say(const std::string& line, float seconds);
    void setupPuzzleDemo();
    void updatePuzzleDemo();

    // ---- Entities / noise -----------------------------------------------------------------
    void collectNoise();
    void startCaught(EntityKind by);
    void updateCaught(float dt);

    /// The camera actually rendered: the player's, blended into a terminal or
    /// phone close-up, or wrenched towards whatever caught them.
    Camera viewCamera() const;
    void drawHud();
    void showMessage(const std::string& text, float seconds);

    // ---- Developer scenes ----------------------------------------------------------------
    void setupDemo();
    void driveAutopilot(Input& input);
    /// A random walk of `steps` cells from `from`, preferring unvisited rooms.
    std::vector<AutopilotPoint> exploreRoute(const glm::vec3& from, int level, int steps, uint64_t seed) const;
    void updateDemo(float dt);
    /// Scripted DOOM input for the "doom" scene.
    doom::Controls demoDoomControls() const;
    /// The "quit-prompt" scene: real key events, pushed from the main loop
    /// (the simulation - and with it the other scenes' scripting - holds still under the prompt).
    void driveQuitPromptDemo(double runTime);

    EngineOptions m_options;
    Settings      m_settings;
    Input         m_input;
    Input         m_idleInput;  ///< No keys held: drives the body while seated / caught.
    GameState     m_state = GameState::Running;
    GameState     m_resumeState = GameState::Running; ///< State to return to after a pause.
    GameState     m_quitResume = GameState::Running;  ///< State to return to if the player does not quit.
    float         m_dialogFade = 0.0f;                ///< 0..1: the pause screen / quit prompt (and the blur behind it) fading in.
    GameState     m_dialog = GameState::Paused;       ///< Which of the two is (or was last) shown.
    float         m_titleTime = 0.0f;                 ///< Seconds the title has been up.
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
    std::unique_ptr<PuzzleChain>    m_puzzle;
    bool                            m_office = false; ///< Escaped: the world is the office now.
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
    float     m_doomNoiseTimer = 0.0f; ///< Until DOOM's music next reaches the Backrooms' ears.

    // Phone in hand.
    std::unique_ptr<PhoneCall> m_call;
    uint64_t    m_phoneId = 0;
    float       m_phoneBlend = 0.0f;   ///< 0 = player view, 1 = leaning over the keypad.
    glm::vec3   m_phoneEye{0.0f};
    float       m_phoneYaw = 0.0f;
    float       m_phonePitch = 0.0f;
    std::string m_phoneKeys;           ///< Keys pressed since the last update (Phone::kMessageChar: the lamp).
    bool        m_phoneHangUp = false; ///< Hang up at the next update.
    int         m_phoneHover = -1;     ///< Key under the mouse pointer.
    int         m_phonePickups = 0;
    float       m_phoneNoiseTimer = 0.0f; ///< Until the howler next carries through the Backrooms.
    SDL_Cursor* m_pointerCursor = nullptr; ///< Hand shown over a key.

    // Filing cabinet being searched.
    uint64_t  m_cabinetId = 0;
    int       m_cabinetDrawer = 2;     ///< Drawer rolled out (0 = bottom).
    float     m_cabinetBlend = 0.0f;   ///< 0 = player view, 1 = leaning over the drawer.
    glm::vec3 m_cabinetEye{0.0f};      ///< Current close-up (glides between drawers).
    float     m_cabinetYaw = 0.0f;
    float     m_cabinetPitch = 0.0f;
    int       m_cabinetMove = 0;       ///< Drawers to move up (+) / down (-) at the next update.
    bool      m_cabinetUse = false;    ///< Take the part at the next update (waits while the drawer rolls out).
    bool      m_cabinetUseCloses = false; ///< ...and that press was E: with no part to take, it closes the cabinet.
    bool      m_cabinetLeave = false;  ///< Close it at the next update.

    // The Tesla gun.
    Inventory                  m_inventory;
    std::unique_ptr<TeslaGun>  m_gun;
    float  m_assembleTimer = -1.0f;    ///< >= 0 while the parts are being put together.
    int    m_assembleStep = 0;         ///< Parts seated so far.
    float  m_swapDip = 0.0f;           ///< > 0 while the gun dips out of view for a hot swap.
    float  m_gunRaise = 0.0f;          ///< 0 = lowered out of view, 1 = in the hands.
    bool   m_demoTrigger = false;      ///< Developer scenes: the trigger held.
    std::vector<ViewModelPart> m_viewModel;

    // Caught sequence.
    glm::vec3 m_caughtFace{0.0f};     ///< What the camera is wrenched towards.
    float     m_caughtTimer = 0.0f;
    bool      m_respawned = false;
    float     m_fade = 0.0f;
    std::string m_message;
    float     m_messageTimer = 0.0f;

    // The way out.
    float       m_noclipTimer = 0.0f;
    bool        m_noclipSwitched = false; ///< The office is loaded (behind the black).
    bool        m_monologueStarted = false;
    glm::vec3   m_noclipDir{0.0f};        ///< The way through the wall.
    float       m_glitch = 0.0f;          ///< 0..1, the picture tearing apart.
    float       m_glitchHumTimer = 0.0f;
    std::string m_dialogue;               ///< Speech captioned at the bottom of the screen...
    float       m_dialogueTimer = 0.0f;   ///< ...for this much longer.
    uint64_t    m_ringingPhone = 0;       ///< The phone ringing, if any...
    float       m_ringTime = 0.0f;        ///< ...for how long.
    float       m_nextRing = 0.0f;        ///< Until a phone next rings.

    // HUD / diagnostics.
    float  m_fpsTimer = 0.0f;         ///< Time accumulated in the current FPS measurement window.
    int    m_frameCounter = 0;        ///< Frames rendered in the current window.
    float  m_fps = 0.0f;              ///< Last measured average FPS.
    float  m_sensitivityTimer = 0.0f; ///< > 0: the mouse sensitivity was just changed (shown on the HUD).
    bool   m_debugHud = false;        ///< F3: entity states, streaming, the puzzle chain.
    std::string m_fpsText = "-- FPS"; ///< On-screen FPS meter (refreshed twice a second).
    int    m_screenshotIndex = 0;
    bool   m_screenshotRequested = false;

    // Developer scenes.
    std::vector<AutopilotPoint> m_autopilot; ///< Waypoints of a scripted walk.
    size_t m_autopilotIndex = 0;
    float  m_autopilotStuck = 0.0f;          ///< Seconds the scripted walk has made no progress.
    float  m_autopilotWait = 0.0f;           ///< Seconds spent waiting for a door to swing open.
    float  m_autopilotDetour = 0.0f;         ///< Seconds left of a sidestep round an obstacle.
    float  m_autopilotSide = 1.0f;           ///< Alternates the sidestep direction.
    int    m_autopilotTries = 0;             ///< Sidesteps tried for the current waypoint.
    float  m_lastDt = 0.0f;
    float  m_demoTime = 0.0f;
    int    m_demoStep = 0;
    float  m_demoWait = 0.0f;                ///< When a scene's next step is due.
    std::string m_lastEntityStates;
    std::string m_lastPhoneLog;              ///< Last logged line state ("phone" scene).
    /// A stairwell entrance door in the "stairs-chase" scene (shut behind the player on each storey).
    struct ChaseDoor {
        int             level = 0, gx = 0, gz = 0;
        world::EdgeAxis axis = world::EdgeAxis::West;
        bool            shut = false;
    };
    ChaseDoor   m_chaseDoors[2];
    glm::vec3   m_chaseWay{0.0f};            ///< Where the way goes past the door the "climb --type door" walk opens.
    EntityKind  m_chaser{};                  ///< Who follows the player in the "stairs-chase" / "stalker-door" scenes.
    uint64_t    m_demoCabinet = 0;           ///< Cabinet searched in the "cabinet" scene...
    int         m_demoDrawer = 0;            ///< ...its drawer...
    glm::vec3   m_demoReturn{0.0f};          ///< ...and where the player stood.
};
