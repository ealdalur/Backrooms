#pragma once
// ---------------------------------------------------------------------------
// SynthKit.h
// Offline synthesis toolkit shared by every procedural sound generator
// (SoundBank's world sounds, DoomSounds' terminal game): buffer helpers,
// noise sources, modal / thump / noise-burst primitives and distance baking.
// Everything works on mono float buffers at dsp::kSampleRate.
// ---------------------------------------------------------------------------

#include "Audio/Dsp.h"
#include "Math/Random.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace synth {

using dsp::Biquad;
using Buffer = std::vector<float>;
constexpr float kRate = static_cast<float>(dsp::kSampleRate);

// ----- Buffer helpers ----------------------------------------------------------

inline size_t samplesFor(float seconds) { return static_cast<size_t>(std::max(0.0f, seconds) * kRate); }
inline Buffer silence(float seconds) { return Buffer(samplesFor(seconds), 0.0f); }
inline float timeOf(size_t i) { return static_cast<float>(i) / kRate; }
inline float smooth01(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/// White noise source in [-1, 1].
class Noise {
public:
    explicit Noise(uint64_t seed) : m_rng(seed) {}
    float operator()() { return m_rng.nextFloat() * 2.0f - 1.0f; }

private:
    rnd::Rng m_rng;
};

inline void applyFilter(Buffer& b, Biquad f) {
    for (float& s : b) s = f.process(s);
}

inline void normalize(Buffer& b, float peak) {
    float m = 0.0f;
    for (float s : b) m = std::max(m, std::fabs(s));
    if (m < 1e-9f) return;
    const float g = peak / m;
    for (float& s : b) s *= g;
}

/// Scales a short sound so its loudest 50 ms stretch has the given RMS level.
/// Keeps noise-based takes consistently loud, which peak normalisation
/// cannot (a noise burst's peak is essentially random).
inline void normalizeLoudness(Buffer& b, float targetRms) {
    const size_t w = samplesFor(0.05f);
    double best = 0.0;
    for (size_t a = 0; a + w <= b.size(); a += w / 4) {
        double e = 0.0;
        for (size_t i = a; i < a + w; ++i) e += static_cast<double>(b[i]) * b[i];
        best = std::max(best, std::sqrt(e / static_cast<double>(w)));
    }
    if (best < 1e-9) return;
    const float g = targetRms / static_cast<float>(best);
    for (float& s : b) s *= g;
}

/// Short fades at both ends so no sound starts or stops with a click.
inline void fadeEdges(Buffer& b, float inSeconds, float outSeconds) {
    const size_t fi = std::min(b.size(), samplesFor(inSeconds));
    const size_t fo = std::min(b.size(), samplesFor(outSeconds));
    for (size_t i = 0; i < fi; ++i) b[i] *= static_cast<float>(i) / static_cast<float>(fi);
    for (size_t i = 0; i < fo; ++i) b[b.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(fo);
}

/// Turns a buffer of (loop + crossfade) seconds into a seamless loop: the
/// overshoot is crossfaded into the head, so the last sample flows into the first.
inline Buffer makeSeamless(const Buffer& b, float crossfadeSeconds) {
    const size_t cf = samplesFor(crossfadeSeconds);
    const size_t length = b.size() - cf;
    Buffer out(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(length));
    for (size_t i = 0; i < cf; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(cf);
        out[i] = b[i] * t + b[length + i] * (1.0f - t);
    }
    return out;
}

// ----- Synthesis primitives ------------------------------------------------------

/// A vibration mode of a struck object.
struct Mode {
    float hz;
    float decay; ///< Seconds for the amplitude to fall by 1/e.
    float amp;
};

/// Adds impulse-excited modes (damped sinusoids) starting at `at` seconds.
/// Uses a complex rotator per mode: one multiply-add chain per sample.
inline void addModes(Buffer& b, float at, const Mode* modes, int count, float gain, float jitter, rnd::Rng& rng) {
    const size_t start = samplesFor(at);
    for (int m = 0; m < count; ++m) {
        const float hz = modes[m].hz * (1.0f + jitter * (rng.nextFloat() * 2.0f - 1.0f));
        const float w = dsp::kTwoPi * hz / kRate;
        const float cw = std::cos(w), sw = std::sin(w);
        const float damp = std::exp(-1.0f / (modes[m].decay * kRate));
        float re = 1.0f, im = 0.0f; // phasor starting at sin = 0 (click-free onset)
        float amp = modes[m].amp * gain;
        const size_t len = samplesFor(modes[m].decay * 7.0f);
        for (size_t i = 0; i < len && start + i < b.size(); ++i) {
            b[start + i] += amp * im;
            const float nre = re * cw - im * sw;
            im = re * sw + im * cw;
            re = nre;
            amp *= damp;
        }
    }
}

/// Adds a low "thump": a sine whose pitch glides from f0 down to f1.
inline void addThump(Buffer& b, float at, float f0, float f1, float decay, float gain) {
    const size_t start = samplesFor(at);
    const size_t len = samplesFor(decay * 7.0f);
    float phase = 0.0f;
    for (size_t i = 0; i < len && start + i < b.size(); ++i) {
        const float t = timeOf(i);
        const float hz = f1 + (f0 - f1) * std::exp(-t / (decay * 0.7f));
        phase += dsp::kTwoPi * hz / kRate;
        const float attack = std::min(1.0f, t / 0.0015f); // soften the very first sample
        b[start + i] += gain * attack * std::exp(-t / decay) * std::sin(phase);
    }
}

/// Adds a filtered noise burst with linear attack and exponential decay.
/// `grain` > 0 modulates it with random amplitude "crackle" (fabric, fibres).
inline void addNoiseBurst(Buffer& b, float at, float attack, float decay, Biquad filter, float gain, Noise& noise,
                   float grain = 0.0f) {
    const size_t start = samplesFor(at);
    const size_t len = samplesFor(attack + decay * 7.0f);
    const size_t grainLen = std::max<size_t>(1, samplesFor(0.0015f));
    float grainAmp = 1.0f;
    for (size_t i = 0; i < len && start + i < b.size(); ++i) {
        const float t = timeOf(i);
        if (grain > 0.0f && i % grainLen == 0) grainAmp = 1.0f - grain * (noise() * 0.5f + 0.5f);
        const float env = t < attack ? t / attack : std::exp(-(t - attack) / decay);
        b[start + i] += gain * env * grainAmp * filter.process(noise());
    }
}

/// Adds a pitchless "thunk": white noise through two cascaded Butterworth
/// low-passes (24 dB/oct, no resonant peak at the cutoff, so no pitch), shaped
/// by a smooth attack and a two-stage decay (`tailFraction` of the level
/// decays more slowly with `tailDecay`).
inline void addNoiseThunk(Buffer& b, Noise& noise, float at, float gain, float cutoffHz, float attack, float decay,
                   float tailFraction = 0.0f, float tailDecay = 0.001f) {
    Biquad lp1 = Biquad::lowpass(cutoffHz), lp2 = Biquad::lowpass(cutoffHz);
    const size_t start = samplesFor(at);
    const size_t len = samplesFor(attack + 7.0f * std::max(decay, tailDecay));
    for (size_t i = 0; i < len && start + i < b.size(); ++i) {
        const float t = timeOf(i);
        const float env = t < attack ? smooth01(0.0f, attack, t)
                                     : (1.0f - tailFraction) * std::exp(-(t - attack) / decay) +
                                           tailFraction * std::exp(-(t - attack) / tailDecay);
        b[start + i] += gain * env * lp2.process(lp1.process(noise()));
    }
}

/// Bakes distance: steep low-pass (occlusion through walls) then a big,
/// damped reverb (long carpeted halls). Extends the buffer by `tailSeconds`.
inline void bakeDistance(Buffer& b, float lowpassHz, float roomSize, float damping, float dryMix, float wetMix,
                  float tailSeconds) {
    b.resize(b.size() + samplesFor(tailSeconds), 0.0f);
    Biquad lp1 = Biquad::lowpass(lowpassHz), lp2 = Biquad::lowpass(lowpassHz);
    dsp::Reverb reverb;
    reverb.configure(roomSize, damping);
    for (float& s : b) {
        const float x = lp2.process(lp1.process(s));
        float l = 0.0f, r = 0.0f;
        reverb.process(x, l, r);
        s = x * dryMix + (l + r) * 0.5f * wetMix;
    }
    applyFilter(b, Biquad::highpass(30.0f));
    fadeEdges(b, 0.002f, 0.4f);
}

} // namespace synth
