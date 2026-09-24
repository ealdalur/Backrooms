// ---------------------------------------------------------------------------
// AudioEngine.cpp
// ---------------------------------------------------------------------------
#include "Audio/AudioEngine.h"

#include "Audio/SoundBank.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
constexpr int kBlockFrames = 512; ///< Frames mixed per inner pass.

/// Soft-knee limiter: linear below the knee, tanh-shaped approach to 1.0 above.
inline float limit(float x) {
    const float knee = 0.7f;
    const float a = std::fabs(x);
    if (a <= knee) return x;
    return std::copysign(knee + (1.0f - knee) * std::tanh((a - knee) / (1.0f - knee)), x);
}
} // namespace

AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::cerr << "[Audio] SDL audio unavailable (" << SDL_GetError() << "); continuing without sound\n";
        return false;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = dsp::kSampleRate;
    m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioEngine::streamCallback, this);
    if (!m_stream) {
        std::cerr << "[Audio] Could not open playback device (" << SDL_GetError() << "); continuing without sound\n";
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    m_reverb.configure(0.72f, 0.55f); // big room, heavily damped (carpet + acoustic tiles)
    m_smooth = dsp::smoothingCoefficient(0.006f);
    m_mixBuffer.assign(static_cast<size_t>(kBlockFrames) * 2, 0.0f);
    m_sendBuffer.assign(static_cast<size_t>(kBlockFrames), 0.0f);
    SDL_ResumeAudioStreamDevice(m_stream); // device streams open paused
    return true;
}

void AudioEngine::shutdown() {
    if (!m_stream) return;
    SDL_DestroyAudioStream(m_stream); // stops the callback before returning
    m_stream = nullptr;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void AudioEngine::panGains(float pan, float& l, float& r) {
    const float theta = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * (dsp::kPi * 0.25f); // equal power
    l = std::cos(theta);
    r = std::sin(theta);
}

AudioEngine::Voice* AudioEngine::resolve(VoiceHandle h) {
    if (h == 0) return nullptr;
    const uint32_t slot = (h & 0xFFu) - 1u;
    if (slot >= static_cast<uint32_t>(kMaxVoices)) return nullptr;
    Voice& v = m_voices[slot];
    return (v.active && v.generation == (h >> 8)) ? &v : nullptr;
}

const AudioEngine::Voice* AudioEngine::resolve(VoiceHandle h) const {
    return const_cast<AudioEngine*>(this)->resolve(h);
}

VoiceHandle AudioEngine::play(const Sound& sound, const VoiceParams& p) {
    if (!m_stream || sound.samples.empty()) return 0;
    std::lock_guard<std::mutex> lock(m_mutex);

    // Free slot, or steal the quietest one-shot if all are busy.
    int slot = -1;
    float quietest = 1e9f;
    for (int i = 0; i < kMaxVoices; ++i) {
        const Voice& v = m_voices[static_cast<size_t>(i)];
        if (!v.active) { slot = i; break; }
        if (!v.loop && v.targetGain < quietest) { quietest = v.targetGain; slot = i; }
    }
    if (slot < 0) return 0;

    Voice& v = m_voices[static_cast<size_t>(slot)];
    const uint32_t generation = (v.generation + 1u) & 0xFFFFFFu;
    v = Voice{};
    v.generation = generation == 0 ? 1u : generation;
    v.samples = sound.samples.data();
    v.length = sound.samples.size();
    v.pitch = std::max(0.05f, p.pitch);
    v.loop = p.loop;
    v.active = true;
    v.delaySamples = static_cast<int>(p.delay * static_cast<float>(dsp::kSampleRate));
    // One-shots start at their target values so transients are not smoothed away.
    v.gain = v.targetGain = p.gain;
    panGains(p.pan, v.targetL, v.targetR);
    v.gainL = v.targetL;
    v.gainR = v.targetR;
    v.lpCoef = v.targetLpCoef = dsp::onePoleCoefficient(p.lowpassHz);
    v.send = p.reverbSend;
    return (v.generation << 8) | static_cast<uint32_t>(slot + 1);
}

void AudioEngine::setVoice(VoiceHandle h, float gain, float pan, float lowpassHz) {
    std::lock_guard<std::mutex> lock(m_mutex);
    Voice* v = resolve(h);
    if (!v || v->stopping) return;
    v->targetGain = gain;
    panGains(pan, v->targetL, v->targetR);
    v->targetLpCoef = dsp::onePoleCoefficient(lowpassHz);
}

void AudioEngine::stop(VoiceHandle h, float fadeSeconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    Voice* v = resolve(h);
    if (!v) return;
    v->stopping = true;
    v->targetGain = 0.0f;
    v->stopRate = dsp::smoothingCoefficient(std::max(fadeSeconds, 0.002f) * 0.25f);
}

bool AudioEngine::isPlaying(VoiceHandle h) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return resolve(h) != nullptr;
}

