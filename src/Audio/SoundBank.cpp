// ---------------------------------------------------------------------------
// SoundBank.cpp
// Procedural synthesis of every sound effect. Techniques used:
//   * Modal synthesis (damped sinusoids) for struck wood / metal objects.
//   * Pitch-swept sine "thumps" for body and foot impacts.
//   * Filtered, grain-modulated noise for scuffs, breath and friction.
//   * Glottal pulse source + formant band-passes for the vocal grunt.
//   * Stick-slip impulse trains into a resonator bank for hinge creaks.
//   * Offline low-pass + reverb baking for distant, muffled events.
//   * Formant speech synthesis (SpeechSynth) for the Wanderer's voice, then
//     broken up with tape wobble, dropouts, an octave-down double and drive.
//   * Sample-and-hold bit crushing, square waves and motor models for the
//     terminals.
//   * Call-progress tones, DTMF, line noise, the operator and the voices on
//     the line for the office phones (PhoneSounds).
// All generation is deterministic (fixed seeds per sound and variant).
// ---------------------------------------------------------------------------
#include "Audio/SoundBank.h"

#include "Audio/DoomSounds.h"
#include "Audio/Dsp.h"
#include "Audio/PhoneSounds.h"
#include "Audio/StorySounds.h"
#include "Audio/SpeechSynth.h"
#include "Audio/SynthKit.h"
#include "Audio/TeslaSounds.h"
#include "Math/Noise.h"
#include "Math/Random.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <future>
#include <iostream>
#include <string>

namespace {

using namespace synth; // buffer helpers and synthesis primitives

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

// ============================================================================
// The Wanderer
// ============================================================================

std::vector<Sound> makeWandererMutter(uint64_t seed) {
    static const char* const kPhrases[] = {
        "HH EH L P | M IY",               // help me
        "W EH R | AA R | Y UW",           // where are you
        "AY | K AE N | HH IY R | Y UW",   // i can hear you
        "K AH M | B AE K",                // come back
        "D OW N T | L IY V | M IY",       // don't leave me
        "HH UW Z | DH EH R",              // who's there
        "L EH T | M IY | AW T",           // let me out
        "HH AH L OW || HH AH L OW",       // hello... hello
        "IH T S | S OW | D AA R K",       // it's so dark
        "S T EY | W IH DH | M IY",        // stay with me
    };
    std::vector<Sound> out;
    int v = 0;
    for (const char* phrase : kPhrases) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v++)));
        speech::Voice voice;
        voice.seed = rng.next();
        voice.pitchStart = rng.range(78.0f, 90.0f);
        voice.pitchEnd = voice.pitchStart * rng.range(0.72f, 0.82f);
        voice.whisper = rng.range(0.35f, 0.75f);
        voice.breathiness = 0.4f;
        voice.tempo = rng.range(1.25f, 1.6f);
        voice.tract = rng.range(0.88f, 0.95f);
        voice.jitter = 0.035f;
        voice.tremor = 0.6f;
        Buffer b = tapeWow(speech::say(phrase, voice), 0.025f, rng.range(0.4f, 0.9f), rng.next());
        dropouts(b, rng, rng.rangeInt(0, 2));
        applyFilter(b, Biquad::lowpass(4200.0f));
        fadeEdges(b, 0.005f, 0.08f);
        normalize(b, 0.9f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeWandererCry(uint64_t seed) {
    static const char* const kPhrases[] = {
        "~HH EH L P | M IY",              // h- h- help me
        "W EH R | AA R | Y UW",           // WHERE ARE YOU
        "AY | F AW N D | Y UW",           // i found you
        "AY | HH IY R | Y UW",            // i hear you
        "~K AH M | B AE K",               // c- c- come back
        "D OW N T | L IY V | M IY",       // don't leave me
    };
    std::vector<Sound> out;
    int v = 0;
    for (const char* phrase : kPhrases) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v++)));
        speech::Voice voice;
        voice.seed = rng.next();
        voice.pitchStart = rng.range(92.0f, 108.0f);
        voice.pitchEnd = voice.pitchStart * rng.range(0.7f, 0.8f);
        voice.whisper = rng.range(0.05f, 0.2f);
        voice.breathiness = 0.25f;
        voice.tempo = rng.range(1.0f, 1.15f);
        voice.tract = rng.range(0.9f, 0.96f);
        voice.jitter = 0.05f;
        voice.tremor = 1.0f;
        Buffer b = speech::say(phrase, voice);
        // A second voice an octave down, speaking in lockstep (timing does not
        // depend on pitch): something else is talking through it.
        speech::Voice low = voice;
        low.pitchStart *= 0.5f;
        low.pitchEnd *= 0.5f;
        low.tract *= 0.9f;
        const Buffer under = speech::say(phrase, low);
        for (size_t i = 0; i < b.size() && i < under.size(); ++i) b[i] = b[i] + 0.65f * under[i];
        b = tapeWow(b, 0.03f, rng.range(0.6f, 1.2f), rng.next());
        dropouts(b, rng, rng.rangeInt(1, 3));
        const float drive = 2.2f; // strained, overdriven
        for (float& s : b) s = std::tanh(drive * s) / std::tanh(drive);
        applyFilter(b, Biquad::lowpass(5000.0f));
        fadeEdges(b, 0.005f, 0.08f);
        normalize(b, 0.9f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeWandererStep(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 4; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.55f);
        addThump(b, 0.0f, rng.range(65.0f, 80.0f), 38.0f, 0.055f, 1.0f);                        // heel
        addNoiseBurst(b, 0.0f, 0.002f, 0.02f, Biquad::bandpass(1100.0f, 0.9f), 0.35f, noise);   // wet skin slap
        addNoiseBurst(b, rng.range(0.1f, 0.16f), 0.08f, 0.09f, Biquad::bandpass(rng.range(1500.0f, 2200.0f), 0.8f),
                      0.14f, noise, 0.8f);                                                      // the dragging toes
        applyFilter(b, Biquad::lowpass(3000.0f));
        applyFilter(b, Biquad::highpass(35.0f));
        fadeEdges(b, 0.0005f, 0.06f);
        normalize(b, 0.9f);
        out.push_back({std::move(b), false});
    }
    return out;
}

