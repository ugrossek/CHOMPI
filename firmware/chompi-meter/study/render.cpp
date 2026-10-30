// CIRCLES · a study in sound. Renders the instrument demos, A/B comparisons
// with today's presets and the ensemble pieces as WAV files, through the
// firmware's own room (dsp/room.h) and circle (dsp/Circle.h), read-only.
//   ./render <out_dir>
// Everything is playable on a CHOMPI: 12-tone keys C3..C5 with the octave
// shift, at most 15 players, loops at CIRCLES' lengths (ratios of L0).

#include "instruments.h"
#include "dsp/Circle.h"
#include "dsp/room.h"
#include "dsp/Mallet.h"
#include "dsp/Presets.h"

#include <chrono>
#include <xmmintrin.h>
#include <pmmintrin.h>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>

using namespace sd;

static constexpr float SR = 48000.f;
static constexpr int BLOCK = 48;
static float Hz(float midi) { return 440.f * std::pow(2.f, (midi - 69.f) / 12.f); }

enum Inst { GANGSA, MBIRA, AMADINDA, SIKU, KORA, VOICES, HANDS, BOWED, BOWL, PIANO, HANG, TAPE, TODAY, kInsts };
static const char* kName[kInsts] = {"gangsa", "mbira", "amadinda", "siku", "kora", "voices", "hands", "bowed", "bowl", "piano", "hang", "tape", "today"};

// Each instrument's own setting: where its knobs sit when it is chosen.
static Knobs Default(Inst i) {
    switch (i) {
        case GANGSA:   return {.70f, .55f, .60f, .75f, .30f, .30f};
        case MBIRA:    return {.55f, .50f, .55f, .00f, .30f, .50f};
        case AMADINDA: return {.70f, .50f, .40f, .10f, .35f, .30f};
        case SIKU:     return {.60f, .45f, .30f, .30f, .35f, .15f};
        case KORA:     return {.55f, .55f, .50f, .30f, .30f, .25f};
        case VOICES:   return {.50f, .50f, .25f, .30f, .35f, .40f};
        case HANDS:    return {.50f, .50f, .30f, .30f, .40f, .20f};
        case BOWED:    return {.25f, .45f, .50f, .30f, .30f, .00f};
        case BOWL:     return {.45f, .50f, .60f, .35f, .20f, .00f};
        case PIANO:    return {.50f, .50f, .50f, .10f, .25f, .00f};
        case HANG:     return {.40f, .50f, .55f, .30f, .25f, .00f};
        case TAPE:     return {.30f, .40f, .50f, .30f, .40f, .30f};
        default:       return {};
    }
}

// Today's CIRCLES mallet preset, as a Voice.
struct Today : Voice {
    meter::MalletVoice v;
    int preset = 0;
    float gain = 1.f;
    void Init(float s, uint32_t seed) override { Voice::Init(s, seed); v.Init(s, seed); }
    void Strike(float f, float vel, const Knobs&) override {
        freq = f;
        gain = std::pow(10.f, meter::kMalletPresets[preset].gain_db / 20.f);
        v.Strike(f, meter::kMalletPresets[preset].p, vel);
        active = true; quiet = 0;
    }
    float Tick() override { return v.Process() * gain; }
    bool Busy() const override { return const_cast<meter::MalletVoice&>(v).Active(); }
};

static std::unique_ptr<Voice> Make(Inst i, int today) {
    switch (i) {
        case GANGSA: return std::make_unique<Gangsa>();
        case MBIRA: return std::make_unique<Mbira>();
        case AMADINDA: return std::make_unique<Amadinda>();
        case SIKU: return std::make_unique<Siku>();
        case KORA: return std::make_unique<Kora>();
        case VOICES: return std::make_unique<Voices>();
        case HANDS: return std::make_unique<Hands>();
        case BOWED: return std::make_unique<Bowed>();
        case BOWL: return std::make_unique<Bowl>();
        case PIANO: return std::make_unique<Piano>();
        case HANG: return std::make_unique<Hang>();
        case TAPE: return std::make_unique<Tape>();
        default: { auto t = std::make_unique<Today>(); t->preset = today; return t; }
    }
}

// ---- Loudness: every instrument as loud as the others at its own setting ----
static float g_calib[kInsts + 16];
static float Loudness(Inst i, int today, const Knobs& k) {
    auto v = Make(i, today);
    v->Init(SR, 7);
    float f = Hz(i == HANDS ? 58 : 60);
    v->Strike(f, .8f, k);
    const int n = int(4.f * SR), win = int(.3f * SR), hop = int(.05f * SR);
    std::vector<float> x(n);
    for (int j = 0; j < n; ++j) x[j] = v->Process();
    float best = 0.f;
    for (int s = 0; s + win < n; s += hop) {
        double e = 0;
        for (int j = s; j < s + win; ++j) e += x[j] * x[j];
        best = std::max(best, float(std::sqrt(e / win)));
    }
    return std::max(best, 1e-5f);
}

