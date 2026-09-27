#pragma once
// ---------------------------------------------------------------------------
// SpeechSynth.h
// A small formant speech synthesiser (in the spirit of Klatt's cascade /
// parallel synthesiser), used to give the Wanderer a voice without any
// audio files.
//
// Input is a phrase written in ARPAbet-like phonemes separated by spaces,
// e.g. "HH EH L P | M IY" ("help me"). '|' is a short pause, '||' a long
// one, and a '~' prefix on a phoneme stutters the syllable it starts.
//
// Each phoneme contributes target parameters (formant frequencies, voicing,
// aspiration and frication amplitudes, burst) which are joined into smooth
// tracks with coarticulated transitions. The source is a Rosenberg glottal
// pulse with jitter and shimmer (plus breath noise; whispering replaces the
// voicing with turbulence), filtered by parallel formant resonators; fricatives
// and plosive bursts are shaped noise.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <vector>

namespace speech {

/// How the phrase is spoken.
struct Voice {
    float    pitchStart  = 92.0f; ///< Hz at the start of the phrase.
    float    pitchEnd    = 72.0f; ///< Hz at the end (declination).
    float    whisper     = 0.0f;  ///< 0 = voiced, 1 = fully whispered.
    float    breathiness = 0.1f;  ///< Breath noise mixed into voiced sound.
    float    tempo       = 1.0f;  ///< > 1 = slower, drawn-out delivery.
    float    tract       = 1.0f;  ///< Formant scale: < 1 = larger, deeper vocal tract.
    float    jitter      = 0.02f; ///< Cycle-to-cycle pitch irregularity.
    float    tremor      = 0.0f;  ///< Slow, shaky pitch wobble (fear, age).
    uint64_t seed        = 1;
};

/// Synthesises `phonemes` as mono 48 kHz samples (peak-normalised to ~0.9).
std::vector<float> say(const char* phonemes, const Voice& voice);

} // namespace speech
