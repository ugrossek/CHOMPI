// CIRCLES · the second set (synths.h): renders a solo for every sound (its own
// setting, then one knob of the CHOMPI layer turned) and a few pieces, through
// the firmware's own pan (dsp/Pan.h) and room (dsp/room.h), as WAV files; and
// what each sound costs against today's mallet voice, on this computer.
//   g++ -std=gnu++17 -O2 -I.. synth_render.cpp -o synth_render && ./synth_render <out_dir>
// Everything is playable on a CHOMPI: keys C3..C5 with the octave shift, loops
// at CIRCLES' lengths (ratios of L0).

#include "synths.h"
#include "dsp/Pan.h"
#include "dsp/room.h"
#include "dsp/Mallet.h"
#include "dsp/Presets.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <xmmintrin.h>
#include <pmmintrin.h>

using namespace sd;

static constexpr float SR = 48000.f;
static constexpr int BLOCK = 48;
static float Hz(float midi) { return 440.f * std::pow(2.f, (midi - 69.f) / 12.f); }

// ---- The sounds --------------------------------------------------------------
enum Snd { KICK, TOM, SNARE, CLAP, HAT, COWBELL, BLIP, ACID, FMPIANO, FMBELL, CHIPLEAD, STAB, GRIT, FMBASS, kSounds,
           MARIMBA = kSounds, GANGSA, kAll };
struct Info { const char* id; const char* name; Knobs k; };
//                                   strike bright decay shimmer feel prep
static const Info kInfo[kAll] = {
    {"kick", "Kick", {.55f, .30f, .35f, .00f, .20f, .15f}},
    {"tom", "Tom", {.45f, .20f, .35f, .45f, .25f, .10f}},
    {"snare", "Snare", {.60f, .45f, .25f, .10f, .30f, .00f}},
    {"clap", "Clap", {.30f, .35f, .35f, .10f, .35f, .85f}},
    {"hat", "Hi-hat", {.50f, .85f, .08f, 1.0f, .30f, .00f}},
    {"cowbell", "Cowbell", {.30f, .35f, .25f, .00f, .25f, .00f}},
    {"blip", "Blip", {.60f, .50f, .12f, .00f, .20f, .00f}},
    {"acid", "Acid", {.70f, .25f, .30f, .00f, .30f, .75f}},
    {"fmpiano", "FM piano", {.55f, .25f, .50f, .40f, .30f, .20f}},
    {"fmbell", "FM bell", {.60f, .30f, .75f, .30f, .25f, .55f}},
    {"chiplead", "Chip lead", {.15f, .45f, .65f, .60f, .25f, .00f}},
    {"stab", "Saw stab", {.40f, .55f, .35f, .80f, .30f, .20f}},
    {"grit", "Grit", {.35f, .55f, .50f, .40f, .30f, .30f}},
    {"fmbass", "FM bass", {.50f, .35f, .30f, .00f, .25f, .05f}},
    {"marimba", "Marimba (set 1)", {}},
    {"gangsa", "Gangsa (set 1)", {.70f, .55f, .60f, .75f, .30f, .30f}},
};

// Today's mallet preset, as a Voice (the marimba of set 1).
struct Today : Voice {
    meter::MalletVoice v;
    int preset = 1;
    void Init(float s, uint32_t seed) override { Voice::Init(s, seed); v.Init(s, seed); }
    void Strike(float f, float vel, const Knobs&) override {
        freq = f;
        v.Strike(f, meter::kMalletPresets[preset].p, vel);
        active = true; quiet = 0;
    }
    float Tick() override { return v.Process(); }
    bool Busy() const override { return const_cast<meter::MalletVoice&>(v).Active(); }
};

static std::unique_ptr<Voice> Make(Snd s) {
    switch (s) {
        case KICK: return std::make_unique<Kick>();
        case TOM: return std::make_unique<Tom>();
        case SNARE: case CLAP: return std::make_unique<Noise>();
        case HAT: case COWBELL: return std::make_unique<Metal>();
        case BLIP: case CHIPLEAD: return std::make_unique<Chip>();
        case ACID: case STAB: return std::make_unique<Acid>();
        case FMPIANO: case FMBELL: case FMBASS: return std::make_unique<Fm>();
        case GRIT: return std::make_unique<Grit>();
        case GANGSA: return std::make_unique<Gangsa>();
        default: return std::make_unique<Today>();
    }
}

