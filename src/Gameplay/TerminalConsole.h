#pragma once
// ---------------------------------------------------------------------------
// TerminalConsole.h
// The interactive session on one retro terminal.
//
// Output model: every line of text is queued and "typed" onto the screen at
// its own speed (system output bursts out instantly, the trapped voice types
// slowly, hesitating). While idle the console streams endless system output;
// interspersed within it - more often the longer the player stays - the
// anomaly injects distressed messages, with a screen glitch and a sound.
// It also reacts to the world: if the Stalker creeps up behind the player,
// or the Wanderer is close enough to hear typing, the voice warns them.
//
// Input model: a prompt line with editing and history. Submitted text is
// matched against a registry of commands (name, aliases, help text and a
// handler); anything else may be answered by the voice.
// ---------------------------------------------------------------------------

#include "Gameplay/TerminalText.h"
#include "Math/Random.h"
#include "Render/TerminalRenderer.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <deque>
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
};

/// Sounds a console asks for (played at the terminal). The player's own key
/// clicks are also noise the Wanderer can hear; the ghost's are not.
enum class TerminalSound : uint8_t { Key, GhostKey, Beep, Glitch, Boot, PowerDown };

class TerminalConsole {
public:
    TerminalConsole(uint64_t terminalId, bool amber, int level);

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

    /// The session wants to end (EXIT / SHUTDOWN); SHUTDOWN also powers off.
    bool exitRequested() const { return m_exitRequested; }
    bool powerOffRequested() const { return m_powerOffRequested; }
    void clearRequests() { m_exitRequested = m_powerOffRequested = false; }

    const TerminalScreen& screen() const { return m_screen; }
    uint64_t terminalId() const { return m_id; }

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
    bool  m_streaming = true;
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
};
