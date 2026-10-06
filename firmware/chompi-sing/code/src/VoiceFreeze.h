#pragma once
#include <cmath>
#include <cstddef>

namespace chompi
{
    /** Freeze and time wheel for the real voice (the Human character).
     *
     *  Live, it records the last kLen samples of the voice and, once per
     *  block, the detected pitch. Frozen, it stops recording and plays from
     *  the recording instead: two overlapping grains (Hann windows) read
     *  around the wheel's position. Each grain is a whole number of the
     *  voice's periods long (about 40 ms) and starts a whole number of
     *  periods after the last, so a frozen position repeats seamlessly as a
     *  held tone, and moving the position scrubs through what was said.
     *
     *  No libDaisy, so the host test (test/voicefreeze_test.cpp) runs it.
     *  The buffers live outside (SDRAM on CHOMPI); Init() clears them.
     */
    class VoiceFreeze
    {
      public:
        static constexpr int kLen    = 131072; // samples, ~2.7 s at 48 kHz, power of two
        static constexpr int kFrames = 4096;   // pitch frames, one per block, power of two

        void Init(float samplerate, float *audio, float *pitch)
        {
            sr_ = samplerate;
            audio_ = audio;
            pitch_ = pitch;
            for (int i = 0; i < kLen; i++)
                audio_[i] = 0.f;
            for (int i = 0; i < kFrames; i++)
                pitch_[i] = 0.f;
            w_ = 0;
            fw_ = 0;
            frozen_ = false;
            pos_ = target_ = 0.f;
            for (int g = 0; g < 2; g++)
            {
                grain_[g].start = 0;
                grain_[g].len = 0;
                grain_[g].t = 0;
            }
            next_ = 0;
            note_ = 0.f;
            block_ = 48;
        }

        /** live: record this block, and the sung note (0: not voiced) */
        void Record(const float *in, size_t size, float note)
        {
            if (frozen_)
                return;
            for (size_t i = 0; i < size; i++)
            {
                audio_[w_] = in[i];
                w_ = (w_ + 1) & (kLen - 1);
            }
            pitch_[fw_] = note;
            fw_ = (fw_ + 1) & (kFrames - 1);
            block_ = int(size);
        }

        void SetFreeze(bool on)
        {
            if (on && !frozen_)
            {
                pos_ = target_ = 0.f;
                next_ = 0;
                grain_[0].len = grain_[1].len = 0;
            }
            frozen_ = on;
        }
        bool Frozen() const { return frozen_; }

        /** move the read point by ms, positive = further back; freezes */
        void Scrub(float ms)
        {
            if (!frozen_)
                SetFreeze(true);
            target_ += ms * sr_ * .001f;
            const float most = float(kLen) - 4.f * kMaxGrain;
            target_ = target_ < 0.f ? 0.f : (target_ > most ? most : target_);
        }
        /** 0 = now .. 1 = the oldest kept moment */
        float Position() const { return target_ / (float(kLen) - 4.f * kMaxGrain); }

        /** the sung note at the read point (0: not voiced there) */
        float Note() const { return note_; }

        /** frozen: the voice from the recording, size samples into out */
        void Play(float *out, size_t size)
        {
            pos_ += (target_ - pos_) * kGlide;

            /* the pitch at the read point gives the period */
            const int frames_back = int(pos_ / float(block_ > 0 ? block_ : 48));
            note_ = pitch_[(fw_ - 1 - frames_back) & (kFrames - 1)];
            const float period = note_ > 0.f ? sr_ / (440.f * exp2f((note_ - 69.f) / 12.f))
                                             : sr_ * .01f;

            for (size_t i = 0; i < size; i++)
            {
                if (--next_ <= 0)
                    StartGrain(period);
                float y = 0.f;
                for (int g = 0; g < 2; g++)
                {
                    Grain &gr = grain_[g];
                    if (gr.t >= gr.len)
                        continue;
                    /* Hann: two of them, half a grain apart, sum to 1 */
                    const float ph = float(gr.t) / float(gr.len);
                    const float wdw = .5f - .5f * cosf(6.2831853f * ph);
                    y += audio_[(gr.start + gr.t) & (kLen - 1)] * wdw;
                    gr.t++;
                }
                out[i] = y;
            }
        }

      private:
        static constexpr int   kMaxGrain = 4096;  // samples
        static constexpr float kGlide    = .08f;  // per block, ~12 ms

        struct Grain
        {
            int start, len, t;
        };

        /** a new grain, ending at the read point. The next one starts hop
         *  samples later, hop being a whole number of the voice's periods
         *  (~20 ms), and each is 2 hops long, so the Hann windows sum to one
         *  and a frozen position repeats in step with the voice's cycle */
        void StartGrain(float period)
        {
            int m = int(.02f * sr_ / period + .5f);
            if (m < 1)
                m = 1;
            int hop = int(m * period + .5f);
            if (hop > kMaxGrain / 2)
                hop = kMaxGrain / 2;
            const int len = 2 * hop;
            const int end = w_ - 1 - int(pos_);
            Grain &gr = grain_[g_];
            gr.start = (end - len) & (kLen - 1);
            gr.len = len;
            gr.t = 0;
            g_ ^= 1;
            next_ = hop;
        }

        float  sr_;
        float *audio_;
        float *pitch_;
        int    w_, fw_, block_;
        bool   frozen_;
        float  pos_, target_;
        Grain  grain_[2];
        int    g_ = 0;
        int    next_;
        float  note_;
    };

} // namespace chompi
