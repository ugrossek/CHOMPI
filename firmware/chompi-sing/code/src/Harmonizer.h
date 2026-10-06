#pragma once
#include <cmath>
#include "daisysp.h"
#include "MicFilter.h"
#include "PitchShifter.h"
#include "Vocoder.h"
#include "VoiceFreeze.h"

namespace chompi
{
    /** doubler delay lines (L, R), in SDRAM: chompi_main.cpp */
    static constexpr size_t kChorusLen = 2048; // power of two, ~42 ms
    extern float chorus_mem[2][kChorusLen];

    /** the vocoder's time-wheel history, in SDRAM: chompi_main.cpp */
    extern float vocoder_hist[Vocoder::kHistFrames][Vocoder::kBands];
    /** the real voice's recording for freeze and the time wheel (Human) */
    extern float voice_audio[VoiceFreeze::kLen];
    extern float voice_pitch[VoiceFreeze::kFrames];

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
                voices_[v].lpf   = Lp2();
                voices_[v].shift = 0.f;
                voices_[v].pan   = 0.f;
            }
            dcblock_.Init(samplerate);
            mic_filter_.Init(samplerate);
            sr_ = samplerate;
            latch_    = false;
            mode_     = ChordMode::Robot;
            pending_  = ChordMode::Robot;
            switch_gain_ = 1.f;
            size_     = .5f;
            vocoder_.Init(samplerate, vocoder_hist);
            vfreeze_.Init(samplerate, voice_audio, voice_pitch);
            human_frozen_ = false;
            human_mix_ = 0.f;
            rec_note_ = 0.f;
            SetSize(.5f);
            SetCharacter(.5f);
            ring_phase_ = 0.f;
            noise_    = 22222u;
            for (size_t v = 0; v < kVoices; v++)
                voices_[v].osc.phase = float(v) / kVoices; // not all in step
            sung_     = 60.f; // C4, key 8, until anything is sung
            heard_any_ = false;
            follow_on_ = true;

            /* the knob defaults in ui.h, so start-up matches the knobs even
               before a page is shown */
            SetTranspose(.5f);
            SetLevel(.75f);
            SetEnvelope(.4f);
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
            gate_up_   = 1.f / (.010f * samplerate);
            gate_down_ = 1.f / (.120f * samplerate);

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

        /** Envelope, one knob (chompi + knob 5): 0 short and plucky (attack
         *  2 ms, release 50 ms) .. 1 slow and swelling (200 ms, 3 s) */
        void SetEnvelope(float v)
        {
            attack_  = 1.f / (.002f * powf(100.f, v) * sr_);
            release_ = 1.f / (.05f * powf(60.f, v) * sr_);
        }

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

        /** Voice gate: when on, the harmonies only sound while the input
         *  has a voice or level (open; decided in DSPEngine.h), fading in
         *  over 10 ms and out over 120 ms, so room noise between phrases
         *  makes no harmonies. Applied to the output, so the voices' own
         *  history keeps running and they don't restart from silence. */
        void SetGateOn(bool on) { gate_on_ = on; }
        bool GateOn() const { return gate_on_; }
        void SetGateOpen(bool open) { gate_open_ = open; }

        /** How keys turn into voices (the play button switches):
         *  Robot: a vocoder: each key plays a synth note that the voice's
         *         words are imprinted on (Kraftwerk). The voice's pitch
         *         doesn't matter; talking works. No shifters run.
         *  Keys:  "Human": each key sounds its own note, made from the real
         *         voice, whatever is sung (needs the pitch detector).
         *  Key 8 is middle C (C4) in both. */
        enum class ChordMode { Robot, Keys, Toy };

        bool Vocoded() const { return mode_ != ChordMode::Keys; } // Robot, Toy
        /** switch character: the harmonies fade out, the switch happens in
         *  the silence, and they fade back in (Process) */
        void SetMode(ChordMode m) { pending_ = m; }

        void ApplyMode(ChordMode m)
        {
            /* a new character starts live */
            vocoder_.SetFreeze(false);
            vfreeze_.SetFreeze(false);
            human_frozen_ = false;
            human_mix_ = 0.f;
            mode_ = m;
            ApplySize();
            for (size_t i = 0; i < kVoices; i++)
                UpdateRatio(voices_[i], i);
        }
        ChordMode Mode() const { return pending_; }

        /** robot: "size", 0 monster .. .5 as sung .. 1 mouse (formants
         *  moved by up to 3 bands, about an octave, either way) */
        void SetSize(float v)
        {
            size_ = v;
            ApplySize();
        }
        void ApplySize() { vocoder_.SetShift((.5f - size_) * 6.f); }

        /** knob 3, "character": Robot's synth note, 0 soft (sine) .. .5
         *  Kraftwerk buzz (sawtooth) .. 1 whisper (noise); Human soft ..
         *  bright (HumanCharacter); Toy how much toy (Toy) */
        void SetCharacter(float v) { character_ = v; }

        /** Freeze and the time wheel, the same controls for every character
         *  that has them: robot holds the voice's band levels (Vocoder),
         *  keys (Human) the real voice (VoiceFreeze). */
        bool CanFreeze() const { return true; } // every character

        /** the voice recording, for the freeze dump (diagnosis) */
        const VoiceFreeze &VoiceRecording() const { return vfreeze_; }
        bool HumanFrozen() const { return mode_ == ChordMode::Keys && vfreeze_.Frozen(); }
        void SetFreeze(bool on)
        {
            if (Vocoded())
                vocoder_.SetFreeze(on);
            else if (mode_ == ChordMode::Keys)
            {
                /* the recording stays frozen until the fade back to the live
                   voice is over (Process), so the held sound doesn't slip */
                human_frozen_ = on;
                if (on)
                    vfreeze_.SetFreeze(true);
            }
        }
        bool Frozen() const
        {
            return Vocoded() ? vocoder_.Frozen() : human_frozen_;
        }

        /** time wheel: ms, positive = further back */
        void Scrub(float ms)
        {
            if (Vocoded())
                vocoder_.Scrub(ms); // one frame per 1 ms block
            else if (mode_ == ChordMode::Keys)
            {
                if (!human_frozen_)
                    SetFreeze(true);
                vfreeze_.Scrub(ms);
            }
        }
        float ScrubPosition() const
        {
            return Vocoded() ? vocoder_.ScrubPosition() : vfreeze_.Position();
        }


        /** keys mode: the detected sung pitch (MIDI note, fractional), once
         *  per block. Smoothed a little; kept through unvoiced gaps. */
        void SetSung(bool voiced, float note)
        {
            rec_note_ = voiced ? note : 0.f; // recorded with the voice
            if (mode_ == ChordMode::Keys && vfreeze_.Frozen())
            {
                /* frozen: the note that was sung where the wheel points */
                const float n = vfreeze_.Note();
                if (n > 0.f)
                    sung_ += (n - sung_) * kSungSmooth;
            }
            else if (voiced)
            {
                sung_ = heard_any_ ? sung_ + (note - sung_) * kSungSmooth : note;
                heard_any_ = true;
            }
            /* the voices move with the sung note in Human (fixed notes), and
               in every character while latched chords follow the voice */
            if (mode_ == ChordMode::Keys || Following())
                for (size_t i = 0; i < kVoices; i++)
                    if (voices_[i].gate || voices_[i].env > 0.f)
                        UpdateRatio(voices_[i], i);
        }

        /** Latched chords follow the voice (options.json, on by default):
         *  while latched, key 8 stands for the sung note and the other keys
         *  keep their distance from it, so the chord moves with the melody.
         *  Not latched, the keys play their own notes. */
        void SetFollow(bool on)
        {
            follow_on_ = on;
            for (size_t i = 0; i < kVoices; i++)
                UpdateRatio(voices_[i], i);
        }
        bool Following() const { return latch_ && follow_on_; }

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
            v->gate  = true;
            UpdateRatio(*v, size_t(v - voices_));
            duck_hold_ = duck_len_;
        }

        void NoteOff(int key)
        {
            duck_hold_ = duck_len_;
            if (latch_)
                return; // latched: released by the next press of the key
            if (Voice *v = Find(key))
            {
                v->gate = false;
            }
        }

        /** toggle switch: keep the voices of the held keys sounding after
         *  the keys are let go; switching it off releases everything */
        void SetLatch(bool on)
        {
            latch_ = on;
            for (size_t i = 0; i < kVoices; i++) // following starts or stops
                UpdateRatio(voices_[i], i);
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

            /* Human frozen: the voice comes from the recording; otherwise
               the live voice is recorded, in every character, so a freeze
               right after switching has something to hold */
            if (mode_ == ChordMode::Keys && vfreeze_.Frozen())
            {
                /* cross-fade live <-> frozen over kHumanFade, equal power:
                   the frozen moment may be an earlier note than the one
                   being sung, and two different sounds summed by amplitude
                   dip in the middle */
                float g[size];
                vfreeze_.Play(g, size);
                const float target = human_frozen_ ? 1.f : 0.f;
                const float step   = 1.f / (kHumanFade * sr_);
                for (size_t i = 0; i < size; i++)
                {
                    human_mix_ += human_mix_ < target ? step : -step;
                    human_mix_ = human_mix_ < 0.f ? 0.f : (human_mix_ > 1.f ? 1.f : human_mix_);
                    const float a = human_mix_ * 1.5707963f;
                    in_[i] = in_[i] * cosf(a) + g[i] * sinf(a);
                }
                if (!human_frozen_ && human_mix_ <= 0.f)
                    vfreeze_.SetFreeze(false); // faded out: record again
            }
            else
                vfreeze_.Record(in_, size, rec_note_, duck_hold_ <= 0 && duck_ >= .98f);
            in_now_ = in_;

            float hl[size], hr[size];
            for (size_t i = 0; i < size; i++)
                hl[i] = hr[i] = 0.f;

            /* Robot and Toy: the voices build the vocoder's carriers */
            const bool robot = Vocoded();
            const bool toy   = mode_ == ChordMode::Toy;
            float car_l[size], car_r[size];
            if (robot)
                for (size_t i = 0; i < size; i++)
                    car_l[i] = car_r[i] = 0.f;

            /* Human: shifting up moves the voice's formants up with the notes
               and brightens it (the chipmunk). Each voice gets a lowpass that
               closes with its upward shift; character towards bright eases
               it off. */
            const float bright = !robot && character_ > .5f ? (character_ - .5f) * 2.f : 0.f;

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

                if (robot)
                {
                    /* the voice's synth note, with a little noise so the
                       vocoder keeps "s" and "sh" (classic vocoders do too) */
                    vo.osc.SetFreq(vo.hz, sr_);
                    /* character: sine -> saw over the first half, saw ->
                       noise over the second; Toy keeps the buzz (its
                       character is how much toy, see Toy()) */
                    const float chr = toy ? .5f : character_;
                    const float m    = chr < .5f ? chr * 2.f : (chr - .5f) * 2.f;
                    const float wsin = chr < .5f ? 1.f - m : 0.f;
                    const float wsaw = chr < .5f ? m : 1.f - m;
                    const float wnoi = chr < .5f ? kRobotNoise : kRobotNoise + m;
                    for (size_t i = 0; i < size; i++)
                    {
                        if (vo.gate)
                            vo.env = vo.env + attack_ < 1.f ? vo.env + attack_ : 1.f;
                        else
                            vo.env = vo.env - release_ > 0.f ? vo.env - release_ : 0.f;
                        /* sine from the phase: sin(2 pi p) ~ -4u(1 - |u|), u = 2p - 1 */
                        const float u   = 2.f * vo.osc.phase - 1.f;
                        const float sn  = -4.f * u * (1.f - fabsf(u)) * 1.4f; // ~ the saw's level in the vocoder
                        const float saw = vo.osc.Process();
                        const float c   = (wsin * sn + wsaw * saw + wnoi * Noise()) * vo.env * kVocodedLevel;
                        car_l[i] += c * gl;
                        car_r[i] += c * gr;
                    }
                }
                else
                {
                    /* a 12 dB/octave lowpass that closes as the voice is
                       shifted up, so the shifted voice reaches about as high
                       as the sung one: shifting moves the voice's 2-6 kHz
                       up into the very highs, which a gentle shelf barely
                       touched (measured) */
                    const float up = vo.shift + transpose_;
                    float fc = up > 0.f ? kChipmunkTop * exp2f(-up * kChipmunkSlope / 12.f) : kChipmunkTop;
                    fc = fc < kChipmunkFloor ? kChipmunkFloor : fc;
                    fc += (kChipmunkTop - fc) * bright;
                    Lowpass(vo.lpf, fc);
                    for (size_t i = 0; i < size; i++)
                    {
                        if (vo.gate)
                            vo.env = vo.env + attack_ < 1.f ? vo.env + attack_ : 1.f;
                        else
                            vo.env = vo.env - release_ > 0.f ? vo.env - release_ : 0.f;

                        float l = in_[i], r = in_[i];
                        vo.shifter.Process(vo.ratio, &l, &r);
                        l = vo.lpf.Run(0, l);
                        r = vo.lpf.Run(1, r);
                        hl[i] += l * vo.env * gl;
                        hr[i] += r * vo.env * gr;
                    }
                }
                if (!vo.gate && vo.env <= 0.f)
                    vo.key = -1;
            }

            if (robot)
                vocoder_.Process(in_, car_l, car_r, hl, hr, size,
                                 spread_ > 0.f, Active());
            else
                HumanCharacter(hl, hr, bright, size);
            if (toy)
                Toy(hl, hr, size);

            /* switching character: fade out, switch in the silence, fade in */
            {
                const float step = 1.f / (kSwitchFade * sr_);
                for (size_t i = 0; i < size; i++)
                {
                    if (pending_ != mode_)
                        switch_gain_ = switch_gain_ - step > 0.f ? switch_gain_ - step : 0.f;
                    else
                        switch_gain_ = switch_gain_ + step < 1.f ? switch_gain_ + step : 1.f;
                    hl[i] *= switch_gain_;
                    hr[i] *= switch_gain_;
                }
                if (pending_ != mode_ && switch_gain_ <= 0.f)
                    ApplyMode(pending_);
            }

            /* a freeze keeps sounding when the voice stops: the gate stays open */
            const float gate_target = !gate_on_ || gate_open_ || Frozen() ? 1.f : 0.f;
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
        /** a 2-pole lowpass (Q .707), stereo */
        struct Lp2
        {
            float b0 = 1.f, b1 = 0.f, a1 = 0.f, a2 = 0.f; // b2 = b0
            float z1[2] = {0.f, 0.f}, z2[2] = {0.f, 0.f};
            float fc = -1.f;
            float Run(int c, float x)
            {
                const float y = b0 * x + z1[c];
                z1[c] = b1 * x - a1 * y + z2[c];
                z2[c] = b0 * x - a2 * y;
                return y;
            }
        };
        struct Voice
        {
            StereoPitchShifter shifter;
            SawOsc             osc;   // robot mode
            float              hz;    // robot mode: the note, with transpose
            int                key;
            float              ratio;
            float              semis; // the key, from middle C
            float              shift; // semitones this voice shifts by, before transpose
            float              pan;
            float              env;
            bool               gate;
            Lp2                lpf;        // Human: the chipmunk compensation
        };

        /** pitch = key + transpose, plus a few cents of alternating detune
         *  when the doubler is up, so stacked voices thicken */
        void UpdateRatio(Voice &v, size_t idx)
        {
            /* Human: from the sung note to the key's (key 8 = C4), or, while
               following, the key's distance from key 8 */
            const bool follow = Following();
            v.shift = follow ? v.semis : (60.f + v.semis) - sung_;
            const float cents = (idx & 1 ? 1.f : -1.f) * doubler_ * 12.f;
            v.ratio = powf(2.f, (v.shift + transpose_ + cents / 100.f) / 12.f);
            /* vocoded: the key's note, or, while following, the sung note
               plus the key's distance from key 8 */
            const float note = (follow ? sung_ : 60.f) + v.semis;
            v.hz    = 440.f * powf(2.f, (note - 69.f + transpose_ + cents / 100.f) / 12.f);
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
                if (voices_[o].shift < voices_[v].shift
                    || (voices_[o].shift == voices_[v].shift && o < v))
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
        /** Human's size and character (the robot's are in the vocoder):
         *  character left of centre softens (a lowpass, 12 kHz down to
         *  800 Hz), right of centre brightens (the chipmunk compensation
         *  eases off, see Process, and a little presence above ~2 kHz);
         *  size tilts the tone, left darker and fuller (bigger), right
         *  brighter and thinner (smaller). */
        void HumanCharacter(float *hl, float *hr, float bright, size_t size)
        {
            if (character_ < .49f)
            {
                const float fc   = 800.f * powf(15.f, character_ * 2.f);
                const float coef = 1.f - expf(-6.2831853f * fc / sr_);
                for (size_t i = 0; i < size; i++)
                {
                    soft_l_ += coef * (hl[i] - soft_l_);
                    soft_r_ += coef * (hr[i] - soft_r_);
                    hl[i] = soft_l_;
                    hr[i] = soft_r_;
                }
            }
            else if (bright > 0.f)
            {
                const float g = kPresence * bright;
                for (size_t i = 0; i < size; i++)
                {
                    pres_l_ += kPresenceCoef * (hl[i] - pres_l_);
                    pres_r_ += kPresenceCoef * (hr[i] - pres_r_);
                    hl[i] += (hl[i] - pres_l_) * g;
                    hr[i] += (hr[i] - pres_r_) * g;
                }
            }

            const float t = (.5f - size_) * 2.f; // + bigger, - smaller
            if (fabsf(t) > .01f)
            {
                const float lo = 1.f + .7f * t, hi = 1.f - .7f * t;
                const float norm = 1.f / (1.f + .25f * fabsf(t));
                for (size_t i = 0; i < size; i++)
                {
                    tilt_l_ += kTiltCoef * (hl[i] - tilt_l_);
                    tilt_r_ += kTiltCoef * (hr[i] - tilt_r_);
                    hl[i] = (tilt_l_ * lo + (hl[i] - tilt_l_) * hi) * norm;
                    hr[i] = (tilt_r_ * lo + (hr[i] - tilt_r_) * hi) * norm;
                }
            }
        }

        /** Toy: the robot as an 80s talking toy. Character is how much:
         *  fewer samples and bits (Speak & Spell, 48 kHz down to ~5 kHz,
         *  16 bits down to 5) and a ring modulator at a low, Dalek-ish
         *  frequency (metal). */
        void Toy(float *hl, float *hr, size_t size)
        {
            const float t    = character_;
            const float step = exp2f(-t * 3.2f);
            const float q    = exp2f(15.f - t * 11.f);
            const float mix  = .75f * t;
            const float inc  = (45.f + 40.f * t) / sr_;
            /* a one-pole near half the reduced sample rate */
            const float smooth = 1.f - expf(-6.2831853f * .45f * step * .5f);
            for (size_t i = 0; i < size; i++)
            {
                toy_acc_ += step;
                if (toy_acc_ >= 1.f)
                {
                    toy_acc_ -= 1.f;
                    toy_l_ = roundf(hl[i] * q) / q;
                    toy_r_ = roundf(hr[i] * q) / q;
                }
                /* smooth the steps, as the real toy's output filter did:
                   held samples alone spray images into the highs */
                toy_sl_ += smooth * (toy_l_ - toy_sl_);
                toy_sr_ += smooth * (toy_r_ - toy_sr_);
                const float u = 2.f * ring_phase_ - 1.f;
                const float m = -4.f * u * (1.f - fabsf(u)); // ~ sin(2 pi phase)
                const float g = 1.f - mix + mix * m;
                hl[i] = toy_sl_ * g;
                hr[i] = toy_sr_ * g;
                ring_phase_ += inc;
                if (ring_phase_ >= 1.f)
                    ring_phase_ -= 1.f;
            }
        }

        static constexpr float kTiltCoef     = .123f; // one pole, ~1 kHz at 48 kHz
        static constexpr float kPresenceCoef = .23f;  // one pole, ~2 kHz
        static constexpr float kPresence     = .4f;   // ~+3 dB above it, fully bright
        /* the chipmunk compensation: the lowpass starts at kChipmunkTop and
           closes kChipmunkSlope octaves per octave of upward shift */
        static constexpr float kChipmunkTop   = 14000.f;
        static constexpr float kChipmunkSlope = 1.f;
        static constexpr float kChipmunkFloor = 3000.f;

        void Lowpass(Lp2 &f, float fc)
        {
            if (fabsf(fc - f.fc) < 1.f)
                return;
            f.fc = fc;
            const float w = 6.2831853f * fc / sr_, al = sinf(w) * .7071068f, c = cosf(w), a0 = 1.f + al;
            f.b0 = (1.f - c) * .5f / a0;
            f.b1 = (1.f - c) / a0;
            f.a1 = -2.f * c / a0;
            f.a2 = (1.f - al) / a0;
        }
        float soft_l_ = 0.f, soft_r_ = 0.f, tilt_l_ = 0.f, tilt_r_ = 0.f;
        float pres_l_ = 0.f, pres_r_ = 0.f;
        float toy_acc_ = 0.f, toy_l_ = 0.f, toy_r_ = 0.f, toy_sl_ = 0.f, toy_sr_ = 0.f;
        const float *in_now_ = nullptr; // this block's voice

        /** white noise, -1..1 (xorshift) */
        float Noise()
        {
            noise_ ^= noise_ << 13;
            noise_ ^= noise_ >> 17;
            noise_ ^= noise_ << 5;
            return float(int32_t(noise_)) * (1.f / 2147483648.f);
        }

        static constexpr float kRobotNoise   = .05f;
        /* the vocoded characters came out quieter than Human on CHOMPI */
        static constexpr float kVocodedLevel = 1.4f; // ~ +3 dB

        Vocoder          vocoder_;
        VoiceFreeze      vfreeze_;
        bool             human_frozen_;
        float            human_mix_;                   // 0 live .. 1 frozen
        static constexpr float kHumanFade = .12f;      // s
        float            rec_note_;
        float            character_, size_;
        ChordMode        pending_;
        float            switch_gain_;
        static constexpr float kSwitchFade = .025f; // s, each way
        float            ring_phase_; // Toy's metal
        uint32_t         noise_;
        daisysp::DcBlock dcblock_;
        MicFilter        mic_filter_;
        float            attack_, release_;
        bool             latch_;
        ChordMode        mode_;
        float            sung_; // smoothed sung note, keys mode
        bool             heard_any_, follow_on_;
        static constexpr float kSungSmooth = .12f; // per 1 ms block: ~8 ms
        float            sr_, transpose_, level_, doubler_, spread_;
        float            lfo_[2];
        size_t           chorus_w_;
        float            duck_, duck_down_, duck_up_;
        bool             gate_on_, gate_open_;
        float            gate_, gate_up_, gate_down_;
        int              duck_hold_, duck_len_;
    };

} // namespace chompi
