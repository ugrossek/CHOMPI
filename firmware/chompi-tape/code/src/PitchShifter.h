#pragma once
#include <cstddef>
#include <cmath>
#include "daisysp.h"

namespace chompi
{
    /** Grain length of the shifter. Longer is smoother on low ratios
     *  but smears transients more (latency is ~half of this). */
    static constexpr size_t kShiftWindow    = 2048; // frames, ~43ms at 48kHz
    static constexpr size_t kShiftBufFrames = 4096; // power of two, > kShiftWindow + 2

    /** per-voice interleaved stereo delay lines, lives in SDRAM (chompi_main.cpp) */
    extern float shift_mem[][kShiftBufFrames * 2];

    /** Stereo time-domain pitch shifter: two taps sweep a delay line half a window
     *  apart and are crossfaded, so pitch changes while playback speed doesn't.
     *  Both channels share one phase to keep the stereo image intact.
     */
    class StereoPitchShifter
    {
    public:
        void Init(float *buff)
        {
            buf_   = buff;
            w_     = 0;
            phase_ = 0.f;
            wet_   = 0.f;
            Reset();
        }

        /** O(1): history written before this point reads back as silence */
        inline void Reset() { filled_ = 0; }

        /** @param ratio pitch ratio, 1.0 = unchanged, 2.0 = octave up */
        void Process(float ratio, float *l, float *r)
        {
            buf_[2 * w_]     = *l;
            buf_[2 * w_ + 1] = *r;

            // at exactly 1.0 the two taps would just comb filter, so fade to dry
            const float target = fabsf(ratio - 1.f) < .003f ? 0.f : 1.f;
            if(filled_ == 0)
                wet_ = target; // new note: no fade from the wrong pitch
            else
                daisysp::fonepole(wet_, target, .002f);

            if(filled_ < kShiftBufFrames)
                filled_++;

            if(wet_ > .0001f)
            {
                phase_ += (1.f - ratio) * (1.f / kShiftWindow);
                phase_ -= floorf(phase_);
                float phase2 = phase_ + .5f;
                if(phase2 >= 1.f)
                    phase2 -= 1.f;

                // triangular gains: zero where a tap's delay wraps, always sum to 1
                const float g1 = 1.f - fabsf(2.f * phase_ - 1.f);
                const float g2 = 1.f - g1;

                float l1, r1, l2, r2;
                Tap(phase_ * kShiftWindow, &l1, &r1);
                Tap(phase2 * kShiftWindow, &l2, &r2);

                *l += wet_ * (g1 * l1 + g2 * l2 - *l);
                *r += wet_ * (g1 * r1 + g2 * r2 - *r);
            }

            w_ = (w_ + 1) & (kShiftBufFrames - 1);
        }

    private:
        /** linear-interpolated read, `delay` frames behind the write head */
        inline void Tap(float delay, float *l, float *r)
        {
            const size_t back = static_cast<size_t>(delay);
            const float  frac = delay - back;

            if(back + 1 >= filled_)
            {
                *l = *r = 0.f;
                return;
            }

            const size_t a = (w_ - back) & (kShiftBufFrames - 1);
            const size_t b = (a - 1) & (kShiftBufFrames - 1);
            *l = buf_[2 * a] + (buf_[2 * b] - buf_[2 * a]) * frac;
            *r = buf_[2 * a + 1] + (buf_[2 * b + 1] - buf_[2 * a + 1]) * frac;
        }

        float *buf_;
        size_t w_;
        size_t filled_;
        float  phase_;
        float  wet_;
    };
} // namespace chompi
