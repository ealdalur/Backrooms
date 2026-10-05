#pragma once
// ---------------------------------------------------------------------------
// PhoneCall.h
// What happens on the line while the player holds a desk phone's handset.
//
// It behaves like a real 1980s office line behind a PBX, down to its
// uselessness: lift the handset and there is a dial tone; the first digit
// silences it; "9" first seizes an outside line (a click, a second dial
// tone). A complete number - 7 digits, 1 + 10, a 3-digit service code, a
// 4-digit extension left to time out, or 0 for the operator - is routed
// (relays clicking far away) and then fails, the way calls fail: it rings
// and rings, or it is busy, or special information tones and a recording
// explain why it cannot go through. Left too long, the exchange gives up
// ("If you'd like to make a call...") and the off-hook howler starts. What
// a number does is fixed by the number, so redialling it gets the same answer.
//
// But the line is not empty. Now and then - more often the longer the player
// listens - a burst of interference forces its way onto it, the tone
// falters, and a voice comes through: faint, far away, breaking up. Several
// different people, none of whom should be there. Some pick-ups find no dial
// tone at all, only someone breathing. Some calls are answered - by them.
// Some recordings are not recordings. And they notice the world: when the
// Stalker is behind the player, or the Wanderer is close, they say so.
//
// Some phones have a message waiting (their lamp blinks). The message key -
// the lamp itself - drops whatever the line is doing and calls the voicemail
// system: "You have one new message", a beep, someone in distress, two beeps,
// "End of message", and the dial tone again. Once it has played, it is gone.
//
// One number does get through: 867-5309. Jenny has been stuck in here for
// years, everyone keeps calling, and she has had enough. She hangs up on
// the player, which ends the call (hungUp).
//
// And one more: the number written on a wall (the puzzle chain's clue). It
// rings twice, and someone answers through the static, in a hurry: "I was
// able to overwrite the memory at 7A9F... ping that, hurry though, I don't
// know how long it will last" - and the line drops back to the dial tone
// (clueAnswered reports it).
//
// A phone that was ringing when it was picked up (incoming) has someone on
// the line already.
//
// The call only asks for sounds (takeSounds) and reports which tone should
// loop (tone); the Engine plays them. Lines of speech come with captions.
// ---------------------------------------------------------------------------

#include "Audio/PhoneSounds.h"
#include "Audio/SoundBank.h"
#include "Math/Random.h"

#include <cstdint>
#include <string>
#include <vector>

/// What the line may know about the world outside the handset.
struct PhoneContext {
    bool  stalkerBehind = false;     ///< The Stalker is close, and outside the player's view.
    float wandererDistance = -1.0f;  ///< < 0 when the Wanderer is not around.
};

/// The phone's voicemail box.
struct PhoneMailbox {
    int  message = -1;        ///< The phonesfx::Message waiting, or -1 for none.
    bool miscounts = false;   ///< The system announces "nine hundred ninety-nine new messages".
};

/// A sound the call asks for. SoundId::Count in the earpiece means: silence
/// everything still playing there (the line has moved on).
struct PhoneSoundEvent {
    SoundId id;
    int     variant;  ///< < 0: any.
    float   gain;
    float   delay;    ///< Seconds from now.
    bool    earpiece; ///< In the player's ear; false = at the phone on the desk.
};

class PhoneCall {
public:
    /// @param bank        For the length of every recording and voice.
    /// @param lineSeed    Decides what each number does (fixed per world).
    /// @param sessionSeed Varies everything else from one pick-up to the next.
    /// @param mailbox     What the phone's voicemail box holds.
    /// @param clueNumber  The digits of the number on the wall ("" for none).
    /// @param incoming    The phone was ringing: someone is already on the line.
    PhoneCall(const SoundBank& bank, uint64_t lineSeed, uint64_t sessionSeed, const PhoneMailbox& mailbox,
              const std::string& clueNumber = std::string(), bool incoming = false);

    /// A keypad key ("123456789*0#"), from the keyboard or a click.
    void press(char key);
    /// The message key (the lamp): calls the voicemail system.
    void pressMessage();
    /// The waiting message is being played back (its lamp burns steadily)...
    bool messagePlaying() const;
    /// ...and has now been heard to the end (its lamp goes out for good).
    bool messageHeard() const { return m_messageHeard; }

    void update(float dt, const PhoneContext& ctx);

    /// The looping tone the earpiece should play now (SoundId::Count: none) and its level.
    SoundId tone() const;
    float toneGain() const;
    /// Level of the open line's hiss (it swells when something is on the line).
    float lineGain() const;

    /// Everything keyed in since the pick-up.
    const std::string& dialed() const { return m_dialed; }

