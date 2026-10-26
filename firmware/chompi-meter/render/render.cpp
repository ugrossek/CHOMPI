// Render tool for the meter engines: writes WAV files to listen to on the PC.
//   ./render <out_dir>
// Scenes: presets, knob sweeps, re-strike and damping, isorhythmic patterns.

#include "../dsp/Mallet.h"
#include "../dsp/Grit.h"
#include "../dsp/Breath.h"
// The CHOMPI's own reverb (Rings), so the room sounds as on the hardware.
#define TWOPI_F 6.28318530717958647692f // from daisysp, which fx_engine.h expects
#include "../dsp/reverb.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace meter;

static constexpr float kSR = 48000.f;
static float MidiToHz(float n) { return 440.f * std::pow(2.f, (n - 69.f) / 12.f); }

struct Stereo { std::vector<float> l, r; };

static void WriteWav(const std::string& path, const Stereo& s) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); return; }
    uint32_t n = s.l.size(), data = n * 4, sr = kSR, br = sr * 4;
    uint16_t ch = 2, bits = 16, align = 4, fmt = 1;
    uint32_t riff = 36 + data, fmtlen = 16;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f); std::fwrite(&fmtlen, 4, 1, f);
    std::fwrite(&fmt, 2, 1, f); std::fwrite(&ch, 2, 1, f);
    std::fwrite(&sr, 4, 1, f); std::fwrite(&br, 4, 1, f);
    std::fwrite(&align, 2, 1, f); std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
    float peak = 0.f;
    double sum = 0.0;
    for (uint32_t i = 0; i < n; ++i) {
        peak = std::max(peak, std::max(std::fabs(s.l[i]), std::fabs(s.r[i])));
        sum += s.l[i] * s.l[i] + s.r[i] * s.r[i];
    }
    for (uint32_t i = 0; i < n; ++i) {
        // Gentle soft clip, as an output stage would.
        int16_t v[2] = {int16_t(std::tanh(s.l[i]) * 32000.f), int16_t(std::tanh(s.r[i]) * 32000.f)};
        std::fwrite(v, 2, 2, f);
    }
    std::fclose(f);
    std::printf("%-46s %5.1f s  peak %.2f  rms %5.1f dB%s\n", path.c_str(), n / kSR, peak,
                10.0 * std::log10(sum / (2.0 * n) + 1e-12), peak > 1.f ? "  (clipping)" : "");
}

// One channel: a voice per key (re-strike rings on), any engine.
struct Channel {
    float pan = 0.5f, level = 1.f;
    virtual ~Channel() = default;
    virtual void Strike(int note) = 0;
    virtual void Damp(int note) = 0;
    virtual void Update() = 0;
    virtual float Mono() = 0;
    void Process(float& l, float& r) {
        float x = Mono() * level;
        l += x * std::cos(pan * float(M_PI) / 2);
        r += x * std::sin(pan * float(M_PI) / 2);
    }
};

template <class Voice, class Params>
struct EngineChannel : Channel {
    Params p;
    struct Slot { int note = -1; Voice v; };
    std::vector<Slot> slots;
    explicit EngineChannel(Params params, int voices = 8) : p(params), slots(voices) {
        uint32_t seed = 1;
        for (auto& s : slots) s.v.Init(kSR, seed++);
    }
    Voice& VoiceFor(int note) {
        for (auto& s : slots) if (s.note == note) return s.v;
        for (auto& s : slots) if (!s.v.Active()) { s.note = note; return s.v; }
        slots[0].note = note; // simple steal for the render tool
        return slots[0].v;
    }
    void Strike(int note) override { VoiceFor(note).Strike(MidiToHz(note), p); }
    void Damp(int note) override { for (auto& s : slots) if (s.note == note) s.v.Damp(); }
    void Update() override { for (auto& s : slots) if (s.v.Active()) s.v.SetParams(p); }
    float Mono() override { float x = 0.f; for (auto& s : slots) x += s.v.Process(); return x; }
};
using MalletChannel = EngineChannel<MalletVoice, MalletParams>;
using GritChannel = EngineChannel<GritVoice, GritParams>;
using BreathChannel = EngineChannel<BreathVoice, BreathParams>;

