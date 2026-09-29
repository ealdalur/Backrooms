// ---------------------------------------------------------------------------
// PhoneCall.cpp
// ---------------------------------------------------------------------------
#include "Gameplay/PhoneCall.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

using phonesfx::Announcement;
using phonesfx::Voice;

constexpr char  kKeys[] = "123456789*0#"; ///< Keypad order (the DTMF variants).
constexpr float kLiftTime = 0.35f;
constexpr float kOpenLineChance = 0.15f;  ///< Pick-ups that find someone already on the line.
constexpr float kDigitTimeout = 4.5f;     ///< Silence after a digit before the exchange acts on what it has.
constexpr float kDialToneTimeout = 25.0f; ///< A dial tone nobody dials into.
constexpr float kRingCycle = 6.0f;        ///< Ringback cadence: 2 s ringing, 4 s silence.
constexpr int   kMaxRings = 14;
constexpr float kWarnCooldown = 30.0f;    ///< Between warnings about the entities.
constexpr float kWarnWandererDist = 14.0f;

/// The voices heard at random; the others are kept for their moments.
const Voice kWandering[] = {Voice::Help,         Voice::WhoAreYou,  Voice::HowLong,       Voice::IsAnyoneThere,
                            Voice::CanYouHearMe, Voice::DontHangUp, Voice::CantFindExit,  Voice::StillHere,
                            Voice::NotSupposedTo, Voice::WhatYear};
/// What they say first, when they are already on the line or pick up.
const Voice kGreetings[] = {Voice::Hello, Voice::WhoAreYou, Voice::IsAnyoneThere, Voice::CanYouHearMe, Voice::HowLong};

uint64_t hashString(const std::string& s) {
    uint64_t h = 0xCA11'5EEDull;
    for (char c : s) h = rnd::hashCombine(h, static_cast<unsigned char>(c));
    return h;
}

template <size_t N>
Voice pick(const Voice (&list)[N], rnd::Rng& rng, int avoid) {
    size_t i = rng.next() % N;
    if (static_cast<int>(list[i]) == avoid) i = (i + 1 + rng.next() % (N - 1)) % N; // never twice in a row
    return list[i];
}

} // namespace

PhoneCall::PhoneCall(const SoundBank& bank, uint64_t lineSeed, uint64_t sessionSeed)
    : m_bank(bank), m_lineSeed(lineSeed), m_rng(sessionSeed) {
    m_phantomTimer = m_rng.range(6.0f, 14.0f);
}

void PhoneCall::enter(State s) {
    m_state = s;
    m_stateTime = 0.0f;
    m_step = 0;
}

void PhoneCall::sound(SoundId id, int variant, float gain, float delay, bool earpiece) {
    m_sounds.push_back({id, variant, gain, delay, earpiece});
}

std::vector<PhoneSoundEvent> PhoneCall::takeSounds() {
    std::vector<PhoneSoundEvent> out;
    out.swap(m_sounds);
    return out;
}

float PhoneCall::speak(SoundId id, int variant, float gain, float delay, const std::string& caption, bool anomalous,
                       float captionDelay) {
    sound(id, variant, gain, delay);
    const float start = m_clock + delay;
    const float end = start + m_bank.duration(id, variant);
    m_speechUntil = std::max(m_speechUntil, end);
    m_caption = caption;
    m_captionStart = start + captionDelay;
    m_captionEnd = end;
    m_captionAnomalous = anomalous;
    return end;
}

void PhoneCall::phantom(Voice v, float delay) {
    // Interference forces its way in first; the voice surfaces out of it.
    sound(SoundId::PhoneStatic, -1, 0.5f, delay);
    const float end = speak(SoundId::PhoneVoice, static_cast<int>(v), m_rng.range(0.55f, 0.85f), delay + 0.25f,
                            phonesfx::voice(v).text, true);
    m_duckUntil = std::max(m_duckUntil, end);
    m_lastVoice = static_cast<int>(v);
}

