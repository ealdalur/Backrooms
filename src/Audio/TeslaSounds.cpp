// ---------------------------------------------------------------------------
// TeslaSounds.cpp
// Procedural synthesis of the Tesla gun, filing-cabinet and entity-death
// sounds. Techniques: noise-burst clicks and snaps for the gun's mechanism,
// modal synthesis for struck plastic and sheet steel,
// pitch-swept thumps, enveloped / filtered noise bursts, stochastic crackle
// (Poisson-timed micro-bursts), an interrupter-pulse spark train for the
// discharge, exponential chirps for the capacitor whine, formant speech
// (SpeechSynth) amplitude-modulated by the mains-rate buzz of electrocution,
// and swept resonator banks for the Stalker's screech.
// All generation is deterministic (fixed seeds per sound and variant).
// ---------------------------------------------------------------------------
#include "Audio/TeslaSounds.h"

#include "Audio/SpeechSynth.h"
#include "Audio/SynthKit.h"

#include <algorithm>
#include <cmath>

namespace teslasfx {
namespace {

using namespace synth;

// ---- Mode sets ------------------------------------------------------------------------
const Mode kPlasticModes[] = {{920, 0.012f, 1.0f}, {1870, 0.009f, 0.6f}, {3150, 0.006f, 0.4f}, {4700, 0.004f, 0.25f}};
const Mode kPartMetalModes[] = {{1280, 0.05f, 1.0f}, {2710, 0.035f, 0.6f}, {4390, 0.025f, 0.4f}, {6120, 0.018f, 0.25f}};
const Mode kRattleModes[] = {{5200, 0.003f, 1.0f}, {7400, 0.002f, 0.6f}};
const Mode kLatchModes[] = {{3100, 0.006f, 1.0f}, {4800, 0.004f, 0.6f}, {7200, 0.002f, 0.3f}};
const Mode kSurfaceModes[] = {{170, 0.05f, 1.0f}, {390, 0.035f, 0.7f}, {720, 0.025f, 0.45f}, {1230, 0.018f, 0.3f}};
/// A three-drawer steel cabinet body: big, boomy sheet-metal panels.
const Mode kSheetModes[] = {{95, 0.22f, 1.0f},  {158, 0.18f, 0.8f}, {232, 0.15f, 0.7f},  {371, 0.12f, 0.5f},
                            {612, 0.08f, 0.35f}, {1030, 0.05f, 0.2f}, {1740, 0.03f, 0.12f}};
constexpr int kSheetCount = static_cast<int>(sizeof(kSheetModes) / sizeof(kSheetModes[0]));

inline Sound oneShot(Buffer b, float peak) {
    fadeEdges(b, 0.001f, 0.02f);
    normalize(b, peak);
    return {std::move(b), false};
}

/// Stochastic crackle: Poisson-timed micro-bursts of high-passed noise
/// between `t0` and `t1`, at `rate(t)` per second with random strength.
template <typename RateFn>
void addCrackle(Buffer& b, float t0, float t1, RateFn rate, float gain, float hpHz, rnd::Rng& rng, Noise& noise) {
    Biquad hp = Biquad::highpass(hpHz);
    float env = 0.0f, decay = 0.0f;
    const size_t i0 = samplesFor(t0), i1 = std::min(b.size(), samplesFor(t1));
    for (size_t i = i0; i < i1; ++i) {
        const float t = timeOf(i);
        if (rng.chance(rate(t) / kRate)) {
            env = rng.range(0.25f, 1.0f);
            decay = std::exp(-1.0f / (rng.range(0.0006f, 0.003f) * kRate));
        }
        b[i] += gain * env * hp.process(noise());
        env *= decay;
    }
}

/// The electrocution buzz: a mains-rate chop that makes a voice sound like
/// current is running through it.
void electrify(Buffer& b, float depth, float hz, float fadeFrom = 1e9f, float fadeTo = 1e9f) {
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = timeOf(i);
        const float d = depth * (1.0f - smooth01(fadeFrom, fadeTo, t));
        const float chop = 0.5f + 0.5f * std::tanh(6.0f * std::sin(dsp::kTwoPi * hz * t));
        b[i] *= 1.0f - d * chop;
    }
}

/// Grabbing a part off a surface.
void addPickup(Buffer& b, float at, rnd::Rng& rng, Noise& noise) {
    addNoiseBurst(b, at, 0.004f, 0.025f, Biquad::bandpass(rng.range(2500.0f, 4200.0f), 0.8f), 0.35f, noise, 0.5f);
    const float lift = at + rng.range(0.025f, 0.04f);
    addModes(b, lift, kPlasticModes, 4, 0.9f, 0.06f, rng);
    addModes(b, lift + 0.002f, kPartMetalModes, 4, 0.35f, 0.05f, rng);
    addThump(b, lift, 220.0f, 120.0f, 0.02f, 0.4f);
    // Loose bits inside.
    for (int k = rng.rangeInt(3, 6); k > 0; --k) {
        addModes(b, lift + rng.range(0.03f, 0.18f), kRattleModes, 2, rng.range(0.1f, 0.3f), 0.15f, rng);
    }
}

/// Setting a part down on a surface.
void addSetDown(Buffer& b, float at, rnd::Rng& rng, Noise& noise) {
    addNoiseThunk(b, noise, at, 0.8f, 500.0f, 0.002f, 0.03f);
    addModes(b, at, kSurfaceModes, 4, 0.6f, 0.08f, rng);
    addModes(b, at + 0.004f, kPlasticModes, 4, 0.7f, 0.06f, rng);
    for (int k = rng.rangeInt(2, 4); k > 0; --k) {
        addModes(b, at + rng.range(0.01f, 0.08f), kRattleModes, 2, rng.range(0.08f, 0.2f), 0.15f, rng);
    }
}

/// A sine chirp whose frequency follows `hz(t)`, amplitude `amp(t)`, from t0 to t1.
template <typename HzFn, typename AmpFn>
void addChirp(Buffer& b, float t0, float t1, HzFn hz, AmpFn amp) {
    float phase = 0.0f;
    const size_t i0 = samplesFor(t0), i1 = std::min(b.size(), samplesFor(t1));
    for (size_t i = i0; i < i1; ++i) {
        const float t = timeOf(i) - t0;
        phase += dsp::kTwoPi * hz(t) / kRate;
        if (phase > dsp::kTwoPi) phase -= dsp::kTwoPi;
        b[i] += amp(t) * (std::sin(phase) + 0.18f * std::sin(2.0f * phase) + 0.08f * std::sin(3.0f * phase));
    }
}

} // namespace

