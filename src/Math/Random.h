#pragma once
// ---------------------------------------------------------------------------
// Random.h
// Deterministic hashing and pseudo-random number generation. Everything that
// is procedurally generated (layouts, furniture, flicker signatures, texture
// noise) derives from these functions so that identical inputs always produce
// identical worlds on every platform/compiler.
// ---------------------------------------------------------------------------

#include <cstdint>

namespace rnd {

/// SplitMix64 finaliser: excellent avalanche, used as the base 64-bit mixer.
inline uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

/// Combines an existing hash with another value (order dependent).
inline uint64_t hashCombine(uint64_t h, uint64_t v) {
    return splitmix64(h ^ (v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2)));
}

/// Hashes a signed integer 2D coordinate together with a seed and a salt.
/// The salt separates independent random "streams" for the same coordinate.
inline uint64_t hashCoords(uint64_t seed, int32_t x, int32_t z, uint64_t salt = 0) {
    uint64_t h = splitmix64(seed ^ (salt * 0xD6E8FEB86659FD93ull));
    h = hashCombine(h, static_cast<uint32_t>(x));
    h = hashCombine(h, static_cast<uint32_t>(z));
    return h;
}

/// Maps a 64-bit hash to a float in [0, 1) using its top 24 bits.
inline float toUnit(uint64_t h) {
    return static_cast<float>(h >> 40) * (1.0f / 16777216.0f);
}

/// Fast 32-bit integer hash (lowbias32 by Chris Wellons).
inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/// Hashes two 32-bit lattice coordinates plus a seed (used by texture noise).
inline uint32_t hash2i(int32_t x, int32_t y, uint32_t seed) {
    return hash32(static_cast<uint32_t>(x) * 0x8DA6B343u ^
                  hash32(static_cast<uint32_t>(y) * 0xD8163841u ^ seed));
}

/// PCG32 generator (O'Neill). Small state, statistically strong, fully
/// deterministic and independent of the standard library implementation.
class Rng {
public:
    explicit Rng(uint64_t seed) {
        m_state = 0u;
        m_inc   = (splitmix64(seed ^ 0xDA3E39CB94B95BDBull) << 1u) | 1u;
        next();
        m_state += splitmix64(seed);
        next();
    }

    /// Returns the next uniformly distributed 32-bit value.
    uint32_t next() {
        const uint64_t old = m_state;
        m_state = old * 6364136223846793005ull + m_inc;
        const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    /// Uniform float in [0, 1).
    float nextFloat() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }

    /// Uniform float in [a, b).
    float range(float a, float b) { return a + (b - a) * nextFloat(); }

    /// Uniform integer in [lo, hi] (inclusive).
    int rangeInt(int lo, int hi) {
        if (hi <= lo) return lo;
        const uint32_t span = static_cast<uint32_t>(hi - lo) + 1u;
        return lo + static_cast<int>(next() % span);
    }

    /// Bernoulli trial with probability p.
    bool chance(float p) { return nextFloat() < p; }

private:
    uint64_t m_state;
    uint64_t m_inc;
};

} // namespace rnd
