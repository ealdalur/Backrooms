// ---------------------------------------------------------------------------
// DoomSounds.cpp
// ---------------------------------------------------------------------------
#include "Audio/DoomSounds.h"

#include "Audio/SynthKit.h"
#include "Math/Noise.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <sstream>
#include <string>

namespace doomsfx {
namespace {

using namespace synth;

float saw(float phase) { return 2.0f * (phase - std::floor(phase)) - 1.0f; }
float square(float phase) { return phase - std::floor(phase) < 0.5f ? 1.0f : -1.0f; }

/// The sound path of a 1993 PC: the card's anti-alias filter, sample-and-hold
/// down to `rateHz` and quantisation to `bits`, then a small speaker.
void lofi(Buffer& b, float rateHz = 11025.0f, int bits = 8) {
    applyFilter(b, Biquad::lowpass(rateHz * 0.45f));
    applyFilter(b, Biquad::lowpass(rateHz * 0.45f));
    normalize(b, 0.95f); // use the whole 8-bit range
    const float levels = static_cast<float>((1 << (bits - 1)) - 1);
    const float step = kRate / rateHz;
    float held = 0.0f, acc = step;
    for (float& s : b) {
        if ((acc += 1.0f) >= step) {
            acc -= step;
            held = std::round(s * levels) / levels;
        }
        s = held;
    }
    applyFilter(b, Biquad::lowpass(7000.0f)); // keeps the grit, tames the harshest stairs
}

Sound finish(Buffer& b, float peak = 0.9f) {
    lofi(b);
    fadeEdges(b, 0.0005f, 0.04f);
    normalize(b, peak);
    return {std::move(b), false};
}

// ----- Creature voices -------------------------------------------------------------

/// A snarl, scream or gurgle: a buzzy sawtooth (with breath noise pulsed by
/// the glottis) gliding along a pitch contour through two vowel formants,
/// ring-modulated for an inhuman edge and driven hard.
struct VoiceShape {
    float duration;
    float f0Start, f0End;     ///< Pitch glide (Hz).
    float formant1, formant2; ///< The vowel.
    float rasp;               ///< 0..1 share of noise in the source.
    float ringHz;             ///< Ring modulator frequency (0 = human).
    float wobble;             ///< Random pitch wobble depth (fraction).
    float gurgle;             ///< 0..1 random amplitude flutter (wet, dying).
    float attack, release;
};

Buffer creatureVoice(const VoiceShape& v, rnd::Rng& rng) {
    Buffer b = silence(v.duration);
    Noise noise(rng.next());
    Biquad f1 = Biquad::bandpass(v.formant1, 5.0f), f2 = Biquad::bandpass(v.formant2, 7.0f);
    Biquad body = Biquad::lowpass(v.formant1 * 1.4f);
    const uint64_t wobbleSeed = rng.next(), gurgleSeed = rng.next();
    float phase = 0.0f, ring = 0.0f;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = timeOf(i), u = t / v.duration;
        const float wob = 1.0f + v.wobble * (2.0f * noise::value1D(t * 18.0, wobbleSeed) - 1.0f);
        phase += (v.f0Start + (v.f0End - v.f0Start) * u) * wob / kRate;
        const float glottis = saw(phase);
        const float src = glottis * (1.0f - v.rasp) + noise() * v.rasp * (0.6f + 0.4f * glottis);
        float x = 1.6f * f1.process(src) + 1.1f * f2.process(src) + 0.5f * body.process(src);
        if (v.ringHz > 0.0f) {
            ring += v.ringHz / kRate;
            x *= 0.55f + 0.45f * std::sin(dsp::kTwoPi * ring);
        }
        if (v.gurgle > 0.0f) x *= 1.0f - v.gurgle * noise::value1D(t * 22.0, gurgleSeed);
        const float env = smooth01(0.0f, v.attack, t) * (1.0f - smooth01(v.duration - v.release, v.duration, t));
        b[i] = std::tanh(2.5f * x) * env;
    }
    return b;
}

std::vector<Sound> voices(uint64_t seed, std::initializer_list<VoiceShape> shapes, float thudAt = -1.0f) {
    std::vector<Sound> out;
    int v = 0;
    for (const VoiceShape& shape : shapes) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v++)));
        Buffer b = creatureVoice(shape, rng);
        normalize(b, 0.8f);
        if (thudAt >= 0.0f) {
            // The body hits the floor.
            Noise noise(rng.next());
            b.resize(std::max(b.size(), samplesFor(thudAt + 0.4f)), 0.0f);
            addNoiseThunk(b, noise, thudAt, 0.9f, 320.0f, 0.003f, 0.07f);
            addThump(b, thudAt, 90.0f, 40.0f, 0.05f, 0.6f);
        }
        out.push_back(finish(b));
    }
    return out;
}

