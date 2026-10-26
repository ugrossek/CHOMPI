#pragma once
// Mallet engine: a modal resonator (a few tuned two-pole resonators) struck by
// a short half-sine pulse, like a mallet hitting a bar. Plain C++, no Daisy
// dependencies, so it builds for the CHOMPI and for the PC render tool.
//
// Striking a voice that still rings adds to the ring (the resonators keep
// their state), like hitting a ringing marimba bar again.
//
// Preparations: shimmer sounds every note as a pair of bars tuned slightly
// apart (Balinese ombak); buzz adds a membrane on the bar, rattling or muffling.

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace meter {

struct MalletParams {
    float material   = 0.2f;  // wood, marimba, vibraphone, bronze, glass, bell, gong
    float brightness = 0.5f;  // tilt of the upper modes
    float decay      = 0.5f;  // ring time of the fundamental, 0.05 s .. 10 s
    float strike     = 0.5f;  // mallet hardness: 0 soft (felt), 1 hard
    float variation  = 0.3f;  // per-hit randomness: hit point, hardness, force, tuning
    float shimmer    = 0.0f;  // 0 one bar .. 1 a pair beating at ~8 Hz
    float buzz       = 0.0f;  // 0 clean .. 1 strong membrane
    int   buzz_kind  = 0;     // 0 rattle (a chromatic snare), 1 muffle (eats the attack)
};

class MalletVoice {
public:
    static constexpr int kModes = 8;
    static constexpr int kAnchors = 7;

    void Init(float sample_rate, uint32_t seed = 1) {
        sr_ = sample_rate;
        Silence();
        ex_left_ = 0;
        active_ = false;
        rng_ = seed * 2654435761u + 1;
    }

    // Strike at a frequency. A ringing voice keeps its ring.
    void Strike(float freq, const MalletParams& p, float velocity = 1.f) {
        // A different note on this voice (a stolen voice) starts from silence.
        if (freq != freq_) Silence();
        freq_ = freq;
        damped_ = false;
        // Variation: no two hits are quite the same, like a real player.
        float v = std::min(std::max(p.variation, 0.f), 1.f);
        hit_pos_ = 0.22f + v * 0.36f * (Rand() - 0.5f);
        float strike = std::min(std::max(p.strike + v * 0.6f * (Rand() - 0.5f), 0.f), 1.f);
        velocity *= 1.f - v * 0.6f * Rand();
        detune_ = 1.f + v * 0.004f * (Rand() - 0.5f); // up to about +-3.5 cents
        memb_tune_ = 1.f + v * 0.12f * (Rand() - 0.5f); // the membrane sits a little differently
        slap_norm_ = std::min(220.f / freq, 4.f);        // slaps get steeper with pitch: even level
        glide_ = 1.f;
        SetParams(p);
        // Hard mallet = short contact = bright; soft = long contact = dull.
        float contact_s = 0.004f * std::pow(0.0375f, strike); // 4 ms .. 0.15 ms
        ex_len_ = std::max(2, int(contact_s * sr_));
        ex_pos_ = 0;
        ex_left_ = ex_len_;
        ex_gain_ = velocity * float(M_PI) / (2.f * ex_len_); // pulse area = velocity
        // A long (soft) contact also mutes the fundamental; make up for it so
        // soft and hard mallets sound about equally loud (limited near the
        // pulse spectrum's nulls).
        float x = 2.f * freq * ex_len_ / sr_;
        float h = std::fabs(x - 1.f) < 1e-3f ? float(M_PI) / 4.f
                                             : std::cos(float(M_PI) * 0.5f * x) / (1.f - x * x);
        ex_gain_ /= std::max(std::fabs(h), 0.15f);
        active_ = true;
        quiet_ = 0;
    }

    // Key released in play mode: damp like felt pressed on the bar, highs first.
    void Damp() { damped_ = true; SetCoeffs(); }

    // Parameter changes while ringing (knob turns).
    void SetParams(const MalletParams& p) { p_ = p; SetCoeffs(); }

    bool Active() const { return active_; }

    // Silence at once (load guard: dropping a voice beats a stalled CHOMPI).
    void Stop() { active_ = false; }

    float Process() {
        if (!active_) return 0.f;
        // Gong: the pitch starts a little sharp after the hit and settles.
        if (glide_amt_ > 0.f && (++glide_tick_ & 31) == 0) {
            glide_ *= glide_mul_;
            UpdateGlide();
        }
        float x = 0.f;
        if (ex_left_ > 0) {
            x = ex_gain_ * std::sin(float(M_PI) * (ex_pos_ + 0.5f) / ex_len_);
            ++ex_pos_;
            --ex_left_;
        }
        float out = 0.f;
        const int n = pair_ ? 2 * kModes : kModes;
        for (int m = 0; m < n; ++m) {
            float y = a1_[m] * y1_[m] - a2_[m] * y2_[m] + g_[m] * x;
            y2_[m] = y1_[m];
            y1_[m] = y;
            out += y;
        }
        // Buzz: a thin membrane on the bar. It slaps whenever the swing passes
        // the point where it touches, and every slap rings the membrane's own
        // resonances (a mirliton, like a kazoo). Quiet swings don't reach it.
        if (buzz_ > 0.f) {
            float slap;
            if (buzz_kind_ == 0) {
                // Rattle: each contact is a little different (where and how
                // hard it touches); slaps plus some sizzle.
                const float th = th_cur_;
                bool touching = std::fabs(out) > th;
                if (touching && !touching_) {
                    th_cur_ = 0.015f * (0.6f + 0.8f * Rand());
                    hit_jit_ = 0.4f + 1.2f * Rand();
                }
                touching_ = touching;
                float b = out - std::min(std::max(out, -th), th);
                slap = (b - b1_) * slap_norm_ * hit_jit_ * 0.7f
                     + std::fabs(b) * (Rand() * 2.f - 1.f) * 0.8f;
                b1_ = b;
            } else {
                // Muffle: every slap takes energy from the bar, so it eats the
                // attack and leaves a short, dry ring.
                const float th = 0.015f * key_th_;
                float b = out - std::min(std::max(out, -th), th);
                slap = (b - b1_) * slap_norm_ * (0.8f + 0.4f * Rand());
                b1_ = b;
                if (b != 0.f) {
                    float keep = 1.f - 0.0001f * buzz_ * std::min(std::fabs(b) / th, 1.f);
                    for (int m = 0; m < n; ++m) { y1_[m] *= keep; y2_[m] *= keep; }
                }
            }
            float y = 0.f;
            for (int k = 0; k < 2; ++k) {
                float z = mb_a1_[k] * mz1_[k] - mb_a2_[k] * mz2_[k] + mb_g_[k] * slap;
                mz2_[k] = mz1_[k];
                mz1_[k] = z;
                y += z;
            }
            out += y * buzz_ * buzz_gain_;
        }
        // Free the voice once it has died away.
        if (ex_left_ == 0 && std::fabs(out) < 1e-5f) {
            if (++quiet_ > 2400) active_ = false;
        } else {
            quiet_ = 0;
        }
        return out;
    }

private:
    struct Table { float ratio[kModes]; float amp[kModes]; float damp; };

    // Material anchors; material morphs between neighbours. Ratios are
    // relative to the note, except from the bell on: relative to the hum an
    // octave below.
    static const Table& Anchor(int i) {
        static const Table t[kAnchors] = {
            // wooden bar: tuned bar, highs die fast, strong fundamental
            {{1.f, 4.f, 10.f, 17.5f, 25.f, 33.f, 42.f, 52.f},
             {1.f, .45f, .2f, .08f, 0.f, 0.f, 0.f, 0.f}, 1.7f},
            // marimba: tuned bar (1 : 4 : 10)
            {{1.f, 4.f, 10.f, 17.5f, 25.f, 33.f, 42.f, 52.f},
             {1.f, .6f, .35f, .15f, 0.f, 0.f, 0.f, 0.f}, 1.2f},
            // vibraphone: same tuning, metal rings long
            {{1.f, 4.f, 10.f, 17.5f, 25.f, 33.f, 42.f, 52.f},
             {1.f, .45f, .3f, .15f, 0.f, 0.f, 0.f, 0.f}, 0.45f},
            // bronze: gamelan metallophone (thick bronze bar over a resonator)
            {{1.f, 2.52f, 4.62f, 7.15f, 10.1f, 13.4f, 17.f, 21.f},
             {1.f, .55f, .35f, .2f, .12f, 0.f, 0.f, 0.f}, 0.55f},
            // glass / glockenspiel: free bar, inharmonic
            {{1.f, 2.756f, 5.404f, 8.933f, 13.344f, 18.64f, 24.81f, 31.87f},
             {1.f, .7f, .5f, .35f, .25f, .15f, 0.f, 0.f}, 0.6f},
            // bell: hum, prime, tierce, quint, nominal, ... (hum kept low: it muddies chords)
            {{1.f, 2.f, 2.366f, 3.012f, 4.f, 5.028f, 5.324f, 6.022f},
             {.25f, 1.f, .8f, .5f, .9f, .4f, .3f, .3f}, 0.3f},
            // gong: bossed gong, strong fundamental, a few shimmering partials
            {{1.f, 2.f, 2.02f, 4.03f, 5.86f, 8.26f, 10.8f, 13.6f},
             {0.f, 1.f, .35f, .45f, .25f, .18f, .1f, .06f}, 0.35f},
        };
        return t[i];
    }

    void Silence() {
        for (int m = 0; m < 2 * kModes; ++m) y1_[m] = y2_[m] = 0.f;
        b1_ = 0.f;
        for (int k = 0; k < 2; ++k) mz1_[k] = mz2_[k] = 0.f;
    }

    float Rand() { // 0 .. 1
        rng_ = rng_ * 1664525u + 1013904223u;
        return (rng_ >> 8) * (1.f / 16777216.f);
    }

    void SetCoeffs() {
        float mat = std::min(std::max(p_.material, 0.f), 1.f) * (kAnchors - 1);
        int i0 = std::min(int(mat), kAnchors - 2);
        float f = mat - i0;
        const Table& A = Anchor(i0);
        const Table& B = Anchor(i0 + 1);
        float damp = A.damp + (B.damp - A.damp) * f;
        // From glass to bell the lowest mode moves down to the hum.
        float base = freq_ * detune_;
        if (i0 == 4) base *= 1.f - 0.5f * f;
        if (i0 >= 5) base *= 0.5f;
        // The gong's pitch settles after the hit.
        glide_amt_ = i0 == 5 ? 0.03f * f : 0.f;
        glide_mul_ = std::exp(-32.f / (0.25f * sr_));
        float t60_fund = 0.05f * std::pow(200.f, p_.decay); // 0.05 .. 10 s
        // Low notes ring a little shorter, so low chords stay clear.
        if (freq_ < 220.f) t60_fund *= std::pow(freq_ / 220.f, 0.5f);
        float tilt = (p_.brightness - 0.5f) * 2.f;           // -1 .. 1
        float pos = std::min(std::max(hit_pos_, 0.02f), 0.5f);

        buzz_ = std::min(std::max(p_.buzz, 0.f), 1.f);
        buzz_kind_ = p_.buzz_kind == 1 ? 1 : 0;
        // Membrane resonances of the buzz. Muffle: sized to the note, and
        // fixed per key (seeded by the note): each bar's membrane is its own.
        static const float kMembF[2] = {1700.f, 3300.f}, kMembBW[2] = {450.f, 900.f};
        float size = 1.f, kt = 1.f, kg = 1.f;
        key_th_ = 1.f;
        if (buzz_kind_ == 1) {
            size = std::min(std::max(std::sqrt(freq_ / 261.6f), 0.5f), 2.f);
            uint32_t h = (uint32_t(int32_t(std::lround(freq_ * 16.f))) * 2654435761u) ^ 0x5bd1e995u;
            auto r = [&h] { h = h * 1664525u + 1013904223u; return (h >> 8) * (1.f / 16777216.f); };
            kt = 1.f + 0.3f * (r() - 0.5f);
            key_th_ = 0.6f + 0.8f * r();
            kg = 0.7f + 0.6f * r();
        }
        buzz_gain_ = (buzz_kind_ == 1 ? 25.f * std::pow(size, 1.1f) * kg : 3.2f);
        for (int k = 0; k < 2; ++k) {
            float r = std::exp(-float(M_PI) * kMembBW[k] * size / sr_);
            mb_a1_[k] = 2.f * r * std::cos(2.f * float(M_PI) * kMembF[k] * memb_tune_ * size * kt / sr_);
            mb_a2_[k] = r * r;
            mb_g_[k] = 1.f - r;
        }

        float amps[kModes], t60s[kModes], norm = 0.f;
        for (int m = 0; m < kModes; ++m) {
            ratio_[m] = A.ratio[m] + (B.ratio[m] - A.ratio[m]) * f;
            float amp = A.amp[m] + (B.amp[m] - A.amp[m]) * f;
            if (m == 0) amp *= 1.f + 0.6f * buzz_; // the gourd under the bar boosts the fundamental
            amp *= std::pow(ratio_[m], tilt);
            amp *= 0.3f + 0.7f * std::fabs(std::sin(float(M_PI) * (m + 1) * pos));
            amps[m] = std::max(amp, 0.f);
            t60s[m] = t60_fund * std::pow(ratio_[m] / ratio_[0], -damp);
            // Loudness as heard: a mode that dies fast counts for less.
            float w = std::sqrt(std::min(t60s[m] / t60_fund, 1.f));
            norm += amps[m] * amps[m] * w;
        }
        // Same loudness whatever the material and brightness.
        norm = 1.f / std::sqrt(std::max(norm, 1e-6f));

        // Shimmer: a second bar tuned up to ~8 Hz higher, beating with the first.
        float sh = std::min(std::max(p_.shimmer, 0.f), 1.f);
        pair_ = sh > 0.001f;
        beat_ = 8.f * sh;
        if (pair_) norm *= 0.7071f;
        base_ = base;
        base2_ = base * (1.f + beat_ / std::max(freq_, 1.f)); // fundamental beat_ Hz higher

        for (int m = 0; m < kModes; ++m) {
            float amp = amps[m] * norm;
            float t60 = t60s[m];
            if (damped_) t60 = std::min(t60, 0.35f * std::pow(ratio_[m], -0.8f));
            t60 = std::max(t60, 0.004f);
            float r = std::exp(-6.9078f / (t60 * sr_));
            for (int k = 0; k < 2; ++k) {
                int j = m + k * kModes;
                float fm = (k ? base2_ : base) * ratio_[m];
                if (amp <= 0.f || fm > 0.45f * sr_ || (k == 1 && !pair_)) {
                    r_[j] = a1_[j] = a2_[j] = g_[j] = 0.f;
                    y1_[j] = y2_[j] = 0.f;
                    continue;
                }
                float w = 2.f * float(M_PI) * fm / sr_;
                r_[j] = r;
                a1_[j] = 2.f * r * std::cos(w);
                a2_[j] = r * r;
                g_[j] = amp * std::sin(w) * 0.35f;
            }
        }
        if (glide_amt_ > 0.f) UpdateGlide();
    }

    void UpdateGlide() {
        float g = 1.f + glide_amt_ * glide_;   // starts sharp, settles to pitch
        for (int k = 0; k < 2; ++k) {
            for (int m = 0; m < kModes; ++m) {
                int j = m + k * kModes;
                if (r_[j] == 0.f) continue;
                float w = 2.f * float(M_PI) * (k ? base2_ : base_) * ratio_[m] * g / sr_;
                a1_[j] = 2.f * r_[j] * std::cos(w);
            }
        }
    }

    float sr_ = 48000.f, freq_ = 0.f;
    MalletParams p_;
    float hit_pos_ = 0.22f, detune_ = 1.f;
    float ratio_[kModes];
    float r_[2 * kModes], a1_[2 * kModes], a2_[2 * kModes], g_[2 * kModes];
    float y1_[2 * kModes], y2_[2 * kModes];
    float base_ = 0.f, base2_ = 0.f, beat_ = 0.f;
    bool pair_ = false;
    float buzz_ = 0.f, b1_ = 0.f, slap_norm_ = 1.f, memb_tune_ = 1.f, buzz_gain_ = 0.f;
    int buzz_kind_ = 0;
    float th_cur_ = 0.015f, hit_jit_ = 1.f, key_th_ = 1.f;
    bool touching_ = false;
    float mb_a1_[2] = {}, mb_a2_[2] = {}, mb_g_[2] = {}, mz1_[2] = {}, mz2_[2] = {};
    float glide_ = 0.f, glide_amt_ = 0.f, glide_mul_ = 1.f;
    uint32_t glide_tick_ = 0;
    int ex_len_ = 0, ex_pos_ = 0, ex_left_ = 0;
    float ex_gain_ = 0.f;
    bool active_ = false, damped_ = false;
    int quiet_ = 0;
    uint32_t rng_ = 1;
};

} // namespace meter