    /// The words being heard on the line (empty if none), their visibility
    /// (0..1), and whether they come from one of the voices rather than the operator.
    const std::string& caption() const;
    float captionAlpha() const;
    bool captionAnomalous() const;

    /// Returns and clears the sounds requested since the last call.
    std::vector<PhoneSoundEvent> takeSounds();

    /// The other end hung up on the player: the call is over.
    bool hungUp() const { return m_hungUp; }
    /// The number on the wall was answered (the memory has been overwritten).
    bool clueAnswered() const { return m_clueAnswered; }

private:
    enum class State : uint8_t {
        Lifting,   ///< The handset coming up to the ear.
        OpenLine,  ///< No dial tone: someone is already on the line.
        DialTone,
        Seizing,   ///< "9": grabbing an outside line.
        Dialing,   ///< Digits going out; silence between them.
        Routing,   ///< Relays clicking somewhere far away.
        Ringing,
        Answered,  ///< Picked up at the other end - by one of them.
        Jenny,     ///< Picked up at 867-5309.
        Clue,      ///< Picked up at the number on the wall: one hurried message, then the line drops.
        Voicemail, ///< Listening to the voicemail system.
        Busy,
        Recording, ///< Special information tones and / or an announcement.
        Dead,      ///< Nothing at all.
        Reorder,   ///< Fast busy.
        Howler,    ///< Left off the hook too long.
    };

    void enter(State s);
    void sound(SoundId id, int variant, float gain, float delay = 0.0f, bool earpiece = true);
    /// Plays speech in the earpiece and captions it (from `captionDelay` into
    /// the sound). Returns its end time.
    float speak(SoundId id, int variant, float gain, float delay, const std::string& caption, bool anomalous,
                float captionDelay = 0.0f);
    /// Silences the earpiece and forgets its speech: the line has moved on.
    void cancelEarpiece();
    /// One of the voices breaking through: interference, then the voice, the tone ducking under both.
    void phantom(phonesfx::Voice v, float delay = 0.0f);
    /// Plays an announcement `repeats` times (optionally after the SIT tones), then enters `next`.
    void record(phonesfx::Announcement a, bool sit, int repeats, State next);
    void scheduleRecording(float delay);
    /// The number is complete (or timed out): route it and decide its fate.
    void route();
    void resolve();
    bool numberComplete() const;
    void updatePhantoms(float dt, const PhoneContext& ctx);
    bool speaking() const { return m_clock < m_speechUntil; }

    struct Caption {
        std::string text;
        float start, end;
        bool  anomalous;
    };
    /// The caption on screen now (the latest to have started), or null.
    const Caption* currentCaption() const;

    const SoundBank& m_bank;
    uint64_t m_lineSeed;
    rnd::Rng m_rng;

    State  m_state = State::Lifting;
    float  m_stateTime = 0.0f;
    float  m_wait = 0.0f;         ///< How long the current state lasts, where that is decided on entry.
    float  m_clock = 0.0f;        ///< Seconds since the pick-up.
    int    m_step = 0;            ///< Progress through a scripted state (open line, answered).

    std::string m_dialed;         ///< Everything keyed in (shown on the HUD).
    std::string m_number;         ///< The number proper (after any outside-line 9).
    bool   m_outside = false;     ///< An outside line was seized.

    int    m_rings = 0;           ///< Rings before it is answered (or given up on).
    bool   m_answers = false;
    bool   m_jenny = false;       ///< This call is to 867-5309 (nothing else gets on the line).
    bool   m_hungUp = false;
    std::string m_clueNumber;     ///< The number on the wall.
    bool   m_clue = false;        ///< This call is to it (nothing else gets on the line).
    bool   m_clueAnswered = false;
    bool   m_incoming = false;

    PhoneMailbox m_mailbox;
    float  m_messageEnd = 0.0f;   ///< Clock time the message being played back ends.
    bool   m_messageHeard = false;

    phonesfx::Announcement m_announcement = phonesfx::Announcement::NotInService;
    bool   m_sit = false;
    int    m_repeatsLeft = 0;
    State  m_after = State::Dead; ///< Where a recording leads.
    float  m_recordingEnd = 0.0f;

    float  m_speechUntil = 0.0f;  ///< Clock time the earpiece's current speech ends.
    float  m_duckUntil = 0.0f;    ///< The tone falters under interference until then.
    float  m_phantomTimer = 0.0f;
    float  m_warnCooldown = 0.0f;
    int    m_lastVoice = -1;

    std::vector<Caption> m_captions; ///< Lines scheduled or still showing, in start order.

    std::vector<PhoneSoundEvent> m_sounds;
};
