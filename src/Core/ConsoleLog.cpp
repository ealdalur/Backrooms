// ---------------------------------------------------------------------------
// ConsoleLog.cpp
// ---------------------------------------------------------------------------
#include "Core/ConsoleLog.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace con {
namespace {

// ---- Output streams ----------------------------------------------------------------------

struct Stream {
    FILE* file = nullptr;
    bool  colour = false;
#if defined(_WIN32)
    HANDLE handle = nullptr;
    bool   console = false; ///< A real console: written as UTF-16, whatever its code page.
    DWORD  savedMode = 0;
#endif
};

Stream     g_out, g_err;
std::mutex g_mutex;
std::atomic<bool> g_spoilers{false};
const auto g_start = std::chrono::steady_clock::now();

#if defined(_WIN32)
void restoreConsoleModes() {
    for (Stream* s : {&g_out, &g_err}) {
        if (s->console) SetConsoleMode(s->handle, s->savedMode);
    }
}
#endif

/// Colour on a terminal that takes it; `force` (FORCE_COLOR) also through a pipe.
void openStream(Stream& s, FILE* file, bool allowColour, bool force) {
    s.file = file;
#if defined(_WIN32)
    s.handle = GetStdHandle(file == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    DWORD mode = 0;
    s.console = s.handle && s.handle != INVALID_HANDLE_VALUE && GetConsoleMode(s.handle, &mode);
    if (s.console) {
        s.savedMode = mode;
        s.colour = allowColour && SetConsoleMode(s.handle, mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#else
    const char* term = std::getenv("TERM");
    s.colour = allowColour && isatty(fileno(file)) && !(term && std::strcmp(term, "dumb") == 0);
#endif
    s.colour = s.colour || (allowColour && force);
}

void write(Stream& s, const std::string& utf8) {
    // Anything else printed so far goes first.
    std::cout.flush();
    std::cerr.flush();
    std::fflush(s.file);
#if defined(_WIN32)
    if (s.console) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring wide(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), n);
        DWORD written = 0;
        WriteConsoleW(s.handle, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr);
        return;
    }
#endif
    std::fwrite(utf8.data(), 1, utf8.size(), s.file);
    std::fflush(s.file);
}

// ---- Colour ------------------------------------------------------------------------------

struct Rgb {
    int r, g, b;
};

const Rgb kFrame{92, 88, 66};     ///< Brackets, rules, separators.
const Rgb kClock{132, 176, 112};  ///< Timestamps.
const Rgb kText{214, 210, 190};   ///< Ordinary text.
const Rgb kValue{255, 232, 128};  ///< {Marked} values: mono-yellow.
const Rgb kWarn{255, 186, 72};
const Rgb kError{255, 92, 80};

std::string fg(const Rgb& c) { return "\033[38;2;" + std::to_string(c.r) + ";" + std::to_string(c.g) + ";" + std::to_string(c.b) + "m"; }
const char* const kBold = "\033[1m";
const char* const kReset = "\033[0m";

Rgb mix(const Rgb& a, const Rgb& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.r + static_cast<int>(static_cast<float>(b.r - a.r) * t), a.g + static_cast<int>(static_cast<float>(b.g - a.g) * t),
            a.b + static_cast<int>(static_cast<float>(b.b - a.b) * t)};
}
Rgb scale(const Rgb& c, float k) {
    return {static_cast<int>(static_cast<float>(c.r) * k), static_cast<int>(static_cast<float>(c.g) * k), static_cast<int>(static_cast<float>(c.b) * k)};
}

/// Each subsystem its own colour, so the log can be read at a glance.
Rgb tagColour(const char* tag) {
    struct Entry {
        const char* tag;
        Rgb         colour;
    };
    static const Entry kTags[] = {
        {"ENGINE", {104, 204, 255}}, {"GPU", {198, 140, 255}},   {"RENDER", {124, 160, 255}}, {"AUDIO", {110, 232, 152}},
        {"WORLD", {255, 214, 92}},   {"ENTITY", {255, 112, 92}}, {"INPUT", {150, 220, 220}},  {"HR", {120, 170, 255}},
    };
    for (const Entry& e : kTags) {
        if (std::strcmp(e.tag, tag) == 0) return e.colour;
    }
    return {255, 128, 204}; // the developer log's own subsystems (PUZZLE, TESLA, DOOM, ...)
}

/// Applies the {value} marking: coloured on a terminal, the braces dropped in plain text.
std::string markup(const std::string& text, bool colour, const Rgb& base) {
    std::string out;
    out.reserve(text.size() + 32);
    if (colour) out += fg(base);
    for (char c : text) {
        if (c == '{') {
            if (colour) out += std::string(kBold) + fg(kValue);
        } else if (c == '}') {
            if (colour) out += std::string(kReset) + fg(base);
        } else {
            out += c;
        }
    }
    if (colour) out += kReset;
    return out;
}

/// Display width of UTF-8 text (code points; the {} marks take no room).
size_t width(const std::string& utf8) {
    size_t n = 0;
    for (unsigned char c : utf8) n += (c & 0xC0) != 0x80 && c != '{' && c != '}';
    return n;
}

std::string repeat(const char* s, size_t n) {
    std::string out;
    for (size_t i = 0; i < n; ++i) out += s;
    return out;
}

std::string paint(bool colour, const Rgb& c, const std::string& text) { return colour ? fg(c) + text + kReset : text; }

constexpr size_t kRuleWidth = 77; ///< The banner's width: rules and boxes line up with it.
constexpr size_t kTagWidth = 6;

std::string prefix(bool colour, const char* tag, const Rgb& tagRgb) {
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
    char clock[16];
    std::snprintf(clock, sizeof(clock), "%7.3f", t);
    std::string name(tag);
    name.resize(std::max(name.size(), kTagWidth), ' ');
    if (!colour) return "  [" + std::string(clock) + " ] " + name + " » ";
    return "  " + fg(kFrame) + "[" + fg(kClock) + clock + fg(kFrame) + " ] " + kReset + kBold + fg(tagRgb) + name + kReset + fg(kFrame) +
           " » " + kReset;
}

// ---- The title -----------------------------------------------------------------------------

// "ANSI Shadow" letters: solid blocks, and box-drawing strokes for their shadow.
const char* const kLetterB[6] = {"██████╗ ", "██╔══██╗", "██████╔╝", "██╔══██╗", "██████╔╝", "╚═════╝ "};
const char* const kLetterA[6] = {" █████╗ ", "██╔══██╗", "███████║", "██╔══██║", "██║  ██║", "╚═╝  ╚═╝"};
const char* const kLetterC[6] = {" ██████╗", "██╔════╝", "██║     ", "██║     ", "╚██████╗", " ╚═════╝"};
const char* const kLetterK[6] = {"██╗  ██╗", "██║ ██╔╝", "█████╔╝ ", "██╔═██╗ ", "██║  ██╗", "╚═╝  ╚═╝"};
const char* const kLetterR[6] = {"██████╗ ", "██╔══██╗", "██████╔╝", "██╔══██╗", "██║  ██║", "╚═╝  ╚═╝"};
const char* const kLetterO[6] = {" ██████╗ ", "██╔═══██╗", "██║   ██║", "██║   ██║", "╚██████╔╝", " ╚═════╝ "};
const char* const kLetterM[6] = {"███╗   ███╗", "████╗ ████║", "██╔████╔██║", "██║╚██╔╝██║", "██║ ╚═╝ ██║", "╚═╝     ╚═╝"};
const char* const kLetterS[6] = {"███████╗", "██╔════╝", "███████╗", "╚════██║", "███████║", "╚══════╝"};
const char* const* const kWord[] = {kLetterB, kLetterA, kLetterC, kLetterK, kLetterR, kLetterO, kLetterO, kLetterM, kLetterS};
constexpr int kFaultyLetter = 6; ///< The second O: its tube is on the way out (as on the title screen).

/// Splits UTF-8 text into its code points (as strings).
std::vector<std::string> codePoints(const char* utf8) {
    std::vector<std::string> out;
    for (const char* p = utf8; *p;) {
        const unsigned char c = static_cast<unsigned char>(*p);
        const size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        out.emplace_back(p, n);
        p += n;
    }
    return out;
}

} // namespace

