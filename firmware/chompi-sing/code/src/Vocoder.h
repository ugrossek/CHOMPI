#pragma once
#include <cmath>
#include <cstddef>

namespace chompi
{
    /** Carrier for the vocoder: a band-limited sawtooth (PolyBLEP), bright
     *  and buzzy like the synths in a classic vocoder. */
    struct SawOsc
    {
        float phase = 0.f, inc = 0.f;

        void SetFreq(float hz, float samplerate) { inc = hz / samplerate; }

        float Process()
        {
            float y = 2.f * phase - 1.f;
            /* smooth the jump, so it doesn't alias */
            if (phase < inc)
            {
                const float t = phase / inc;
                y -= t + t - t * t - 1.f;
            }
            else if (phase > 1.f - inc)
            {
                const float t = (phase - 1.f) / inc;
                y -= t * t + t + t + 1.f;
            }
            phase += inc;
            if (phase >= 1.f)
                phase -= 1.f;
            return y;
        }
    };

    /** A channel vocoder, the Kraftwerk kind: the voice (modulator) is split
     *  into kBands bands, each band's level follows the voice, and those
     *  levels shape the same bands of a synth sound (carrier) that plays the
     *  held notes. The voice's words come out on the carrier's notes, so it
     *  doesn't matter what pitch is sung, or whether it is sung at all:
     *  talking works too. No pitch detection, nothing to glitch.
     *
     *  Stereo: one analysis of the voice, two carriers (left, right) each
     *  through its own synthesis bank, so voices can be panned.
     *
     *  No libDaisy here, so the same code runs in the host test
     *  (test/vocoder_test.cpp). Init() sets every member.
     */
    class Vocoder
    {
      public:
        static constexpr int   kBands = 16;
        static constexpr float kLow   = 120.f;  // centre of the lowest band, Hz
        static constexpr float kHigh  = 7500.f; // centre of the highest band, Hz

        void Init(float samplerate)
        {
            sr_ = samplerate;
            const float ratio = powf(kHigh / kLow, 1.f / (kBands - 1));
            /* Q so that neighbouring bands meet about where each is 3 dB
               down; two biquads in a row make each band steeper */
            const float q = sqrtf(ratio) / (ratio - 1.f) * 1.2f;
            for (int b = 0; b < kBands; b++)
            {
                const float fc = kLow * powf(ratio, float(b));
                for (int s = 0; s < 2; s++)
                {
                    SetBandpass(ana_[b][s], fc, q);
                    SetBandpass(syn_[0][b][s], fc, q);
                    SetBandpass(syn_[1][b][s], fc, q);
                }
                env_[b] = 0.f;
            }
            shift_ = 0.f;
            freeze_ = false;
            att_ = 1.f - expf(-1.f / (.002f * sr_));
            rel_ = 1.f - expf(-1.f / (.020f * sr_));
        }

        /** "size": moves the voice's formants by bands against the carrier.
         *  Positive: each synth band takes its level from a higher voice band,
         *  so the mouth shape moves down: a bigger robot. Negative: smaller.
         *  One band is about a third of an octave. */
        void SetShift(float bands) { shift_ = bands; }

        /** Freeze: hold the voice's band levels as they are now, so the
         *  synth keeps saying the current sound ("aaa") while the voice is
         *  free to stop. Off: follow the voice again. */
        void SetFreeze(bool on) { freeze_ = on; }
        bool Frozen() const { return freeze_; }

        /** mod: the voice; car_l/car_r: the synth carriers; adds the result
         *  into out_l/out_r */
        void Process(const float *mod, const float *car_l, const float *car_r,
                     float *out_l, float *out_r, size_t size)
        {
            if (size > kMaxBlock)
                size = kMaxBlock;

            /* the voice's level in every band (frozen: as it was) */
            float band[kMaxBlock];
            for (int b = 0; b < kBands; b++)
            {
                if (freeze_)
                {
                    for (size_t i = 0; i < size; i++)
                        lev_[b][i] = env_[b];
                    continue;
                }
                for (size_t i = 0; i < size; i++)
                    band[i] = Run(ana_[b][1], Run(ana_[b][0], mod[i]));
                float env = env_[b];
                for (size_t i = 0; i < size; i++)
                {
                    const float x = fabsf(band[i]);
                    env += (x - env) * (x > env ? att_ : rel_);
                    lev_[b][i] = env;
                }
                env_[b] = env;
            }

            /* each synth band at the level of voice band b + shift; shifted,
               part of the voice falls off the ends, so make up a little */
            float cl[kMaxBlock], cr[kMaxBlock];
            const float gain = kGain * (1.f + .12f * fabsf(shift_));
            for (int b = 0; b < kBands; b++)
            {
                const float src = b + shift_;
                const int   s0  = int(floorf(src));
                const float fr  = src - s0;
                const float w0  = s0 >= 0 && s0 < kBands ? 1.f - fr : 0.f;
                const float w1  = s0 + 1 >= 0 && s0 + 1 < kBands ? fr : 0.f;
                if (w0 == 0.f && w1 == 0.f)
                    continue; // nothing of the voice lands here
                const float *l0 = lev_[s0 >= 0 && s0 < kBands ? s0 : 0];
                const float *l1 = lev_[s0 + 1 >= 0 && s0 + 1 < kBands ? s0 + 1 : 0];

                for (size_t i = 0; i < size; i++)
                {
                    cl[i] = Run(syn_[0][b][1], Run(syn_[0][b][0], car_l[i]));
                    cr[i] = Run(syn_[1][b][1], Run(syn_[1][b][0], car_r[i]));
                }
                for (size_t i = 0; i < size; i++)
                {
                    const float e = (l0[i] * w0 + l1[i] * w1) * gain;
                    out_l[i] += cl[i] * e;
                    out_r[i] += cr[i] * e;
                }
            }
        }

      private:
        static constexpr size_t kMaxBlock = 64;
        /* a held note through the vocoder comes out about as loud as the
           voice going in (test/vocoder_test.cpp) */
        static constexpr float kGain = 10.f;

        struct Bq
        {
            float b0, b2, a1, a2, z1, z2; // b1 = 0 for a bandpass
        };

        /** bandpass with 0 dB at the centre */
        void SetBandpass(Bq &q, float fc, float Q)
        {
            const float w = 2.f * 3.14159265f * fc / sr_;
            const float alpha = sinf(w) / (2.f * Q);
            const float a0 = 1.f + alpha;
            q.b0 = alpha / a0;
            q.b2 = -alpha / a0;
            q.a1 = -2.f * cosf(w) / a0;
            q.a2 = (1.f - alpha) / a0;
            q.z1 = q.z2 = 0.f;
        }

        static inline float Run(Bq &q, float x)
        {
            const float y = q.b0 * x + q.z1;
            q.z1 = -q.a1 * y + q.z2;
            q.z2 = q.b2 * x - q.a2 * y;
            return y;
        }

        float sr_;
        Bq    ana_[kBands][2];
        Bq    syn_[2][kBands][2];
        float env_[kBands];
        float lev_[kBands][kMaxBlock];
        float att_, rel_;
        float shift_;
        bool  freeze_;
    };

} // namespace chompi
