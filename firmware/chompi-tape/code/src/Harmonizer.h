#pragma once
#include <cmath>
#include "daisysp.h"
#include "MicFilter.h"
#include "PitchShifter.h"

namespace chompi
{
    /** SING prototype: live harmonizer.
     *
     *  Every held key gets a voice that pitch-shifts the live input by its
     *  distance from the middle C, so holding C and E while singing gives the
     *  voice and a third above it. The C key itself is unshifted (the shifter
     *  fades to dry at a ratio of 1, so it adds no delay).
     *
     *  The voices reuse the sample voices' shifter buffers (shift_mem,
     *  shift_ana): in this mode the sample voices never play. The dry voice is
     *  not added here -- TAPE's own mic monitor provides it, switched by the
     *  toggle switch as before.
     *
     *  Init() sets every member, so the object may live in DTCM, which is
     *  not zeroed at start-up (chompi_main.cpp puts it there: .bss is full).
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
            }
            dcblock_.Init(samplerate);
            mic_filter_.Init(samplerate);
            /* linear ramps, per sample */
            attack_  = 1.f / (.005f * samplerate);
            release_ = 1.f / (.150f * samplerate);
        }

        /** key: hardware key id (to match the note-off), semis: from middle C */
        void NoteOn(int key, float semis)
        {
            Voice *v = Find(key);       // retrigger of a held key
            if (!v) v = FindFree();
            if (!v) v = FindQuietest(); // steal
            if (v->key != key || v->env <= 0.f)
                v->shifter.Reset();     // fresh history: fades in from silence
            v->key   = key;
            v->ratio = powf(2.f, semis / 12.f);
            v->gate  = true;
        }

        void NoteOff(int key)
        {
            if (Voice *v = Find(key))
                v->gate = false;
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
            float in_[size]; /* one audio block, 48 samples */
            for (size_t i = 0; i < size; i++)
            {
                float x = dcblock_.Process(in[i]);
                if (mic)
                    x = mic_filter_.Process(x);
                in_[i] = x;
            }

            for (size_t v = 0; v < kVoices; v++)
            {
                Voice &vo = voices_[v];
                if (!vo.gate && vo.env <= 0.f)
                    continue;

                for (size_t i = 0; i < size; i++)
                {
                    if (vo.gate)
                        vo.env = vo.env + attack_ < 1.f ? vo.env + attack_ : 1.f;
                    else
                        vo.env = vo.env - release_ > 0.f ? vo.env - release_ : 0.f;

                    float l = in_[i], r = in_[i];
                    vo.shifter.Process(vo.ratio, &l, &r);
                    outl[i] += l * vo.env * kLevel;
                    outr[i] += r * vo.env * kLevel;
                }
                if (!vo.gate && vo.env <= 0.f)
                    vo.key = -1;
            }
        }

    private:
        struct Voice
        {
            StereoPitchShifter shifter;
            int                key;
            float              ratio;
            float              env;
            bool               gate;
        };

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

        static constexpr float kLevel = .7f;

        Voice            voices_[kVoices];
        daisysp::DcBlock dcblock_;
        MicFilter        mic_filter_;
        float            attack_, release_;
    };

} // namespace chompi
