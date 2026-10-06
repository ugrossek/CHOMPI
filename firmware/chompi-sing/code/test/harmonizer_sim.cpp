/* The whole Harmonizer on the PC (with DaisySP's DC blocker and SVF), for
 * what the DSP-only tests can't see: Human, a chord held, a sung vowel,
 * then freeze. Measures how much the output level moves in 4 ms windows
 * (a "flap" shows up as a high number), with and without latch.
 *
 *   L=../../../chompi-tape/code/libs/DaisySP/Source
 *   g++ -std=c++14 -O2 -I../src -I$L harmonizer_sim.cpp \
 *       $L/Utility/dcblock.cpp $L/Filters/svf.cpp -o hsim && ./hsim
 */
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include "Harmonizer.h"
#include "PitchDetector.h"

static const int kPoly = 7;
float chompi::shift_mem[kPoly][chompi::kShiftBufFrames * 2];
int16_t chompi::shift_ana[kPoly][chompi::kShiftAnaLen];
float chompi::chorus_mem[2][chompi::kChorusLen];
float chompi::vocoder_hist[chompi::Vocoder::kHistFrames][chompi::Vocoder::kBands];
float chompi::voice_audio[chompi::VoiceFreeze::kLen];
float chompi::voice_pitch[chompi::VoiceFreeze::kFrames];

static const float kSr = 48000.f, kPi = 3.14159265f;
static const int kBlock = 48;
static float Noise() { return 2.f * (rand() / float(RAND_MAX)) - 1.f; }

struct Voice
{
    float f0, ph = 0.f, j = 0.f;
    float Sample(float amp)
    {
        j += .002f * (.006f * 30.f * Noise() - j);
        ph += f0 * (1.f + j) / kSr;
        if (ph > 1000.f) ph -= 1000.f;
        auto fm = [](float x, float c, float bw) { const float d = (x - c) / bw; return 1.f / (1.f + d * d); };
        float s = 0.f;
        for (int h = 1; h * f0 < 6000.f; h++)
        {
            const float fh = h * f0;
            s += 1.f / h * (fm(fh, 700, 80) + .6f * fm(fh, 1220, 100) + .3f * fm(fh, 2600, 120) + .02f)
                 * sinf(2.f * kPi * h * ph + h * 1.3f);
        }
        return amp * (s * .9f + .03f * Noise());
    }
};

/* level of x in 4 ms windows: spread / mean */
struct Flap
{
    double win = 0; int n = 0; std::vector<double> w;
    void Add(const float *x, int k)
    {
        for (int i = 0; i < k; i++) { win += x[i] * x[i]; if (++n == 192) { w.push_back(sqrt(win / 192)); win = 0; n = 0; } }
    }
    double Depth() const
    {
        double m = 0, v = 0; for (double a : w) m += a; m /= w.size();
        for (double a : w) v += (a - m) * (a - m);
        return sqrt(v / w.size()) / (m > 0 ? m : 1);
    }
    double Mean() const { double m = 0; for (double a : w) m += a; return m / w.size(); }
};

static void Run(bool latch)
{
    static chompi::Harmonizer<kPoly> h;
    static chompi::PitchDetector pd;
    h.Init(kSr);
    pd.Init();
    h.SetMode(chompi::Harmonizer<kPoly>::ChordMode::Keys);
    h.SetLatch(latch);
    Voice voice{196.f};
    float in[kBlock], ol[kBlock], orr[kBlock];
    Flap live, frozen;
    for (int b = 0; b < 2500; b++)
    {
        if (b == 50) { h.NoteOn(1, 0.f); h.NoteOn(2, 4.f); h.NoteOn(3, 7.f); } // C E G
        if (b == 1200) h.SetFreeze(true);
        const bool sing = b < 1300;
        for (int i = 0; i < kBlock; i++) { in[i] = sing ? voice.Sample(.05f) : 0.f; ol[i] = orr[i] = 0.f; }
        pd.Process(in, kBlock);
        pd.Update();
        h.SetSung(pd.Voiced(), pd.Note());
        h.Process(in, false, ol, orr, kBlock);
        if (b >= 700 && b < 1200) live.Add(ol, kBlock);
        if (b >= 1500) frozen.Add(ol, kBlock);
    }
    printf("latch %-3s: live level %.4f flap %.2f | frozen level %.4f flap %.2f\n",
           latch ? "on" : "off", live.Mean(), live.Depth(), frozen.Mean(), frozen.Depth());
}

int main()
{
    Run(false);
    Run(true);
}