// ---- API -------------------------------------------------------------------------------------

void init() {
    // NO_COLOR asks for plain text (https://no-color.org); FORCE_COLOR for colour even when piped.
    auto envSet = [](const char* name) {
#if defined(_WIN32)
        char value[2] = {};
        return GetEnvironmentVariableA(name, value, sizeof(value)) != 0;
#else
        const char* value = std::getenv(name);
        return value && *value;
#endif
    };
    const bool allow = !envSet("NO_COLOR"), force = envSet("FORCE_COLOR");
    openStream(g_out, stdout, allow, force);
    openStream(g_err, stderr, allow, force);
#if defined(_WIN32)
    std::atexit(restoreConsoleModes);
#endif
}

void banner() {
    std::lock_guard<std::mutex> lock(g_mutex);
    const bool colour = g_out.colour;
    // Fluorescent mono-yellow, from a hot top edge down to amber.
    const Rgb top{255, 246, 184}, bottom{226, 156, 44}, shadow{124, 94, 40};
    std::string out = "\n";
    for (int row = 0; row < 6; ++row) {
        std::string text = "  ", current;
        for (size_t letter = 0; letter < sizeof(kWord) / sizeof(kWord[0]); ++letter) {
            const float dim = static_cast<int>(letter) == kFaultyLetter ? 0.62f : 1.0f;
            const Rgb face = scale(mix(top, bottom, static_cast<float>(row) / 5.0f), dim);
            for (const std::string& cp : codePoints(kWord[letter][row])) {
                if (colour && cp != " ") {
                    const std::string want = fg(cp == "█" ? face : scale(shadow, dim));
                    if (want != current) text += current = want;
                }
                text += cp;
            }
        }
        out += text + (colour ? kReset : "") + "\n";
    }
    // A light strip under it: brightest in the middle, dying away to the ends.
    std::string strip = "  ";
    for (size_t i = 0; i < kRuleWidth; ++i) {
        const float x = std::abs(static_cast<float>(i) / static_cast<float>(kRuleWidth - 1) * 2.0f - 1.0f);
        strip += colour ? fg(mix({255, 224, 120}, {54, 46, 22}, x * x)) + "▀" : "▀";
    }
    out += strip + (colour ? kReset : "") + "\n";
    const std::string tagline = "MONO-YELLOW  ·  MOIST CARPET  ·  HUM-BUZZ";
    const std::string pad(2 + (kRuleWidth - width(tagline)) / 2, ' ');
    out += pad + paint(colour, {176, 160, 108}, tagline) + "\n\n";
    write(g_out, out);
}

