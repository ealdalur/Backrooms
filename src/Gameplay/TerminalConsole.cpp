// ---------------------------------------------------------------------------
// TerminalConsole.cpp
// ---------------------------------------------------------------------------
#include "Gameplay/TerminalConsole.h"

#include "World/WorldConstants.h"
#include "World/WorldGenerator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace {

constexpr size_t kMaxScrollback = 200;
constexpr int    kOutputRows = TerminalScreen::kRows - 1; ///< The last row is the prompt.
constexpr float  kDesperationTime = 240.0f;               ///< Seconds until the voice is at its worst.

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string toUpper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

bool hasUpperCase(const std::string& s) {
    return std::any_of(s.begin(), s.end(), [](char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; });
}

} // namespace

TerminalConsole::TerminalConsole(uint64_t terminalId, bool amber, int level)
    : m_id(terminalId), m_level(level), m_rng(rnd::hashCombine(terminalId, 0x7E4Dull)) {
    m_screen.amber = amber;
    m_screen.brightness = 0.0f;
}

// ---- Command registry ----------------------------------------------------------------

const std::vector<TerminalConsole::Command>& TerminalConsole::commands() {
    static const std::vector<Command> kCommands = {
        {"help",     "? commands", "HELP",              "LIST COMMANDS",                       &TerminalConsole::cmdHelp},
        {"clear",    "cls",        "CLEAR",             "CLEAR THE SCREEN",                    &TerminalConsole::cmdClear},
        {"diag",     "test",       "DIAG",              "RUN HARDWARE DIAGNOSTICS",            &TerminalConsole::cmdDiag},
        {"stream",   "logs log monitor", "STREAM",      "BACK TO THE LIVE SYSTEM LOG",         &TerminalConsole::cmdStream},
        {"mode",     "",           "MODE <SOURCE>",     "LOG: ALL KERNEL DIAG NET HEX",        &TerminalConsole::cmdMode},
        {"status",   "info",       "STATUS",            "NODE STATUS",                         &TerminalConsole::cmdStatus},
        {"whoami",   "",           "WHOAMI",            "SHOW CURRENT USER",                   &TerminalConsole::cmdWhoami},
        {"ping",     "",           "PING <HOST>",       "TEST A NETWORK NODE",                 &TerminalConsole::cmdPing},
        {"map",      "",           "MAP",               "FLOOR PLAN AROUND YOU",               &TerminalConsole::cmdMap},
        {"echo",     "print",      "ECHO <TEXT>",       "PRINT TEXT",                          &TerminalConsole::cmdEcho},
        {"reboot",   "restart",    "REBOOT",            "RESTART THE SYSTEM",                  &TerminalConsole::cmdReboot},
        {"shutdown", "poweroff",   "SHUTDOWN",          "POWER OFF AND LEAVE",                 &TerminalConsole::cmdShutdown},
        {"exit",     "logout quit bye", "EXIT",         "LEAVE THE TERMINAL (ALSO: ESC)",      &TerminalConsole::cmdExit},
    };
    return kCommands;
}

const TerminalConsole::Command* TerminalConsole::findCommand(const std::string& name) {
    for (const Command& c : commands()) {
        if (name == c.name) return &c;
        for (const std::string& alias : split(c.aliases)) {
            if (name == alias) return &c;
        }
    }
    return nullptr;
}

// ---- Session ------------------------------------------------------------------------------

void TerminalConsole::open(bool powered) {
    m_session = 0.0f;
    m_exitRequested = m_powerOffRequested = false;
    m_powerOffTimer = -1.0f;
    m_targetBrightness = 1.0f;
    m_input.clear();
    if (!powered) {
        cmdReboot({}, TerminalContext{});
        return;
    }
    if (m_lines.empty() && m_queue.empty()) {
        // Already running when the player arrives: straight into the monitor.
        m_screen.brightness = 1.0f;
        m_uptime = 3600.0 + m_rng.range(0.0f, 900000.0f);
        char header[80];
        std::snprintf(header, sizeof(header), "FACILITY MONITOR 2.3 - NODE %04X - LEVEL %d",
                      static_cast<unsigned>(m_id & 0xFFFF), m_level);
        print(header, TerminalScreen::Bright);
        print("PRESS ENTER FOR A COMMAND PROMPT.", TerminalScreen::Dim);
        print("");
    } else {
        print("", TerminalScreen::Normal);
        print("SESSION RESUMED.", TerminalScreen::Dim);
    }
    m_ready = false; // until the queue drains
}

