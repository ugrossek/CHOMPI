/* Host test for PitchDetector.h with synthetic signals.
 * (On CHOMPI, Update() runs in the main loop; here once per block.)
 *
 *   g++ -std=c++14 -O2 -I../src pitch_test.cpp -o pitch_test && ./pitch_test
 *
 * Feeds 48 kHz audio in 48-sample blocks, as on CHOMPI, and reports accuracy,
 * voiced share and lock-in time. Exits non-zero if a check fails. */
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <functional>
#include "PitchDetector.h"

using chompi::PitchDetector;

static const float kSr    = 48000.f;
static const int   kBlock = 48;
static const float kPi    = 3.14159265f;
static int         failures = 0;

static float Cents(float f, float ref) { return 1200.f * log2f(f / ref); }

static void Check(bool ok, const char *what)
{
    if (!ok)
    {
        printf("    FAIL: %s\n", what);
        failures++;
    }
}

struct Run
{
    int   estimates = 0, voiced = 0;
    float worst_cents = 0.f, sum_abs_cents = 0.f;
    int   octave_errors = 0;
    float lock_ms = -1.f; // first time voiced and within 30 cents of the reference
};

/** gen(t) gives the sample, ref(t) the true pitch in Hz (0 = none);
 *  estimates before skip_ms are not scored */
static Run Analyse(std::function<float(float)> gen, std::function<float(float)> ref,
                   float seconds, float skip_ms)
{
    static PitchDetector pd;
    pd.Init();
    Run r;
    unsigned last = 0;
    const int n = int(seconds * kSr);
    std::vector<float> blk(kBlock);
    for (int s = 0; s < n; s += kBlock)
    {
        for (int i = 0; i < kBlock; i++)
            blk[i] = gen((s + i) / kSr);
        pd.Process(blk.data(), kBlock);
        pd.Update(); // the main loop's half, once per block here
        if (pd.Estimates() == last)
            continue;
        last = pd.Estimates();
        const float t_ms = (s + kBlock) / kSr * 1000.f;
        const float f0 = ref((s + kBlock) / kSr);
        if (f0 > 0.f && r.lock_ms < 0.f && pd.Voiced()
            && fabsf(Cents(pd.Frequency(), f0)) < 30.f)
            r.lock_ms = t_ms;
        if (t_ms < skip_ms)
            continue;
        r.estimates++;
        if (!pd.Voiced())
            continue;
        r.voiced++;
        if (f0 > 0.f)
        {
            const float c = Cents(pd.Frequency(), f0);
            if (fabsf(c) > 600.f)
                r.octave_errors++;
            else
            {
                r.sum_abs_cents += fabsf(c);
                if (fabsf(c) > fabsf(r.worst_cents))
                    r.worst_cents = c;
            }
        }
    }
    return r;
}

static void Report(const char *name, const Run &r)
{
    const int good = r.voiced - r.octave_errors;
    printf("%-34s voiced %5.1f%%  mean %5.1f c  worst %+6.1f c  octave err %d",
           name, 100.f * r.voiced / (r.estimates ? r.estimates : 1),
           good > 0 ? r.sum_abs_cents / good : 0.f, r.worst_cents, r.octave_errors);
    if (r.lock_ms >= 0.f)
        printf("  lock %.0f ms", r.lock_ms);
    printf("\n");
}

/* --- signals --------------------------------------------------------- */

static float Noise()
{
    return 2.f * (rand() / float(RAND_MAX)) - 1.f;
}

/** a crude sung vowel: band-limited pulse train (harmonics falling
 *  6 dB/octave) shaped by three formant resonances */
struct Vowel
{
    float f1, f2, f3;
    /** amp is the rms of the result */
    float Sample(float t, float f0, float amp) const
    {
        float s = 0.f, p = 0.f;
        for (int h = 1; h * f0 < 5000.f; h++)
        {
            const float g = Gain(h, f0);
            s += g * sinf(2.f * kPi * h * f0 * t + h * 1.3f);
            p += g * g * .5f;
        }
        return amp * s / sqrtf(p);
    }
    float Gain(int h, float f0) const
    {
        const float fh = h * f0;
        return 1.f / h
               * (Formant(fh, f1, 80.f) + .6f * Formant(fh, f2, 100.f)
                  + .3f * Formant(fh, f3, 120.f) + .02f);
    }
    static float Formant(float f, float fc, float bw)
    {
        const float d = (f - fc) / bw;
        return 1.f / (1.f + d * d);
    }
};

