#pragma once
// CIRCLES · the second set: electronic instruments. The drum machine for the
// rhythm keys, FM, acid and chip sounds for the melodies. Every one is struck
// and dies away, as the first set's are, and answers to the same six knobs:
//   K1 strike   how hard it is set going (the snap, the accent, the blip)
//   K2 bright   its colour (a filter, a waveform, the FM index)
//   K3 decay    how long it sounds
//   CHOMPI + K1 shimmer  what makes it beat or double
//   CHOMPI + K2 feel     the player: no two notes alike
//   CHOMPI + K3 prep     the machine's own trick (the zap of a drum, the
//                        claps of a clap, the arpeggio of a chip, the
//                        resonance of the acid, the ratio of the FM ...)
// Everything is cheap: sines from two parabolas, polyBLEP oscillators,
// state-variable filters set at most every 8 samples, no delay lines.

#include "instruments.h"

namespace sd {

// An envelope: a short straight rise from wherever it is (no click when it is
// struck again while sounding), then an exponential fall.
struct AD {
    float y = 0.f, up = 0.f, mul = 1.f;
    int n = 0;
    void Start(float level, float att_s, float t60, float sr) {
        n = std::max(1, int(att_s * sr));
        up = (level - y) / float(n);
        mul = std::exp(-6.9078f / (std::max(t60, .002f) * sr));
    }
    void Fall(float t60, float sr) { mul = std::exp(-6.9078f / (std::max(t60, .002f) * sr)); }
    float Tick() {
        if (n > 0) { y += up; --n; }
        else y *= mul;
        return y;
    }
};

// polyBLEP: the step of a saw or a square, smoothed over one sample each side.
inline float Blep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}
inline float SawB(float ph, float dt) { return 2.f * ph - 1.f - Blep(ph, dt); }
inline float PulseB(float ph, float dt, float duty) {
    float y = ph < duty ? 1.f : -1.f;
    y += Blep(ph, dt);
    float q = ph - duty;
    if (q < 0.f) q += 1.f;
    return y - Blep(q, dt);
}
inline float Wrap(float p) { return p >= 1.f ? p - 1.f : p; }

// ---------------------------------------------------------------------------
// DRUM — a tuned sine with a falling pitch: the 808's kick and toms.
// Strike: how far the pitch falls (the punch) and the beater's click. Bright:
// drive, from a round sine to a hard square-ish thump. Prep: how long the fall
// takes, from a punch to a laser zap. Shimmer: the head's second mode
// (1.59 times up), a tom's ring. The note sets the pitch: a kick two octaves
// under the key, a tom one.
struct Drum : Voice {
    float div = 4.f;
    float ph = 0.f, ph2 = 0.f, f0 = 0.f, sweep = 0.f, sweep_mul = 1.f, drive = 1.f, norm = 1.f, ring = 0.f;
    AD amp, ring_env;
    Burst click;
    OnePole click_hp;
    void Strike(float f, float vel, const Knobs& k) override {
        const float v = k.feel;
        if (!active) { ph = ph2 = 0.f; amp.y = ring_env.y = 0.f; }
        freq = f;
        f0 = f / div * Cents(v * 18.f * rng.B());
        vel *= 1.f - v * .3f * rng.U();
        sweep = .3f + 6.f * k.strike * k.strike;
        sweep_mul = std::exp(-1.f / ((.006f + .3f * k.prep * k.prep) * sr));
        drive = 1.f + 7.f * k.bright * k.bright;
        norm = 1.f / Tanh(drive);
        const float t60 = (.06f + 2.2f * k.decay * k.decay) * (1.f + v * .2f * rng.B());
        amp.Start(vel, .0006f, t60, sr);
        ring = .6f * k.shimmer;
        ring_env.Start(vel, .0006f, t60 * .45f, sr);
        click.Start(vel * (.05f + .5f * k.strike), .004f, sr);
        click_hp.Set(1500.f, sr);
        active = true;
        quiet = 0;
    }
    float Tick() override {
        const float fn = f0 * (1.f + sweep);
        sweep *= sweep_mul;
        ph = Wrap(ph + fn / sr);
        float y = Sin1(ph);
        if (ring > 0.f) { ph2 = Wrap(ph2 + 1.59f * fn / sr); y += ring * ring_env.Tick() * Sin1(ph2); }
        y = Tanh(y * drive) * norm;
        return y * amp.Tick() + click_hp.Hp(click.Tick(rng));
    }
};
struct Kick : Drum { Kick() { div = 4.f; } };
struct Tom : Drum { Tom() { div = 2.f; } };

