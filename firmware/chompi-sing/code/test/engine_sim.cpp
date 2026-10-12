/* SING's whole audio engine on the PC: DSPEngine.h's Engine::Process, the
 * same code as on CHOMPI (input gain, key-click ducking, pitch detector,
 * harmonizer, filter, echo, reverb, limiter), fed from a recording as if it
 * came into the built-in mic, with keys held and the loop button pressed.
 * libDaisy is replaced by test/host/daisy.h.
 *
 *   L=../../../chompi-tape/code/libs
 *   g++ -std=c++14 -O2 -Ihost -I../src -I$L/DaisySP/Source -I$L/DaisySP/Source/Utility \
 *       -I$L/libDaisy/src engine_sim.cpp $(find $L/DaisySP/Source -name '*.cpp') -o engine_sim
 *   ./engine_sim voice.wav out.wav [options]
 *
 * Input: a 16-bit wav at 48 kHz, or a Freeze Dump's freeze-audio.f32. Options:
 *   --mode robot|human|toy   character (default human)
 *   --keys 0,4,7             semitones from middle C held from the start (default 0)
 *   --freeze T               press loop (freeze) at T seconds
 *   --click A                the loop button's mechanical click into the mic,
 *                            raw peak A (default 0: none); press and release
 *   --mainloop N             the pitch update every N blocks, as the main loop
 *                            manages (default 1)
 *   --level DB               the voice's rms at the mic, dBFS raw (default -37.5,
 *                            which the input gain brings to the sims' .05)
 *   --tail S                 seconds of silence after the recording (default 3)
 *   --stop-at-freeze         the voice stops when loop is pressed
 *   --wheel-jitter HZ        the big wheel chatters one detent back and forth,
 *                            HZ times a second, while frozen
 *   --speed X                play the recording at X (.55: slower and lower, a
 *                            child's voice in a grown-up's range)
 *   --noise DB               add breath: noise following the voice, DB relative
 *                            to it
 *
 * Prints how steady the frozen sound is (as test/harmonizer_sim.cpp: the
 * spread of 4 ms rms windows, and the strongest 10-120 Hz level modulation),
 * and the PC's time per block (each block's time to $BLOCK_LOG if set:
 * seconds, microseconds, frozen). Writes the headphone output. */
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <xmmintrin.h>
#include <pmmintrin.h>
#include "DSPEngine.h"

using namespace daisy;
using H = chompi::Harmonizer<7>;

/* what chompi_main.cpp defines (there in SDRAM / DTCM) */
daisysp::Reverb reverb;
chompi::InterpolatedDelayLine::AudioSample del_mem[kMaxDelayTime];
float   chompi::shift_mem[kMaxPoly][chompi::kShiftBufFrames * 2];
int16_t chompi::shift_ana[kMaxPoly][chompi::kShiftAnaLen];
H       harmonizer;
float   chompi::chorus_mem[2][chompi::kChorusLen];
float   chompi::vocoder_hist[chompi::Vocoder::kHistFrames][chompi::Vocoder::kBands];
float   chompi::voice_audio[chompi::VoiceFreeze::kLen];
float   chompi::voice_pitch[chompi::VoiceFreeze::kFrames];

static Engine engine;
static const float kSr = 48000.f;
static const int   kBlock = 48;