// ---- A scene: players, their notes, the room --------------------------------
struct Chan { Inst inst; Knobs k; float level = .8f, place = 0.f; int today = 0; std::string who; };
struct Ev { double t; int ch; float midi, vel; Knobs* k = nullptr; };
struct Scene {
    std::string id, title;
    float room = .5f, size = 1.f, l0 = 2.f, len = 10.f;
    std::vector<Chan> ch;
    std::vector<Ev> ev;
    std::vector<std::unique_ptr<Knobs>> kstore;
    int Add(Inst i, float place, float level = .8f, Knobs k = Knobs{-1}, int today = 0, std::string who = "") {
        if (k.strike < 0.f) k = Default(i);
        ch.push_back({i, k, level, place, today, who});
        return int(ch.size()) - 1;
    }
    void Note(double t, int c, float midi, float vel = .8f) { ev.push_back({t, c, midi, vel}); }
    // A knob change for one channel from this time on (applies to the next notes).
    void Set(double t, int c, Knobs k) { kstore.push_back(std::make_unique<Knobs>(k)); ev.push_back({t, c, -1.f, 0.f, kstore.back().get()}); }
    // A CIRCLES loop: the figure (pulse, note, velocity) with `ppl` pulses per
    // L0, repeated every ratio x L0 from L0 number `from` to `to`.
    void Loop(int c, const std::vector<std::array<float, 3>>& fig, float ppl, float ratio, float from, float to, float swing = 0.f) {
        Rng r; r.Seed(c * 97 + 13);
        const Knobs& k = ch[c].k;
        for (int rep = 0;; ++rep) {
            const double start = (from + rep * ratio) * l0;
            if (start >= to * l0 - 1e-6) break;
            for (auto& n : fig) {
                const double pos = n[0] / ppl;
                if (pos >= ratio) continue;
                double t = start + pos * l0;
                if (t >= to * l0) continue;
                const int pi = int(n[0]);
                if (swing > 0.f && (pi & 1)) t += swing * l0 / ppl;
                t += k.feel * .012 * r.B(); // the player is a person
                Note(std::max(0.0, t), c, n[1], n[2] * (1.f - k.feel * .25f * r.U()));
            }
        }
    }
};

struct Stats { float peak = 0, rms = 0, dc = 0, jump = 0; int nan = 0; };

static Stats Render(Scene& sc, std::vector<float>& L, std::vector<float>& R) {
    std::sort(sc.ev.begin(), sc.ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    const int nch = int(sc.ch.size());
    std::vector<std::vector<std::unique_ptr<Voice>>> vs(nch);
    std::vector<meter::Seat> seat(nch);
    std::vector<int> rr(nch, 0);
    for (int c = 0; c < nch; ++c) {
        const int nv = sc.ch[c].inst == HANDS ? 6 : 8;
        for (int v = 0; v < nv; ++v) { vs[c].push_back(Make(sc.ch[c].inst, sc.ch[c].today)); vs[c].back()->Init(SR, 1000 + c * 31 + v); }
        seat[c].Init(SR);
        seat[c].Set(sc.ch[c].place, sc.size);
        seat[c].Snap();
    }
    static float mem[meter::Room::kMemory];
    meter::Room room;
    room.Init(SR, mem);
    room.SetDecay(sc.room);
    const float wet = 2.f * sc.room * .77f;
    const int total = int((sc.len + 0.f) * SR / BLOCK) * BLOCK;
    L.assign(total, 0.f); R.assign(total, 0.f);
    size_t e = 0;
    float bus[BLOCK], l[BLOCK], r[BLOCK], sl[BLOCK], sr[BLOCK];
    OnePole dcl, dcr; // the CHOMPI's output is AC-coupled: no DC, nothing below ~15 Hz
    dcl.Set(15.f, SR); dcr.Set(15.f, SR);
    for (int b0 = 0; b0 < total; b0 += BLOCK) {
        std::fill(l, l + BLOCK, 0.f); std::fill(r, r + BLOCK, 0.f);
        std::fill(sl, sl + BLOCK, 0.f); std::fill(sr, sr + BLOCK, 0.f);
        // Events land at their sample: split the block at each one.
        std::vector<std::vector<float>> chbus(nch, std::vector<float>(BLOCK, 0.f));
        int i = 0;
        while (i < BLOCK) {
            const double now = double(b0 + i) / SR;
            while (e < sc.ev.size() && sc.ev[e].t <= now) {
                Ev& x = sc.ev[e++];
                Chan& C = sc.ch[x.ch];
                if (x.k) { C.k = *x.k; continue; }
                const float f = Hz(x.midi);
                auto& pool = vs[x.ch];
                int pick = -1;
                for (int v = 0; v < int(pool.size()); ++v) if (pool[v]->active && pool[v]->freq == f && C.inst != HANDS && C.inst != VOICES && C.inst != SIKU && C.inst != BOWED && C.inst != TAPE) pick = v;
                if (C.inst == GANGSA) for (auto& v : pool) if (v->active) v->Choke(C.k.prep);
                if (pick < 0) for (int v = 0; v < int(pool.size()); ++v) if (!pool[v]->active) { pick = v; break; }
                if (pick < 0) { pick = rr[x.ch]; rr[x.ch] = (rr[x.ch] + 1) % int(pool.size()); }
                pool[pick]->Strike(f, x.vel, C.k);
            }
            int upto = BLOCK;
            if (e < sc.ev.size()) upto = std::min(BLOCK, std::max(i + 1, int(std::ceil(sc.ev[e].t * SR)) - b0));
            for (int c = 0; c < nch; ++c) {
                const float g = sc.ch[c].level * g_calib[sc.ch[c].inst == TODAY ? kInsts + sc.ch[c].today : sc.ch[c].inst];
                for (auto& v : vs[c]) if (v->active) for (int j = i; j < upto; ++j) chbus[c][j] += v->Process() * g;
            }
            i = upto;
        }
        for (int c = 0; c < nch; ++c) {
            std::copy(chbus[c].begin(), chbus[c].end(), bus);
            seat[c].Process(bus, l, r, sl, sr, BLOCK);
        }
        for (int j = 0; j < BLOCK; ++j) {
            float wl, wr;
            room.Process(sl[j], sr[j], &wl, &wr);
            L[b0 + j] = dcl.Hp(l[j] + wet * wl);
            R[b0 + j] = dcr.Hp(r[j] + wet * wr);
        }
    }
    // Loudness: about -18 dBFS RMS, peaks kept under -1 dBFS by a soft knee.
    double s2 = 0, dc = 0;
    for (int j = 0; j < total; ++j) { s2 += L[j] * L[j] + R[j] * R[j]; dc += L[j] + R[j]; }
    const float rms = float(std::sqrt(s2 / (2.0 * total)));
    const float g = .125f / std::max(rms, 1e-6f);
    Stats st;
    float prevl = 0, prevr = 0;
    for (int j = 0; j < total; ++j) {
        for (float* p : {&L[j], &R[j]}) {
            float x = *p * g;
            const float a = std::fabs(x);
            if (a > .7f) x = std::copysign(.7f + .19f * std::tanh((a - .7f) / .19f), x);
            if (std::isnan(x)) { st.nan++; x = 0.f; }
            *p = x;
            st.peak = std::max(st.peak, std::fabs(x));
        }
        st.jump = std::max(st.jump, std::max(std::fabs(L[j] - prevl), std::fabs(R[j] - prevr)));
        prevl = L[j]; prevr = R[j];
    }
    double s3 = 0;
    for (int j = 0; j < total; ++j) s3 += L[j] * L[j] + R[j] * R[j];
    st.rms = float(std::sqrt(s3 / (2.0 * total)));
    st.dc = float(dc / (2.0 * total));
    return st;
}

static void WriteWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R) {
    FILE* f = std::fopen(path.c_str(), "wb");
    const uint32_t n = L.size(), data = n * 4, srate = SR, br = srate * 4, riff = 36 + data, fl = 16;
    const uint16_t fmt = 1, ch = 2, al = 4, bits = 16;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f); std::fwrite(&fl, 4, 1, f);
    std::fwrite(&fmt, 2, 1, f); std::fwrite(&ch, 2, 1, f); std::fwrite(&srate, 4, 1, f); std::fwrite(&br, 4, 1, f);
    std::fwrite(&al, 2, 1, f); std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
    std::vector<int16_t> buf(2 * n);
    for (uint32_t i = 0; i < n; ++i) {
        buf[2 * i] = int16_t(Clamp(L[i], -1.f, 1.f) * 32767.f);
        buf[2 * i + 1] = int16_t(Clamp(R[i], -1.f, 1.f) * 32767.f);
    }
    std::fwrite(buf.data(), 2, buf.size(), f);
    std::fclose(f);
}