void TerminalConsole::enterCommandMode() {
    m_commandMode = true;
    // Routine output still waiting to be typed is dropped; whatever the voice
    // is saying stays queued (and restarts on the clean screen).
    m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(), [](const Pending& p) { return !p.anomaly; }),
                  m_queue.end());
    m_lines.clear();
    m_typingLine = false;
    m_typedChars = 0;
    m_typeAccum = 0.0f;
    m_delay = 0.0f;
    print("COMMAND PROMPT. TYPE HELP FOR COMMANDS, STREAM TO WATCH THE LOG.", TerminalScreen::Dim);
    print("");
    m_anomalyTimer = m_rng.range(10.0f, 25.0f);
}

void TerminalConsole::update(float dt, const TerminalContext& ctx) {
    m_uptime += dt;
    m_session += dt;
    m_warnCooldown = std::max(0.0f, m_warnCooldown - dt);
    m_hushCooldown = std::max(0.0f, m_hushCooldown - dt);
    m_screen.glitch = std::max(0.0f, m_screen.glitch - dt * 1.8f);

    // Tube brightness: slow warm-up, fast collapse.
    const float rate = m_targetBrightness > m_screen.brightness ? 1.8f : 9.0f;
    m_screen.brightness += (m_targetBrightness - m_screen.brightness) * (1.0f - std::exp(-rate * dt));

    if (m_powerOffTimer >= 0.0f) {
        m_powerOffTimer -= dt;
        if (m_powerOffTimer < 0.4f) m_targetBrightness = 0.0f; // the picture collapses at the very end
        if (m_powerOffTimer < 0.0f) m_exitRequested = m_powerOffRequested = true;
    }
    if (!m_ready && m_powerOffTimer < 0.0f && m_queue.empty() && !m_typingLine) m_ready = true;

    if (m_ready) {
        // The voice watches out for the player.
        if (ctx.stalkerBehind && m_warnCooldown <= 0.0f) {
            static const char* const kWarnings[] = {"it's right behind you", "TURN AROUND", "don't you feel it? behind you",
                                                    "it's standing behind your chair"};
            printAnomaly(kWarnings[m_rng.next() % 4], 0.2f, true);
            m_warnCooldown = 22.0f;
        } else if (ctx.wandererDistance >= 0.0f && ctx.wandererDistance < 14.0f && m_hushCooldown <= 0.0f) {
            printAnomaly(m_rng.chance(0.5f) ? "shhh. it can hear you typing." : "stop typing. it's listening.", 0.3f, true);
            m_hushCooldown = 40.0f;
        }
        const bool idle = m_queue.empty() && !m_typingLine;
        if (!m_commandMode) {
            // Watching the live log: system output with the anomaly woven through it.
            m_streamTimer -= dt;
            if (idle && m_streamTimer <= 0.0f) streamBurst(ctx);
        } else {
            // At the prompt the log is silent - but the voice is not.
            m_anomalyTimer -= dt;
            if (idle && m_anomalyTimer <= 0.0f) {
                const float desperation = std::min(1.0f, m_session / kDesperationTime);
                printAnomaly(termtext::anomalyMessage(m_rng, desperation), 0.0f);
                m_anomalyTimer = m_rng.range(15.0f, 35.0f) * (1.0f - 0.5f * desperation);
            }
        }
    }
    advanceTyping(dt);
    compose();
}

std::vector<TerminalSound> TerminalConsole::takeSounds() {
    std::vector<TerminalSound> out;
    out.swap(m_sounds);
    return out;
}

// ---- Keyboard ---------------------------------------------------------------------------------

void TerminalConsole::type(const char* text) {
    if (m_powerOffTimer >= 0.0f || !m_ready) return;
    if (!m_commandMode) enterCommandMode(); // typing opens the prompt
    for (const char* c = text; *c; ++c) {
        if (*c < 32 || *c > 126) continue; // printable ASCII only (the font's range)
        if (m_input.size() >= 120) break;
        m_input += *c;
        sound(TerminalSound::Key);
    }
}

void TerminalConsole::backspace() {
    if (m_input.empty()) return;
    m_input.pop_back();
    sound(TerminalSound::Key);
}

void TerminalConsole::historyUp() {
    if (m_history.empty()) return;
    m_historyPos = std::max(0, m_historyPos - 1);
    m_input = m_history[static_cast<size_t>(m_historyPos)];
}

