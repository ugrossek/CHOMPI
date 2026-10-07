#pragma once
#include <cstddef>
#include <cmath>
#include <algorithm>
#include "daisysp.h"

namespace chompi
{
    static constexpr size_t kShiftBufFrames = 4096; // power of two
    static constexpr size_t kAnaDecim       = 4;
    static constexpr size_t kShiftAnaLen    = kShiftBufFrames / kAnaDecim;

    /** per-voice interleaved stereo delay lines, lives in SDRAM (chompi_main.cpp) */
    extern float shift_mem[][kShiftBufFrames * 2];
    /** per-voice decimated mono copy for the splice search, lives in DTCM so the
     *  search never touches SDRAM (and the cache) */
    extern int16_t shift_ana[][kShiftAnaLen];

    /** Stereo time-domain pitch shifter (WSOLA-style).
     *  A single read tap moves through a delay line at the pitch ratio. When it
     *  runs out of room it is spliced back by about one grain, at the offset where
     *  the waveform best lines up with what is playing (cross-correlation), and
     *  crossfaded. The search runs on a decimated mono copy in fast RAM, is
     *  spread over many samples, and all voices share a per-block budget, so
     *  playing many notes can't overload the audio callback.
     *  Both channels share the tap so the stereo image stays intact.
     */
    class StereoPitchShifter
    {
    public:
        void Init(float *buff, int16_t *ana)
        {
            buf_ = buff;
            ana_ = ana;
            acc_ = 0.f;
            w_   = 0;
            wet_ = 0.f;
            Reset();
        }

        /** O(1): history written before this point reads back as silence */
        inline void Reset()
        {
            filled_ = 0;
            primed_ = false;
            state_  = State::IDLE;
            d0_     = 1.f;
        }

        /** call once per audio block, before processing any voice */
        static inline void NewBlock(size_t block_size) { Budget() = int(block_size) * kSearchPerSample; }

        /** @param ratio pitch ratio, 1.0 = unchanged, 2.0 = octave up */
        void Process(float ratio, float *l, float *r)
        {
            buf_[2 * w_]     = *l;
            buf_[2 * w_ + 1] = *r;

            // every kAnaDecim frames, store their average (mono) for the search
            acc_ += *l + *r;
            if((w_ & (kAnaDecim - 1)) == kAnaDecim - 1)
            {
                const float v = daisysp::fclamp(acc_ * (32767.f / (2 * kAnaDecim)), -32767.f, 32767.f);
                ana_[w_ / kAnaDecim] = int16_t(v);
                acc_ = 0.f;
            }

            // at exactly 1.0 there's nothing to do, so fade to dry
            const float target = fabsf(ratio - 1.f) < .003f ? 0.f : 1.f;
            if(filled_ == 0)
                wet_ = target; // new note: no fade from the wrong pitch
            else
                daisysp::fonepole(wet_, target, .002f);

            if(filled_ < kShiftBufFrames)
                filled_++;

            if(wet_ > .0001f)
            {
                const float rr = daisysp::fclamp(ratio, kMinRatio, kMaxRatio);
                Advance(rr);

                float l0, r0;
                Tap(d0_, &l0, &r0);
                if(state_ == State::FADE)
                {
                    float l1, r1;
                    Tap(d1_, &l1, &r1);
                    const float g = float(fade_) / fade_len_;
                    l0 += g * (l1 - l0);
                    r0 += g * (r1 - r0);
                }

                *l += wet_ * (l0 - *l);
                *r += wet_ * (r0 - *r);
            }

            w_ = (w_ + 1) & kMask;
        }

    private:
        static constexpr size_t kMask      = kShiftBufFrames - 1;
        static constexpr float  kMinRatio  = .0625f;
        static constexpr float  kMaxRatio  = 4.f;
        static constexpr int    kCorrLen   = 320; // input frames compared per candidate
        static constexpr int    kFade      = 320; // splice crossfade, ~7ms
        static constexpr int    kGrain     = 768; // nominal splice jump, ~16ms
        static constexpr int    kSearch    = 320; // +/- search around the jump
        static constexpr int    kLagStep   = int(kAnaDecim); // one analysis sample
        static constexpr int    kSearchPerSample = 2; // candidates per sample, all voices together
        static constexpr float  kSearchSamps = (2 * kSearch) / kLagStep + 1;
        static constexpr float  kDLo = kCorrLen + 8;         // shortest delay a tap may reach
        static constexpr float  kDHi = 2 * kGrain + kSearch + kCorrLen; // longest

        enum class State { IDLE, SEARCH, FADE };

