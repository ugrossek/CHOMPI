#pragma once
#include <cmath>
#include "daisysp.h"
#include "MicFilter.h"
#include "PitchShifter.h"

namespace chompi
{
    /** doubler delay lines (L, R), in SDRAM: chompi_main.cpp */
    static constexpr size_t kChorusLen = 2048; // power of two, ~42 ms
    extern float chorus_mem[2][kChorusLen];

    /** SING's live harmonizer.
     *
     *  Every held key gets a voice that pitch-shifts the live input by its
     *  distance from the middle C, so holding C and E while singing gives the
     *  voice and a third above it. The C key itself is unshifted (the shifter
     *  fades to dry at a ratio of 1, so it adds no delay).
     *
     *  Each voice runs one StereoPitchShifter on its own delay line
     *  (shift_mem, shift_ana in chompi_main.cpp). The dry voice is not added
     *  here: the engine's mic monitor provides it, routed by the monitor mode.
     *
     *  Init() sets every member, so the object may live in DTCM, which is
     *  not zeroed at start-up.
     */
    template <size_t kVoices>
    class Harmonizer
    {
    public:
        void Init(float samplerate)
        {
            for (size_t v = 0; v < kVoices; v++)
            {
                voices_[v].shifter.Init(shift_mem[v], shift_ana[v]);
                voices_[v].key  = -1;
                voices_[v].env  = 0.f;
                voices_[v].gate = false;
                voices_[v].semis = 0.f;
                voices_[v].pan   = 0.f;
            }
            dcblock_.Init(samplerate);
            mic_filter_.Init(samplerate);
            sr_ = samplerate;
            latch_ = false;

            /* the knob defaults in ui.h, so start-up matches the knobs even
               before a page is shown */
            SetTranspose(.5f);
            SetLevel(.75f);
            SetAttack(.1f);
            SetRelease(.5f);
            SetDoubler(0.f);
            SetSpread(0.f);

            for (size_t c = 0; c < 2; c++)
                for (size_t i = 0; i < kChorusLen; i++)
                    chorus_mem[c][i] = 0.f; // SDRAM is not zeroed at start-up
            chorus_w_  = 0;
            lfo_[0]    = 0.f;
            lfo_[1]    = .37f;

            /* voice gate, see SetGate() */
            gate_on_   = false;
            gate_open_ = true;
            gate_      = 1.f;
            gate_up_   = 1.f / (.003f * samplerate);
            gate_down_ = 1.f / (.060f * samplerate);

            /* key-click ducking, see Duck() */
            duck_      = 1.f;
            duck_hold_ = 0;
            duck_len_  = int(.035f * samplerate);
            duck_down_ = 1.f / (.003f * samplerate);
            duck_up_   = 1.f / (.025f * samplerate);
        }

        /** Gain for the built-in mic, one value per sample of this block.
         *  The mic sits on the same board as the keys and hears every click,
         *  and a key press is exactly when a new voice starts. So for a moment
         *  after every key change the mic is pulled down (3 ms), held, and
         *  brought back (25 ms). */
        void Duck(float *gain, size_t size)
        {
            for (size_t i = 0; i < size; i++)
            {
                if (duck_hold_ > 0)
                {
                    duck_hold_--;
                    duck_ = duck_ - duck_down_ > kDuckFloor ? duck_ - duck_down_ : kDuckFloor;
                }
                else
                    duck_ = duck_ + duck_up_ < 1.f ? duck_ + duck_up_ : 1.f;
                gain[i] = duck_;
            }
        }

        /* ---- knobs, each 0..1 ------------------------------------------ */

        /** knob 1, page 1: continuous -12..+12 semitones on every voice,
         *  .5 = none. In the menu the knob steps through fifths and octaves. */
        void SetTranspose(float v)
        {
            transpose_ = (v - .5f) * 24.f;
            for (size_t i = 0; i < kVoices; i++)
                UpdateRatio(voices_[i], i);
        }
        float Transpose() const { return transpose_; }

        /** knob 1, page 2: harmony volume, .5 = about as loud as the dry voice */
        void SetLevel(float v) { level_ = v * 2.f * kLevel; }

        /** knob 2, page 2: 2 ms .. 500 ms */
        void SetAttack(float v) { attack_ = 1.f / (.002f * powf(250.f, v) * sr_); }

        /** knob 3, page 2: 20 ms .. 3 s */
        void SetRelease(float v) { release_ = 1.f / (.02f * powf(150.f, v) * sr_); }

