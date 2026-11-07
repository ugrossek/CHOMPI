#pragma once
// Acid house's space in place of the room: a ping-pong delay, its time a
// dotted eighth of L0 (3/16, L0 being a bar), each repeat darker and thinner,
// over to the other side. The amount (the room's knob) sets both how loud the
// repeats are and how many come back.

#include <cmath>
#include <cstddef>

namespace meter {

class Echo {
public:
    static constexpr float kMaxSeconds = 8.f * 3.f / 16.f; // the longest L0
    static constexpr size_t kLen = size_t(kMaxSeconds * 48000.f) + 64;
    static constexpr size_t kMemory = 2 * kLen;

    void Init(float sample_rate, float* mem) {
        sr_ = sample_rate;
        l_ = mem;
        r_ = mem + kLen;
        for (size_t i = 0; i < kMemory; ++i) mem[i] = 0.f;
        lp_ = 1.f - std::exp(-2.f * float(M_PI) * 3200.f / sr_);
        hp_ = 1.f - std::exp(-2.f * float(M_PI) * 180.f / sr_);
        glide_ = 1.f - std::exp(-1.f / (.08f * sr_));
    }
    // L0 in seconds; the amount 0..1.
    void Set(float l0, float amount) {
        target_ = std::fmin(l0 * 3.f / 16.f * sr_, float(kLen - 4));
        if (d_ <= 0.f) d_ = target_;
        fb_ = .15f + .5f * amount;
    }
    void Process(float in_l, float in_r, float* out_l, float* out_r) {
        d_ += glide_ * (target_ - d_); // a new tempo glides, a tape's bend
        const float yl = Read(l_, d_), yr = Read(r_, d_);
        // The repeats: darker (lowpass) and thinner (highpass), each to the other side.
        fl_ += lp_ * (yl - fl_);
        fr_ += lp_ * (yr - fr_);
        hl_ += hp_ * (fl_ - hl_);
        hr_ += hp_ * (fr_ - hr_);
        l_[w_] = in_l + fb_ * (fr_ - hr_);
        r_[w_] = in_r + fb_ * (fl_ - hl_);
        if (++w_ >= kLen) w_ = 0;
        *out_l = yl;
        *out_r = yr;
    }

private:
    float Read(const float* b, float d) const {
        float p = float(w_) - d;
        if (p < 0.f) p += float(kLen);
        const size_t i = size_t(p);
        const float f = p - float(i);
        const size_t j = i + 1 < kLen ? i + 1 : 0;
        return b[i] + f * (b[j] - b[i]);
    }
    float sr_ = 48000.f, lp_ = 0.f, hp_ = 0.f, glide_ = 0.f;
    float d_ = 0.f, target_ = 0.f, fb_ = 0.f;
    float fl_ = 0.f, fr_ = 0.f, hl_ = 0.f, hr_ = 0.f;
    float* l_ = nullptr;
    float* r_ = nullptr;
    size_t w_ = 0;
};

} // namespace meter
