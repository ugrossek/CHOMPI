#pragma once
// Breath engine: soft sung or blown tones. Three slightly detuned voices (a
// small choir) and breath noise through three vowel resonances (formants),
// with a slow swell instead of a hit. Pitch drifts a little, as a singer's
// does. Plain C++, no Daisy dependencies.
//
// Striking a voice that still sounds swells again from where it is.

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace meter {

struct BreathParams {
    float vowel      = 0.4f; // u .. o .. a .. e .. i
    float breath     = 0.3f; // 0 pure voice .. 1 mostly air
    float swell      = 0.4f; // attack: 0.02 s .. 2.5 s
    float decay      = 0.6f; // 0.2 s .. 12 s
    float variation  = 0.3f; // per-note drift and tuning spread
};

class BreathVoice {
public:
    void Init(float sample_rate, uint32_t seed = 1) {
        sr_ = sample_rate;
        active_ = false;
        rng_ = seed * 2654435761u + 3;
        for (auto& s : st_) s = {0.f, 0.f};
        for (auto& p : ph_) p = 0.f;
    }

    void Strike(float freq, const BreathParams& p, float velocity = 1.f) {
        if (freq != freq_) { for (auto& s : st_) s = {0.f, 0.f}; env_ = 0.f; }
        freq_ = freq;
        float v = std::min(std::max(p.variation, 0.f), 1.f);
        spread_ = 0.002f + v * 0.004f;          // choir detune, about 3 .. 10 cents
        drift_depth_ = 0.0008f + v * 0.0025f;   // slow pitch wander
        tune_ = 1.f + v * 0.003f * (Rand() - 0.5f);
        level_ = velocity * (1.f - v * 0.3f * Rand());
        rising_ = true;
        damped_ = false;
        SetParams(p);
        active_ = true;
    }

    void Damp() { damped_ = true; SetCoeffs(); }
    void SetParams(const BreathParams& p) { p_ = p; SetCoeffs(); }
    bool Active() const { return active_; }

    float Process() {
        if (!active_) return 0.f;
        // Envelope: swell up (smooth, from where it is), then a long fade.
        if (rising_) {
            env_ += (level_ * 1.05f - env_) * att_coef_;
            if (env_ >= level_) { env_ = level_; rising_ = false; }
        } else {
            env_ *= dec_mul_;
        }
        // Slow random drift of the pitch (a smoothed random walk).
        if ((++tick_ & 63) == 0) drift_target_ = (Rand() - 0.5f) * 2.f;
        drift_ += (drift_target_ - drift_) * 0.0004f;
        float f0 = freq_ * tune_ * (1.f + drift_depth_ * drift_);

        // Three voices: soft sawtooths (poly-BLEP), detuned around the note.
        float src = 0.f;
        static const float kDet[3] = {-1.f, 0.f, 1.f};
        for (int k = 0; k < 3; ++k) {
            float dt = f0 * (1.f + spread_ * kDet[k]) / sr_;
            ph_[k] += dt;
            if (ph_[k] >= 1.f) ph_[k] -= 1.f;
            float t = ph_[k], s = 2.f * t - 1.f;
            if (t < dt) { float x = t / dt; s -= x + x - x * x - 1.f; }
            else if (t > 1.f - dt) { float x = (t - 1.f) / dt; s -= x * x + x + x + 1.f; }
            src += s;
        }
        src *= 0.33f;
        // Soften the voices (a gentle low-pass), mix in breath noise.
        lp_ += lp_coef_ * (src - lp_);
        float noise = Rand() * 2.f - 1.f;
        float x = lp_ * (1.f - breath_) + noise * breath_ * 1.8f;

        // Vowel: three parallel band-pass resonances.
        float y = 0.f;
        for (int k = 0; k < 3; ++k) {
            float o = b0_[k] * x + st_[k].z1;
            st_[k].z1 = -a1_[k] * o + st_[k].z2;
            st_[k].z2 = -b0_[k] * x - a2_[k] * o;   // b1 = 0, b2 = -b0
            y += o * fg_[k];
        }
        float out = y * env_ * 4.5f;
        if (!rising_ && env_ < 1e-4f) active_ = false;
        return out;
    }

private:
    struct State { float z1, z2; };

    float Rand() {
        rng_ = rng_ * 1664525u + 1013904223u;
        return (rng_ >> 8) * (1.f / 16777216.f);
    }

    void SetCoeffs() {
        // Formants (Hz) and levels for u, o, a, e, i (alto-ish).
        static const float F[5][3] = {{325, 700, 2530}, {450, 800, 2830}, {800, 1150, 2900},
                                      {400, 1600, 2700}, {350, 1700, 2700}};
        static const float G[5][3] = {{1.f, .35f, .1f}, {1.f, .45f, .12f}, {1.f, .6f, .2f},
                                      {1.f, .5f, .2f}, {1.f, .35f, .2f}};
        static const float BW[3] = {80.f, 100.f, 140.f};
        float v = std::min(std::max(p_.vowel, 0.f), 1.f) * 4.f;
        int i = std::min(int(v), 3);
        float f = v - i;
        for (int k = 0; k < 3; ++k) {
            float fc = F[i][k] + (F[i + 1][k] - F[i][k]) * f;
            float g = G[i][k] + (G[i + 1][k] - G[i][k]) * f;
            // RBJ band-pass, 0 dB peak.
            float w = 2.f * float(M_PI) * std::min(fc, 0.45f * sr_) / sr_;
            float q = fc / BW[k];
            float alpha = std::sin(w) / (2.f * q);
            float a0 = 1.f + alpha;
            b0_[k] = alpha / a0;
            a1_[k] = -2.f * std::cos(w) / a0;
            a2_[k] = (1.f - alpha) / a0;
            fg_[k] = g;
        }
        breath_ = std::min(std::max(p_.breath, 0.f), 1.f);
        float cutoff = std::min(freq_ * 6.f + 400.f, 0.4f * sr_);
        lp_coef_ = 1.f - std::exp(-2.f * float(M_PI) * cutoff / sr_);
        float att = 0.02f * std::pow(125.f, p_.swell);    // 0.02 .. 2.5 s
        att_coef_ = 1.f - std::exp(-3.f / (att * sr_));
        float t60 = 0.2f * std::pow(60.f, p_.decay);       // 0.2 .. 12 s
        if (damped_) t60 = std::min(t60, 0.6f);
        dec_mul_ = std::exp(-6.9078f / (t60 * sr_));
    }

    float sr_ = 48000.f, freq_ = 0.f;
    BreathParams p_;
    float ph_[3];
    State st_[3];
    float b0_[3], a1_[3], a2_[3], fg_[3];
    float lp_ = 0.f, lp_coef_ = 0.f, breath_ = 0.f;
    float env_ = 0.f, level_ = 1.f, att_coef_ = 0.f, dec_mul_ = 0.f;
    float spread_ = 0.f, drift_ = 0.f, drift_target_ = 0.f, drift_depth_ = 0.f, tune_ = 1.f;
    uint32_t tick_ = 0;
    bool active_ = false, rising_ = false, damped_ = false;
    uint32_t rng_ = 1;
};

} // namespace meter
