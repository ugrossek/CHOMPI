#pragma once
// A small wooden room: a few early reflections, then an 8-line feedback delay
// network (Householder mixing, no modulation) with a short, soft tail. The
// bars sit in the room instead of in front of a reverb. Plain C++, the delay
// memory is handed in (SDRAM on the CHOMPI).

#include <cmath>
#include <cstddef>

namespace meter {

class Room {
public:
    static constexpr int kLines = 8;
    static constexpr size_t kMemory = 32768; // floats, up to 48 kHz

    void Init(float sample_rate, float* mem) {
        sr_ = sample_rate;
        const float s = sample_rate / 48000.f;
        // Line lengths 23 .. 62 ms, mutually prime: a room of a few metres.
        static const int kLen[kLines] = {1123, 1361, 1597, 1847, 2129, 2389, 2659, 2963};
        size_t used = 0;
        for (int i = 0; i < kLines; ++i) {
            len_[i] = size_t(kLen[i] * s);
            buf_[i] = mem + used;
            used += len_[i];
            pos_[i] = 0;
            lp_[i] = 0.f;
        }
        // Early reflections: one line, taps 7 .. 37 ms, alternating sides.
        er_len_ = size_t(.04f * sample_rate);
        er_ = mem + used;
        used += er_len_;
        er_pos_ = 0;
        for (size_t i = 0; i < used; ++i) mem[i] = 0.f;
        SetDecay(.5f);
        SetDamping(.6f);
    }

    // 0 small room .. 1 a bigger hall (RT60 about 0.7 .. 3 s).
    void SetDecay(float d) {
        const float rt60 = .7f + 2.3f * d * d;
        for (int i = 0; i < kLines; ++i)
            g_[i] = std::pow(10.f, -3.f * float(len_[i]) / (rt60 * sr_));
    }
    // 0 bright .. 1 dark: highs die faster than lows, like wood and curtains.
    void SetDamping(float d) { damp_ = .2f + .6f * d; }

    // In: the dry signal. Out: the wet signal only.
    void Process(float in_l, float in_r, float* out_l, float* out_r) {
        const float x = (in_l + in_r) * .5f;

        er_[er_pos_] = x;
        auto er = [&](float ms) {
            size_t ago = size_t(ms * .001f * sr_);
            size_t i = er_pos_ + er_len_ - ago;
            if (i >= er_len_) i -= er_len_;
            return er_[i];
        };
        const float el = .5f * er(7.f) + .35f * er(17.f) + .25f * er(29.f);
        const float erv = .45f * er(11.f) + .3f * er(23.f) + .2f * er(37.f);
        if (++er_pos_ >= er_len_) er_pos_ = 0;

        // The network: read, damp, mix (Householder), feed back with the input.
        float y[kLines], sum = 0.f;
        for (int i = 0; i < kLines; ++i) {
            y[i] = buf_[i][pos_[i]];
            lp_[i] += (1.f - damp_) * (y[i] - lp_[i]);
            y[i] = lp_[i] * g_[i];
            sum += y[i];
        }
        const float h = sum * (2.f / kLines);
        float wl = 0.f, wr = 0.f;
        for (int i = 0; i < kLines; ++i) {
            const float in = (i & 1 ? -1.f : 1.f) * (x * .35f + (i & 2 ? el : erv) * .3f);
            buf_[i][pos_[i]] = y[i] - h + in;
            if (++pos_[i] >= len_[i]) pos_[i] = 0;
            if (i & 1) wr += y[i] * (i & 2 ? 1.f : -1.f);
            else wl += y[i] * (i & 4 ? 1.f : -1.f);
        }
        *out_l = .5f * wl + .6f * el;
        *out_r = .5f * wr + .6f * erv;
    }

private:
    float* buf_[kLines] = {};
    size_t len_[kLines] = {}, pos_[kLines] = {};
    float g_[kLines] = {}, lp_[kLines] = {};
    float* er_ = nullptr;
    size_t er_len_ = 1, er_pos_ = 0;
    float sr_ = 48000.f, damp_ = .5f;
};

} // namespace meter