// ============================================================================
// Handling the parts
// ============================================================================

std::vector<Sound> makePartPickup(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 4; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.45f);
        addPickup(b, 0.0f, rng, noise);
        out.push_back(oneShot(std::move(b), 0.85f));
    }
    return out;
}

std::vector<Sound> makePartSwap(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.75f);
        addSetDown(b, 0.0f, rng, noise);
        addPickup(b, rng.range(0.22f, 0.3f), rng, noise);
        out.push_back(oneShot(std::move(b), 0.85f));
    }
    return out;
}

std::vector<Sound> makeAssembleSnap(uint64_t seed) {
    // Parts made to snap together engaging: a short, gritty "chh" as the
    // mating faces slide over each other, then the "k" of the catch
    // springing home. All filtered noise (no resonant modes, no pitched
    // thumps), so it reads as a click, not as metal struck.
    std::vector<Sound> out;
    for (int v = 0; v < 4; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.35f);
        // "chh": sliding into place - a grainy burst of bright noise.
        const float slide = rng.range(0.025f, 0.045f);
        addNoiseBurst(b, 0.0f, slide * 0.6f, slide * 0.35f, Biquad::bandpass(rng.range(3500.0f, 5500.0f), 0.7f), 0.3f, noise, 0.7f);
        // "k": the catch snaps over - a sharp broadband impulse, a brighter
        // tick a few milliseconds later as it seats fully, and a short,
        // pitchless low thud of the parts meeting.
        const float snap = slide + rng.range(0.004f, 0.012f);
        addNoiseBurst(b, snap, 0.0002f, rng.range(0.0015f, 0.0025f), Biquad::highpass(1500.0f), 1.0f, noise);
        addNoiseBurst(b, snap + rng.range(0.003f, 0.006f), 0.0001f, 0.0008f, Biquad::highpass(4000.0f), 0.45f, noise);
        addNoiseThunk(b, noise, snap, 0.35f, 900.0f, 0.0005f, 0.012f);
        if (v >= 2) { // snapped down with a few ratcheting clicks
            for (int k = 0; k < 4; ++k) {
                const float at = snap + 0.06f + 0.028f * static_cast<float>(k) + rng.range(-0.003f, 0.003f);
                addNoiseBurst(b, at, 0.0001f, 0.001f, Biquad::highpass(2500.0f), 0.45f - 0.07f * static_cast<float>(k), noise);
            }
        }
        out.push_back(oneShot(std::move(b), 0.9f));
    }
    return out;
}

