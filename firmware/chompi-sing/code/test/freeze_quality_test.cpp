/* Does a freeze sound like what was playing? Compares brightness (spectral
 * centre, via the sample-to-sample difference) and level of the live and
 * the frozen output, for the robot (Vocoder) and Human (VoiceFreeze).
 *
 *   g++ -std=c++14 -O2 -I../src freeze_quality_test.cpp -o fq && ./fq
 */
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <initializer_list>
#include "Vocoder.h"
#include "VoiceFreeze.h"
#include "PitchDetector.h"

using namespace chompi;
static const float kSr = 48000.f, kPi = 3.14159265f;
static const int kBlock = 48;
static int failures = 0;
static void Check(bool ok, const char *what) { if (!ok) { printf("    FAIL: %s\n", what); failures++; } }
static float hist[Vocoder::kHistFrames][Vocoder::kBands];
static float audio[VoiceFreeze::kLen], pitchs[VoiceFreeze::kFrames];
static float Noise() { return 2.f * (rand() / float(RAND_MAX)) - 1.f; }

/* a sung "a" with a little jitter and breath, rms ~ amp */
struct Voice
{
    float f0, ph = 0.f, j = 0.f;
    float Sample(float amp)
    {
        j += .002f * (.006f * 30.f * Noise() - j);
        const float f = f0 * (1.f + j);
        ph += f / kSr;
        if (ph > 1000.f) ph -= 1000.f;
        auto fm = [](float x, float c, float bw) { const float d = (x - c) / bw; return 1.f / (1.f + d * d); };
        float s = 0.f;
        for (int h = 1; h * f < 6000.f; h++)
        {
            const float fh = h * f;
            s += 1.f / h * (fm(fh, 700, 80) + .6f * fm(fh, 1220, 100) + .3f * fm(fh, 2600, 120) + .02f)
                 * sinf(2.f * kPi * h * ph + h * 1.3f);
        }
        return amp * (s * .9f + .03f * Noise());
    }
};

struct Meter
{
    double e = 0, d = 0; float prev = 0.f;
    void Add(const float *x, int n) { for (int i = 0; i < n; i++) { e += x[i] * x[i]; d += (x[i] - prev) * (x[i] - prev); prev = x[i]; } }
    double Bright() const { return e > 0 ? sqrt(d / e) * kSr / (2 * kPi) : 0; }
    double Rms(int n) const { return sqrt(e / n); }
};

int main()
{
    for (float f0 : {110.f, 220.f})
    {
        /* robot */
        {
            Vocoder v; v.Init(kSr, hist);
            SawOsc osc; osc.SetFreq(130.8f, kSr);
            Voice voice{f0};
            float mod[kBlock], car[kBlock], ol[kBlock], orr[kBlock];
            Meter live, frozen;
            int n = 0;
            for (int b = 0; b < 1500; b++)
            {
                const bool frz = b >= 1000;
                if (b == 1000) v.SetFreeze(true);
                for (int i = 0; i < kBlock; i++) { mod[i] = frz ? 0.f : voice.Sample(.05f); car[i] = osc.Process(); ol[i] = orr[i] = 0.f; }
                v.Process(mod, car, car, ol, orr, kBlock);
                if (b >= 500 && b < 1000) live.Add(ol, kBlock);
                if (b >= 1100) { frozen.Add(ol, kBlock); n += kBlock; }
            }
            const double db = 20 * log10(frozen.Rms(n) / live.Rms(500 * kBlock));
            printf("robot, voice %3.0f Hz: live %4.0f Hz bright, frozen %4.0f Hz (%+5.1f%%), level %+5.1f dB\n",
                   f0, live.Bright(), frozen.Bright(), 100 * (frozen.Bright() / live.Bright() - 1), db);
            Check(fabs(frozen.Bright() / live.Bright() - 1) < .08, "robot freeze keeps the brightness (8%)");
            Check(fabs(db) < 1.5, "robot freeze keeps the level (1.5 dB)");
        }
        /* human */
        {
            VoiceFreeze vf; vf.Init(kSr, audio, pitchs);
            PitchDetector pd; pd.Init();
            Voice voice{f0};
            float in[kBlock], out[kBlock];
            Meter live, frozen;
            int n = 0;
            for (int b = 0; b < 1500; b++)
            {
                if (b < 1000)
                {
                    for (int i = 0; i < kBlock; i++) in[i] = voice.Sample(.05f);
                    pd.Process(in, kBlock); pd.Update();
                    vf.Record(in, kBlock, pd.Voiced() ? pd.Note() : 0.f);
                    if (b >= 500) live.Add(in, kBlock);
                }
                else
                {
                    if (b == 1000) vf.SetFreeze(true);
                    vf.Play(out, kBlock);
                    if (b >= 1100) { frozen.Add(out, kBlock); n += kBlock; }
                }
            }
            const double db = 20 * log10(frozen.Rms(n) / live.Rms(500 * kBlock));
            printf("human, voice %3.0f Hz: live %4.0f Hz bright, frozen %4.0f Hz (%+5.1f%%), level %+5.1f dB\n",
                   f0, live.Bright(), frozen.Bright(), 100 * (frozen.Bright() / live.Bright() - 1), db);
            Check(fabs(frozen.Bright() / live.Bright() - 1) < .08, "human freeze keeps the brightness (8%)");
            Check(fabs(db) < 1.5, "human freeze keeps the level (1.5 dB)");
        }
    }
    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all checks passed", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