/// A dry metallic clack (gun actions, switches).
void addClack(Buffer& b, float at, float pitch, float gain, rnd::Rng& rng, Noise& noise) {
    const Mode metal[] = {{1850.0f * pitch, 0.012f, 1.0f}, {2700.0f * pitch, 0.009f, 0.7f},
                          {4200.0f * pitch, 0.006f, 0.4f}, {620.0f * pitch, 0.02f, 0.5f}};
    addModes(b, at, metal, 4, 0.5f * gain, 0.03f, rng);
    addNoiseBurst(b, at, 0.0005f, 0.012f, Biquad::highpass(1500.0f), 0.4f * gain, noise, 0.4f);
    addNoiseThunk(b, noise, at + 0.004f, 0.3f * gain, 900.0f, 0.001f, 0.02f);
}

/// A rising square-wave chirp (pickups).
void addChirp(Buffer& b, float at, float length, float hz0, float hz1, float gain) {
    const size_t s0 = samplesFor(at);
    float phase = 0.0f;
    for (size_t i = 0; i < samplesFor(length) && s0 + i < b.size(); ++i) {
        const float t = timeOf(i);
        phase += (hz0 + (hz1 - hz0) * t / length) / kRate;
        b[s0 + i] += gain * square(phase) * smooth01(0.0f, 0.002f, t) * (1.0f - smooth01(length * 0.6f, length, t));
    }
}

// ----- FM music -----------------------------------------------------------------------

/// A 2-operator FM voice with modulator self-feedback: the OPL2 chip on
/// every 1993 sound card. The modulation index has its own decay, so notes
/// start bright and mellow as they ring.
struct FmPatch {
    float modRatio = 1.0f;
    float index = 2.0f;        ///< Peak modulation index.
    float indexSustain = 1.0f; ///< Index once its decay has run.
    float indexDecay = 0.1f;   ///< Seconds.
    float feedback = 0.0f;     ///< Modulator self-feedback (OPL "FB"): sine -> saw-like.
    float attack = 0.002f, decay = 0.2f, sustain = 0.6f, release = 0.05f;
    float vibrato = 0.0f;      ///< Pitch vibrato depth (fraction), fades in after 0.15 s.
};

void fmNote(Buffer& b, float at, float length, float hz, const FmPatch& p, float gain) {
    const size_t s0 = samplesFor(at), n = samplesFor(length + p.release * 6.0f);
    float cp = 0.0f, mp = 0.0f, m1 = 0.0f, m2 = 0.0f;
    for (size_t i = 0; i < n && s0 + i < b.size(); ++i) {
        const float t = timeOf(i);
        float env = t < p.attack ? t / p.attack : p.sustain + (1.0f - p.sustain) * std::exp(-(t - p.attack) / p.decay);
        if (t > length) env *= std::exp(-(t - length) / p.release);
        const float f = hz * (1.0f + p.vibrato * smooth01(0.15f, 0.35f, t) * std::sin(dsp::kTwoPi * 5.5f * t));
        mp += f * p.modRatio / kRate;
        cp += f / kRate;
        mp -= std::floor(mp);
        cp -= std::floor(cp);
        const float mod = std::sin(dsp::kTwoPi * mp + p.feedback * 0.5f * (m1 + m2));
        m2 = m1;
        m1 = mod;
        const float index = p.indexSustain + (p.index - p.indexSustain) * std::exp(-t / p.indexDecay);
        b[s0 + i] += gain * env * std::sin(dsp::kTwoPi * cp + index * mod);
    }
}