std::vector<Sound> makePowerUp(uint64_t seed) {
    rnd::Rng rng(seed);
    Noise noise(rng.next());
    Buffer b = silence(2.2f);
    // The power relay pulls in.
    addModes(b, 0.0f, kLatchModes, 3, 0.8f, 0.03f, rng);
    addThump(b, 0.0f, 120.0f, 60.0f, 0.03f, 0.6f);
    // Tank capacitors charging: the classic rising whine (an exponential
    // chirp from 300 Hz to 7 kHz), with the driver's hum swelling under it.
    const float rise = 1.5f;
    addChirp(b, 0.05f, 1.95f,
             [rise](float t) { return 300.0f * std::pow(7000.0f / 300.0f, std::pow(std::min(t / rise, 1.0f), 0.7f)); },
             [rise](float t) { return 0.35f * smooth01(0.0f, 0.3f, t) * (1.0f - 0.6f * smooth01(rise, rise + 0.35f, t)); });
    for (size_t i = samplesFor(0.05f); i < b.size(); ++i) {
        const float t = timeOf(i);
        b[i] += 0.12f * smooth01(0.1f, 1.2f, t) * (1.0f - smooth01(1.7f, 2.15f, t)) * std::tanh(3.0f * std::sin(dsp::kTwoPi * 120.0f * t));
    }
    // Ready: a test spark snaps across the spike.
    addNoiseBurst(b, rise + 0.05f, 0.0003f, 0.005f, Biquad::highpass(600.0f), 0.9f, noise);
    addCrackle(b, rise + 0.05f, rise + 0.25f, [](float) { return 260.0f; }, 0.35f, 2000.0f, rng, noise);
    addThump(b, rise + 0.05f, 220.0f, 70.0f, 0.03f, 0.5f);
    applyFilter(b, Biquad::highpass(40.0f));
    return {oneShot(std::move(b), 0.85f)};
}

// ============================================================================
// The discharge
// ============================================================================

