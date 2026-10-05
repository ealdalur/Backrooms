#pragma once
// ---------------------------------------------------------------------------
// TerminalText.h
// Procedural text for the retro terminals:
//   * an endless stream of plausible system output (kernel logs, hardware
//     diagnostics, network chatter, hex dumps) - some of it quietly wrong;
//   * the "anomaly": distressed messages from someone trapped inside the
//     machine, plus context-aware ones (an entity is near, time passing);
//   * boot sequences and the voice's replies to what the player types.
// All generation is driven by a caller-supplied deterministic RNG.
// ---------------------------------------------------------------------------

#include "Math/Random.h"

#include <string>
#include <vector>

namespace termtext {

/// Which flavour of system output the stream produces.
enum class StreamMode : uint8_t { All, Kernel, Diag, Net, Hex };

/// Parses a mode name ("all", "kernel", "diag", "net", "hex"). False if unknown.
bool parseMode(const std::string& name, StreamMode& out);
const char* modeName(StreamMode mode);

/// One line of routine system output (upper case, <= 64 characters).
/// `uptime` feeds the kernel-style timestamps; `level` the facility lore.
std::string systemLine(rnd::Rng& rng, StreamMode mode, double uptime, int level);

/// A distressed message from whoever is in the machine (lower case, mostly).
/// `desperation` 0..1 grows with session time and shifts the tone.
std::string anomalyMessage(rnd::Rng& rng, float desperation);

/// Rewrites some characters of `text` as line noise (probability `amount`).
std::string corrupt(rnd::Rng& rng, const std::string& text, float amount);

/// Power-on self test and boot log.
std::vector<std::string> bootSequence(rnd::Rng& rng, int level, uint32_t node);

/// The trapped voice answering the player's free text, or "" for silence.
std::string reply(rnd::Rng& rng, const std::string& lowercaseInput);

/// True if the input reads like talking to someone rather than a command.
bool isConversational(const std::string& lowercaseInput);

/// The hex dump line at `address` (four hex digits) whose 16 bytes spell
/// `ascii` (padded with NULs), formatted like every other HEX line.
std::string memoryLine(const std::string& address, const std::string& ascii);

/// The voice, once it has overwritten the memory, nudging the player towards it.
std::string memoryHint(rnd::Rng& rng, const std::string& address);

} // namespace termtext
