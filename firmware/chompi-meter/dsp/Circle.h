#pragma once
// The circle: every player stands on a circle around the conductor, and this is
// how one player's place is heard in stereo. Two speakers cannot put a sound
// behind you, so the place is told with the cues stereo does carry:
//  - the side: the level between the ears (a little short of hard left/right),
//    the far ear a fraction of a millisecond later, and duller (the head's shadow);
//  - in front or behind: behind is darker (what the outer ear does to sound from
//    behind) and has more of the room.
// The circle's size moves everyone together: 0 all in the middle, close and dry,
// 1 as placed, 2 wider and further away (more room, a little quieter).
// Plain C++: one Seat per player, run once per block on that player's sum.

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace meter {

class Seat {
public:
    static constexpr int kDelay = 64; // the delay line, samples (> the largest ear delay)

    void Init(float sample_rate) {
        sr_ = sample_rate;
        // One-pole low-passes: the head's shadow on the far ear, the outer ear's from behind.
        shadow_k_ = 1.f - std::exp(-2.f * float(M_PI) * 1800.f / sample_rate);
        back_k_ = 1.f - std::exp(-2.f * float(M_PI) * 3000.f / sample_rate);
        itd_max_ = kItd * sample_rate;
        Reset();
        Set(0.f, 1.f);
        Snap();
    }

    // place: 0..1, a turn round the circle from in front, clockwise (.25 right,
    // .5 behind, .75 left). size: the circle's size, 0..2.
    void Set(float place, float size) {
        const float a = 2.f * float(M_PI) * place;
        float x = std::sin(a);              // -1 left .. 1 right
        const float behind = std::max(0.f, -std::cos(a));
        const float ax = std::fabs(x);
        if (size <= 1.f) x *= size;         // a smaller circle gathers in the middle
        else x = std::copysign(std::pow(ax, 1.f / size), x); // a bigger one pushes out
        const float near = std::min(size, 1.f);
        const float pan = .5f + kPan * x;
        t_.gl = std::cos(pan * float(M_PI) / 2.f);
        t_.gr = std::sin(pan * float(M_PI) / 2.f);
        const float dist = .5f + .5f * size; // .5 close .. 1.5 far
        const float direct = 1.12f - .12f * dist * dist;
        t_.gl *= direct;
        t_.gr *= direct;
        t_.dl = x > 0.f ? itd_max_ * x : 0.f; // the far ear hears it later
        t_.dr = x < 0.f ? -itd_max_ * x : 0.f;
        t_.shl = x > 0.f ? kShadow * x : 0.f;
        t_.shr = x < 0.f ? -kShadow * x : 0.f;
        t_.dark = kDark * behind * near;
        t_.send = dist * (1.f + kBackRoom * behind * near);
    }
    // Take the place at once (a new start), not over the next block.
    void Snap() { c_ = t_; }

    // A silent player: the delay line and the filters start from rest.
    void Reset() {
        std::fill(buf_, buf_ + kDelay, 0.f);
        w_ = 0;
        back_lp_ = lp_l_ = lp_r_ = 0.f;
    }

    // in: the player's sum (mono). Adds to the dry pair and to the room's pair.
    void Process(const float* in, float* l, float* r, float* sl, float* sr, size_t n) {
        // Glide from the last block's place to this one's, so turning is smooth.
        const float k = 1.f / float(n);
        const Params d = {(t_.gl - c_.gl) * k, (t_.gr - c_.gr) * k, (t_.dl - c_.dl) * k, (t_.dr - c_.dr) * k,
                          (t_.shl - c_.shl) * k, (t_.shr - c_.shr) * k, (t_.dark - c_.dark) * k,
                          (t_.send - c_.send) * k};
        for (size_t i = 0; i < n; ++i) {
            back_lp_ += back_k_ * (in[i] - back_lp_);
            buf_[w_] = in[i] + c_.dark * (back_lp_ - in[i]);
            float xl = Read(c_.dl), xr = Read(c_.dr);
            w_ = (w_ + 1) & (kDelay - 1);
            lp_l_ += shadow_k_ * (xl - lp_l_);
            lp_r_ += shadow_k_ * (xr - lp_r_);
            xl = (xl + c_.shl * (lp_l_ - xl)) * c_.gl;
            xr = (xr + c_.shr * (lp_r_ - xr)) * c_.gr;
            l[i] += xl;
            r[i] += xr;
            sl[i] += xl * c_.send;
            sr[i] += xr * c_.send;
            c_.gl += d.gl; c_.gr += d.gr; c_.dl += d.dl; c_.dr += d.dr;
            c_.shl += d.shl; c_.shr += d.shr; c_.dark += d.dark; c_.send += d.send;
        }
        c_ = t_;
    }

private:
    static constexpr float kItd = .00045f;  // the far ear's delay at the side, s
    static constexpr float kPan = .42f;     // the side: a little short of one speaker
    static constexpr float kShadow = .55f;  // how much of the far ear is dulled at the side
    static constexpr float kDark = .6f;     // how much darker straight behind
    static constexpr float kBackRoom = .7f; // how much more room straight behind

    struct Params { float gl, gr, dl, dr, shl, shr, dark, send; };
    Params t_ = {}, c_ = {}; // where it is going, where it is

    // The line at a fractional delay (linear), from the sample just written.
    float Read(float d) const {
        const int di = int(d);
        const float f = d - float(di);
        const float a = buf_[(w_ - di) & (kDelay - 1)];
        const float b = buf_[(w_ - di - 1) & (kDelay - 1)];
        return a + f * (b - a);
    }

    float buf_[kDelay] = {};
    int w_ = 0;
    float back_lp_ = 0.f, lp_l_ = 0.f, lp_r_ = 0.f;
    float sr_ = 48000.f, shadow_k_ = .2f, back_k_ = .3f, itd_max_ = 21.6f;
};

} // namespace meter