std::vector<Sound> makeArcLoop(uint64_t seed) {
    // A solid-state coil sparks once per interrupter pulse: 120 pulses a
    // second, each a sharp broadband crack of random strength (some misfire),
    // so the periodicity reads as a harsh, roaring buzz and the randomness as
    // crackle. Under it the driver's clipped 120 Hz hum; over it, sizzle and
    // the odd big snap. 2 s holds 240 pulses exactly; the noise is crossfaded.
    const float loop = 2.0f, cf = 0.2f;
    rnd::Rng rng(seed);
    Noise noise(rng.next());
    Buffer b = silence(loop + cf);
    const int pulses = static_cast<int>((loop + cf) * 120.0f);
    for (int k = 0; k < pulses; ++k) {
        if (rng.chance(0.08f)) continue; // misfire
        const float at = static_cast<float>(k) / 120.0f + rng.range(-0.0004f, 0.0004f);
        const float strength = rng.range(0.55f, 1.0f);
        addNoiseBurst(b, std::max(0.0f, at), 0.0002f, rng.range(0.0015f, 0.004f), Biquad::highpass(300.0f), strength, noise);
        addNoiseBurst(b, std::max(0.0f, at), 0.0002f, 0.002f, Biquad::bandpass(rng.range(2200.0f, 3800.0f), 1.5f), 0.6f * strength, noise);
    }
    Biquad humLp = Biquad::lowpass(800.0f), sizzleHp = Biquad::highpass(3000.0f);
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = timeOf(i);
        b[i] += 0.22f * humLp.process(std::tanh(4.0f * std::sin(dsp::kTwoPi * 120.0f * t)));
        b[i] += 0.12f * (0.7f + 0.3f * std::sin(dsp::kTwoPi * 3.5f * t)) * sizzleHp.process(noise());
    }
    // Big snaps: a streamer finding somewhere to ground.
    for (int k = 0; k < 12; ++k) {
        const float at = rng.range(0.0f, loop);
        addNoiseBurst(b, at, 0.0003f, rng.range(0.006f, 0.012f), Biquad::highpass(400.0f), rng.range(0.8f, 1.4f), noise);
        addThump(b, at, 200.0f, 80.0f, 0.015f, 0.4f);
    }
    for (float& s : b) s = std::tanh(1.5f * s);
    Sound out{makeSeamless(b, cf), true};
    normalize(out.samples, 0.8f);
    return {out};
}

std::vector<Sound> makeZap(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 4; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.6f);
        addNoiseBurst(b, 0.0f, 0.0003f, 0.004f, Biquad::highpass(500.0f), 1.0f, noise);        // the crack
        addThump(b, 0.0f, 240.0f, 60.0f, 0.03f, 0.6f);                                        // air snapping back
        for (int k = rng.rangeInt(4, 8); k > 0; --k) {                                         // the channel re-striking
            const float at = rng.range(0.003f, 0.12f);
            addNoiseBurst(b, at, 0.0002f, rng.range(0.001f, 0.004f), Biquad::highpass(800.0f), rng.range(0.2f, 0.6f), noise);
        }
        addNoiseBurst(b, 0.01f, 0.01f, 0.07f, Biquad::bandpass(5000.0f, 0.7f), 0.25f, noise, 0.6f); // sizzle
        for (float& s : b) s = std::tanh(2.0f * s);
        out.push_back(oneShot(std::move(b), 0.95f));
    }
    return out;
}

std::vector<Sound> makeDryClick(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        // Nothing but the mechanism: the trigger taking up its slack, breaking
        // with a dry click, and the relay behind it ticking over - short
        // bursts of filtered noise, nothing that rings.
        Buffer b = silence(0.12f);
        addNoiseBurst(b, 0.0f, 0.0001f, 0.0007f, Biquad::highpass(3000.0f), 0.25f, noise);          // take-up
        const float click = rng.range(0.005f, 0.009f);
        addNoiseBurst(b, click, 0.0002f, rng.range(0.0012f, 0.002f), Biquad::highpass(1000.0f), 1.0f, noise); // the click
        addNoiseThunk(b, noise, click, 0.2f, 1200.0f, 0.0003f, 0.005f);                              // a little body
        addNoiseBurst(b, click + rng.range(0.012f, 0.02f), 0.0001f, 0.0009f, Biquad::highpass(3500.0f), 0.3f, noise); // relay tick
        out.push_back(oneShot(std::move(b), 0.8f));
    }
    return out;
}

