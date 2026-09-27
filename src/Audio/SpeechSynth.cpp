// ---------------------------------------------------------------------------
// SpeechSynth.cpp
// ---------------------------------------------------------------------------
#include "Audio/SpeechSynth.h"

#include "Audio/Dsp.h"
#include "Math/Random.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>

namespace speech {
namespace {

constexpr float kRate = static_cast<float>(dsp::kSampleRate);
constexpr float kFrame = 0.001f; ///< Parameter tracks are sampled every millisecond.

enum class Kind : uint8_t { Vowel, Diphthong, Approximant, Nasal, Fricative, Aspirate, Plosive };

/// One phoneme's targets. Formants of 0 mean "keep whatever the neighbours
/// have" (fricatives), diphthongs glide from (f1..f3) to (g1..g3).
struct Phone {
    const char* name;
    Kind  kind;
    float f1, f2, f3;
    float g1, g2, g3;
    float ms;      ///< Nominal duration.
    float av;      ///< Voicing amplitude.
    float af;      ///< Frication amplitude.
    float fricHz;  ///< Frication / burst centre frequency.
    float fricQ;
};

// Formant values for an adult male vocal tract (Peterson & Barney; Klatt 1980).
const Phone kPhones[] = {
    // Vowels.
    {"IY", Kind::Vowel, 270, 2290, 3010, 0, 0, 0, 150, 1.0f, 0, 0, 0},
    {"IH", Kind::Vowel, 390, 1990, 2550, 0, 0, 0, 105, 1.0f, 0, 0, 0},
    {"EH", Kind::Vowel, 530, 1840, 2480, 0, 0, 0, 115, 1.0f, 0, 0, 0},
    {"AE", Kind::Vowel, 660, 1720, 2410, 0, 0, 0, 145, 1.0f, 0, 0, 0},
    {"AA", Kind::Vowel, 730, 1090, 2440, 0, 0, 0, 155, 1.0f, 0, 0, 0},
    {"AO", Kind::Vowel, 570, 840, 2410, 0, 0, 0, 155, 1.0f, 0, 0, 0},
    {"AH", Kind::Vowel, 640, 1190, 2390, 0, 0, 0, 105, 1.0f, 0, 0, 0},
    {"UH", Kind::Vowel, 440, 1020, 2240, 0, 0, 0, 105, 1.0f, 0, 0, 0},
    {"UW", Kind::Vowel, 300, 870, 2240, 0, 0, 0, 155, 1.0f, 0, 0, 0},
    {"ER", Kind::Vowel, 490, 1350, 1690, 0, 0, 0, 145, 1.0f, 0, 0, 0},
    // Diphthongs.
    {"AY", Kind::Diphthong, 730, 1090, 2440, 390, 1990, 2550, 210, 1.0f, 0, 0, 0},
    {"AW", Kind::Diphthong, 730, 1090, 2440, 440, 1020, 2240, 210, 1.0f, 0, 0, 0},
    {"OW", Kind::Diphthong, 520, 900, 2400, 330, 870, 2240, 190, 1.0f, 0, 0, 0},
    {"EY", Kind::Diphthong, 530, 1840, 2480, 300, 2200, 2900, 190, 1.0f, 0, 0, 0},
    {"OY", Kind::Diphthong, 570, 840, 2410, 390, 1990, 2550, 210, 1.0f, 0, 0, 0},
    // Approximants and nasals.
    {"L",  Kind::Approximant, 360, 1100, 2600, 0, 0, 0, 70, 0.85f, 0, 0, 0},
    {"R",  Kind::Approximant, 420, 1300, 1600, 0, 0, 0, 70, 0.85f, 0, 0, 0},
    {"W",  Kind::Approximant, 290, 610, 2150, 0, 0, 0, 65, 0.8f, 0, 0, 0},
    {"Y",  Kind::Approximant, 260, 2070, 3020, 0, 0, 0, 60, 0.8f, 0, 0, 0},
    {"M",  Kind::Nasal, 250, 1100, 2200, 0, 0, 0, 75, 0.6f, 0, 0, 0},
    {"N",  Kind::Nasal, 250, 1700, 2600, 0, 0, 0, 70, 0.6f, 0, 0, 0},
    {"NG", Kind::Nasal, 250, 2300, 2750, 0, 0, 0, 75, 0.6f, 0, 0, 0},
    // Fricatives (formants inherited from the neighbours).
    {"S",  Kind::Fricative, 0, 0, 0, 0, 0, 0, 110, 0.0f, 0.55f, 6200, 1.6f},
    {"Z",  Kind::Fricative, 0, 0, 0, 0, 0, 0, 85, 0.35f, 0.4f, 6000, 1.6f},
    {"SH", Kind::Fricative, 0, 0, 0, 0, 0, 0, 120, 0.0f, 0.6f, 2900, 1.4f},
    {"ZH", Kind::Fricative, 0, 0, 0, 0, 0, 0, 90, 0.35f, 0.45f, 2800, 1.4f},
    {"F",  Kind::Fricative, 0, 0, 0, 0, 0, 0, 100, 0.0f, 0.18f, 4500, 0.5f},
    {"V",  Kind::Fricative, 0, 0, 0, 0, 0, 0, 75, 0.5f, 0.12f, 4200, 0.5f},
    {"TH", Kind::Fricative, 0, 0, 0, 0, 0, 0, 95, 0.0f, 0.14f, 5000, 0.6f},
    {"DH", Kind::Fricative, 0, 0, 0, 0, 0, 0, 60, 0.55f, 0.1f, 4800, 0.6f},
    {"HH", Kind::Aspirate,  0, 0, 0, 0, 0, 0, 70, 0.0f, 0, 0, 0},
    // Plosives: burst centre frequency by place of articulation.
    {"P",  Kind::Plosive, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.7f, 900, 0.7f},
    {"B",  Kind::Plosive, 0, 0, 0, 0, 0, 0, 0, 0.12f, 0.45f, 900, 0.7f},
    {"T",  Kind::Plosive, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.8f, 4200, 1.0f},
    {"D",  Kind::Plosive, 0, 0, 0, 0, 0, 0, 0, 0.12f, 0.5f, 3800, 1.0f},
    {"K",  Kind::Plosive, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.8f, 2100, 1.5f},
    {"G",  Kind::Plosive, 0, 0, 0, 0, 0, 0, 0, 0.12f, 0.5f, 2000, 1.5f},
};

const Phone* findPhone(const std::string& name) {
    for (const Phone& p : kPhones) {
        if (name == p.name) return &p;
    }
    return nullptr;
}

bool hasFormants(const Phone* p) { return p && p->f1 > 0.0f; }

/// Instantaneous synthesis parameters (the tracks).
struct Params {
    float f1 = 500, f2 = 1500, f3 = 2500;
    float av = 0;     ///< Voicing.
    float ah = 0;     ///< Aspiration through the formants.
    float af = 0;     ///< Frication (separate noise channel).
    float fricHz = 4000, fricQ = 1;
};

/// A span of the utterance and where its parameters head.
struct Segment {
    Params from, to;   ///< Targets at the start / end (they differ for diphthongs).
    float  seconds;
    float  formantTau; ///< Coarticulation time constant for the formants.
    float  ampTau;     ///< Time constant for the amplitudes.
};

/// Expands the phoneme string into segments.
std::vector<Segment> plan(const char* text, const Voice& voice) {
    // Tokenise, expanding stutters: "~X ..." repeats from X up to the next vowel.
    std::vector<std::string> tokens;
    {
        std::istringstream in(text);
        std::vector<std::string> raw;
        for (std::string t; in >> t;) raw.push_back(t);
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i][0] != '~') {
                tokens.push_back(raw[i]);
                continue;
            }
            raw[i] = raw[i].substr(1);
            std::vector<std::string> syllable;
            for (size_t j = i; j < raw.size(); ++j) {
                syllable.push_back(raw[j]);
                const Phone* p = findPhone(raw[j]);
                if (p && (p->kind == Kind::Vowel || p->kind == Kind::Diphthong)) break;
            }
            for (int rep = 0; rep < 2; ++rep) {
                tokens.insert(tokens.end(), syllable.begin(), syllable.end());
                tokens.push_back("|");
            }
            tokens.push_back(raw[i]);
        }
    }

    std::vector<Segment> out;
    Params cur;
    auto push = [&](Params to, float ms, float formantTau, float ampTau, const Params* glideTo = nullptr) {
        Segment s;
        s.from = to;
        s.to = glideTo ? *glideTo : to;
        s.seconds = ms * 0.001f * voice.tempo;
        s.formantTau = formantTau;
        s.ampTau = ampTau;
        out.push_back(s);
        cur = s.to;
    };
    auto nextWithFormants = [&](size_t i) -> const Phone* {
        for (size_t j = i + 1; j < tokens.size(); ++j) {
            const Phone* p = findPhone(tokens[j]);
            if (hasFormants(p)) return p;
        }
        return nullptr;
    };
    auto withFormants = [&](Params p, const Phone* ph) {
        if (hasFormants(ph)) {
            p.f1 = ph->f1 * voice.tract;
            p.f2 = ph->f2 * voice.tract;
            p.f3 = ph->f3 * voice.tract;
        }
        return p;
    };
    // The final vowel is drawn out (phrase-final lengthening).
    size_t lastVowel = tokens.size();
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Phone* p = findPhone(tokens[i]);
        if (p && (p->kind == Kind::Vowel || p->kind == Kind::Diphthong)) lastVowel = i;
    }

    push(cur, 30.0f, 0.02f, 0.005f); // lead-in silence
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& tok = tokens[i];
        if (tok == "|" || tok == "||") {
            Params silent = cur;
            silent.av = silent.ah = silent.af = 0.0f;
            push(silent, tok == "|" ? 90.0f : 260.0f, 0.03f, 0.008f);
            continue;
        }
        const Phone* ph = findPhone(tok);
        if (!ph) continue;
        const float lengthen = i == lastVowel ? 1.5f : 1.0f;
        Params t = cur;
        t.av = ph->av;
        t.ah = 0.0f;
        t.af = ph->af;
        if (ph->af > 0.0f) { // only noisy phonemes retune the frication filter
            t.fricHz = ph->fricHz;
            t.fricQ = ph->fricQ;
        }

        switch (ph->kind) {
        case Kind::Vowel:
        case Kind::Approximant:
        case Kind::Nasal:
            push(withFormants(t, ph), ph->ms * lengthen, ph->kind == Kind::Vowel ? 0.025f : 0.018f, 0.008f);
            break;
        case Kind::Diphthong: {
            const Params a = withFormants(t, ph);
            Params b = a;
            b.f1 = ph->g1 * voice.tract;
            b.f2 = ph->g2 * voice.tract;
            b.f3 = ph->g3 * voice.tract;
            push(a, ph->ms * lengthen, 0.03f, 0.008f, &b);
            break;
        }
        case Kind::Fricative:
            push(withFormants(t, nextWithFormants(i)), ph->ms, 0.03f, 0.006f);
            break;
        case Kind::Aspirate: {
            // "h": breath shaped by the vocal tract of the vowel that follows.
            Params h = withFormants(t, nextWithFormants(i));
            h.ah = 0.7f;
            push(h, ph->ms, 0.01f, 0.006f);
            break;
        }
        case Kind::Plosive: {
            // Closure (silent, or a faint voice bar), burst, then aspiration
            // for voiceless stops before a sonorant. The formants already
            // head for the next vowel during the closure (locus transitions).
            const Phone* next = nextWithFormants(i);
            Params closure = withFormants(t, next);
            closure.af = 0.0f;
            push(closure, 55.0f, 0.02f, 0.004f);
            Params burst = closure;
            burst.af = ph->af;
            burst.av = ph->av;
            push(burst, 14.0f, 0.02f, 0.0015f);
            const Phone* follower = i + 1 < tokens.size() ? findPhone(tokens[i + 1]) : nullptr;
            if (ph->av == 0.0f && hasFormants(follower)) {
                Params asp = closure;
                asp.av = 0.0f;
                asp.ah = 0.45f;
                push(asp, 40.0f, 0.02f, 0.006f);
            }
            break;
        }
        }
    }
    Params end = cur;
    end.av = end.ah = end.af = 0.0f;
    push(end, 120.0f, 0.04f, 0.02f); // release into silence
    return out;
}

