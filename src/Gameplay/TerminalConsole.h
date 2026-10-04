#pragma once
// ---------------------------------------------------------------------------
// TerminalConsole.h
// The interactive session on one retro terminal.
//
// Two views:
//   * The live log (what a terminal shows when the player sits down): an
//     endless stream of system output. Woven into it - more often the longer
//     the player stays - the anomaly injects distressed messages, with a
//     screen glitch and a sound.
//   * The command prompt (Enter, or simply start typing): the log is
//     suspended so command output is never interrupted, but the voice still
//     speaks up now and then. STREAM goes back to the live log.
// Every line is queued and "typed" onto the screen at its own speed (system
// output bursts out, the trapped voice types slowly, hesitating). The console
// also reacts to the world: if the Stalker creeps up behind the player, or
// the Wanderer is close enough to hear typing, the voice warns them.
//
// Input model: a prompt line with editing and history. Submitted text is
// matched against a registry of commands (name, aliases, help text and a
// handler); anything else may be answered by the voice. Hidden commands are
// left out of HELP.
//
// Graphics mode: the hidden DOOM command boots a game (Gameplay/Doom) that
// takes over the tube and the keyboard. While it runs, graphics() returns its
// framebuffer (drawn instead of the text grid), the Engine feeds it held keys
// through TerminalContext::doom and plays its sounds, and ESC quits back to
// the prompt. The console keeps running underneath: the voice still warns
// and whispers - its lines break into the game's HUD message line.
// ---------------------------------------------------------------------------

#include "Gameplay/Doom/DoomGame.h"
#include "Gameplay/TerminalText.h"
#include "Math/Random.h"
#include "Render/TerminalRenderer.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

class WorldGenerator;

/// What a console may know about the world outside the screen.
struct TerminalContext {
    int       level = 0;
    glm::vec3 playerFeet{0.0f};
    float     stalkerDistance = -1.0f;  ///< < 0 when the Stalker is not around.
    bool      stalkerBehind = false;    ///< Close, and outside the player's view.
    float     wandererDistance = -1.0f; ///< < 0 when the Wanderer is not around.
    const WorldGenerator* world = nullptr;
    /// Grid direction (cells) the player faces sitting at this screen: up - "north" - on the map.
    glm::ivec2 mapUp{0, -1};
    doom::Controls doom;                ///< Held keys, while DOOM runs.
};

/// Sounds a console asks for (played at the terminal). The player's own key
/// clicks are also noise the Wanderer can hear; the ghost's are not.
enum class TerminalSound : uint8_t { Key, GhostKey, Beep, Glitch, Boot, PowerDown };

class TerminalConsole {
public:
    TerminalConsole(uint64_t terminalId, bool amber, int level);
    ~TerminalConsole();

    /// The player sits down. An unpowered terminal cold-boots first.
    void open(bool powered);

    /// Advances typing, streaming, anomalies and the power state.
    void update(float dt, const TerminalContext& ctx);

    // ---- Keyboard ---------------------------------------------------------------
    void type(const char* text);      ///< Printable ASCII is appended to the prompt.
    void backspace();
    void submit(const TerminalContext& ctx);
    void historyUp();
    void historyDown();
    /// ESC: quits a running program (true), or does nothing (false: leave the terminal).
    bool escape();
    /// A key struck while a program owns the keyboard: it still clatters (and can be heard).
    void keyClick();

    /// The session wants to end (EXIT / SHUTDOWN); SHUTDOWN also powers off.
    bool exitRequested() const { return m_exitRequested; }
    bool powerOffRequested() const { return m_powerOffRequested; }
    void clearRequests() { m_exitRequested = m_powerOffRequested = false; }

    const TerminalScreen& screen() const { return m_screen; }
    /// True at the command prompt, false while watching the live log.
    bool commandMode() const { return m_commandMode; }
    uint64_t terminalId() const { return m_id; }

    /// The game, while DOOM is running (for its sounds), else null.
    doom::Game* doom() { return m_doom.get(); }
    bool doomActive() const { return m_doom != nullptr || m_doomBooting; }
    /// The picture to show instead of the text grid, if a program is in graphics mode.
    const TerminalGraphics* graphics() const { return m_doom ? &m_doom->frame() : nullptr; }