void TerminalConsole::historyDown() {
    if (m_history.empty()) return;
    m_historyPos = std::min(static_cast<int>(m_history.size()), m_historyPos + 1);
    m_input = m_historyPos < static_cast<int>(m_history.size()) ? m_history[static_cast<size_t>(m_historyPos)] : "";
}

void TerminalConsole::submit(const TerminalContext& ctx) {
    sound(TerminalSound::Key);
    if (!m_ready) {
        sound(TerminalSound::Beep);
        return;
    }
    if (!m_commandMode) { // Enter on the live log opens the prompt
        enterCommandMode();
        return;
    }
    const std::string raw = m_input;
    m_input.clear();
    print("> " + raw, TerminalScreen::Input);
    const std::vector<std::string> words = split(toLower(raw));
    if (words.empty()) return;
    if (m_history.empty() || m_history.back() != raw) m_history.push_back(raw);
    m_historyPos = static_cast<int>(m_history.size());

    if (const Command* c = findCommand(words[0])) {
        const Args args(words.begin() + 1, words.end());
        (this->*c->handler)(args, ctx);
        return;
    }
    // Not a command. The system complains... and sometimes someone answers.
    print("BAD COMMAND OR FILE NAME");
    sound(TerminalSound::Beep);
    const std::string lower = toLower(raw);
    const float answerChance = termtext::isConversational(lower) ? 0.85f : 0.15f;
    if (m_rng.chance(answerChance)) {
        const std::string answer = termtext::reply(m_rng, lower);
        if (!answer.empty()) printAnomaly(answer, m_rng.range(1.0f, 2.6f));
    }
}

// ---- Output ------------------------------------------------------------------------------------

void TerminalConsole::print(const std::string& text, uint8_t color, float cps, float delay) {
    // Word-wrap to the screen width.
    std::string rest = text;
    do {
        std::string line = rest.substr(0, TerminalScreen::kCols);
        rest = rest.size() > line.size() ? rest.substr(line.size()) : std::string();
        Pending p;
        p.text = line;
        p.color = color;
        p.charsPerSecond = cps;
        p.delay = delay;
        m_queue.push_back(p);
        delay = 0.0f;
    } while (!rest.empty());
}

void TerminalConsole::printAnomaly(const std::string& text, float delay, bool urgent) {
    Pending p;
    p.text = text.substr(0, TerminalScreen::kCols);
    // Shouting comes through in red; everything else in a cold, unfamiliar white.
    p.color = hasUpperCase(text) ? TerminalScreen::Alert : TerminalScreen::Anomaly;
    p.charsPerSecond = m_rng.range(8.0f, 18.0f);
    p.delay = delay;
    p.anomaly = true;
    if (urgent) m_queue.push_front(p); // cuts in ahead of routine output
    else m_queue.push_back(p);
}

void TerminalConsole::advanceTyping(float dt) {
    int instantBudget = 3; // routine output bursts out, but stays readable as it scrolls
    float time = dt;
    while (true) {
        if (m_delay > 0.0f) {
            m_delay -= time;
            return;
        }
        if (!m_typingLine) {
            if (m_queue.empty()) return;
            Pending& p = m_queue.front();
            if (p.delay > 0.0f) {
                m_delay = p.delay;
                p.delay = 0.0f;
                return;
            }
            if (p.anomaly) {
                m_screen.glitch = std::max(m_screen.glitch, m_rng.range(0.55f, 1.0f));
                sound(TerminalSound::Glitch);
            }
            m_lines.push_back({"", p.color});
            while (m_lines.size() > kMaxScrollback) m_lines.pop_front();
            m_typingLine = true;
            m_typedChars = 0;
            m_typeAccum = 0.0f;
        }
        Pending& p = m_queue.front();
        if (p.charsPerSecond <= 0.0f) {
            if (instantBudget-- <= 0) return;
            m_lines.back().text = p.text;
        } else {
            m_typeAccum += time * p.charsPerSecond;
            time = 0.0f;
            while (m_typeAccum >= 1.0f && m_typedChars < p.text.size()) {
                m_typeAccum -= 1.0f;
                m_lines.back().text += p.text[m_typedChars++];
                if (p.anomaly) {
                    // Nobody is at the keyboard, yet the keys click... and hesitate.
                    if (m_typedChars % 2 == 0) sound(TerminalSound::GhostKey);
                    if (m_rng.chance(0.07f)) {
                        m_delay = m_rng.range(0.15f, 0.7f);
                        return;
                    }
                }
            }
            if (m_typedChars < p.text.size()) return;
        }
        m_typingLine = false;
        m_queue.pop_front();
    }
}