void AudioEngine::setPaused(bool paused) {
    if (!m_stream) return;
    if (paused) SDL_PauseAudioStreamDevice(m_stream);
    else SDL_ResumeAudioStreamDevice(m_stream);
}

void AudioEngine::setMasterGain(float gain) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_master = gain;
}

void SDLCALL AudioEngine::streamCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int) {
    AudioEngine* self = static_cast<AudioEngine*>(userdata);
    int frames = additionalAmount / static_cast<int>(sizeof(float) * 2);
    while (frames > 0) {
        const int n = std::min(frames, kBlockFrames);
        self->mix(self->m_mixBuffer.data(), n);
        SDL_PutAudioStreamData(stream, self->m_mixBuffer.data(), n * static_cast<int>(sizeof(float) * 2));
        frames -= n;
    }
}

void AudioEngine::mix(float* out, int frames) {
    std::fill(out, out + frames * 2, 0.0f);
    std::fill(m_sendBuffer.begin(), m_sendBuffer.begin() + frames, 0.0f);

    std::lock_guard<std::mutex> lock(m_mutex);
    const float k = m_smooth;
    for (Voice& v : m_voices) {
        if (!v.active) continue;
        const float gk = v.stopping ? v.stopRate : k;
        for (int f = 0; f < frames; ++f) {
            if (v.delaySamples > 0) {
                --v.delaySamples;
                continue;
            }
            // Linear interpolation between neighbouring samples (pitch shifting).
            const size_t i0 = static_cast<size_t>(v.position);
            const float frac = static_cast<float>(v.position - static_cast<double>(i0));
            const size_t i1 = (i0 + 1 < v.length) ? i0 + 1 : (v.loop ? 0 : i0);
            const float x = v.samples[i0] + (v.samples[i1] - v.samples[i0]) * frac;

            v.gain += (v.targetGain - v.gain) * gk;
            v.gainL += (v.targetL - v.gainL) * k;
            v.gainR += (v.targetR - v.gainR) * k;
            v.lpCoef += (v.targetLpCoef - v.lpCoef) * k;
            v.lpState += v.lpCoef * (x - v.lpState);

            const float y = v.lpState * v.gain;
            out[f * 2 + 0] += y * v.gainL;
            out[f * 2 + 1] += y * v.gainR;
            m_sendBuffer[static_cast<size_t>(f)] += y * v.send;

            v.position += v.pitch;
            if (v.position >= static_cast<double>(v.length)) {
                if (v.loop) {
                    v.position -= static_cast<double>(v.length);
                } else {
                    v.active = false;
                    break;
                }
            }
            if (v.stopping && v.gain < 1e-4f) {
                v.active = false;
                break;
            }
        }
    }

    // Reverb send bus, master gain and limiter.
    for (int f = 0; f < frames; ++f) {
        float wl = 0.0f, wr = 0.0f;
        m_reverb.process(m_sendBuffer[static_cast<size_t>(f)], wl, wr);
        out[f * 2 + 0] = limit((out[f * 2 + 0] + wl) * m_master);
        out[f * 2 + 1] = limit((out[f * 2 + 1] + wr) * m_master);
    }
}