// ---------------------------------------------------------------------------
// NOISE — a snare and a clap: noise through a band tuned to the key, with a
// head under it. Strike: the snap (how loud the head and the first crack).
// Bright: where the band sits. Shimmer: how narrow it is, up to a ring on the
// key's own pitch. Prep: the claps, one hit (a snare) up to four quick ones
// and the tail after them (a clap).
struct Noise : Voice {
    Svf band;
    OnePole lp;
    AD amp, head;
    float hph = 0.f, hf = 0.f, head_lvl = 0.f, qgain = 1.f, vel0 = 0.f, tail = .2f;
    int bursts = 0, gap = 0;
    void Strike(float f, float vel, const Knobs& k) override {
        const float v = k.feel;
        freq = f;
        vel *= 1.f - v * .3f * rng.U();
        vel0 = vel;
        const float fc = Clamp(f * (1.5f + 22.f * k.bright * k.bright) * Cents(v * 60.f * rng.B()), 300.f, 15000.f);
        const float q = .6f + 14.f * k.shimmer * k.shimmer;
        band.Set(fc, q, sr);
        qgain = std::sqrt(q) * 1.4f;
        lp.Set(9000.f, sr);
        tail = (.05f + 1.2f * k.decay * k.decay) * (1.f + v * .2f * rng.B());
        bursts = int(k.prep * 3.99f); // the claps before the tail
        gap = 0;
        hf = f * .5f;
        head_lvl = .9f * k.strike * (1.f - .7f * k.prep);
        if (!active) hph = 0.f;
        head.Start(vel, .0008f, .05f + .12f * k.decay, sr);
        if (bursts == 0) amp.Start(vel, .0008f, tail, sr);
        active = true;
        quiet = 0;
    }
    float Tick() override {
        if (bursts > 0 && --gap <= 0) {
            // A quick clap, a little uneven, then the next; after the last one, the tail.
            --bursts;
            const bool last = bursts == 0;
            amp.Start(vel0 * (last ? 1.f : .85f), .0004f, last ? tail : .012f, sr);
            gap = int((.0075f + .004f * rng.U()) * sr);
        }
        const float n = rng.B();
        float y = lp.Lp(band.Bp(n) * qgain) * amp.Tick();
        if (head_lvl > 0.f) { hph = Wrap(hph + hf / sr); y += head_lvl * Sin1(hph) * head.Tick(); }
        return y;
    }
    bool Busy() const override { return bursts > 0; }
};

