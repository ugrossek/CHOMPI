#pragma once
// A plate reverb after Jon Dattorro, "Effect Design, Part 1" (1997): input
// diffusion into a figure-eight tank of two modulated allpasses and delays.
// Smooth and dense; darker and shorter than the Rings reverb. Plain C++, the
// delay memory is handed in (SDRAM on the CHOMPI).

#include <cmath>
#include <cstddef>

namespace meter {

class Plate {
public:
    // Memory needed, in floats, at a sample rate up to 48 kHz.
    static constexpr size_t kMemory = 48000;

    void Init(float sample_rate, float* mem) {
        sr_ = sample_rate;
        const float s = sample_rate / 29761.f; // the paper's rates are at 29.761 kHz
        size_t used = 0;
        auto line = [&](Line& l, float len) {
            l.size = size_t(len * s) + 1;
            l.buf = mem + used;
            used += l.size + 32; // room for the modulation
            l.pos = 0;
        };
        line(pre_, 0.025f * 29761.f); // 25 ms pre-delay: the room sits behind the bars
        line(in_[0], 142); line(in_[1], 107); line(in_[2], 379); line(in_[3], 277);
        line(ap1_[0], 672); line(ap1_[1], 908);
        line(d1_[0], 4453); line(d1_[1], 4217);
        line(ap2_[0], 1800); line(ap2_[1], 2656);
        line(d2_[0], 3720); line(d2_[1], 3163);
        for (size_t i = 0; i < used; ++i) mem[i] = 0.f;
        scale_ = s;
        lfo_inc_ = 2.f * float(M_PI) * 0.6f / sample_rate;
        SetDecay(.5f);
        SetDamping(.6f);
    }

    // 0 short .. 1 long (about 0.6 .. 3 s).
    void SetDecay(float d) { decay_ = .35f + .5f * d; }
    // 0 bright .. 1 dark.
    void SetDamping(float d) {
        damp_ = .15f + .65f * d;
        bandwidth_ = .85f - .45f * d;
    }

    // In: the dry signal. Out: the wet signal only.
    void Process(float in_l, float in_r, float* out_l, float* out_r) {
        float x = (in_l + in_r) * .5f;
        x = pre_.Push(x);
        bw_state_ += bandwidth_ * (x - bw_state_);
        x = bw_state_;
        x = in_[0].Allpass(x, .75f);
        x = in_[1].Allpass(x, .75f);
        x = in_[2].Allpass(x, .625f);
        x = in_[3].Allpass(x, .625f);

        lfo_ += lfo_inc_;
        if (lfo_ > 2.f * float(M_PI)) lfo_ -= 2.f * float(M_PI);
        const float mod = 8.f * scale_ * .5f; // a slow, small wobble: no grain
        const float m0 = mod * (1.f + std::sin(lfo_)), m1 = mod * (1.f + std::cos(lfo_));

        for (int h = 0; h < 2; ++h) {
            float t = x + decay_ * last_[1 - h];
            t = ap1_[h].ModAllpass(t, -.7f, h ? m1 : m0);
            t = d1_[h].Push(t);
            damp_state_[h] += (1.f - damp_) * (t - damp_state_[h]);
            t = damp_state_[h] * decay_;
            t = ap2_[h].Allpass(t, .5f);
            last_[h] = d2_[h].Push(t);
        }

        auto tap = [&](Line& l, float at) { return l.Tap(size_t(at * scale_)); };
        float l = tap(d1_[1], 266) + tap(d1_[1], 2974) - tap(ap2_[1], 1913) + tap(d2_[1], 1996)
                  - tap(d1_[0], 1990) - tap(ap2_[0], 187) - tap(d2_[0], 1066);
        float r = tap(d1_[0], 353) + tap(d1_[0], 3627) - tap(ap2_[0], 1228) + tap(d2_[0], 2673)
                  - tap(d1_[1], 2111) - tap(ap2_[1], 335) - tap(d2_[1], 121);
        *out_l = l * .6f;
        *out_r = r * .6f;
    }

private:
    struct Line {
        float* buf = nullptr;
        size_t size = 1, pos = 0;
        // Write x, return what left the line (its full length ago).
        float Push(float x) {
            const float out = buf[pos];
            buf[pos] = x;
            if (++pos >= size) pos = 0;
            return out;
        }
        // The sample written `ago` samples back.
        float Tap(size_t ago) const {
            size_t i = pos + size - 1 - (ago % size);
            if (i >= size) i -= size;
            return buf[i];
        }
        float Allpass(float x, float g) {
            const float d = buf[pos];
            const float v = x + g * d;
            buf[pos] = v;
            if (++pos >= size) pos = 0;
            return d - g * v;
        }
        // An allpass whose length swings by up to `mod` samples (read with interpolation).
        float ModAllpass(float x, float g, float mod) {
            // Read size + mod samples back; the line has 32 samples of extra room.
            const size_t cap = size + 32;
            const float f = float(pos) - float(size) - mod + float(cap) * 2.f;
            const size_t i0 = size_t(f) % cap, i1 = (i0 + 1) % cap;
            const float frac = f - std::floor(f);
            const float d = buf[i0] + frac * (buf[i1] - buf[i0]);
            const float v = x + g * d;
            buf[pos] = v;
            if (++pos >= cap) pos = 0;
            return d - g * v;
        }
    };

    Line pre_, in_[4], ap1_[2], d1_[2], ap2_[2], d2_[2];
    float sr_ = 48000.f, scale_ = 1.f;
    float decay_ = .5f, damp_ = .5f, bandwidth_ = .7f, bw_state_ = 0.f;
    float damp_state_[2] = {0.f, 0.f}, last_[2] = {0.f, 0.f};
    float lfo_ = 0.f, lfo_inc_ = 0.f;
};

} // namespace meter