void PhoneCall::record(Announcement a, bool sit, int repeats, State next) {
    m_announcement = a;
    m_sit = sit;
    m_repeatsLeft = repeats;
    m_after = next;
    enter(State::Recording);
    scheduleRecording(0.3f);
}

void PhoneCall::scheduleRecording(float delay) {
    float at = delay;
    if (m_sit) {
        sound(SoundId::PhoneSit, 0, 0.6f, at);
        at += m_bank.duration(SoundId::PhoneSit, 0) + 0.1f;
    }
    m_recordingEnd = speak(SoundId::PhoneOperator, static_cast<int>(m_announcement), 0.8f, at,
                           phonesfx::announcement(m_announcement).text, false);
    --m_repeatsLeft;
}

void PhoneCall::press(char key) {
    const char* p = key != '\0' ? std::strchr(kKeys, key) : nullptr;
    if (!p) return;
    // The key clicks and its tone sounds in the earpiece whatever the line is doing...
    sound(SoundId::PhoneKey, -1, 0.35f, 0.0f, false);
    sound(SoundId::PhoneDtmf, static_cast<int>(p - kKeys), 0.45f);
    // ...but only a line waiting for digits takes any notice.
    if (m_state != State::DialTone && m_state != State::OpenLine && m_state != State::Seizing && m_state != State::Dialing) return;
    m_dialed += key;
    if (!m_outside && m_number.empty() && key == '9' && m_state != State::Seizing) {
        // "Dial 9 for outside line": a trunk is seized, then a second dial tone.
        m_outside = true;
        sound(SoundId::PhoneSwitching, -1, 0.3f, 0.1f);
        enter(State::Seizing);
        return;
    }
    m_number += key;
    if (m_dialed == "911") m_number = m_dialed; // the exchange lets 911 through without the outside line
    enter(State::Dialing);
    if (numberComplete()) route();
}

bool PhoneCall::numberComplete() const {
    const std::string& n = m_number;
    if (n.empty()) return false;
    if (n[0] == '*' || n[0] == '#') return n.size() >= 3;                     // feature codes
    if (n.size() == 3 && n[0] >= '2' && n[1] == '1' && n[2] == '1') return true; // 411, 911...
    if (n[0] == '1' || n[0] == '0') return n.size() >= 11;                    // long distance / operator-assisted
    return n.size() >= 7;
}

void PhoneCall::route() {
    // A lone 0 (the operator) and a 4-digit extension are only known once the
    // caller stops dialling; anything else short is simply incomplete.
    if (!numberComplete() && m_number != "0" && m_number.size() != 4) {
        record(Announcement::CannotComplete, true, 2, State::Dead);
        return;
    }
    enter(State::Routing);
    m_wait = m_rng.range(1.6f, 3.2f);
}

void PhoneCall::resolve() {
    const std::string& n = m_number;
    // The number decides its fate, so it answers the same way every time...
    const uint64_t h = rnd::hashCombine(m_lineSeed, hashString(n));
    const float roll = rnd::toUnit(h);
    // ...except that, more and more often, it is not the exchange answering at all.
    const bool wrong = m_rng.chance(0.12f + std::min(0.2f, m_clock / 600.0f));

    if (n.size() >= 7 && n.compare(n.size() - 7, 7, phonesfx::kJennyNumber) == 0) {
        // It actually rings through. She picks up fast; she knows who it is.
        m_rings = 2;
        m_answers = true;
        m_jenny = true;
        enter(State::Ringing);
    } else if (n == "0") {
        // The operator never comes on the line. Someone does.
        m_rings = 2 + static_cast<int>((h >> 8) % 3);
        m_answers = true;
        enter(State::Ringing);
    } else if (n[0] == '*' || n[0] == '#') {
        record(Announcement::CannotComplete, true, 2, State::Dead);
    } else if (n.size() == 3) {
        record(Announcement::NotFromHere, true, 2, State::Dead);
    } else if (roll < 0.32f) {
        m_rings = 3 + static_cast<int>((h >> 8) % 7);
        m_answers = wrong || rnd::toUnit(rnd::hashCombine(h, 1)) < 0.3f;
        enter(State::Ringing);
    } else if (roll < 0.47f) {
        enter(State::Busy);
    } else if (wrong) {
        if (m_rng.chance(0.5f)) record(Announcement::NoOneLeft, true, 1, State::Dead);
        else                    record(Announcement::StayOnLine, false, 1, State::Answered);
    } else if (roll < 0.67f) {
        record(Announcement::NotInService, true, 2, State::Dead);
    } else if (roll < 0.85f) {
        record(Announcement::CannotGoThrough, true, 2, State::Dead);
    } else {
        record(Announcement::CircuitsBusy, true, 2, State::Reorder);
    }
}

