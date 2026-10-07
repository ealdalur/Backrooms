#pragma once
// ---------------------------------------------------------------------------
// PuzzleChain.h
// The way out of the Backrooms: a chain of clues, each found in a different
// part of the game, and the state of the player's progress along it.
//
//   WallClue      A phone number is written on a wall somewhere - rarely,
//                 alone, without a word of explanation (World/Decals).
//   NumberDialed  Dialled on any desk phone, it rings twice and someone
//                 answers through the static: "I was able to overwrite the
//                 memory at 7A9F... ping that, hurry though, I don't know
//                 how long it will last" - and the line drops.
//   MemoryFound   From then on a terminal streaming its hex log shows,
//                 sooner or later (95 % within two minutes of watching), the
//                 dump line at that address - its ASCII column an IP address.
//                 SPACE pauses the stream to read it.
//   TargetMapped  PING that address from any terminal: it answers, and the
//                 data it returns is an offset from that terminal, shaped
//                 like the HUD's position without its words: "+2 | [-4, +3]"
//                 (storeys | [chunks along x, along z]) - "maps are here.
//                 count from where you are." The chunk there now holds a
//                 terminal - and a chunk next door, the glitch room.
//   ExitLocated   MAP on that terminal shows the room, blinking, a few cells
//                 beyond its chunk (World/WorldGenerator: never in it).
//   Escaped       Walking into its glitching walls: noclip, out of the
//                 Backrooms, into the office.
//
// Only the number on the wall is a function of the world seed (it has to be:
// it is written into the world). Everything after it depends on where the
// player happened to be, so the steps cannot simply be memorised:
//   * the phone the number is first called from picks the memory address
//     (one of kMemoryAddressCount, each with its own recording of the voice)
//     and the IP address stored there (10.x.x.x: the facility's network);
//   * the terminal the IP is first pinged from picks the offset to the exit
//     (each part 1 to 8 either way: storeys, chunks along x, chunks along z).
// Both are fixed by the first time they happen; later calls and pings report
// the same memory, and the same exit (as seen from where they are made).
//
// Stages only ever move forward, and may be skipped (a player who knows the
// IP can ping it as soon as the number has been called). The chain reacts to
// events the other systems report; it decides nothing about the world itself
// - the Engine wires its answers into the world generator, the terminals and
// the phones.
// ---------------------------------------------------------------------------

#include "World/ChunkCoord.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <optional>
#include <string>

enum class PuzzleStage : uint8_t {
    WallClue = 0,
    NumberDialed,
    MemoryFound,
    TargetMapped,
    ExitLocated,
    Escaped,
};

/// What there is to find. The number is fixed per world seed; the rest is
/// empty until the call (and the ping) that decide it.
struct PuzzleSecrets {
    std::string phoneNumber;   ///< As written on the wall: "555-0198".
    std::string phoneDigits;   ///< As dialled: "5550198".
    // Decided by the first call that gets through:
    int         memoryIndex = -1; ///< Which of the memory addresses (the voice's recording), < 0 until then.
    std::string memoryAddress;    ///< "7A9F" (the voice on the phone says it).
    std::string ipAddress;        ///< In the dump's ASCII column: "10.42.7.19".
    // Decided by the first ping of that address:
    bool        offsetFixed = false;
    int         floorDelta = 0;     ///< Storeys from the pinging terminal to the exit...
    glm::ivec2  chunkDelta{0, 0};   ///< ...and chunks (x, z); each part 1..8 either way.
};

/// Something a terminal reports to the chain.
struct PuzzleEvent {
    enum class Kind : uint8_t {
        MemoryShown, ///< The memory's line came up on a screen.
        Pinged,      ///< The IP answered a ping from chunk `from`, sent by terminal `terminal`.
        ExitMapped,  ///< MAP showed the glitch room.
    };
    Kind       kind;
    ChunkCoord from;
    uint64_t   terminal = 0;
};

class PuzzleChain {
public:
    /// How many memory addresses the voice on the phone can name.
    static constexpr int kMemoryAddressCount = 32;
    /// One of them: four hex digits, at least one a letter A-F, never ending
    /// in 0 - the hex log's own lines are 16-byte aligned (they always end in
    /// 0), so none of these is ever seen there until the call puts it there.
    static const std::string& memoryAddress(int index);

    explicit PuzzleChain(uint64_t worldSeed);

    const PuzzleSecrets& secrets() const { return m_secrets; }
    PuzzleStage stage() const { return m_stage; }
    bool reached(PuzzleStage s) const { return m_stage >= s; }

    /// Moves on to `s` if that is further along. True if the stage changed.
    /// (Reaching NumberDialed without a call - a developer's shortcut - decides the memory as if from "phone 0".)
    bool advance(PuzzleStage s);

    /// The memory a call of the number from phone `phone` (its id) gets told
    /// about: the one an earlier call fixed, or this phone's own.
    int memoryFor(uint64_t phone) const;
    /// The number answered on phone `phone`: fixes the memory and the IP in it,
    /// if no call has yet. True if it was just fixed.
    bool answeredOn(uint64_t phone);

    /// The exit's chunk, once a ping has located it.
    const std::optional<ChunkCoord>& exitChunk() const { return m_exit; }

    /// Where a ping from chunk `from`, sent by terminal `terminal`, points: the
    /// exit if it is already mapped, otherwise `from` plus that terminal's offset.
    ChunkCoord targetFrom(const ChunkCoord& from, uint64_t terminal) const;
    /// Fixes the exit (and the offset) from the first ping; later pings only
    /// report it. True if it was just fixed.
    bool mapExit(const ChunkCoord& from, uint64_t terminal);

    /// An offset as a ping reports it: "+2 | [-4, +3]" (storeys | [chunks along x, along z]).
    static std::string offsetText(int floor, int x, int z);

    static const char* stageName(PuzzleStage s);

private:
    uint64_t                  m_seed;
    PuzzleSecrets             m_secrets;
    PuzzleStage               m_stage = PuzzleStage::WallClue;
    std::optional<ChunkCoord> m_exit;
};