std::vector<Sound> makeBatteryLow(uint64_t) {
    // Piezo buzzer chirp: bee-boo, square-ish tones (odd harmonics).
    Buffer b = silence(0.32f);
    auto beep = [&b](float at, float dur, float hz) {
        const size_t i0 = samplesFor(at), n = samplesFor(dur);
        for (size_t i = 0; i < n && i0 + i < b.size(); ++i) {
            const float t = timeOf(i);
            const float env = smooth01(0.0f, 0.003f, t) * (1.0f - smooth01(dur - 0.004f, dur, t));
            const float w = dsp::kTwoPi * hz * t;
            b[i0 + i] += env * (std::sin(w) + 0.3f * std::sin(3.0f * w) + 0.12f * std::sin(5.0f * w));
        }
    };
    beep(0.0f, 0.07f, 2950.0f);
    beep(0.11f, 0.09f, 2350.0f);
    return {oneShot(std::move(b), 0.7f)};
}

// ============================================================================
// Filing cabinets
// ============================================================================

std::vector<Sound> makeCabinetOpen(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        const float travel = rng.range(0.38f, 0.46f); // matches the drawer's roll
        Buffer b = silence(travel + 0.6f);
        // The pull is gripped; the drawer breaks free with a rattle of the body.
        addModes(b, 0.0f, kLatchModes, 3, 0.35f, 0.05f, rng);
        addModes(b, 0.01f, kSheetModes, kSheetCount, 0.12f, 0.04f, rng);
        // Rolling out: bearing ticks and a rumble whose rate and level follow
        // the drawer's speed (accelerating, then braking to the stop).
        Biquad rumbleA = Biquad::bandpass(220.0f, 0.9f), rumbleB = Biquad::bandpass(520.0f, 1.2f);
        float tickPhase = 0.0f;
        for (size_t i = 0; i < samplesFor(travel); ++i) {
            const float t = timeOf(i);
            const float speed = std::sin(3.14159f * t / travel);
            const float n = noise();
            b[i] += speed * (0.35f * rumbleA.process(n) + 0.2f * rumbleB.process(n));
            tickPhase += (30.0f + 90.0f * speed) / kRate;
            if (tickPhase >= 1.0f) {
                tickPhase -= 1.0f;
                const Mode ticks[] = {{rng.range(2400.0f, 2900.0f), 0.004f, 1.0f}, {rng.range(3700.0f, 4200.0f), 0.003f, 0.6f}};
                addModes(b, t, ticks, 2, 0.08f * speed, 0.0f, rng);
            }
        }
        // The stop: a clunk that sets the whole cabinet ringing, contents sliding forward.
        addModes(b, travel, kSheetModes, kSheetCount, 0.6f, 0.05f, rng);
        addThump(b, travel, 130.0f, 70.0f, 0.05f, 0.6f);
        addModes(b, travel + 0.002f, kLatchModes, 3, 0.5f, 0.05f, rng);
        addNoiseBurst(b, travel + 0.005f, 0.01f, 0.08f, Biquad::bandpass(2500.0f, 0.6f), 0.12f, noise, 0.4f);
        out.push_back(oneShot(std::move(b), 0.85f));
    }
    return out;
}

