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

    /** per-shifter interleaved stereo delay lines, 16 bit, in D2 RAM
     *  (chompi_main.cpp): much faster than SDRAM, where they started */
    extern int16_t shift_mem[][kShiftBufFrames * 2];
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
     *
     *  Input comes in with Push(), output with Out(). They needn't keep pace:
     *  a sample voice pushes its sample's frames unchanged, one every `length`
     *  output samples, and the tap reads them at the pitch ratio. So the delay
     *  line holds the sample at its own speed, the shifter only does the pitch,
     *  and its grains are the same length of sample however far it's stretched.
     *  Process() is the classic in-step version (the looper).
     */
    class StereoPitchShifter
    {
    public:
        void Init(int16_t *buff, int16_t *ana)
        {
            buf_ = buff;
            ana_ = ana;
            acc_ = 0.f;
            w_   = kMask; // Push steps first: the first frame lands at 0
            wet_ = 0.f;
            Reset();
        }

        /** O(1): history written before this point reads back as silence */
        inline void Reset()
        {
            filled_ = 0;
            pending_ = 0;
            fresh_  = true;
            primed_ = false;
            state_  = State::IDLE;
            d0_     = 1.f;
        }

        /** call once per audio block, before processing any voice */
        static inline void NewBlock(size_t block_size) { Budget() = int(block_size) * kSearchPerSample; }

        /** one frame of input */
        inline void Push(float l, float r)
        {
            w_ = (w_ + 1) & kMask;
            Write(l, r);
            if(filled_ < kShiftBufFrames)
                filled_++;
            pending_++;
        }

        static inline float ClampRatio(float ratio) { return daisysp::fclamp(ratio, kMinRatio, kMaxRatio); }

        /** input and output in step
         *  @param ratio pitch ratio, 1.0 = unchanged, 2.0 = octave up */
        void Process(float ratio, float *l, float *r)
        {
            Push(*l, *r);
            const float rr = ClampRatio(ratio);
            Out(rr, rr - 1.f, fabsf(ratio - 1.f) < .003f, l, r);
        }

        /** one frame of output, replacing *l / *r (the dry signal)
         *  @param ratio how fast the tap reads, in input frames per output frame
         *  @param close how much faster the tap reads than input comes in (ratio
         *      minus the push rate): > 0 runs into "now", < 0 falls behind
         *  @param dry *l / *r already sound right: fade to them */
        void Out(float ratio, float close, bool dry, float *l, float *r)
        {
            const float target = dry ? 0.f : 1.f;
            if(fresh_)
                wet_ = target; // new note: no fade from the wrong pitch
            else
                daisysp::fonepole(wet_, target, .002f);
            fresh_ = false;

            if(wet_ <= .0001f)
                pending_ = 0; // the tap keeps its delay while dry
            else
            {
                Advance(ratio, close);

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
        }

        static constexpr float  kMinRatio  = .0625f;
        // 3 octaves, at any TEHP length. The highest splice target, kDLo + FadeUse
        // + Margin + JumpUp + kSearch, stays inside kShiftBufFrames up to a
        // closing speed of about 9
        static constexpr float  kMaxRatio  = 8.f;

    private:
        static constexpr size_t kMask      = kShiftBufFrames - 1;
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

        void Write(float l, float r)
        {
            buf_[2 * w_]     = ToS16(l);
            buf_[2 * w_ + 1] = ToS16(r);

            // every kAnaDecim frames, store their average (mono) for the search
            acc_ += l + r;
            if((w_ & (kAnaDecim - 1)) == kAnaDecim - 1)
            {
                const float v = daisysp::fclamp(acc_ * (32767.f / (2 * kAnaDecim)), -32767.f, 32767.f);
                ana_[w_ / kAnaDecim] = int16_t(v);
                acc_ = 0.f;
            }
        }

        // closing in on "now" (close > 0): above 1 the crossfade is shortened
        // so it always uses the same delay, and the jump grows so every splice
        // gains more room than the search and fade use up
        static inline float FadeUse(float close) { return close > 0.f ? kFade * fminf(close, 1.f) : 0.f; }
        static inline float Margin(float close) { return kSearchSamps * fabsf(close); }
        static inline int   JumpUp(float close)
        {
            return std::max(kGrain, int(FadeUse(close) + .5f * Margin(close)) + kSearch + 64);
        }

        /** history a note needs before closing in on "now" can start */
        static inline size_t PrimeFrames(float close)
        {
            return size_t(kDLo + FadeUse(close) + Margin(close) + JumpUp(close));
        }

        void Advance(float rr, float close)
        {
            const float inc      = float(pending_) - rr; // delay change since the last output
            pending_ = 0;
            const float margin   = Margin(close);
            const float fade_use = FadeUse(close);
            const int   jump_up  = JumpUp(close);

            if(!primed_)
            {
                // reading faster than input comes in: collect some history
                // first, then start at the beginning of the note
                if(close > 0.f && filled_ < PrimeFrames(close))
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
                    const bool down = close < 0.f && d0_ >= kDHi - margin;
                    const bool up   = close > 0.f && d0_ <= kDLo + fade_use + margin;
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
                    const bool late = (close < 0.f && d0_ >= kDHi)
                                      || (close > 0.f && d0_ <= kDLo + fade_use);
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
                        fade_len_ = close > 1.f ? std::max(1, int(kFade / close)) : kFade;
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
            const float la = buf_[2 * a], ra = buf_[2 * a + 1];
            *l = (la + (buf_[2 * b] - la) * frac) * (1.f / 32767.f);
            *r = (ra + (buf_[2 * b + 1] - ra) * frac) * (1.f / 32767.f);
        }

        static inline int16_t ToS16(float x)
        {
            return int16_t(daisysp::fclamp(x, -1.f, 1.f) * 32767.f);
        }

        /** search candidates left in this audio block, shared by all voices */
        static int &Budget()
        {
            static int budget = 0;
            return budget;
        }

        int16_t *buf_;
        int16_t *ana_;
        float    acc_;
        size_t   w_;
        size_t filled_;
        int    pending_; // frames pushed since the last Advance
        bool   fresh_;   // no output since Reset
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