        void Advance(float rr)
        {
            const float inc    = 1.f - rr; // delay change per output sample
            const float margin = kSearchSamps * fabsf(inc);

            // shifting up, the tap closes in on "now": above 2x the crossfade is
            // shortened so it always uses the same delay, and the jump grows so
            // every splice gains more room than the search and fade use up
            const float fade_use = rr > 1.f ? kFade * fminf(rr - 1.f, 1.f) : 0.f;
            const int   jump_up  = std::max(kGrain, int(fade_use + .5f * margin) + kSearch + 64);

            if(!primed_)
            {
                // shifting up reads faster than real time: collect some history
                // first, then start at the beginning of the note
                if(rr > 1.f && filled_ < size_t(kDLo + fade_use + margin + jump_up))
                {
                    d0_ = filled_; // reads silence until primed
                    return;
                }
                primed_ = true;
            }

            d0_ += inc;
            if(state_ == State::FADE)
                d1_ += inc;

            switch(state_)
            {
                case State::IDLE:
                {
                    const bool down = rr < 1.f && d0_ >= kDHi - margin;
                    const bool up   = rr > 1.f && d0_ <= kDLo + fade_use + margin;
                    if(down || up)
                    {
                        jump_     = up ? jump_up : -kGrain;
                        lag_      = -kSearch;
                        best_     = -1e30f;
                        best_off_ = float(jump_);
                        prev_     = 0.f;
                        state_    = State::SEARCH;
                    }
                    break;
                }
                case State::SEARCH:
                {
                    // out of time (budget starved by other voices): use the best so far
                    const bool late = (rr < 1.f && d0_ >= kDHi)
                                      || (rr > 1.f && d0_ <= kDLo + fade_use);
                    while(Budget() > 0 && lag_ <= kSearch && !late)
                    {
                        Budget()--;
                        Consider(jump_ + lag_);
                        lag_ += kLagStep;
                        if(lag_ % (kSearchPerSample * kLagStep) == 0)
                            break; // at most kSearchPerSample per voice per sample
                    }
                    if(lag_ > kSearch || late)
                    {
                        // no candidate fitted (e.g. right after a note start): the
                        // nominal jump, limited to the history we have
                        const float d_max = fminf(float(filled_) - 2.f, float(kShiftBufFrames) - 2.f);
                        d1_       = daisysp::fclamp(d0_ + best_off_, 1.f, d_max);
                        fade_     = 0;
                        fade_len_ = rr > 2.f ? std::max(1, int(kFade / (rr - 1.f))) : kFade;
                        state_ = State::FADE;
                    }
                    break;
                }
                case State::FADE:
                {
                    if(++fade_ >= fade_len_)
                    {
                        d0_    = d1_;
                        state_ = State::IDLE;
                    }
                    break;
                }
            }

            // ratio changes can push the tap out of range; keep it readable
            d0_ = daisysp::fclamp(d0_, 1.f, float(kShiftBufFrames) - 2.f);
        }

        /** scores a splice to (current delay + off) against what's playing now,
         *  on the decimated mono copy; refines the best with a parabolic fit */
        void Consider(int off)
        {
            const float dc = d0_ + off;
            float score = -1e30f;
            if(dc >= kDLo && dc <= float(filled_) - 2.f && dc <= float(kShiftBufFrames) - 2.f)
            {
                // both segments run forward in time from their read positions
                size_t a = ((w_ - size_t(d0_)) & kMask) / kAnaDecim;
                size_t b = ((w_ - size_t(dc)) & kMask) / kAnaDecim;
                int32_t xy = 0, yy = 0; // +/-2047^2 * 80 fits
                for(int k = 0; k < kCorrLen / int(kAnaDecim); k++)
                {
                    const int32_t x = ana_[a] >> 4, y = ana_[b] >> 4; // headroom
                    xy += x * y;
                    yy += y * y;
                    a = (a + 1) & (kShiftAnaLen - 1);
                    b = (b + 1) & (kShiftAnaLen - 1);
                }
                score = float(xy) / sqrtf(float(yy) + 1.f);
            }

            if(score > best_)
            {
                best_     = score;
                best_off_ = float(off);
                best_l_   = prev_;
                fit_      = true; // fit once the right neighbour is known
            }
            else
            {
                if(fit_ && best_l_ > -1e29f && score > -1e29f)
                {
                    // parabola through the best and its neighbours: sub-step offset
                    const float den = best_l_ - 2.f * best_ + score;
                    if(den < 0.f)
                        best_off_ += daisysp::fclamp(.5f * (best_l_ - score) / den, -.5f, .5f) * kLagStep;
                }
                fit_ = false;
            }
            prev_ = score;
        }

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

            const size_t a = (w_ - back) & kMask;
            const size_t b = (a - 1) & kMask;
            *l = buf_[2 * a] + (buf_[2 * b] - buf_[2 * a]) * frac;
            *r = buf_[2 * a + 1] + (buf_[2 * b + 1] - buf_[2 * a + 1]) * frac;
        }

        /** search candidates left in this audio block, shared by all voices */
        static int &Budget()
        {
            static int budget = 0;
            return budget;
        }

        float   *buf_;
        int16_t *ana_;
        float    acc_;
        size_t   w_;
        size_t filled_;
        bool   primed_;
        State  state_;
        float  d0_, d1_; // delay of the playing tap, and of the one fading in
        int    fade_, fade_len_;
        int    jump_, lag_;
        float  best_off_, best_, best_l_, prev_;
        bool   fit_;
        float  wet_;
    };
} // namespace chompi