std::vector<Sound> makeCabinetClose(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        const float travel = rng.range(0.25f, 0.32f);
        Buffer b = silence(travel + 0.9f);
        // Shoved: rolling faster and faster...
        Biquad rumble = Biquad::bandpass(260.0f, 0.9f);
        float tickPhase = 0.0f;
        for (size_t i = 0; i < samplesFor(travel); ++i) {
            const float t = timeOf(i);
            const float speed = t / travel;
            b[i] += 0.4f * speed * rumble.process(noise());
            tickPhase += (40.0f + 120.0f * speed) / kRate;
            if (tickPhase >= 1.0f) {
                tickPhase -= 1.0f;
                const Mode ticks[] = {{rng.range(2400.0f, 2900.0f), 0.004f, 1.0f}};
                addModes(b, t, ticks, 1, 0.08f * speed, 0.0f, rng);
            }
        }
        // ...into a slam: the body booms, the latch catches, bounces, catches again.
        addModes(b, travel, kSheetModes, kSheetCount, 1.0f, 0.05f, rng);
        addThump(b, travel, 110.0f, 50.0f, 0.08f, 1.0f);
        addNoiseThunk(b, noise, travel, 0.6f, 400.0f, 0.002f, 0.05f);
        addModes(b, travel + 0.004f, kLatchModes, 3, 0.6f, 0.05f, rng);
        addModes(b, travel + rng.range(0.025f, 0.04f), kLatchModes, 3, 0.3f, 0.05f, rng);
        addNoiseBurst(b, travel + 0.01f, 0.01f, 0.1f, Biquad::bandpass(1800.0f, 0.6f), 0.1f, noise, 0.4f); // papers
        out.push_back(oneShot(std::move(b), 0.9f));
    }
    return out;
}

// ============================================================================
// The entities under the arc
// ============================================================================

std::vector<Sound> makeWandererPain(uint64_t seed) {
    // Its own voice (the formant synthesiser, as for its muttering), forced
    // up into a shriek, doubled an octave down, and chopped by the mains-rate
    // buzz of the current - crackling where the arc touches.
    static const char* const kShrieks[] = {"AA AA AA", "AY AA", "~AA AO", "EH AA AA"};
    std::vector<Sound> out;
    int v = 0;
    for (const char* phrase : kShrieks) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v++)));
        Noise noise(rng.next());
        speech::Voice voice;
        voice.seed = rng.next();
        voice.pitchStart = rng.range(200.0f, 250.0f);
        voice.pitchEnd = voice.pitchStart * rng.range(1.2f, 1.45f);
        voice.whisper = 0.1f;
        voice.breathiness = 0.35f;
        voice.tempo = rng.range(1.6f, 2.0f);
        voice.tract = rng.range(0.88f, 0.94f);
        voice.jitter = 0.09f;
        voice.tremor = 1.5f;
        Buffer b = speech::say(phrase, voice);
        speech::Voice low = voice;
        low.pitchStart *= 0.5f;
        low.pitchEnd *= 0.5f;
        low.tract *= 0.9f;
        const Buffer under = speech::say(phrase, low);
        for (size_t i = 0; i < b.size() && i < under.size(); ++i) b[i] += 0.55f * under[i];
        electrify(b, 0.6f, rng.range(55.0f, 65.0f));
        addCrackle(b, 0.0f, timeOf(b.size()), [](float) { return 90.0f; }, 0.35f, 2500.0f, rng, noise);
        for (float& s : b) s = std::tanh(3.0f * s) / std::tanh(3.0f);
        applyFilter(b, Biquad::lowpass(6000.0f));
        out.push_back(oneShot(std::move(b), 0.95f));
    }
    return out;
}