void TerminalConsole::streamBurst(const TerminalContext& ctx) {
    const float desperation = std::min(1.0f, m_session / kDesperationTime);
    const int level = ctx.level;

    if (m_rng.chance(0.045f + 0.11f * desperation)) {
        // The anomaly breaks through the log.
        printAnomaly(termtext::anomalyMessage(m_rng, desperation), m_rng.range(0.5f, 1.6f));
        if (m_rng.chance(0.35f)) {
            print(termtext::corrupt(m_rng, termtext::systemLine(m_rng, m_mode, m_uptime, level), 0.45f), TerminalScreen::Dim);
        }
        m_streamTimer = m_rng.range(1.0f, 2.5f);
        return;
    }
    if (m_rng.chance(0.012f + 0.02f * desperation)) {
        char buf[80];
        const int minutes = std::max(1, static_cast<int>(m_session / 60.0f));
        if (m_rng.chance(0.5f)) {
            std::snprintf(buf, sizeof(buf), "you've been here %d minute%s. i've been here %d years.", minutes,
                          minutes == 1 ? "" : "s", m_rng.rangeInt(11, 40));
        } else {
            std::snprintf(buf, sizeof(buf), "level %d? i was on level %d once. it's the same.", level,
                          level + m_rng.rangeInt(-9, 9));
        }
        printAnomaly(buf, m_rng.range(0.6f, 1.4f));
        m_streamTimer = 1.5f;
        return;
    }

    const int count = m_rng.rangeInt(1, 4);
    for (int i = 0; i < count; ++i) {
        const float r = m_rng.nextFloat();
        const uint8_t color = r < 0.15f ? TerminalScreen::Dim : r < 0.2f ? TerminalScreen::Bright : TerminalScreen::Normal;
        const float cps = m_rng.chance(0.08f) ? m_rng.range(60.0f, 140.0f) : 0.0f; // now and then, a line crawls out
        print(termtext::systemLine(m_rng, m_mode, m_uptime, level), color, cps);
    }
    m_streamTimer = m_rng.chance(0.2f) ? m_rng.range(0.8f, 2.2f) : m_rng.range(0.04f, 0.35f);
}

void TerminalConsole::compose() {
    for (TerminalScreen::Cell& c : m_screen.cells) c = {' ', TerminalScreen::Normal};

    // Output: the newest lines, bottom-aligned above the prompt.
    const int shown = std::min(kOutputRows, static_cast<int>(m_lines.size()));
    const int first = static_cast<int>(m_lines.size()) - shown;
    for (int i = 0; i < shown; ++i) {
        const Line& line = m_lines[static_cast<size_t>(first + i)];
        const int row = kOutputRows - shown + i;
        for (int col = 0; col < TerminalScreen::kCols && col < static_cast<int>(line.text.size()); ++col) {
            m_screen.at(col, row) = {line.text[static_cast<size_t>(col)], line.color};
        }
    }

    // Prompt line (scrolled so the end of long input stays visible). On the
    // live log it only says how to get a prompt.
    const int promptRow = TerminalScreen::kRows - 1;
    const bool usable = m_ready && m_powerOffTimer < 0.0f;
    m_screen.cursorVisible = usable && m_commandMode;
    if (usable && !m_commandMode) {
        const std::string hint = "PRESS ENTER FOR A COMMAND PROMPT";
        for (int col = 0; col < static_cast<int>(hint.size()); ++col) {
            m_screen.at(col, promptRow) = {hint[static_cast<size_t>(col)], TerminalScreen::Dim};
        }
    }
    if (m_screen.cursorVisible) {
        const int room = TerminalScreen::kCols - 3;
        const std::string shownInput =
            m_input.size() > static_cast<size_t>(room) ? m_input.substr(m_input.size() - static_cast<size_t>(room)) : m_input;
        const std::string prompt = "> " + shownInput;
        for (int col = 0; col < static_cast<int>(prompt.size()); ++col) {
            m_screen.at(col, promptRow) = {prompt[static_cast<size_t>(col)], TerminalScreen::Input};
        }
        m_screen.cursorCol = static_cast<int>(prompt.size());
        m_screen.cursorRow = promptRow;
    }

    // Heavy interference scrambles random characters for a moment.
    if (m_screen.glitch > 0.35f) {
        static const char kNoise[] = "#%&@$*!?/\\|<>~^=+";
        const int hits = static_cast<int>(m_screen.glitch * 40.0f);
        for (int i = 0; i < hits; ++i) {
            TerminalScreen::Cell& c = m_screen.cells[m_rng.next() % m_screen.cells.size()];
            const uint8_t color = m_rng.chance(0.5f) ? static_cast<uint8_t>(TerminalScreen::Anomaly) : c.color;
            c = {kNoise[m_rng.next() % (sizeof(kNoise) - 1)], color};
        }
    }
}

