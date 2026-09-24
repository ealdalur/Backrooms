#pragma once
// ---------------------------------------------------------------------------
// Dsp.h
// Small DSP building blocks shared by the offline procedural sound
// synthesiser (SoundBank) and the real-time mixer (AudioEngine).
// ---------------------------------------------------------------------------

#include <cmath>
#include <vector>

namespace dsp {

inline constexpr int   kSampleRate = 48000;
inline constexpr float kPi         = 3.14159265358979f;
inline constexpr float kTwoPi      = 6.28318530717959f;

/// Second-order IIR filter (RBJ "Audio EQ Cookbook"), transposed direct form II.
/// configure() may be called at any time to re-tune without resetting state,
/// which allows smooth sweeps (e.g. vocal formant glides).
class Biquad {
public:
    enum class Type { Lowpass, Highpass, Bandpass };

    Biquad() = default;
    Biquad(Type type, float hz, float q) { configure(type, hz, q); }

    static Biquad lowpass(float hz, float q = 0.7071f) { return {Type::Lowpass, hz, q}; }
    static Biquad highpass(float hz, float q = 0.7071f) { return {Type::Highpass, hz, q}; }
    /// Band-pass with 0 dB peak gain: acts as a resonator whose ring time grows with Q.
    static Biquad bandpass(float hz, float q) { return {Type::Bandpass, hz, q}; }

    void configure(Type type, float hz, float q) {
        const float w0 = kTwoPi * std::fmin(hz, 0.45f * kSampleRate) / kSampleRate;
        const float cw = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        float b0, b1, b2;
        switch (type) {
        case Type::Lowpass:  b0 = (1.0f - cw) * 0.5f; b1 = 1.0f - cw;    b2 = b0;     break;
        case Type::Highpass: b0 = (1.0f + cw) * 0.5f; b1 = -(1.0f + cw); b2 = b0;     break;
        default:             b0 = alpha;              b1 = 0.0f;         b2 = -alpha; break;
        }
        const float a0 = 1.0f + alpha;
        m_b0 = b0 / a0;
        m_b1 = b1 / a0;
        m_b2 = b2 / a0;
        m_a1 = -2.0f * cw / a0;
        m_a2 = (1.0f - alpha) / a0;
    }

    float process(float x) {
        const float y = m_b0 * x + m_z1;
        m_z1 = m_b1 * x - m_a1 * y + m_z2;
        m_z2 = m_b2 * x - m_a2 * y;
        return y;
    }

private:
    float m_b0 = 1.0f, m_b1 = 0.0f, m_b2 = 0.0f, m_a1 = 0.0f, m_a2 = 0.0f;
    float m_z1 = 0.0f, m_z2 = 0.0f;
};

/// Coefficient of a one-pole low-pass for a cutoff frequency.
inline float onePoleCoefficient(float hz) {
    return 1.0f - std::exp(-kTwoPi * hz / static_cast<float>(kSampleRate));
}

/// Coefficient for per-sample exponential smoothing with time constant `seconds`.
inline float smoothingCoefficient(float seconds) {
    return 1.0f - std::exp(-1.0f / (seconds * static_cast<float>(kSampleRate)));
}

/// Stereo Schroeder/Moorer reverb (the classic "Freeverb" topology): eight
/// damped feedback combs in parallel followed by four series all-passes per
/// channel, the right channel offset for stereo width. Used live on the
/// mixer's send bus and offline to bake distance into far-away sounds.
class Reverb {
public:
    /// @param roomSize 0..1, longer tails as it grows.
    /// @param damping  0..1, high-frequency absorption (carpet & ceiling tiles damp a lot).
    void configure(float roomSize, float damping) {
        static const int kCombTuning[8]    = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int kAllpassTuning[4] = {556, 441, 341, 225};
        const int spread = 23;
        const float scale = static_cast<float>(kSampleRate) / 44100.0f; // tunings are for 44.1 kHz
        const float feedback = 0.7f + 0.28f * roomSize;
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < 8; ++i) {
                m_combs[c][i].init(static_cast<int>((kCombTuning[i] + c * spread) * scale), feedback, damping);
            }
            for (int i = 0; i < 4; ++i) {
                m_allpasses[c][i].init(static_cast<int>((kAllpassTuning[i] + c * spread) * scale));
            }
        }
    }

    /// Processes one mono input sample into a stereo wet signal.
    void process(float input, float& outL, float& outR) {
        const float in = input * 0.015f; // Freeverb's fixed input gain
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < 8; ++i) {
            l += m_combs[0][i].process(in);
            r += m_combs[1][i].process(in);
        }
        for (int i = 0; i < 4; ++i) {
            l = m_allpasses[0][i].process(l);
            r = m_allpasses[1][i].process(r);
        }
        outL = l;
        outR = r;
    }

private:
    struct Comb {
        std::vector<float> buffer;
        size_t index = 0;
        float store = 0.0f, feedback = 0.0f, damp = 0.0f;
        void init(int size, float fb, float dampAmount) {
            buffer.assign(static_cast<size_t>(size), 0.0f);
            index = 0;
            store = 0.0f;
            feedback = fb;
            damp = dampAmount;
        }
        float process(float x) {
            const float out = buffer[index];
            store = out * (1.0f - damp) + store * damp;
            buffer[index] = x + store * feedback;
            if (++index >= buffer.size()) index = 0;
            return out;
        }
    };
    struct Allpass {
        std::vector<float> buffer;
        size_t index = 0;
        void init(int size) {
            buffer.assign(static_cast<size_t>(size), 0.0f);
            index = 0;
        }
        float process(float x) {
            const float delayed = buffer[index];
            buffer[index] = x + delayed * 0.5f;
            if (++index >= buffer.size()) index = 0;
            return delayed - x;
        }
    };

    Comb    m_combs[2][8];
    Allpass m_allpasses[2][4];
};

} // namespace dsp