// Small Schroeder reverb, standing in for the CHOMPI's reverb.
struct Reverb {
    struct Comb { std::vector<float> b; size_t i = 0; float fb, lp = 0.f;
        float Do(float x) { float y = b[i]; lp = y * 0.7f + lp * 0.3f; b[i] = x + lp * fb; i = (i + 1) % b.size(); return y; } };
    struct AP { std::vector<float> b; size_t i = 0;
        float Do(float x) { float y = b[i]; float o = -x + y; b[i] = x + y * 0.5f; i = (i + 1) % b.size(); return o; } };
    Comb c[2][4]; AP a[2][2];
    Reverb() {
        const int cl[4] = {1557, 1617, 1491, 1422}, al[2] = {556, 441};
        for (int s = 0; s < 2; ++s) {
            for (int k = 0; k < 4; ++k) { c[s][k].b.assign(cl[k] + s * 23, 0.f); c[s][k].fb = 0.84f; }
            for (int k = 0; k < 2; ++k) a[s][k].b.assign(al[k] + s * 23, 0.f);
        }
    }
    void Apply(Stereo& io, float wet) {
        for (size_t n = 0; n < io.l.size(); ++n) {
            float in = (io.l[n] + io.r[n]) * 0.15f, out[2];
            for (int s = 0; s < 2; ++s) {
                float y = 0.f;
                for (auto& k : c[s]) y += k.Do(in);
                for (auto& k : a[s]) y = k.Do(y);
                out[s] = y;
            }
            io.l[n] += wet * out[0];
            io.r[n] += wet * out[1];
        }
    }
};

struct Event { double t; std::function<void()> fn; };

static Stereo Run(std::vector<Channel*> chans, std::vector<Event> ev, double seconds,
                  std::function<void(double)> every_ms = nullptr) {
    std::stable_sort(ev.begin(), ev.end(), [](const Event& a, const Event& b) { return a.t < b.t; });
    Stereo out;
    size_t n = size_t(seconds * kSR), e = 0;
    out.l.resize(n); out.r.resize(n);
    for (size_t i = 0; i < n; ++i) {
        double t = i / kSR;
        while (e < ev.size() && ev[e].t <= t) ev[e++].fn();
        if (every_ms && i % 48 == 0) every_ms(t);
        float l = 0.f, r = 0.f;
        for (auto* c : chans) c->Process(l, r);
        out.l[i] = l; out.r[i] = r;
    }
    return out;
}

static std::vector<Event> Phrase(Channel& c) {
    std::vector<Event> ev;
    const int phrase[] = {60, 64, 67, 72, 67, 64, 60, 55};
    for (int i = 0; i < 8; ++i) ev.push_back({0.1 + i * 0.3, [&c, n = phrase[i]] { c.Strike(n); }});
    for (int i = 0; i < 3; ++i) ev.push_back({2.8 + i * 0.02, [&c, n = 48 + i * 7] { c.Strike(n); }});
    return ev;
}

//                       material brightness decay strike variation
static const MalletParams kWood    {0.000f, 0.50f, 0.32f, 0.60f, 0.3f};
static const MalletParams kMarimba {0.167f, 0.56f, 0.45f, 0.50f, 0.3f};
static const MalletParams kVibes   {0.333f, 0.50f, 0.75f, 0.50f, 0.3f};
static const MalletParams kGlass   {0.667f, 0.60f, 0.65f, 0.75f, 0.3f};
static const MalletParams kBell    {0.833f, 0.50f, 0.80f, 0.60f, 0.3f};
static const MalletParams kFelt    {0.180f, 0.30f, 0.75f, 0.05f, 0.35f};
// Material is spread over 7 anchors: wood 0, marimba 1/6, vibraphone 2/6,
// bronze 3/6, glass 4/6, bell 5/6, gong 1 (the same values as the playground).
//                       timbre brightness decay crush variation
static const GritParams kBleep {0.15f, 0.35f, 0.30f, 0.00f, 0.3f};
static const GritParams kClang {0.55f, 0.65f, 0.45f, 0.25f, 0.3f};
static const GritParams kDust  {0.85f, 0.75f, 0.35f, 0.70f, 0.3f};

template <class Ch, class P>
static void Sweep(const std::string& path, P base, float P::*knob, double len, double strikes_until) {
    Ch c(base);
    std::vector<Event> ev;
    for (double t = 0.1; t < strikes_until; t += 0.3) ev.push_back({t, [&c] { c.Strike(60); }});
    WriteWav(path, Run({&c}, ev, len, [&c, knob, strikes_until](double t) {
        c.p.*knob = float(std::min(t / strikes_until, 1.0)); c.Update(); }));
}

// Isorhythmic pattern: channels latched on L0 = 2 s at their ratios.
struct Latch { Channel* ch; double ratio, phase; int note; double from; };
static Stereo Pattern(std::vector<Channel*> chans, const std::vector<Latch>& latches, double len) {
    const double L0 = 2.0;
    std::vector<Event> ev;
    for (const auto& l : latches) {
        double L = L0 * l.ratio;
        for (double t = l.from + l.phase * L; t < len - 4.0; t += L)
            ev.push_back({t, [ch = l.ch, n = l.note] { ch->Strike(n); }});
    }
    return Run(chans, ev, len);
}


