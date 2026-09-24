// ---------------------------------------------------------------------------
// LightFixture.cpp
// Deterministic fluorescent flicker model. Output is a pure function of the
// fixture seed and absolute time, so it is reproducible and needs no state.
// ---------------------------------------------------------------------------
#include "World/LightFixture.h"

#include "Math/Noise.h"
#include "Math/Random.h"

#include <algorithm>
#include <cmath>

namespace {
/// Deterministic unit float for an integer event index of a given stream.
inline float eventRandom(uint64_t seed, int64_t index, uint64_t stream) {
    return rnd::toUnit(rnd::hashCombine(rnd::hashCombine(seed, stream), static_cast<uint64_t>(index)));
}
} // namespace

LightFixture::LightFixture(const glm::vec3& center, const glm::vec2& halfSize, uint64_t seed, float chunkUnrest)
    : m_center(center), m_halfSize(halfSize), m_seed(seed) {
    rnd::Rng rng(seed);

    // Fault probabilities scale with the chunk's "electrical unrest", so some
    // regions of the Backrooms are noticeably more unstable than others.
    const float roll = rng.nextFloat();
    const float pDead     = 0.02f + 0.04f * chunkUnrest;
    const float pFailing  = pDead + 0.03f + 0.07f * chunkUnrest;
    const float pIntermit = pFailing + 0.08f + 0.18f * chunkUnrest;
    if (roll < pDead)          m_mode = FlickerMode::Dead;
    else if (roll < pFailing)  m_mode = FlickerMode::Failing;
    else if (roll < pIntermit) m_mode = FlickerMode::Intermittent;
    else                       m_mode = FlickerMode::Steady;

    m_burstPeriod   = rng.range(2.5f, 14.0f);
    m_burstDuration = rng.range(0.15f, 1.6f);
    m_burstChance   = rng.range(0.55f, 1.0f);
    m_flickerRate   = rng.range(7.0f, 24.0f);
    m_dimLevel      = rng.range(0.0f, 0.35f);
    m_threshold     = rng.range(0.30f, 0.65f);
    m_phase         = rng.range(0.0f, 100.0f);
    m_shimmer       = rng.range(0.008f, 0.03f);

    // Slight colour temperature variation between tubes (aged phosphor).
    const float warm  = rng.range(-1.0f, 1.0f);
    const float green = rng.range(0.0f, 1.0f);
    m_color = glm::vec3(1.0f + 0.03f * warm, 0.97f + 0.02f * green, 0.86f - 0.05f * warm);
}

float LightFixture::intermittent(double t) const {
    const double local = t + m_phase;
    const int64_t period = static_cast<int64_t>(std::floor(local / m_burstPeriod));
    if (eventRandom(m_seed, period, 1) > m_burstChance) return 1.0f; // this period stays calm

    // Burst start jittered within the period.
    const double start = static_cast<double>(period) * m_burstPeriod +
                         eventRandom(m_seed, period, 2) * std::max(0.0f, m_burstPeriod - m_burstDuration);
    const double inBurst = local - start;
    if (inBurst < 0.0 || inBurst >= m_burstDuration) return 1.0f;

    // Quantised on/off pattern inside the burst.
    const int64_t step = static_cast<int64_t>(std::floor(inBurst * m_flickerRate));
    const bool on = eventRandom(m_seed, period * 1024 + step, 3) > m_threshold;
    return on ? 1.0f : m_dimLevel;
}

float LightFixture::failing(double t) const {
    const double local = t + m_phase;
    // Slowly varying "health": phases where the tube struggles more.
    const float health = noise::value1D(local * 0.15, m_seed ^ 0xFA11ull);
    const int64_t step = static_cast<int64_t>(std::floor(local * (m_flickerRate * 0.5f)));
    const float r = eventRandom(m_seed, step, 4);
    if (r < 0.15f + 0.45f * health) {
        // Strike: the arc catches and the tube briefly reaches full output.
        return 1.0f;
    }
    // Dim glowing ends of a dying tube, with a little random wobble.
    return m_dimLevel * 0.5f + 0.08f * eventRandom(m_seed, step, 5);
}

float LightFixture::intensity(double t) const {
    switch (m_mode) {
    case FlickerMode::Dead:
        return 0.0f;
    case FlickerMode::Failing:
        return failing(t);
    case FlickerMode::Intermittent: {
        const float shimmer = 1.0f - m_shimmer * noise::value1D((t + m_phase) * 9.0, m_seed);
        return intermittent(t) * shimmer;
    }
    case FlickerMode::Steady:
    default:
        return 1.0f - m_shimmer * noise::value1D((t + m_phase) * 9.0, m_seed);
    }
}
