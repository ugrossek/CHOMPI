#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

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
            {
                pitch_[i] = 0.f;
                clean_[i] = false;
            }
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

        /** live: record this block, and the sung note (0: not voiced).
         *  clean: false while the mic is ducked for a key click; a freeze
         *  never holds such a block (with latch, chords change while
         *  singing, and a repeated dip flutters). */
        void Record(const float *in, size_t size, float note, bool clean = true)
        {
            if (frozen_)
                return;
            for (size_t i = 0; i < size; i++)
            {
                audio_[w_] = in[i];
                w_ = (w_ + 1) & (kLen - 1);
            }
            clean_[fw_] = clean;
            pitch_[fw_] = note;
            fw_ = (fw_ + 1) & (kFrames - 1);
            block_ = int(size);
        }

        void SetFreeze(bool on)
        {
            if (on && !frozen_)
            {
                /* freeze the latest steady moment, not one in the middle of
                   a glide or a consonant: repeated, those flutter */
                pos_ = target_ = float(SteadyFramesBack() * block_);
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

        /** for the freeze dump: the newest sample and pitch frame, and where
         *  the read point is (samples back) */
        int   WriteHead() const { return w_; }
        int   PitchHead() const { return fw_; }
        float ReadBack() const { return pos_; }
        const float *Audio() const { return audio_; }
        const float *Pitches() const { return pitch_; }

        /** the sung note at the read point (0: not voiced there) */
        float Note() const { return note_; }

        /** frozen: the voice from the recording, size samples into out */
        void Play(float *out, size_t size)
        {
            pos_ += (target_ - pos_) * kGlide;

            /* the pitch at the read point gives the period */
            note_ = PitchAt(int(pos_ / float(block_ > 0 ? block_ : 48)));
            const float period = note_ > 0.f ? sr_ / (440.f * exp2f((note_ - 69.f) / 12.f))
                                             : sr_ * .01f;

            for (size_t i = 0; i < size; i++)
            {
                if (--next_ <= 0)
                    StartGrain(period);
                float y = 0.f, w[2] = {0.f, 0.f};
                for (int g = 0; g < 2; g++)
                {
                    Grain &gr = grain_[g];
                    if (gr.t >= gr.len)
                        continue;
                    /* Hann: two of them, half a grain apart, sum to 1 (for
                       grains that line up); noise grains, which share
                       nothing, get the sine window, whose squares sum to 1,
                       so the noise keeps its level through the fade */
                    const float ph = float(gr.t) / float(gr.len);
                    w[g] = note_ > 0.f ? .5f - .5f * cosf(6.2831853f * ph)
                                       : sinf(3.1415927f * ph);
                    y += audio_[(gr.start + gr.t) & (kLen - 1)] * w[g];
                    gr.t++;
                }
                /* The breath and air in a voice differ from grain to grain,
                   so the fades cost them a little level and a frozen voice
                   is slightly duller. A steady lift of the highs (~+1 dB
                   above ~2 kHz) makes up for it. (A lift that followed the
                   fades pumped the highs at the grain rate: an audible high
                   component on the unit.) */
                (void)w;
                hp_lp_ += kHpCoef * (y - hp_lp_);
                out[i] = note_ > 0.f ? y + (y - hp_lp_) * kAirLift : y;
            }
        }

      private:
        /* the detector describes audio from about this many blocks before
           it reports (half its 29 ms frame) */
        static constexpr int kDetLag      = 15;
        /* Searched back over the whole recording, so a freeze pressed a
           little after the note (on breath or a fading tail) still holds the
           note. A held note may have vibrato, a glide may not get in: over
           kLongWin (about one vibrato cycle) vibrato swings around its note,
           so both halves average the same, while a glide drifts. Notes too
           short for that get the strict short test. */
        static constexpr int   kSteadyLook = 2000; // blocks searched back
        static constexpr int   kLongWin    = 180;  // blocks
        static constexpr float kLongRange  = 1.2f; // semitones, vibrato fits
        static constexpr float kLongDrift  = .25f; // semitones between the halves
        static constexpr int   kShortWin   = 45;   // blocks a frozen grain pair covers
        static constexpr float kShortRange = .3f;  // semitones

        /** the sung note for the audio frames_back blocks before the newest */
        float PitchAt(int frames_back) const
        {
            int f = frames_back - kDetLag;
            if (f < 0)
                f = 0;
            return pitch_[(fw_ - 1 - f) & (kFrames - 1)];
        }

        /** how far back the latest steady, pitched stretch is (blocks);
         *  0 if there is none in the last kSteadyLook */
        int SteadyFramesBack() const
        {
            for (int k = 0; k <= kSteadyLook; k += 3)
                if (Steady(k, kLongWin, kLongRange, kLongDrift))
                    return k + kLongWin / 2 - kShortWin / 2; // the middle of it
            for (int k = 0; k <= kSteadyLook; k += 3)
                if (Steady(k, kShortWin, kShortRange, 1e9f))
                    return k;
            return 0;
        }

        /** blocks k .. k + win back all pitched, within range, and the two
         *  halves' averages no further apart than drift */
        bool Steady(int k, int win, float range, float drift) const
        {
            float lo = 1e9f, hi = -1e9f, a = 0.f, b = 0.f;
            for (int j = k; j < k + win; j++)
            {
                const float n = PitchAt(j);
                if (n <= 0.f || !clean_[(fw_ - 1 - j) & (kFrames - 1)])
                    return false;
                lo = n < lo ? n : lo;
                hi = n > hi ? n : hi;
                (j < k + win / 2 ? a : b) += n;
            }
            return hi - lo < range && fabsf(a - b) / float(win / 2) < drift;
        }

        static constexpr int   kMaxGrain = 8192;  // samples
        /* no pitch at the read point (noise, breath): long grains from
           randomly spread places, so it doesn't loop audibly */
        static constexpr int   kNoiseHop    = 2880; // 60 ms
        static constexpr int   kNoiseSpread = 4800; // +- 100 ms
        static constexpr float kGlide    = .08f;  // per block, ~12 ms
        static constexpr int   kCorr     = 128;   // samples compared when lining up
        static constexpr int   kSearch   = 240;   // +- samples searched

        struct Grain
        {
            int start, len, t;
        };

        /** A new grain, ending near the read point. The next one starts hop
         *  samples later, hop being a whole number of the voice's periods
         *  (~20 ms), and each is 2 hops long, so the Hann windows sum to
         *  one. Where exactly it starts is found by comparison: the new
         *  grain's beginning has to line up with the part of the old grain
         *  it fades against. An estimated period alone is never exact, and
         *  even a little drift cancels high frequencies (the frozen sound
         *  got duller). A random whole number of periods further back each
         *  time keeps the frozen sound from turning mechanical. */
        void StartGrain(float period)
        {
            if (note_ <= 0.f)
            {
                StartNoiseGrain();
                return;
            }
            int m = int(.02f * sr_ / period + .5f);
            if (m < 1)
                m = 1;
            int hop = int(m * period + .5f);
            if (hop > kMaxGrain / 2)
                hop = kMaxGrain / 2;
            const int len = 2 * hop;

            /* nominal start: ending at the read point, 0..2 periods back */
            rand_ = rand_ * 1664525u + 1013904223u;
            const int vary = int((rand_ >> 16) % 3u) * int(period + .5f);
            int start = w_ - 1 - int(pos_) - vary - len;

            /* the old grain, from where the new one comes in */
            const Grain &old = grain_[g_ ^ 1];
            if (old.len > 0 && old.t < old.len)
            {
                const int o = old.start + old.t;
                int range = int(period * .5f);
                if (range > kSearch)
                    range = kSearch;
                int   best = 0;
                float best_score = -1e30f, best_r = 1.f;
                for (int pass = 0; pass < 2; pass++)
                {
                    /* coarse every 2 samples, then +-2 around the best */
                    const int from = pass ? best - 2 : -range;
                    const int to   = pass ? best + 2 : range;
                    const int step = pass ? 1 : 2;
                    for (int d = from; d <= to; d += step)
                    {
                        float xy = 0.f, yy = 1e-9f, xx = 1e-9f;
                        for (int i = 0; i < kCorr; i++)
                        {
                            const float x = audio_[(o + i) & (kLen - 1)];
                            const float y = audio_[(start + d + i) & (kLen - 1)];
                            xy += x * y;
                            yy += y * y;
                            xx += x * x;
                        }
                        const float score = xy / sqrtf(yy);
                        if (score > best_score)
                        {
                            best_score = score;
                            best = d;
                            best_r = xy / sqrtf(xx * yy);
                        }
                    }
                }
                start += best;
                r_ = best_r < 0.f ? 0.f : (best_r > 1.f ? 1.f : best_r);
            }
            else
                r_ = 1.f;

            Grain &gr = grain_[g_];
            gr.start = start & (kLen - 1);
            gr.len = len;
            gr.t = 0;
            g_ ^= 1;
            next_ = hop;
        }

        void StartNoiseGrain()
        {
            rand_ = rand_ * 1664525u + 1013904223u;
            const int spread = int((rand_ >> 8) % uint32_t(2 * kNoiseSpread)) - kNoiseSpread;
            const int len = 2 * kNoiseHop;
            Grain &gr = grain_[g_];
            gr.start = (w_ - 1 - int(pos_) - len - kNoiseSpread + spread) & (kLen - 1);
            gr.len = len;
            gr.t = 0;
            g_ ^= 1;
            next_ = kNoiseHop;
            r_ = 0.f;
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
        uint32_t rand_ = 12345u;
        float    hp_lp_ = 0.f;
        static constexpr float kHpCoef  = .23f;  // one pole, ~2 kHz at 48 kHz
        static constexpr float kAirLift = .15f;  // ~+1.2 dB above it
        bool     clean_[kFrames] = {}; // per block, as recorded (no detector lag)
        float    r_ = 1.f;     // how alike the two overlapping grains are, 0..1
    };

} // namespace chompi
