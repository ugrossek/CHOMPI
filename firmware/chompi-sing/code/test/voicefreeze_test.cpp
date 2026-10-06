/* Host test for VoiceFreeze.h: the real voice frozen and scrubbed.
 *
 *   g++ -std=c++14 -O2 -I../src voicefreeze_test.cpp -o voicefreeze_test && ./voicefreeze_test
 *
 * Sings into it with the pitch detector feeding the recorded notes (as the
 * engine does), then freezes: the output must hold the frozen note, steady
 * and about as loud; turned back, it must play what was sung then. */
#include <cstdio>
#include <cmath>
#include <vector>
#include "VoiceFreeze.h"
#include "PitchDetector.h"

using namespace chompi;

static const float kSr = 48000.f, kPi = 3.14159265f;
static const int   kBlock = 48;
static int failures = 0;
static void Check(bool ok, const char *what) { if (!ok) { printf("    FAIL: %s\n", what); failures++; } }

static float audio[VoiceFreeze::kLen];
static float pitch[VoiceFreeze::kFrames];

static float Vowel(float t, float f0, float amp)
{
    auto fm = [](float f, float fc, float bw) { const float d = (f - fc) / bw; return 1.f / (1.f + d * d); };
    float s = 0.f, p = 0.f;
    for (int h = 1; h * f0 < 6000.f; h++)
    {
        const float fh = h * f0;
        const float g = 1.f / h * (fm(fh, 700, 80) + .6f * fm(fh, 1220, 100) + .3f * fm(fh, 2600, 120) + .02f);
        s += g * sinf(2.f * kPi * fh * t + h * 1.3f);
        p += g * g * .5f;
    }
    return amp * s / sqrtf(p);
}

int main()
{
    static VoiceFreeze vf;
    static PitchDetector live, check;
    vf.Init(kSr, audio, pitch);
    live.Init();
    float in[kBlock], out[kBlock];
    int s = 0;

    /* sing 196 Hz for 800 ms, then 330 Hz for 800 ms */
    for (int b = 0; b < 1600; b++, s += kBlock)
    {
        const float f0 = b < 800 ? 196.f : 330.f;
        for (int i = 0; i < kBlock; i++)
            in[i] = Vowel((s + i) / kSr, f0, .05f);
        live.Process(in, kBlock);
        live.Update();
        vf.Record(in, kBlock, live.Voiced() ? live.Note() : 0.f);
    }

    /* freeze at now, then turned 1200 ms back; measure pitch, steadiness, level */
    auto measure = [&](const char *name, float expect_hz) {
        check.Init();
        double e = 0;
        int n = 0, est = 0, good = 0;
        float worst = 0.f;
        unsigned last = 0;
        for (int b = 0; b < 600; b++, s += kBlock)
        {
            vf.Play(out, kBlock);
            check.Process(out, kBlock);
            check.Update();
            if (b < 100)
                continue; // the glide and the detector settle
            for (int i = 0; i < kBlock; i++) { e += out[i] * out[i]; n++; }
            if (check.Estimates() != last)
            {
                last = check.Estimates();
                est++;
                if (check.Voiced())
                {
                    const float c = 1200.f * log2f(check.Frequency() / expect_hz);
                    if (fabsf(c) < 50.f) good++;
                    if (fabsf(c) > fabsf(worst)) worst = c;
                }
            }
        }
        const float rms = float(sqrt(e / n));
        printf("%-30s note %5.1f (expect %5.1f), on pitch %3d%%, worst %+6.1f c, level %+5.1f dB\n",
               name, vf.Note(), 69.f + 12.f * log2f(expect_hz / 440.f), 100 * good / (est ? est : 1),
               worst, 20.f * log10f(rms / .05f));
        Check(good > .95f * est, "frozen output holds the note");
        Check(fabsf(20.f * log10f(rms / .05f)) < 3.f, "frozen output about as loud as the voice");
        Check(fabsf(vf.Note() - (69.f + 12.f * log2f(expect_hz / 440.f))) < .3f, "reports the note sung there");
    };

    vf.SetFreeze(true);
    measure("frozen at now (330 Hz)", 330.f);
    vf.Scrub(1200.f);
    measure("turned back 1200 ms (196 Hz)", 196.f);
    printf("position %.2f\n", vf.Position());
    Check(vf.Position() > .45f && vf.Position() < .55f, "position ~1.2 of ~2.4 s usable");

    /* freezing in the middle of a glide holds the last steady note before
       it, not a repeating piece of the glide (which flutters) */
    {
        static VoiceFreeze g;
        static PitchDetector gd, gc;
        g.Init(kSr, audio, pitch);
        gd.Init();
        float ph = 0.f;
        for (int b = 0; b < 1000; b++)
        {
            for (int i = 0; i < kBlock; i++)
            {
                const float t = (b * kBlock + i) / kSr;
                const float f = t > .85f ? 196.f * powf(262.f / 196.f, (t - .85f) / .15f) : 196.f;
                ph += f / kSr;
                if (ph > 1000.f) ph -= 1000.f;
                float x = 0.f;
                for (int h = 1; h * f < 5000.f; h++) x += sinf(2.f * kPi * h * ph) / h;
                in[i] = .05f * x;
            }
            gd.Process(in, kBlock);
            gd.Update();
            g.Record(in, kBlock, gd.Voiced() ? gd.Note() : 0.f);
        }
        g.SetFreeze(true);
        gc.Init();
        unsigned last = 0;
        float lo = 1e9f, hi = -1e9f;
        for (int b = 0; b < 800; b++)
        {
            g.Play(out, kBlock);
            gc.Process(out, kBlock);
            gc.Update();
            if (b > 100 && gc.Estimates() != last)
            {
                last = gc.Estimates();
                if (gc.Voiced())
                {
                    const float c = 1200.f * log2f(gc.Frequency() / 196.f);
                    lo = fminf(lo, c);
                    hi = fmaxf(hi, c);
                }
            }
        }
        printf("frozen during a glide: pitch %+.0f .. %+.0f cents from the note before it\n", lo, hi);
        Check(hi - lo < 15.f && fabsf(lo) < 15.f, "a freeze during a glide holds the steady note before it");
    }

    /* unfreeze: records again */
    vf.SetFreeze(false);
    Check(!vf.Frozen(), "unfreezes");

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all checks passed", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