// ---- Loudness: every sound as loud as the others at its own setting --------
static float g_calib[kAll];
static float Loudness(Snd s) {
    auto v = Make(s);
    v->Init(SR, 7);
    v->Strike(Hz(60), .8f, kInfo[s].k);
    const int n = int(3.f * SR), win = int(.3f * SR), hop = int(.02f * SR);
    std::vector<float> x(n);
    for (int j = 0; j < n; ++j) x[j] = v->Process();
    float best = 0.f;
    for (int a = 0; a + win < n; a += hop) {
        double e = 0;
        for (int j = a; j < a + win; ++j) e += x[j] * x[j];
        best = std::max(best, float(std::sqrt(e / win)));
    }
    return std::max(best, 1e-5f);
}

// ---- A scene: players, their notes, the room --------------------------------
struct Chan { Snd s; Knobs k; float level = .8f, pan = 0.f; std::string who; };
struct Ev { double t; int ch; float midi, vel; int k = -1; };
using Fig = std::vector<std::array<float, 3>>; // pulse, note, velocity
struct Scene {
    std::string id, title, note;
    float room = .4f, l0 = 2.f, len = 10.f;
    std::vector<Chan> ch;
    std::vector<Ev> ev;
    std::vector<Knobs> ks;
    int Add(Snd s, float pan, float level = .8f, std::string who = "") { ch.push_back({s, kInfo[s].k, level, pan, who}); return int(ch.size()) - 1; }
    void Note(double t, int c, float midi, float vel = .8f) { ev.push_back({t, c, midi, vel}); }
    void Set(double t, int c, Knobs k) { ks.push_back(k); ev.push_back({t, c, -1.f, 0.f, int(ks.size()) - 1}); }
    // A CIRCLES loop: the figure with `ppl` pulses per L0, every ratio x L0,
    // from L0 number `from` to `to`; a person plays it (the feel).
    void Loop(int c, const Fig& fig, float ppl, float ratio, float from, float to) {
        Rng r; r.Seed(c * 97 + 13);
        for (int rep = 0;; ++rep) {
            const double start = (from + rep * ratio) * l0;
            if (start >= to * l0 - 1e-6) break;
            for (auto& n : fig) {
                const double pos = n[0] / ppl;
                if (pos >= ratio) continue;
                double t = start + pos * l0 + ch[c].k.feel * .01 * r.B();
                if (t >= to * l0) continue;
                Note(std::max(0.0, t), c, n[1], n[2] * (1.f - ch[c].k.feel * .2f * r.U()));
            }
        }
    }
};