std::vector<Sound> makeWandererDeath(uint64_t seed) {
    rnd::Rng rng(seed);
    Noise noise(rng.next());
    speech::Voice voice;
    voice.seed = rng.next();
    voice.pitchStart = 290.0f;
    voice.pitchEnd = 70.0f;
    voice.whisper = 0.15f;
    voice.breathiness = 0.4f;
    voice.tempo = 3.2f;
    voice.tract = 0.9f;
    voice.jitter = 0.12f;
    voice.tremor = 2.0f;
    Buffer b = speech::say("AA AO OW UW", voice);
    speech::Voice low = voice;
    low.pitchStart *= 0.5f;
    low.pitchEnd *= 0.5f;
    low.tract *= 0.88f;
    const Buffer under = speech::say("AA AO OW UW", low);
    for (size_t i = 0; i < b.size() && i < under.size(); ++i) b[i] += 0.7f * under[i];
    b = tapeWow(b, 0.05f, 0.7f, rng.next()); // the voice sags as it goes
    const float wail = timeOf(b.size());
    electrify(b, 0.65f, 60.0f, 0.3f * wail, wail);
    // It ends in a wet gurgle, and the sizzle carries on after it.
    b.resize(b.size() + samplesFor(1.2f), 0.0f);
    Biquad gurgleLp = Biquad::lowpass(420.0f);
    for (size_t i = samplesFor(wail - 0.2f); i < b.size(); ++i) {
        const float t = timeOf(i) - (wail - 0.2f);
        const float env = smooth01(0.0f, 0.1f, t) * std::exp(-t / 0.45f);
        const float bubble = 0.5f + 0.5f * std::sin(dsp::kTwoPi * (14.0f + 6.0f * std::sin(t * 9.0f)) * t);
        b[i] += 0.8f * env * bubble * gurgleLp.process(noise());
    }
    addCrackle(b, 0.0f, timeOf(b.size()), [wail](float t) { return 140.0f * (1.0f - smooth01(wail * 0.5f, wail + 1.2f, t)) + 10.0f; },
               0.3f, 2500.0f, rng, noise);
    for (float& s : b) s = std::tanh(2.5f * s) / std::tanh(2.5f);
    applyFilter(b, Biquad::lowpass(6500.0f));
    fadeEdges(b, 0.005f, 0.4f);
    normalize(b, 0.95f);
    return {Sound{std::move(b), false}};
}

std::vector<Sound> makeStalkerPain(uint64_t seed) {
    // Not a voice: noise through narrow resonators sweeping upwards, a shrill
    // FM whistle ring-modulated into dissonance, all chopped by the current.
    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        const float dur = rng.range(0.6f, 0.85f);
        Buffer b = silence(dur);
        Biquad r1, r2, r3;
        const float f1 = rng.range(2200.0f, 2600.0f), f2 = rng.range(4500.0f, 5000.0f), f3 = rng.range(1100.0f, 1300.0f);
        const float whistle = rng.range(1650.0f, 1850.0f), ring = rng.range(280.0f, 340.0f);
        float phase = 0.0f;
        for (size_t i = 0; i < b.size(); ++i) {
            const float t = timeOf(i);
            const float u = t / dur;
            if (i % 64 == 0) { // retune the resonators every 64 samples
                r1.configure(Biquad::Type::Bandpass, f1 * (1.0f + 0.5f * u), 12.0f);
                r2.configure(Biquad::Type::Bandpass, f2 * (1.0f + 0.3f * u), 12.0f);
                r3.configure(Biquad::Type::Bandpass, f3 * (1.0f + 0.4f * u), 9.0f);
            }
            const float n = noise();
            const float env = smooth01(0.0f, 0.02f, t) * (1.0f - smooth01(dur - 0.25f, dur, t));
            phase += dsp::kTwoPi * whistle * (1.0f + 0.2f * u) / kRate;
            const float tone = std::sin(phase + 3.0f * std::sin(dsp::kTwoPi * 37.0f * t)) * std::sin(dsp::kTwoPi * ring * t);
            b[i] = env * (2.5f * r1.process(n) + 2.0f * r2.process(n) + 2.0f * r3.process(n) + 0.35f * tone);
        }
        electrify(b, 0.55f, rng.range(55.0f, 65.0f));
        addCrackle(b, 0.0f, dur, [](float) { return 120.0f; }, 0.3f, 3000.0f, rng, noise);
        for (float& s : b) s = std::tanh(2.5f * s);
        out.push_back(oneShot(std::move(b), 0.95f));
    }
    return out;
}

