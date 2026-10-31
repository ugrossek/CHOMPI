#pragma once
// A player's pan: equal-power between the speakers, the same share into the
// room. The width moves everyone together: 0 all in the middle, 1 as panned,
// 2 pushed further out. Plain C++: one Pan per player, run once per block on
// that player's sum. (The circle with its ear cues, dsp/Circle.h, stays for
// the sound study.)

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace meter {

class Pan {
public:
    // pan: -1 left .. 1 right. width: 0..2.
    void Set(float pan, float width) {
        float x = std::min(std::max(pan, -1.f), 1.f);
        if (width <= 1.f) x *= width;
        else x = std::copysign(std::pow(std::fabs(x), 1.f / width), x);
        const float a = (.5f + .5f * x) * float(M_PI) / 2.f;
        tl_ = std::cos(a); // in the middle .71 each, as the circle had it in front
        tr_ = std::sin(a);
    }
    // Take the pan at once (a new start), not over the next block.
    void Snap() { gl_ = tl_; gr_ = tr_; }

    // in: the player's sum (mono). Adds to the dry pair and to the room's pair.
    void Process(const float* in, float* l, float* r, float* sl, float* sr, size_t n) {
        // Glide from the last block's pan to this one's, so turning is smooth.
        const float k = 1.f / float(n);
        const float dl = (tl_ - gl_) * k, dr = (tr_ - gr_) * k;
        for (size_t i = 0; i < n; ++i) {
            const float xl = in[i] * gl_, xr = in[i] * gr_;
            l[i] += xl;
            r[i] += xr;
            sl[i] += xl;
            sr[i] += xr;
            gl_ += dl;
            gr_ += dr;
        }
        gl_ = tl_;
        gr_ = tr_;
    }

private:
    float tl_ = .7071f, tr_ = .7071f, gl_ = .7071f, gr_ = .7071f;
};

} // namespace meter