/// Samples the segment plan into millisecond tracks with exponential
/// smoothing towards each segment's (possibly moving) targets.
std::vector<Params> tracks(const std::vector<Segment>& plan) {
    std::vector<Params> out;
    Params cur = plan.empty() ? Params{} : plan.front().from;
    for (const Segment& s : plan) {
        const int frames = std::max(1, static_cast<int>(s.seconds / kFrame + 0.5f));
        const float kf = 1.0f - std::exp(-kFrame / s.formantTau);
        const float ka = 1.0f - std::exp(-kFrame / s.ampTau);
        for (int f = 0; f < frames; ++f) {
            const float u = static_cast<float>(f) / static_cast<float>(frames);
            const float f1 = s.from.f1 + (s.to.f1 - s.from.f1) * u;
            const float f2 = s.from.f2 + (s.to.f2 - s.from.f2) * u;
            const float f3 = s.from.f3 + (s.to.f3 - s.from.f3) * u;
            cur.f1 += (f1 - cur.f1) * kf;
            cur.f2 += (f2 - cur.f2) * kf;
            cur.f3 += (f3 - cur.f3) * kf;
            cur.av += (s.to.av - cur.av) * ka;
            cur.ah += (s.to.ah - cur.ah) * ka;
            cur.af += (s.to.af - cur.af) * ka;
            cur.fricHz = s.to.fricHz;
            cur.fricQ = s.to.fricQ;
            out.push_back(cur);
        }
    }
    return out;
}

} // namespace

