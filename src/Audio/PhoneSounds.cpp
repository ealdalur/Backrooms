// ---------------------------------------------------------------------------
// PhoneSounds.cpp
// Synthesis of the office telephones. Everything heard in the earpiece ends
// with telephoneBand(); the desk-side handling sounds do not.
// ---------------------------------------------------------------------------
#include "Audio/PhoneSounds.h"

#include "Audio/SpeechSynth.h"
#include "Audio/SynthKit.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace phonesfx {
namespace {

using namespace synth;

constexpr double kTwoPiD = 6.283185307179586;

// ----- Scripts ------------------------------------------------------------------
// The operator speaks fluently: words run together, '|' only at the commas.

const Line kAnnouncements[kAnnouncementCount] = {
    {"We're sorry. The number you have dialed is not in service. Please check the number and dial again.",
     "W IY R S AA R IY || DH AH N AH M B ER Y UW HH AE V D AY AH L D | IH Z N AA T IH N S ER V IH S || "
     "P L IY Z T SH EH K DH AH N AH M B ER | AE N D D AY AH L AH G EH N"},
    {"Your call cannot be completed as dialed. Please check the number and dial again.",
     "Y AO R K AO L | K AE N AA T B IY K AH M P L IY T IH D | AE Z D AY AH L D || "
     "P L IY Z T SH EH K DH AH N AH M B ER | AE N D D AY AH L AH G EH N"},
    {"We're sorry. The call cannot go through at this time. Please try again later.",
     "W IY R S AA R IY || DH AH K AO L | K AE N AA T G OW TH R UW | AE T DH IH S T AY M || "
     "P L IY Z T R AY AH G EH N | L EY T ER"},
    {"All circuits are busy now. Please try your call again later.",
     "AO L S ER K IH T S | AA R B IH Z IY N AW || P L IY Z T R AY Y AO R K AO L | AH G EH N L EY T ER"},
    {"The number you have dialed cannot be reached from this location.",
     "DH AH N AH M B ER Y UW HH AE V D AY AH L D | K AE N AA T B IY R IY T SH T | F R AH M DH IH S L OW K EY SH AH N"},
    {"If you'd like to make a call, please hang up and try again.",
     "IH F Y UW D L AY K T UW M EY K AH K AO L | P L IY Z HH AE NG AH P | AE N D T R AY AH G EH N"},
    {"We're sorry. There is no one left to call.",
     "W IY R S AA R IY || DH EH R IH Z | N OW W AH N | L EH F T || T UW K AO L"},
    {"Please stay on the line.",
     "P L IY Z || S T EY | AA N DH AH | L AY N"},
    {"You have one new message.",
     "Y UW HH AE V | W AH N N UW M EH S IH D ZH"},
    {"You have no new messages.",
     "Y UW HH AE V | N OW N UW M EH S IH D ZH IH Z"},
    {"End of message.",
     "EH N D AH V M EH S IH D ZH"},
    {"You have nine hundred ninety-nine new messages.",
     "Y UW HH AE V || N AY N HH AH N D R IH D | N AY N T IY N AY N || N UW M EH S IH D ZH IH Z"},
};

/// Who is speaking on the line.
enum class Who : uint8_t { Woman, Child, OldMan, Man, Whisperer, Chorus };

struct VoiceLine {
    Line line;
    Who  who;
};

// The voices hesitate between every word.
const VoiceLine kVoices[kVoiceCount] = {
    {{"help... help me", "HH EH L P || HH EH L P | M IY"}, Who::Woman},
    {{"who... are you?", "HH UW || AA R | Y UW"}, Who::Child},
    {{"how long have you been here?", "HH AW | L AO NG | HH AE V | Y UW | B IH N | HH IY R"}, Who::OldMan},
    {{"they can hear you", "DH EY | K AE N | HH IY R | Y UW"}, Who::Whisperer},
    {{"is anyone there?", "IH Z | EH N IY W AH N | DH EH R"}, Who::Woman},
    {{"can you hear me?", "K AE N | Y UW | HH IY R | M IY"}, Who::OldMan},
    {{"don't... hang up", "D OW N T || HH AE NG | AH P"}, Who::Child},
    {{"i can't find the way out", "AY | K AE N T | F AY N D | DH AH | W EY | AW T"}, Who::Woman},
    {{"we're still here", "W IY R | S T IH L | HH IY R"}, Who::Chorus},
    {{"it's behind you", "IH T S | B IH HH AY N D | Y UW"}, Who::Whisperer},
    {{"hello?", "HH AH L OW"}, Who::Child},
    {{"you're not supposed to be here", "Y UH R | N AA T | S AH P OW Z D | T UW | B IY | HH IY R"}, Who::OldMan},
    {{"what year is it?", "W AH T | Y IH R | IH Z | IH T"}, Who::Woman},
};

// Jenny, fed up: short phrases, each with its own contour, so the stress
// lands where an annoyed speaker puts it ("I CHANGED my number!").
struct Phrase {
    const char* phonemes;
    float pitchStart, pitchEnd;
    float pause;    ///< Silence after it (s).
    float emphasis; ///< Level before the mouthpiece overdrives.
};
const Phrase kJennyPhrases[] = {
    {"L UH K", 250.0f, 205.0f, 0.22f, 1.0f},                       // Look,
    {"F AO R DH AH L AE S T T AY M", 240.0f, 212.0f, 0.16f, 1.0f}, // for the last time,
    {"AY T SH EY N D ZH D", 295.0f, 255.0f, 0.0f, 1.25f},         // I CHANGED
    {"M AY N AH M B ER", 255.0f, 195.0f, 0.38f, 1.1f},             // my number!
    {"AY D OW N T K EH R", 265.0f, 232.0f, 0.02f, 1.1f},           // I don't care
    {"IH F IH T S AA N DH AH W AO L", 240.0f, 200.0f, 0.2f, 1.0f}, // if it's on the wall,
    {"S T AA P", 305.0f, 265.0f, 0.05f, 1.3f},                     // STOP
    {"K AO L IH NG", 275.0f, 240.0f, 0.03f, 1.15f},                // calling
    {"HH IY R", 285.0f, 185.0f, 0.0f, 1.25f},                      // here!
};
const char* const kJennyText = "Look, for the last time, I changed my number! I don't care if it's on the wall, stop calling here!";

// Messages left in voicemail: said in pieces, with their own contours, by
// people falling apart (the pauses are where they stop to breathe, or sob).
struct MessageScript {
    const char*   text;
    Who           who;
    float         tremor; ///< How badly the voice shakes.
    const Phrase* phrases;
    int           count;
};
const Phrase kHowDidIGetHere[] = {
    {"HH AH L OW", 225.0f, 262.0f, 0.6f, 1.0f},                            // hello?
    {"HH AW | D IH D | AY | G EH T | HH IY R", 238.0f, 272.0f, 0.55f, 1.1f}, // how did I get here?
    {"AY | W AA Z | AE T | W ER K", 222.0f, 202.0f, 0.12f, 1.0f},           // I was at work,
    {"AE N D | DH EH N", 214.0f, 188.0f, 0.9f, 0.9f},                       // and then...
    {"P L IY Z", 252.0f, 232.0f, 0.2f, 1.1f},                               // please,
    {"S AH M B AA D IY | K AO L | M IY | B AE K", 246.0f, 186.0f, 0.0f, 1.0f}, // somebody call me back.
};
const Phrase kNeverEnds[] = {
    {"DH IH S | N EH V ER | EH N D Z", 172.0f, 140.0f, 0.4f, 1.3f},        // this never ends!
    {"EH V R IY | R UW M | IH Z | DH AH | S EY M", 150.0f, 126.0f, 0.5f, 1.1f}, // every room is the same.
    {"DH AH | L AY T S", 142.0f, 130.0f, 0.4f, 1.0f},                       // the lights...
    {"DH AH | HH AH M IH NG", 136.0f, 120.0f, 0.55f, 1.0f},                 // the humming...
    {"IH T | N EH V ER | EH N D Z", 178.0f, 118.0f, 0.0f, 1.35f},           // it never ends!
};
const Phrase kHelpMe[] = {
    {"HH EH L P | M IY", 322.0f, 282.0f, 0.6f, 1.2f},                       // help me!
    {"AY | K AE N T | F AY N D | DH AH | D AO R", 292.0f, 262.0f, 0.5f, 1.0f}, // I can't find the door.
    {"IH T S | S OW | L AW D | IH N | HH IY R", 286.0f, 252.0f, 0.6f, 1.0f},  // it's so loud in here.
    {"HH EH L P | M IY", 332.0f, 272.0f, 0.0f, 1.25f},                      // help me!
};
const Phrase kDontLookForMe[] = {
    {"AY | D OW N T | N OW | W AH T | D EY | IH T | IH Z", 126.0f, 106.0f, 0.5f, 1.0f},  // I don't know what day it is.
    {"AY V | B IH N | W AO K IH NG | F AO R | S OW | L AO NG", 121.0f, 100.0f, 0.7f, 1.0f}, // I've been walking for so long.
    {"D OW N T | K AH M | L UH K IH NG | F AO R | M IY", 118.0f, 92.0f, 0.0f, 1.0f},      // don't come looking for me.
};
const Phrase kDontPickUp[] = {
    {"IH F | Y UW | G EH T | DH IH S", 150.0f, 140.0f, 0.6f, 1.0f},         // if you get this...
    {"D OW N T | P IH K | AH P | DH AH | F OW N", 150.0f, 130.0f, 0.7f, 1.1f}, // don't pick up the phone.
    {"IH T S | L IH S AH N IH NG", 150.0f, 120.0f, 0.0f, 1.0f},             // it's listening.
};
const Phrase kWhatLevel[] = {
    {"AY | F AW N D | AH | F OW N", 216.0f, 200.0f, 0.4f, 1.0f},            // I found a phone.
    {"IH F | EH N IY W AH N | HH IY R Z | DH IH S", 226.0f, 212.0f, 0.5f, 1.0f}, // if anyone hears this...
    {"AY M | AA N | L EH V AH L", 222.0f, 216.0f, 0.8f, 1.0f},              // I'm on level...
    {"AY | D OW N T | N OW | W AH T | L EH V AH L", 228.0f, 196.0f, 0.6f, 1.05f}, // I don't know what level.
    {"AY | K AE N | HH IY R | IH T | K AH M IH NG", 204.0f, 176.0f, 0.0f, 0.9f},  // I can hear it coming.
};
#define PHRASES(a) a, static_cast<int>(sizeof(a) / sizeof(a[0]))
const MessageScript kMessages[kMessageCount] = {
    {"hello? how did i get here? i was at work, and then... please, somebody call me back.", Who::Woman, 1.6f,
     PHRASES(kHowDidIGetHere)},
    {"this never ends! every room is the same. the lights... the humming... it never ends!", Who::Man, 0.8f,
     PHRASES(kNeverEnds)},
    {"help me! i can't find the door. it's so loud in here. help me!", Who::Child, 1.0f, PHRASES(kHelpMe)},
    {"i don't know what day it is. i've been walking for so long. don't come looking for me.", Who::OldMan, 1.3f,
     PHRASES(kDontLookForMe)},
    {"if you get this... don't pick up the phone. it's listening.", Who::Whisperer, 0.0f, PHRASES(kDontPickUp)},
    {"i found a phone. if anyone hears this... i'm on level... i don't know what level. i can hear it coming.", Who::Woman,
     1.0f, PHRASES(kWhatLevel)},
};
#undef PHRASES

// ----- Shared processing ------------------------------------------------------------

/// The telephone channel: 300 - 3400 Hz with the earpiece's presence peak.
void telephoneBand(Buffer& b) {
    for (int i = 0; i < 2; ++i) applyFilter(b, Biquad::highpass(300.0f));
    for (int i = 0; i < 2; ++i) applyFilter(b, Biquad::lowpass(3400.0f));
    Biquad presence = Biquad::bandpass(1800.0f, 1.0f);
    for (float& s : b) s += 0.35f * presence.process(s);
}

/// Adds a keyed sine of `hz` from `at` for `len` seconds (4 ms edges).
void addTone(Buffer& b, float at, float len, float hz, float gain) {
    const size_t s0 = samplesFor(at), n = samplesFor(len);
    const double w = kTwoPiD * hz / kRate;
    for (size_t i = 0; i < n && s0 + i < b.size(); ++i) {
        const float t = timeOf(i);
        const float edge = smooth01(0.0f, 0.004f, t) * (1.0f - smooth01(len - 0.004f, len, t));
        b[s0 + i] += gain * edge * static_cast<float>(std::sin(w * static_cast<double>(i)));
    }
}

/// A call-progress tone: `freqs` summed and keyed on for `on` seconds of every
/// `period`, as a seamless loop of `loop` seconds. `loop` must hold whole
/// cycles of every frequency and of the cadence; two loops are rendered and
/// the second kept, so the filters have settled and the loop is exactly periodic.
Sound toneLoop(std::initializer_list<float> freqs, float on, float period, float loop, float peak, float drive = 1.2f) {
    const size_t n = samplesFor(loop);
    Buffer b(2 * n, 0.0f);
    for (size_t i = 0; i < b.size(); ++i) {
        const float p = std::fmod(timeOf(i), period);
        const float key = on >= period ? 1.0f : smooth01(0.0f, 0.004f, p) * (1.0f - smooth01(on - 0.004f, on, p));
        if (key <= 0.0f) continue;
        double s = 0.0;
        for (float f : freqs) s += std::sin(kTwoPiD * f * static_cast<double>(i) / kRate);
        b[i] = key * static_cast<float>(s) / static_cast<float>(freqs.size());
    }
    // A touch of line saturation, then the channel.
    for (float& s : b) s = std::tanh(drive * s) / std::tanh(drive);
    telephoneBand(b);
    Sound out{Buffer(b.begin() + static_cast<std::ptrdiff_t>(n), b.end()), true};
    normalize(out.samples, peak);
    return out;
}

/// Sparse line crackle: short decaying clicks at `perSecond`, added to `b`.
void addCrackle(Buffer& b, float perSecond, float gain, Noise& noise, rnd::Rng& rng) {
    const float decay = std::exp(-1.0f / (0.0012f * kRate));
    float env = 0.0f;
    for (float& s : b) {
        if (rng.chance(perSecond / kRate)) env = rng.range(0.3f, 1.0f);
        s += gain * env * noise();
        env *= decay;
    }
}

/// Lo-fi line coding: sample-and-hold at ~7 kHz and coarse quantisation.
void crunch(Buffer& b, int hold, float steps) {
    normalize(b, 1.0f);
    float held = 0.0f;
    for (size_t i = 0; i < b.size(); ++i) {
        if (i % static_cast<size_t>(hold) == 0) held = std::round(b[i] * steps) / steps;
        b[i] = held;
    }
}

speech::Voice voiceOf(Who who, rnd::Rng& rng) {
    speech::Voice v;
    v.seed = rng.next();
    switch (who) {
    case Who::Woman:
        v.pitchStart = rng.range(190.0f, 215.0f);
        v.pitchEnd = v.pitchStart * rng.range(0.78f, 0.88f);
        v.whisper = rng.range(0.25f, 0.45f);
        v.breathiness = 0.35f;
        v.tempo = rng.range(1.2f, 1.4f);
        v.tract = 1.15f;
        v.jitter = 0.03f;
        v.tremor = 0.6f;
        break;
    case Who::Child:
        v.pitchStart = rng.range(265.0f, 295.0f);
        v.pitchEnd = v.pitchStart * rng.range(0.85f, 0.95f);
        v.whisper = rng.range(0.15f, 0.35f);
        v.breathiness = 0.3f;
        v.tempo = rng.range(1.15f, 1.3f);
        v.tract = 1.3f;
        v.jitter = 0.02f;
        v.tremor = 0.35f;
        break;
    case Who::OldMan:
        v.pitchStart = rng.range(108.0f, 122.0f);
        v.pitchEnd = v.pitchStart * rng.range(0.75f, 0.85f);
        v.whisper = rng.range(0.3f, 0.5f);
        v.breathiness = 0.45f;
        v.tempo = rng.range(1.35f, 1.55f);
        v.tract = 0.97f;
        v.jitter = 0.05f;
        v.tremor = 1.3f;
        break;
    case Who::Man:
        v.pitchStart = rng.range(135.0f, 150.0f);
        v.pitchEnd = v.pitchStart * rng.range(0.8f, 0.88f);
        v.whisper = rng.range(0.05f, 0.15f);
        v.breathiness = 0.3f;
        v.tempo = rng.range(0.95f, 1.05f);
        v.tract = 1.0f;
        v.jitter = 0.03f;
        v.tremor = 0.7f;
        break;
    case Who::Whisperer:
    case Who::Chorus:
        v.pitchStart = 150.0f;
        v.pitchEnd = 130.0f;
        v.whisper = 1.0f;
        v.breathiness = 0.5f;
        v.tempo = rng.range(1.25f, 1.45f);
        v.tract = 1.05f;
        break;
    }
    return v;
}

} // namespace

