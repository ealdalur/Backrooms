// ---------------------------------------------------------------------------
// StorySounds.cpp
// ---------------------------------------------------------------------------
#include "Audio/StorySounds.h"

#include "Audio/SpeechSynth.h"
#include "Audio/SynthKit.h"

#include <algorithm>
#include <cmath>

namespace storysfx {
namespace {

using namespace synth;

struct Phrase {
    const char* phonemes;
    float pitchStart, pitchEnd;
    float pause;   ///< Silence after it (s).
    float whisper; ///< Breath instead of voice.
    float breath;  ///< Breath mixed into the voice.
    float tempo;
};

// Relief, then the doubt creeping in, then resignation. One man throughout:
// the "phew" is his voice too (a breathy sigh in the same register), not a whisper.
const Phrase kMonologue[] = {
    {"F Y UW", 142.0f, 112.0f, 0.35f, 0.12f, 0.55f, 1.0f},                                  // Phew,
    {"B AE K IH N DH AH | R IY L | W ER L D", 138.0f, 110.0f, 0.45f, 0.05f, 0.22f, 0.8f},     // back in the real world.
    {"HH AW L AO NG | HH AE V AY | B IH N | G AA N", 122.0f, 146.0f, 0.55f, 0.05f, 0.22f, 0.8f}, // How long have I been gone?
    {"AY | G EH S", 128.0f, 118.0f, 0.12f, 0.05f, 0.22f, 0.85f},                            // I guess
    {"AY M | S T IH L | AE T | W ER K", 120.0f, 92.0f, 0.0f, 0.08f, 0.25f, 1.0f},             // I'm still at work...
};

/// A-weighting (IEC 61672) at 48 kHz as three biquad sections (b0, b1, b2, a1, a2):
/// the ear's sensitivity across frequencies - a man's voice fundamental counts
/// for little next to its harmonics and breath (-19 dB at 100 Hz, 0 dB at 1 kHz).
constexpr double kAWeighting[3][5] = {
    {2.343017922995e-01, 4.686035845990e-01, 2.343017922995e-01, -2.245584580598e-01, 1.260662527155e-02},
    {1.000000000000e+00, -1.999999966037e+00, 9.999999912869e-01, -1.893870494421e+00, 8.951597688205e-01},
    {1.000000000000e+00, -2.000000033963e+00, 1.000000008713e+00, -1.994614456295e+00, 9.946217073187e-01},
};
static_assert(dsp::kSampleRate == 48000, "the A-weighting coefficients are for 48 kHz");

/// How loud speech sounds: the A-weighted RMS of its voiced stretches (20 ms
/// windows within 30 dB of the loudest; pauses and lead-in silence do not count).
float heardLevel(const Buffer& speech) {
    std::vector<double> b(speech.begin(), speech.end());
    for (const auto& s : kAWeighting) {
        double z1 = 0.0, z2 = 0.0;
        for (double& x : b) {
            const double y = s[0] * x + z1;
            z1 = s[1] * x - s[3] * y + z2;
            z2 = s[2] * x - s[4] * y;
            x = y;
        }
    }
    const size_t win = samplesFor(0.02f);
    std::vector<double> energy;
    for (size_t i = 0; i + win <= b.size(); i += win) {
        double e = 0.0;
        for (size_t k = 0; k < win; ++k) e += b[i + k] * b[i + k];
        energy.push_back(e / static_cast<double>(win));
    }
    if (energy.empty()) return 0.0f;
    const double loudest = *std::max_element(energy.begin(), energy.end());
    double sum = 0.0;
    int count = 0;
    for (double e : energy) {
        if (e < loudest * 1e-3) continue; // pauses and the lead-in silence
        sum += e;
        ++count;
    }
    return count > 0 ? static_cast<float>(std::sqrt(sum / count)) : 0.0f;
}
const char* const kMonologueText = "Phew, back in the real world. How long have I been gone? I guess I'm still at work...";

} // namespace

const char* monologueText() { return kMonologueText; }

std::vector<Sound> makeNoclipTear(uint64_t seed) {
    // A sawtooth screaming up five octaves while the bit depth and sample rate
    // collapse under it; chunks of it stutter, digital shrieks and bursts of
    // noise tear through; at the top, a deep blow - then silence.
    rnd::Rng rng(seed);
    Noise noise(rng.next());
    const float rise = 2.1f, total = 3.6f;
    Buffer b = silence(total);
    double phase = 0.0, phase2 = 0.0;
    for (size_t i = 0; i < samplesFor(rise); ++i) {
        const float t = timeOf(i);
        const double f = 55.0 * std::pow(32.0, t / rise);
        phase += f / kRate;
        phase2 += f * 1.5017 / kRate;
        const float saw = static_cast<float>(2.0 * (phase - std::floor(phase)) - 1.0);
        const float saw2 = static_cast<float>(2.0 * (phase2 - std::floor(phase2)) - 1.0);
        b[i] = (0.45f * saw + 0.3f * saw2) * smooth01(0.0f, 0.3f, t);
    }
    // Collapsing resolution: sample-and-hold and quantisation, worse as it climbs.
    float held = 0.0f;
    for (size_t i = 0; i < samplesFor(rise); ++i) {
        const float t = timeOf(i) / rise;
        const size_t hold = 1 + static_cast<size_t>(24.0f * t * t);
        const float steps = 64.0f * (1.0f - 0.9f * t) + 2.0f;
        if (i % hold == 0) held = std::round(b[i] * steps) / steps;
        b[i] = held;
    }
    // Stutters: short windows replayed over and over.
    for (int s = 0; s < 9; ++s) {
        const size_t len = samplesFor(rng.range(0.02f, 0.07f));
        const size_t from = samplesFor(rng.range(0.2f, rise - 0.3f));
        const int repeats = rng.rangeInt(2, 5);
        for (size_t r = 1; r <= static_cast<size_t>(repeats); ++r) {
            for (size_t i = 0; i < len && from + r * len + i < samplesFor(rise); ++i) b[from + r * len + i] = b[from + i];
        }
    }
    // Shrieks and bursts.
    for (int k = 0; k < 14; ++k) {
        const float at = rng.range(0.1f, rise - 0.05f);
        addNoiseBurst(b, at, 0.001f, rng.range(0.02f, 0.08f), Biquad::bandpass(rng.range(1500.0f, 7000.0f), 3.0f), rng.range(0.3f, 0.8f), noise);
        const float hz = rng.range(2000.0f, 6000.0f);
        for (size_t i = 0; i < samplesFor(0.05f); ++i) {
            const size_t j = samplesFor(at) + i;
            if (j < b.size()) b[j] += 0.25f * std::sin(dsp::kTwoPi * hz * timeOf(i)) * (1.0f - timeOf(i) / 0.05f);
        }
    }
    // The blow: everything stops, and a sub-bass thump rolls away.
    addThump(b, rise + 0.02f, 95.0f, 28.0f, 0.9f, 1.6f);
    addNoiseThunk(b, noise, rise + 0.02f, 1.2f, 300.0f, 0.002f, 0.4f, 0.3f, 0.6f);
    for (float& s : b) s = std::tanh(1.4f * s);
    fadeEdges(b, 0.01f, 0.4f);
    normalize(b, 0.9f);
    return {{std::move(b), false}};
}

std::vector<Sound> makeMonologue(uint64_t seed) {
    // The player, talking to himself: a tired man's voice, close and dry in a
    // quiet room - only a little of the office's air around it.
    rnd::Rng rng(seed);
    speech::Voice v;
    v.breathiness = 0.22f;
    v.tract = 0.98f;
    v.jitter = 0.018f;
    v.tremor = 0.1f;
    float t = 0.15f;
    Buffer b = silence(t);
    for (const Phrase& p : kMonologue) {
        v.seed = rng.next();
        v.pitchStart = p.pitchStart;
        v.pitchEnd = p.pitchEnd;
        v.whisper = p.whisper;
        v.breathiness = p.breath;
        v.tempo = p.tempo;
        const Buffer part = speech::say(p.phonemes, v);
        // say() normalises each phrase by its peak; level them by how loud they sound instead.
        const float gain = 0.1f / std::max(heardLevel(part), 1e-4f);
        const size_t at = samplesFor(t - 0.03f);
        if (b.size() < at + part.size()) b.resize(at + part.size(), 0.0f);
        for (size_t i = 0; i < part.size(); ++i) b[at + i] += gain * part[i];
        t += static_cast<float>(part.size()) / kRate - 0.15f + p.pause;
    }
    b.resize(samplesFor(t + 0.3f), 0.0f);
    bakeDistance(b, 9000.0f, 0.3f, 0.7f, 1.0f, 0.12f, 0.6f);
    fadeEdges(b, 0.01f, 0.2f);
    // The whole line as loud as a voice in the player's head should be (-21 dB, A-weighted).
    const float level = 0.089f / std::max(heardLevel(b), 1e-4f);
    for (float& s : b) s = std::tanh(level * s);
    return {{std::move(b), false}};
}

} // namespace storysfx