float noteHz(float semitonesFromE2) { return 82.4069f * std::pow(2.0f, semitonesFromE2 / 12.0f); }

/// A two-bar riff: one token per eighth note. A number is a power chord that
/// many semitones above low E, "x" a palm-muted chug on the open E, "-"
/// holds the previous chord, "." is a rest.
struct Riff {
    const char* notes;
    bool halfTime; ///< Drums: kick and snare on half the beats (the heavy bridge).
};

std::vector<std::string> tokens(const char* s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string t; in >> t;) out.push_back(t);
    return out;
}

/// A lead phrase: (start eighth, length in eighths, semitones above E4).
struct LeadNote {
    int start, length, semitone;
};

} // namespace

// ============================================================================
// Weapons
// ============================================================================

std::vector<Sound> makePistol(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 2; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.45f);
        addNoiseBurst(b, 0.0f, 0.0005f, 0.045f, Biquad::lowpass(rng.range(3000.0f, 4000.0f)), 1.0f, noise);
        addNoiseBurst(b, 0.0f, 0.0003f, 0.012f, Biquad::highpass(2500.0f), 0.6f, noise); // the crack
        addThump(b, 0.0f, 180.0f, 55.0f, 0.035f, 0.9f);
        addNoiseBurst(b, 0.02f, 0.005f, 0.09f, Biquad::lowpass(900.0f), 0.35f, noise);  // the room
        out.push_back(finish(b));
    }
    return out;
}

std::vector<Sound> makeShotgun(uint64_t seed) {
    rnd::Rng rng(seed);
    Noise noise(rng.next());
    Buffer b = silence(1.05f);
    addNoiseBurst(b, 0.0f, 0.0008f, 0.11f, Biquad::lowpass(2200.0f), 1.0f, noise);
    addNoiseBurst(b, 0.0f, 0.0004f, 0.02f, Biquad::highpass(3000.0f), 0.5f, noise);
    addThump(b, 0.0f, 130.0f, 38.0f, 0.08f, 1.2f);
    addNoiseBurst(b, 0.03f, 0.02f, 0.22f, Biquad::lowpass(500.0f), 0.45f, noise); // rumble
    // Pump back, pump forward.
    addClack(b, 0.48f, 1.0f, 0.7f, rng, noise);
    addClack(b, 0.66f, 1.12f, 0.8f, rng, noise);
    return {finish(b)};
}

// ============================================================================
// Monsters
// ============================================================================

std::vector<Sound> makeImpSight(uint64_t seed) {
    // A raspy, rising screech.
    return voices(seed, {{0.80f, 170.0f, 105.0f, 750.0f, 1250.0f, 0.45f, 55.0f, 0.08f, 0.0f, 0.03f, 0.25f},
                         {0.70f, 140.0f, 210.0f, 820.0f, 1400.0f, 0.40f, 70.0f, 0.10f, 0.0f, 0.03f, 0.20f}});
}

std::vector<Sound> makeTrooperSight(uint64_t seed) {
    // A low, gargled "urrgh".
    return voices(seed, {{0.65f, 115.0f, 78.0f, 520.0f, 880.0f, 0.60f, 28.0f, 0.05f, 0.3f, 0.04f, 0.20f},
                         {0.55f, 100.0f, 85.0f, 480.0f, 820.0f, 0.65f, 0.0f, 0.06f, 0.4f, 0.04f, 0.20f}});
}

std::vector<Sound> makeMonsterPain(uint64_t seed) {
    return voices(seed, {{0.32f, 240.0f, 170.0f, 900.0f, 1600.0f, 0.35f, 40.0f, 0.10f, 0.0f, 0.01f, 0.12f},
                         {0.28f, 200.0f, 150.0f, 800.0f, 1450.0f, 0.40f, 55.0f, 0.10f, 0.0f, 0.01f, 0.10f}});
}