// ---- Commands ------------------------------------------------------------------------------------

void TerminalConsole::cmdHelp(const Args&, const TerminalContext&) {
    print("AVAILABLE COMMANDS:", TerminalScreen::Bright);
    for (const Command& c : commands()) {
        char line[80];
        std::snprintf(line, sizeof(line), "  %-18s %s", c.usage, c.help);
        print(line);
    }
}

void TerminalConsole::cmdClear(const Args&, const TerminalContext&) {
    m_lines.clear();
    m_typingLine = false;
}

void TerminalConsole::cmdDiag(const Args&, const TerminalContext& ctx) {
    print("RUNNING DIAGNOSTICS...", TerminalScreen::Bright);
    for (int i = 0; i < 10; ++i) print(termtext::systemLine(m_rng, termtext::StreamMode::Diag, m_uptime, ctx.level), 0, 0.0f, 0.12f);
    print("DIAGNOSTICS COMPLETE: 1 OCCUPANT UNACCOUNTED FOR.", TerminalScreen::Bright, 50.0f, 0.4f);
}

void TerminalConsole::cmdStream(const Args&, const TerminalContext&) {
    m_commandMode = false;
    m_streamTimer = 0.6f;
    print(std::string("RESUMING LIVE LOG (") + termtext::modeName(m_mode) + "). PRESS ENTER FOR THE PROMPT.", TerminalScreen::Dim);
}

void TerminalConsole::cmdMode(const Args& args, const TerminalContext&) {
    termtext::StreamMode mode;
    if (args.empty() || !termtext::parseMode(args[0], mode)) {
        print(std::string("SOURCE IS ") + termtext::modeName(m_mode) + ". USAGE: MODE <ALL|KERNEL|DIAG|NET|HEX>");
        return;
    }
    m_mode = mode;
    print(std::string("LOG SOURCE: ") + termtext::modeName(m_mode) + ". TYPE STREAM TO WATCH IT.", TerminalScreen::Dim);
}

void TerminalConsole::cmdStatus(const Args&, const TerminalContext& ctx) {
    char line[80];
    std::snprintf(line, sizeof(line), "NODE %04X   LEVEL %d   UPTIME %.0fS", static_cast<unsigned>(m_id & 0xFFFF), ctx.level, m_uptime);
    print(line, TerminalScreen::Bright);
    std::snprintf(line, sizeof(line), "LOG SOURCE: %s", termtext::modeName(m_mode));
    print(line);
    print("POWER: MAINS   LIGHTING: DEGRADED   EXITS: 0");
    print(ctx.stalkerDistance >= 0.0f || ctx.wandererDistance >= 0.0f ? "OCCUPANTS: 1 (+2 UNREGISTERED)" : "OCCUPANTS: 1 (+1 UNREGISTERED)");
}

void TerminalConsole::cmdWhoami(const Args&, const TerminalContext&) {
    if (m_session > 90.0f && m_rng.chance(0.5f)) {
        printAnomaly("you're not the operator. i am. i was.", 0.8f);
    } else {
        print("OPERATOR");
    }
}

void TerminalConsole::cmdPing(const Args& args, const TerminalContext&) {
    const std::string host = args.empty() ? "10.0.0.1" : toUpper(args[0]);
    print("PINGING " + host + " WITH 32 BYTES OF DATA:");
    for (int i = 0; i < 4; ++i) {
        char line[80];
        if (i == 3 && m_rng.chance(0.3f)) {
            print("REPLY FROM " + host + ": help me", TerminalScreen::Anomaly, 14.0f, 1.0f);
            continue;
        }
        if (m_rng.chance(0.25f)) std::snprintf(line, sizeof(line), "REQUEST TIMED OUT.");
        else std::snprintf(line, sizeof(line), "REPLY FROM %s: BYTES=32 TIME=%dMS TTL=64", host.c_str(), m_rng.rangeInt(1, 900));
        print(line, TerminalScreen::Normal, 0.0f, 0.6f);
    }
}