// The CHOMPI's reverb with the volume knob's reverb page at `amt` (as DSPEngine.h maps it).
static void Room(Stereo& io, float amt, float time = 0.9f) {
    static daisysp::Reverb rv;
    rv.Init(kSR);
    rv.Clear();
    rv.SetInputGain(.3f);
    rv.SetAmount(amt * amt * .8f);
    rv.SetTime(time);
    rv.SetLowpass(amt * .6f + .4f);
    rv.SetDiffusion(amt * .6f);
    for (size_t n = 0; n < io.l.size(); ++n) rv.Process(&io.l[n], &io.r[n]);
}

static void Out(const std::string& path, Stereo s, float room = 0.f) {
    if (room > 0.f) Room(s, room);
    WriteWav(path, s);
}

// Pelog-like five notes (E F G B C) for the gamelan set.
static const int kPelog[] = {52, 53, 55, 59, 60, 64, 65, 67, 71, 72};

int main(int argc, char** argv) {
    std::string d = (argc > 1 ? argv[1] : std::string(".")) + "/";
    const float kRoom = 0.6f;

    // --- Gamelan set ---------------------------------------------------------
    MalletParams bronze {0.5f, 0.5f, 0.72f, 0.65f, 0.35f, 0.7f, 0.f};
    MalletParams gong   {1.0f, 0.45f, 0.92f, 0.25f, 0.2f, 0.4f, 0.f};
    {
        MalletChannel c(bronze);
        std::vector<Event> ev;
        const int ph[] = {64, 65, 67, 71, 72, 71, 67, 65, 64, 60};
        for (int i = 0; i < 10; ++i) ev.push_back({0.1 + i * 0.35, [&c, n = ph[i]] { c.Strike(n); }});
        Out(d + "r3_g_metallophone.wav", Run({&c}, ev, 7.0), kRoom);
    }
    {
        MalletParams b = bronze; b.shimmer = 0.f;
        MalletChannel c(b);
        std::vector<Event> ev;
        const int ph[] = {64, 65, 67, 71, 72, 71, 67, 65, 64, 60};
        for (int i = 0; i < 10; ++i) ev.push_back({0.1 + i * 0.35, [&c, n = ph[i]] { c.Strike(n); }});
        Out(d + "r3_g_metallophone_noshimmer.wav", Run({&c}, ev, 7.0), kRoom);
    }
    {
        MalletChannel c(gong);
        std::vector<Event> ev = {{0.1, [&c] { c.Strike(40); }}, {5.0, [&c] { c.Strike(43); }},
                                 {10.0, [&c] { c.Strike(40); }}};
        Out(d + "r3_g_gong.wav", Run({&c}, ev, 18.0), kRoom);
    }
    Sweep<MalletChannel>(d + "r3_g_sweep_shimmer.wav", bronze, &MalletParams::shimmer, 12, 11);
    {
        // Colotomic pattern on L0 = 2 s: gong every 8 s (ratio 4), low bronze
        // every 4 s (ratio 2), the melody every 2 s, a high pair every 1 s.
        MalletParams low = bronze; low.decay = 0.8f; low.strike = 0.4f;
        MalletParams high = bronze; high.strike = 0.8f; high.decay = 0.6f;
        MalletChannel g(gong), k(low), m(bronze), h(high);
        g.pan = 0.5f; k.pan = 0.35f; m.pan = 0.6f; m.level = 0.8f; h.pan = 0.7f; h.level = 0.55f;
        std::vector<Latch> l = {
            {&g, 4.0, 0.00, 40, 0},
            {&k, 2.0, 0.50, 52, 0}, {&k, 2.0, 0.25, 55, 8},
            {&m, 1.0, 0.00, 64, 2}, {&m, 1.0, 0.25, 65, 2}, {&m, 1.0, 0.50, 67, 4}, {&m, 1.0, 0.75, 71, 6},
            {&h, 0.5, 0.25, 76, 12}, {&h, 0.5, 0.75, 79, 14},
        };
        Out(d + "r3_g_pattern.wav", Pattern({&g, &k, &m, &h}, l, 36.0), kRoom);
    }

    // --- Music for Airports set ----------------------------------------------
    BreathParams ah {0.45f, 0.25f, 0.55f, 0.6f, 0.35f};
    MalletParams felt {0.18f, 0.3f, 0.75f, 0.05f, 0.35f, 0.f, 0.f};
    {
        BreathChannel c(ah);
        std::vector<Event> ev;
        const int ph[] = {56, 60, 63, 65, 68};
        for (int i = 0; i < 5; ++i) ev.push_back({0.1 + i * 1.6, [&c, n = ph[i]] { c.Strike(n); }});
        Out(d + "r3_a_voices.wav", Run({&c}, ev, 14.0), kRoom);
    }
    {
        BreathChannel c(ah);
        std::vector<Event> ev;
        for (double t = 0.1; t < 11; t += 1.2) ev.push_back({t, [&c] { c.Strike(60); }});
        WriteWav(d + "r3_a_sweep_vowel.wav", [&] {
            Stereo s = Run({&c}, ev, 14, [&c](double t) { c.p.vowel = float(std::min(t / 11.0, 1.0)); c.Update(); });
            Room(s, kRoom); return s; }());
    }
    {
        BreathChannel c(ah);
        std::vector<Event> ev;
        for (double t = 0.1; t < 11; t += 1.2) ev.push_back({t, [&c] { c.Strike(60); }});
        WriteWav(d + "r3_a_sweep_breath.wav", [&] {
            Stereo s = Run({&c}, ev, 14, [&c](double t) { c.p.breath = float(std::min(t / 11.0, 1.0)); c.Update(); });
            Room(s, kRoom); return s; }());
    }
    {
        MalletChannel c(felt);
        Out(d + "r3_a_felt.wav", Run({&c}, Phrase(c), 7.0), kRoom);
    }
    {
        // Loops of unrelated lengths (free ratios), as in "2/1": L0 = 6 s.
        BreathParams oh = ah; oh.vowel = 0.3f;
        BreathChannel v1(ah), v2(oh), v3(ah);
        MalletChannel p(felt);
        v1.pan = 0.3f; v2.pan = 0.7f; v3.pan = 0.5f; v3.level = 0.8f; p.pan = 0.45f; p.level = 0.7f;
        const double L0 = 6.0, len = 60.0;
        struct L { Channel* ch; double L; double t0; int note; };
        const L loops[] = {
            {&v1, L0 * 1.00, 0.0, 56}, {&v2, L0 * 1.31, 1.5, 63}, {&v3, L0 * 1.73, 4.0, 60},
            {&v1, L0 * 1.00, 3.2, 65}, {&p, L0 * 1.19, 2.0, 68}, {&p, L0 * 1.47, 7.0, 75},
        };
        std::vector<Event> ev;
        for (const auto& l : loops)
            for (double t = l.t0; t < len - 8.0; t += l.L)
                ev.push_back({t, [ch = l.ch, n = l.note] { ch->Strike(n); }});
        Out(d + "r3_a_pattern.wav", Run({&v1, &v2, &v3, &p}, ev, len), kRoom);
    }

    // --- Preparations and fixes ----------------------------------------------
    Sweep<MalletChannel>(d + "r3_sweep_buzz.wav", kWood, &MalletParams::buzz, 12, 11);
    Sweep<MalletChannel>(d + "r3_sweep_variation.wav", kMarimba, &MalletParams::variation, 12, 11);
    {
        // Bell at a lower level, so the low chord no longer overdrives.
        MalletChannel c(kBell); c.level = 0.6f;
        Out(d + "r3_preset_bell.wav", Run({&c}, Phrase(c), 6.0));
    }

    // --- Stems of the round 2 pattern: find the "guitar" ----------------------
    {
        MalletParams bass = kMarimba; bass.brightness = 0.45f; bass.strike = 0.35f; bass.decay = 0.6f;
        struct Stem { const char* name; MalletParams p; std::vector<std::pair<double, int>> notes; double ratio; };
        const Stem stems[] = {
            {"marimba", kMarimba, {{0.00, 62}, {0.62, 69}, {0.31, 66}}, 1.0},
            {"bass", bass, {{0.00, 38}, {0.50, 45}}, 2.0},
            {"wood", kWood, {{0.10, 74}}, 0.75},
            {"bell", kBell, {{0.20, 78}}, 1.5},
        };
        for (const auto& st : stems) {
            MalletChannel c(st.p);
            std::vector<Event> ev;
            for (const auto& [ph, n] : st.notes)
                for (double t = ph * 2.0 * st.ratio; t < 12.0; t += 2.0 * st.ratio)
                    ev.push_back({t, [&c, n = n] { c.Strike(n); }});
            Out(d + "r3_stem_" + st.name + ".wav", Run({&c}, ev, 14.0));
        }
    }
    return 0;
}