std::vector<float> say(const char* phonemes, const Voice& voice) {
    const std::vector<Params> track = tracks(plan(phonemes, voice));
    const size_t perFrame = static_cast<size_t>(kRate * kFrame);
    const size_t total = track.size() * perFrame;
    std::vector<float> out(total, 0.0f);
    if (total == 0) return out;

    rnd::Rng rng(voice.seed);
    auto noise = [&rng]() { return rng.nextFloat() * 2.0f - 1.0f; };
    using dsp::Biquad;
    Biquad F1 = Biquad::bandpass(500, 7), F2 = Biquad::bandpass(1500, 12), F3 = Biquad::bandpass(2500, 15);
    Biquad F4 = Biquad::bandpass(3300.0f * voice.tract, 14.0f);
    Biquad fric = Biquad::bandpass(4000, 1);

    float phase = 0.0f, prevGlottal = 0.0f, jitter = 0.0f, pulseAmp = 1.0f;
    const float duration = static_cast<float>(total) / kRate;
    for (size_t i = 0; i < total; ++i) {
        const float t = static_cast<float>(i) / kRate;
        // Interpolated parameters for this sample.
        const size_t fi = std::min(i / perFrame, track.size() - 1);
        const size_t fj = std::min(fi + 1, track.size() - 1);
        const float u = static_cast<float>(i % perFrame) / static_cast<float>(perFrame);
        const Params& a = track[fi];
        const Params& b = track[fj];
        auto lerp = [u](float x, float y) { return x + (y - x) * u; };
        if (i % 32 == 0) {
            F1.configure(Biquad::Type::Bandpass, lerp(a.f1, b.f1), lerp(a.f1, b.f1) / 70.0f);
            F2.configure(Biquad::Type::Bandpass, lerp(a.f2, b.f2), lerp(a.f2, b.f2) / 110.0f);
            F3.configure(Biquad::Type::Bandpass, lerp(a.f3, b.f3), lerp(a.f3, b.f3) / 170.0f);
            fric.configure(Biquad::Type::Bandpass, std::max(a.fricHz, 200.0f), std::max(a.fricQ, 0.3f));
        }
        const float av = lerp(a.av, b.av), ah = lerp(a.ah, b.ah), af = lerp(a.af, b.af);

        // ---- Glottal source: Rosenberg pulse with jitter, shimmer and tremor.
        jitter = 0.995f * jitter + 0.005f * noise();
        const float f0 = (voice.pitchStart + (voice.pitchEnd - voice.pitchStart) * (t / duration)) *
                         (1.0f + voice.tremor * 0.05f * std::sin(dsp::kTwoPi * 4.7f * t)) * (1.0f + voice.jitter * 8.0f * jitter);
        phase += f0 / kRate;
        if (phase >= 1.0f) {
            phase -= 1.0f;
            pulseAmp = 1.0f - rng.range(0.0f, 0.15f);
        }
        float glottal = 0.0f;
        if (phase < 0.42f) glottal = 0.5f * (1.0f - std::cos(dsp::kPi * phase / 0.42f));
        else if (phase < 0.55f) glottal = std::cos(0.5f * dsp::kPi * (phase - 0.42f) / 0.13f);
        const float voiced = (glottal - prevGlottal) * 30.0f * pulseAmp;
        prevGlottal = glottal;

        // Whispering trades voicing for turbulence through the same tract.
        const float n = noise();
        const float source = voiced * av * (1.0f - voice.whisper) +
                             n * (ah + av * (voice.whisper * 1.1f + voice.breathiness * 0.3f * (0.5f + glottal)));
        const float tract = F1.process(source) + 0.6f * F2.process(source) + 0.3f * F3.process(source) +
                            0.12f * F4.process(source);
        out[i] = tract + af * 0.8f * fric.process(noise());
    }

    // Clean-up: rumble and hiss out, click-free edges, consistent level.
    Biquad hp = Biquad::highpass(80.0f), lp = Biquad::lowpass(6500.0f);
    float peak = 0.0f;
    for (float& s : out) {
        s = lp.process(hp.process(s));
        peak = std::max(peak, std::fabs(s));
    }
    const size_t fade = std::min(out.size() / 2, static_cast<size_t>(0.01f * kRate));
    for (size_t i = 0; i < fade; ++i) {
        const float g = static_cast<float>(i) / static_cast<float>(fade);
        out[i] *= g;
        out[out.size() - 1 - i] *= g;
    }
    if (peak > 1e-6f) {
        for (float& s : out) s *= 0.9f / peak;
    }
    return out;
}

} // namespace speech