        /** knob 3, page 1: chorus taps + slight detune per voice */
        void SetDoubler(float v)
        {
            doubler_ = v;
            for (size_t i = 0; i < kVoices; i++)
                UpdateRatio(voices_[i], i);
        }

        /** knob 2, page 1: held voices, ordered by pitch, go alternately left
         *  and right (lowest left), so highs and lows end up on both sides */
        void SetSpread(float v) { spread_ = v; }

        /** Voice gate: when on, the harmonies only sound while the pitch
         *  detector hears a voice (open), fading in over 3 ms and out over
         *  60 ms; clicks, breath and room noise then make no harmonies.
         *  Applied to the output: the detector needs ~30 ms to recognise a
         *  voice, about what the shifter delays the voices anyway, so gating
         *  the input would cut off the start of every syllable. */
        void SetGateOn(bool on) { gate_on_ = on; }
        bool GateOn() const { return gate_on_; }
        void SetGateOpen(bool open) { gate_open_ = open; }

        /** key: hardware key id (to match the note-off), semis: from middle C */
        void NoteOn(int key, float semis)
        {
            Voice *v = Find(key);       // retrigger of a held key
            if (latch_ && v && v->gate)
            {
                v->gate    = false;     // latched: a second press lets it go
                duck_hold_ = duck_len_;
                return;
            }
            if (!v) v = FindFree();
            if (!v) v = FindQuietest(); // steal
            if (v->key != key || v->env <= 0.f)
                v->shifter.Reset();     // fresh history: fades in from silence
            v->key   = key;
            v->semis = semis;
            UpdateRatio(*v, size_t(v - voices_));
            v->gate  = true;
            duck_hold_ = duck_len_;
        }

        void NoteOff(int key)
        {
            duck_hold_ = duck_len_;
            if (latch_)
                return; // latched: released by the next press of the key
            if (Voice *v = Find(key))
                v->gate = false;
        }

        /** toggle switch: keep the voices of the held keys sounding after
         *  the keys are let go; switching it off releases everything */
        void SetLatch(bool on)
        {
            latch_ = on;
            if (!on)
                AllOff();
        }
        bool Latched() const { return latch_; }

        /** release every voice (they ring out with the release time) */
        void AllOff()
        {
            for (size_t v = 0; v < kVoices; v++)
                voices_[v].gate = false;
        }

        /** a key that is held down right now (not just ringing out) */
        bool Held(int key) const
        {
            for (size_t v = 0; v < kVoices; v++)
                if (voices_[v].gate && voices_[v].key == key)
                    return true;
            return false;
        }

        bool Active() const
        {
            for (size_t v = 0; v < kVoices; v++)
                if (voices_[v].gate || voices_[v].env > 0.f)
                    return true;
            return false;
        }

