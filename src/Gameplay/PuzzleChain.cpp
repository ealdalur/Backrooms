// ---------------------------------------------------------------------------
// PuzzleChain.cpp
// ---------------------------------------------------------------------------
#include "Gameplay/PuzzleChain.h"

#include "Math/Random.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr uint64_t kSaltPuzzle = 0x9022'1E00ull;
} // namespace

PuzzleChain::PuzzleChain(uint64_t worldSeed) : m_secrets(secretsFor(worldSeed)) {}

PuzzleSecrets PuzzleChain::secretsFor(uint64_t worldSeed) {
    rnd::Rng rng(rnd::hashCombine(worldSeed, kSaltPuzzle));
    PuzzleSecrets s;
    // 555-0100..0199: the range set aside for fiction.
    const int line = rng.rangeInt(0, 99);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "555-01%02d", line);
    s.phoneNumber = buf;
    std::snprintf(buf, sizeof(buf), "55501%02d", line);
    s.phoneDigits = buf;
    s.memoryAddress = "7A9F";
    std::snprintf(buf, sizeof(buf), "192.168.%d.%d", rng.rangeInt(1, 254), rng.rangeInt(2, 254));
    s.ipAddress = buf;
    // Somewhere else: one to three storeys away, a few chunks off.
    s.floorDelta = rng.rangeInt(1, 3) * (rng.chance(0.5f) ? 1 : -1);
    do {
        s.chunkDelta = glm::ivec2(rng.rangeInt(-9, 9), rng.rangeInt(-9, 9));
    } while (std::max(std::abs(s.chunkDelta.x), std::abs(s.chunkDelta.y)) < 3);
    return s;
}

bool PuzzleChain::advance(PuzzleStage s) {
    if (s <= m_stage) return false;
    m_stage = s;
    return true;
}

ChunkCoord PuzzleChain::targetFrom(const ChunkCoord& from) const {
    if (m_exit) return *m_exit;
    return {from.x + m_secrets.chunkDelta.x, from.z + m_secrets.chunkDelta.y, from.level + m_secrets.floorDelta};
}

std::string PuzzleChain::payloadFrom(const ChunkCoord& from) const {
    const ChunkCoord t = targetFrom(from);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "maps are here: %+d;(%+d,%+d)", t.level - from.level, t.x - from.x, t.z - from.z);
    return buf;
}

bool PuzzleChain::mapExit(const ChunkCoord& from) {
    if (m_exit) return false;
    m_exit = targetFrom(from);
    return true;
}

const char* PuzzleChain::stageName(PuzzleStage s) {
    switch (s) {
    case PuzzleStage::WallClue:     return "WALL CLUE";
    case PuzzleStage::NumberDialed: return "NUMBER DIALED";
    case PuzzleStage::MemoryFound:  return "MEMORY FOUND";
    case PuzzleStage::TargetMapped: return "TARGET MAPPED";
    case PuzzleStage::ExitLocated:  return "EXIT LOCATED";
    case PuzzleStage::Escaped:      return "ESCAPED";
    }
    return "?";
}