std::vector<Sound> makeMonsterDeath(uint64_t seed) {
    // A falling scream that turns into a gurgle, then the body hits the floor.
    return voices(seed, {{0.95f, 180.0f, 55.0f, 650.0f, 1050.0f, 0.50f, 45.0f, 0.12f, 0.6f, 0.02f, 0.40f},
                         {0.85f, 230.0f, 70.0f, 760.0f, 1300.0f, 0.45f, 60.0f, 0.12f, 0.5f, 0.02f, 0.35f}},
                  0.85f);
}

std::vector<Sound> makeClaw(uint64_t seed) {
    // A swipe: noise through a band-pass sweeping up.
    rnd::Rng rng(seed);
    Noise noise(rng.next());
    Buffer b = silence(0.3f);
    Biquad bp;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = timeOf(i);
        if (i % 32 == 0) bp.configure(Biquad::Type::Bandpass, 500.0f + 3500.0f * t / 0.3f, 2.0f);
        b[i] = bp.process(noise()) * smooth01(0.0f, 0.04f, t) * std::exp(-t / 0.08f);
    }
    return {finish(b, 0.8f)};
}

std::vector<Sound> makeFireball(uint64_t seed) {
    // Launch: a roaring whoosh with crackle.
    rnd::Rng rng(seed);
    Noise noise(rng.next());
    Buffer b = silence(0.7f);
    Biquad bp, roar = Biquad::lowpass(260.0f);
    float grain = 1.0f;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = timeOf(i);
        if (i % 32 == 0) bp.configure(Biquad::Type::Bandpass, 300.0f + 1600.0f * smooth01(0.0f, 0.35f, t), 1.5f);
        if (i % 90 == 0) grain = 0.5f + 0.5f * rng.nextFloat();
        const float n = noise();
        const float env = smooth01(0.0f, 0.05f, t) * std::exp(-t / 0.25f);
        b[i] = env * (grain * bp.process(n) * 1.5f + 0.8f * roar.process(n));
    }
    return {finish(b, 0.8f)};
}

std::vector<Sound> makeExplode(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 2; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(1.3f);
        Biquad lp;
        for (size_t i = 0; i < b.size(); ++i) {
            const float t = timeOf(i);
            if (i % 32 == 0) lp.configure(Biquad::Type::Lowpass, 200.0f + 3800.0f * std::exp(-t / 0.12f), 0.8f);
            float crackle = 0.0f;
            if (rng.chance(0.002f * std::exp(-t / 0.4f))) crackle = rng.range(-1.0f, 1.0f) * 3.0f;
            b[i] = (lp.process(noise()) + crackle) * smooth01(0.0f, 0.004f, t) * std::exp(-t / 0.3f);
        }
        addThump(b, 0.0f, 95.0f, 28.0f, 0.2f, 1.4f);
        out.push_back(finish(b));
    }
    return out;
}

// ============================================================================
// Player
// ============================================================================

std::vector<Sound> makePlayerPain(uint64_t seed) {
    return voices(seed, {{0.30f, 150.0f, 110.0f, 620.0f, 1050.0f, 0.12f, 0.0f, 0.03f, 0.0f, 0.01f, 0.10f},
                         {0.26f, 165.0f, 120.0f, 580.0f, 980.0f, 0.15f, 0.0f, 0.03f, 0.0f, 0.01f, 0.10f}});
}

std::vector<Sound> makePlayerDeath(uint64_t seed) {
    return voices(seed, {{1.10f, 170.0f, 65.0f, 700.0f, 1100.0f, 0.25f, 0.0f, 0.06f, 0.5f, 0.02f, 0.50f}}, 1.0f);
}

std::vector<Sound> makeItemUp(uint64_t) {
    Buffer b = silence(0.14f);
    addChirp(b, 0.0f, 0.05f, 900.0f, 1100.0f, 0.8f);
    addChirp(b, 0.05f, 0.08f, 1350.0f, 1500.0f, 0.8f);
    return {finish(b, 0.6f)};
}

