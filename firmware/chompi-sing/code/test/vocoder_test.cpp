/* Host test for Vocoder.h: does a voice come out on the carrier's notes?
 *
 *   g++ -std=c++14 -O2 -I../src vocoder_test.cpp -o vocoder_test && ./vocoder_test
 *
 * The pitch detector listens to the output: whatever the voice's pitch, it
 * must hear the carrier's note. Also checks level, silence and how fast the
 * output follows the voice. */
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <functional>
#include "Vocoder.h"
#include "PitchDetector.h"

using namespace chompi;

static const float kSr = 48000.f, kPi = 3.14159265f;
static const int   kBlock = 48;
static int failures = 0;
static void Check(bool ok, const char *what)
{
    if (!ok) { printf("    FAIL: %s\n", what); failures++; }
}
static float Noise() { return 2.f * (rand() / float(RAND_MAX)) - 1.f; }

/* a sung "a": harmonics shaped by three formants, rms = amp */
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

struct Result { float out_rms, in_rms, pitch_hz; float voiced_share; float bright_hz; };

/** run voice(t) through the vocoder with one carrier note; detector on output */
static Result Run(std::function<float(float)> voice, float note_hz, float seconds,
                  std::vector<float> *trace = nullptr, float shift = 0.f)
{
    static Vocoder v;
    static PitchDetector pd;
    v.Init(kSr);
    v.SetShift(shift);
    /* brightness: rms of the sample-to-sample difference against the rms,
       which grows with the spectrum's centre of gravity */
    float prev = 0.f;
    double s_diff = 0;
    pd.Init();
    SawOsc osc;
    osc.SetFreq(note_hz, kSr);
    double so = 0, si = 0;
    int n = 0, est = 0, voiced = 0;
    float last_hz = 0.f;
    unsigned last = 0;
    float mod[kBlock], car[kBlock], ol[kBlock], or_[kBlock];
    for (int s = 0; s < int(seconds * kSr); s += kBlock)
    {
        for (int i = 0; i < kBlock; i++)
        {
            mod[i] = voice((s + i) / kSr);
            car[i] = osc.Process() + .05f * Noise();
            ol[i] = or_[i] = 0.f;
        }
        v.Process(mod, car, car, ol, or_, kBlock);
        pd.Process(ol, kBlock);
        pd.Update();
        for (int i = 0; i < kBlock; i++)
        {
            if (trace) trace->push_back(ol[i]);
            const float d = ol[i] - prev;
            prev = ol[i];
            if (s > .1f * kSr) { so += ol[i] * ol[i]; si += mod[i] * mod[i]; s_diff += d * d; n++; }
        }
        if (pd.Estimates() != last && s > .1f * kSr)
        {
            last = pd.Estimates();
            est++;
            if (pd.Voiced()) { voiced++; last_hz = pd.Frequency(); }
        }
    }
    return { float(sqrt(so / (n ? n : 1))), float(sqrt(si / (n ? n : 1))), last_hz,
             est ? float(voiced) / est : 0.f,
             so > 0 ? float(sqrt(s_diff / so) * kSr / (2.f * kPi)) : 0.f };
}