static bool ReadInput(const std::string &path, std::vector<float> &out)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    if (path.size() > 4 && path.substr(path.size() - 4) == ".f32") // a Freeze Dump
    {
        float v;
        while (fread(&v, 4, 1, f) == 1) out.push_back(v);
        fclose(f);
        return !out.empty();
    }
    char id[4];
    uint32_t size, rate = 0;
    uint16_t fmt = 0, ch = 0, bits = 0;
    fseek(f, 12, SEEK_SET);
    while (fread(id, 1, 4, f) == 4 && fread(&size, 4, 1, f) == 1)
    {
        if (!memcmp(id, "fmt ", 4))
        {
            fread(&fmt, 2, 1, f); fread(&ch, 2, 1, f); fread(&rate, 4, 1, f);
            fseek(f, 6, SEEK_CUR); fread(&bits, 2, 1, f);
            fseek(f, size - 16, SEEK_CUR);
        }
        else if (!memcmp(id, "data", 4))
        {
            if (fmt != 1 || bits != 16 || rate != 48000 || ch < 1)
            {
                fprintf(stderr, "need 16-bit PCM at 48 kHz\n");
                break;
            }
            std::vector<int16_t> s(size / 2);
            fread(s.data(), 2, s.size(), f);
            for (size_t i = 0; i + ch <= s.size(); i += ch)
            {
                float v = 0.f;
                for (int c = 0; c < ch; c++) v += s[i + c];
                out.push_back(v / ch / 32768.f);
            }
            fclose(f);
            return true;
        }
        else
            fseek(f, size + (size & 1), SEEK_CUR);
    }
    fclose(f);
    return false;
}

static void WriteWav(const std::string &path, const std::vector<float> &l, const std::vector<float> &r)
{
    FILE *f = fopen(path.c_str(), "wb");
    const uint32_t n = uint32_t(l.size()), data = n * 4;
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); w32(36 + data); fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(2); w32(48000); w32(48000 * 4); w16(4); w16(16);
    fwrite("data", 1, 4, f); w32(data);
    /* the headphone output is quiet (kHpGain .2): +12 dB, as headphones would */
    for (uint32_t i = 0; i < n; i++)
    {
        w16(uint16_t(int16_t(lrintf(fmaxf(-1.f, fminf(1.f, l[i] * 4.f)) * 32767.f))));
        w16(uint16_t(int16_t(lrintf(fmaxf(-1.f, fminf(1.f, r[i] * 4.f)) * 32767.f))));
    }
    fclose(f);
}

/** a key's mechanical click as the built-in mic hears it: a few ms of a
 *  decaying, ringing burst */
static float Click(int i, float amp)
{
    if (i < 0 || i >= 240) return 0.f;
    const float t = i / kSr;
    return amp * expf(-t / .0015f) * sinf(2.f * 3.14159265f * 2400.f * t) * (1.f + .5f * sinf(2.f * 3.14159265f * 5300.f * t));
}

struct Steadiness { float flap, am_hz, am; };