        /** Adds the harmony voices into outl/outr.
         *  @param in    mono input, already scaled (mic or summed line in)
         *  @param mic   true: apply the mic filter as TAPE's monitor does */
        void Process(const float *in, bool mic, float *outl, float *outr, size_t size)
        {
            /* the shifters share a per-block budget for their splice search */
            StereoPitchShifter::NewBlock(size);

            float in_[size]; /* one audio block, 48 samples */
            for (size_t i = 0; i < size; i++)
            {
                float x = dcblock_.Process(in[i]);
                if (mic)
                    x = mic_filter_.Process(x);
                in_[i] = x;
            }

            float hl[size], hr[size];
            for (size_t i = 0; i < size; i++)
                hl[i] = hr[i] = 0.f;

            for (size_t v = 0; v < kVoices; v++)
            {
                Voice &vo = voices_[v];
                if (!vo.gate && vo.env <= 0.f)
                    continue;

                /* constant-power pan, glides so a voice that changes side
                   when a key is added doesn't jump */
                vo.pan += (PanTarget(v) - vo.pan) * .15f;
                const float a  = (vo.pan + 1.f) * .7853982f; // 0..pi/2
                const float gl = cosf(a) * 1.4142136f * level_;
                const float gr = sinf(a) * 1.4142136f * level_;

                for (size_t i = 0; i < size; i++)
                {
                    if (vo.gate)
                        vo.env = vo.env + attack_ < 1.f ? vo.env + attack_ : 1.f;
                    else
                        vo.env = vo.env - release_ > 0.f ? vo.env - release_ : 0.f;

                    float l = in_[i], r = in_[i];
                    vo.shifter.Process(vo.ratio, &l, &r);
                    hl[i] += l * vo.env * gl;
                    hr[i] += r * vo.env * gr;
                }
                if (!vo.gate && vo.env <= 0.f)
                    vo.key = -1;
            }

            const float gate_target = !gate_on_ || gate_open_ ? 1.f : 0.f;
            for (size_t i = 0; i < size; i++)
            {
                if (gate_ < gate_target)
                    gate_ = gate_ + gate_up_ < gate_target ? gate_ + gate_up_ : gate_target;
                else if (gate_ > gate_target)
                    gate_ = gate_ - gate_down_ > gate_target ? gate_ - gate_down_ : gate_target;
                hl[i] *= gate_;
                hr[i] *= gate_;
            }

            /* doubler: two slowly wandering taps (12 and 17 ms, +-3 ms),
               one per side, mixed in by the knob */
            const float lfo_inc = .35f / sr_;
            for (size_t i = 0; i < size; i++)
            {
                const float m = (hl[i] + hr[i]) * .5f;
                chorus_mem[0][chorus_w_] = m;

                float wet[2];
                for (size_t c = 0; c < 2; c++)
                {
                    lfo_[c] += lfo_inc * (c ? 1.3f : 1.f);
                    if (lfo_[c] >= 1.f)
                        lfo_[c] -= 1.f;
                    const float d = ((c ? .017f : .012f)
                                     + .003f * sinf(6.2831853f * lfo_[c])) * sr_;
                    const float rp = float(chorus_w_) - d;
                    const float fl = floorf(rp);
                    const size_t i0 = size_t(int(fl)) & (kChorusLen - 1);
                    const size_t i1 = (i0 + 1) & (kChorusLen - 1);
                    const float  fr = rp - fl;
                    wet[c] = chorus_mem[0][i0] + (chorus_mem[0][i1] - chorus_mem[0][i0]) * fr;
                }
                chorus_w_ = (chorus_w_ + 1) & (kChorusLen - 1);

                outl[i] += hl[i] + wet[0] * doubler_;
                outr[i] += hr[i] + wet[1] * doubler_;
            }
        }

    private:
        struct Voice
        {
            StereoPitchShifter shifter;
            int                key;
            float              ratio;
            float              semis;
            float              pan;
            float              env;
            bool               gate;
        };

        /** pitch = key + transpose, plus a few cents of alternating detune
         *  when the doubler is up, so stacked voices thicken */
        void UpdateRatio(Voice &v, size_t idx)
        {
            const float cents = (idx & 1 ? 1.f : -1.f) * doubler_ * 12.f;
            v.ratio = powf(2.f, (v.semis + transpose_ + cents / 100.f) / 12.f);
        }

        /** -1..1 for voice v: its rank by pitch among the sounding voices,
         *  alternating sides; a lone voice stays in the middle */
        float PanTarget(size_t v) const
        {
            int rank = 0, count = 0;
            for (size_t o = 0; o < kVoices; o++)
            {
                if (!voices_[o].gate && voices_[o].env <= 0.f)
                    continue;
                count++;
                if (voices_[o].semis < voices_[v].semis
                    || (voices_[o].semis == voices_[v].semis && o < v))
                    rank++;
            }
            if (count < 2)
                return 0.f;
            return (rank & 1 ? 1.f : -1.f) * spread_;
        }

        Voice *Find(int key)
        {
            for (size_t v = 0; v < kVoices; v++)
                if (voices_[v].key == key)
                    return &voices_[v];
            return nullptr;
        }

        Voice *FindFree()
        {
            for (size_t v = 0; v < kVoices; v++)
                if (voices_[v].key < 0)
                    return &voices_[v];
            return nullptr;
        }

        Voice *FindQuietest()
        {
            Voice *best = &voices_[0];
            for (size_t v = 1; v < kVoices; v++)
                if (!voices_[v].gate && (best->gate || voices_[v].env < best->env))
                    best = &voices_[v];
            return best;
        }

        static constexpr float kLevel     = 1.4f;  /* about as loud as the dry voice */
        static constexpr float kDuckFloor = .03f;  /* -30 dB while a key clicks */

        Voice            voices_[kVoices];
        daisysp::DcBlock dcblock_;
        MicFilter        mic_filter_;
        float            attack_, release_;
        bool             latch_;
        float            sr_, transpose_, level_, doubler_, spread_;
        float            lfo_[2];
        size_t           chorus_w_;
        float            duck_, duck_down_, duck_up_;
        bool             gate_on_, gate_open_;
        float            gate_, gate_up_, gate_down_;
        int              duck_hold_, duck_len_;
    };

} // namespace chompi