// ---------------------------------------------------------------------------
// METAL — the 808's six square waves at odd ratios through a band: from a
// cowbell (two of them) to a hi-hat and a cymbal (all six). Strike: a crack of
// noise on the attack. Bright: the band, from a dull clank to a fizz.
// Shimmer: how many of the six sound. Prep: a ring modulator, the clang of
// struck sheet metal.
struct Metal : Voice {
    // The 808's oscillators (205.3, 304.4, 369.6, 522.7, 540, 800 Hz), the
    // cowbell's pair (540 and 800) last; against 540, which is the key's pitch.
    static constexpr float R[6] = {.3802f, .5637f, .6844f, .9680f, 1.f, 1.4815f};
    float ph[6] = {}, f6[6] = {}, w6[6] = {}, rph = 0.f, rf = 0.f, rmix = 0.f;
    Svf band;
    OnePole hp;
    AD amp, crack_env;
    void Strike(float f, float vel, const Knobs& k) override {
        const float v = k.feel;
        freq = f;
        vel *= 1.f - v * .3f * rng.U();
        const float base = f * 2.f * Cents(v * 25.f * rng.B());
        const float many = k.shimmer * 4.f; // 0: the pair .. 4: all six
        for (int i = 0; i < 6; ++i) {
            f6[i] = base * R[i];
            const float from = i >= 4 ? 0.f : float(3 - i);
            w6[i] = i >= 4 ? 1.f : Clamp(many - from, 0.f, 1.f);
        }
        const float fc = Clamp(base * (.9f + 9.f * k.bright * k.bright), 400.f, 14000.f);
        band.Set(fc, 1.4f, sr);
        hp.Set(fc * .35f, sr);
        rf = base * 1.17f;
        rmix = k.prep;
        const float t60 = (.03f + 1.6f * k.decay * k.decay) * (1.f + v * .2f * rng.B());
        amp.Start(vel, .0005f, t60, sr);
        crack_env.Start(vel * k.strike * .8f, .0003f, .01f, sr);
        active = true;
        quiet = 0;
    }
    float Tick() override {
        float s = 0.f;
        for (int i = 0; i < 6; ++i) {
            if (w6[i] <= 0.f) continue;
            const float dt = f6[i] / sr;
            ph[i] = Wrap(ph[i] + dt);
            // The cowbell's pair smoothed; the four high ones plain (a hat is noise anyway).
            s += w6[i] * (i >= 4 ? PulseB(ph[i], dt, .5f) : ph[i] < .5f ? 1.f : -1.f);
        }
        if (rmix > 0.f) { rph = Wrap(rph + rf / sr); s *= 1.f - rmix + rmix * 1.6f * Sin1(rph); }
        s = s * .3f + crack_env.Tick() * rng.B();
        return hp.Hp(band.Bp(s)) * amp.Tick() * 1.8f;
    }
};

// ---------------------------------------------------------------------------
// CHIP — a game console's pulse and triangle, its volume in 16 steps.
// Strike: the blip, a jump up in pitch at the start. Bright: the wave, a
// stepped triangle (dark) then a pulse from square to thin. Shimmer: vibrato,
// coming in after a moment. Prep: the arpeggio, none, the octave, or a major
// chord, a step every 1/60 s.
struct Chip : Voice {
    float ph = 0.f, f = 0.f, duty = .5f, blip = 0.f, blip_mul = 1.f, vib = 0.f, vph = 0.f;
    bool tri = false;
    int arp_n = 1, arp_i = 0, frame = 0, age = 0;
    float arp[3] = {1.f, 1.f, 1.f};
    AD amp;
    void Strike(float fr, float vel, const Knobs& k) override {
        const float v = k.feel;
        freq = fr;
        f = fr * Cents(v * 12.f * rng.B());
        vel *= 1.f - v * .3f * rng.U();
        tri = k.bright < .2f;
        duty = .5f - .375f * Clamp((k.bright - .2f) / .8f, 0.f, 1.f);
        blip = k.strike * k.strike * 3.f; // up to two octaves
        blip_mul = std::exp(-1.f / (.012f * sr));
        vib = .025f * k.shimmer; // up to about 40 cents
        arp_n = k.prep < .34f ? 1 : k.prep < .67f ? 2 : 3;
        arp[1] = arp_n == 2 ? 2.f : 1.2599f; // the octave, or the major third
        arp[2] = 1.4983f;                    // the fifth
        arp_i = 0;
        frame = 0;
        age = 0;
        if (!active) ph = 0.f;
        amp.Start(vel, .001f, (.05f + 2.f * k.decay * k.decay) * (1.f + v * .2f * rng.B()), sr);
        active = true;
        quiet = 0;
    }
    float Tick() override {
        if (++frame >= int(sr / 60.f)) { frame = 0; if (arp_n > 1) arp_i = (arp_i + 1) % arp_n; }
        float fn = f * (1.f + blip) * arp[arp_i];
        blip *= blip_mul;
        if (vib > 0.f && ++age > int(.15f * sr)) { vph = Wrap(vph + 5.5f / sr); fn *= 1.f + vib * Sin1(vph); }
        const float dt = fn / sr;
        ph = Wrap(ph + dt);
        float y;
        if (tri) {
            const float t = ph < .5f ? 4.f * ph - 1.f : 3.f - 4.f * ph;
            y = std::floor(t * 7.5f + 8.f) / 7.5f - 1.f + 1.f / 15.f; // 16 steps
        } else {
            y = PulseB(ph, dt, duty);
        }
        const float e = std::floor(amp.Tick() * 15.f + .5f) / 15.f; // 16 volume steps
        return y * e * .6f;
    }
};

