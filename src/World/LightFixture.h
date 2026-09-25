#pragma once
// ---------------------------------------------------------------------------
// LightFixture.h
// A rectangular fluorescent troffer. Its flicker behaviour (mode, rate, burst
// period and duration, dim level...) is derived solely from a seed that is
// itself hashed from the parent chunk's seed and the fixture index, so every
// light replays the exact same signature whenever its chunk is regenerated.
// ---------------------------------------------------------------------------

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

enum class FlickerMode : uint8_t {
    Steady,       ///< Healthy tube with a barely perceptible shimmer.
    Intermittent, ///< Mostly steady, with periodic bursts of rapid flicker.
    Failing,      ///< Dying ballast: dim, stuttering, occasional full strikes.
    Dead,         ///< Burnt out.
};

class LightFixture {
public:
    /// @param center     Centre of the emitting (downward-facing) surface.
    /// @param halfSize   Half extents of the emitter along world X and Z.
    /// @param seed       Deterministic seed (parent chunk seed + fixture index).
    /// @param chunkUnrest Chunk-wide multiplier (0..1) biasing towards faulty lights.
    LightFixture(const glm::vec3& center, const glm::vec2& halfSize, uint64_t seed, float chunkUnrest);

    /// Relative light output in [0, 1] at absolute simulation time `t` (s).
    float intensity(double t) const;

    /// True while the tube is actively misbehaving at time `t`: inside a
    /// flicker burst (Intermittent) or at any time (Failing). Drives the
    /// malfunction buzz, which should fall quiet between bursts.
    bool isMalfunctioning(double t) const;

    const glm::vec3& center() const { return m_center; }
    const glm::vec2& halfSize() const { return m_halfSize; }
    const glm::vec3& color() const { return m_color; }
    FlickerMode mode() const { return m_mode; }

    /// Global light-grid cells (2.5 m squares) this light can reach without
    /// being blocked by walls. Filled in by the world generator.
    std::vector<glm::ivec2>& visibleCells() { return m_visibleCells; }
    const std::vector<glm::ivec2>& visibleCells() const { return m_visibleCells; }

private:
    /// Whether `t` falls inside an Intermittent flicker burst; also returns
    /// the burst's period index and the time elapsed since it started.
    bool burstAt(double t, int64_t& period, double& inBurst) const;
    float intermittent(double t) const;
    float failing(double t) const;

    glm::vec3   m_center;
    glm::vec2   m_halfSize;
    glm::vec3   m_color;
    uint64_t    m_seed;
    FlickerMode m_mode;

    // Flicker signature parameters (all derived from m_seed).
    float m_burstPeriod;   ///< Seconds between potential flicker bursts.
    float m_burstDuration; ///< Seconds a burst lasts.
    float m_burstChance;   ///< Probability that a given period contains a burst.
    float m_flickerRate;   ///< On/off toggles per second inside a burst.
    float m_dimLevel;      ///< Output while "off" during a flicker.
    float m_threshold;     ///< Duty-cycle threshold of the on/off pattern.
    float m_phase;         ///< Time offset so fixtures are not synchronised.
    float m_shimmer;       ///< Amplitude of the steady-state shimmer.

    std::vector<glm::ivec2> m_visibleCells;
};