    /// Returns and clears the sounds requested since the last call.
    std::vector<TerminalSound> takeSounds();

private:
    using Args = std::vector<std::string>;
    using Handler = void (TerminalConsole::*)(const Args& args, const TerminalContext& ctx);
    struct Command {
        const char* name;
        const char* aliases; ///< Space-separated alternative names ("" for none).
        const char* usage;
        const char* help;
        Handler     handler;
        bool        hidden = false; ///< Not listed by HELP.
    };
    /// The command registry. Adding a command = one entry + one handler.
    static const std::vector<Command>& commands();
    static const Command* findCommand(const std::string& name);

    struct Line {
        std::string text;
        uint8_t     color;
    };
    struct Pending {
        std::string text;
        uint8_t     color = TerminalScreen::Normal;
        float       charsPerSecond = 0.0f; ///< <= 0: appears at once.
        float       delay = 0.0f;          ///< Seconds before it starts.
        bool        anomaly = false;       ///< Glitch + sound when it starts.
    };

    // ---- Output -------------------------------------------------------------------
    void print(const std::string& text, uint8_t color = TerminalScreen::Normal, float cps = 0.0f, float delay = 0.0f);
    void printAnomaly(const std::string& text, float delay, bool urgent = false);
    void advanceTyping(float dt);
    void streamBurst(const TerminalContext& ctx);
    /// Switches from the live log to a clean prompt.
    void enterCommandMode();
    void compose();
    void sound(TerminalSound s) { m_sounds.push_back(s); }

    // ---- Commands -----------------------------------------------------------------
    void cmdHelp(const Args&, const TerminalContext&);
    void cmdClear(const Args&, const TerminalContext&);
    void cmdDiag(const Args&, const TerminalContext&);
    void cmdStream(const Args&, const TerminalContext&);
    void cmdMode(const Args&, const TerminalContext&);
    void cmdStatus(const Args&, const TerminalContext&);
    void cmdWhoami(const Args&, const TerminalContext&);
    void cmdPing(const Args&, const TerminalContext&);
    void cmdMap(const Args&, const TerminalContext&);
    void cmdEcho(const Args&, const TerminalContext&);
    void cmdReboot(const Args&, const TerminalContext&);
    void cmdShutdown(const Args&, const TerminalContext&);
    void cmdExit(const Args&, const TerminalContext&);
    void cmdDoom(const Args&, const TerminalContext&);
    void cmdIddqd(const Args&, const TerminalContext&);

    uint64_t m_id;
    int      m_level;
    rnd::Rng m_rng;

    std::deque<Line>    m_lines;     ///< Scrollback; the last line may be mid-typing.
    std::deque<Pending> m_queue;
    bool   m_typingLine = false;     ///< m_lines.back() is still being typed.
    size_t m_typedChars = 0;
    float  m_typeAccum = 0.0f;
    float  m_delay = 0.0f;

    std::string              m_input;
    std::vector<std::string> m_history;
    int                      m_historyPos = 0;

    bool  m_ready = false;           ///< Booted and accepting commands.
    bool  m_commandMode = false;    ///< At the prompt (live log suspended) rather than watching the log.
    float m_anomalyTimer = 0.0f;    ///< At the prompt: countdown to the voice's next message.
    termtext::StreamMode m_mode = termtext::StreamMode::All;
    float m_streamTimer = 0.0f;
    double m_uptime = 0.0;           ///< Seconds since boot (kernel timestamps).
    float m_session = 0.0f;          ///< Seconds the player has spent here this visit.
    float m_warnCooldown = 0.0f;
    float m_hushCooldown = 0.0f;
    float m_powerOffTimer = -1.0f;   ///< Counting down to power-off after SHUTDOWN.
    float m_targetBrightness = 1.0f;
    bool  m_exitRequested = false;
    bool  m_powerOffRequested = false;

    TerminalScreen             m_screen;
    std::vector<TerminalSound> m_sounds;

    std::unique_ptr<doom::Game> m_doom;
    bool m_doomBooting = false;      ///< DOOM's start-up text is still printing.
};
