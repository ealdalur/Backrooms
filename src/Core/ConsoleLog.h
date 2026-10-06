#pragma once
// ---------------------------------------------------------------------------
// ConsoleLog.h
// The game's terminal output, dressed up: the title in ASCII art at start-up,
// then one line per event in the style of a boot log -
//
//     [  0.412 ] ENGINE  » OpenGL 3.3.0 NVIDIA 595.79 · 1600x900
//
// with 24-bit colour when the stream is a terminal that understands it, and
// plain text when it is redirected to a file (or NO_COLOR is set). Values
// that should stand out are marked with braces: "{83} sounds in {494 ms}".
//
// Spoilers: lines that would give the game's secrets away (the hidden things
// to find, the way out) go through spoiler(), which prints only when the
// developer log is on (--verbose, a developer scene or a puzzle stage).
// Safe to call from any thread.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <string>

namespace con {

enum class Level { Info, Warn, Error };

/// A row of the key guide: the keys, and what they do.
struct KeyHelp {
    const char* keys;
    const char* action;
};

/// Turns on colour (on Windows: the console's escape-sequence processing). Call first.
void init();

/// The title, in ASCII art, with its tagline.
void banner();

/// A divider with a caption: "──[ BOOT ]──────────".
void section(const char* caption);

/// One line from subsystem `tag`. Warnings and errors go to stderr.
void line(const char* tag, const std::string& text, Level level = Level::Info);

/// A further line of the previous entry, indented under its text.
void detail(const std::string& text, Level level = Level::Info);

/// Free text (help pages), {marked} values standing out; to stdout.
void print(const std::string& text);

/// The key guide, as a boxed two-column table.
void keyTable(const char* caption, const KeyHelp* keys, size_t count);

/// printf-style formatting into a string.
std::string format(const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

/// Whether spoiler() lines are printed (off by default).
void setSpoilers(bool on);
bool spoilers();

/// line(), but only with spoilers on: for anything that gives a secret away.
void spoiler(const char* tag, const std::string& text);

} // namespace con