std::vector<Sound> makeWeaponUp(uint64_t seed) {
    rnd::Rng rng(seed);
    Noise noise(rng.next());
    Buffer b = silence(0.45f);
    addClack(b, 0.0f, 0.9f, 1.0f, rng, noise);
    addClack(b, 0.13f, 1.05f, 1.0f, rng, noise);
    addChirp(b, 0.2f, 0.06f, 700.0f, 900.0f, 0.4f);
    addChirp(b, 0.26f, 0.1f, 1050.0f, 1300.0f, 0.4f);
    return {finish(b, 0.8f)};
}

// ============================================================================
// World
// ============================================================================

std::vector<Sound> makeDoor(uint64_t seed) {
    // A heavy slab grinding along its track, driven by a labouring motor,
    // ending in a thud.
    rnd::Rng rng(seed);
    Noise noise(rng.next());
    const uint64_t jitterSeed = rng.next();
    const float dur = 1.0f;
    Buffer b = silence(dur + 0.4f);
    Biquad grind = Biquad::lowpass(350.0f), motorLp = Biquad::lowpass(400.0f);
    float phase = 0.0f;
    for (size_t i = 0; i < samplesFor(dur); ++i) {
        const float t = timeOf(i);
        phase += (55.0f + 15.0f * t / dur) / kRate;
        const float jitter = 0.4f + 0.6f * noise::value1D(t * 25.0, jitterSeed);
        const float env = smooth01(0.0f, 0.06f, t) * (1.0f - smooth01(dur - 0.08f, dur, t));
        b[i] = env * (1.4f * jitter * grind.process(noise()) + 0.35f * motorLp.process(saw(phase)));
    }
    addNoiseThunk(b, noise, dur - 0.06f, 0.7f, 250.0f, 0.003f, 0.08f);
    addThump(b, dur - 0.06f, 80.0f, 40.0f, 0.06f, 0.5f);
    return {finish(b, 0.8f)};
}

std::vector<Sound> makeSwitch(uint64_t seed) {
    rnd::Rng rng(seed);
    Noise noise(rng.next());
    Buffer b = silence(0.35f);
    const Mode clunk[] = {{420.0f, 0.03f, 1.0f}, {1100.0f, 0.02f, 0.6f}, {2300.0f, 0.012f, 0.4f}, {3500.0f, 0.008f, 0.3f}};
    addModes(b, 0.0f, clunk, 4, 0.6f, 0.02f, rng);
    addNoiseThunk(b, noise, 0.0f, 0.5f, 700.0f, 0.001f, 0.03f);
    addClack(b, 0.07f, 1.3f, 0.5f, rng, noise);
    return {finish(b, 0.8f)};
}

// ============================================================================
// Music
// ============================================================================

