#pragma once
// Grit engine: a lo-fi, explorative voice. Two sine oscillators, the second
// phase-modulating the first (with feedback that turns to noise), a sine
// wavefolder, and a bitcrusher that can stutter. Struck and decaying like the
// mallet, so it plays patterns. The knobs interact on purpose: the same turn
// does different things depending on where the others are.
//
// Plain C++, no Daisy dependencies.

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace meter {

struct GritParams {
    float timbre     = 0.3f; // modulator ratio; the top quarter adds feedback (noise)
    float brightness = 0.4f; // modulation depth; the top part folds the wave
    float decay      = 0.4f; // 0.03 s .. 6 s
    float crush      = 0.0f; // 0 clean .. 1 low sample rate, few bits, stutter
    float variation  = 0.3f; // per-hit randomness of ratio and depth
};

class GritVoice {
public:
    void Init(float sample_rate, uint32_t seed = 1) {
        sr_ = sample_rate;
        active_ = false;
        rng_ = seed * 2654435761u + 7;
    }

    void Strike(float freq, const GritParams& p, float velocity = 1.f) {
        if (freq != freq_) { pc_ = pm_ = 0.f; prev_m_ = 0.f; amp_ = 0.f; }
        freq_ = freq;
        float v = std::min(std::max(p.variation, 0.f), 1.f);
        ratio_jit_ = 1.f + v * 0.04f * (Rand() - 0.5f);
        depth_jit_ = 1.f - v * 0.5f * Rand();
        target_ = velocity;   // envelope rises from where it is: no click
        attack_ = true;
        idx_env_ = 1.f;
        damped_ = false;
        SetParams(p);
        active_ = true;
    }

    void Damp() { damped_ = true; SetCoeffs(); }
    void SetParams(const GritParams& p) { p_ = p; SetCoeffs(); }
    bool Active() const { return active_; }

    float Process() {
        if (!active_) return 0.f;
        // Envelopes: 1.5 ms attack, exponential decay; depth decays faster.
        if (attack_) {
            amp_ += att_step_;
            if (amp_ >= target_) { amp_ = target_; attack_ = false; }
        } else {
            amp_ *= amp_mul_;
        }
        idx_env_ *= idx_mul_;

        // Modulator with feedback (high timbre: it breaks into noise).
        pm_ += fm_ / sr_;
        if (pm_ >= 1.f) pm_ -= 1.f;
        float m = std::sin(2.f * float(M_PI) * pm_ + fb_ * prev_m_);
        prev_m_ = m;
        pc_ += freq_ / sr_;
        if (pc_ >= 1.f) pc_ -= 1.f;
        float idx = index_ * (0.25f + 0.75f * idx_env_);
        float y = std::sin(2.f * float(M_PI) * pc_ + idx * m);
        // Sine wavefolder.
        if (fold_ > 1.f) y = std::sin(0.5f * float(M_PI) * fold_ * y);

        // Bitcrusher: sample-and-hold at a lower rate, quantized; at high
        // crush a held sample sometimes sticks (stutter).
        hold_ph_ += hold_rate_;
        if (hold_ph_ >= 1.f) {
            hold_ph_ -= 1.f;
            if (stick_left_ > 0) {
                --stick_left_;
            } else {
                held_ = levels_ > 0.f ? std::round(y * levels_) / levels_ : y;
                if (stutter_ > 0.f && Rand() < stutter_ * 0.002f)
                    stick_left_ = int(Rand() * stutter_ * 400.f);
            }
        }
        float out = held_ * amp_ * 0.5f;

        if (!attack_ && amp_ < 1e-4f) active_ = false;
        return out;
    }

private:
    float Rand() {
        rng_ = rng_ * 1664525u + 1013904223u;
        return (rng_ >> 8) * (1.f / 16777216.f);
    }

    void SetCoeffs() {
        // Ratios to wander through; in between they detune into clangs.
        static const float kRatios[] = {0.5f, 1.f, 1.5f, 2.f, 2.76f, 3.5f, 5.f, 7.13f};
        float t = std::min(std::max(p_.timbre, 0.f), 1.f) * 7.f;
        int i = std::min(int(t), 6);
        float f = t - i;
        fm_ = freq_ * (kRatios[i] + (kRatios[i + 1] - kRatios[i]) * f) * ratio_jit_;
        fb_ = std::max(0.f, (p_.timbre - 0.75f) / 0.25f) * 1.6f;

        float b = std::min(std::max(p_.brightness, 0.f), 1.f);
        index_ = 6.f * b * b * depth_jit_;
        fold_ = b > 0.6f ? 1.f + (b - 0.6f) * 7.5f : 1.f;

        float t60 = 0.03f * std::pow(200.f, p_.decay); // 0.03 .. 6 s
        if (damped_) t60 = std::min(t60, 0.15f);
        amp_mul_ = std::exp(-6.9078f / (t60 * sr_));
        idx_mul_ = std::exp(-6.9078f / (0.35f * t60 * sr_));
        att_step_ = 1.f / (0.0015f * sr_);

        float c = std::min(std::max(p_.crush, 0.f), 1.f);
        hold_rate_ = std::pow(0.04f, c);                  // 48 kHz .. ~2 kHz
        levels_ = c < 0.05f ? 0.f : std::pow(2.f, 15.f - 12.f * c); // 15 .. 3 bits
        stutter_ = std::max(0.f, (c - 0.6f) / 0.4f);
    }

    float sr_ = 48000.f, freq_ = 0.f;
    GritParams p_;
    float pc_ = 0.f, pm_ = 0.f, prev_m_ = 0.f;
    float fm_ = 0.f, fb_ = 0.f, index_ = 0.f, fold_ = 1.f;
    float amp_ = 0.f, target_ = 1.f, amp_mul_ = 0.f, att_step_ = 0.f;
    float idx_env_ = 0.f, idx_mul_ = 0.f;
    float ratio_jit_ = 1.f, depth_jit_ = 1.f;
    float hold_ph_ = 0.f, hold_rate_ = 1.f, held_ = 0.f, levels_ = 0.f, stutter_ = 0.f;
    int stick_left_ = 0;
    bool active_ = false, attack_ = false, damped_ = false;
    uint32_t rng_ = 1;
};

} // namespace meter