// ---- CPU: each voice against today's mallet voice, on this computer ---------
static double TimeVoice(Inst i, int today, float hold_s) {
    auto v = Make(i, today);
    v->Init(SR, 3);
    const int n = int(SR * .3f);
    double best = 1e9;
    (void)hold_s;
    for (int rep = 0; rep < 40; ++rep) {
        v->Init(SR, 3 + rep);
        v->Strike(Hz(60), .8f, i == TODAY ? Knobs{} : Default(i));
        auto t0 = std::chrono::steady_clock::now();
        volatile float acc = 0;
        for (int j = 0; j < n; ++j) acc = acc + v->Tick();
        auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double>(t1 - t0).count());
    }
    return best;
}

// ---- The music ---------------------------------------------------------------
using Fig = std::vector<std::array<float, 3>>;
static Fig Seq(const std::vector<float>& notes, float vel = .8f, int step = 1, int start = 0) {
    Fig f;
    for (size_t i = 0; i < notes.size(); ++i) if (notes[i] > 0) f.push_back({float(start + i * step), notes[i], vel});
    return f;
}

static std::vector<std::unique_ptr<Scene>> Scenes() {
    std::vector<std::unique_ptr<Scene>> out;
    auto S = [&](std::string id, std::string title, float l0, float len, float room = .5f) {
        out.push_back(std::make_unique<Scene>());
        auto& s = *out.back();
        s.id = id; s.title = title; s.l0 = l0; s.len = len; s.room = room;
        return &s;
    };

    // ===== Solo demos: the first half plain, the second with its prep ========
    {
        auto* s = S("solo-gangsa", "Gangsa", 2.4f, 12.f, .45f);
        Knobs k = Default(GANGSA); k.prep = 0.f;
        int c = s->Add(GANGSA, 0.f, .8f, k);
        std::vector<float> run = {67, 71, 72, 71, 67, 65, 67, 71, 72, 76, 72, 71, 67, 71, 67, 65};
        for (int i = 0; i < 16; ++i) s->Note(.2 + i * .15, c, run[i], .75f + .15f * (i % 4 == 0));
        s->Note(2.8, c, 64, .9f); s->Note(3.6, c, 67, .8f); s->Note(4.4, c, 52, .9f);
        Knobs k2 = Default(GANGSA); k2.prep = .85f;
        s->Set(6.4, c, k2);
        for (int i = 0; i < 16; ++i) s->Note(6.5 + i * .15, c, run[i], .75f + .15f * (i % 4 == 0));
        s->Note(9.1, c, 64, .9f); s->Note(9.9, c, 72, .8f);
    }
    {
        auto* s = S("solo-mbira", "Mbira", 2.6f, 12.f, .4f);
        Knobs k = Default(MBIRA); k.prep = 0.f;
        int c = s->Add(MBIRA, 0.f, .8f, k);
        Fig kush = {{0, 55, .9f}, {1, 67, .7f}, {2, 62, .75f}, {3, 59, .7f}, {4, 67, .8f}, {5, 64, .7f},
                    {6, 57, .9f}, {7, 69, .7f}, {8, 64, .75f}, {9, 59, .7f}, {10, 66, .8f}, {11, 62, .7f}};
        s->Loop(c, kush, 12, 1, 0, 2);
        k.prep = .7f;
        s->Set(2 * 2.6 - .05, c, k);
        s->Loop(c, kush, 12, 1, 2, 4);
    }
    {
        auto* s = S("solo-amadinda", "Amadinda", 2.4f, 11.f, .35f);
        Knobs k = Default(AMADINDA); k.prep = 0.f;
        int c = s->Add(AMADINDA, 0.f, .8f, k);
        std::vector<float> a = {72, 69, 67, 65, 62, 65, 67, 69, 72, 74, 72, 69}, b = {62, 65, 69, 72, 74, 72, 69, 65, 67, 65, 62, 60};
        Fig comp;
        for (int i = 0; i < 12; ++i) { comp.push_back({float(2 * i), a[i], .85f}); comp.push_back({float(2 * i + 1), b[i], .8f}); }
        s->Loop(c, comp, 24, 1, 0, 2);
        k.prep = .75f;
        s->Set(2 * 2.4 - .02, c, k);
        s->Loop(c, comp, 24, 1, 2, 4);
    }
    {
        auto* s = S("solo-siku", "Siku", 2.f, 12.f, .5f);
        Knobs k = Default(SIKU); k.shimmer = 0.f; k.prep = 0.f;
        int c = s->Add(SIKU, 0.f, .8f, k);
        Fig mel = {{0, 76, .85f}, {2, 74, .7f}, {3, 71, .7f}, {4, 69, .8f}, {6, 67, .7f}, {7, 69, .7f},
                   {8, 71, .85f}, {10, 74, .7f}, {11, 71, .7f}, {12, 69, .9f}};
        s->Loop(c, mel, 8, 2, 0, 2);
        Knobs k2 = Default(SIKU); k2.shimmer = .6f; k2.prep = .5f;
        s->Set(4 - .05, c, k2);
        s->Loop(c, mel, 8, 2, 2, 4);
        s->Note(8.2, c, 64, .9f);
    }
    {
        auto* s = S("solo-kora", "Kora", 2.4f, 12.f, .4f);
        Knobs k = Default(KORA); k.prep = 0.f; k.shimmer = 0.f;
        int c = s->Add(KORA, 0.f, .8f, k);
        Fig kb = {{0, 41, .9f}, {1, 65, .6f}, {2, 69, .65f}, {3, 48, .8f}, {4, 72, .6f}, {5, 69, .6f},
                  {6, 41, .85f}, {7, 65, .6f}, {8, 70, .7f}, {9, 48, .8f}, {10, 69, .6f}, {11, 67, .6f}};
        s->Loop(c, kb, 12, 1, 0, 2, .12f);
        Knobs k2 = Default(KORA); k2.prep = .6f; k2.shimmer = .6f;
        s->Set(2 * 2.4 - .05, c, k2);
        s->Loop(c, kb, 12, 1, 2, 4, .12f);
    }
    {
        auto* s = S("solo-voices", "Voices", 2.2f, 12.f, .45f);
        Knobs k = Default(VOICES); k.prep = 0.f; k.shimmer = 0.f;
        int c = s->Add(VOICES, 0.f, .8f, k);
        Fig f = {{0, 48, .9f}, {2, 55, .7f}, {3, 52, .7f}, {4, 50, .85f}, {6, 55, .7f}, {7, 48, .6f}};
        s->Loop(c, f, 8, 1, 0, 2);
        Knobs k2 = Default(VOICES); k2.prep = .9f; k2.shimmer = .7f;
        s->Set(2 * 2.2 - .05, c, k2);
        s->Loop(c, f, 8, 1, 2, 4);
        Knobs k3 = Default(VOICES); k3.strike = .1f; k3.bright = .05f; k3.decay = .55f; k3.shimmer = 1.f; k3.prep = 0.f;
        s->Set(4 * 2.2 - .05, c, k3);
        s->Note(4 * 2.2, c, 64, .8f);
        s->Note(4 * 2.2 + 1.1, c, 67, .7f);
    }
    {
        auto* s = S("solo-hands", "Hands", 2.f, 10.f, .45f);
        Knobs k = Default(HANDS); k.shimmer = 0.f; k.prep = 0.f;
        int c = s->Add(HANDS, 0.f, .8f, k);
        Fig f = {{0, 40, .9f}, {2, 58, .8f}, {3, 40, .6f}, {4, 40, .8f}, {5, 70, .6f}, {6, 58, .85f}, {7, 70, .5f}};
        s->Loop(c, f, 8, 1, 0, 2);
        Knobs k2 = Default(HANDS); k2.shimmer = .9f; k2.prep = .6f;
        s->Set(4 - .05, c, k2);
        s->Loop(c, f, 8, 1, 2, 4);
    }
    {
        auto* s = S("solo-bowed", "Bowed glass", 3.f, 14.f, .55f);
        Knobs k = Default(BOWED);
        int c = s->Add(BOWED, 0.f, .8f, k);
        s->Note(.1, c, 65, .8f);
        s->Note(2.2, c, 72, .7f);
        Knobs k2 = Default(BOWED); k2.prep = 1.f; k2.decay = .4f;
        s->Set(7.f, c, k2);
        s->Note(7.1, c, 65, .8f);
        s->Note(8.3, c, 69, .7f);
    }
    {
        auto* s = S("solo-bowl", "Singing bowl", 3.f, 16.f, .5f);
        int c = s->Add(BOWL, 0.f, .8f);
        s->Note(.1, c, 50, .85f);
        s->Note(3.0, c, 62, .6f);
        Knobs k2 = Default(BOWL); k2.prep = .75f; k2.decay = .5f;
        s->Set(6.5, c, k2);
        s->Note(6.6, c, 50, .8f);
    }
    {
        auto* s = S("solo-piano", "Pulse piano", 2.f, 12.f, .45f);
        int c = s->Add(PIANO, 0.f, .8f);
        Fig pulse = Seq({72, 72, 72, 72, 72, 72, 72, 72}, .7f);
        Fig pulse2 = Seq({84, 84, 84, 84, 84, 84, 84, 84}, .55f);
        s->Loop(c, pulse, 8, 1, 0, 2);
        s->Loop(c, pulse2, 8, 1, 0, 2);
        s->Note(0, c, 48, .7f); s->Note(2, c, 52, .7f);
        Knobs k2 = Default(PIANO); k2.prep = .7f; k2.decay = .55f;
        s->Set(4 - .05, c, k2);
        Fig prep = {{0, 64, .8f}, {1, 67, .7f}, {2, 72, .75f}, {3, 71, .7f}, {4, 67, .8f}, {5, 64, .7f}, {6, 62, .7f}, {7, 60, .75f}};
        s->Loop(c, prep, 8, 1, 2, 5);
        s->Note(4, c, 41, .8f);
    }
    {
        auto* s = S("solo-hang", "Hang", 2.4f, 12.f, .5f);
        int c = s->Add(HANG, 0.f, .8f);
        Fig f = {{0, 50, .85f}, {2, 57, .7f}, {3, 58, .65f}, {4, 60, .7f}, {6, 62, .75f}, {7, 64, .6f}, {8, 65, .7f}, {10, 69, .65f}};
        s->Loop(c, f, 12, 1, 0, 2);
        Knobs k2 = Default(HANG); k2.prep = .9f;
        s->Set(2 * 2.4 - .05, c, k2);
        s->Loop(c, f, 12, 1, 2, 4);
    }
    {
        auto* s = S("solo-tape", "Tape", 4.f, 16.f, .55f);
        Knobs k = Default(TAPE); k.prep = 0.f;
        int c = s->Add(TAPE, 0.f, .8f, k);
        s->Note(.1, c, 56, .7f); s->Note(.8, c, 60, .6f); s->Note(1.6, c, 63, .6f);
        Knobs k2 = Default(TAPE); k2.prep = .9f; k2.feel = .9f;
        s->Set(7.9, c, k2);
        s->Note(8, c, 53, .7f); s->Note(8.7, c, 60, .6f); s->Note(9.4, c, 65, .6f);
    }

    // ===== A/B: today's preset, then the new instrument, same notes ===========
    auto AB = [&](std::string id, std::string title, int today, Inst inst, Fig fig, float ppl, float l0, float octave = 0) {
        auto* s = S(id, title, l0, 4 * l0 + 2.f, .45f);
        int a = s->Add(TODAY, 0.f, .8f, Knobs{}, today);
        int b = s->Add(inst, 0.f, .8f);
        Fig f2 = fig; for (auto& n : f2) n[1] += octave;
        s->Loop(a, f2, ppl, 1, 0, 2);
        s->Loop(b, f2, ppl, 1, 2, 4);
    };
    AB("ab-bronze-gangsa", "Bronze → Gangsa", 3, GANGSA, Seq({67, 71, 72, 71, 67, 65, 67, 71, 72, 76, 72, 71, 67, 71, 67, 65}, .8f), 16, 2.4f);
    AB("ab-marimba-amadinda", "Marimba → Amadinda", 1, AMADINDA, Seq({72, 62, 69, 65, 67, 69, 65, 72, 62, 74, 65, 72, 67, 69, 69, 65, 72, 62, 74, 60, 72, 62, 69, 60}, .8f), 24, 2.4f);
    AB("ab-bell-bowl", "Bell → Singing bowl", 5, BOWL, Fig{{0, 62, .8f}, {6, 69, .7f}, {12, 65, .75f}}, 16, 3.f);
    AB("ab-vibes-bowed", "Vibes → Bowed glass", 2, BOWED, Fig{{0, 65, .8f}, {6, 69, .7f}, {10, 72, .7f}}, 16, 3.f);

    // ===== Ensemble pieces ======================================================
    {   // KOTEKAN: two gangsas interlock (polos, sangsih) over the jegogan's slow
        // core melody, a gong every four L0, the kempli's beat.
        auto* s = S("piece-kotekan", "Kotekan", 2.4f, 12 * 2.4f + 3.f, .45f);
        Knobs kp = Default(GANGSA); kp.shimmer = .7f; kp.prep = .55f;
        Knobs ks = kp; ks.shimmer = .8f;
        int polos = s->Add(GANGSA, .93f, .75f, kp, 0, "polos");
        int sang = s->Add(GANGSA, .07f, .75f, ks, 0, "sangsih");
        Knobs kj = Default(GANGSA); kj.strike = .35f; kj.decay = .8f; kj.shimmer = .6f; kj.prep = 0.f; kj.bright = .4f;
        int jeg = s->Add(GANGSA, .5f, .85f, kj, 0, "jegogan");
        int gong = s->Add(TODAY, .45f, .9f, Knobs{}, 6, "gong");
        Knobs kk = Default(GANGSA); kk.decay = .05f; kk.prep = 1.f; kk.shimmer = 0.f; kk.strike = .9f;
        int kemp = s->Add(GANGSA, 0.f, .5f, kk, 0, "kempli");
        std::vector<float> c = {67, 71, 72, 71, 67, 65, 67, 71, 72, 76, 72, 71, 67, 71, 67, 65,
                                64, 65, 67, 65, 64, 60, 64, 65, 67, 71, 67, 65, 64, 65, 67, 64};
        Fig pf, sf;
        for (int i = 0; i < 32; ++i) (i % 2 ? sf : pf).push_back({float(i), c[i], i % 4 == 0 ? .9f : .75f});
        s->Loop(polos, pf, 16, 2, 2, 11);
        s->Loop(sang, sf, 16, 2, 2, 11);
        s->Loop(jeg, Fig{{0, 55, .9f}, {8, 60, .8f}, {16, 53, .85f}, {24, 52, .9f}}, 16, 2, 0, 12);
        s->Loop(gong, Fig{{0, 40, 1.f}}, 1, 4, 0, 12.5f);
        s->Loop(kemp, Fig{{4, 76, .6f}, {12, 76, .6f}}, 16, 1, 0, 11);
    }
    {   // KUSHAURA: two mbiras, the kutsinhira between the kushaura's notes,
        // hosho shaking, a low hum (huro) that comes and goes.
        auto* s = S("piece-kushaura", "Kushaura", 2.6f, 12 * 2.6f + 3.f, .4f);
        int ku = s->Add(MBIRA, .9f, .8f, Knobs{-1}, 0, "kushaura");
        Knobs kt = Default(MBIRA); kt.prep = .6f; kt.strike = .45f;
        int kt_ = s->Add(MBIRA, .1f, .75f, kt, 0, "kutsinhira");
        Knobs kh = Default(HANDS); kh.shimmer = .2f; kh.decay = .1f; kh.prep = 0.f;
        int hosho = s->Add(HANDS, 0.f, .45f, kh, 0, "hosho");
        Knobs kv = Default(VOICES); kv.strike = .05f; kv.bright = .1f; kv.decay = .6f; kv.shimmer = .6f; kv.prep = .3f;
        int huro = s->Add(VOICES, .5f, .5f, kv, 0, "huro");
        std::vector<float> a = {55, 67, 62, 59, 67, 64, 57, 69, 64, 59, 66, 62, 55, 67, 62, 60, 67, 64, 52, 67, 64, 59, 66, 62};
        Fig kf, tf;
        for (int i = 0; i < 24; ++i) kf.push_back({float(i), a[i], i % 3 == 0 ? .9f : .72f});
        for (int i = 0; i < 24; ++i) tf.push_back({float(i) + .5f, a[(i + 7) % 24] + (i % 2 ? 0.f : 12.f), .7f});
        s->Loop(ku, kf, 12, 2, 0, 12);
        s->Loop(kt_, tf, 12, 2, 2, 12);
        s->Loop(hosho, Fig{{0, 74, .7f}, {1, 74, .4f}, {2, 74, .5f}, {3, 74, .7f}, {4, 74, .4f}, {5, 74, .5f},
                           {6, 74, .7f}, {7, 74, .4f}, {8, 74, .5f}, {9, 74, .7f}, {10, 74, .4f}, {11, 74, .5f}}, 12, 1, 1, 12);
        s->Loop(huro, Fig{{0, 55, .7f}, {12, 52, .7f}}, 12, 2, 4, 12);
    }
    {   // HOCKET: two sikus share one melody, a note each, the bombo below; a
        // second pair an octave up joins.
        auto* s = S("piece-hocket", "Hocket", 2.f, 12 * 2.f + 2.f, .5f);
        Knobs ki = Default(SIKU); ki.shimmer = .45f;
        int ira = s->Add(SIKU, .88f, .75f, ki, 0, "ira");
        int arca = s->Add(SIKU, .12f, .75f, ki, 0, "arca");
        Knobs kc = Default(SIKU); kc.shimmer = 0.f; kc.bright = .55f;
        int chi = s->Add(SIKU, .8f, .45f, kc, 0, "chuli (ira)");
        int cha = s->Add(SIKU, .2f, .45f, kc, 0, "chuli (arca)");
        Knobs kb = Default(HANDS); kb.prep = .6f; kb.shimmer = 0.f; kb.decay = .4f;
        int bombo = s->Add(HANDS, .5f, .75f, kb, 0, "bombo");
        std::vector<float> m = {64, 67, 69, 71, 74, 71, 69, 67, 69, 71, 69, 67, 64, 62, 64, 0};
        Fig fi, fa, ci, ca;
        for (int i = 0; i < 16; ++i) if (m[i] > 0) {
            (i % 2 ? fa : fi).push_back({float(i), m[i], i % 4 == 0 ? .9f : .75f});
            (i % 2 ? ca : ci).push_back({float(i), m[i] + 12, .7f});
        }
        s->Loop(ira, fi, 8, 2, 1, 11);
        s->Loop(arca, fa, 8, 2, 1, 11);
        s->Loop(chi, ci, 8, 2, 5, 11);
        s->Loop(cha, ca, 8, 2, 5, 11);
        s->Loop(bombo, Fig{{0, 40, .9f}, {3, 44, .6f}, {4, 40, .8f}, {6, 44, .6f}}, 8, 1, 0, 11.5f);
        s->Note(11 * 2.f, ira, 64, .9f); s->Note(11 * 2.f, arca, 52, .8f);
    }
    {   // OKUNAGA: amadinda, two players each on every other pulse, the third
        // doubling the lowest two notes an octave down: the inherent pattern.
        auto* s = S("piece-okunaga", "Okunaga", 2.4f, 12 * 2.4f + 2.f, .35f);
        int o1 = s->Add(AMADINDA, .9f, .8f, Knobs{-1}, 0, "okunaga");
        int o2 = s->Add(AMADINDA, .1f, .8f, Knobs{-1}, 0, "okwawula");
        Knobs k3 = Default(AMADINDA); k3.bright = .35f; k3.decay = .5f;
        int o3 = s->Add(AMADINDA, 0.f, .7f, k3, 0, "okukoonera");
        std::vector<float> a = {72, 69, 67, 65, 62, 65, 67, 69, 72, 74, 72, 69}, b = {62, 65, 69, 72, 74, 72, 69, 65, 67, 65, 62, 60};
        Fig f1, f2, f3;
        for (int i = 0; i < 12; ++i) {
            f1.push_back({float(2 * i), a[i], .85f});
            f2.push_back({float(2 * i + 1), b[i], .8f});
            if (a[i] <= 62) f3.push_back({float(2 * i), a[i] - 12, .8f});
            if (b[i] <= 62) f3.push_back({float(2 * i + 1), b[i] - 12, .8f});
        }
        s->Loop(o1, f1, 24, 1, 0, 12);
        s->Loop(o2, f2, 24, 1, 2, 12);
        s->Loop(o3, f3, 24, 1, 6, 12);
    }
    {   // CIRCLE SONG: voices enter one by one, a leader sings over the top.
        auto* s = S("piece-circle-song", "Circle song", 2.2f, 16 * 2.2f + 2.f, .5f);
        Knobs kb = Default(VOICES); kb.strike = .55f; kb.bright = .3f; kb.decay = .1f; kb.prep = .9f; kb.shimmer = .35f;
        int bass = s->Add(VOICES, 0.f, .85f, kb, 0, "bass: dum");
        Knobs km = Default(VOICES); km.strike = .7f; km.bright = .5f; km.decay = .05f; km.prep = .2f; km.shimmer = .3f;
        int mid = s->Add(VOICES, .85f, .7f, km, 0, "middle: ba-da");
        Knobs kh = Default(VOICES); kh.strike = .08f; kh.bright = .05f; kh.decay = .62f; kh.prep = 0.f; kh.shimmer = .9f;
        int high = s->Add(VOICES, .15f, .55f, kh, 0, "high: ooh");
        Knobs kc = Default(HANDS); kc.shimmer = .75f; kc.prep = .35f;
        int claps = s->Add(HANDS, .5f, .32f, kc, 0, "claps");
        Knobs kl = Default(VOICES); kl.strike = .45f; kl.bright = .62f; kl.decay = .3f; kl.prep = .1f; kl.shimmer = 0.f; kl.feel = .5f;
        int lead = s->Add(VOICES, .03f, .75f, kl, 0, "leader");
        s->Loop(bass, Fig{{0, 48, .9f}, {3, 48, .7f}, {5, 43, .8f}, {6, 45, .7f}}, 8, 1, 0, 16);
        s->Loop(claps, Fig{{2, 58, .8f}, {6, 58, .85f}, {7, 62, .4f}}, 8, 1, 2, 16);
        s->Loop(mid, Fig{{2, 55, .8f}, {3, 52, .65f}, {4, 55, .75f}, {6, 57, .8f}, {7, 55, .6f}}, 8, 1, 4, 16);
        s->Loop(high, Fig{{0, 64, .7f}, {8, 62, .65f}}, 8, 2, 6, 16);
        const float L0 = 2.2f;
        Fig solo = {{0, 72, .8f}, {2, 74, .7f}, {3, 76, .8f}, {6, 74, .7f}, {8, 72, .75f}, {11, 69, .7f}, {12, 67, .8f},
                    {16, 76, .85f}, {18, 79, .8f}, {20, 76, .75f}, {21, 74, .7f}, {22, 72, .75f}, {24, 74, .8f}, {28, 72, .8f}};
        for (auto& n : solo) s->Note(8 * L0 + n[0] * L0 / 8.f, lead, n[1], n[2]);
        for (auto& n : solo) s->Note(12 * L0 + n[0] * L0 / 8.f, lead, n[1] + (n[0] > 15 ? -5 : 0), n[2]);
    }
    {   // PULSE: In C's idea, not its notes. The pulse on the piano, figures at
        // CIRCLES' ratios: 1, 3/2, 3/4, and voices and glass at 3.
        auto* s = S("piece-pulse", "Pulse", 2.f, 14 * 2.f + 3.f, .5f);
        Knobs kp = Default(PIANO); kp.strike = .35f; kp.bright = .4f; kp.decay = .2f;
        int pulse = s->Add(PIANO, 0.f, .55f, kp, 0, "pulse ×1");
        int kora = s->Add(KORA, .88f, .7f, Knobs{-1}, 0, "kora ×1");
        int ama = s->Add(AMADINDA, .12f, .7f, Knobs{-1}, 0, "amadinda ×3/4");
        Knobs kv = Default(VOICES); kv.strike = .1f; kv.bright = .15f; kv.decay = .7f; kv.shimmer = .8f; kv.prep = 0.f;
        int ch = s->Add(VOICES, .62f, .55f, kv, 0, "voices ×3");
        int glass = s->Add(BOWED, .38f, .55f, Knobs{-1}, 0, "glass ×3/2");
        s->Loop(pulse, Seq({84, 84, 84, 84, 84, 84, 84, 84}, .6f), 8, 1, 0, 14);
        s->Loop(pulse, Seq({72, 72, 72, 72, 72, 72, 72, 72}, .7f), 8, 1, 0, 14);
        s->Loop(kora, Fig{{0, 64, .8f}, {1, 65, .7f}, {2, 64, .75f}, {4, 64, .8f}, {5, 65, .7f}, {6, 64, .75f}}, 8, 1, 2, 14);
        s->Loop(ama, Fig{{0, 67, .8f}, {1, 64, .7f}, {2, 67, .75f}, {4, 71, .8f}}, 8, .75f, 4, 14);
        s->Loop(ch, Fig{{0, 64, .7f}, {8, 67, .65f}, {16, 60, .7f}}, 8, 3, 6, 14);
        s->Loop(glass, Fig{{0, 79, .6f}}, 8, 1.5f, 8, 14);
    }
    {   // AIRPORT: long loops that hardly ever meet. Bowed glass ×3, tape ×2,
        // voices ×5/4, a bowl struck and rubbed ×3, a felt piano ×4/3.
        auto* s = S("piece-airport", "Long loops", 4.f, 52.f, .65f);
        int glass = s->Add(BOWED, .9f, .7f, Knobs{-1}, 0, "glass ×3");
        int tape = s->Add(TAPE, .12f, .75f, Knobs{-1}, 0, "tape ×2");
        Knobs kv = Default(VOICES); kv.strike = .05f; kv.bright = .45f; kv.decay = .75f; kv.shimmer = 1.f; kv.prep = 0.f;
        int vo = s->Add(VOICES, .5f, .5f, kv, 0, "voices ×5/4");
        Knobs kb = Default(BOWL); kb.prep = .6f; kb.decay = .7f;
        int bowl = s->Add(BOWL, .4f, .32f, kb, 0, "bowl ×3");
        Knobs kp = Default(PIANO); kp.strike = .08f; kp.bright = .25f; kp.decay = .7f;
        int pno = s->Add(PIANO, .05f, .6f, kp, 0, "felt piano ×4/3");
        s->Loop(glass, Fig{{0, 65, .7f}, {5, 72, .6f}}, 1, 3, 0, 13);
        s->Loop(tape, Fig{{0, 56, .7f}, {.5f, 63, .6f}}, 1, 2, .5f, 13);
        s->Loop(vo, Fig{{0, 60, .6f}}, 1, 1.25f, 2, 13);
        s->Loop(bowl, Fig{{0, 41, .7f}}, 1, 3, 1, 13);
        s->Loop(pno, Fig{{0, 68, .6f}, {.5f, 72, .5f}}, 1, 4.f / 3.f, 3, 13);
    }
    {   // KUMBENGO: the kora's two hands, the thumbs' bass and the fingers'
        // treble, a hum underneath, the nyenyemo jingling.
        auto* s = S("piece-kumbengo", "Kumbengo", 2.4f, 12 * 2.4f + 3.f, .4f);
        int bass = s->Add(KORA, .97f, .8f, Knobs{-1}, 0, "kora: thumbs");
        Knobs kt = Default(KORA); kt.strike = .45f; kt.bright = .6f;
        int treb = s->Add(KORA, .03f, .75f, kt, 0, "kora: fingers");
        Knobs kv = Default(VOICES); kv.strike = .05f; kv.bright = .2f; kv.decay = .45f; kv.prep = .7f; kv.shimmer = .3f;
        int hum = s->Add(VOICES, .5f, .45f, kv, 0, "hum");
        Knobs kh = Default(HANDS); kh.shimmer = .1f; kh.decay = .1f;
        int snap = s->Add(HANDS, .7f, .35f, kh, 0, "snaps");
        s->Loop(bass, Fig{{0, 41, .9f}, {3, 48, .75f}, {6, 41, .85f}, {9, 46, .75f}, {12, 41, .9f}, {15, 48, .75f}, {18, 43, .85f}, {21, 48, .75f}}, 12, 2, 0, 12, .1f);
        s->Loop(treb, Fig{{1, 65, .7f}, {2, 69, .65f}, {4, 72, .7f}, {5, 69, .6f}, {7, 70, .7f}, {8, 69, .6f}, {10, 67, .65f}, {11, 65, .6f},
                          {13, 65, .7f}, {14, 69, .65f}, {16, 72, .7f}, {17, 74, .65f}, {19, 72, .7f}, {20, 70, .6f}, {22, 69, .65f}, {23, 67, .6f}}, 12, 2, 2, 12, .1f);
        s->Loop(hum, Fig{{0, 57, .7f}, {12, 55, .65f}}, 12, 2, 6, 12);
        s->Loop(snap, Fig{{3, 72, .6f}, {9, 72, .6f}}, 12, 1, 4, 12);
    }
    return out;
}