int main()
{
    printf("PitchDetector: %d Hz..%d Hz, frame %.1f ms, hop %.1f ms\n\n",
           int(PitchDetector::kFs / PitchDetector::kMaxLag),
           int(PitchDetector::kFs / PitchDetector::kMinLag),
           1000.f * PitchDetector::kFrame / PitchDetector::kFs,
           1000.f * PitchDetector::kHop / PitchDetector::kFs);

    /* sines across the range */
    for (float f : {82.4f, 110.f, 196.f, 330.f, 523.f, 880.f})
    {
        char name[64];
        snprintf(name, sizeof name, "sine %.0f Hz, -20 dB", f);
        Run r = Analyse([f](float t) { return .1f * sinf(2.f * kPi * f * t); },
                        [f](float) { return f; }, 1.f, 100.f);
        Report(name, r);
        Check(r.voiced > .95f * r.estimates, "sine voiced > 95%");
        Check(r.octave_errors == 0 && fabsf(r.worst_cents) < 10.f, "sine within 10 cents");
    }
    printf("\n");

    /* sung vowels: "a" and "i", low male to high female */
    const Vowel a{700.f, 1220.f, 2600.f}, ii{280.f, 2250.f, 2900.f};
    for (float f : {98.f, 147.f, 220.f, 392.f, 659.f})
        for (const Vowel *v : {&a, &ii})
        {
            char name[64];
            snprintf(name, sizeof name, "vowel \"%s\" %.0f Hz", v == &a ? "a" : "i", f);
            Run r = Analyse([v, f](float t) { return v->Sample(t, f, .05f); },
                            [f](float) { return f; }, 1.f, 100.f);
            Report(name, r);
            Check(r.voiced > .9f * r.estimates, "vowel voiced > 90%");
            Check(r.octave_errors == 0, "vowel: no octave errors");
            Check(fabsf(r.worst_cents) < 20.f, "vowel within 20 cents");
        }
    printf("\n");

    /* every semitone G2..F5, both vowels: the formants move over the
       harmonics, which is where octave errors come from */
    {
        int notes = 0, bad = 0;
        for (int m = 43; m <= 77; m++)
            for (const Vowel *v : {&a, &ii})
            {
                const float f = 440.f * powf(2.f, (m - 69) / 12.f);
                Run r = Analyse([v, f](float t) { return v->Sample(t, f, .05f); },
                                [f](float) { return f; }, .4f, 100.f);
                notes++;
                if (r.octave_errors || r.voiced < .9f * r.estimates || fabsf(r.worst_cents) > 20.f)
                {
                    bad++;
                    char name[64];
                    snprintf(name, sizeof name, "  vowel \"%s\" midi %d (%.0f Hz)", v == &a ? "a" : "i", m, f);
                    Report(name, r);
                }
            }
        printf("semitone sweep G2..F5, 2 vowels: %d of %d notes clean\n", notes - bad, notes);
        Check(bad == 0, "every note of the sweep clean");
    }
    printf("\n");

    /* vibrato: 220 Hz +-50 cents at 5.5 Hz */
    {
        float ph = 0.f, last_t = 0.f;
        auto fr = [](float t) { return 220.f * powf(2.f, .5f / 12.f * sinf(2.f * kPi * 5.5f * t)); };
        Run r = Analyse([&](float t) { ph += 2.f * kPi * fr(t) * (t - last_t); last_t = t; return .1f * sinf(ph); },
                        fr, 2.f, 100.f);
        Report("vibrato 220 Hz +-50 c, 5.5 Hz", r);
        Check(r.voiced > .95f * r.estimates, "vibrato voiced");
        Check(r.octave_errors == 0 && r.sum_abs_cents / r.voiced < 25.f, "vibrato tracked (mean < 25 c)");
    }

    /* a less regular voice: the pitch wanders randomly by ~1% (jitter)
       and the level by ~10% (shimmer), both smoothed over a few periods */
    for (float f : {110.f, 262.f})
    {
        float ph = 0.f, last_t = 0.f, j = 0.f, sh = 0.f;
        Run r = Analyse([&](float t) {
                            j  += .02f * (.01f * 3.f * Noise() - j);
                            sh += .02f * (.1f * 3.f * Noise() - sh);
                            const float fi = f * (1.f + j);
                            ph += 2.f * kPi * fi * (t - last_t);
                            last_t = t;
                            float s = 0.f;
                            for (int h = 1; h * fi < 4000.f; h++)
                                s += a.Gain(h, fi) * sinf(h * ph + h * 1.3f);
                            return .05f * (1.f + sh) * s;
                        },
                        [f](float) { return f; }, 1.f, 100.f);
        char name[64];
        snprintf(name, sizeof name, "vowel %.0f Hz, jitter + shimmer", f);
        Report(name, r);
        Check(r.voiced > .9f * r.estimates && r.octave_errors == 0, "jittery voice tracked");
    }

    /* vowel in noise, 20 dB SNR */
    {
        Run r = Analyse([&](float t) { return a.Sample(t, 165.f, .05f) + .004f * Noise(); },
                        [](float) { return 165.f; }, 1.f, 100.f);
        Report("vowel 165 Hz + noise (~20 dB SNR)", r);
        Check(r.voiced > .8f * r.estimates && r.octave_errors == 0, "noisy vowel tracked");
    }
    printf("\n");

    /* things that must not count as a voice */
    {
        Run r = Analyse([](float) { return 0.f; }, [](float) { return 0.f; }, 1.f, 0.f);
        Report("silence", r);
        Check(r.voiced == 0, "silence never voiced");
    }
    {
        Run r = Analyse([](float) { return .05f * Noise(); }, [](float) { return 0.f; }, 1.f, 100.f);
        Report("white noise -26 dB", r);
        Check(r.voiced < .05f * r.estimates, "noise voiced < 5%");
    }
    {
        /* key clicks: a sharp decaying burst every 120 ms */
        Run r = Analyse([](float t) {
                            const float u = fmodf(t, .12f);
                            return u < .004f ? .3f * expf(-u * 1500.f) * Noise() : 0.f;
                        },
                        [](float) { return 0.f; }, 1.f, 0.f);
        Report("key clicks every 120 ms", r);
        Check(r.voiced < .1f * r.estimates, "clicks voiced < 10%");
    }
    {
        /* breathy consonant: lowpassed noise */
        float lp = 0.f;
        Run r = Analyse([&](float) { lp += .2f * (.2f * Noise() - lp); return lp; },
                        [](float) { return 0.f; }, 1.f, 100.f);
        Report("breath / \"sh\" (filtered noise)", r);
        Check(r.voiced < .1f * r.estimates, "breath voiced < 10%");
    }
    printf("\n");

    /* lock-in: silence, then a note at 300 ms */
    for (float f : {98.f, 220.f, 440.f})
    {
        char name[64];
        snprintf(name, sizeof name, "onset %.0f Hz after silence", f);
        Run r = Analyse([&a, f](float t) { return t < .3f ? 0.f : a.Sample(t, f, .05f); },
                        [f](float t) { return t < .3f ? 0.f : f; }, .6f, 0.f);
        r.lock_ms -= 300.f;
        Report(name, r);
        Check(r.lock_ms > 0.f && r.lock_ms < 45.f, "locks within 45 ms of the onset");
    }
    /* note change: 220 -> 330 Hz at 300 ms */
    {
        Run r = Analyse([&a](float t) { return a.Sample(t, t < .3f ? 220.f : 330.f, .05f); },
                        [](float t) { return t < .3f ? 0.f : 330.f; }, .6f, 0.f);
        r.lock_ms -= 300.f;
        Report("note change 220 -> 330 Hz", r);
        Check(r.lock_ms > 0.f && r.lock_ms < 45.f, "follows a new note within 45 ms");
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all checks passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
