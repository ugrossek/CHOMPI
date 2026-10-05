#pragma once
#include <cmath>
#include <cstddef>

namespace chompi
{
    /** Pitch of the live voice, YIN (de Cheveigné & Kawahara 2002).
     *
     *  Two halves. Process(), in the audio callback, lowpasses the mono
     *  input, decimates it to 12 kHz into a ring and every kHop samples
     *  there (4 ms) marks a new frame: cheap. Update(), from the main loop,
     *  estimates the pitch over the latest kWin + kMaxLag samples (29 ms):
     *  the expensive part, kept out of the audio callback, where it took
     *  the time the harmony voices need. If the main loop falls behind it
     *  just takes the latest frame.
     *
     *  Range 80 Hz .. 1 kHz. The lowest pitch sets the frame length and so
     *  the latency: lowering it to 60 Hz would add about 8 ms.
     *
     *  No libDaisy here, so the same code runs in the host test
     *  (test/pitch_test.cpp). Init() sets every member.
     */
    class PitchDetector
    {
      public:
        static constexpr int   kDecim  = 4;
        static constexpr float kFs     = 12000.f; // after decimation
        static constexpr int   kMinLag = 12;      // 1000 Hz
        static constexpr int   kMaxLag = 150;     // 80 Hz
        static constexpr int   kWin    = 200;     // ~17 ms
        static constexpr int   kFrame  = kWin + kMaxLag;
        static constexpr int   kHop    = 48;      // 4 ms
        static constexpr int   kRing   = 1024;    // kFrame + ~50 ms of slack, power of 2

        /** YIN's threshold on the normalised difference: lower is stricter */
        static constexpr float kThreshold = .15f;
        /** a dip only counts if it comes this close to the deepest one: a
         *  loud 2nd harmonic (a sung "a" near E4, on the first formant) makes
         *  a dip at half the period that passes the threshold alone */
        static constexpr float kNearBest  = .05f;

        void Init()
        {
            /* 4th-order Butterworth lowpass at 3 kHz for 48 kHz: two biquads */
            SetLowpass(lp_[0], 3000.f / 48000.f, .5411961f);
            SetLowpass(lp_[1], 3000.f / 48000.f, 1.3065630f);
            hp_x1_ = hp_y1_ = 0.f;
            decim_ = 0;
            for (int i = 0; i < kRing; i++)
                ring_[i] = 0.f;
            w_ = 0;
            hop_count_ = 0;
            frame_end_ = 0;
            frames_    = 0;
            done_      = 0;
            silence_   = .003f;       // about -50 dBFS rms
            freq_ = 0.f;
            conf_ = 0.f;
            voiced_ = false;
            estimates_ = 0;
        }

        /** below this rms (of the decimated frame) the input counts as
         *  silence, and nothing is reported */
        void SetSilence(float rms) { silence_ = rms; }

        void Process(const float *in, size_t size)
        {
            for (size_t i = 0; i < size; i++)
            {
                /* DC and rumble out (one pole at ~40 Hz), then lowpass */
                const float hp = in[i] - hp_x1_ + .9948f * hp_y1_;
                hp_x1_ = in[i];
                hp_y1_ = hp;
                const float x = Biquad(lp_[1], Biquad(lp_[0], hp));

                if (++decim_ < kDecim)
                    continue;
                decim_ = 0;

                ring_[w_] = x;
                w_ = (w_ + 1) & (kRing - 1);
                if (++hop_count_ >= kHop)
                {
                    hop_count_ = 0;
                    frame_end_ = w_; // a new frame ends here
                    frames_    = frames_ + 1;
                }
            }
        }

        /** From the main loop: estimate the pitch of the latest frame, if
         *  there is one Update() hasn't seen. */
        void Update()
        {
            const unsigned f = frames_;
            if (f == done_)
                return;
            done_ = f;
            Snapshot(frame_end_);
            if (frames_ - f > (kRing - kFrame) / kHop - 1)
                return; // the callback overwrote the frame while we copied it
            Work();
            Estimate();
        }

        /** Hz of the latest voiced estimate (0 before the first) */
        float Frequency() const { return freq_; }
        /** 0..1, 1 - YIN's normalised difference at the chosen lag */
        float Confidence() const { return conf_; }
        /** a clear pitch right now (loud enough and periodic enough) */
        bool Voiced() const { return voiced_; }
        /** MIDI note number, fractional (69 = A4 440 Hz) */
        float Note() const { return freq_ > 0.f ? 69.f + 12.f * log2f(freq_ / 440.f) : 0.f; }
        /** counts estimates, for tests */
        unsigned Estimates() const { return estimates_; }

      private:
        struct Bq
        {
            float b0, b1, b2, a1, a2, z1, z2;
        };