static Steadiness Measure(const std::vector<float> &x)
{
    Steadiness s{0, 0, 0};
    if (x.size() < 4800) return s;
    std::vector<float> e4, e1;
    for (size_t i = 0; i + 192 <= x.size(); i += 192)
    {
        double e = 0; for (size_t j = i; j < i + 192; j++) e += x[j] * x[j];
        e4.push_back(sqrtf(float(e / 192)));
    }
    double m = 0, v = 0;
    for (float a : e4) m += a; m /= e4.size();
    for (float a : e4) v += (a - m) * (a - m);
    s.flap = float(sqrt(v / e4.size()) / (m > 1e-9 ? m : 1));
    for (size_t i = 0; i + 48 <= x.size(); i += 48)
    {
        double e = 0; for (size_t j = i; j < i + 48; j++) e += x[j] * x[j];
        e1.push_back(sqrtf(float(e / 48)));
    }
    double mean = 0; for (float a : e1) mean += a; mean /= e1.size();
    for (int hz = 10; hz <= 120; hz++)
    {
        double re = 0, im = 0;
        for (size_t i = 0; i < e1.size(); i++)
        {
            const double ph = 2 * M_PI * hz * i / 1000.0;
            re += (e1[i] - mean) * cos(ph); im += (e1[i] - mean) * sin(ph);
        }
        const float a = float(2 * sqrt(re * re + im * im) / e1.size() / (mean > 1e-9 ? mean : 1));
        if (a > s.am) { s.am = a; s.am_hz = float(hz); }
    }
    return s;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: engine_sim voice.wav|freeze-audio.f32 out.wav [options], see the source\n");
        return 1;
    }
    std::string mode = "human";
    std::vector<float> keys = {0.f};
    float t_freeze = -1.f, click = 0.f, jitter_hz = 0.f, level_db = -37.5f, tail = 3.f, speed = 1.f, noise_db = -999.f;
    int   mainloop = 1;
    bool  stop_at_freeze = false;
    for (int a = 3; a < argc; a++)
    {
        const std::string o = argv[a];
        auto next = [&]() -> const char * { return a + 1 < argc ? argv[++a] : ""; };
        if (o == "--mode") mode = next();
        else if (o == "--keys")
        {
            keys.clear();
            std::string s = next();
            for (size_t p = 0; p <= s.size();)
            {
                size_t q = s.find(',', p);
                if (q == std::string::npos) q = s.size();
                keys.push_back(strtof(s.substr(p, q - p).c_str(), nullptr));
                p = q + 1;
            }
        }
        else if (o == "--freeze") t_freeze = strtof(next(), nullptr);
        else if (o == "--click") click = strtof(next(), nullptr);
        else if (o == "--mainloop") mainloop = atoi(next());
        else if (o == "--level") level_db = strtof(next(), nullptr);
        else if (o == "--tail") tail = strtof(next(), nullptr);
        else if (o == "--stop-at-freeze") stop_at_freeze = true;
        else if (o == "--wheel-jitter") jitter_hz = strtof(next(), nullptr);
        else if (o == "--speed") speed = strtof(next(), nullptr);
        else if (o == "--noise") noise_db = strtof(next(), nullptr);
        else { fprintf(stderr, "unknown option %s\n", o.c_str()); return 1; }
    }

    /* x86 slows down a lot on denormals (what decaying filters make in
       silence); CHOMPI's FPU doesn't, so flush them as the timing would */
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);

    std::vector<float> voice;
    if (!ReadInput(argv[1], voice)) { fprintf(stderr, "can't read %s\n", argv[1]); return 1; }
    if (speed != 1.f) // tape-style: slower is lower (a child's voice in a grown-up's range)
    {
        std::vector<float> v;
        for (float p = 0.f; p + 1.f < voice.size(); p += speed)
        {
            const size_t k = size_t(p);
            v.push_back(voice[k] + (voice[k + 1] - voice[k]) * (p - k));
        }
        voice.swap(v);
    }
    if (noise_db > -200.f) // breath: noise following the voice's level, relative to it
    {
        const float a = powf(10.f, noise_db / 20.f) * sqrtf(3.f);
        float env = 0.f;
        uint32_t seed = 12345;
        for (float &v : voice)
        {
            env += (fabsf(v) * 1.57f - env) * .002f;
            seed = seed * 1664525u + 1013904223u;
            v += a * env * (2.f * (seed >> 8) / 16777216.f - 1.f);
        }
    }
    double e = 0; for (float v : voice) e += v * v;
    const float g = powf(10.f, level_db / 20.f) / sqrtf(float(e / voice.size()) + 1e-12f);
    for (float &v : voice) v *= g;

    /* as chompi_main.cpp and the UI's Init set it up */
    engine.Init(kSr, &reverb, &del_mem[0], MonitorMode::HP);
    engine.SetLatchFollows(true);
    engine.SetVoiceGate(false);
    engine.cpu_meter.Init(kSr, kBlock);
    engine.SetInputSource(InputSource::MIC);
    engine.SetInputMonitor(true);
    engine.SetInputGain(.75f);
    engine.SetMainGain(.84f);
    engine.SetReverb(0.f);
    engine.SetDelayFeedback(0.f);
    engine.SetFilter(.5f);
    engine.SetFinalComp(0.f);
    engine.SetFilterResonance(0.f);
    engine.SetDelayTime(.5f);
    engine.SetWarble(0.f);
    engine.SetTranspose(.5f);
    engine.SetSize(.5f);
    engine.SetCharacter(.5f);
    engine.SetSpread(0.f);
    engine.SetDoubler(0.f);
    engine.SetEnvelope(.4f);
    harmonizer.SetMode(mode == "robot" ? H::ChordMode::Robot : mode == "toy" ? H::ChordMode::Toy : H::ChordMode::Keys);

    const size_t n      = (voice.size() + size_t(tail * kSr)) / kBlock * kBlock;
    const long   fz     = t_freeze >= 0.f ? long(t_freeze * kSr) / kBlock * kBlock : -1;
    const long   rel    = fz >= 0 ? fz + long(.12f * kSr) : -1; // the button's release
    std::vector<float> L(n), R(n), frozen;
    float mic[kBlock], none[kBlock] = {}, o0[kBlock], o1[kBlock], o2[kBlock], o3[kBlock];
    const float *in[4] = {mic, none, none, none};
    float       *out[4] = {o0, o1, o2, o3};
    int key_id = 1;
    double max_us = 0, sum_us = 0;
    bool was_frozen = false;
    FILE *block_log = getenv("BLOCK_LOG") ? fopen(getenv("BLOCK_LOG"), "w") : nullptr;

    for (size_t b = 0; b < n; b += kBlock)
    {
        if (b == kBlock * 20) // keys go down after 20 ms
            for (float k : keys)
                engine.request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, k, key_id++, 127.f));
        if (long(b) == fz)
        {
            engine.ToggleFreeze();
            printf("loop pressed at %.2f s: %s\n", b / kSr, engine.Frozen() ? "frozen" : "NOT frozen");
        }
        /* the big wheel chattering one detent back and forth while frozen */
        if (jitter_hz > 0.f && fz >= 0 && long(b) > fz + long(.5f * kSr))
        {
            const long period = long(kSr / jitter_hz) / kBlock * kBlock;
            const long ph     = (long(b) - fz) % period;
            if (ph == 0) engine.ScrubTime(-1);
            else if (ph == period / 2 / kBlock * kBlock) engine.ScrubTime(1);
        }
        for (int i = 0; i < kBlock; i++)
        {
            const long k = long(b) + i;
            float v = size_t(k) < voice.size() && !(stop_at_freeze && fz >= 0 && k >= fz) ? voice[k] : 0.f;
            if (click > 0.f && fz >= 0)
                v += Click(int(k - fz), click) + Click(int(k - rel), click * .6f);
            mic[i] = v;
        }
        const auto t0 = std::chrono::steady_clock::now();
        engine.Prepare();
        engine.Process(in, out, kBlock);
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        max_us = us > max_us ? us : max_us;
        if (block_log) fprintf(block_log, "%.4f %.2f %d\n", b / kSr, us, int(engine.Frozen()));
        sum_us += us;
        if ((b / kBlock) % mainloop == 0)
            engine.UpdatePitch();
        memcpy(&L[b], o0, sizeof o0);
        memcpy(&R[b], o1, sizeof o1);
        if (fz >= 0 && long(b) >= fz + long(.5f * kSr) && engine.Frozen())
            for (int i = 0; i < kBlock; i++) frozen.push_back(o0[i] + o1[i]);
        was_frozen = engine.Frozen();
    }
    (void)was_frozen;

    if (block_log) fclose(block_log);
    WriteWav(argv[2], L, R);
    printf("%s: %.2f s in, mode %s, %zu key(s), pitch update every %d block(s)\n", argv[1],
           voice.size() / kSr, mode.c_str(), keys.size(), mainloop);
    if (!frozen.empty())
    {
        const Steadiness s = Measure(frozen);
        printf("frozen: flap %.3f, strongest level modulation %.0f Hz %.1f%%%s\n", s.flap, s.am_hz,
               100.f * s.am, s.am > .1f ? "  <-- fly?" : "");
    }
    printf("PC time per block: avg %.1f us, max %.1f us (budget 1000 us)\n", sum_us / (n / kBlock), max_us);
    return 0;
}
