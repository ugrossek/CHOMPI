#pragma once
// Small DSP kit for the CIRCLES sound-design study. Everything here is cheap
// enough for an STM32H7 voice: two-pole resonators, a TPT state-variable
// filter, one-pole filters, fractional delay lines, a half-sine mallet pulse.
// Plain C++, no allocation after Init.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sd {

constexpr float kPi = 3.14159265358979f;
constexpr float kTau = 6.28318530717959f;

inline float Clamp(float x, float a, float b) { return std::min(std::max(x, a), b); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float Cents(float c) { return std::pow(2.f, c / 1200.f); }

// sin(2 pi p) for any p, to about 0.1 %: two parabolas (no libm call).
inline float Sin1(float p) {
    p -= float(int(p));      // the phases here are >= 0: no floor() call
    if (p < 0.f) p += 1.f;
    float x = p < .5f ? p : p - 1.f;           // -0.5 .. 0.5 turns
    x *= 2.f;                                   // -1 .. 1 (half turns)
    float y = 4.f * x * (1.f - std::fabs(x));   // parabola through sin
    return y * (.775f + .225f * std::fabs(y));  // refined
}
inline float Cos1(float p) { return Sin1(p + .25f); }
// tanh, to about 2 % (a soft saturation, not a math library).
inline float Tanh(float x) {
    x = Clamp(x, -3.f, 3.f);
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

struct Rng {
    uint32_t s = 1;
    void Seed(uint32_t x) { s = x * 2654435761u + 0x9e3779b9u; if (!s) s = 1; }
    float U() { s = s * 1664525u + 1013904223u; return (s >> 8) * (1.f / 16777216.f); } // 0..1
    float B() { return 2.f * U() - 1.f; }                                               // -1..1
};

// The six knobs every CIRCLES instrument answers to (0..1 each).
// K1 strike, K2 brightness, K3 decay; with CHOMPI: K1 shimmer, K2 feel, K3 prep.
struct Knobs {
    float strike = .5f, bright = .5f, decay = .5f;
    float shimmer = 0.f, feel = .3f, prep = 0.f;
};

// A two-pole resonator: one mode of a bar, a bowl, a body. Its impulse
// response is amp * r^n * sin(w n): a decaying sine of amplitude amp.
struct Res {
    float a1 = 0, a2 = 0, g = 0, y1 = 0, y2 = 0, w = 0, amp = 0, sr = 48000;
    void Set(float f, float t60, float a, float sample_rate) {
        sr = sample_rate;
        if (f <= 0.f || f >= .45f * sr || a == 0.f) { a1 = a2 = g = 0.f; w = 0.f; return; }
        w = kTau * f / sr;
        amp = a;
        Decay(t60);
    }
    // Change the ring time and keep the pitch (a damping hand).
    void Decay(float t60) {
        if (w == 0.f) return;
        const float r = std::exp(-6.9078f / (std::max(t60, .002f) * sr));
        a1 = 2.f * r * std::cos(w);
        a2 = r * r;
        g = amp * std::sin(w);
    }
    // Same, but driven by noise (a bowed or rubbed mode): a bandpass with
    // zeros at DC and Nyquist, so noise far from the mode stays out, scaled so
    // the output's spread is amp times the input's, whatever the ring time.
    void SetNoise(float f, float t60, float a, float sample_rate) {
        Set(f, t60, a, sample_rate);
        if (w == 0.f) return;
        const float r = std::sqrt(a2);
        bp = true;
        g = a * std::sqrt(1.f - r);
    }
    void Retune(float f) { // keep r, new pitch
        if (w == 0.f || f >= .45f * sr) return;
        w = kTau * f / sr;
        a1 = 2.f * std::sqrt(a2) * std::cos(w);
    }
    float Tick(float x) {
        const float in = bp ? x - x2 : x;
        x2 = x1; x1 = x;
        const float y = a1 * y1 - a2 * y2 + g * in;
        y2 = y1; y1 = y;
        return y;
    }
    void Clear() { y1 = y2 = x1 = x2 = 0.f; }
    bool bp = false;
    float x1 = 0, x2 = 0;
};

// A contact: a loose part (a bottle cap, a ring on a tin plate, a bolt) that
// the vibration throws against its seat once per swing past a threshold. Each
// landing is a slap, a little different every time, that rings the part's
// own two resonances. So the buzz follows the note's pitch, as a real one does.
struct Contact {
    Res ring[2];
    float th = .02f, th_cur = .02f, b1 = 0.f, jit = 1.f;
    bool touching = false;
    void Set(float f1, float f2, float t60, float threshold, float sr) {
        ring[0].Set(f1, t60, 1.f, sr);
        ring[1].Set(f2, t60 * .7f, .7f, sr);
        th = th_cur = threshold;
    }
    float Tick(float x, Rng& r) {
        const bool t = std::fabs(x) > th_cur;
        if (t && !touching) { th_cur = th * (.6f + .8f * r.U()); jit = .4f + 1.2f * r.U(); }
        touching = t;
        const float b = x - Clamp(x, -th_cur, th_cur);
        const float slap = (b - b1) * jit + std::fabs(b) * r.B() * .15f;
        b1 = b;
        return ring[0].Tick(slap) + ring[1].Tick(slap);
    }
};

// Topology-preserving state-variable filter (Zavalishin): stable when swept.
struct Svf {
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    float lp = 0, bp = 0, hp = 0;
    void Set(float f, float q, float sr) {
        g = std::tan(kPi * Clamp(f, 10.f, .45f * sr) / sr);
        k = 1.f / q;
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void Tick(float v0) {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
    float Bp(float x) { Tick(x); return bp * k; } // unity gain at the centre
    float Lp(float x) { Tick(x); return lp; }
    float Hp(float x) { Tick(x); return hp; }
    void Clear() { ic1 = ic2 = 0.f; }
};

struct OnePole {
    float k = 1, y = 0;
    void Set(float f, float sr) { k = 1.f - std::exp(-kTau * f / sr); }
    float Lp(float x) { y += k * (x - y); return y; }
    float Hp(float x) { y += k * (x - y); return x - y; }
};

// A delay line, power-of-two long, read with linear or allpass interpolation.
struct Delay {
    std::vector<float> b;
    int mask = 0, w = 0;
    float ap = 0.f; // allpass interpolator state
    void Init(int min_len) {
        int n = 1;
        while (n < min_len + 4) n <<= 1;
        b.assign(n, 0.f);
        mask = n - 1;
        w = 0;
        ap = 0.f;
    }
    void Clear() { std::fill(b.begin(), b.end(), 0.f); ap = 0.f; }
    void Push(float x) { b[w] = x; w = (w + 1) & mask; }
    float Tap(int d) const { return b[(w - 1 - d) & mask]; }  // d = 0: the newest
    float Lin(float d) const {
        const int i = int(d);
        const float f = d - i;
        return Tap(i) + f * (Tap(i + 1) - Tap(i));
    }
    // First-order allpass (Thiran) interpolation: flat in level, so a string
    // keeps its brightness at every pitch. d >= 1.
    float All(float d) {
        int i = int(d - .5f);
        float f = d - i; // .5 .. 1.5
        const float c = (1.f - f) / (1.f + f);
        const float x0 = Tap(i), x1 = Tap(i + 1);
        const float y = c * x0 + x1 - c * ap;
        ap = y;
        return y;
    }
};

// The contact of a mallet: a half-sine force pulse with area = velocity.
struct Pulse {
    int len = 0, pos = 0;
    float gain = 0.f;
    void Start(float contact_s, float vel, float sr) {
        len = std::max(2, int(contact_s * sr));
        pos = 0;
        gain = vel * kPi / (2.f * len);
    }
    bool Busy() const { return pos < len; }
    float Tick() {
        if (pos >= len) return 0.f;
        const float x = gain * std::sin(kPi * (pos + .5f) / len);
        ++pos;
        return x;
    }
};

// A short noise burst with an exponential tail (a click, a knock, a breath).
struct Burst {
    float env = 0.f, mul = 0.f;
    void Start(float level, float t_s, float sr) { env = level; mul = std::exp(-6.9078f / (std::max(t_s, .0005f) * sr)); }
    float Tick(Rng& r) { if (env < 1e-6f) return 0.f; const float x = env * r.B(); env *= mul; return x; }
};

// Follows the level of a signal: fast up, slow down.
struct Follow {
    float y = 0.f, up = .5f, down = .001f;
    void Set(float up_s, float down_s, float sr) { up = 1.f - std::exp(-1.f / (up_s * sr)); down = 1.f - std::exp(-1.f / (down_s * sr)); }
    float Tick(float x) { const float a = std::fabs(x); y += (a > y ? up : down) * (a - y); return y; }
};

// Phase delay (samples) of a one-pole lowpass y += k (x - y) at w.
inline float OnePoleDelay(float k, float w) {
    // H = k / (1 - (1-k) z^-1)
    const float a = 1.f - k;
    const float ph = std::atan2(a * std::sin(w), 1.f - a * std::cos(w));
    return ph / w;
}

} // namespace sd