        static void SetLowpass(Bq &q, float f, float Q)
        {
            const float w = 2.f * 3.14159265f * f;
            const float alpha = sinf(w) / (2.f * Q);
            const float c = cosf(w);
            const float a0 = 1.f + alpha;
            q.b0 = (1.f - c) * .5f / a0;
            q.b1 = (1.f - c) / a0;
            q.b2 = q.b0;
            q.a1 = -2.f * c / a0;
            q.a2 = (1.f - alpha) / a0;
            q.z1 = q.z2 = 0.f;
        }

        static float Biquad(Bq &q, float x)
        {
            const float y = q.b0 * x + q.z1;
            q.z1 = q.b1 * x - q.a1 * y + q.z2;
            q.z2 = q.b2 * x - q.a2 * y;
            return y;
        }

        /** copy the frame ending at end, oldest first */
        void Snapshot(int end)
        {
            int r = (end - kFrame) & (kRing - 1);
            float e = 0.f;
            for (int i = 0; i < kFrame; i++)
            {
                frame_[i] = ring_[r];
                r = (r + 1) & (kRing - 1);
            }
            for (int j = 0; j < kWin; j++)
                e += frame_[j] * frame_[j];
            rms_ = sqrtf(e / kWin);
        }

        /** the difference function over all lags */
        void Work()
        {
            for (int tau = 1; tau <= kMaxLag; tau++)
            {
                float d = 0.f;
                const float *a = frame_;
                const float *b = frame_ + tau;
                for (int j = 0; j < kWin; j++)
                {
                    const float t = a[j] - b[j];
                    d += t * t;
                }
                diff_[tau] = d;
            }
        }

        void Estimate()
        {
            estimates_++;

            /* cumulative mean normalised difference */
            float sum = 0.f;
            cmnd_[0] = 1.f;
            for (int tau = 1; tau <= kMaxLag; tau++)
            {
                sum += diff_[tau];
                cmnd_[tau] = sum > 0.f ? diff_[tau] * tau / sum : 1.f;
            }

            /* dips are compared by their depth between the lags (parabola):
               at 12 kHz a period falls between two lags, and the raw value
               there can look shallower than the dip at twice the period,
               which reads an octave low */
            float lowest = 1.f;
            for (int tau = kMinLag; tau <= kMaxLag; tau++)
                if (IsDip(tau) && Depth(tau) < lowest)
                    lowest = Depth(tau);

            /* first dip below the threshold and near the deepest; else the
               lowest point (reported, but not as voiced) */
            const float thresh = lowest + kNearBest < kThreshold ? lowest + kNearBest : kThreshold;
            int best = -1;
            for (int tau = kMinLag; tau <= kMaxLag; tau++)
                if (IsDip(tau) && Depth(tau) < thresh)
                {
                    best = tau;
                    break;
                }
            const bool dip = best >= 0;
            if (!dip)
            {
                best = kMinLag;
                for (int tau = kMinLag + 1; tau <= kMaxLag; tau++)
                    if (cmnd_[tau] < cmnd_[best])
                        best = tau;
            }

            conf_ = 1.f - cmnd_[best];
            if (conf_ < 0.f)
                conf_ = 0.f;

            if (!dip || rms_ < silence_)
            {
                voiced_ = false;
                return;
            }

            /* parabola through the neighbours for a fractional lag */
            float lag = float(best);
            if (best > 1 && best < kMaxLag)
            {
                const float a = cmnd_[best - 1], b = cmnd_[best], c = cmnd_[best + 1];
                const float den = a - 2.f * b + c;
                if (den > 0.f)
                    lag += .5f * (a - c) / den;
            }
            freq_   = kFs / lag;
            voiced_ = true;
        }

        /** a local minimum of the normalised difference */
        bool IsDip(int tau) const
        {
            return (tau == kMinLag || cmnd_[tau] <= cmnd_[tau - 1])
                   && (tau == kMaxLag || cmnd_[tau] < cmnd_[tau + 1]);
        }

        /** its bottom, from a parabola through the neighbours */
        float Depth(int tau) const
        {
            if (tau <= kMinLag || tau >= kMaxLag)
                return cmnd_[tau];
            const float a = cmnd_[tau - 1], b = cmnd_[tau], c = cmnd_[tau + 1];
            const float den = a - 2.f * b + c;
            return den > 0.f ? b - (a - c) * (a - c) / (8.f * den) : b;
        }

        Bq       lp_[2];
        float    hp_x1_, hp_y1_;
        int      decim_;
        float    ring_[kRing];
        int      w_, hop_count_;
        /* written by the callback, read by Update() in the main loop */
        volatile int      frame_end_;
        volatile unsigned frames_;
        unsigned          done_;
        float    frame_[kFrame];
        float    diff_[kMaxLag + 1];
        float    cmnd_[kMaxLag + 1];
        float    rms_, silence_;
        /* written by Update(), read by the callback (voice gate) and the UI */
        volatile float freq_, conf_;
        volatile bool  voiced_;
        unsigned estimates_;
    };

} // namespace chompi