void TerminalConsole::cmdMap(const Args&, const TerminalContext& ctx) {
    if (!ctx.world) {
        print("MAP DATA UNAVAILABLE.");
        return;
    }
    // Floor plan of the cells around the terminal, north up.
    // Walls: - |   doors: = :   arches: . (gaps are open plan)
    // @ this terminal   ^ stairs up   v stairs down
    const int R = 4; // 9 x 9 cells: 19 rows, plus title and legend
    const float S = world::kCellSize;
    const int px = static_cast<int>(std::floor(ctx.playerFeet.x / S));
    const int pz = static_cast<int>(std::floor(ctx.playerFeet.z / S));
    const WorldGenerator& w = *ctx.world;
    auto edgeChar = [](world::EdgeType t, bool horizontal) {
        switch (t) {
        case world::EdgeType::Wall:    return horizontal ? '-' : '|';
        case world::EdgeType::Door:    return horizontal ? '=' : ':';
        case world::EdgeType::Archway: return '.';
        case world::EdgeType::Open:
        default:                       return ' ';
        }
    };
    char title[64];
    std::snprintf(title, sizeof(title), "LEVEL %d FLOOR PLAN (5M GRID, NORTH UP)", ctx.level);
    print(title, TerminalScreen::Bright);
    // Three characters per cell: a corner and two edge characters on the
    // edge rows, a wall and a centred marker on the cell rows.
    auto edgeRow = [&](int gz) {
        std::string row = "      ";
        for (int gx = px - R; gx <= px + R + 1; ++gx) {
            row += w.vertexHasWall(ctx.level, gx, gz) ? '+' : ' ';
            if (gx <= px + R) row += std::string(2, edgeChar(w.edge(ctx.level, gx, gz, world::EdgeAxis::South), true));
        }
        return row;
    };
    for (int gz = pz + R; gz >= pz - R; --gz) {
        print(edgeRow(gz + 1), TerminalScreen::Normal, 0.0f, 0.03f);
        std::string row = "      ";
        for (int gx = px - R; gx <= px + R + 1; ++gx) {
            row += edgeChar(w.edge(ctx.level, gx, gz, world::EdgeAxis::West), false);
            if (gx > px + R) break;
            const world::CellRole role = w.cellRole(ctx.level, gx, gz);
            row += (gx == px && gz == pz) ? '@' : role == world::CellRole::StairsLower ? '^'
                                              : role == world::CellRole::StairsUpper ? 'v' : ' ';
            row += ' ';
        }
        print(row, TerminalScreen::Normal, 0.0f, 0.03f);
    }
    print(edgeRow(pz - R), TerminalScreen::Normal, 0.0f, 0.03f);
    print("@ YOU   ^ STAIRS UP   v STAIRS DOWN   = : DOORS   . ARCHES", TerminalScreen::Dim);
}

void TerminalConsole::cmdEcho(const Args& args, const TerminalContext&) {
    std::string text;
    for (const std::string& a : args) text += (text.empty() ? "" : " ") + toUpper(a);
    print(text);
    if (!text.empty() && m_rng.chance(0.2f)) printAnomaly(toLower(text) + "?", 1.5f);
}

void TerminalConsole::cmdReboot(const Args&, const TerminalContext&) {
    m_commandMode = false; // it boots to the live log
    m_lines.clear();
    m_queue.clear();
    m_typingLine = false;
    m_delay = 0.0f;
    m_ready = false;
    m_uptime = 0.0;
    m_screen.brightness = 0.0f;
    m_targetBrightness = 1.0f;
    sound(TerminalSound::Boot);
    const std::vector<std::string> boot = termtext::bootSequence(m_rng, m_level, static_cast<uint32_t>(m_id));
    for (size_t i = 0; i < boot.size(); ++i) {
        const float delay = i == 0 ? 1.2f : m_rng.range(0.08f, 0.45f);
        print(boot[i], i >= boot.size() - 3 ? TerminalScreen::Bright : TerminalScreen::Normal, i == 3 ? 40.0f : 0.0f, delay);
    }
}

void TerminalConsole::cmdShutdown(const Args&, const TerminalContext&) {
    print("SYSTEM IS GOING DOWN FOR POWER OFF NOW.", TerminalScreen::Bright);
    if (m_rng.chance(0.6f)) printAnomaly("no no no don't turn me off", 0.1f);
    m_powerOffTimer = 2.2f;
    m_ready = false;
    sound(TerminalSound::PowerDown);
}

void TerminalConsole::cmdExit(const Args&, const TerminalContext&) {
    print("LOGGED OUT.", TerminalScreen::Dim);
    m_exitRequested = true;
    if (m_rng.chance(0.4f)) printAnomaly("come back", 0.8f);
}
