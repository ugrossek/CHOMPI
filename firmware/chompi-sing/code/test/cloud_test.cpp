/* Host test for Cloud.h: the glitter cloud
 *
 *   g++ -std=c++14 -O2 -I../src cloud_test.cpp -o cloud_test && ./cloud_test
 *
 * A sung "a" at 220 Hz goes in. Off, it must come out untouched; plain, the
 * cloud stays on 220 Hz; sparkling, much of it moves an octave up. The level
 * must stay near the input's whatever the density, and a grain must never
 * read audio that hasn't been recorded yet. */
#include <cstdio>
#include <cmath>
#include <vector>
#include "Cloud.h"

using namespace chompi;

float chompi::cloud_mem[kCloudLen];

static const float kSr = 48000.f, kPi = 3.14159265f;
static const int   kBlock = 48;
static int failures = 0;
static void Check(bool ok, const char *what)
{
    if (!ok) { printf("    FAIL: %s\n", what); failures++; }
}

static float Vowel(float t, float f0)
{
    static const float f[3] = {700.f, 1200.f, 2600.f}, bw[3] = {110.f, 120.f, 160.f};
    float x = 0.f;
    for (int h = 1; f0 * h < 6000.f; h++)
    {
        float a = 0.f;
        for (int k = 0; k < 3; k++)
        {
            const float d = (f0 * h - f[k]) / bw[k];
            a += 1.f / (1.f + d * d);
        }
        x += a / h * sinf(2.f * kPi * f0 * h * t);
    }
    return .1f * x;
}

/** energy around frequency f: Goertzel over 1024-sample frames (a ~50 Hz
    band, wide enough for grains, whose phases are random), averaged */
static float Energy(const std::vector<float> &x, float f)
{
    const size_t kFrame = 1024;
    const float  w = 2.f * kPi * f / kSr, c = 2.f * cosf(w);
    double sum = 0.;
    size_t frames = 0;
    for (size_t at = 0; at + kFrame <= x.size(); at += kFrame, frames++)
    {
        float s1 = 0.f, s2 = 0.f;
        for (size_t i = at; i < at + kFrame; i++) { const float s = x[i] + c * s1 - s2; s2 = s1; s1 = s; }
        sum += s1 * s1 + s2 * s2 - c * s1 * s2;
    }
    return float(sum / frames);
}

static float Rms(const std::vector<float> &x)
{
    double e = 0.;
    for (float v : x) e += v * v;
    return sqrtf(float(e / x.size()));
}

struct Run { std::vector<float> in, l, r; };

/** 3 s of the vowel through a cloud; returns the last 2 s */
static Run Through(float amount, float glitter, float f0 = 220.f, bool sine = false)
{
    Cloud c;
    c.Init(kSr, cloud_mem);
    c.SetAmount(amount);
    c.SetGlitter(glitter);
    Run run;
    const int n = int(3.f * kSr) / kBlock * kBlock;
    for (int b = 0; b < n; b += kBlock)
    {
        float l[kBlock], r[kBlock];
        for (int i = 0; i < kBlock; i++)
            l[i] = r[i] = sine ? .3f * sinf(2.f * kPi * f0 * (b + i) / kSr)
                               : Vowel((b + i) / kSr, f0);
        if (b >= kSr)
            for (int i = 0; i < kBlock; i++)
                run.in.push_back(l[i]);
        c.Process(l, r, kBlock);
        if (b >= kSr)
            for (int i = 0; i < kBlock; i++) { run.l.push_back(l[i]); run.r.push_back(r[i]); }
    }
    return run;
}

int main()
{
    printf("off: untouched\n");
    {
        Run o = Through(0.f, .5f);
        float d = 0.f;
        for (size_t i = 0; i < o.in.size(); i++)
            d = fmaxf(d, fmaxf(fabsf(o.l[i] - o.in[i]), fabsf(o.r[i] - o.in[i])));
        printf("    max difference %g\n", d);
        Check(d == 0.f, "amount 0 must pass the input through unchanged");
    }

    printf("level at every density, plain and sparkling\n");
    for (float g : {0.f, 1.f})
        for (float a : {.2f, .5f, 1.f})
        {
            Run o = Through(a, g);
            const float in = Rms(o.in), out = .5f * (Rms(o.l) + Rms(o.r));
            bool finite = true;
            for (float v : o.l) finite = finite && std::isfinite(v);
            printf("    glitter %.0f amount %.1f: out/in %.2f (%+.1f dB)\n", g, a, out / in,
                   20.f * log10f(out / in));
            Check(finite, "output must be finite");
            Check(out / in > .5f && out / in < 2.f, "level within 6 dB of the input");
        }

    printf("pitch: plain stays, sparkle goes an octave up (a 220 Hz sine)\n");
    {
        Run plain = Through(1.f, 0.f, 220.f, true), sparkle = Through(1.f, 1.f, 220.f, true);
        auto ratio = [](const Run &o) {
            std::vector<float> m(o.l.size());
            for (size_t i = 0; i < m.size(); i++) m[i] = o.l[i] + o.r[i];
            return Energy(m, 440.f) / Energy(m, 220.f);
        };
        const float rp = ratio(plain), rs = ratio(sparkle);
        printf("    440/220 energy: plain %.3f, sparkle %.3f\n", rp, rs);
        Check(rp < .05f, "plain must stay on the note");
        Check(rs > .5f, "sparkle must put much of it an octave up");
    }

    printf("no grain reads ahead of the recording\n");
    {
        /* the buffer starts as NaN; until it wraps (1.37 s) everything
           ahead of the write head is NaN, so a read there poisons the
           output. Grains reach at most ~.91 s back and last up to ~.18 s,
           so from 1.1 s on no grain may still touch the NaN past */
        for (float g : {0.f, .5f, 1.f})
        {
            Cloud c;
            c.Init(kSr, cloud_mem);
            for (size_t i = 0; i < kCloudLen; i++) cloud_mem[i] = NAN;
            c.SetAmount(1.f);
            c.SetGlitter(g);
            bool ok = true;
            for (int b = 0; b < int(1.35f * kSr); b += kBlock)
            {
                float l[kBlock], r[kBlock];
                for (int i = 0; i < kBlock; i++) l[i] = r[i] = Vowel((b + i) / kSr, 220.f);
                c.Process(l, r, kBlock);
                if (b > int(1.1f * kSr))
                    for (int i = 0; i < kBlock; i++)
                        ok = ok && std::isfinite(l[i]) && std::isfinite(r[i]);
            }
            printf("    glitter %.1f: %s\n", g, ok ? "only recorded audio read" : "READ AHEAD");
            Check(ok, "grains must read only recorded audio");
        }
    }

    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