std::vector<Sound> makeStalkerDeath(uint64_t seed) {
    // The screech, split into three detuned voices, falls away and comes
    // apart; what is left of it is steam hissing out.
    rnd::Rng rng(seed);
    Noise noise(rng.next());
    const float dur = 3.0f;
    Buffer b = silence(dur);
    const float detune[3] = {1.0f, 0.96f, 1.05f};
    Biquad res[3], steamBp = Biquad::bandpass(4200.0f, 0.5f), steamHp = Biquad::highpass(2500.0f);
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = timeOf(i);
        const float fall = std::exp(-t / 0.9f);
        if (i % 64 == 0) {
            for (int k = 0; k < 3; ++k) {
                res[k].configure(Biquad::Type::Bandpass, (900.0f + 2700.0f * fall) * detune[k], 10.0f);
            }
        }
        const float n = noise();
        const float scream = smooth01(0.0f, 0.02f, t) * (1.0f - smooth01(0.8f, 1.9f, t));
        float voices = 0.0f;
        for (int k = 0; k < 3; ++k) voices += res[k].process(n);
        const float chop = 1.0f - 0.5f * (1.0f - smooth01(0.0f, 1.2f, t)) * (0.5f + 0.5f * std::tanh(6.0f * std::sin(dsp::kTwoPi * 60.0f * t)));
        const float steam = smooth01(0.3f, 1.2f, t) * (1.0f - smooth01(1.8f, dur, t));
        b[i] = 2.2f * scream * chop * voices + 0.35f * steam * steamBp.process(steamHp.process(noise()));
    }
    addCrackle(b, 0.0f, dur, [](float t) { return 160.0f * std::exp(-t / 1.2f) + 8.0f; }, 0.3f, 2500.0f, rng, noise);
    for (float& s : b) s = std::tanh(2.0f * s);
    fadeEdges(b, 0.002f, 0.3f);
    normalize(b, 0.95f);
    return {Sound{std::move(b), false}};
}

std::vector<Sound> makeVaporize(uint64_t seed) {
    std::vector<Sound> out;
    for (int v = 0; v < 2; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        const float dur = 3.4f;
        Buffer b = silence(dur);
        // A deep whump as it goes up...
        addThump(b, 0.0f, 90.0f, 35.0f, 0.35f, 1.0f);
        addNoiseThunk(b, noise, 0.0f, 0.6f, 300.0f, 0.01f, 0.3f);
        // ...the arc still buzzing through it for a moment...
        for (int k = 0; k < 70; ++k) {
            const float at = static_cast<float>(k) / 120.0f;
            addNoiseBurst(b, at, 0.0002f, 0.002f, Biquad::highpass(400.0f), 0.5f * (1.0f - static_cast<float>(k) / 70.0f), noise);
        }
        // ...a frying crackle, a low roar of burning, and steam hissing out.
        addCrackle(b, 0.0f, dur, [dur](float t) { return 220.0f * (1.0f - t / dur) + 20.0f; }, 0.45f, 2000.0f, rng, noise);
        Biquad roarLp = Biquad::lowpass(300.0f), hissBp = Biquad::bandpass(rng.range(4000.0f, 5000.0f), 0.5f);
        float brown = 0.0f;
        for (size_t i = 0; i < b.size(); ++i) {
            const float t = timeOf(i);
            brown = 0.995f * brown + 0.05f * noise();
            const float roar = smooth01(0.0f, 0.4f, t) * std::exp(-std::max(0.0f, t - 0.4f) / 0.9f);
            const float hiss = smooth01(0.2f, 1.5f, t) * (1.0f - smooth01(1.8f, dur, t));
            b[i] += 0.9f * roar * roarLp.process(brown) + 0.3f * hiss * hissBp.process(noise());
        }
        applyFilter(b, Biquad::highpass(28.0f));
        fadeEdges(b, 0.002f, 0.5f);
        normalize(b, 0.9f);
        out.push_back({std::move(b), false});
    }
    return out;
}

} // namespace teslasfx
