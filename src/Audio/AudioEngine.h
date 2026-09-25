#pragma once
// ---------------------------------------------------------------------------
// AudioEngine.h
// Real-time software mixer on top of SDL3's audio stream API (no
// SDL3_mixer: every sound is synthesised PCM, so only mixing is needed).
//
// Features: 48 voices of 48 kHz float audio, per-voice gain / equal-power
// pan / pitch / one-pole low-pass (occlusion) with click-free per-sample
// smoothing, optional start delay, looping, voice stealing, a stereo reverb
// send bus and a soft-knee limiter on the master output.
//
// Threading: the SDL audio thread calls mix(); the game thread calls the
// public API. Both sides serialise on a mutex held only for short, bounded
// sections.
// ---------------------------------------------------------------------------

#include "Audio/Dsp.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

struct Sound;

/// Opaque handle to a playing voice (0 = invalid). Encodes slot + generation,
/// so a handle to a finished voice never controls a newer one in its slot.
using VoiceHandle = uint32_t;

/// Parameters of a voice. Gain / pan / low-pass can be changed while playing.
struct VoiceParams {
    float gain       = 1.0f;     ///< Linear amplitude.
    float pan        = 0.0f;     ///< -1 = hard left, +1 = hard right.
    float pitch      = 1.0f;     ///< Playback rate multiplier.
    float lowpassHz  = 20000.0f; ///< One-pole low-pass cutoff (muffling).
    float reverbSend = 0.1f;     ///< Amount sent to the reverb bus.
    float delay      = 0.0f;     ///< Seconds to wait before starting.
    float startOffset = 0.0f;    ///< Start position as a fraction (0..1) of the sound.
    bool  loop       = false;
};

class AudioEngine {
public:
    static constexpr int kMaxVoices = 48;

    AudioEngine() = default;
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /// Opens the default playback device. Returns false (and the engine
    /// stays silent but safe to call) if no audio device is available.
    bool init();
    void shutdown();
    bool available() const { return m_stream != nullptr; }

    /// Starts a sound. Returns 0 if the engine is unavailable.
    VoiceHandle play(const Sound& sound, const VoiceParams& params);

    /// Updates a playing voice's targets; changes glide over a few ms.
    void setVoice(VoiceHandle voice, float gain, float pan, float lowpassHz);

    /// Fades a voice out and frees it.
    void stop(VoiceHandle voice, float fadeSeconds = 0.05f);

    bool isPlaying(VoiceHandle voice) const;

    /// Pauses / resumes the output device.
    void setPaused(bool paused);

    void setMasterGain(float gain);

private:
    struct Voice {
        const float* samples = nullptr;
        size_t       length = 0;
        double       position = 0.0;
        float        pitch = 1.0f;
        bool         loop = false;
        bool         active = false;
        bool         stopping = false;
        uint32_t     generation = 0;
        int          delaySamples = 0;
        // Current (smoothed) and target mix parameters.
        float gain = 0.0f, targetGain = 0.0f;
        float gainL = 0.7071f, gainR = 0.7071f, targetL = 0.7071f, targetR = 0.7071f;
        float lpCoef = 1.0f, targetLpCoef = 1.0f, lpState = 0.0f;
        float send = 0.0f;
        float stopRate = 0.0f;
    };

    static void SDLCALL streamCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount);
    void mix(float* out, int frames);
    Voice* resolve(VoiceHandle h);
    const Voice* resolve(VoiceHandle h) const;
    static void panGains(float pan, float& l, float& r);

    SDL_AudioStream*     m_stream = nullptr;
    mutable std::mutex   m_mutex;
    std::array<Voice, kMaxVoices> m_voices{};
    dsp::Reverb          m_reverb;
    std::vector<float>   m_mixBuffer;
    std::vector<float>   m_sendBuffer;
    float                m_master = 0.8f;
    float                m_smooth = 0.0f; ///< Per-sample parameter smoothing coefficient.
};