// ============================================================================
// The Stalker
// ============================================================================

std::vector<Sound> makeStalkerSkitter(uint64_t seed) {
    const Mode claw[] = {{2800, 0.004f, 1.0f}, {4600, 0.003f, 0.6f}, {7100, 0.002f, 0.35f}};
    std::vector<Sound> out;
    for (int v = 0; v < 4; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        const float dur = rng.range(0.45f, 0.7f);
        Buffer b = silence(dur + 0.1f);
        // Four-beat gallops of claw clicks with carpet swishes.
        for (float t = 0.0f; t < dur - 0.05f; t += rng.range(0.1f, 0.16f)) {
            for (int k = 0; k < 4; ++k) {
                const float at = t + static_cast<float>(k) * rng.range(0.012f, 0.028f);
                addModes(b, at, claw, 3, rng.range(0.3f, 1.0f), 0.15f, rng);
            }
            addNoiseBurst(b, t, 0.01f, 0.03f, Biquad::bandpass(rng.range(2500.0f, 3500.0f), 0.8f), 0.25f, noise, 0.7f);
        }
        applyFilter(b, Biquad::lowpass(9000.0f));
        applyFilter(b, Biquad::highpass(400.0f));
        fadeEdges(b, 0.001f, 0.04f);
        normalize(b, 0.8f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeStalkerHiss(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        const float dur = 0.7f;
        Buffer b = silence(dur);
        Biquad band = Biquad::bandpass(3500.0f, 2.0f);
        const float from = rng.range(3200.0f, 4200.0f), to = rng.range(1200.0f, 1700.0f);
        for (size_t i = 0; i < b.size(); ++i) {
            const float t = timeOf(i);
            if (i % 64 == 0) band.configure(Biquad::Type::Bandpass, from + (to - from) * smooth01(0.0f, dur, t), 2.0f);
            const float env = smooth01(0.0f, 0.012f, t) * std::exp(-t / 0.22f);
            b[i] = band.process(noise()) * env;
        }
        // A throaty whispered undertone.
        speech::Voice voice;
        voice.seed = rng.next();
        voice.whisper = 1.0f;
        voice.tract = 0.7f;
        voice.tempo = 0.8f;
        const Buffer throat = speech::say("HH AA", voice);
        for (size_t i = 0; i < b.size() && i < throat.size(); ++i) b[i] += 0.35f * throat[i];
        fadeEdges(b, 0.001f, 0.1f);
        normalize(b, 0.85f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeStalkerBreath(uint64_t seed) {
    // Two slow breaths in 4 s: a thin inhale through the teeth, then a long,
    // wet, rattling exhale. Crossfaded into a seamless loop.
    const float loop = 4.0f, cf = 0.5f;
    Buffer b = silence(loop + cf);
    Noise noise(seed);
    Biquad in1 = Biquad::bandpass(2400.0f, 3.0f), out1 = Biquad::bandpass(700.0f, 2.0f), out2 = Biquad::bandpass(1250.0f, 4.0f);
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = timeOf(i);
        const float p = std::fmod(t, 2.0f);
        const float inhale = smooth01(0.0f, 0.25f, p) * (1.0f - smooth01(0.5f, 0.75f, p));
        const float exhale = smooth01(0.8f, 0.95f, p) * (1.0f - smooth01(1.4f, 1.95f, p));
        const float rattle = 0.55f + 0.45f * std::sin(dsp::kTwoPi * 27.0f * t) * std::sin(dsp::kTwoPi * 3.0f * t);
        const float n = noise();
        b[i] = 0.5f * in1.process(n) * inhale + (out1.process(n) + 0.6f * out2.process(n)) * exhale * rattle;
    }
    Sound s{makeSeamless(b, cf), true};
    applyFilter(s.samples, Biquad::highpass(120.0f));
    normalize(s.samples, 0.8f);
    return {s};
}

// ============================================================================
// Dread
// ============================================================================

std::vector<Sound> makeHeartbeat(uint64_t) {
    // Exactly one beat per second: silence at both ends makes it loop cleanly,
    // and the playback rate sets the tempo.
    Buffer b = silence(1.0f);
    addThump(b, 0.02f, 62.0f, 34.0f, 0.055f, 1.0f); // lub
    addThump(b, 0.30f, 58.0f, 36.0f, 0.045f, 0.7f); // dub
    applyFilter(b, Biquad::lowpass(220.0f));
    fadeEdges(b, 0.005f, 0.2f);
    normalize(b, 0.9f);
    return {{std::move(b), true}};
}

std::vector<Sound> makeCatchSting(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 2; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        const float dur = 2.8f;
        Buffer b = silence(dur);
        // A cluster of detuned, bowed-sounding saws that scream upwards.
        const float cluster[6] = {220.0f, 233.1f, 311.1f, 329.6f, 466.2f, 493.9f};
        float phases[6] = {};
        for (size_t i = 0; i < b.size(); ++i) {
            const float t = timeOf(i);
            const float glide = 1.0f + 0.35f * smooth01(0.0f, 0.45f, t);
            const float env = smooth01(0.0f, 0.02f, t) * std::exp(-t / 1.1f);
            float s = 0.0f;
            for (int k = 0; k < 6; ++k) {
                const float vib = 1.0f + 0.012f * std::sin(dsp::kTwoPi * (5.5f + 0.4f * static_cast<float>(k)) * t);
                phases[k] += cluster[k] * rng.range(0.998f, 1.002f) * glide * vib / kRate;
                phases[k] -= std::floor(phases[k]);
                s += (2.0f * phases[k] - 1.0f) * (k < 2 ? 0.8f : 0.5f);
            }
            b[i] = s * env * 0.25f;
        }
        addThump(b, 0.0f, 55.0f, 28.0f, 0.3f, 1.4f);                                        // the boom
        addNoiseBurst(b, 0.0f, 0.003f, 0.12f, Biquad::highpass(1500.0f), 0.5f, noise);        // the crack
        for (float& s : b) s = std::tanh(1.8f * s);
        applyFilter(b, Biquad::lowpass(8000.0f));
        fadeEdges(b, 0.001f, 0.3f);
        normalize(b, 0.95f);
        out.push_back({std::move(b), false});
    }
    return out;
}

// ============================================================================
// Terminals
// ============================================================================

std::vector<Sound> makeTerminalKey(uint64_t seed) {
    // A chunky 1980s keyboard: "tack" as the keycap bottoms out, "tick" as it
    // springs back. Like the door sounds, every layer is enveloped white noise
    // through non-resonant filters (Butterworth high / low passes), so the
    // clicks are percussive and pitchless rather than bell-like.
    std::vector<Sound> out;
    for (int v = 0; v < 6; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.14f);
        // Press: a sharp, bright transient over the dull body of a thick
        // plastic keycap, and a deep knock from the case beneath it.
        addNoiseBurst(b, 0.0005f, 0.0002f, rng.range(0.0012f, 0.0020f), Biquad::highpass(rng.range(2400.0f, 3400.0f)),
                      rng.range(0.8f, 1.0f), noise);                                                       // tack
        addNoiseThunk(b, noise, 0.0005f, 1.0f, rng.range(1000.0f, 1500.0f), 0.0006f, rng.range(0.005f, 0.009f));  // chunk
        addNoiseThunk(b, noise, 0.001f, rng.range(0.45f, 0.65f), rng.range(300.0f, 420.0f), 0.001f, 0.011f);       // thock
        // Release: the switch returns - lighter and brighter.
        const float release = rng.range(0.055f, 0.09f);
        addNoiseBurst(b, release, 0.0002f, 0.0012f, Biquad::highpass(3200.0f), rng.range(0.25f, 0.4f), noise);
        addNoiseThunk(b, noise, release, 0.3f, rng.range(1600.0f, 2200.0f), 0.0005f, 0.004f);
        applyFilter(b, Biquad::highpass(120.0f));
        fadeEdges(b, 0.0003f, 0.02f);
        normalize(b, 0.85f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeTerminalBeep(uint64_t) {
    auto square = [](Buffer& b, float at, float len, float hz) {
        const size_t s0 = samplesFor(at), n = samplesFor(len);
        for (size_t i = 0; i < n && s0 + i < b.size(); ++i) {
            const float t = timeOf(i);
            const float edge = smooth01(0.0f, 0.002f, t) * (1.0f - smooth01(len - 0.002f, len, t));
            b[s0 + i] += (std::sin(dsp::kTwoPi * hz * t) >= 0.0f ? 1.0f : -1.0f) * edge;
        }
    };
    Buffer ok = silence(0.16f);
    square(ok, 0.0f, 0.12f, 880.0f);
    Buffer error = silence(0.3f);
    square(error, 0.0f, 0.1f, 440.0f);
    square(error, 0.16f, 0.1f, 440.0f);
    std::vector<Sound> out;
    for (Buffer* b : {&ok, &error}) {
        applyFilter(*b, Biquad::lowpass(5000.0f));
        normalize(*b, 0.5f);
        out.push_back({std::move(*b), false});
    }
    return out;
}

std::vector<Sound> makeTerminalGlitch(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(rng.range(0.35f, 0.6f));
        // Sample-and-hold noise at a jumping "sample rate", gated in stutters,
        // with a stuck tone underneath.
        size_t hold = 8, held = 0;
        float value = 0.0f, gate = 1.0f;
        const float tone = rng.range(900.0f, 2800.0f);
        for (size_t i = 0; i < b.size(); ++i) {
            if (i % samplesFor(0.02f) == 0) {
                hold = static_cast<size_t>(rng.rangeInt(3, 40));
                gate = rng.chance(0.7f) ? 1.0f : 0.0f;
            }
            if (held++ >= hold) {
                held = 0;
                value = std::round(noise() * 4.0f) / 4.0f; // 3-bit
            }
            b[i] = gate * (0.7f * value + 0.25f * (std::sin(dsp::kTwoPi * tone * timeOf(i)) >= 0.0f ? 1.0f : -1.0f));
        }
        applyFilter(b, Biquad::lowpass(7000.0f));
        fadeEdges(b, 0.002f, 0.05f);
        normalize(b, 0.8f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeTerminalBoot(uint64_t seed) {
    rnd::Rng rng(seed);
    Noise noise(rng.next());
    const float dur = 3.0f;
    Buffer b = silence(dur);
    // POST beep.
    for (size_t i = 0; i < samplesFor(0.12f); ++i) {
        const float t = timeOf(i);
        b[samplesFor(0.05f) + i] += 0.5f * (std::sin(dsp::kTwoPi * 1000.0f * t) >= 0.0f ? 1.0f : -1.0f) *
                                     smooth01(0.0f, 0.002f, t) * (1.0f - smooth01(0.118f, 0.12f, t));
    }
    // Spindle motor spinning up to 5400 rpm (90 Hz) with a rising bearing whine.
    float phase = 0.0f, whine = 0.0f;
    Biquad air = Biquad::lowpass(600.0f);
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = timeOf(i);
        const float spin = smooth01(0.3f, 2.2f, t);
        phase += (15.0f + 75.0f * spin) / kRate;
        whine += (400.0f + 3200.0f * spin) / kRate;
        const float motor = std::sin(dsp::kTwoPi * phase) + 0.4f * std::sin(dsp::kTwoPi * 2.0f * phase);
        b[i] += smooth01(0.25f, 0.5f, t) * (0.25f * motor + 0.05f * std::sin(dsp::kTwoPi * whine) + 0.2f * air.process(noise()) * spin);
    }
    // Head seeks: dry, pitchless ticks (enveloped noise, like the key clicks
    // but smaller) in irregular bursts as the drive reads its boot sectors.
    for (float t = 1.3f; t < 2.8f;) {
        const int burst = rng.rangeInt(2, 5);
        for (int k = 0; k < burst && t < 2.8f; ++k) {
            const float level = rng.range(0.2f, 0.5f);
            addNoiseBurst(b, t, 0.0002f, rng.range(0.0006f, 0.0012f), Biquad::highpass(rng.range(2200.0f, 3200.0f)), level, noise);
            addNoiseThunk(b, noise, t, 0.6f * level, rng.range(700.0f, 1100.0f), 0.0004f, rng.range(0.002f, 0.004f));
            t += rng.range(0.015f, 0.045f);
        }
        t += rng.range(0.08f, 0.3f);
    }
    applyFilter(b, Biquad::highpass(40.0f));
    fadeEdges(b, 0.002f, 0.2f);
    normalize(b, 0.8f);
    return {{std::move(b), false}};
}

std::vector<Sound> makeTerminalOff(uint64_t seed) {
    Noise noise(seed);
    Buffer b = silence(0.8f);
    float phase = 0.0f;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = timeOf(i);
        phase += (9000.0f * std::exp(-t / 0.08f) + 60.0f) / kRate; // the flyback collapsing
        b[i] = 0.3f * std::sin(dsp::kTwoPi * phase) * std::exp(-t / 0.25f);
    }
    addThump(b, 0.0f, 90.0f, 45.0f, 0.05f, 0.8f); // relay
    addNoiseBurst(b, 0.0f, 0.001f, 0.05f, Biquad::highpass(3000.0f), 0.3f, noise); // static discharge
    fadeEdges(b, 0.0005f, 0.1f);
    normalize(b, 0.8f);
    return {{std::move(b), false}};
}

std::vector<Sound> makeCrtHum(uint64_t seed) {
    // 2 s holds whole cycles of 60 Hz and of the 15.734 kHz line frequency.
    const float loop = 2.0f, cf = 0.25f;
    Buffer b = silence(loop + cf);
    Noise noise(seed);
    Biquad staticHp = Biquad::highpass(4000.0f);
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = timeOf(i);
        b[i] = 0.12f * std::sin(dsp::kTwoPi * 15734.0f * t) + 0.5f * std::sin(dsp::kTwoPi * 60.0f * t) +
               0.2f * std::sin(dsp::kTwoPi * 120.0f * t) + 0.04f * staticHp.process(noise());
    }
    Sound s{makeSeamless(b, cf), true};
    normalize(s.samples, 0.6f);
    return {s};
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
        makeWandererMutter, makeWandererCry, makeWandererStep,
        makeStalkerSkitter, makeStalkerHiss, makeStalkerBreath,
        makeHeartbeat,    makeCatchSting,
        makeTerminalKey,  makeTerminalBeep, makeTerminalGlitch, makeTerminalBoot, makeTerminalOff, makeCrtHum,
        doomsfx::makePistol, doomsfx::makeShotgun, doomsfx::makeImpSight, doomsfx::makeTrooperSight,
        doomsfx::makeMonsterPain, doomsfx::makeMonsterDeath, doomsfx::makeClaw, doomsfx::makeFireball,
        doomsfx::makeExplode, doomsfx::makePlayerPain, doomsfx::makePlayerDeath, doomsfx::makeItemUp,
        doomsfx::makeWeaponUp, doomsfx::makeDoor, doomsfx::makeSwitch, doomsfx::makeMusic,
        phonesfx::makeLine, phonesfx::makeDialTone, phonesfx::makeRingback, phonesfx::makeBusy, phonesfx::makeReorder,
        phonesfx::makeHowler, phonesfx::makeDtmf, phonesfx::makeKey, phonesfx::makePickup, phonesfx::makeHangup,
        phonesfx::makeSwitching, phonesfx::makeSit, phonesfx::makeOperator, phonesfx::makeStatic, phonesfx::makeVoice,
        phonesfx::makeBreath, phonesfx::makeJenny, phonesfx::makeBeep, phonesfx::makeMessage,
        teslasfx::makePartPickup, teslasfx::makePartSwap, teslasfx::makeAssembleSnap, teslasfx::makePowerUp,
        teslasfx::makeArcLoop, teslasfx::makeZap, teslasfx::makeDryClick, teslasfx::makeBatteryLow,
        teslasfx::makeCabinetOpen, teslasfx::makeCabinetClose, teslasfx::makeWandererPain, teslasfx::makeWandererDeath,
        teslasfx::makeStalkerPain, teslasfx::makeStalkerDeath, teslasfx::makeVaporize,
        phonesfx::makeClue, phonesfx::makeBell, storysfx::makeNoclipTear, storysfx::makeMonologue,
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

void SoundBank::writeWavFiles(const std::string& directory) const {
    static const char* const kNames[] = {
        "hum", "flicker_buzz", "drone", "foot_carpet", "foot_hard", "land_carpet", "land_hard", "grunt",
        "door_handle", "door_creak", "door_shut", "distant_bang", "distant_pounding", "distant_footsteps",
        "distant_machinery", "wanderer_mutter", "wanderer_cry", "wanderer_step", "stalker_skitter", "stalker_hiss",
        "stalker_breath", "heartbeat", "catch_sting", "terminal_key", "terminal_beep", "terminal_glitch",
        "terminal_boot", "terminal_off", "crt_hum",
        "doom_pistol", "doom_shotgun", "doom_imp_sight", "doom_trooper_sight", "doom_monster_pain",
        "doom_monster_death", "doom_claw", "doom_fireball", "doom_explode", "doom_player_pain", "doom_player_death",
        "doom_item_up", "doom_weapon_up", "doom_door", "doom_switch", "doom_music",
        "phone_line", "phone_dial_tone", "phone_ringback", "phone_busy", "phone_reorder", "phone_howler",
        "phone_dtmf", "phone_key", "phone_pickup", "phone_hangup", "phone_switching", "phone_sit",
        "phone_operator", "phone_static", "phone_voice", "phone_breath", "phone_jenny", "phone_beep", "phone_message",
        "part_pickup", "part_swap", "assemble_snap", "weapon_power_up", "tesla_arc", "tesla_zap", "tesla_dry_click",
        "battery_low", "cabinet_open", "cabinet_close", "wanderer_pain", "wanderer_death", "stalker_pain",
        "stalker_death", "vaporize", "phone_clue", "phone_bell", "noclip_tear", "monologue",
    };
    static_assert(sizeof(kNames) / sizeof(kNames[0]) == kSoundIdCount, "one file name per SoundId");
    auto put16 = [](std::ofstream& f, uint16_t v) { f.put(static_cast<char>(v & 0xFF)).put(static_cast<char>(v >> 8)); };
    auto put32 = [](std::ofstream& f, uint32_t v) {
        for (int i = 0; i < 4; ++i) f.put(static_cast<char>((v >> (8 * i)) & 0xFF));
    };
    for (int id = 0; id < kSoundIdCount; ++id) {
        const std::vector<Sound>& variants = m_sounds[static_cast<size_t>(id)];
        for (size_t v = 0; v < variants.size(); ++v) {
            const Buffer& b = variants[v].samples;
            double sum = 0.0;
            float peak = 0.0f;
            bool finite = true;
            for (float s : b) {
                finite = finite && std::isfinite(s);
                peak = std::max(peak, std::fabs(s));
                sum += static_cast<double>(s) * s;
            }
            const std::string path = directory + "/" + kNames[id] + "_" + std::to_string(v) + ".wav";
            std::ofstream f(path, std::ios::binary);
            if (!f) {
                std::cerr << "[Audio] Cannot write " << path << "\n";
                return;
            }
            const uint32_t bytes = static_cast<uint32_t>(b.size() * 2);
            f.write("RIFF", 4);
            put32(f, 36 + bytes);
            f.write("WAVEfmt ", 8);
            put32(f, 16);
            put16(f, 1);                                    // PCM
            put16(f, 1);                                    // mono
            put32(f, static_cast<uint32_t>(dsp::kSampleRate));
            put32(f, static_cast<uint32_t>(dsp::kSampleRate) * 2);
            put16(f, 2);
            put16(f, 16);
            f.write("data", 4);
            put32(f, bytes);
            for (float s : b) put16(f, static_cast<uint16_t>(static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f)));
            std::printf("[Audio] %-24s %5.2fs  peak %.2f  rms %.3f%s\n", (std::string(kNames[id]) + "_" + std::to_string(v)).c_str(),
                        static_cast<float>(b.size()) / kRate, peak, std::sqrt(sum / std::max<size_t>(1, b.size())),
                        finite ? "" : "  NON-FINITE SAMPLES");
        }
    }
}

const Sound& SoundBank::get(SoundId id, int variant) const {
    const std::vector<Sound>& variants = m_sounds[static_cast<size_t>(id)];
    return variants[static_cast<size_t>(variant) % variants.size()];
}

float SoundBank::duration(SoundId id, int variant) const {
    return static_cast<float>(get(id, variant).samples.size()) / kRate;
}