int main()
{
    printf("Vocoder: %d bands, %.0f Hz .. %.0f Hz\n\n", Vocoder::kBands, Vocoder::kLow, Vocoder::kHigh);

    /* whatever the voice sings, the output plays the carrier's note */
    for (float voice_hz : {110.f, 196.f, 330.f})
        for (float note_hz : {130.8f, 220.f, 329.6f})
        {
            Result r = Run([voice_hz](float t) { return Vowel(t, voice_hz, .05f); }, note_hz, 1.f);
            const float cents = r.pitch_hz > 0.f ? 1200.f * log2f(r.pitch_hz / note_hz) : 9999.f;
            printf("voice %3.0f Hz, key %5.1f Hz -> output %6.1f Hz (%+5.1f c), level %+5.1f dB, voiced %3.0f%%\n",
                   voice_hz, note_hz, r.pitch_hz, cents,
                   20.f * log10f(r.out_rms / r.in_rms), 100.f * r.voiced_share);
            Check(fabsf(cents) < 15.f, "output on the key's note");
            Check(r.voiced_share > .9f, "output clearly pitched");
            Check(fabsf(20.f * log10f(r.out_rms / r.in_rms)) < 6.f, "output level within 6 dB of the voice");
        }
    printf("\n");

    /* size: formants moved by +-3 bands; still on the key's note, darker
       for a big robot, brighter for a small one */
    {
        float bright[3];
        const float shifts[3] = {3.f, 0.f, -3.f};
        const char *names[3] = {"big (monster)", "as sung", "small (mouse)"};
        for (int k = 0; k < 3; k++)
        {
            Result r = Run([](float t) { return Vowel(t, 196.f, .05f); }, 220.f, 1.f, nullptr, shifts[k]);
            bright[k] = r.bright_hz;
            printf("size %-14s -> output %6.1f Hz, level %+5.1f dB, brightness %5.0f Hz\n",
                   names[k], r.pitch_hz, 20.f * log10f(r.out_rms / r.in_rms), r.bright_hz);
            Check(fabsf(1200.f * log2f(r.pitch_hz / 220.f)) < 15.f, "size keeps the key's note");
            Check(fabsf(20.f * log10f(r.out_rms / r.in_rms)) < 10.f, "size keeps the level within 10 dB");
        }
        Check(bright[0] < bright[1] && bright[1] < bright[2], "big is darker, small is brighter");
    }
    printf("\n");

    /* talking: a whispered/unvoiced voice (filtered noise) still comes out */
    {
        float lp = 0.f;
        Result r = Run([&](float) { lp += .3f * (.15f * Noise() - lp); return lp; }, 220.f, 1.f);
        printf("whisper (filtered noise)     -> level %+5.1f dB, pitch %6.1f Hz\n",
               20.f * log10f(r.out_rms / r.in_rms), r.pitch_hz);
        Check(r.out_rms > .2f * r.in_rms, "a whisper is heard");
    }

    /* freeze: sing, freeze, stop singing: the sound stays; unfreeze: gone */
    {
        Vocoder v;
        v.Init(kSr);
        SawOsc osc;
        osc.SetFreq(220.f, kSr);
        float mod[kBlock], car[kBlock], ol[kBlock], orr[kBlock];
        auto block = [&](int s, bool voice) {
            double e = 0;
            for (int i = 0; i < kBlock; i++)
            {
                mod[i] = voice ? Vowel((s + i) / kSr, 196.f, .05f) : 0.f;
                car[i] = osc.Process();
                ol[i] = orr[i] = 0.f;
            }
            v.Process(mod, car, car, ol, orr, kBlock);
            for (int i = 0; i < kBlock; i++) e += ol[i] * ol[i];
            return sqrt(e / kBlock);
        };
        int s = 0;
        double sung = 0, frozen = 0, after = 0;
        for (int b = 0; b < 300; b++, s += kBlock) sung = block(s, true);       // 300 ms singing
        v.SetFreeze(true);
        for (int b = 0; b < 500; b++, s += kBlock) frozen = block(s, false);    // 500 ms silent, frozen
        v.SetFreeze(false);
        for (int b = 0; b < 300; b++, s += kBlock) after = block(s, false);     // 300 ms silent, live
        printf("freeze: singing %.4f, frozen in silence %.4f, unfrozen %.6f\n", sung, frozen, after);
        Check(frozen > .5 * sung, "a freeze keeps the sound while the voice is silent");
        Check(after < 1e-4, "unfreezing lets it go");
    }

    /* silence in, silence out */
    {
        Result r = Run([](float) { return 0.f; }, 220.f, .5f);
        printf("silence                      -> output rms %.6f\n", r.out_rms);
        Check(r.out_rms < 1e-4f, "silence stays silent");
    }

    /* how fast the output follows the voice: onset at 200 ms */
    {
        std::vector<float> tr;
        Run([](float t) { return t < .2f ? 0.f : Vowel(t, 196.f, .05f); }, 220.f, .4f, &tr);
        /* level in 1 ms windows; final = mean of the last 100 ms */
        auto win = [&](int ms) { double s = 0; for (int i = 0; i < 48; i++) { float x = tr[ms * 48 + i]; s += x * x; } return sqrt(s / 48); };
        double fin = 0; for (int ms = 300; ms < 400; ms++) fin += win(ms); fin /= 100;
        int t_half = -1;
        for (int ms = 200; ms < 400; ms++) if (win(ms) > .5 * fin) { t_half = ms - 200; break; }
        printf("onset: half level after %d ms\n", t_half);
        Check(t_half >= 0 && t_half <= 10, "follows the voice within 10 ms");
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all checks passed", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
