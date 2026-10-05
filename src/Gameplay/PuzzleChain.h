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
//                 dump line at 7A9F - its ASCII column an IP address. SPACE
//                 pauses the stream to read it.
//   TargetMapped  PING that address from any terminal: it answers, and the
//                 data it returns says "maps are here: +2;(-4,+3)" - a storey
//                 offset and a chunk offset from where the terminal stands.
//                 The chunk there now holds the glitch room (and a terminal).
//   ExitLocated   MAP on a terminal in that chunk shows the room, blinking.
//   Escaped       Walking into its glitching walls: noclip, out of the
//                 Backrooms, into the office.
//
// The secrets (the number, the address, the IP and the offset) are a pure
// function of the world seed. Stages only ever move forward, and may be
// skipped (a player who knows the IP can ping it as soon as the number has
// been called). The chain reacts to events the other systems report; it
// decides nothing about the world itself - the Engine wires its answers
// into the world generator, the terminals and the phones.
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

/// Everything there is to find, fixed per world seed.
struct PuzzleSecrets {
    std::string phoneNumber;   ///< As written on the wall: "555-0198".
    std::string phoneDigits;   ///< As dialled: "5550198".
    std::string memoryAddress; ///< "7A9F" (the voice on the phone says it).
    std::string ipAddress;     ///< In the dump's ASCII column: "192.168.86.29".
    int         floorDelta = 2;        ///< Storeys from the pinging terminal to the exit (never 0).
    glm::ivec2  chunkDelta{-4, 3};     ///< Chunks (x, z) from it; each |component| < 10.
};

/// Something a terminal reports to the chain.
struct PuzzleEvent {
    enum class Kind : uint8_t {
        MemoryShown, ///< The 7A9F line came up on a screen.
        Pinged,      ///< The IP answered a ping from chunk `from`.
        ExitMapped,  ///< MAP showed the glitch room.
    };
    Kind       kind;
    ChunkCoord from;
};

class PuzzleChain {
public:
    explicit PuzzleChain(uint64_t worldSeed);

    /// The secrets of a world (a pure function of its seed).
    static PuzzleSecrets secretsFor(uint64_t worldSeed);

    const PuzzleSecrets& secrets() const { return m_secrets; }
    PuzzleStage stage() const { return m_stage; }
    bool reached(PuzzleStage s) const { return m_stage >= s; }

    /// Moves on to `s` if that is further along. True if the stage changed.
    bool advance(PuzzleStage s);

    /// The exit's chunk, once a ping has located it.
    const std::optional<ChunkCoord>& exitChunk() const { return m_exit; }

    /// Where a ping from `from` points: the exit if it is already mapped,
    /// otherwise `from` plus the secret offset.
    ChunkCoord targetFrom(const ChunkCoord& from) const;
    /// The data a ping from `from` returns: "maps are here: +2;(-4,+3)".
    std::string payloadFrom(const ChunkCoord& from) const;
    /// Fixes the exit from the first ping (later pings only report it). True if it was just fixed.
    bool mapExit(const ChunkCoord& from);

    static const char* stageName(PuzzleStage s);

private:
    PuzzleSecrets             m_secrets;
    PuzzleStage               m_stage = PuzzleStage::WallClue;
    std::optional<ChunkCoord> m_exit;
};