void PhoneCall::update(float dt, const PhoneContext& ctx) {
    m_clock += dt;
    m_stateTime += dt;
    m_warnCooldown = std::max(0.0f, m_warnCooldown - dt);

    switch (m_state) {
    case State::Lifting:
        if (m_stateTime > kLiftTime) enter(m_rng.chance(kOpenLineChance) ? State::OpenLine : State::DialTone);
        break;
    case State::OpenLine:
        // No dial tone. Breathing; a voice; then, as if nothing happened, the dial tone.
        if (m_step == 0) {
            sound(SoundId::PhoneBreath, -1, 0.5f, 0.3f);
            m_step = 1;
        } else if (m_step == 1 && m_stateTime > 2.4f) {
            phantom(pick(kGreetings, m_rng, m_lastVoice));
            m_step = 2;
        } else if (m_step == 2 && m_clock > m_speechUntil + 1.2f) {
            sound(SoundId::PhoneStatic, -1, 0.25f);
            enter(State::DialTone);
        }
        break;
    case State::DialTone:
        if (m_stateTime > kDialToneTimeout) record(Announcement::HangUp, false, 1, State::Howler);
        break;
    case State::Seizing:
        if (m_stateTime > 0.8f) enter(State::DialTone);
        break;
    case State::Dialing:
        if (m_stateTime > kDigitTimeout) route();
        break;
    case State::Routing:
        if (m_step == 0) {
            sound(SoundId::PhoneSwitching, -1, 0.6f, 0.2f);
            m_step = 1;
        }
        if (m_stateTime > m_wait) resolve();
        break;
    case State::Ringing:
        // Answered in the silence after its last ring - or never.
        if (m_answers && m_stateTime > static_cast<float>(m_rings - 1) * kRingCycle + 2.6f) {
            enter(m_jenny ? State::Jenny : State::Answered);
        }
        else if (m_stateTime > static_cast<float>(kMaxRings) * kRingCycle) enter(State::Dead);
        break;
    case State::Answered:
        // A click, someone breathing close to the mouthpiece, a voice or two, then nothing.
        if (m_step == 0) {
            sound(SoundId::PhoneStatic, -1, 0.3f);
            sound(SoundId::PhoneBreath, -1, 0.55f, 0.6f);
            m_step = 1;
        } else if (m_step == 1 && m_stateTime > 3.2f) {
            phantom(pick(kGreetings, m_rng, m_lastVoice));
            m_step = 2;
        } else if (m_step == 2 && m_clock > m_speechUntil + 1.5f) {
            if (m_rng.chance(0.6f)) phantom(pick(kWandering, m_rng, m_lastVoice));
            m_step = 3;
        } else if (m_step == 3 && m_clock > m_speechUntil + 2.0f) {
            sound(SoundId::PhoneSwitching, -1, 0.35f);
            enter(State::Dead);
        }
        break;
    case State::Jenny:
        // She says her piece, slams the phone down, and that is the end of that.
        if (m_step == 0) {
            m_wait = speak(SoundId::PhoneJenny, 0, 0.9f, 0.0f, phonesfx::jennyText(), false, phonesfx::kJennySpeechStart) - m_clock;
            m_step = 1;
        } else if (m_stateTime > m_wait + 0.2f) {
            m_hungUp = true;
        }
        break;
    case State::Busy:
        if (m_stateTime > 18.0f) record(Announcement::HangUp, false, 1, State::Howler);
        break;
    case State::Recording:
        // A voice may have broken in between repeats: the machine waits it out.
        if (m_clock > std::max(m_recordingEnd, m_speechUntil) + 1.2f) {
            if (m_repeatsLeft > 0) scheduleRecording(0.0f);
            else enter(m_after);
        }
        break;
    case State::Dead:
        if (m_stateTime > 3.0f) enter(State::Reorder);
        break;
    case State::Reorder:
        if (m_stateTime > 12.0f) record(Announcement::HangUp, false, 1, State::Howler);
        break;
    case State::Howler:
        break;
    }
    updatePhantoms(dt, ctx);
}