const Line& announcement(Announcement a) { return kAnnouncements[static_cast<size_t>(a)]; }

const Line& voice(Voice v) { return kVoices[static_cast<size_t>(v)].line; }

const char* jennyText() { return kJennyText; }

const char* messageText(Message m) { return kMessages[static_cast<size_t>(m)].text; }

// ============================================================================
// The line and its tones
// ============================================================================

std::vector<Sound> makeLine(uint64_t seed) {
    // An open line: soft hiss that breathes, a faint mains buzz leaking in
    // (the 60 Hz fundamental itself is below the channel: only its odd
    // harmonics get through) and sparse crackle. 6 s holds whole cycles of
    // every harmonic; the noise is crossfaded.
    const float loop = 6.0f, cf = 0.5f;
    rnd::Rng rng(seed);
    Noise noise(rng.next());
    const uint64_t breathe = rng.next();
    Buffer b = silence(loop + cf);
    for (size_t i = 0; i < b.size(); ++i) {
        const double t = static_cast<double>(i) / kRate;
        const float buzz = static_cast<float>(std::sin(kTwoPiD * 180.0 * t) + 0.7 * std::sin(kTwoPiD * 300.0 * t) +
                                              0.45 * std::sin(kTwoPiD * 420.0 * t));
        const float swell = 0.75f + 0.25f * noise::value1D(t * 0.7, breathe);
        b[i] = 0.10f * noise() * swell + 0.02f * buzz;
    }
    addCrackle(b, 3.0f, 0.5f, noise, rng);
    telephoneBand(b);
    Sound s{makeSeamless(b, cf), true};
    normalize(s.samples, 0.8f);
    return {s};
}

