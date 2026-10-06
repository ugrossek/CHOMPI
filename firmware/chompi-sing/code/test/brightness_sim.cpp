/* How bright each character comes out: the share of energy above 6 kHz in
 * the output against the sung voice going in (the whole Harmonizer on the
 * PC with DaisySP's filters). Robot is darker than the voice; Human shifted
 * up gets brighter (the chipmunk, eased by a lowpass per voice; the real fix
 * is formant-preserving shifting); Toy is smoothed like the real toy.
 *
 *   L=../../../chompi-tape/code/libs/DaisySP/Source
 *   g++ -std=c++14 -O2 -I../src -I$L -I$L/Utility brightness_sim.cpp \
 *       $L/Utility/dcblock.cpp $L/Filters/svf.cpp -o bsim && ./bsim
 */
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <initializer_list>
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

/* energy above ~6 kHz (2nd-order highpass), as a share of all, in dB */
struct HF
{
    float b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    double hi = 0, all = 0;
    HF()
    {
        const float w = 2.f * kPi * 6000.f / kSr, al = sinf(w) / (2.f * .707f), c = cosf(w), a0 = 1 + al;
        b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    void Add(float x)
    {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        hi += y * y; all += x * x;
    }
    double dB() const { return 10 * log10(hi / (all > 0 ? all : 1) + 1e-12); }
    double Rms(int n) const { return sqrt(all / n); }
};

static void Case(const char *name, chompi::Harmonizer<kPoly>::ChordMode mode, float chr,
                 std::vector<float> keys, bool freeze, float size = .5f)
{
    static chompi::Harmonizer<kPoly> h;
    static chompi::PitchDetector pd;
    h.Init(kSr); pd.Init();
    h.SetMode(mode);
    h.SetCharacter(chr);
    h.SetSize(size);
    Voice voice{196.f};
    float in[kBlock], ol[kBlock], orr[kBlock];
    HF hin, hout;
    int n = 0;
    for (int b = 0; b < 2500; b++)
    {
        if (b == 50) for (size_t k = 0; k < keys.size(); k++) h.NoteOn(int(k) + 1, keys[k]);
        if (freeze && b == 1200) h.SetFreeze(true);
        const bool sing = !freeze || b < 1300;
        for (int i = 0; i < kBlock; i++) { in[i] = sing ? voice.Sample(.05f) : 0.f; ol[i] = orr[i] = 0.f; }
        pd.Process(in, kBlock); pd.Update();
        h.SetSung(pd.Voiced(), pd.Note());
        h.Process(in, false, ol, orr, kBlock);
        const bool measure = freeze ? b >= 1500 : (b >= 700);
        if (b >= 700 && b < 1200) for (int i = 0; i < kBlock; i++) hin.Add(in[i]);
        if (measure) { for (int i = 0; i < kBlock; i++) hout.Add(ol[i]); n += kBlock; }
    }
    printf("%-44s voice %+6.1f dB  out %+6.1f dB  (%+5.1f)  level %.4f\n", name, hin.dB(), hout.dB(), hout.dB() - hin.dB(), hout.Rms(n));
}

int main()
{
    using M = chompi::Harmonizer<kPoly>::ChordMode;
    printf("share of energy above 6 kHz\n");
    Case("Robot, character soft (0)", M::Robot, 0.f, {0, 4, 7}, false);
    Case("Robot, character middle (.5)", M::Robot, .5f, {0, 4, 7}, false);
    Case("Robot, character .75", M::Robot, .75f, {0, 4, 7}, false);
    Case("Robot, size small (1)", M::Robot, .5f, {0, 4, 7}, false, 1.f);
    Case("Robot, frozen", M::Robot, .5f, {0, 4, 7}, true);
    Case("Human, keys a little above the voice", M::Keys, .5f, {0, 4, 7}, false);
    Case("Human, keys an octave above", M::Keys, .5f, {12, 16, 19}, false);
    Case("Human, keys below the voice", M::Keys, .5f, {-12, -8, -5}, false);
    Case("Human, one key at the voice (G3)", M::Keys, .5f, {-5}, false);
    Case("Human, frozen", M::Keys, .5f, {0, 4, 7}, true);
    Case("Human, keys an octave above, bright (1)", M::Keys, 1.f, {12, 16, 19}, false);
    Case("Human, keys two octaves above", M::Keys, .5f, {24}, false);
    Case("Toy, character middle", M::Toy, .5f, {0, 4, 7}, false);
    Case("Toy, character full", M::Toy, 1.f, {0, 4, 7}, false);
}