// ---------------------------------------------------------------------------
// ACID — a saw through a resonant lowpass that snaps open and closes again:
// the 303. Strike: the accent (how far the filter opens). Bright: where it
// sits. Prep: the resonance, up to the squelch. Shimmer: a second saw, a
// little apart, for a wide stab.
struct Acid : Voice {
    float ph = 0.f, ph2 = 0.f, f = 0.f, cut = 500.f, env = 0.f, env_mul = 1.f, amt = 0.f, q = 1.f, mix2 = 0.f, comp = 1.f;
    int ctl = 0;
    Svf a, b;
    AD amp;
    void Strike(float fr, float vel, const Knobs& k) override {
        const float v = k.feel;
        freq = fr;
        f = fr * Cents(v * 8.f * rng.B());
        vel *= 1.f - v * .3f * rng.U();
        cut = 70.f * std::pow(2.f, 7.f * k.bright);           // 70 Hz .. 9 kHz
        amt = 5.f * k.strike * (1.f - v * .3f * rng.U());     // octaves the filter opens
        env = 1.f;
        const float t = (.04f + .9f * k.decay * k.decay) * (1.f + v * .2f * rng.B());
        env_mul = std::exp(-1.f / (t * .35f * sr));
        q = .7f + 11.f * k.prep * k.prep;
        comp = 1.f / (1.f + .12f * q);
        mix2 = .8f * k.shimmer;
        if (!active) { ph = 0.f; ph2 = .37f; a.Clear(); b.Clear(); }
        ctl = 0;
        amp.Start(vel, .001f, t * 1.6f + .05f, sr);
        active = true;
        quiet = 0;
    }
    float Tick() override {
        if (ctl-- <= 0) {
            ctl = 7;
            const float fc = Clamp(cut * std::exp2(amt * env), 40.f, 16000.f);
            a.Set(fc, .707f, sr);
            b.Set(fc, q, sr);
        }
        env *= env_mul;
        float dt = f / sr;
        ph = Wrap(ph + dt);
        float x = SawB(ph, dt);
        if (mix2 > 0.f) {
            const float dt2 = dt * 1.0069f; // 12 cents up
            ph2 = Wrap(ph2 + dt2);
            x = (x + mix2 * SawB(ph2, dt2)) / (1.f + .5f * mix2);
        }
        const float y = b.Lp(a.Lp(x));
        return Tanh(1.6f * y * comp) * amp.Tick() * .7f;
    }
};

// ---------------------------------------------------------------------------
// FM — two sines, the one bending the other's phase: electric pianos, bells,
// basses, metal. Strike: the index at the attack and a tine's knock. Bright:
// the index it keeps. Prep: the ratio of the two, in steps (1/2, 1, 2, 3, 3.5,
// 1.41, 5, 7). Shimmer: a second carrier a few cents up, the chorus.
struct Fm : Voice {
    static constexpr float kRatio[8] = {.5f, 1.f, 2.f, 3.f, 3.5f, 1.4142f, 5.f, 7.f};
    float pc = 0.f, pc2 = 0.f, pm = 0.f, pt = 0.f, f = 0.f, ratio = 1.f, i_att = 0.f, i_keep = 0.f, mix2 = 0.f, tine = 0.f;
    AD amp, idx, tine_env;
    void Strike(float fr, float vel, const Knobs& k) override {
        const float v = k.feel;
        freq = fr;
        f = fr * Cents(v * 6.f * rng.B());
        vel *= 1.f - v * .3f * rng.U();
        ratio = kRatio[std::min(int(k.prep * 8.f), 7)];
        // The index in turns (one turn: 2 pi); it falls with the note, and less when soft.
        i_att = (.05f + 1.1f * k.strike * k.strike) * (.5f + .5f * vel);
        i_keep = .02f + .45f * k.bright * k.bright;
        const float t60 = (.15f + 5.f * k.decay * k.decay) * (1.f + v * .2f * rng.B());
        amp.Start(vel, .001f, t60, sr);
        idx.y = 0.f;
        idx.Start(1.f, .001f, .06f + .4f * k.decay, sr);
        tine = .25f * k.strike;
        tine_env.Start(vel, .0005f, .03f, sr);
        mix2 = .9f * k.shimmer;
        if (!active) { pc = pm = pt = 0.f; pc2 = .5f; }
        active = true;
        quiet = 0;
    }
    float Tick() override {
        pm = Wrap(pm + f * ratio / sr);
        const float I = i_keep + (i_att - i_keep) * idx.Tick();
        const float m = I * Sin1(pm);
        pc = Wrap(pc + f / sr);
        float y = Sin1(pc + m + 2.f); // (+2: the phase stays positive)
        if (mix2 > 0.f) {
            pc2 = Wrap(pc2 + f * 1.004f / sr); // 7 cents up
            y = (y + mix2 * Sin1(pc2 + m + 2.f)) / (1.f + .6f * mix2);
        }
        float out = y * amp.Tick();
        if (tine > 0.f) { pt = Wrap(pt + f * 14.f / sr); out += tine * Sin1(pt) * tine_env.Tick(); }
        return out * .8f;
    }
};