int main(int argc, char** argv) {
    // Denormals flushed to zero, as the CHOMPI's FPU does.
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    const std::string dir = argc > 1 ? argv[1] : "out";
    // Loudness calibration: each at its own setting.
    for (int i = 0; i < kInsts - 1; ++i) g_calib[i] = .12f / Loudness(Inst(i), 0, Default(Inst(i)));
    for (int p = 0; p < meter::kNumClassicPresets; ++p) g_calib[kInsts + p] = .12f / Loudness(TODAY, p, Knobs{});
    // Today's presets are already level-matched among themselves: one gain for all.
    float tg = 0.f;
    for (int p = 0; p < meter::kNumClassicPresets; ++p) tg += g_calib[kInsts + p];
    tg /= meter::kNumClassicPresets;
    for (int p = 0; p < meter::kNumClassicPresets; ++p) g_calib[kInsts + p] = tg;
    // CPU, against today's mallet voice (marimba preset).
    FILE* cpu = std::fopen((dir + "/cpu.json").c_str(), "w");
    const double base = TimeVoice(TODAY, 1, 0.f);
    std::fprintf(cpu, "{");
    for (int i = 0; i < kInsts - 1; ++i) {
        const double t = TimeVoice(Inst(i), 0, .5f);
        std::fprintf(cpu, "%s\"%s\": %.2f", i ? ", " : "", kName[i], t / base);
        std::printf("cpu %-9s %.2f x mallet\n", kName[i], t / base);
    }
    std::fprintf(cpu, "}\n");
    std::fclose(cpu);
    FILE* man = std::fopen((dir + "/manifest.json").c_str(), "w");
    std::fprintf(man, "[\n");
    auto scenes = Scenes();
    bool first = true;
    for (auto& sc : scenes) {
        std::vector<float> L, R;
        const Stats st = Render(*sc, L, R);
        WriteWav(dir + "/" + sc->id + ".wav", L, R);
        std::printf("%-22s %5.1fs peak %.2f rms %.3f dc %+.4f jump %.2f nan %d\n", sc->id.c_str(), sc->len, st.peak, st.rms, st.dc, st.jump, st.nan);
        std::fprintf(man, "%s  {\"id\": \"%s\", \"title\": \"%s\", \"len\": %.1f, \"l0\": %.2f, \"players\": [", first ? "" : ",\n", sc->id.c_str(), sc->title.c_str(), sc->len, sc->l0);
        for (size_t c = 0; c < sc->ch.size(); ++c)
            std::fprintf(man, "%s{\"inst\": \"%s\", \"who\": \"%s\", \"place\": %.2f}", c ? ", " : "", kName[sc->ch[c].inst], sc->ch[c].who.c_str(), sc->ch[c].place);
        std::fprintf(man, "]}");
        first = false;
    }
    std::fprintf(man, "\n]\n");
    std::fclose(man);
}