void PhoneCall::updatePhantoms(float dt, const PhoneContext& ctx) {
    if (m_state == State::Lifting || m_jenny || speaking()) return; // not even they call Jenny
    // They notice what is happening around the player.
    if (m_warnCooldown <= 0.0f) {
        if (ctx.stalkerBehind) {
            phantom(Voice::BehindYou);
            m_warnCooldown = kWarnCooldown;
            return;
        }
        if (ctx.wandererDistance > 0.0f && ctx.wandererDistance < kWarnWandererDist) {
            phantom(Voice::TheyCanHearYou);
            m_warnCooldown = kWarnCooldown;
            return;
        }
    }
    if (m_state == State::OpenLine || m_state == State::Answered) return; // they are already here
    if ((m_phantomTimer -= dt) > 0.0f) return;
    // More often the longer the player listens.
    m_phantomTimer = m_rng.range(10.0f, 22.0f) * std::max(0.4f, 1.0f - m_clock / 240.0f);
    if (m_rng.chance(0.25f)) { // it almost gets through
        sound(SoundId::PhoneStatic, -1, 0.45f);
        m_duckUntil = m_clock + 0.8f;
        return;
    }
    phantom(pick(kWandering, m_rng, m_lastVoice));
}

SoundId PhoneCall::tone() const {
    switch (m_state) {
    case State::DialTone: return SoundId::PhoneDialTone;
    case State::Ringing:  return SoundId::PhoneRingback;
    case State::Busy:     return SoundId::PhoneBusy;
    case State::Reorder:  return SoundId::PhoneReorder;
    case State::Howler:   return SoundId::PhoneHowler;
    default:              return SoundId::Count;
    }
}

float PhoneCall::toneGain() const {
    float g = 1.0f;
    if (m_state == State::Howler) g = 0.5f + 0.5f * std::min(1.0f, m_stateTime / 20.0f); // it gets louder
    // Under interference the tone falters and wavers.
    if (m_clock < m_duckUntil) g *= 0.12f + 0.12f * std::sin(m_clock * 37.0f) * std::sin(m_clock * 5.3f);
    return std::max(0.0f, g);
}

float PhoneCall::lineGain() const {
    float g = 1.0f;
    if (m_state == State::OpenLine || m_state == State::Answered) g = 1.8f;
    else if (m_state == State::Dead) g = 0.35f;
    if (m_clock < m_duckUntil) g = std::max(g, 2.2f);
    return g;
}

float PhoneCall::captionAlpha() const {
    if (m_caption.empty() || m_clock < m_captionStart) return 0.0f;
    const float in = std::min(1.0f, (m_clock - m_captionStart) / 0.25f);
    const float out = 1.0f - std::clamp((m_clock - m_captionEnd) / 0.8f, 0.0f, 1.0f);
    return in * out;
}