struct Stats { float peak = 0, rms = 0; int nan = 0; };
static Stats Render(Scene& sc, std::vector<float>& L, std::vector<float>& R) {
    std::stable_sort(sc.ev.begin(), sc.ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    const int nch = int(sc.ch.size());
    std::vector<std::vector<std::unique_ptr<Voice>>> vs(nch);
    std::vector<meter::Pan> pan(nch);
    std::vector<int> rr(nch, 0);
    for (int c = 0; c < nch; ++c) {
        for (int v = 0; v < 6; ++v) { vs[c].push_back(Make(sc.ch[c].s)); vs[c].back()->Init(SR, 1000 + c * 31 + v); }
        pan[c].Set(sc.ch[c].pan, 1.f);
        pan[c].Snap();
    }
    static float mem[meter::Room::kMemory];
    meter::Room room;
    room.Init(SR, mem);
    room.SetDecay(sc.room);
    const float wet = 2.f * sc.room * .77f;
    const int total = int(sc.len * SR / BLOCK) * BLOCK;
    L.assign(total, 0.f); R.assign(total, 0.f);
    size_t e = 0;
    float l[BLOCK], r[BLOCK], sl[BLOCK], sr[BLOCK];
    OnePole dcl, dcr;
    dcl.Set(15.f, SR); dcr.Set(15.f, SR);
    std::vector<std::vector<float>> bus(nch, std::vector<float>(BLOCK));
    for (int b0 = 0; b0 < total; b0 += BLOCK) {
        std::fill(l, l + BLOCK, 0.f); std::fill(r, r + BLOCK, 0.f);
        std::fill(sl, sl + BLOCK, 0.f); std::fill(sr, sr + BLOCK, 0.f);
        for (auto& b : bus) std::fill(b.begin(), b.end(), 0.f);
        int i = 0;
        while (i < BLOCK) {
            const double now = double(b0 + i) / SR;
            while (e < sc.ev.size() && sc.ev[e].t <= now) {
                Ev& x = sc.ev[e++];
                Chan& C = sc.ch[x.ch];
                if (x.k >= 0) { C.k = sc.ks[x.k]; continue; }
                const float f = Hz(x.midi);
                auto& pool = vs[x.ch];
                // As the firmware does: the same note again on the voice that rings it.
                int pick = -1;
                for (int v = 0; v < int(pool.size()); ++v) if (pool[v]->active && pool[v]->freq == f) pick = v;
                if (pick < 0) for (int v = 0; v < int(pool.size()); ++v) if (!pool[v]->active) { pick = v; break; }
                if (pick < 0) { pick = rr[x.ch]; rr[x.ch] = (rr[x.ch] + 1) % int(pool.size()); }
                pool[pick]->Strike(f, x.vel, C.k);
            }
            int upto = BLOCK;
            if (e < sc.ev.size()) upto = std::min(BLOCK, std::max(i + 1, int(std::ceil(sc.ev[e].t * SR)) - b0));
            for (int c = 0; c < nch; ++c) {
                const float g = sc.ch[c].level * g_calib[sc.ch[c].s];
                for (auto& v : vs[c]) if (v->active) for (int j = i; j < upto; ++j) bus[c][j] += v->Process() * g;
            }
            i = upto;
        }
        for (int c = 0; c < nch; ++c) pan[c].Process(bus[c].data(), l, r, sl, sr, BLOCK);
        for (int j = 0; j < BLOCK; ++j) {
            float wl, wr;
            room.Process(sl[j], sr[j], &wl, &wr);
            L[b0 + j] = dcl.Hp(l[j] + wet * wl);
            R[b0 + j] = dcr.Hp(r[j] + wet * wr);
        }
    }
    // About -18 dBFS RMS, peaks kept under -1 dBFS by a soft knee.
    double s2 = 0;
    for (int j = 0; j < total; ++j) s2 += L[j] * L[j] + R[j] * R[j];
    const float g = .125f / std::max(float(std::sqrt(s2 / (2.0 * total))), 1e-6f);
    Stats st;
    for (int j = 0; j < total; ++j)
        for (float* p : {&L[j], &R[j]}) {
            float x = *p * g;
            const float a = std::fabs(x);
            if (a > .7f) x = std::copysign(.7f + .19f * std::tanh((a - .7f) / .19f), x);
            if (std::isnan(x)) { st.nan++; x = 0.f; }
            *p = x;
            st.peak = std::max(st.peak, std::fabs(x));
        }
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

// ---- CPU: each sound against today's mallet voice, on this computer ---------
static double TimeVoice(Snd s) {
    double best = 1e9;
    for (int rep = 0; rep < 40; ++rep) {
        auto v = Make(s);
        v->Init(SR, 3 + rep);
        v->Strike(Hz(60), .8f, kInfo[s].k);
        const int n = int(SR * .3f);
        auto t0 = std::chrono::steady_clock::now();
        volatile float acc = 0;
        for (int j = 0; j < n; ++j) acc = acc + v->Tick();
        auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double>(t1 - t0).count());
    }
    return best;
}

// ---- The music ---------------------------------------------------------------
static std::vector<std::unique_ptr<Scene>> Scenes() {
    std::vector<std::unique_ptr<Scene>> out;
    auto S = [&](std::string id, std::string title, float l0, float len, float room, std::string note = "") {
        out.push_back(std::make_unique<Scene>());
        auto& s = *out.back();
        s.id = id; s.title = title; s.l0 = l0; s.len = len; s.room = room; s.note = note;
        return &s;
    };
    // A solo: two L0 at its own setting, two with one knob turned.
    auto Solo = [&](Snd snd, float l0, const Fig& fig, float ppl, float ratio, Knobs k2, std::string what) {
        auto* s = S(std::string("solo-") + kInfo[snd].id, kInfo[snd].name, l0, 4.f * l0 + 1.2f, .3f, what);
        const int c = s->Add(snd, 0.f, .8f);
        s->Loop(c, fig, ppl, ratio, 0, 2);
        s->Set(2.f * l0 - .03f, c, k2);
        s->Loop(c, fig, ppl, ratio, 2, 4);
    };
    auto K = [](Snd s) { return kInfo[s].k; };

    { Knobs k = K(KICK); k.prep = .7f; k.strike = .75f;
      Solo(KICK, 2.f, {{0, 57, .9f}, {4, 57, .8f}, {7, 60, .6f}, {8, 57, .85f}, {12, 55, .8f}, {14, 62, .55f}}, 16, 1, k, "then CHOMPI + K3 (prep): the zap"); }
    { Knobs k = K(TOM); k.shimmer = 0.f; k.strike = .8f;
      Solo(TOM, 2.f, {{0, 60, .8f}, {3, 57, .7f}, {6, 55, .75f}, {8, 52, .8f}, {10, 55, .6f}, {12, 48, .85f}}, 16, 1, k, "then without the head's ring (shimmer) and harder (strike)"); }
    { Knobs k = K(SNARE); k.shimmer = .85f; k.bright = .3f;
      Solo(SNARE, 2.f, {{4, 60, .85f}, {10, 60, .4f}, {12, 60, .9f}, {15, 62, .45f}}, 16, 1, k, "then CHOMPI + K1 (shimmer): the band rings on the key"); }
    { Knobs k = K(CLAP); k.prep = 1.f; k.decay = .55f;
      Solo(CLAP, 2.f, {{4, 60, .85f}, {12, 60, .9f}, {14, 62, .5f}}, 16, 1, k, "then CHOMPI + K3 (prep) up: four claps and a longer tail"); }
    { Knobs k = K(HAT); k.decay = .35f;
      Solo(HAT, 2.f, {{0, 67, .5f}, {2, 67, .85f}, {4, 67, .5f}, {6, 67, .85f}, {8, 67, .5f}, {10, 67, .85f}, {12, 67, .5f}, {14, 72, .9f}}, 16, 1, k, "then K3 (decay) up: open"); }
    { Knobs k = K(COWBELL); k.prep = .6f;
      Solo(COWBELL, 2.f, {{0, 60, .8f}, {3, 67, .7f}, {6, 60, .75f}, {10, 63, .7f}, {12, 60, .8f}}, 16, 1, k, "then CHOMPI + K3 (prep): the ring modulator's clang"); }
    { Knobs k = K(BLIP); k.prep = .5f;
      Solo(BLIP, 2.f, {{0, 72, .8f}, {2, 67, .7f}, {4, 64, .7f}, {6, 67, .7f}, {8, 72, .8f}, {10, 76, .7f}, {12, 79, .75f}, {14, 76, .7f}}, 16, 1, k, "then CHOMPI + K3 (prep): the octave arpeggio"); }
    { Knobs k = K(ACID); k.prep = 1.f; k.bright = .4f; k.decay = .4f;
      Solo(ACID, 2.f, {{0, 43, .9f}, {2, 43, .6f}, {3, 55, .8f}, {5, 43, .6f}, {6, 46, .9f}, {8, 43, .6f}, {10, 50, .8f}, {11, 43, .6f}, {13, 55, .9f}, {14, 48, .6f}}, 16, 1, k, "then the squelch: resonance (prep) all the way, brighter"); }
    { Knobs k = K(FMPIANO); k.strike = .85f; k.shimmer = 0.f;
      Solo(FMPIANO, 2.4f, {{0, 60, .8f}, {0, 64, .7f}, {0, 67, .7f}, {3, 69, .6f}, {6, 62, .8f}, {6, 65, .7f}, {6, 69, .7f}, {9, 72, .6f}}, 12, 1, k, "then harder (strike: more tine) and without the chorus (shimmer)"); }
    { Knobs k = K(FMBELL); k.prep = .7f;
      Solo(FMBELL, 3.f, {{0, 72, .8f}, {3, 67, .6f}, {5, 76, .7f}, {8, 64, .6f}}, 12, 1, k, "then CHOMPI + K3 (prep): another ratio, metal"); }
    { Knobs k = K(CHIPLEAD); k.prep = .9f;
      Solo(CHIPLEAD, 2.4f, {{0, 72, .8f}, {3, 74, .7f}, {4, 76, .8f}, {6, 79, .7f}, {9, 76, .6f}, {10, 74, .7f}}, 12, 1, k, "then CHOMPI + K3 (prep): the chord arpeggio"); }
    { Knobs k = K(STAB); k.shimmer = 0.f; k.strike = .75f;
      Solo(STAB, 2.f, {{0, 60, .8f}, {3, 63, .7f}, {6, 67, .75f}, {10, 65, .7f}, {12, 58, .8f}}, 16, 1, k, "then one saw (shimmer down), more accent"); }
    { Knobs k = K(GRIT); k.prep = .8f;
      Solo(GRIT, 2.f, {{0, 48, .8f}, {3, 55, .7f}, {6, 51, .75f}, {8, 48, .8f}, {11, 58, .7f}, {14, 53, .6f}}, 16, 1, k, "then CHOMPI + K3 (prep): crushed, with the stutter"); }
    { Knobs k = K(FMBASS); k.bright = .7f; k.decay = .45f;
      Solo(FMBASS, 2.f, {{0, 36, .9f}, {3, 36, .6f}, {6, 43, .8f}, {8, 36, .85f}, {11, 39, .7f}, {14, 41, .7f}}, 16, 1, k, "then brighter (K2) and longer (K3)"); }

    {   // MACHINE: the drum machine in circles. Four kicks in L0, the hat in
        // ×3/4, claps from the second L0, the cowbell ×5/4, the acid ×1, toms ×4/3.
        auto* s = S("piece-machine", "Machine", 2.f, 13 * 2.f + 1.5f, .3f,
                    "kick ×1, hat ×3/4, clap ×1, cowbell ×5/4, toms ×4/3, acid ×1, the squelch turned up as it goes");
        int kick = s->Add(KICK, 0.f, .85f, "kick ×1");
        int hat = s->Add(HAT, .35f, .45f, "hat ×3/4");
        int clap = s->Add(CLAP, -.15f, .6f, "clap ×1");
        int bell = s->Add(COWBELL, -.45f, .4f, "cowbell ×5/4");
        int tom = s->Add(TOM, .55f, .5f, "toms ×4/3");
        int acid = s->Add(ACID, -.05f, .6f, "acid ×1");
        s->Loop(kick, {{0, 57, .9f}, {4, 57, .8f}, {8, 57, .85f}, {12, 57, .8f}}, 16, 1, 0, 13);
        s->Loop(hat, {{2, 67, .8f}, {6, 67, .7f}, {10, 67, .85f}, {11, 72, .4f}}, 16, .75f, 1, 13);
        s->Loop(clap, {{4, 60, .85f}, {12, 60, .9f}}, 16, 1, 2, 13);
        s->Loop(bell, {{0, 60, .8f}, {3, 67, .7f}, {10, 63, .7f}}, 16, 1.25f, 4, 13);
        s->Loop(tom, {{14, 55, .7f}, {15, 52, .7f}, {19, 48, .8f}}, 16, 4.f / 3.f, 6, 13);
        s->Loop(acid, {{0, 43, .9f}, {2, 43, .6f}, {3, 55, .8f}, {6, 46, .9f}, {8, 43, .6f}, {10, 50, .8f}, {13, 55, .9f}, {14, 48, .6f}}, 16, 1, 3, 13);
        for (int b = 3; b < 13; ++b) { Knobs k = K(ACID); k.prep = .45f + .055f * (b - 3); k.bright = .15f + .03f * (b - 3); s->Set(b * 2.f - .01, acid, k); }
    }
    {   // CONSOLE: chip and FM. The bass ×1, the lead ×2, blips ×3/4, the
        // piano's chords ×3/2, a bell ×3, grit ×5/4.
        auto* s = S("piece-console", "Console", 2.4f, 12 * 2.4f + 2.f, .4f,
                    "FM bass ×1, chip lead ×2, blips ×3/4, FM piano ×3/2, FM bell ×3, grit ×5/4");
        int bass = s->Add(FMBASS, 0.f, .8f, "FM bass ×1");
        int lead = s->Add(CHIPLEAD, .3f, .5f, "chip lead ×2");
        int blip = s->Add(BLIP, -.5f, .4f, "blips ×3/4");
        int pno = s->Add(FMPIANO, -.25f, .55f, "FM piano ×3/2");
        int bell = s->Add(FMBELL, .55f, .4f, "FM bell ×3");
        int grit = s->Add(GRIT, .1f, .35f, "grit ×5/4");
        s->Loop(bass, {{0, 36, .9f}, {3, 36, .6f}, {6, 43, .8f}, {8, 36, .85f}, {11, 39, .7f}}, 12, 1, 0, 12);
        s->Loop(lead, {{0, 72, .8f}, {3, 74, .7f}, {4, 76, .8f}, {6, 79, .7f}, {9, 76, .6f}, {12, 74, .7f}, {15, 72, .6f}, {18, 67, .8f}}, 12, 2, 4, 12);
        s->Loop(blip, {{0, 84, .7f}, {2, 79, .6f}, {4, 88, .65f}}, 12, .75f, 1, 12);
        s->Loop(pno, {{0, 60, .7f}, {0, 63, .6f}, {0, 67, .6f}, {9, 58, .6f}, {9, 62, .55f}, {9, 65, .55f}}, 12, 1.5f, 2, 12);
        s->Loop(bell, {{0, 79, .7f}, {18, 74, .6f}}, 12, 3, 3, 12);
        s->Loop(grit, {{5, 48, .7f}, {8, 51, .6f}}, 12, 1.25f, 6, 12);
    }
    {   // TWO SETS: the marimba and the gangsa of set 1 with the kick, the hat,
        // the FM bell and the acid of set 2.
        auto* s = S("piece-two-sets", "Two sets", 2.4f, 12 * 2.4f + 2.f, .45f,
                    "set 1: marimba ×1, gangsa ×3/4; set 2: kick ×1, hat ×5/4, FM bell ×3, acid ×2/3");
        int mar = s->Add(MARIMBA, -.3f, .7f, "marimba ×1");
        int gan = s->Add(GANGSA, .35f, .5f, "gangsa ×3/4");
        int kick = s->Add(KICK, 0.f, .7f, "kick ×1");
        int hat = s->Add(HAT, .5f, .35f, "hat ×5/4");
        int bell = s->Add(FMBELL, -.55f, .35f, "FM bell ×3");
        int acid = s->Add(ACID, .1f, .45f, "acid ×2/3");
        s->Loop(mar, {{0, 60, .8f}, {2, 67, .7f}, {3, 64, .7f}, {6, 62, .8f}, {8, 67, .7f}, {9, 65, .6f}}, 12, 1, 0, 12);
        s->Loop(gan, {{0, 72, .7f}, {1, 74, .6f}, {3, 76, .65f}, {5, 74, .6f}, {6, 72, .7f}}, 12, .75f, 2, 12);
        s->Loop(kick, {{0, 57, .85f}, {6, 57, .7f}}, 12, 1, 1, 12);
        s->Loop(hat, {{3, 67, .7f}, {9, 67, .8f}, {13, 72, .5f}}, 12, 1.25f, 3, 12);
        s->Loop(bell, {{0, 84, .6f}, {16, 79, .55f}}, 12, 3, 4, 12);
        s->Loop(acid, {{0, 43, .8f}, {3, 55, .7f}, {5, 46, .8f}}, 12, 2.f / 3.f, 5, 12);
    }
    return out;
}

static std::string Esc(const std::string& s) {
    std::string r;
    for (char c : s) { if (c == '"' || c == '\\') r += '\\'; r += c; }
    return r;
}

int main(int argc, char** argv) {
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    const std::string dir = argc > 1 ? argv[1] : "out";
    for (int s = 0; s < kAll; ++s) g_calib[s] = .12f / Loudness(Snd(s));
    FILE* cpu = std::fopen((dir + "/cpu.json").c_str(), "w");
    const double base = TimeVoice(MARIMBA);
    std::fprintf(cpu, "{");
    for (int s = 0; s < kSounds; ++s) {
        const double t = TimeVoice(Snd(s));
        std::fprintf(cpu, "%s\"%s\": %.2f", s ? ", " : "", kInfo[s].id, t / base);
        std::printf("cpu %-9s %.2f x mallet\n", kInfo[s].id, t / base);
    }
    std::fprintf(cpu, ", \"gangsa\": %.2f}\n", TimeVoice(GANGSA) / base);
    std::fclose(cpu);
    FILE* man = std::fopen((dir + "/manifest.json").c_str(), "w");
    std::fprintf(man, "[\n");
    bool first = true;
    for (auto& sc : Scenes()) {
        std::vector<float> L, R;
        const Stats st = Render(*sc, L, R);
        WriteWav(dir + "/" + sc->id + ".wav", L, R);
        std::printf("%-20s %5.1fs peak %.2f nan %d\n", sc->id.c_str(), sc->len, st.peak, st.nan);
        std::fprintf(man, "%s  {\"id\": \"%s\", \"title\": \"%s\", \"note\": \"%s\", \"len\": %.1f, \"players\": [", first ? "" : ",\n",
                     sc->id.c_str(), Esc(sc->title).c_str(), Esc(sc->note).c_str(), sc->len);
        for (size_t c = 0; c < sc->ch.size(); ++c) std::fprintf(man, "%s\"%s\"", c ? ", " : "", Esc(sc->ch[c].who).c_str());
        std::fprintf(man, "]}");
        first = false;
    }
    std::fprintf(man, "\n]\n");
    std::fclose(man);
}