std::vector<Sound> makeDialTone(uint64_t) { return {toneLoop({350.0f, 440.0f}, 1.0f, 1.0f, 2.0f, 0.5f)}; }

std::vector<Sound> makeRingback(uint64_t) { return {toneLoop({440.0f, 480.0f}, 2.0f, 6.0f, 6.0f, 0.5f)}; }

std::vector<Sound> makeBusy(uint64_t) { return {toneLoop({480.0f, 620.0f}, 0.5f, 1.0f, 1.0f, 0.5f)}; }

std::vector<Sound> makeReorder(uint64_t) { return {toneLoop({480.0f, 620.0f}, 0.25f, 0.5f, 1.0f, 0.5f)}; }

std::vector<Sound> makeHowler(uint64_t) {
    // Four shrill tones pulsed ten times a second, driven hard: meant to be
    // heard across a room.
    return {toneLoop({1400.0f, 2060.0f, 2450.0f, 2600.0f}, 0.1f, 0.2f, 1.0f, 0.6f, 2.5f)};
}

std::vector<Sound> makeDtmf(uint64_t) {
    // Touch-tone: one low (row) and one high (column) frequency per key, the
    // high group a little louder ("twist"). Variant = key, in keypad order.
    const float rows[4] = {697.0f, 770.0f, 852.0f, 941.0f};
    const float cols[3] = {1209.0f, 1336.0f, 1477.0f};
    std::vector<Sound> out;
    for (int k = 0; k < 12; ++k) {
        Buffer b = silence(0.2f);
        addTone(b, 0.005f, 0.16f, rows[k / 3], 0.45f);
        addTone(b, 0.005f, 0.16f, cols[k % 3], 0.55f);
        telephoneBand(b);
        normalize(b, 0.5f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeSit(uint64_t) {
    // Special information tones: three rising tones ahead of a recording
    // (913.8, 1370.6 and 1776.7 Hz; short, short, long).
    Buffer b = silence(1.0f);
    addTone(b, 0.0f, 0.274f, 913.8f, 1.0f);
    addTone(b, 0.276f, 0.274f, 1370.6f, 1.0f);
    addTone(b, 0.552f, 0.380f, 1776.7f, 1.0f);
    telephoneBand(b);
    normalize(b, 0.5f);
    return {{std::move(b), false}};
}

std::vector<Sound> makeSwitching(uint64_t seed) {
    // A call being routed: the line is seized with a clunk, then bursts of
    // relay chatter from a distant exchange, over a swell of line noise.
    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        const float dur = rng.range(0.9f, 1.6f);
        Buffer b = silence(dur);
        addNoiseThunk(b, noise, 0.02f, 1.0f, 700.0f, 0.0008f, 0.015f, 0.2f, 0.05f);
        for (float t = rng.range(0.12f, 0.25f); t < dur - 0.15f; t += rng.range(0.08f, 0.3f)) {
            const int clicks = rng.rangeInt(2, 6);
            for (int k = 0; k < clicks; ++k) {
                const float g = rng.range(0.3f, 0.9f);
                addNoiseBurst(b, t, 0.0002f, rng.range(0.001f, 0.003f), Biquad::bandpass(rng.range(1200.0f, 2600.0f), 1.2f),
                              g, noise);
                addNoiseThunk(b, noise, t, 0.4f * g, rng.range(600.0f, 900.0f), 0.0005f, 0.004f);
                t += rng.range(0.01f, 0.04f);
            }
        }
        for (size_t i = 0; i < b.size(); ++i) {
            b[i] += 0.05f * noise() * std::sin(dsp::kPi * timeOf(i) / dur);
        }
        telephoneBand(b);
        fadeEdges(b, 0.002f, 0.05f);
        normalize(b, 0.7f);
        out.push_back({std::move(b), false});
    }
    return out;
}

// ============================================================================
// Voices
// ============================================================================

std::vector<Sound> makeOperator(uint64_t seed) {
    // The recorded operator: a clear, brisk female voice from an announcement
    // machine (a faint click as it starts, the slightest wow of its tape),
    // levelled and squeezed through the channel. The same voice runs the
    // voicemail system. Some recordings are not recordings: the same voice,
    // too slow, sagging in pitch, with something speaking under it.
    std::vector<Sound> out;
    for (int a = 0; a < kAnnouncementCount; ++a) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(a)));
        Noise noise(rng.next());
        const Announcement id = static_cast<Announcement>(a);
        const bool wrong = id == Announcement::NoOneLeft || id == Announcement::StayOnLine || id == Announcement::ManyMessages;
        speech::Voice v;
        v.seed = rng.next();
        v.pitchStart = 212.0f;
        v.pitchEnd = wrong ? 150.0f : 178.0f;
        v.whisper = 0.02f;
        v.breathiness = 0.1f;
        v.tempo = wrong ? 1.3f : 0.92f;
        v.tract = 1.17f;
        v.jitter = 0.006f;
        v.tremor = wrong ? 0.25f : 0.0f;
        const char* phonemes = kAnnouncements[a].phonemes;
        Buffer speech = speech::say(phonemes, v);
        if (wrong) {
            speech::Voice low = v;
            low.pitchStart *= 0.5f;
            low.pitchEnd *= 0.5f;
            low.tract *= 0.9f;
            const Buffer under = speech::say(phonemes, low);
            for (size_t i = 0; i < speech.size() && i < under.size(); ++i) speech[i] += 0.3f * under[i];
        }
        speech = tapeWow(speech, wrong ? 0.012f : 0.0035f, rng.range(0.4f, 0.7f), rng.next());

        Buffer b = silence(0.12f);
        b.insert(b.end(), speech.begin(), speech.end());
        b.resize(b.size() + samplesFor(0.15f), 0.0f);
        normalize(b, 1.0f);
        for (float& s : b) s = std::tanh(1.8f * s) / std::tanh(1.8f); // the machine's levelling
        addNoiseBurst(b, 0.03f, 0.0003f, 0.002f, Biquad::highpass(1500.0f), 0.3f, noise); // the machine engaging
        telephoneBand(b);
        fadeEdges(b, 0.002f, 0.05f);
        normalize(b, 0.85f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeVoice(uint64_t seed) {
    // Voices that should not be on the line. Each one surfaces out of a swell
    // of static, faint and far from its receiver (a big, empty room around
    // it), keeps breaking up, and sinks back into the noise. Different people:
    // a woman, a child, an old man, a whisperer - and once, all of them.
    std::vector<Sound> out;
    for (int i = 0; i < kVoiceCount; ++i) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(i)));
        Noise noise(rng.next());
        const VoiceLine& line = kVoices[i];
        Buffer b;
        if (line.who == Who::Chorus) {
            // Three of them, not quite together.
            const Who who[3] = {Who::Woman, Who::Child, Who::OldMan};
            const float offset[3] = {0.0f, 0.07f, 0.16f};
            for (int k = 0; k < 3; ++k) {
                const Buffer part = speech::say(line.line.phonemes, voiceOf(who[k], rng));
                const size_t at = samplesFor(offset[k]);
                if (b.size() < at + part.size()) b.resize(at + part.size(), 0.0f);
                for (size_t s = 0; s < part.size(); ++s) b[at + s] += (k == 0 ? 0.8f : 0.6f) * part[s];
            }
        } else {
            b = speech::say(line.line.phonemes, voiceOf(line.who, rng));
        }
        b = tapeWow(b, 0.02f, rng.range(0.3f, 0.8f), rng.next());
        dropouts(b, rng, rng.rangeInt(1, 3));
        bakeDistance(b, 3200.0f, 0.6f, 0.5f, 0.6f, 1.2f, 0.8f);

        // Surfacing out of the static, and sinking back.
        const float lead = 0.35f, tail = 0.4f;
        Buffer line2 = silence(lead);
        line2.insert(line2.end(), b.begin(), b.end());
        line2.resize(line2.size() + samplesFor(tail), 0.0f);
        normalize(line2, 1.0f);
        const float dur = static_cast<float>(line2.size()) / kRate;
        for (size_t s = 0; s < line2.size(); ++s) {
            const float t = timeOf(s);
            const float bed = 0.06f + 0.3f * (1.0f - smooth01(0.1f, lead, t)) + 0.3f * smooth01(dur - tail, dur - 0.05f, t);
            line2[s] += bed * noise() * smooth01(0.0f, 0.08f, t);
        }
        addCrackle(line2, 6.0f, 0.4f, noise, rng);
        crunch(line2, 7, 24.0f);
        telephoneBand(line2);
        fadeEdges(line2, 0.01f, 0.1f);
        normalize(line2, 0.9f);
        out.push_back({std::move(line2), false});
    }
    return out;
}