void section(const char* caption) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const bool colour = g_out.colour;
    const std::string head = "──[ ";
    const std::string text(caption);
    const size_t used = width(head) + text.size() + 2;
    const std::string tail = " ]" + repeat("─", kRuleWidth > used ? kRuleWidth - used : 0);
    std::string out = "\n  ";
    if (colour) out += fg(kFrame) + head + kBold + fg(kValue) + text + kReset + fg(kFrame) + tail + kReset;
    else out += head + text + tail;
    write(g_out, out + "\n");
}

void line(const char* tag, const std::string& text, Level level) {
    std::lock_guard<std::mutex> lock(g_mutex);
    Stream& s = level == Level::Info ? g_out : g_err;
    const Rgb base = level == Level::Warn ? kWarn : level == Level::Error ? kError : kText;
    const Rgb tagRgb = level == Level::Info ? tagColour(tag) : base;
    write(s, prefix(s.colour, tag, tagRgb) + markup(text, s.colour, base) + "\n");
}

void detail(const std::string& text, Level level) {
    std::lock_guard<std::mutex> lock(g_mutex);
    Stream& s = level == Level::Info ? g_out : g_err;
    const Rgb base = level == Level::Warn ? kWarn : level == Level::Error ? kError : kText;
    const size_t indent = 2 + 11 + kTagWidth + 3; // under the text of a line()
    write(s, std::string(indent, ' ') + markup(text, s.colour, base) + "\n");
}

void print(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    write(g_out, markup(text, g_out.colour, kText));
}

void keyTable(const char* caption, const KeyHelp* keys, size_t count) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const bool colour = g_out.colour;
    size_t keyW = 0, actW = 0;
    for (size_t i = 0; i < count; ++i) {
        keyW = std::max(keyW, width(keys[i].keys));
        actW = std::max(actW, width(keys[i].action));
    }
    // Two columns, as wide as the banner.
    const size_t cell = keyW + 2 + actW;
    const size_t inner = kRuleWidth - 2;
    const size_t gutter = inner > 2 * cell + 4 ? inner - 2 * cell - 4 : 2;
    auto edge = [&](const std::string& s) { return paint(colour, kFrame, s); };
    auto entry = [&](size_t i) {
        if (i >= count) return std::string(cell, ' ');
        std::string k(keys[i].keys), a(keys[i].action);
        k += std::string(keyW - width(k), ' ');
        a += std::string(actW - width(a), ' ');
        return paint(colour, kValue, k) + "  " + paint(colour, kText, a);
    };

    const std::string title = "─[ " + std::string(caption) + " ]";
    std::string out = "\n  " + edge("╭" + (colour ? std::string() : title)) +
                      (colour ? edge("─[ ") + std::string(kBold) + fg(kValue) + caption + kReset + edge(" ]") : std::string()) +
                      edge(repeat("─", inner - width(title)) + "╮") + "\n";
    const size_t rows = (count + 1) / 2;
    for (size_t r = 0; r < rows; ++r) {
        out += "  " + edge("│") + "  " + entry(r) + std::string(gutter, ' ') + entry(r + rows) + "  " + edge("│") + "\n";
    }
    out += "  " + edge("╰" + repeat("─", inner) + "╯") + "\n";
    write(g_out, out);
}

std::string format(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

void setSpoilers(bool on) { g_spoilers = on; }
bool spoilers() { return g_spoilers; }

void spoiler(const char* tag, const std::string& text) {
    if (g_spoilers) line(tag, text);
}

} // namespace con
