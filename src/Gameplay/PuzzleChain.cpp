// ---------------------------------------------------------------------------
// PuzzleChain.cpp
// ---------------------------------------------------------------------------
#include "Gameplay/PuzzleChain.h"

#include "Math/Random.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
constexpr uint64_t kSaltPuzzle  = 0x9022'1E00ull;
constexpr uint64_t kSaltMemory  = 0x9022'1E01ull; ///< Which memory a phone's call hears...
constexpr uint64_t kSaltHost    = 0x9022'1E02ull; ///< ...the IP stored there...
constexpr uint64_t kSaltOffset  = 0x9022'1E03ull; ///< ...and the offset a terminal's ping returns.
constexpr uint64_t kSaltAddress = 0xADD2'E55Eull; ///< The pool of addresses (the same in every world: they are recorded).
constexpr int      kMaxOffset   = 8;
} // namespace

const std::string& PuzzleChain::memoryAddress(int index) {
    // Built once (thread-safe: the sound bank reads it while it synthesises the voice).
    static const std::vector<std::string> kPool = [] {
        std::vector<std::string> pool{"7A9F"};
        rnd::Rng rng(kSaltAddress);
        while (static_cast<int>(pool.size()) < kMemoryAddressCount) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%04X", static_cast<unsigned>(rng.next() & 0xFFFFu));
            const std::string a(buf);
            const bool letter = std::any_of(a.begin(), a.end(), [](char c) { return c >= 'A' && c <= 'F'; });
            if (a[0] == '0' || a[3] == '0' || !letter || std::find(pool.begin(), pool.end(), a) != pool.end()) continue;
            pool.push_back(a);
        }
        return pool;
    }();
    return kPool[static_cast<size_t>(std::clamp(index, 0, kMemoryAddressCount - 1))];
}

PuzzleChain::PuzzleChain(uint64_t worldSeed) : m_seed(worldSeed) {
    rnd::Rng rng(rnd::hashCombine(worldSeed, kSaltPuzzle));
    // 555-0100..0199: the range set aside for fiction.
    const int line = rng.rangeInt(0, 99);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "555-01%02d", line);
    m_secrets.phoneNumber = buf;
    std::snprintf(buf, sizeof(buf), "55501%02d", line);
    m_secrets.phoneDigits = buf;
}

bool PuzzleChain::advance(PuzzleStage s) {
    if (s <= m_stage) return false;
    if (s >= PuzzleStage::NumberDialed) answeredOn(0); // (no-op once a call has decided it)
    m_stage = s;
    return true;
}

int PuzzleChain::memoryFor(uint64_t phone) const {
    if (m_secrets.memoryIndex >= 0) return m_secrets.memoryIndex;
    return static_cast<int>(rnd::hashCombine(rnd::hashCombine(m_seed, kSaltMemory), phone) % kMemoryAddressCount);
}

bool PuzzleChain::answeredOn(uint64_t phone) {
    if (m_secrets.memoryIndex >= 0) return false;
    m_secrets.memoryIndex = memoryFor(phone);
    m_secrets.memoryAddress = memoryAddress(m_secrets.memoryIndex);
    // A host on the facility's network (10.0.0.0/8: an enterprise's, not someone's home router).
    rnd::Rng rng(rnd::hashCombine(rnd::hashCombine(m_seed, kSaltHost), phone));
    char buf[32];
    std::snprintf(buf, sizeof(buf), "10.%d.%d.%d", rng.rangeInt(0, 255), rng.rangeInt(0, 255), rng.rangeInt(1, 254));
    m_secrets.ipAddress = buf;
    return true;
}

ChunkCoord PuzzleChain::targetFrom(const ChunkCoord& from, uint64_t terminal) const {
    if (m_exit) return *m_exit;
    // Somewhere else, picked by the terminal that asks: 1 to 8 storeys up or
    // down, and 1 to 8 chunks either way along x and along z (never 0).
    rnd::Rng rng(rnd::hashCombine(rnd::hashCombine(m_seed, kSaltOffset), terminal));
    auto part = [&rng] { return rng.rangeInt(1, kMaxOffset) * (rng.chance(0.5f) ? 1 : -1); };
    const int floor = part(), dx = part(), dz = part();
    return {from.x + dx, from.z + dz, from.level + floor};
}

bool PuzzleChain::mapExit(const ChunkCoord& from, uint64_t terminal) {
    if (m_exit) return false;
    m_exit = targetFrom(from, terminal);
    m_secrets.offsetFixed = true;
    m_secrets.floorDelta = m_exit->level - from.level;
    m_secrets.chunkDelta = glm::ivec2(m_exit->x - from.x, m_exit->z - from.z);
    return true;
}

std::string PuzzleChain::offsetText(int floor, int x, int z) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%+d | [%+d, %+d]", floor, x, z);
    return buf;
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