std::vector<Sound> makeStatic(uint64_t seed) {
    // Interference: a burst of crackle and hiss with a wandering heterodyne
    // whistle - something forcing its way onto the line.
    std::vector<Sound> out;
    for (int v = 0; v < 3; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        const uint64_t wander = rng.next(), flutter = rng.next();
        const float dur = rng.range(0.5f, 1.2f);
        Buffer b = silence(dur);
        float phase = 0.0f;
        for (size_t i = 0; i < b.size(); ++i) {
            const float t = timeOf(i);
            const float env = smooth01(0.0f, 0.08f, t) * (1.0f - smooth01(dur - 0.2f, dur, t));
            const float hz = 700.0f + 1500.0f * noise::value1D(t * 1.3, wander) + 60.0f * std::sin(dsp::kTwoPi * 7.0f * t);
            phase += hz / kRate;
            phase -= std::floor(phase);
            const float whistle = 0.3f * noise::value1D(t * 4.0, flutter) * std::sin(dsp::kTwoPi * phase);
            const float hiss = 0.35f * noise() * (0.5f + 0.5f * noise::value1D(t * 20.0, flutter + 1));
            b[i] = env * (hiss + whistle);
        }
        addCrackle(b, 120.0f, 0.8f, noise, rng);
        telephoneBand(b);
        fadeEdges(b, 0.005f, 0.05f);
        normalize(b, 0.8f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeBreath(uint64_t seed) {
    // Someone breathing into the mouthpiece: slow, heavy breaths close to the
    // microphone (the carbon mic rasps on every exhale), a wet click now and then.
    std::vector<Sound> out;
    for (int v = 0; v < 2; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        const float dur = 6.5f;
        Buffer b = silence(dur);
        Biquad in1 = Biquad::bandpass(1800.0f, 1.5f), out1 = Biquad::bandpass(700.0f, 1.0f), out2 = Biquad::bandpass(1300.0f, 2.0f);
        for (float t0 = rng.range(0.1f, 0.4f); t0 < dur - 2.2f; t0 += rng.range(2.5f, 3.2f)) {
            const float inLen = rng.range(0.8f, 1.1f), gap = rng.range(0.15f, 0.3f), outLen = rng.range(1.1f, 1.5f);
            const size_t s0 = samplesFor(t0), n = samplesFor(inLen + gap + outLen);
            for (size_t i = 0; i < n && s0 + i < b.size(); ++i) {
                const float t = timeOf(i);
                const float inhale = smooth01(0.0f, 0.4f * inLen, t) * (1.0f - smooth01(0.7f * inLen, inLen, t));
                const float e = t - inLen - gap;
                const float exhale = smooth01(0.0f, 0.15f, e) * (1.0f - smooth01(0.5f * outLen, outLen, e));
                const float rasp = 0.6f + 0.4f * std::sin(dsp::kTwoPi * 31.0f * t) * std::sin(dsp::kTwoPi * 2.3f * t);
                const float n0 = noise();
                b[s0 + i] += 0.45f * in1.process(n0) * inhale + (out1.process(n0) + 0.6f * out2.process(n0)) * exhale * rasp;
            }
            if (rng.chance(0.5f)) {
                addNoiseBurst(b, t0 + inLen + gap * 0.5f, 0.0005f, 0.004f, Biquad::bandpass(2500.0f, 2.0f), 0.25f, noise);
            }
        }
        for (float& s : b) s = std::tanh(2.0f * s);
        telephoneBand(b);
        fadeEdges(b, 0.1f, 0.3f);
        normalize(b, 0.8f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeJenny(uint64_t seed) {
    // 867-5309. She picks up - a fumble, an exasperated sigh - and lets rip:
    // a young woman's voice, fast, pressed and pitched up with irritation,
    // right up against her mouthpiece so the loud words overdrive it. Then
    // her handset is slammed down hard enough to crackle the line, the line
    // drops with a click, and nothing.
    rnd::Rng rng(seed);
    Noise noise(rng.next());
    const float start = kJennySpeechStart;
    Buffer b = silence(start + 7.0f);

    // Picking up at her end: the handset knocked about, then her room.
    addNoiseThunk(b, noise, 0.02f, 0.6f, 900.0f, 0.001f, 0.02f, 0.2f, 0.05f);
    addNoiseBurst(b, 0.05f, 0.0003f, 0.002f, Biquad::highpass(2000.0f), 0.3f, noise);
    addNoiseThunk(b, noise, 0.16f, 0.35f, 700.0f, 0.001f, 0.015f);
    speech::Voice sigh;
    sigh.seed = rng.next();
    sigh.whisper = 1.0f;
    sigh.tract = 1.2f;
    sigh.tempo = 1.6f;
    const Buffer exhale = speech::say("HH AH", sigh);
    for (size_t i = 0; i < exhale.size(); ++i) {
        const size_t at = samplesFor(0.42f) + i;
        if (at < b.size()) b[at] += 0.45f * exhale[i];
    }

    // The rant: phrases overlapped through say()'s lead-in / release silence.
    speech::Voice v;
    v.whisper = 0.0f;
    v.breathiness = 0.12f;
    v.tempo = 0.78f;
    v.tract = 1.2f;
    v.jitter = 0.015f;
    v.tremor = 0.0f;
    float t = start;
    for (const Phrase& p : kJennyPhrases) {
        v.seed = rng.next();
        v.pitchStart = p.pitchStart;
        v.pitchEnd = p.pitchEnd;
        const Buffer part = speech::say(p.phonemes, v);
        const size_t at = samplesFor(t - 0.03f); // skip say()'s lead-in
        if (b.size() < at + part.size()) b.resize(at + part.size(), 0.0f);
        for (size_t i = 0; i < part.size(); ++i) b[at + i] += p.emphasis * part[i];
        t += static_cast<float>(part.size()) / kRate - 0.15f + p.pause;
    }

    // SLAM. Close to her mouthpiece: it overloads the line. Then the drop.
    const float slam = t + 0.18f;
    b.resize(std::max(b.size(), samplesFor(slam + 0.9f)), 0.0f);
    addNoiseThunk(b, noise, slam, 3.0f, 650.0f, 0.001f, 0.05f, 0.3f, 0.12f);
    addNoiseThunk(b, noise, slam, 1.4f, 2600.0f, 0.0005f, 0.006f);
    addNoiseThunk(b, noise, slam + 0.03f, 1.2f, 500.0f, 0.001f, 0.03f);
    addCrackle(b, 900.0f, 0.4f, noise, rng); // the mic crackling as it is shaken...
    for (size_t i = samplesFor(slam + 0.25f); i < b.size(); ++i) b[i] = 0.0f; // ...until the line drops
    addNoiseBurst(b, slam + 0.3f, 0.0003f, 0.002f, Biquad::bandpass(1500.0f, 1.2f), 0.5f, noise);

    // Her end of the line had a little crackle of its own before that.
    for (size_t i = 0; i < samplesFor(slam); ++i) b[i] += 0.015f * noise();
    for (float& s : b) s = std::tanh(2.2f * s) / std::tanh(2.2f);
    telephoneBand(b);
    b.resize(samplesFor(slam + 0.7f));
    fadeEdges(b, 0.005f, 0.05f);
    normalize(b, 0.9f);
    return {{std::move(b), false}};
}

std::vector<Sound> makeBeep(uint64_t seed) {
    // The voicemail system's signals, through the channel: a single beep
    // before a message, two short ones after it, and the click of the line
    // switching over to the system (enveloped noise: no pitch).
    Buffer start = silence(0.4f);
    addTone(start, 0.01f, 0.32f, 1000.0f, 1.0f);
    Buffer end = silence(0.45f);
    addTone(end, 0.01f, 0.12f, 850.0f, 1.0f);
    addTone(end, 0.23f, 0.12f, 850.0f, 1.0f);
    Buffer click = silence(0.12f);
    Noise noise(seed);
    addNoiseThunk(click, noise, 0.005f, 1.0f, 900.0f, 0.0005f, 0.008f);
    addNoiseBurst(click, 0.005f, 0.0002f, 0.0015f, Biquad::highpass(2000.0f), 0.6f, noise);
    std::vector<Sound> out;
    for (Buffer* b : {&start, &end, &click}) {
        telephoneBand(*b);
        fadeEdges(*b, 0.001f, 0.01f);
        normalize(*b, 0.5f);
        out.push_back({std::move(*b), false});
    }
    return out;
}

std::vector<Sound> makeMessage(uint64_t seed) {
    // Messages left in the voicemail boxes. Unlike the voices on the line
    // they are close to the mouthpiece - but recorded: tape hiss under them,
    // the tape wobbling, the machine clicking at either end. And the people
    // leaving them are falling apart: shaking voices, and in the longer
    // pauses a shuddering breath, or a sob.
    std::vector<Sound> out;
    for (int m = 0; m < kMessageCount; ++m) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(m)));
        Noise noise(rng.next());
        const MessageScript& script = kMessages[m];
        speech::Voice v = voiceOf(script.who, rng);
        v.tremor = std::max(v.tremor, script.tremor);
        const bool whisper = script.who == Who::Whisperer;

        float t = 0.4f;
        Buffer b = silence(t);
        for (int i = 0; i < script.count; ++i) {
            const Phrase& p = script.phrases[i];
            v.seed = rng.next();
            if (!whisper) {
                v.pitchStart = p.pitchStart;
                v.pitchEnd = p.pitchEnd;
            }
            const Buffer part = speech::say(p.phonemes, v);
            const size_t at = samplesFor(t - 0.03f); // skip say()'s lead-in
            if (b.size() < at + part.size()) b.resize(at + part.size(), 0.0f);
            for (size_t s = 0; s < part.size(); ++s) b[at + s] += p.emphasis * part[s];
            t += static_cast<float>(part.size()) / kRate - 0.15f + p.pause;
            if (!whisper && p.pause >= 0.45f) {
                // A shuddering intake of breath in the pause: a few quick, pitchless pulses.
                b.resize(std::max(b.size(), samplesFor(t + 0.2f)), 0.0f);
                const float breath = t - p.pause + 0.12f;
                const int pulses = rng.rangeInt(2, 4);
                for (int k = 0; k < pulses; ++k) {
                    addNoiseBurst(b, breath + 0.08f * static_cast<float>(k), 0.03f, 0.03f, Biquad::highpass(1400.0f),
                                  rng.range(0.1f, 0.16f), noise, 0.3f);
                }
            }
        }
        b.resize(samplesFor(t + 0.45f), 0.0f);
        b = tapeWow(b, 0.01f, rng.range(0.5f, 1.1f), rng.next());
        dropouts(b, rng, rng.rangeInt(0, 1));
        bakeDistance(b, 5000.0f, 0.35f, 0.6f, 0.9f, 0.35f, 0.3f); // a small room around them
        normalize(b, 1.0f);

        // The tape: steady hiss, and the machine's clicks as it starts and stops.
        Biquad hissLp = Biquad::lowpass(4000.0f);
        for (float& s : b) s += 0.035f * hissLp.process(noise());
        const float len = static_cast<float>(b.size()) / kRate;
        for (float at : {0.08f, len - 0.2f}) {
            addNoiseThunk(b, noise, at, 0.5f, 900.0f, 0.0005f, 0.006f);
            addNoiseBurst(b, at, 0.0002f, 0.0015f, Biquad::highpass(2000.0f), 0.3f, noise);
        }
        telephoneBand(b);
        fadeEdges(b, 0.01f, 0.05f);
        // Level them by their speech, not by a stray click: each as loud as the others.
        normalizeLoudness(b, 0.4f);
        for (float& s : b) s = std::tanh(s);
        out.push_back({std::move(b), false});
    }
    return out;
}

// ============================================================================
// The handset at the desk
// ============================================================================

std::vector<Sound> makeKey(uint64_t seed) {
    // A telephone keypad button: a light plastic "tick" as it bottoms out in
    // the hollow shell of the base, a softer one as it springs back.
    const Mode shell[] = {{1450.0f, 0.012f, 1.0f}, {2900.0f, 0.008f, 0.5f}};
    std::vector<Sound> out;
    for (int v = 0; v < 4; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.12f);
        addNoiseBurst(b, 0.0005f, 0.0002f, rng.range(0.0008f, 0.0014f), Biquad::highpass(rng.range(3500.0f, 4500.0f)),
                      rng.range(0.6f, 0.9f), noise);
        addNoiseThunk(b, noise, 0.0005f, 0.7f, rng.range(1800.0f, 2600.0f), 0.0004f, rng.range(0.003f, 0.005f));
        addModes(b, 0.0005f, shell, 2, 0.12f, 0.1f, rng);
        const float release = rng.range(0.05f, 0.08f);
        addNoiseBurst(b, release, 0.0002f, 0.0008f, Biquad::highpass(4000.0f), rng.range(0.2f, 0.3f), noise);
        applyFilter(b, Biquad::highpass(200.0f));
        fadeEdges(b, 0.0003f, 0.02f);
        normalize(b, 0.8f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makePickup(uint64_t seed) {
    // The handset lifted out of the cradle: plastic scraping free, both hook
    // switches clacking up, the coiled cord rattling against the desk. Like
    // the hang-up, every layer is enveloped white noise through non-resonant
    // (Butterworth) filters: no pitch anywhere.
    std::vector<Sound> out;
    for (int v = 0; v < 2; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.7f);
        // The scrape: grainy noise in a broad band (high-pass, then low-pass).
        Buffer scrape = silence(0.3f);
        addNoiseBurst(scrape, 0.0f, 0.03f, 0.04f, Biquad::highpass(rng.range(1200.0f, 1600.0f)), 0.25f, noise, 0.6f);
        applyFilter(scrape, Biquad::lowpass(2800.0f));
        for (size_t i = 0; i < scrape.size(); ++i) b[i] += scrape[i];
        const float first = rng.range(0.05f, 0.08f);
        const float second = first + rng.range(0.015f, 0.035f);
        for (float at : {first, second}) {
            const float g = at == first ? 1.0f : 0.7f;
            addNoiseThunk(b, noise, at, g, rng.range(900.0f, 1300.0f), 0.0006f, 0.012f, 0.2f, 0.04f);
            addNoiseBurst(b, at, 0.0002f, 0.0015f, Biquad::highpass(3000.0f), 0.5f * g, noise);
        }
        for (int k = 0; k < 7; ++k) {
            addNoiseBurst(b, rng.range(0.1f, 0.4f), 0.0002f, 0.001f, Biquad::highpass(2500.0f), rng.range(0.05f, 0.15f), noise);
        }
        applyFilter(b, Biquad::lowpass(9000.0f));
        applyFilter(b, Biquad::highpass(90.0f));
        fadeEdges(b, 0.001f, 0.08f);
        normalize(b, 0.8f);
        out.push_back({std::move(b), false});
    }
    return out;
}

std::vector<Sound> makeHangup(uint64_t seed) {
    // The handset put back in its cradle: a hollow plastic clunk as one end
    // lands, the other a beat later pressing the hook switches down with a
    // dry click, then a faint knock as it rocks and settles. Like the door
    // sounds, every layer is enveloped white noise through non-resonant
    // (Butterworth) filters, so the clunk and clicks carry no pitch.
    std::vector<Sound> out;
    for (int v = 0; v < 2; ++v) {
        rnd::Rng rng(rnd::hashCombine(seed, static_cast<uint64_t>(v)));
        Noise noise(rng.next());
        Buffer b = silence(0.6f);
        // First end: the dull body of the plastic shell, and a brighter contact edge.
        addNoiseThunk(b, noise, 0.0f, 1.0f, rng.range(380.0f, 500.0f), 0.001f, rng.range(0.025f, 0.035f), 0.2f, 0.06f);
        addNoiseThunk(b, noise, 0.0f, 0.4f, rng.range(1800.0f, 2400.0f), 0.0005f, 0.004f);
        // Second end, onto the hook switches.
        const float second = rng.range(0.03f, 0.06f);
        addNoiseThunk(b, noise, second, 0.75f, rng.range(350.0f, 450.0f), 0.001f, 0.03f, 0.2f, 0.05f);
        addNoiseThunk(b, noise, second, 0.3f, 2000.0f, 0.0005f, 0.004f);
        // The switches snap down: two small, dry clicks.
        const float click = second + rng.range(0.004f, 0.01f);
        addNoiseBurst(b, click, 0.0002f, 0.0012f, Biquad::highpass(3000.0f), 0.22f, noise);
        addNoiseBurst(b, click + rng.range(0.008f, 0.02f), 0.0002f, 0.001f, Biquad::highpass(3200.0f), 0.15f, noise);
        // It rocks and settles.
        addNoiseThunk(b, noise, second + rng.range(0.08f, 0.12f), 0.2f, 600.0f, 0.001f, 0.015f);
        applyFilter(b, Biquad::highpass(60.0f));
        fadeEdges(b, 0.0005f, 0.08f);
        normalize(b, 0.85f);
        out.push_back({std::move(b), false});
    }
    return out;
}

} // namespace phonesfx