// ---------------------------------------------------------------------------
// GRIT — a lo-fi voice: FM with feedback, a wave folder and a bit crusher
// (dsp/Grit.h, with cheap sines). Strike: the modulator's ratio; at the top
// its feedback breaks into noise. Bright: the depth, then the folding. Prep:
// the crusher, down to a few bits and a stutter. Shimmer: the depth falls
// slowly (a long growl) or quickly (a pluck).
struct Grit : Voice {
    static constexpr float kR[8] = {.5f, 1.f, 1.5f, 2.f, 2.76f, 3.5f, 5.f, 7.13f};
    float pc = 0.f, pm = 0.f, prev = 0.f, fm = 0.f, fb = 0.f, index = 0.f, fold = 1.f;
    float hold = 0.f, hold_rate = 1.f, held = 0.f, levels = 0.f, stutter = 0.f;
    int stick = 0;
    AD amp, idx;
    void Strike(float fr, float vel, const Knobs& k) override {
        const float v = k.feel;
        freq = fr;
        vel *= 1.f - v * .3f * rng.U();
        const float t = Clamp(k.strike, 0.f, 1.f) * 7.f;
        const int i = std::min(int(t), 6);
        fm = fr * (kR[i] + (kR[i + 1] - kR[i]) * (t - i)) * (1.f + v * .04f * rng.B());
        fb = std::max(0.f, (k.strike - .75f) / .25f) * .25f;
        index = .95f * k.bright * k.bright * (1.f - v * .5f * rng.U());
        fold = k.bright > .6f ? 1.f + (k.bright - .6f) * 7.5f : 1.f;
        const float t60 = .03f * std::pow(200.f, k.decay);
        amp.Start(vel, .0015f, t60, sr);
        idx.y = 0.f;
        idx.Start(1.f, .001f, t60 * (.15f + .8f * k.shimmer), sr);
        hold_rate = std::pow(.04f, k.prep);
        levels = k.prep < .05f ? 0.f : std::exp2(15.f - 12.f * k.prep);
        stutter = std::max(0.f, (k.prep - .6f) / .4f);
        active = true;
        quiet = 0;
    }
    float Tick() override {
        pm = Wrap(pm + fm / sr);
        const float m = Sin1(pm + fb * prev + 1.f);
        prev = m;
        pc = Wrap(pc + freq / sr);
        float y = Sin1(pc + index * (.25f + .75f * idx.Tick()) * m + 1.f);
        if (fold > 1.f) y = Sin1(.25f * fold * y + 1.f);
        hold += hold_rate;
        if (hold >= 1.f) {
            hold -= 1.f;
            if (stick > 0) --stick;
            else {
                held = levels > 0.f ? std::floor(y * levels + .5f) / levels : y;
                if (stutter > 0.f && rng.U() < stutter * .002f) stick = int(rng.U() * stutter * 400.f);
            }
        }
        return held * amp.Tick() * .6f;
    }
};

} // namespace sd