std::vector<Sound> makeMusic(uint64_t seed) {
    // An original riff in the style of the 1993 soundtrack: chugging low E,
    // chromatic blues-metal moves and a phrygian bridge, on FM guitar and
    // bass, FM-style drums and a vibrato lead. 150 BPM, 16 bars.
    const float eighth = 60.0f / 150.0f * 0.5f;
    const Riff kA  = {"x x 3 x x 5 x 6 x x 3 x x 7 6 5", false};
    const Riff kA2 = {"x x 3 x x 5 x 6 x x 10 - 8 - 7 -", false};
    const Riff kB  = {"5 - - - 3 - - - 1 - - - 0 - x x", true};
    const Riff kC  = {"x x x x 12 11 10 9 x x x x 8 7 6 5", false};
    const Riff* song[] = {&kA, &kA2, &kA, &kA2, &kB, &kB, &kA, &kC};
    constexpr int kPatterns = 8, kSteps = 16;
    const float songLength = kPatterns * kSteps * eighth;
    const float tail = 1.5f;

    Buffer guitar = silence(songLength + tail), bass = silence(songLength + tail);
    Buffer drums = silence(songLength + tail), lead = silence(songLength + tail);
    rnd::Rng rng(seed);
    Noise noise(rng.next());

    FmPatch chord;
    chord.modRatio = 1.0f;
    chord.index = 3.0f;
    chord.indexSustain = 1.8f;
    chord.indexDecay = 0.15f;
    chord.feedback = 1.4f;
    chord.decay = 0.6f;
    chord.sustain = 0.55f;
    chord.release = 0.04f;
    FmPatch mute = chord;
    mute.index = 2.2f;
    mute.indexSustain = 0.8f;
    mute.indexDecay = 0.05f;
    mute.feedback = 1.1f;
    mute.decay = 0.07f;
    mute.sustain = 0.1f;
    mute.release = 0.03f;
    FmPatch bassPatch;
    bassPatch.index = 1.8f;
    bassPatch.indexSustain = 1.0f;
    bassPatch.feedback = 0.6f;
    bassPatch.decay = 0.3f;
    bassPatch.sustain = 0.7f;
    FmPatch leadPatch;
    leadPatch.index = 2.0f;
    leadPatch.indexSustain = 1.6f;
    leadPatch.indexDecay = 0.2f;
    leadPatch.feedback = 0.9f;
    leadPatch.attack = 0.01f;
    leadPatch.decay = 0.4f;
    leadPatch.sustain = 0.8f;
    leadPatch.release = 0.08f;
    leadPatch.vibrato = 0.006f;

    auto powerChord = [&](float at, float length, float root) {
        for (float detune : {1.0f, 0.996f}) { // doubled, slightly detuned: two guitars
            fmNote(guitar, at, length, noteHz(root) * detune, chord, 1.0f);
            fmNote(guitar, at, length, noteHz(root + 7.0f) * detune, chord, 0.8f);
            fmNote(guitar, at, length, noteHz(root + 12.0f) * detune, chord, 0.45f);
        }
    };
    auto kick = [&](float at) {
        addThump(drums, at, 160.0f, 48.0f, 0.07f, 1.0f);
        addNoiseBurst(drums, at, 0.0003f, 0.003f, Biquad::highpass(3000.0f), 0.2f, noise);
    };
    auto snare = [&](float at) {
        addNoiseBurst(drums, at, 0.0005f, 0.07f, Biquad::highpass(1500.0f), 0.8f, noise);
        addThump(drums, at, 260.0f, 185.0f, 0.05f, 0.5f);
    };
    auto hat = [&](float at) { addNoiseBurst(drums, at, 0.0003f, 0.018f, Biquad::highpass(7000.0f), 0.25f, noise); };
    auto crash = [&](float at) { addNoiseBurst(drums, at, 0.001f, 0.45f, Biquad::highpass(4500.0f), 0.45f, noise, 0.2f); };
    auto tom = [&](float at, float hz) { addThump(drums, at, hz * 1.6f, hz, 0.12f, 0.8f); };

    for (int p = 0; p < kPatterns; ++p) {
        const float t0 = static_cast<float>(p * kSteps) * eighth;
        const Riff& riff = *song[p];
        const std::vector<std::string> steps = tokens(riff.notes);
        // ---- Guitar and bass.
        for (int s = 0; s < kSteps && s < static_cast<int>(steps.size()); ++s) {
            const std::string& tok = steps[static_cast<size_t>(s)];
            if (tok == "-" || tok == ".") continue;
            int held = 1;
            while (s + held < kSteps && steps[static_cast<size_t>(s + held)] == "-") ++held;
            const float at = t0 + static_cast<float>(s) * eighth;
            if (tok == "x") {
                for (float detune : {1.0f, 0.996f}) fmNote(guitar, at, eighth * 0.55f, noteHz(0.0f) * detune, mute, 1.2f);
                fmNote(bass, at, eighth * 0.8f, noteHz(-12.0f), bassPatch, 1.0f);
            } else {
                const float root = static_cast<float>(std::atoi(tok.c_str()));
                const float length = static_cast<float>(held) * eighth - 0.02f;
                powerChord(at, length, root);
                fmNote(bass, at, length, noteHz(root - 12.0f), bassPatch, 1.0f);
            }
        }
        // ---- Drums.
        if (p == 0 || p == 4 || p == 6) crash(t0);
        const bool fill = p == kPatterns - 1;
        for (int s = 0; s < kSteps; ++s) {
            const float at = t0 + static_cast<float>(s) * eighth;
            const int beat = s % 8;
            if (fill && s >= 12) {
                // Tom fill in sixteenths into the loop point.
                const float toms[] = {220.0f, 190.0f, 160.0f, 130.0f, 110.0f, 95.0f, 85.0f, 75.0f};
                tom(at, toms[(s - 12) * 2]);
                tom(at + eighth * 0.5f, toms[(s - 12) * 2 + 1]);
                continue;
            }
            if (riff.halfTime) {
                if (beat == 0) kick(at);
                if (beat == 4) snare(at);
                if (beat % 2 == 0) hat(at);
            } else {
                if (beat == 0 || beat == 3 || beat == 4 || (s >= 8 && beat == 5)) kick(at);
                if (beat == 2 || beat == 6) snare(at);
                hat(at);
            }
        }
    }

    // ---- Lead: a phrase over the second pass of the main riff, long notes over the bridge.
    const LeadNote phrase[] = {{0, 2, 7},   {2, 2, 8},   {4, 1, 7},   {5, 1, 5},   {6, 2, 3},   {8, 6, 0},
                               {14, 1, -2}, {15, 1, 0},  {16, 2, 3},  {18, 2, 5},  {20, 2, 6},  {22, 2, 7},
                               {24, 4, 12}, {28, 2, 10}, {30, 2, 7}};
    const LeadNote bridge[] = {{0, 8, 12}, {8, 8, 10}, {16, 8, 8}, {24, 6, 7}};
    auto playLead = [&](const LeadNote* notes, size_t count, int pattern) {
        const float t0 = static_cast<float>(pattern * kSteps) * eighth;
        for (size_t i = 0; i < count; ++i) {
            fmNote(lead, t0 + static_cast<float>(notes[i].start) * eighth, static_cast<float>(notes[i].length) * eighth - 0.03f,
                   noteHz(24.0f + static_cast<float>(notes[i].semitone)), leadPatch, 1.0f);
        }
    };
    playLead(phrase, sizeof(phrase) / sizeof(phrase[0]), 2);
    playLead(bridge, sizeof(bridge) / sizeof(bridge[0]), 4);

    // ---- Buses.
    normalize(guitar, 1.0f);
    for (float& s : guitar) s = std::tanh(2.5f * s); // overdrive
    applyFilter(guitar, Biquad::lowpass(3500.0f, 0.9f)); // speaker cabinet
    applyFilter(guitar, Biquad::lowpass(3500.0f, 0.9f));
    applyFilter(guitar, Biquad::highpass(100.0f));
    normalize(bass, 1.0f);
    normalize(drums, 1.0f);
    normalize(lead, 1.0f);
    for (float& s : lead) s = std::tanh(1.5f * s);
    {   // Dotted-eighth echo on the lead.
        const size_t delay = samplesFor(eighth * 1.5f);
        for (size_t i = delay; i < lead.size(); ++i) lead[i] += 0.3f * lead[i - delay];
    }

    Buffer mix(guitar.size(), 0.0f);
    for (size_t i = 0; i < mix.size(); ++i) {
        mix[i] = 0.5f * guitar[i] + 0.28f * bass[i] + 0.5f * drums[i] + 0.3f * lead[i];
    }
    applyFilter(mix, Biquad::highpass(60.0f)); // a small speaker
    applyFilter(mix, Biquad::lowpass(10000.0f));

    // Seamless loop: the tails ringing past the end are wrapped onto the head.
    const size_t length = samplesFor(songLength);
    Buffer loop(mix.begin(), mix.begin() + static_cast<std::ptrdiff_t>(length));
    for (size_t i = length; i < mix.size(); ++i) loop[i - length] += mix[i];
    normalize(loop, 0.8f);
    return {{std::move(loop), true}};
}

} // namespace doomsfx
