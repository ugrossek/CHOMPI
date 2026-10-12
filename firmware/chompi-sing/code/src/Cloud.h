/** Cloud: a granular glitter cloud (knob 4, page 3)
 *
 *  The output of the harmonies is recorded into a ring buffer, and short
 *  grains of it are played back from random moments of the last half second,
 *  panned at random. Glitter sets what the grains sound like: plain copies of
 *  the voice to the left, shorter grains an octave up (and the odd octave and
 *  a fifth, two octaves) to the right: sparkle.
 *
 *  Each grain is a Hann window; its cosine runs as a rotation (two
 *  multiply-adds per sample), so there is no table. Grains start at block
 *  boundaries (1 ms), which a cloud doesn't mind.
 */
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace chompi
{
    static constexpr size_t kCloudLen = 65536; // 1.37 s mono at 48 kHz, a power of 2
    extern float cloud_mem[kCloudLen];         // in SDRAM, chompi_main.cpp

    class Cloud
    {
      public:
        static constexpr int kGrains = 10;

        void Init(float sr, float *mem)
        {
            sr_  = sr;
            buf_ = mem;
            for (size_t i = 0; i < kCloudLen; i++)
                buf_[i] = 0.f;
            write_ = 0;
            amount_ = amount_target_ = 0.f;
            glitter_ = .5f;
            wait_ = 0;
            seed_ = 0x9e3779b9u;
            for (Grain &g : grains_)
                g.left = 0;
        }

        /** 0 off .. 1 dense */
        void SetAmount(float a) { amount_target_ = a < 0.f ? 0.f : a > 1.f ? 1.f : a; }
        /** 0 plain .. 1 sparkle */
        void SetGlitter(float g) { glitter_ = g < 0.f ? 0.f : g > 1.f ? 1.f : g; }

        bool Active() const
        {
            if (amount_target_ > 0.f || amount_ > 1e-4f)
                return true;
            for (const Grain &g : grains_)
                if (g.left > 0)
                    return true;
            return false;
        }

        void Process(float *l, float *r, size_t n)
        {
            /* always record, so that turning it up starts with the recent past */
            for (size_t i = 0; i < n; i++)
            {
                buf_[write_] = (l[i] + r[i]) * .5f;
                write_       = (write_ + 1) & kMask;
            }

            amount_ += (amount_target_ - amount_) * .02f; // per block: ~50 ms
            if (amount_target_ == 0.f && amount_ < 1e-4f)
                amount_ = 0.f;
            if (!Active())
                return;

            /* new grains while there's an amount; the last ones play out */
            const float rate = 5.f + 35.f * amount_ * sqrtf(amount_); // grains per second
            if (amount_target_ > 0.f)
            {
                wait_ -= int(n);
                while (wait_ <= 0)
                {
                    Trigger();
                    wait_ += int(sr_ / rate * (.5f + Rand()));
                }
            }

            float wl[n], wr[n];
            for (size_t i = 0; i < n; i++)
                wl[i] = wr[i] = 0.f;

            for (Grain &g : grains_)
            {
                if (g.left <= 0)
                    continue;
                const int m = g.left < int(n) ? g.left : int(n);
                float pos = g.pos, c = g.c, s = g.s;
                for (int i = 0; i < m; i++)
                {
                    const uint32_t k = uint32_t(pos);
                    const float    f = pos - float(k);
                    const float    a = buf_[k & kMask];
                    const float    b = buf_[(k + 1) & kMask];
                    const float    x = (a + (b - a) * f) * (.5f - .5f * c); // Hann
                    wl[i] += x * g.gl;
                    wr[i] += x * g.gr;
                    pos += g.speed;
                    const float c2 = c * g.cd - s * g.sd;
                    s = s * g.cd + c * g.sd;
                    c = c2;
                }
                if (pos >= float(kCloudLen))
                    pos -= float(kCloudLen);
                g.pos = pos;
                g.c = c;
                g.s = s;
                g.left -= m;
            }

            /* about rate * length grains overlap, at random phases, so they
               add up as the square root; with the window and the pan that
               keeps the cloud near the voice's level whatever the density
               (test/cloud_test.cpp) */
            const float overlap = rate * AvgLength() / sr_;
            const float wet     = amount_ * 2.1f / sqrtf(overlap > 1.f ? overlap : 1.f);
            const float dry     = 1.f - .6f * amount_;
            for (size_t i = 0; i < n; i++)
            {
                l[i] = l[i] * dry + wl[i] * wet;
                r[i] = r[i] * dry + wr[i] * wet;
            }
        }

      private:
        static constexpr uint32_t kMask = kCloudLen - 1;
        static constexpr float    kPi   = 3.14159265f;

        struct Grain
        {
            float pos, speed;   // read position in the buffer, step per sample
            float c, s, cd, sd; // the window's cosine as a rotation
            float gl, gr;       // pan and level
            int   left;         // samples still to play
        };

        /** 0..1 */
        float Rand()
        {
            seed_ ^= seed_ << 13;
            seed_ ^= seed_ >> 17;
            seed_ ^= seed_ << 5;
            return float(seed_ >> 8) * (1.f / 16777216.f);
        }

        /** grain length in samples before the random spread: long and smooth
            when plain, short and bright when sparkling */
        float AvgLength() const { return sr_ * (.13f - .085f * glitter_); }

        void Trigger()
        {
            Grain *g = nullptr;
            for (Grain &c : grains_)
                if (c.left <= 0) { g = &c; break; }
            if (!g)
                return; // all busy: skip this one

            const int len = int(AvgLength() * (.7f + .6f * Rand()));

            /* pitch: up an octave with glitter's probability; past the
               middle, some go an octave and a fifth or two octaves up. Plain
               grains are detuned a few cents, for a shimmer */
            float semis = 0.f;
            if (Rand() < glitter_)
            {
                semis = 12.f;
                const float high = glitter_ > .5f ? (glitter_ - .5f) * .8f : 0.f;
                const float h    = Rand();
                if (h < high * .5f)
                    semis = 24.f;
                else if (h < high)
                    semis = 19.f;
            }
            else
                semis = (Rand() - .5f) * .16f;
            g->speed = exp2f(semis / 12.f);

            /* where: somewhere in the last ~.4 s, but far enough back that a
               grain played faster never catches up with the recording (the
               margin covers the block this grain starts in) */
            const float back = 128.f + fmaxf(g->speed - 1.f, 0.f) * float(len) + Rand() * .4f * sr_;
            float pos = float(write_) - back;
            while (pos < 0.f)
                pos += float(kCloudLen);
            g->pos = pos;

            const float w = 2.f * kPi / float(len);
            g->c  = 1.f;
            g->s  = 0.f;
            g->cd = cosf(w);
            g->sd = sinf(w);

            /* equal-power pan, wider the denser the cloud */
            const float p   = (Rand() - .5f) * (.5f + .5f * amount_); // -.5 .. .5
            const float lvl = .6f + .4f * Rand();
            g->gl   = lvl * sqrtf(.5f - p);
            g->gr   = lvl * sqrtf(.5f + p);
            g->left = len;
        }

        float    sr_ = 48000.f;
        float   *buf_ = nullptr;
        uint32_t write_ = 0;
        float    amount_ = 0.f, amount_target_ = 0.f, glitter_ = .5f;
        int      wait_ = 0;
        uint32_t seed_ = 1;
        Grain    grains_[kGrains];
    };
} // namespace chompi
