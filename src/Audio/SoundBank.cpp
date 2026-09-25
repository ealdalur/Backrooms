// ---------------------------------------------------------------------------
// SoundBank.cpp
// Procedural synthesis of every sound effect. Techniques used:
//   * Modal synthesis (damped sinusoids) for struck wood / metal objects.
//   * Pitch-swept sine "thumps" for body and foot impacts.
//   * Filtered, grain-modulated noise for scuffs, breath and friction.
//   * Glottal pulse source + formant band-passes for the vocal grunt.
//   * Stick-slip impulse trains into a resonator bank for hinge creaks.
//   * Offline low-pass + reverb baking for distant, muffled events.
// All generation is deterministic (fixed seeds per sound and variant).
// ---------------------------------------------------------------------------
#include "Audio/SoundBank.h"

#include "Audio/Dsp.h"
#include "Math/Noise.h"
#include "Math/Random.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>

namespace {

using dsp::Biquad;
using Buffer = std::vector<float>;
constexpr float kRate = static_cast<float>(dsp::kSampleRate);

// ----- Buffer helpers ----------------------------------------------------------

size_t samplesFor(float seconds) { return static_cast<size_t>(std::max(0.0f, seconds) * kRate); }
Buffer silence(float seconds) { return Buffer(samplesFor(seconds), 0.0f); }
float timeOf(size_t i) { return static_cast<float>(i) / kRate; }
float smooth01(float e0, float e1, float x) {
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

void applyFilter(Buffer& b, Biquad f) {
    for (float& s : b) s = f.process(s);
}

void normalize(Buffer& b, float peak) {
    float m = 0.0f;
    for (float s : b) m = std::max(m, std::fabs(s));
    if (m < 1e-9f) return;
    const float g = peak / m;
    for (float& s : b) s *= g;
}

/// Scales a short sound so its loudest 50 ms stretch has the given RMS level.
/// Keeps noise-based takes consistently loud, which peak normalisation
/// cannot (a noise burst's peak is essentially random).
void normalizeLoudness(Buffer& b, float targetRms) {
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
void fadeEdges(Buffer& b, float inSeconds, float outSeconds) {
    const size_t fi = std::min(b.size(), samplesFor(inSeconds));
    const size_t fo = std::min(b.size(), samplesFor(outSeconds));
    for (size_t i = 0; i < fi; ++i) b[i] *= static_cast<float>(i) / static_cast<float>(fi);
    for (size_t i = 0; i < fo; ++i) b[b.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(fo);
}

/// Turns a buffer of (loop + crossfade) seconds into a seamless loop: the
/// overshoot is crossfaded into the head, so the last sample flows into the first.
Buffer makeSeamless(const Buffer& b, float crossfadeSeconds) {
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
void addModes(Buffer& b, float at, const Mode* modes, int count, float gain, float jitter, rnd::Rng& rng) {
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
void addThump(Buffer& b, float at, float f0, float f1, float decay, float gain) {
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
void addNoiseBurst(Buffer& b, float at, float attack, float decay, Biquad filter, float gain, Noise& noise,
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
void addNoiseThunk(Buffer& b, Noise& noise, float at, float gain, float cutoffHz, float attack, float decay,
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
void bakeDistance(Buffer& b, float lowpassHz, float roomSize, float damping, float dryMix, float wetMix,
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

// ----- Shared material mode sets --------------------------------------------------

const Mode kDeskModes[] = {{170, 0.05f, 1.0f}, {390, 0.035f, 0.7f}, {720, 0.025f, 0.45f},
                           {1230, 0.018f, 0.3f}, {2250, 0.03f, 0.12f}, {3600, 0.02f, 0.08f}};

// ============================================================================
// Loops
// ============================================================================

std::vector<Sound> makeHum(uint64_t seed) {
    // 8 s holds an integer number of cycles of 60 / 120 Hz and of every LFO
    // (k/8 Hz), so the tonal part is exactly periodic; the hiss is crossfaded.
    const float loop = 8.0f, cf = 0.5f;
    Buffer b = silence(loop + cf);
    Noise noise(seed);
    Biquad hissLp = Biquad::lowpass(2800.0f), hissHp = Biquad::highpass(400.0f);
    const float amps[10] = {1.0f, 0.6f, 0.38f, 0.3f, 0.16f, 0.12f, 0.07f, 0.05f, 0.03f, 0.02f};
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = timeOf(i);
        float s = 0.25f * std::sin(dsp::kTwoPi * 60.0f * t); // transformer core
        for (int h = 1; h <= 10; ++h) {
            const float lfo = 1.0f + 0.15f * std::sin(dsp::kTwoPi * static_cast<float>(h % 3 + 1) / 8.0f * t + h);
            s += amps[h - 1] * lfo * std::sin(dsp::kTwoPi * 120.0f * static_cast<float>(h) * t + 0.7f * h);
        }
        s += 0.05f * hissHp.process(hissLp.process(noise()));
        b[i] = s;
    }
    Sound out{makeSeamless(b, cf), true};
    normalize(out.samples, 0.8f);
    return {out};
}

std::vector<Sound> makeFlickerBuzz(uint64_t seed) {
    // The "bzzzt" of a malfunctioning fluorescent tube. The arc re-ignites on
    // every half-cycle of the 60 Hz mains, so its energy arrives in 120 Hz
    // pulses: a hard-clipped 120 Hz buzz, a spiky 240 Hz magnetostriction
    // rattle from the ballast core, bright arcing sizzle pulsed on each current
    // peak, sparse crackles and an irregular flutter. The game gates each
    // light's copy with that light's flicker, so the buzz lands on every flash.
    // 2 s holds whole cycles of 120 / 240 Hz; the noisy parts are crossfaded.
    const float loop = 2.0f, cf = 0.2f;
    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        const uint64_t flutterSeed = rng.next();
        Buffer b = silence(loop + cf);
        Biquad coreHp = Biquad::highpass(110.0f), coreLp = Biquad::lowpass(4200.0f);
        Biquad body = Biquad::bandpass(rng.range(900.0f, 1600.0f), 1.2f);
        Biquad sizzleA = Biquad::bandpass(rng.range(3800.0f, 5200.0f), 0.9f);
        Biquad sizzleB = Biquad::bandpass(rng.range(6500.0f, 8500.0f), 1.2f);
        Biquad crackleHp = Biquad::highpass(1800.0f);
        const float drive = rng.range(3.0f, 6.0f);
        const float rattlePhase = rng.range(0.0f, dsp::kTwoPi);
        const float crackleDecay = std::exp(-1.0f / (0.0012f * kRate));
        float crackleEnv = 0.0f;
        for (size_t i = 0; i < b.size(); ++i) {
            const float t = timeOf(i);
            const float mains = std::sin(dsp::kTwoPi * 120.0f * t);
            const float core = std::tanh(drive * mains); // clipped: rich odd harmonics
            const float rattle = std::pow(std::sin(dsp::kTwoPi * 240.0f * t + rattlePhase), 9.0f);
            const float peaks = std::pow(std::fabs(mains), 6.0f); // arc current crests
            const float n = noise();
            const float sizzle = (0.7f * sizzleA.process(n) + 0.5f * sizzleB.process(n)) * (0.25f + 0.75f * peaks);
            if (rng.chance(55.0f / kRate)) crackleEnv = rng.range(0.5f, 1.0f);
            const float crackle = crackleHp.process(noise()) * crackleEnv;
            crackleEnv *= crackleDecay;
            const float flutter = 0.65f + 0.35f * noise::value1D(t * 27.0, flutterSeed);
            b[i] = (0.55f * coreLp.process(coreHp.process(core + 0.45f * rattle)) + 0.35f * body.process(core) +
                    0.55f * sizzle + 0.4f * crackle) *
                   flutter;
        }
        Sound s{makeSeamless(b, cf), true};
        normalize(s.samples, 0.85f);
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<Sound> makeDrone(uint64_t seed) {
    // Distant building services: brown-noise rumble, low tonal drones with
    // slow beating, a motor "thrum" swelling in and out, and faint air handling.
    // Every periodic component completes a whole number of cycles in 24 s
    // (frequencies are multiples of 1/24 Hz), so only the noise needs the crossfade.
    const float loop = 24.0f, cf = 3.0f;
    Buffer b = silence(loop + cf);
    Noise noise(seed);
    Biquad rumbleLp = Biquad::lowpass(110.0f), thrumLp = Biquad::lowpass(280.0f), air = Biquad::bandpass(350.0f, 0.4f);
    float brown = 0.0f;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = timeOf(i);
        brown = 0.995f * brown + 0.05f * noise();
        const float rumble = rumbleLp.process(brown) * (0.6f + 0.4f * std::sin(dsp::kTwoPi * t / 12.0f));

        const float drones = 0.5f * std::sin(dsp::kTwoPi * 41.25f * t) + 0.35f * std::sin(dsp::kTwoPi * 55.125f * t) +
                             0.3f * std::sin(dsp::kTwoPi * 55.375f * t) + 0.2f * std::sin(dsp::kTwoPi * 82.5f * t) +
                             0.12f * std::sin(dsp::kTwoPi * 165.0f * t) + 0.06f * std::sin(dsp::kTwoPi * 220.0f * t);

        float saw = 0.0f;
        for (int h = 1; h <= 12; ++h) saw += std::sin(dsp::kTwoPi * 98.0f * static_cast<float>(h) * t) / static_cast<float>(h);
        const float am = 0.5f + 0.5f * std::sin(dsp::kTwoPi * 6.5f * t);
        const float swell = std::pow(std::sin(dsp::kPi * t / 12.0f), 2.0f);
        const float thrum = thrumLp.process(saw * am * am) * swell;

        const float airflow = air.process(noise()) * (0.7f + 0.3f * std::sin(dsp::kTwoPi * t / 8.0f));

        b[i] = 1.6f * rumble + 0.35f * drones + 0.25f * thrum + 0.12f * airflow;
    }
    Sound out{makeSeamless(b, cf), true};
    normalize(out.samples, 0.8f);
    return {out};
}

// ============================================================================
// Player
// ============================================================================

std::vector<Sound> makeFootCarpet(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 6; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.4f);
        const float heel = rng.range(95.0f, 125.0f);
        // Heel strike: muffled thud + a little low noise.
        addThump(b, 0.004f, heel, 50.0f, 0.028f, 1.0f);
        addNoiseBurst(b, 0.0f, 0.002f, 0.018f, Biquad::lowpass(450.0f), 0.55f, noise);
        addNoiseBurst(b, 0.0f, 0.004f, 0.02f, Biquad::bandpass(1200.0f, 0.8f), 0.12f, noise, 0.5f);
        // Roll onto the ball of the foot, dragging fibres (crackly scuff).
        const float toe = rng.range(0.075f, 0.115f);
        addThump(b, toe, heel * 1.15f, 60.0f, 0.02f, 0.45f);
        addNoiseBurst(b, toe - 0.01f, 0.015f, 0.035f, Biquad::bandpass(rng.range(1500.0f, 2200.0f), 0.7f),
                      0.22f, noise, 0.7f);
        applyFilter(b, Biquad::lowpass(3200.0f)); // carpet swallows the highs
        applyFilter(b, Biquad::highpass(35.0f));
        fadeEdges(b, 0.0005f, 0.05f);
        normalize(b, 0.9f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeFootHard(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 4; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.45f);
        addNoiseBurst(b, 0.0f, 0.0005f, 0.003f, Biquad::highpass(2500.0f), 0.35f, noise);
        addThump(b, 0.001f, 110.0f, 70.0f, 0.025f, 0.6f);
        addModes(b, 0.001f, kDeskModes, 6, 0.5f, 0.08f, rng);
        const float toe = rng.range(0.07f, 0.1f);
        addNoiseBurst(b, toe, 0.0005f, 0.003f, Biquad::highpass(2500.0f), 0.2f, noise);
        addModes(b, toe, kDeskModes, 6, 0.3f, 0.08f, rng);
        applyFilter(b, Biquad::lowpass(7000.0f));
        applyFilter(b, Biquad::highpass(50.0f));
        fadeEdges(b, 0.0005f, 0.05f);
        normalize(b, 0.9f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeLandCarpet(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.6f);
        const float gap = rng.range(0.02f, 0.045f); // the two feet rarely land together
        for (int foot = 0; foot < 2; ++foot) {
            const float at = foot * gap;
            const float g = foot == 0 ? 1.0f : 0.8f;
            addThump(b, at, rng.range(85.0f, 100.0f), 38.0f, 0.06f, g);
            addNoiseBurst(b, at, 0.002f, 0.035f, Biquad::lowpass(300.0f), 0.7f * g, noise);
        }
        // Clothing / body rustle as the knees absorb the impact.
        addNoiseBurst(b, 0.005f, 0.01f, 0.09f, Biquad::bandpass(900.0f, 0.6f), 0.25f, noise, 0.6f);
        addNoiseBurst(b, 0.1f, 0.02f, 0.06f, Biquad::bandpass(1400.0f, 0.7f), 0.1f, noise, 0.6f);
        applyFilter(b, Biquad::lowpass(2800.0f));
        applyFilter(b, Biquad::highpass(30.0f));
        fadeEdges(b, 0.0005f, 0.08f);
        normalize(b, 0.95f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeLandHard(uint64_t seed) {
    const Mode ring[] = {{430, 0.25f, 0.25f}, {1120, 0.18f, 0.18f}, {2650, 0.12f, 0.1f}};
    Mode longDesk[6];
    for (int i = 0; i < 6; ++i) longDesk[i] = {kDeskModes[i].hz, kDeskModes[i].decay * 1.6f, kDeskModes[i].amp};

    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.9f);
        const float gap = rng.range(0.02f, 0.045f);
        for (int foot = 0; foot < 2; ++foot) {
            const float at = foot * gap;
            const float g = foot == 0 ? 1.0f : 0.8f;
            addThump(b, at, 80.0f, 45.0f, 0.07f, 0.9f * g);
            addModes(b, at, longDesk, 6, 0.8f * g, 0.08f, rng);
            addNoiseBurst(b, at, 0.0005f, 0.004f, Biquad::highpass(2200.0f), 0.3f * g, noise);
        }
        addModes(b, 0.0f, ring, 3, 1.0f, 0.1f, rng); // the furniture itself rings
        applyFilter(b, Biquad::lowpass(7000.0f));
        applyFilter(b, Biquad::highpass(40.0f));
        fadeEdges(b, 0.0005f, 0.1f);
        normalize(b, 0.95f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeGrunt(uint64_t seed) {
    // An exerted "huh!" as the player pushes off:
    //   * "h": a forceful burst of breath before the voice, turbulent air
    //     already shaped by the vowel (it passes through the same vocal tract);
    //   * "uh": a short, low male vowel under strain: a pressed glottal pulse,
    //     constant cycle-to-cycle irregularity (jitter / shimmer), alternating
    //     strong and weak pulses (the growl of a tight throat), turbulence
    //     riding every pulse, and gentle saturation for grit;
    //   * a brief breath out as the voice cuts off.
    // Pitch only falls and the formants are held steady (a closing glide reads as "oh").
    std::vector<Sound> out;
    for (int v = 0; v < 4; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        const float dur = 0.32f;
        Buffer b = silence(dur);
        const float f0 = rng.range(86.0f, 98.0f);          // at voice onset, falling ~25%: low male
        const float voiceOn = rng.range(0.035f, 0.05f);    // length of the "h" before the voice
        const float drive = rng.range(1.8f, 2.6f);         // saturation (grit)
        // "Huh" vowel formants for a large (male) vocal tract, jaw open with effort.
        Biquad F1 = Biquad::bandpass(rng.range(600.0f, 650.0f), 6.0f);
        Biquad F2 = Biquad::bandpass(rng.range(1220.0f, 1320.0f), 9.0f);
        Biquad F3 = Biquad::bandpass(rng.range(2350.0f, 2500.0f), 13.0f);
        Biquad F4 = Biquad::bandpass(3300.0f, 15.0f);
        Biquad hiss = Biquad::highpass(1500.0f); // broadband part of the breath

        float phase = 0.0f, prevGlottal = 0.0f, jitter = 0.0f;
        float periodScale = 1.0f, pulseAmp = 1.0f;
        bool strongPulse = false;
        for (size_t i = 0; i < b.size(); ++i) {
            const float t = timeOf(i);
            const float tv = t - voiceOn; // time since the voice started
            const bool voiced = tv >= 0.0f;

            float glottal = 0.0f, source = 0.0f;
            if (voiced) {
                const float contour = 1.0f - 0.25f * smooth01(0.0f, dur - voiceOn, tv);
                const float fry = smooth01(0.12f, 0.2f, tv); // creak deepens in the tail
                jitter = 0.999f * jitter + 0.001f * noise();
                phase += f0 * contour * (1.0f + 0.5f * jitter) * periodScale / kRate;
                if (phase >= 1.0f) {
                    // New glottal cycle: rough, strained phonation throughout.
                    phase -= 1.0f;
                    strongPulse = !strongPulse;
                    periodScale = 1.0f / (1.0f + rng.range(-0.015f, 0.015f) +
                                          fry * ((strongPulse ? 0.2f : -0.1f) + rng.range(-0.1f, 0.1f)));
                    pulseAmp = (strongPulse ? 1.0f : rng.range(0.78f, 0.9f)) * (1.0f - fry * rng.range(0.0f, 0.5f));
                }
                // Pressed Rosenberg pulse (short open phase, abrupt closure);
                // its derivative models lip radiation.
                if (phase < 0.4f) glottal = 0.5f * (1.0f - std::cos(dsp::kPi * phase / 0.4f));
                else if (phase < 0.52f) glottal = std::cos(0.5f * dsp::kPi * (phase - 0.4f) / 0.12f);
                source = (glottal - prevGlottal) * 40.0f * pulseAmp;
            }
            prevGlottal = glottal;

            const float voiceEnv =
                voiced ? smooth01(0.0f, 0.006f, tv) * (tv < 0.1f ? 1.0f : std::exp(-(tv - 0.1f) / 0.05f)) : 0.0f;
            const float hBurst = smooth01(0.0f, 0.012f, t) * (1.0f - smooth01(voiceOn - 0.005f, voiceOn + 0.025f, t));
            const float exhale = smooth01(voiceOn + 0.1f, voiceOn + 0.14f, t) *
                                 std::exp(-std::max(0.0f, t - (voiceOn + 0.14f)) / 0.06f);
            const float n = noise();
            const float breath = n * (0.35f * hBurst + 0.16f * glottal * voiceEnv + 0.12f * exhale);

            const float x = source * voiceEnv + breath;
            b[i] = F1.process(x) + 0.5f * F2.process(x) + 0.22f * F3.process(x) + 0.1f * F4.process(x) +
                   0.06f * hiss.process(n) * hBurst;
        }
        // Gentle saturation: strained, gritty harmonics.
        normalize(b, 1.0f);
        const float norm = 1.0f / std::tanh(drive);
        for (float& s : b) s = std::tanh(drive * s) * norm;

        applyFilter(b, Biquad::lowpass(4000.0f));
        applyFilter(b, Biquad::highpass(80.0f));
        fadeEdges(b, 0.001f, 0.04f);
        normalize(b, 0.8f);
        out.push_back({std::move(b), false});
    }
    return out;
}

// ============================================================================
// Doors
// ============================================================================

std::vector<Sound> makeDoorHandle(uint64_t seed) {
    // Commercial lever handle: "chunk-chunk", two dull knocks as the latch bolt
    // snaps inside a solid-core door. Like the shut "thunk", every layer is
    // enveloped, low-passed white noise: no tuned resonances, so no pitch.
    // Knock 1: lever pressed down, bolt retracts. Knock 2: the bolt clears the
    // strike plate as the door starts to move (a little lighter). Each knock is
    // smaller and brighter than the door slam: a quick noise body plus a
    // very short contact burst that gives it a defined edge.
    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.4f);
        // Faint friction of the lever turning against its spring.
        addNoiseBurst(b, 0.0f, 0.03f, 0.025f, Biquad::bandpass(1100.0f, 1.5f), 0.05f, noise, 0.4f);
        const float bodyCutoff = rng.range(450.0f, 650.0f);
        auto knock = [&](float at, float gain) {
            addNoiseThunk(b, noise, at, gain, bodyCutoff, 0.001f, 0.022f, 0.25f, 0.05f); // body
            addNoiseThunk(b, noise, at, 0.35f * gain, 2200.0f, 0.0005f, 0.004f);          // contact
        };
        const float first = rng.range(0.03f, 0.05f);
        const float gap = rng.range(0.10f, 0.14f);
        const float secondGain = rng.range(0.6f, 0.75f);
        knock(first, 1.0f);
        knock(first + gap, secondGain);
        applyFilter(b, Biquad::lowpass(3000.0f)); // nothing bright: a chunk, not a tink
        applyFilter(b, Biquad::highpass(70.0f));
        fadeEdges(b, 0.001f, 0.05f);
        // Consistent loudness across takes; 0.224 matches the average level of
        // the previous (modal) handle, so the tuned kHandleGain keeps its meaning.
        normalizeLoudness(b, 0.224f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeDoorCreak(uint64_t seed) {
    struct Resonance { float hz, q, weight; };
    const Resonance kBank[] = {{340, 12, 1.0f}, {610, 14, 0.8f}, {980, 16, 0.6f}, {1480, 18, 0.45f},
                               {1900, 30, 0.5f}, {2750, 35, 0.35f}, {3900, 40, 0.2f}};
    constexpr int kBankSize = 7;

    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        const float dur = 1.0f;
        Buffer b = silence(dur);

        Biquad bank[kBankSize];
        for (int k = 0; k < kBankSize; ++k) {
            bank[k] = Biquad::bandpass(kBank[k].hz * rng.range(0.9f, 1.1f), kBank[k].q);
        }
        const float baseRate = rng.range(70.0f, 150.0f);
        const float p1 = rng.range(0.0f, 6.28f), p2 = rng.range(0.0f, 6.28f);
        const uint64_t envSeed = rng.next();
        float stickPhase = 0.0f;
        for (size_t i = 0; i < b.size(); ++i) {
            const float t = timeOf(i);
            // Stick-slip: the hinge pin grabs and releases at a wandering rate.
            const float rate = std::max(20.0f, baseRate * (1.0f + 0.55f * std::sin(dsp::kTwoPi * 0.9f * t + p1) +
                                                           0.25f * std::sin(dsp::kTwoPi * 2.3f * t + p2)));
            stickPhase += rate / kRate;
            float impulse = 0.0f;
            if (stickPhase >= 1.0f) {
                stickPhase -= 1.0f;
                if (rng.chance(0.9f)) impulse = 0.6f + 0.4f * rng.nextFloat();
            }
            const float env = std::pow(std::sin(dsp::kPi * t / dur), 0.8f) *
                              (0.55f + 0.45f * noise::value1D(t * 5.0, envSeed));
            const float x = (impulse + 0.03f * noise()) * env;
            float y = 0.0f;
            for (int k = 0; k < kBankSize; ++k) y += kBank[k].weight * bank[k].process(x);
            b[i] = y;
        }
        applyFilter(b, Biquad::lowpass(6000.0f));
        applyFilter(b, Biquad::highpass(150.0f));
        fadeEdges(b, 0.005f, 0.06f);
        normalize(b, 0.8f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeDoorShut(uint64_t seed) {
    // Door shutting: a "thunk" built purely from white noise, low-passed and
    // shaped by an impact envelope. There are no sine sweeps or resonant modes,
    // so it carries no pitch. Every layer is noise:
    //   * the body: dark noise with a fast decay and a small longer tail;
    //   * contact: a brief, brighter burst giving the impact definition;
    //   * settling: a small knock as the door seats against its stop.
    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.6f);
        addNoiseThunk(b, noise, 0.0f, 1.0f, rng.range(260.0f, 380.0f), 0.002f, rng.range(0.045f, 0.065f), 0.2f, 0.14f); // body
        addNoiseThunk(b, noise, 0.0f, 0.25f, 1400.0f, 0.0008f, 0.006f, 0.0f, 0.001f);                                     // contact
        addNoiseThunk(b, noise, rng.range(0.018f, 0.03f), 0.3f, 600.0f, 0.001f, 0.02f, 0.0f, 0.001f);                   // settling
        applyFilter(b, Biquad::highpass(40.0f));
        fadeEdges(b, 0.0005f, 0.08f);
        normalize(b, 0.9f);
        out.push_back({std::move(b), false});
    }
    return out;
}

// ============================================================================
// Distant ambience (muffled through walls, reverb baked in)
// ============================================================================

std::vector<Sound> makeDistantBang(uint64_t seed) {
    const Mode slam[] = {{95, 0.35f, 1.0f}, {182, 0.3f, 0.7f}, {313, 0.25f, 0.55f},
                         {521, 0.2f, 0.4f}, {790, 0.15f, 0.3f}, {1210, 0.1f, 0.2f}};
    std::vector<Sound> out;
    for (int v = 0; v < 4; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.8f);
        addThump(b, 0.0f, 60.0f, 32.0f, 0.12f, 1.0f);
        addModes(b, 0.0f, slam, 6, 0.7f, 0.12f, rng);
        addNoiseBurst(b, 0.0f, 0.001f, 0.03f, Biquad::lowpass(800.0f), 0.6f, noise);
        if (rng.chance(0.5f)) { // something rebounding / a second, smaller hit
            const float at = rng.range(0.12f, 0.3f);
            addThump(b, at, 70.0f, 40.0f, 0.08f, 0.4f);
            addModes(b, at, slam, 6, 0.25f, 0.15f, rng);
        }
        bakeDistance(b, rng.range(350.0f, 700.0f), 0.85f, 0.5f, 0.35f, 2.0f, 2.2f);
        normalize(b, 0.9f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeDistantPounding(uint64_t seed) {
    const Mode panel[] = {{110, 0.1f, 1.0f}, {230, 0.08f, 0.6f}, {390, 0.06f, 0.4f}};
    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        const int count = rng.rangeInt(3, 7);
        Buffer b = silence(0.6f * static_cast<float>(count) + 0.5f);
        float t = 0.0f;
        for (int k = 0; k < count; ++k) {
            const float g = rng.range(0.7f, 1.0f);
            addThump(b, t, 70.0f, 42.0f, 0.07f, g);
            addModes(b, t, panel, 3, 0.6f * g, 0.1f, rng);
            addNoiseBurst(b, t, 0.001f, 0.02f, Biquad::lowpass(500.0f), 0.4f * g, noise);
            // Human rhythm: mostly steady, occasional hesitation.
            t += rng.range(0.28f, 0.45f) + (rng.chance(0.25f) ? rng.range(0.2f, 0.5f) : 0.0f);
        }
        b.resize(samplesFor(t + 0.3f), 0.0f);
        bakeDistance(b, rng.range(380.0f, 500.0f), 0.8f, 0.55f, 0.4f, 1.8f, 1.8f);
        normalize(b, 0.9f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeDistantFootsteps(uint64_t seed) {
    const Mode heel[] = {{1150, 0.02f, 1.0f}, {1850, 0.015f, 0.5f}};
    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        const bool running = v == 2;
        const bool approaching = (v & 1) == 0;
        const int count = rng.rangeInt(7, 12);
        const float interval = running ? rng.range(0.3f, 0.34f) : rng.range(0.52f, 0.6f);
        Buffer b = silence(interval * static_cast<float>(count) + 0.5f);
        for (int k = 0; k < count; ++k) {
            const float progress = static_cast<float>(k) / static_cast<float>(count - 1);
            const float g = (approaching ? 0.25f + 0.75f * progress : 1.0f - 0.8f * progress) * rng.range(0.85f, 1.0f);
            const float at = static_cast<float>(k) * interval + rng.range(-0.02f, 0.02f) + 0.03f;
            addModes(b, at, heel, 2, 0.5f * g, 0.1f, rng);
            addThump(b, at, 120.0f, 80.0f, 0.02f, 0.7f * g);
            addThump(b, at + 0.06f, 130.0f, 90.0f, 0.015f, 0.3f * g); // toe
        }
        bakeDistance(b, rng.range(700.0f, 900.0f), 0.8f, 0.5f, 0.5f, 1.6f, 1.5f);
        normalize(b, 0.85f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeDistantMachinery(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b;
        if (v == 0) {
            // A motor spinning up, running, then winding down.
            const float dur = 3.5f;
            b = silence(dur);
            float phase = 0.0f;
            for (size_t i = 0; i < b.size(); ++i) {
                const float t = timeOf(i);
                const float spin = smooth01(0.0f, 1.0f, t) * (1.0f - smooth01(2.5f, dur, t));
                const float hz = 20.0f + 65.0f * spin;
                phase += hz / kRate;
                float s = 0.0f;
                for (int h = 1; h <= 16; ++h) s += std::sin(dsp::kTwoPi * phase * static_cast<float>(h)) / static_cast<float>(h);
                s += 0.3f * std::sin(dsp::kTwoPi * phase * 7.0f); // gear whine
                b[i] = s * (0.2f + 0.8f * spin);
            }
            addThump(b, 0.0f, 80.0f, 40.0f, 0.1f, 1.5f);
            addThump(b, dur - 0.4f, 70.0f, 35.0f, 0.1f, 1.0f);
        } else if (v == 1) {
            // Heavy metal clanks and a rattling chain.
            const Mode metal[] = {{210, 0.6f, 1.0f}, {470, 0.5f, 0.7f}, {820, 0.4f, 0.5f},
                                  {1330, 0.3f, 0.35f}, {1990, 0.25f, 0.2f}};
            b = silence(2.5f);
            const float hits[3] = {0.0f, rng.range(0.4f, 0.55f), rng.range(0.95f, 1.2f)};
            for (int k = 0; k < 3; ++k) {
                const float g = k == 0 ? 1.0f : rng.range(0.5f, 0.8f);
                addThump(b, hits[k], 65.0f, 38.0f, 0.1f, g);
                addModes(b, hits[k], metal, 5, 0.6f * g, 0.08f, rng);
            }
            const Mode link[] = {{2400, 0.02f, 1.0f}, {3700, 0.015f, 0.6f}};
            for (int k = 0; k < 40; ++k) addModes(b, rng.range(0.2f, 1.0f), link, 2, rng.range(0.03f, 0.1f), 0.2f, rng);
        } else {
            // Groaning pipe: a resonant noise band sweeping slowly, then a water-hammer knock.
            const float dur = 3.0f;
            b = silence(dur);
            Biquad band = Biquad::bandpass(90.0f, 10.0f);
            for (size_t i = 0; i < b.size(); ++i) {
                const float t = timeOf(i);
                if (i % 64 == 0) {
                    const float hz = 90.0f + 60.0f * std::sin(dsp::kPi * t / dur) + 20.0f * std::sin(dsp::kTwoPi * 0.7f * t);
                    band.configure(Biquad::Type::Bandpass, hz, 10.0f);
                }
                b[i] = band.process(noise()) * std::sin(dsp::kPi * t / dur) * 4.0f;
            }
            addThump(b, dur - 0.35f, 90.0f, 50.0f, 0.06f, 1.2f);
        }
        bakeDistance(b, rng.range(600.0f, 900.0f), 0.85f, 0.45f, 0.45f, 1.8f, 2.0f);
        normalize(b, 0.85f);
        out.push_back({std::move(b), false});
    }
    return out;
}

} // namespace

void SoundBank::build() {
    const auto start = std::chrono::steady_clock::now();

    using Generator = std::vector<Sound> (*)(uint64_t);
    const Generator generators[kSoundIdCount] = {
        makeHum,          makeFlickerBuzz, makeDrone,
        makeFootCarpet,   makeFootHard,    makeLandCarpet,  makeLandHard,  makeGrunt,
        makeDoorHandle,   makeDoorCreak,   makeDoorShut,
        makeDistantBang,  makeDistantPounding, makeDistantFootsteps, makeDistantMachinery,
    };

    // Every sound is independent: synthesise them all concurrently.
    std::vector<std::future<std::vector<Sound>>> jobs;
    jobs.reserve(kSoundIdCount);
    for (int i = 0; i < kSoundIdCount; ++i) {
        const uint64_t seed = rnd::splitmix64(0x50D0'0000ull + static_cast<uint64_t>(i));
        jobs.push_back(std::async(std::launch::async, generators[i], seed));
    }
    size_t totalSamples = 0;
    for (int i = 0; i < kSoundIdCount; ++i) {
        m_sounds[static_cast<size_t>(i)] = jobs[static_cast<size_t>(i)].get();
        for (const Sound& s : m_sounds[static_cast<size_t>(i)]) totalSamples += s.samples.size();
    }

    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "[Audio] Synthesised " << kSoundIdCount << " sounds (" << static_cast<int>(totalSamples / kRate)
              << " s of audio) in " << static_cast<int>(ms) << " ms\n";
}

const Sound& SoundBank::get(SoundId id, int variant) const {
    const std::vector<Sound>& variants = m_sounds[static_cast<size_t>(id)];
    return variants[static_cast<size_t>(variant) % variants.size()];
}
