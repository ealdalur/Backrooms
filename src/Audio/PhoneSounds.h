#pragma once
// ---------------------------------------------------------------------------
// PhoneSounds.h
// Everything heard through an office telephone, synthesised like the rest of
// the game and squeezed through the 300 - 3400 Hz telephone channel:
//   * Call progress tones on the North American precise-tone plan: dial tone
//     (350 + 440 Hz), ringback, busy, reorder (fast busy), the off-hook
//     howler, special information tones, and the touch-tone (DTMF) pairs.
//   * The line itself: hiss, mains buzz and crackle; exchange relays clicking
//     as a call is routed.
//   * The recorded operator: intercept announcements in a clear female voice
//     (formant speech), with the faint wow of an announcement machine.
//   * Things that should not be on the line: distant voices of several people
//     (formant speech, far from their receivers, breaking up), breathing,
//     and bursts of interference.
//   * The handset at the desk: keys, lifting it off the cradle, and dropping
//     it back into the cradle (a pitchless clunk and click).
//   * Jenny, at 867-5309: a young woman who has been trapped in here for
//     years and is sick of the calls - picked up, ranted and slammed down.
// Registered in SoundBank under the SoundId::Phone* ids.
// ---------------------------------------------------------------------------

#include "Audio/SoundBank.h"

#include <cstdint>
#include <vector>

namespace phonesfx {

/// A spoken line: what it says (for captions) and how (SpeechSynth phonemes).
struct Line {
    const char* text;
    const char* phonemes;
};

/// Recorded intercept announcements, in the order of the PhoneOperator variants.
enum class Announcement : uint8_t {
    NotInService,    ///< The number dialled does not exist.
    CannotComplete,  ///< Not enough digits.
    CannotGoThrough, ///< "...cannot go through at this time."
    CircuitsBusy,    ///< "All circuits are busy now."
    NotFromHere,     ///< Service numbers: "...cannot be reached from this location."
    HangUp,          ///< Left off the hook: "If you'd like to make a call..."
    NoOneLeft,       ///< (Not a real recording.) "There is no one left to call."
    StayOnLine,      ///< (Nor this.) "Please stay on the line."
    Count
};
inline constexpr int kAnnouncementCount = static_cast<int>(Announcement::Count);
const Line& announcement(Announcement a);

/// Voices on the line, in the order of the PhoneVoice variants.
enum class Voice : uint8_t {
    Help,
    WhoAreYou,
    HowLong,
    TheyCanHearYou,
    IsAnyoneThere,
    CanYouHearMe,
    DontHangUp,
    CantFindExit,
    StillHere,       ///< Several of them at once.
    BehindYou,
    Hello,
    NotSupposedTo,
    WhatYear,
    Count
};
inline constexpr int kVoiceCount = static_cast<int>(Voice::Count);
const Line& voice(Voice v);

/// The one number that gets through (an early-80s hit's refrain).
inline constexpr const char* kJennyNumber = "8675309";
/// Jenny's answer (the PhoneJenny sound) and when, into it, she starts talking.
const char* jennyText();
inline constexpr float kJennySpeechStart = 0.95f;

std::vector<Sound> makeLine(uint64_t seed);
std::vector<Sound> makeDialTone(uint64_t seed);
std::vector<Sound> makeRingback(uint64_t seed);
std::vector<Sound> makeBusy(uint64_t seed);
std::vector<Sound> makeReorder(uint64_t seed);
std::vector<Sound> makeHowler(uint64_t seed);
std::vector<Sound> makeDtmf(uint64_t seed);
std::vector<Sound> makeKey(uint64_t seed);
std::vector<Sound> makePickup(uint64_t seed);
std::vector<Sound> makeHangup(uint64_t seed);
std::vector<Sound> makeSwitching(uint64_t seed);
std::vector<Sound> makeSit(uint64_t seed);
std::vector<Sound> makeOperator(uint64_t seed);
std::vector<Sound> makeStatic(uint64_t seed);
std::vector<Sound> makeVoice(uint64_t seed);
std::vector<Sound> makeBreath(uint64_t seed);
std::vector<Sound> makeJenny(uint64_t seed);

} // namespace phonesfx
