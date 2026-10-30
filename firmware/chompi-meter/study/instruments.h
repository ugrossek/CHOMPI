#pragma once
// CIRCLES · a study in sound: twelve instruments from music played in circles.
// Every one answers to the same six knobs (kit.h Knobs):
//   K1 strike   how it is set going: mallet, thumb, breath, bow
//   K2 bright   its colour
//   K3 decay    how long it sounds
//   CHOMPI + K1 shimmer  what makes it beat or double
//   CHOMPI + K2 feel     the player: no two notes alike
//   CHOMPI + K3 prep     the instrument's own preparation (a damping hand,
//                        bottle caps, loose keys, a bolt between the strings ...)

#include "kit.h"

namespace sd {

struct Voice {
    float sr = 48000.f, freq = 0.f;
    Rng rng;
    bool active = false;
    int quiet = 0;
    virtual ~Voice() {}
    virtual void Init(float s, uint32_t seed) { sr = s; rng.Seed(seed); }
    virtual void Strike(float f, float vel, const Knobs& k) = 0;
    virtual float Tick() = 0;
    virtual bool Busy() const { return false; } // still being played (bow, breath)
    virtual void Choke(float) {}
    float Process() {
        if (!active) return 0.f;
        const float y = Tick();
        if (!Busy() && std::fabs(y) < 1e-4f) { if (++quiet > 4800) active = false; }
        else quiet = 0;
        return y;
    }
};

// ---------------------------------------------------------------------------
// 1 · GANGSA — Balinese bronze, kotekan
// A thick bronze bar over a bamboo tube, hit with a wooden hammer. Shimmer is
// ombak: the instrument and its partner tuned a few Hz apart, so every note
// breathes. Prep is the left hand: how much of the last note the player damps
// as the next one sounds (tetekep), from a wash of bronze to a clean line.
struct Gangsa : Voice {
    static constexpr int M = 6;
    static constexpr float R[M] = {1.f, 2.46f, 4.68f, 7.36f, 10.5f, 14.2f};
    static constexpr float A[M] = {1.f, .5f, .34f, .22f, .13f, .08f};
    Res bar[2][M], tube;
    Pulse p;
    Burst click;
    Svf clickf;
    float t60[M] = {}, tube_t60 = .4f;
    bool pair = false;
    void Strike(float f, float vel, const Knobs& k) override {
        const float v = k.feel;
        if (f != freq) { for (auto& b : bar) for (auto& m : b) m.Clear(); tube.Clear(); }
        freq = f;
        const float pos = .2f + v * .2f * rng.B();
        const float hard = Clamp(k.strike + v * .25f * rng.B(), 0.f, 1.f);
        vel *= 1.f - v * .35f * rng.U();
        const float fd = f * Cents(v * 5.f * rng.B());
        const float tilt = (k.bright - .5f) * 2.4f;
        float t60f = .6f * std::pow(25.f, k.decay);
        if (f < 200.f) t60f *= std::sqrt(200.f / f); // the big low bars ring longest
        const float beat = .5f + 8.f * k.shimmer;
        pair = k.shimmer > .01f;
        float amps[M], norm = 0.f;
        for (int m = 0; m < M; ++m) {
            amps[m] = A[m] * std::pow(R[m], tilt) * (.35f + .65f * std::fabs(std::sin(kPi * (m + 1) * pos)));
            norm += amps[m] * amps[m];
        }
        norm = 1.f / std::sqrt(norm);
        if (pair) norm *= .7071f;
        for (int m = 0; m < M; ++m) {
            t60[m] = t60f * std::pow(R[m], -1.05f);
            bar[0][m].Set(fd * R[m], t60[m], amps[m] * norm, sr);
            bar[1][m].Set((fd + beat) * R[m], t60[m] * .97f, pair ? amps[m] * norm : 0.f, sr);
        }
        tube_t60 = .3f + .3f * k.decay;
        tube.Set(fd, tube_t60, .5f * norm, sr);
        p.Start(.0028f * std::pow(.075f, hard), vel, sr); // 2.8 ms (soft) .. 0.2 ms (horn)
        click.Start(vel * .05f * (.2f + hard), .004f, sr);
        clickf.Set(2400.f + 1800.f * hard, 1.4f, sr);
        active = true; quiet = 0;
    }
    float Tick() override {
        const float x = p.Tick();
        float y = tube.Tick(x);
        for (int m = 0; m < M; ++m) { y += bar[0][m].Tick(x); if (pair) y += bar[1][m].Tick(x); }
        return y + clickf.Bp(click.Tick(rng));
    }
    bool Busy() const override { return p.Busy(); }
    void Choke(float a) override {
        if (a <= 0.f) return;
        for (int m = 0; m < M; ++m) {
            const float t = Lerp(t60[m], .07f * std::pow(R[m], -.4f), a);
            bar[0][m].Decay(t); bar[1][m].Decay(t * .97f);
        }
        tube.Decay(Lerp(tube_t60, .05f, a));
    }
};

// ---------------------------------------------------------------------------
// 2 · MBIRA — Shona thumb piano, kushaura and kutsinhira
// A steel tine (clamped bar: 1 : 6.1 : 17) on a soundboard in a gourd. The
// thumbnail or the flesh of the thumb (strike). Prep is the machira: bottle
// caps on the board that sizzle with every note, as long as it rings.
struct Mbira : Voice {
    static constexpr int M = 3;
    Res tine[2][M], body[3];
    Pulse p;
    Burst thumb;
    Svf thumbf;
    Contact cap[2];
    bool pair = false;
    float buzz = 0.f;
    void Init(float s, uint32_t seed) override {
        Voice::Init(s, seed);
        body[0].Set(205.f, .10f, .55f, s);
        body[1].Set(470.f, .06f, .35f, s);
        body[2].Set(1180.f, .03f, .2f, s);
        cap[0].Set(3300.f, 6100.f, .012f, .32f, s);
        cap[1].Set(4200.f, 7600.f, .009f, .42f, s);
    }
    void Strike(float f, float vel, const Knobs& k) override {
        static const float R[M] = {1.f, 6.07f, 16.9f}, A[M] = {1.f, .34f, .12f};
        const float v = k.feel;
        if (f != freq) for (auto& t : tine) for (auto& m : t) m.Clear();
        freq = f;
        vel *= 1.f - v * .4f * rng.U();
        const float fd = f * Cents(v * 4.f * rng.B());
        const float tilt = (k.bright - .5f) * 2.f;
        const float t60f = 1.f * std::pow(8.f, k.decay);
        pair = k.shimmer > .01f;
        const float beat = .3f + 3.5f * k.shimmer;
        for (int m = 0; m < M; ++m) {
            const float r = R[m] * (1.f + v * .01f * rng.B());
            const float a = A[m] * std::pow(r, tilt * .5f) * (pair ? .7071f : 1.f);
            const float t = t60f * std::pow(r, -.85f);
            tine[0][m].Set(fd * r, t, a, sr);
            tine[1][m].Set((fd + beat) * r, t, pair ? a : 0.f, sr);
        }
        const float hard = Clamp(k.strike + v * .2f * rng.B(), 0.f, 1.f);
        p.Start(.0025f * std::pow(.12f, hard), vel, sr);
        thumb.Start(vel * .03f * (1.2f - hard), .007f, sr);
        thumbf.Set(700.f + 900.f * hard, .9f, sr);
        buzz = k.prep;
        active = true; quiet = 0;
    }
    float Tick() override {
        const float x = p.Tick();
        float t = 0.f;
        for (int m = 0; m < M; ++m) { t += tine[0][m].Tick(x); if (pair) t += tine[1][m].Tick(x); }
        float b = 0.f;
        const float drive = t * .12f + thumbf.Bp(thumb.Tick(rng));
        for (auto& r : body) b += r.Tick(drive);
        float y = t + b;
        if (buzz > 0.f) {
            // Machira: two bottle caps on the board, thrown up by the board's
            // swing and landing again, once per cycle while it is loud enough.
            const float board = t + b * 2.f;
            y += buzz * .2f * (cap[0].Tick(board, rng) + cap[1].Tick(board * .8f, rng));
        }
        return y;
    }
    bool Busy() const override { return p.Busy(); }
};

// ---------------------------------------------------------------------------
// 3 · AMADINDA — Ugandan log xylophone, okunaga and okwawula
// Loose logs on banana stems, hit with hard sticks: short, woody, the highs
// gone almost at once, a knock and a thump of the stems. Prep loosens the keys:
// each hit bounces against the pegs once or twice.
struct Amadinda : Voice {
    static constexpr int M = 4;
    Res log[2][M], stem;
    Pulse p;
    Burst knock;
    Svf knockf;
    bool pair = false;
    float prep = 0.f, hard = .5f, vel0 = 1.f;
    int bounce_at[3] = {-1, -1, -1};
    float bounce_v[3] = {};
    int t = 0;
    void Init(float s, uint32_t seed) override { Voice::Init(s, seed); stem.Set(118.f, .06f, .12f, s); }
    void Strike(float f, float vel, const Knobs& k) override {
        static const float R[M] = {1.f, 2.62f, 4.95f, 7.9f}, A[M] = {1.f, .5f, .28f, .14f};
        const float v = k.feel;
        if (f != freq) for (auto& l : log) for (auto& m : l) m.Clear();
        freq = f;
        vel *= 1.f - v * .4f * rng.U();
        const float fd = f * Cents(v * 8.f * rng.B()); // logs are never quite in tune
        const float tilt = (k.bright - .5f) * 2.f;
        const float t60f = .2f * std::pow(12.f, k.decay);
        const float pos = .25f + v * .2f * rng.B();
        pair = k.shimmer > .01f;
        const float beat = .5f + 3.f * k.shimmer;
        float amps[M], norm = 0.f;
        for (int m = 0; m < M; ++m) {
            amps[m] = A[m] * std::pow(R[m], tilt) * (.4f + .6f * std::fabs(std::sin(kPi * (m + 1) * pos)));
            norm += amps[m] * amps[m];
        }
        norm = (pair ? .7071f : 1.f) / std::sqrt(norm);
        for (int m = 0; m < M; ++m) {
            const float tt = t60f * std::pow(R[m], -2.f);
            log[0][m].Set(fd * R[m], tt, amps[m] * norm, sr);
            log[1][m].Set((fd + beat) * R[m], tt, pair ? amps[m] * norm : 0.f, sr);
        }
        hard = Clamp(k.strike + v * .2f * rng.B(), 0.f, 1.f);
        Hit(vel);
        prep = k.prep;
        t = 0;
        // Loose keys: one to three bounces, 8 to 30 ms apart, each softer.
        int at = 0;
        for (int i = 0; i < 3; ++i) {
            if (prep > .01f && rng.U() < prep * (1.2f - .3f * i)) {
                at += int((.008f + .022f * rng.U()) * sr);
                bounce_at[i] = at;
                bounce_v[i] = vel * prep * (.45f - .13f * i) * (.6f + .4f * rng.U());
            } else bounce_at[i] = -1;
        }
        active = true; quiet = 0;
    }
    void Hit(float vel) {
        p.Start(.0016f * std::pow(.18f, hard), vel, sr);
        knock.Start(vel * .07f * (.4f + hard), .005f, sr);
        knockf.Set(1500.f + 900.f * hard, 1.3f, sr);
    }
    float Tick() override {
        for (int i = 0; i < 3; ++i) if (t == bounce_at[i]) Hit(bounce_v[i]);
        ++t;
        const float x = p.Tick();
        float y = stem.Tick(x);
        for (int m = 0; m < M; ++m) { y += log[0][m].Tick(x); if (pair) y += log[1][m].Tick(x); }
        return y + knockf.Bp(knock.Tick(rng));
    }
    bool Busy() const override { return p.Busy() || t < 2000; }
};

// ---------------------------------------------------------------------------
// 4 · SIKU — Andean panpipes, played in hocket by two players (ira and arca)
// A stopped bamboo pipe: a resonator with only the odd harmonics, blown across
// the edge. The tone and the air are separate: an edge tone, and breath noise
// that the pipe turns into a pitch. Shimmer adds the pipe an octave below (the
// tropa's malta, played together); prep roughens the breath to the husky tara.
struct Siku : Voice {
    Delay pipe;
    OnePole loss, air, airhp, wob;
    Burst chiff;
    float D = 10.f, fb = .99f, ph = 0.f, ph2 = 0.f, tone_amp = .5f, air_amp = .3f;
    float scoop = 1.f, malta = 0.f, rasp = 0.f, drift = 0.f, drift_v = 0.f, wander = 0.f;
    int t = 0, att = 1, hold = 1, rel = 1;
    void Init(float s, uint32_t seed) override {
        Voice::Init(s, seed);
        pipe.Init(int(s / 40.f));
        wob.Set(4.f, s);
        airhp.Set(350.f, s);
    }
    void Strike(float f, float vel, const Knobs& k) override {
        freq = f * Cents(k.feel * 8.f * rng.B());
        loss.Set(2500.f + 6000.f * k.bright, sr);
        air.Set(1800.f + 5000.f * k.bright, sr);
        const float w = kTau * freq / sr;
        // Stopped pipe: the loop's delay is half a period, inverted.
        D = std::max(2.f, sr / (2.f * freq) - OnePoleDelay(loss.k, w));
        fb = .985f + .01f * k.bright;
        pipe.Clear();
        att = int((.012f + .07f * (1.f - k.strike) * (1.f - k.strike)) * sr);
        hold = int((.06f + 2.2f * k.decay * k.decay) * sr);
        rel = int(.07f * sr);
        scoop = Cents(-30.f);
        tone_amp = vel * (.55f + .1f * rng.B() * k.feel);
        air_amp = vel * (.28f + .25f * k.bright + .35f * k.prep);
        chiff.Start(vel * (.6f + .6f * k.strike), .025f, sr);
        malta = k.shimmer * .7f;
        rasp = k.prep;
        wander = k.feel;
        t = 0;
        active = true; quiet = 0;
    }
    float Env() const {
        if (t < att) { const float a = float(t) / att; return a * a * (3.f - 2.f * a); }
        if (t < att + hold) return 1.f - .25f * float(t - att) / hold; // breath runs out a little
        const int r = t - att - hold;
        return r < rel ? .75f * (1.f - float(r) / rel) : 0.f;
    }
    float Tick() override {
        const float e = Env();
        ++t;
        // The breath: a little uneven, more so with feel; rasp is a fast flutter.
        drift_v += .00002f * rng.B() * (1.f + 3.f * wander);
        drift_v *= .999f;
        drift += drift_v;
        drift *= .9995f;
        const float press = e * (1.f + .1f * wob.Lp(rng.B()) * 8.f * (.3f + wander))
                              * (1.f - rasp * .45f * (.5f + .5f * Sin1(24.f * t / sr)));
        // Blowing in: the pitch rises the last few cents to the note.
        if ((t & 31) == 1) scoop = t < att ? Cents(-30.f * (1.f - float(t) / att)) : 1.f;
        const float f0 = freq * scoop * (1.f + drift * .02f);
        ph += f0 / sr; if (ph >= 1.f) ph -= 1.f;
        ph2 += .5f * f0 / sr; if (ph2 >= 1.f) ph2 -= 1.f;
        float tone = Sin1(ph) + .16f * Sin1(3.f * ph) + .05f * Sin1(5.f * ph);
        if (malta > 0.f) tone += malta * (Sin1(ph2) + .2f * Sin1(3.f * ph2));
        // The air through the pipe: noise, made a pitch by the stopped tube.
        const float n = airhp.Hp(air.Lp(rng.B())) * press * air_amp + chiff.Tick(rng) * .5f;
        const float back = loss.Lp(pipe.Lin(D - 1.f));
        const float y = n - fb * back;
        pipe.Push(y);
        return tone * press * tone_amp + y * .09f;
    }
    bool Busy() const override { return t < att + hold + rel; }
};

// ---------------------------------------------------------------------------
// 5 · KORA — Mande harp-lute, kumbengo
// A nylon string (Karplus-Strong with a loss filter tuned for its ring time),
// plucked by thumb or finger at a point along it, through a calabash under a
// cow-skin soundboard. The nylon goes sharp for a moment when plucked hard.
// Shimmer: the octave string above rings in sympathy. Prep: the nyenyemo,
// a tin plate with rings on the bridge, jingling with the string.
struct Kora : Voice {
    Delay str, sym;
    OnePole loss, sloss, exlp;
    Res body[3];
    Contact plate[2];
    std::vector<float> ex;
    float D = 100.f, g = .99f, sD = 50.f, sg = .99f, bend = 0.f, bend_mul = 1.f, symp = 0.f, jingle = 0.f;
    int exn = 0, exi = 0;
    void Init(float s, uint32_t seed) override {
        Voice::Init(s, seed);
        str.Init(int(s / 40.f));
        sym.Init(int(s / 80.f));
        ex.assign(int(s / 40.f), 0.f);
        body[0].Set(118.f, .28f, .5f, s);
        body[1].Set(262.f, .14f, .35f, s);
        body[2].Set(730.f, .05f, .22f, s);
        plate[0].Set(2900.f, 5300.f, .025f, .2f, s);  // the tin plate
        plate[1].Set(4600.f, 7900.f, .018f, .28f, s); // its rings
    }
    void Strike(float f, float vel, const Knobs& k) override {
        const float v = k.feel;
        freq = f * Cents(v * 3.f * rng.B());
        vel *= 1.f - v * .35f * rng.U();
        const float w = kTau * freq / sr;
        loss.Set(Clamp(1500.f + 9000.f * k.bright * k.bright + 2.f * freq, 800.f, 16000.f), sr);
        D = sr / freq - OnePoleDelay(loss.k, w) - 1.f;
        const float t60 = (.7f + 7.f * std::pow(k.decay, 1.5f)) * (freq < 200.f ? 1.25f : 1.f);
        // Loop gain: the loss filter's own damping at the fundamental is made up.
        const float a = 1.f - loss.k;
        const float hmag = loss.k / std::sqrt(1.f - 2.f * a * std::cos(w) + a * a);
        g = std::min(std::pow(10.f, -3.f / (freq * t60)) / hmag, .9995f);
        // The pluck: one period of noise, as soft as the finger, minus itself
        // delayed to the plucking point.
        const float hard = Clamp(k.strike + v * .2f * rng.B(), 0.f, 1.f);
        exlp.Set(500.f + 7000.f * hard * hard, sr);
        exlp.y = 0.f;
        exn = std::min(int(D), int(ex.size()) - 1);
        const float pos = .13f + .04f * v * rng.B();
        const int pd = std::max(1, int(pos * exn));
        std::vector<float> tmp(exn);
        float mean = 0.f;
        for (int i = 0; i < exn; ++i) { tmp[i] = exlp.Lp(rng.B()); mean += tmp[i]; }
        mean /= exn;
        for (int i = 0; i < exn; ++i) ex[i] = vel * 1.6f * ((tmp[i] - mean) - (i >= pd ? tmp[i - pd] - mean : 0.f));
        exi = 0;
        bend = .007f * vel * (.5f + hard); // nylon goes sharp, then settles
        bend_mul = std::exp(-1.f / (.07f * sr));
        // The sympathetic octave string.
        sloss.Set(6000.f, sr);
        sD = sr / (2.f * freq) - OnePoleDelay(sloss.k, 2.f * w) - 1.f;
        sg = std::pow(10.f, -3.f / (2.f * freq * 3.f));
        symp = k.shimmer;
        jingle = k.prep;
        active = true; quiet = 0;
    }
    float Tick() override {
        const float x = exi < exn ? ex[exi++] : 0.f;
        bend *= bend_mul;
        const float y = str.Lin(std::max(1.f, D * (1.f - bend)));
        str.Push(x + g * loss.Lp(y));
        float out = y;
        if (symp > 0.f) {
            const float s = sym.Lin(std::max(1.f, sD));
            sym.Push(y * .02f * symp + sg * sloss.Lp(s));
            out += s * .8f;
        }
        const float drive = out * .1f;
        float b = 0.f;
        for (auto& r : body) b += r.Tick(drive);
        out = out * .75f + b;
        if (jingle > 0.f) out += jingle * .45f * (plate[0].Tick(y, rng) + plate[1].Tick(y * .7f, rng));
        return out;
    }
    bool Busy() const override { return exi < exn; }
};

// ---------------------------------------------------------------------------
// 6 · VOICES — circle singing
// A voice, from the glottis up: a glottal pulse (Rosenberg) with a little
// jitter, through five formants (Klatt cascade), with a consonant before the
// vowel: hummed (m, n) for a soft strike, voiced and plosive (b, d, t) for a
// hard one. Brightness is the vowel, u o a e i. Decay is how long the note is
// sung. Shimmer: two more singers on the same part. Prep closes the mouth at
// the end of every note: "da" becomes "dum", "ba" becomes "bam".
struct Voices : Voice {
    struct KR {
        float a = 0, b = 0, c = 0, y1 = 0, y2 = 0;
        void Set(float f, float bw, float sr) {
            const float r = std::exp(-kPi * bw / sr);
            c = -r * r; b = 2.f * r * std::cos(kTau * f / sr); a = 1.f - b - c;
        }
        float Tick(float x) { const float y = a * x + b * y1 + c * y2; y2 = y1; y1 = y; return y; }
    };
    static constexpr int NF = 5;
    KR F[NF];
    float ph[3] = {}, det[3] = {1.f, 1.f, 1.f}, flow_prev[3] = {};
    int lag[3] = {};
    int nsrc = 1, t = 0, len = 1, rel = 1, cons = 0, cl = 0, coda = 0;
    float pitch = 0.f, vowel[NF] = {}, locus[NF] = {}, nasal_amt = 0.f, breath = .1f, effort = .5f, vib = 0.f, jit = 0.f, amp = 1.f, scale = 1.f, f0 = 100.f;
    Burst burst;
    Svf burstf;
    OnePole asp;
    static void Vowel(float x, float* out) {
        static const float T[5][NF] = {{310, 870, 2250, 3300, 3850}, {500, 850, 2500, 3300, 3850},
                                       {720, 1150, 2550, 3300, 3850}, {480, 1850, 2550, 3300, 3850},
                                       {290, 2250, 2900, 3400, 3900}};
        x = Clamp(x, 0.f, 1.f) * 4.f;
        const int i = std::min(int(x), 3);
        const float f = x - i;
        for (int k = 0; k < NF; ++k) out[k] = Lerp(T[i][k], T[i + 1][k], f);
    }
    void Init(float s, uint32_t seed) override { Voice::Init(s, seed); asp.Set(900.f, s); }
    void Strike(float f, float vel, const Knobs& k) override {
        freq = f;
        const float v = k.feel;
        // Men below, women above, as in a circle: the formants move up with the register.
        scale = f < 180.f ? 1.f : f > 330.f ? 1.17f : 1.f + .17f * (f - 180.f) / 150.f;
        Vowel(k.bright + v * .08f * rng.B(), vowel);
        // The consonant: soft strikes hum in, hard ones are plosive.
        const float c = Clamp(k.strike + .25f * rng.B() * (.3f + v), 0.f, 1.f);
        cons = c < .3f ? 0 : c < .55f ? 1 : c < .8f ? 2 : 3; // m, b, d, t
        static const float L[4][NF] = {{260, 1050, 2200, 3200, 3800}, {220, 850, 2200, 3200, 3800},
                                       {220, 1750, 2650, 3300, 3850}, {250, 1800, 2700, 3300, 3850}};
        for (int i = 0; i < NF; ++i) { locus[i] = L[cons][i]; }
        cl = cons == 0 ? int(.06f * sr) : int((.018f + .012f * rng.U()) * sr);
        len = int((.13f + 3.2f * k.decay * k.decay) * sr) + cl;
        rel = int(.08f * sr);
        coda = int((.05f + .1f * k.prep) * sr);
        nasal_amt = k.prep;
        breath = .06f + .1f * (1.f - k.strike) + .1f * v;
        effort = .35f + .5f * k.strike;
        vib = .006f * (.4f + v);
        jit = .004f * (.5f + v);
        amp = vel;
        nsrc = 1 + int(k.shimmer * 2.99f);
        for (int i = 0; i < 3; ++i) {
            det[i] = i == 0 ? 1.f : Cents((i == 1 ? -1.f : 1.f) * (5.f + 6.f * v) + 2.f * rng.B());
            lag[i] = i == 0 ? 0 : int((.012f + .02f * rng.U()) * sr);
            ph[i] = rng.U();
        }
        if (cons >= 1) {
            burst.Start(vel * (cons == 3 ? .5f : .3f), cons == 3 ? .02f : .007f, sr);
            burstf.Set(cons == 1 ? 1200.f : cons == 2 ? 3600.f : 4800.f, 1.2f, sr);
        }
        t = 0;
        for (auto& r : F) { r.y1 = r.y2 = 0.f; }
        active = true; quiet = 0;
    }
    void Formants() {
        // Consonant locus -> vowel over 60 ms after the release; the coda back to m.
        const int since = t - cl;
        float k = since < 0 ? 0.f : std::min(1.f, since / (.06f * sr));
        k = k * k * (3.f - 2.f * k);
        float f[NF];
        static const float Mf[NF] = {260, 1150, 2200, 3200, 3800};
        const int to_end = len - t;
        float m = 0.f;
        if (nasal_amt > 0.f && to_end < coda) m = nasal_amt * Clamp(1.f - float(std::max(to_end, 0)) / coda, 0.f, 1.f);
        if (t > len) m = nasal_amt;
        static const float BW[NF] = {70, 90, 130, 200, 260};
        for (int i = 0; i < NF; ++i) {
            f[i] = Lerp(locus[i], vowel[i], k);
            f[i] = Lerp(f[i], Mf[i], m);
            const float bw = BW[i] * (1.f + m * (i == 0 ? .2f : 2.5f)) * (since < 0 && cons == 0 ? 2.f : 1.f);
            F[i].Set(std::min(f[i] * (i < 3 ? scale : 1.f), .45f * sr), bw, sr);
        }
    }
    float Glottis(float p) const {
        // Rosenberg flow: opening, closing (shorter with effort), closed.
        const float tp = .42f, tn = .26f - .14f * effort;
        if (p < tp) return .5f * (1.f - Cos1(.5f * p / tp));
        if (p < tp + tn) return Cos1(.25f * (p - tp) / tn);
        return 0.f;
    }
    float Tick() override {
        if ((t & 31) == 0) {
            Formants();
            // Pitch: a scoop up into the note, vibrato that grows in (control rate).
            const float tt = float(t) / sr;
            const float scoop = tt > .3f ? 1.f : Cents(-45.f * std::exp(-tt / .05f));
            const float vgrow = Clamp((tt - .25f) / .4f, 0.f, 1.f);
            pitch = freq * scoop * (1.f + vib * vgrow * Sin1(5.3f * tt)) / sr;
        }
        // Loudness: closure (quiet voicing for b/d, silence for t), the note, the release.
        float e;
        if (t < cl) e = cons == 0 ? .5f * t / float(cl) + .1f : cons == 3 ? 0.f : .08f;
        else if (t < len) {
            const float a = std::min(1.f, (t - cl) / (.02f * sr));
            e = a * (1.f - .2f * float(t - cl) / std::max(1, len - cl));
            if (nasal_amt > 0.f && len - t < coda) e *= 1.f - .45f * nasal_amt * (1.f - float(len - t) / coda);
        } else {
            const int r = t - len;
            e = r < rel ? (1.f - float(r) / rel) * (.8f - .35f * nasal_amt) : 0.f;
        }
        float src = 0.f;
        for (int i = 0; i < nsrc; ++i) {
            if (t < lag[i]) continue;
            ph[i] += pitch * det[i] * (1.f + jit * rng.B());
            if (ph[i] >= 1.f) ph[i] -= 1.f;
            const float fl = Glottis(ph[i]);
            src += (fl - flow_prev[i]) * (i == 0 ? 1.f : .8f);
            flow_prev[i] = fl;
        }
        src *= 12.f / std::sqrt(float(nsrc)) / std::max(1.f, freq / 150.f) * std::sqrt(std::max(1.f, freq / 150.f));
        const float n = rng.B();
        const float aspir = asp.Hp(n) * breath * (.3f + flow_prev[0]) * .5f;
        float y = (src + aspir) * e;
        for (auto& r : F) y = r.Tick(y);
        float b = 0.f;
        if (t >= cl && t < cl + int(.03f * sr)) b = burstf.Bp(burst.Tick(rng));
        ++t;
        return (y + b * .6f) * amp;
    }
    bool Busy() const override { return t < len + rel; }
};

// ---------------------------------------------------------------------------
// 7 · HANDS — the circle's own percussion
// No instrument but the singers' bodies: the low keys pat the chest and
// thighs, the middle ones clap (cupped low, flat and bright higher up), the top
// ones snap. A clap is three or four small slaps in 20 ms and a short ring.
// Shimmer: more people clapping along, never quite together. Prep: a step,
// the body's thump under every hit.
struct Hands : Voice {
    struct Hit { int at; float level, tau; int who; };
    Hit hits[24];
    int nh = 0, t = 0, kind = 1;
    Svf f[4];
    Burst b[4];
    Res thump, chest;
    OnePole lp;
    float tail = .08f;
    void Strike(float fr, float vel, const Knobs& k) override {
        freq = fr;
        const float n = 69.f + 12.f * std::log2(fr / 440.f);
        kind = n < 52.f ? 0 : n < 66.f ? 1 : 2;
        const float v = k.feel;
        const int people = 1 + int(k.shimmer * 3.99f);
        nh = 0;
        tail = .03f + .22f * k.decay;
        for (int p = 0; p < people; ++p) {
            const int off = p == 0 ? 0 : int((.006f + .022f * rng.U() * (.5f + v)) * sr);
            const float lv = vel * (p == 0 ? 1.f : .55f + .3f * rng.U()) * (1.f - .3f * v * rng.U());
            if (kind == 0) {
                f[p].Set(220.f + 8.f * (n - 36.f) * (1.f + .1f * rng.B()), 1.f, sr);
                hits[nh++] = {off, lv * .9f, .012f + .02f * k.decay, p};
            } else if (kind == 1) {
                const float fc = (650.f + 1500.f * Clamp((n - 52.f) / 14.f, 0.f, 1.f)) * (1.f + .12f * rng.B()) * (.8f + .4f * k.bright);
                f[p].Set(fc, 1.6f, sr);
                const int slaps = 3 + (rng.U() < .5f);
                int at = off;
                for (int s = 0; s < slaps && nh < 23; ++s) {
                    hits[nh++] = {at, lv * (s == slaps - 1 ? 1.f : .55f), s == slaps - 1 ? tail * (.5f + k.strike) : .0025f, p};
                    at += int((.005f + .004f * rng.U()) * sr);
                }
            } else {
                f[p].Set((2400.f + 120.f * (n - 66.f)) * (1.f + .08f * rng.B()) * (.8f + .4f * k.bright), 3.f, sr);
                hits[nh++] = {off, lv * .8f, .01f + .01f * k.decay, p};
            }
        }
        thump.Set(kind == 0 ? 110.f + 3.f * (n - 36.f) : 85.f, .09f, kind == 0 ? .9f : 0.f, sr);
        chest.Set(70.f, .12f, k.prep, sr);
        if (kind == 0) thump.Tick(vel * 1.2f);
        if (k.prep > 0.f) chest.Tick(vel * 1.5f);
        lp.Set(kind == 0 ? 900.f : kind == 1 ? 6500.f : 9000.f, sr); // hands are not cymbals
        t = 0;
        active = true; quiet = 0;
    }
    float Tick() override {
        for (int i = 0; i < nh; ++i) if (hits[i].at == t) b[hits[i].who].Start(hits[i].level, hits[i].tau, sr);
        ++t;
        float y = 0.f;
        for (int p = 0; p < 4; ++p) y += f[p].Bp(b[p].Tick(rng));
        return lp.Lp(y) * 2.2f + thump.Tick(0.f) + chest.Tick(0.f);
    }
    bool Busy() const override { return t < int(.12f * sr); }
};

// ---------------------------------------------------------------------------
// 8 · BOWED GLASS — the long loops (Eno)
// A glass rubbed or bowed: very narrow modes driven by rosin noise, so the
// tone swells and wavers like the real thing. Strike is the bow's speed: slow
// swells, fast catches with a scratch. Prep morphs the glass into a bowed
// vibraphone bar (1 : 4 : 10), its motor turning the tremolo on.
struct Bowed : Voice {
    static constexpr int M = 4;
    Res mode[2][M];
    Svf pre[M];
    OnePole nlp, wob;
    Svf scratch;
    int t = 0, att = 1, hold = 1, rel = 1;
    float lvl = 1.f, trem = 0.f, scr = 0.f, wavy = 0.f;
    bool pair = false;
    void Init(float s, uint32_t seed) override { Voice::Init(s, seed); wob.Set(1.5f, s); scratch.Set(5000.f, .8f, s); }
    void Strike(float f, float vel, const Knobs& k) override {
        static const float RG[M] = {1.f, 2.32f, 4.25f, 6.63f}, AG[M] = {1.f, .35f, .16f, .07f};
        static const float RV[M] = {1.f, 3.99f, 9.9f, 17.6f}, AV[M] = {1.f, .28f, .1f, .04f};
        freq = f;
        const float pr = k.prep;
        pair = k.shimmer > .01f;
        const float split = .25f + 1.6f * k.shimmer;
        const float tilt = (k.bright - .5f) * 2.f;
        const float t60 = 1.6f + 1.4f * k.decay;
        for (int m = 0; m < M; ++m) {
            const float r = Lerp(RG[m], RV[m], pr);
            const float a = Lerp(AG[m], AV[m], pr) * std::pow(r, tilt * .6f) * (pair ? .7071f : 1.f);
            const float tt = t60 * std::pow(r, -.5f);
            pre[m].Set(f * r, 6.f, sr);
            mode[0][m].SetNoise(f * r, tt, a, sr);
            mode[1][m].SetNoise((f + split) * r, tt, pair ? a : 0.f, sr);
        }
        nlp.Set(1200.f + 3000.f * k.bright, sr);
        const float slow = 1.f - k.strike;
        att = int((.04f + 1.6f * slow * slow) * sr);
        hold = int((.35f + 7.f * k.decay * k.decay) * sr);
        rel = int(.25f * sr);
        lvl = vel * 2.2f;
        trem = pr;
        scr = k.strike * k.strike * .5f;
        wavy = k.feel;
        t = 0;
        active = true; quiet = 0;
    }
    float Bow() const {
        if (t < att) { const float a = float(t) / att; return a * a * (3.f - 2.f * a); }
        if (t < att + hold) return 1.f;
        const int r = t - att - hold;
        return r < rel ? 1.f - float(r) / rel : 0.f;
    }
    float Tick() override {
        const float bow = Bow();
        const float press = bow * (1.f + wavy * .6f * wob.Lp(rng.B()) * 10.f);
        const float x = nlp.Lp(rng.B()) * press * lvl;
        float y = 0.f;
        for (int m = 0; m < M; ++m) {
            const float xm = pre[m].Bp(x) * 2.f;
            y += mode[0][m].Tick(xm);
            if (pair) y += mode[1][m].Tick(xm);
        }
        if (scr > 0.f && t < att + int(.15f * sr)) {
            const float sk = std::exp(-float(t) / (.04f * sr));
            y += scratch.Bp(rng.B()) * sk * scr * lvl * .15f;
        }
        if (trem > 0.f) y *= 1.f - trem * .5f * (.5f + .5f * Sin1(5.2f * t / sr));
        ++t;
        return y;
    }
    bool Busy() const override { return t < att + hold + rel; }
};

// ---------------------------------------------------------------------------
// 9 · SINGING BOWL — strike and rub
// A hammered bowl: inharmonic modes, each split into two close frequencies
// (the bowl is never quite round), so every partial beats at its own rate.
// Shimmer widens the split. Prep rubs the rim after the strike: the bowl
// starts to sing and keeps singing.
struct Bowl : Voice {
    static constexpr int M = 5;
    Res mode[2][M];
    Pulse p;
    Burst ting;
    Svf tingf;
    OnePole nlp;
    Res rubm[2];
    Svf rubpre;
    float rub = 0.f;
    int t = 0, rub_len = 1;
    void Init(float s, uint32_t seed) override { Voice::Init(s, seed); }
    void Strike(float f, float vel, const Knobs& k) override {
        static const float R[M] = {1.f, 2.71f, 5.12f, 8.18f, 11.9f}, A[M] = {1.f, .55f, .3f, .16f, .08f};
        if (f != freq) for (auto& b : mode) for (auto& m : b) m.Clear();
        freq = f;
        const float v = k.feel;
        const float tilt = (k.bright - .5f) * 2.f;
        const float t60f = 3.f * std::pow(8.f, k.decay);
        const float split = .4f + 2.2f * k.shimmer;
        for (int m = 0; m < M; ++m) {
            const float a = A[m] * std::pow(R[m], tilt) * .7071f;
            const float tt = t60f * std::pow(R[m], -.75f);
            const float d = split * std::sqrt(R[m]) * (1.f + .2f * v * rng.B());
            mode[0][m].Set(f * R[m], tt, a, sr);
            mode[1][m].Set(f * R[m] + d, tt * .95f, a, sr);
        }
        // Rubbing: the stick drives the lowest mode pair through the rim.
        const float d0 = split * (1.f + .2f * v * rng.B());
        rubpre.Set(f, 6.f, sr);
        rubm[0].SetNoise(f, 2.5f, .7f, sr);
        rubm[1].SetNoise(f + d0, 2.5f, .7f, sr);
        const float hard = Clamp(k.strike + v * .15f * rng.B(), 0.f, 1.f);
        p.Start(.006f * std::pow(.08f, hard), vel * (1.f - .3f * v * rng.U()), sr);
        ting.Start(vel * .02f * hard * hard, .01f, sr);
        tingf.Set(5000.f, 2.f, sr);
        rub = k.prep;
        rub_len = int((1.5f + 7.f * k.decay) * sr);
        t = 0;
        active = true; quiet = 0;
    }
    float Tick() override {
        const float x = p.Tick();
        float y = 0.f;
        if (rub > 0.f && t < rub_len + int(1.5f * sr)) {
            const float tt = float(t) / sr;
            const float env = Clamp((tt - .3f) / 1.8f, 0.f, 1.f) * (t < rub_len ? 1.f : 1.f - float(t - rub_len) / (1.5f * sr));
            const float n = rubpre.Bp(rng.B()) * env * rub * 4.f;
            y += rubm[0].Tick(n) + rubm[1].Tick(n);
        }
        for (int m = 0; m < M; ++m) { y += mode[0][m].Tick(x); y += mode[1][m].Tick(x); }
        ++t;
        return y + tingf.Bp(ting.Tick(rng));
    }
    bool Busy() const override { return p.Busy() || (rub > 0.f && t < rub_len + int(1.5f * sr)); }
};

// ---------------------------------------------------------------------------
// 10 · PULSE PIANO — In C's pulse, and Cage's preparations
// Two strings per note (waveguides with allpass tuning and a touch of
// stiffness), a felt hammer striking at an eighth of the length, a soundboard.
// Shimmer detunes the two strings (a honky-tonk at the top). Prep is a bolt
// and rubber between the strings: the tone goes short and metallic, a rattle
// rides on it, a prepared piano in the gamelan's direction.
struct Piano : Voice {
    Delay s[2], hd;
    OnePole lp[2];
    float D[2] = {}, g[2] = {}, apc = 0.f, apx[2] = {}, apy[2] = {};
    Res board[3], metal[4];
    Pulse ham;
    Burst thump;
    OnePole thumplp;
    float prep = 0.f, b1 = 0.f, th = .02f;
    int hpos = 10;
    static float ApDelay(float a, float w) {
        // H = (a + z^-1) / (1 + a z^-1)
        const float re = a + std::cos(w), im = -std::sin(w);
        const float re2 = 1.f + a * std::cos(w), im2 = -a * std::sin(w);
        float ph = std::atan2(im, re) - std::atan2(im2, re2);
        while (ph > 0.f) ph -= kTau;
        return -ph / w;
    }
    void Init(float rate, uint32_t seed) override {
        Voice::Init(rate, seed);
        s[0].Init(int(rate / 25.f)); s[1].Init(int(rate / 25.f)); hd.Init(int(rate / 25.f));
        board[0].Set(98.f, .25f, .5f, rate);
        board[1].Set(215.f, .14f, .35f, rate);
        board[2].Set(470.f, .07f, .25f, rate);
        thumplp.Set(220.f, rate);
    }
    void Strike(float f, float vel, const Knobs& k) override {
        const float v = k.feel;
        freq = f;
        vel *= 1.f - v * .3f * rng.U();
        prep = k.prep;
        const float det = (.4f + 9.f * k.shimmer * k.shimmer) * .5f;
        apc = -Clamp(.06f + .3f * f / 1200.f, .06f, .45f);
        float t60 = (1.6f + 14.f * std::pow(k.decay, 1.3f)) * std::pow(261.6f / f, .55f);
        t60 = Clamp(t60, .3f, 25.f) * (1.f - .85f * prep);
        for (int i = 0; i < 2; ++i) {
            const float fi = f * Cents(i ? det : -det);
            const float w = kTau * fi / sr;
            const float cut = Clamp((1200.f + 9000.f * k.bright * k.bright + 3.f * fi) * (1.f - .6f * prep), 600.f, 18000.f);
            lp[i].Set(cut, sr);
            const float a = 1.f - lp[i].k;
            const float hmag = lp[i].k / std::sqrt(1.f - 2.f * a * std::cos(w) + a * a);
            D[i] = sr / fi - OnePoleDelay(lp[i].k, w) - ApDelay(apc, w);
            g[i] = std::min(std::pow(10.f, -3.f / (fi * t60)) / hmag, .9995f);
        }
        hpos = std::max(1, int(D[0] / 8.f));
        const float hard = Clamp(k.strike * .8f + .2f * vel + v * .1f * rng.B(), 0.f, 1.f);
        ham.Start(.004f * std::pow(.12f, hard), vel * .9f, sr);
        thump.Start(vel * .15f, .03f, sr);
        th = .015f + .02f * (1.f - prep);
        static const float MR[4] = {2.37f, 3.91f, 5.62f, 7.3f};
        for (int m = 0; m < 4; ++m) metal[m].Set(std::min(f * MR[m], 9000.f), .35f - .05f * m, prep, sr);
        active = true; quiet = 0;
    }
    float Tick() override {
        const float e = ham.Tick();
        hd.Push(e);
        const float x = e - hd.Tap(hpos); // the hammer's point along the string
        float out = 0.f;
        for (int i = 0; i < 2; ++i) {
            const float y = s[i].All(std::max(1.f, D[i] - 1.f));
            const float z = apc * y + apx[i] - apc * apy[i]; // stiffness
            apx[i] = y; apy[i] = z;
            s[i].Push(x + g[i] * lp[i].Lp(z));
            out += y;
        }
        out *= .5f;
        if (prep > 0.f) {
            // The bolt: the string slaps it as it swings past.
            const float b = out - Clamp(out, -th, th);
            const float slap = (b - b1) * 6.f;
            b1 = b;
            float m = 0.f;
            for (auto& r : metal) m += r.Tick(slap + e * .5f);
            out += m * .6f;
        }
        float bd = 0.f;
        for (auto& r : board) bd += r.Tick(out * .06f);
        return out + bd + thumplp.Lp(thump.Tick(rng)) * 1.5f;
    }
    bool Busy() const override { return ham.Busy(); }
};

// ---------------------------------------------------------------------------
// 11 · HANG — a steel shell, touched with the fingers
// Each note field rings fundamental, octave and the twelfth (1 : 2 : 3), the
// steel a little sharp at first. The cavity hums under it. Shimmer: the shell
// sings along, other fields ringing in sympathy. Prep: the harmonic touch, a
// light finger in the centre of the field takes the fundamental away.
struct Hang : Voice {
    static constexpr int M = 5;
    Res mode[M], sym[3], gu;
    Pulse p;
    Burst slap;
    OnePole slaplp;
    float bend = 0.f, bend_mul = 1.f, base[M] = {};
    int t = 0;
    void Init(float s, uint32_t seed) override { Voice::Init(s, seed); slaplp.Set(1100.f, s); }
    void Strike(float f, float vel, const Knobs& k) override {
        static const float R[M] = {1.f, 2.f, 3.f, 4.12f, 5.07f}, A[M] = {1.f, .5f, .33f, .07f, .05f};
        if (f != freq) { for (auto& m : mode) m.Clear(); for (auto& m : sym) m.Clear(); }
        freq = f;
        const float v = k.feel;
        vel *= 1.f - v * .35f * rng.U();
        const float tilt = (k.bright - .5f) * 2.f;
        const float t60f = .8f * std::pow(10.f, k.decay);
        const float h = k.prep;
        float amps[M], norm = 0.f;
        for (int m = 0; m < M; ++m) {
            float a = A[m] * std::pow(R[m], tilt);
            if (m == 0) a *= 1.f - .93f * h;
            if (m == 1 || m == 2) a *= 1.f + .5f * h;
            amps[m] = a;
            norm += a * a;
        }
        norm = 1.f / std::sqrt(std::max(norm, .05f));
        for (int m = 0; m < M; ++m) {
            base[m] = f * R[m] * (1.f + .002f * v * rng.B());
            mode[m].Set(base[m], t60f * std::pow(R[m], -.6f), amps[m] * norm, sr);
        }
        static const float SR_[3] = {1.335f, 1.5f, 2.25f};
        for (int i = 0; i < 3; ++i) sym[i].Set(f * SR_[i], 2.5f + 2.f * k.decay, .1f * k.shimmer, sr);
        gu.Set(92.f, .5f, .12f, sr);
        const float hard = Clamp(k.strike + v * .2f * rng.B(), 0.f, 1.f);
        p.Start(.0045f * std::pow(.3f, hard) * (1.f - .5f * h), vel, sr);
        slap.Start(vel * .05f * hard, .006f, sr);
        bend = .005f * vel; // steel: a few cents sharp, settling
        bend_mul = std::exp(-32.f / (.06f * sr));
        t = 0;
        active = true; quiet = 0;
    }
    float Tick() override {
        if ((t++ & 31) == 0 && bend > 1e-5f) {
            bend *= bend_mul;
            for (int m = 0; m < M; ++m) mode[m].Retune(base[m] * (1.f + bend));
        }
        const float x = p.Tick();
        float y = gu.Tick(x);
        for (auto& m : mode) y += m.Tick(x);
        for (auto& m : sym) y += m.Tick(x * .5f);
        return y + slaplp.Lp(slap.Tick(rng));
    }
    bool Busy() const override { return p.Busy(); }
};

// ---------------------------------------------------------------------------
// 12 · TAPE — Discreet Music
// A soft synth tone already on tape: two detuned saws and a sub through a
// gentle lowpass, then the tape: wow and flutter, saturation, hiss, the highs
// lost to each generation. Strike is the fade-in, shimmer the detune, feel the
// wow, prep the tape's age (hiss, dropouts, duller).
struct Tape : Voice {
    float ph[3] = {}, dt[3] = {}, sub = 0.f;
    Svf lpf;
    Delay wowd;
    OnePole gen, hiss_hp, drop_s;
    int t = 0, att = 1, hold = 1, rel = 1;
    float lvl = 1.f, age = 0.f, wowd_amt = 0.f, wph = 0.f, fph = 0.f, drop = 1.f;
    static float Blep(float t, float dt) {
        if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
        if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
        return 0.f;
    }
    void Init(float s, uint32_t seed) override { Voice::Init(s, seed); wowd.Init(int(.03f * s)); hiss_hp.Set(3000.f, s); drop_s.Set(30.f, s); }
    void Strike(float f, float vel, const Knobs& k) override {
        freq = f;
        const float sp = 2.f + 14.f * k.shimmer;
        dt[0] = f * Cents(-sp) / sr; dt[1] = f * Cents(sp) / sr; dt[2] = .5f * f / sr;
        lpf.Set(Clamp(f * (1.2f + 9.f * k.bright * k.bright), 200.f, 9000.f), .75f, sr);
        const float slow = 1.f - k.strike;
        att = int((.006f + 1.4f * slow * slow) * sr);
        hold = int((.2f + 6.f * k.decay * k.decay) * sr);
        rel = int((.25f + 1.2f * k.decay) * sr);
        lvl = vel * .5f;
        age = k.prep;
        wowd_amt = .00035f + .0012f * k.feel;
        gen.Set(12000.f - 7500.f * age, sr);
        for (auto& p : ph) p = rng.U();
        wowd.Clear();
        t = 0;
        active = true; quiet = 0;
    }
    float Env() const {
        if (t < att) return float(t) / att;
        if (t < att + hold) return 1.f;
        const int r = t - att - hold;
        return r < rel ? 1.f - float(r) / rel : 0.f;
    }
    float Tick() override {
        const float e = Env();
        float x = 0.f;
        for (int i = 0; i < 2; ++i) {
            ph[i] += dt[i]; if (ph[i] >= 1.f) ph[i] -= 1.f;
            x += (2.f * ph[i] - 1.f - Blep(ph[i], dt[i])) * .5f;
        }
        ph[2] += dt[2]; if (ph[2] >= 1.f) ph[2] -= 1.f;
        x += .35f * Sin1(ph[2]);
        x = lpf.Lp(x) * e * lvl;
        // The tape: wow (slow) and flutter (fast) as a moving read head.
        wph += .55f / sr; fph += 6.8f / sr;
        if (wph >= 1.f) wph -= 1.f;
        if (fph >= 1.f) fph -= 1.f;
        wowd.Push(x);
        const float d = (.012f + wowd_amt * Sin1(wph) + .00004f * Sin1(fph)) * sr;
        float y = wowd.Lin(d);
        y = Tanh(1.6f * y) / 1.6f;
        // Age: dropouts (a brief dip), hiss, the highs going.
        if (age > .3f && rng.U() < (age - .3f) * .00004f) drop = .35f;
        drop += (1.f - drop) * .0004f;
        y = gen.Lp(y) * drop_s.Lp(drop);
        y += hiss_hp.Hp(rng.B()) * (.0015f + .006f * age) * (e > 0.f ? 1.f : 0.f);
        ++t;
        return y;
    }
    bool Busy() const override { return t < att + hold + rel + int(.03f * sr); }
};

} // namespace sd
