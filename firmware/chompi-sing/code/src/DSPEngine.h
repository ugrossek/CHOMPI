/** DSPEngine
 *  Core DSP for sampling engine, looping engine, and additional DSP
 */
#pragma once
#include "daisy.h"
#include "LooperEngine.h"
#include "daisysp.h"
#include "DJFilter.h"
#include "Warble.h"
#include "EnvFollower.h"
#include "MicFilter.h"
#include "Harmonizer.h"
#include "PitchDetector.h"

/* SING: defined in chompi_main.cpp, in DTCM */
extern chompi::Harmonizer<7> harmonizer;
#include "reverb.h"
#include "RamBuffer.h"
#include "limiter.h"
#include "InterpolatedDelayLine.h"
#include <algorithm>

using namespace daisy;
static constexpr size_t kMaxDelayTime = 48128 * 2; // stereo, > 1 seconds at 48kHz

static constexpr float kLineOutGain = .3f;
static constexpr float kHpGain = .2f;
static constexpr float kMicGain = 5.f;
static constexpr float kLineInGain = 3.f;
static constexpr size_t kMaxPoly = 7;

namespace daisy
{
    enum class VUTarget
    {
        VU_INPUT = 0,
        VU_OUTPUT,
        VU_LAST
    };

    enum class InputSource
    {
        MIC = 0,
        LINE_IN,
        RESAMPLE,
        LAST,
    };

    enum class MonitorMode
    {
        HP = 0,
        BOTH,
        SEND_RET, // TAPE's; SING's menu does not offer it
        OFF,      // SING: no dry voice
        LAST,
    };

    struct KeyRequest
    {
        enum class Type
        {
            START,
            STOP,
            DUMMY,
        };

        Type type_;
        float transpose_nn_;
        int key_;
        float vel_;

        /** constructor for full request data */
        KeyRequest(Type type,
                    float transpose_nn,
                    int key,
                    float vel
                  )
            : type_(type),
                transpose_nn_(transpose_nn),
                key_(key),
                vel_(vel)
        {
        }

        /** Empty, invalid request */
        KeyRequest()
            : type_(Type::DUMMY),
                transpose_nn_(0.f),
                key_(0),
                vel_(127.f)
        {
        }


    };

    /** @brief core engine for running entire modules audio
     *
     *  For now now abstraction for various sampling modes,
     *  possibly that can all be handled in the UI.
     *
     *  This will build out CHOMPI Mode and add the looping engine, and other fixed engines
     */
    class Engine
    {
    public:
        Engine() {}
        ~Engine() {}

        void Init(
            float samplerate, 
            daisysp::Reverb* reverb, 
            chompi::InterpolatedDelayLine::AudioSample* del,
            RamBufferMemory* loop_buff,
            bool tape_slew,
            MonitorMode mon_mode)
        {
            monitor_mode = mon_mode;

            input_env_follower.Init();
            output_env_follower.Init();


            reverb_ = reverb;

            reverb_->Init(samplerate);
            reverb_->SetAmount(0.f);
            reverb_->SetInputGain(.3f);
            reverb_->SetLowpass(1.f);

            del_.Init(del, kMaxDelayTime);
            del_.SetDelay(kMaxDelayTime * .5f);

            SetDelayTime(.5f);
            dly_time_ = dly_time_target_;

            mic_filter_.Init(samplerate);
            harmonizer.Init(samplerate);
            pitch_.Init();

            filter_.Init(samplerate);
            filter_.SetControl(.5f);
            cutoff_target_ = .5f;
            res_ = res_target_ = 0.f;

            SetSaturate(0.f);

            mgain_ = mgain_target_ = .8f;

            warble_.Init(samplerate);
            warble_.SetFreq(.1f);
            
            dcblock_mic_in_.Init(samplerate);
            dcblock_line_in_l_.Init(samplerate);
            dcblock_line_in_r_.Init(samplerate);
            dcblock_fx_l_.Init(samplerate);
            dcblock_fx_r_.Init(samplerate);


            fx_pre_loop = true;
            fx_env_ = fx_env_target_ = 1.f;
            final_lim_ = 0.f;

            resamp_env_ = resamp_env_target_ = 1.f;


            /* looper */
            looper.Init(samplerate, loop_buff, tape_slew);

            /** Final output compressors */
            lim_hp_l_.Init();
            lim_hp_r_.Init();
            lim_line_l_.Init();
            lim_line_r_.Init();
        }

        // fill chompi buffer with 2 second long cosine
        void ApplyFx(float* outl, float* outr, size_t size)
        {
            for(size_t i = 0; i < size; i++)
            {
                // slew controls at audio rate
                fonepole(cutoff_, cutoff_target_, .001f);
                fonepole(res_, res_target_, .001f);
                fonepole(saturate_amt_, saturate_amt_target_, .001f);

                // input gain
                fonepole(fx_env_, fx_env_target_, .001f);
                outl[i] *= fx_env_;
                outr[i] *= fx_env_;

                filter_.SetControl(cutoff_);
                filter_.SetRes(res_);

                outl[i] = dcblock_fx_l_.Process(outl[i]);
                outr[i] = dcblock_fx_r_.Process(outr[i]);

                // filter
                filter_.Process(outl[i], outr[i], &outl[i], &outr[i]);

                // then clip
                outl[i] = daisysp::SoftClip(saturate_amt_ * outl[i]); 
                outr[i] = daisysp::SoftClip(saturate_amt_ * outr[i]); 

                // reduce amplitude to prevent LUFs from blowing up
                const float gain = 1.f - daisysp::SoftClip(.4f * (saturate_amt_ - 1.f)) * .7f;
                outl[i] *= gain;
                outr[i] *= gain;

                // wow and flutter
                warble_.Process(outl[i], outr[i], &outl[i], &outr[i]);
            }

            // delay, in its own block to optimize SDRAM access
            for(size_t i = 0; i < size; i++)
            {
                // fonepole and set controls
                // dly_amt_ is a duplicate of reverb_amt_, but we want to fonepole it in this loop
                fonepole(dly_feedback_, dly_feedback_target_, .001f);
                fonepole(dly_time_, dly_time_target_, .001f);
                fonepole(dly_amt_, dly_amt_target_, .001f); 
                del_.SetDelay(dly_time_); 

                // read
                const float del_vol = dly_feedback_ < .2f ? dly_feedback_ * 5.f : 1.f;
                InterpolatedDelayLine::AudioSample del_read = del_.Read();
                const float delsig_l = s162f(del_read.l) * del_vol;
                const float delsig_r = s162f(del_read.r) * del_vol;

                // write
                const float mono_sum = (outl[i] + outr[i]) * .5f;
                const float del_in = mono_sum + delsig_r * powf(dly_feedback_, .7f);
                const InterpolatedDelayLine::AudioSample del_write = {int16_t(f2s16(del_in)), int16_t(f2s16(delsig_l))};
                del_.Write(del_write);

                // dry/wet mix
                const float wet_mix = dly_feedback_ > .25f ? .5f : 2.f * dly_feedback_; // quickly to 50%
                const float dry_mix = dly_feedback_ > .83f ? .5f : (1 - .6f * dly_feedback_); // slowly to 50%

                outl[i] = outl[i] * dry_mix + delsig_l * wet_mix;
                outr[i] = outr[i] * dry_mix + delsig_r * wet_mix;
            }

            // reverb, in its own block to optimize SDRAM access
            for(size_t i = 0; i < size; i++)
            {
                // fonepole and set controls
                fonepole(reverb_amt_, reverb_amt_target_, .001f);
                fonepole(reverb_time_, reverb_time_target_, .001f);
                reverb_->SetAmount(reverb_amt_ * reverb_amt_ * .8f);
                reverb_->SetTime(reverb_time_);
                reverb_->SetLowpass(reverb_amt_ * .6f + .4f);
                reverb_->SetDiffusion(reverb_amt_ * .6f);

                reverb_->Process(&outl[i], &outr[i]);
            }
        }

        /** Apply the total monitor signal to the input envelope follower */
        void ApplyEnvelopeFollower(size_t size, float* monitor)
        {
            for (size_t i = 0; i < size; i++)
                input_env_follower.Process((monitor[i] + monitor[size + i]) * .8f);
        }

        /** add the mic monitor into both outputs, 
            also add it to the monitor buffer, which will be used to feed the env follower at the end
        */
        void ApplyMicMonitor(const float* const* in, float **out, size_t size, float* monitor)
        {
            for (size_t i = 0; i < size; i++)
            {
                /* the dry voice gets a shallower key-click duck (about
                   -12 dB) than the harmonies, so it has no audible gap */
                const float d = duck_ ? 1.f - (1.f - duck_[i]) * .75f : 1.f;
                float sig = dcblock_mic_in_.Process(in[0][i] * ingain_ * kMicGain * d);
                sig = mic_filter_.Process(sig);

                monitor[i] += sig;
                monitor[size + i] += sig;

                out[0][i] += sig;
                out[1][i] += sig;
            }
        }

        /** add the line in monitor into both outputs, 
            also add it to the monitor buffer, which will be used to feed the env follower at the end
        */
        void ApplyLineMonitor(const float* const* in, float **out, size_t size, float* monitor)
        {
            for (size_t i = 0; i < size; i++)
            {
                const float sigl = dcblock_line_in_l_.Process(in[2][i] * ingain_ * kLineInGain);
                const float sigr = dcblock_line_in_r_.Process(in[3][i] * ingain_ * kLineInGain);

                monitor[i] += sigl;
                monitor[size + i] += sigr;

                out[0][i] += sigl;
                out[1][i] += sigr;
            }
        }

        bool looper_reset = false;
        bool CheckReset()
        {
            if(looper_reset)
            {
                looper_reset = false;
                return true;
            }

            return false;
        }

        float old_fx_outl, old_fx_outr;
        void Process(const float *const *in, float **out, size_t size)
        {            
            // we'll use out[0] and out[1] as our working space, 
            // then copy to out[2] and out[3] at the end (and apply gain settings)
            std::fill(out[0], out[0] + size, 0.f);
            std::fill(out[1], out[1] + size, 0.f);

            /* SING: key-click ducking for the built-in mic, used
               by the harmonizer input and by the dry mic monitor below */
            float duck[size];
            harmonizer.Duck(duck, size);
            duck_ = duck;

            /* SING: the live input: its pitch is tracked all the time, and
               held keys add harmony voices from it; FX and looper follow as
               they did for TAPE's sample voices */
            if(in_source == InputSource::MIC || in_source == InputSource::LINE_IN)
            {
                float live[size];
                const bool mic = in_source == InputSource::MIC;
                /* the built-in mic reaches the harmonies kMicPreDelay late,
                   so the key-click duck, triggered on the first contact
                   change about a block after the click starts, covers the
                   click from its first sample */
                for(size_t i = 0; i < size; i++)
                {
                    mic_delay_[mic_delay_w_] = in[0][i];
                    const float m = mic_delay_[(mic_delay_w_ - kMicPreDelay) & (kMicDelayLen - 1)];
                    mic_delay_w_ = (mic_delay_w_ + 1) & (kMicDelayLen - 1);
                    live[i] = mic ? m * ingain_ * kMicGain * duck[i]
                                  : (in[2][i] + in[3][i]) * .5f * ingain_ * kLineInGain;
                }
                pitch_.Process(live, size);

                /* voice gate: opens at once on input level (the detector
                   needs ~30 ms, which cut off word starts), stays open while
                   there is level or a voice, and closes only after
                   kGateHoldBlocks of neither, so consonants and short gaps
                   inside words don't chop them up. Key clicks don't open it:
                   the mic is ducked while they happen. */
                float peak = 0.f;
                for(size_t i = 0; i < size; i++)
                    peak = fmaxf(peak, fabsf(live[i]));
                gate_env_ = peak > gate_env_ ? peak : gate_env_ * kGateEnvFall;
                if(pitch_.Voiced() || gate_env_ > kGateLevel)
                    gate_hold_ = kGateHoldBlocks;
                else if(gate_hold_ > 0)
                    gate_hold_--;
                harmonizer.SetGateOpen(gate_hold_ > 0);

                if(harmonizer.Active())
                    harmonizer.Process(live, mic, out[0], out[1], size);
            }

            for(size_t i = 0; i < size; i++)
            {
                out[0][i] = daisysp::SoftClip(out[0][i]);
                out[1][i] = daisysp::SoftClip(out[1][i]);
            }

            // add the monitor to the working space pre-FX
            float monitor[2][size];
            std::fill(&monitor[0][0], &monitor[1][size], 0.f);

            if(monitor_mode == MonitorMode::BOTH)
            {
                if(in_source == InputSource::MIC)
                    ApplyMicMonitor(in, out, size, &monitor[0][0]);
                else if(in_source == InputSource::LINE_IN)
                    ApplyLineMonitor(in, out, size, &monitor[0][0]);
            }
            else if (monitor_mode == MonitorMode::SEND_RET && input_monitor && in_source == InputSource::MIC)
            {
                ApplyMicMonitor(in, out, size, &monitor[0][0]);
            }


            /** looper read + write */
            looper.CheckRecordReady();
            if(looper.CheckReset())
                looper_reset = true;

            //fx_xfade_pre
            if(fx_pre_loop)
                ApplyFx(out[0], out[1], size);

            looper.Process(out[0], out[1], size);

            if(!fx_pre_loop)
                ApplyFx(out[0], out[1], size);

            if(fx_env_ < .01f)
            {
                fx_pre_loop = !fx_pre_loop;
                fx_env_target_ = 1.f;
            }

            //working space (HP) copy to line out
            std::copy(out[0], out[0] + size, out[2]);
            std::copy(out[1], out[1] + size, out[3]);

            // add the dry monitor to the HPs only
            if (monitor_mode == MonitorMode::HP && input_monitor)
            {
                if (in_source == InputSource::MIC)
                    ApplyMicMonitor(in, out, size, &monitor[0][0]);
                if (in_source == InputSource::LINE_IN)
                    ApplyLineMonitor(in, out, size, &monitor[0][0]);
            }
            else if (monitor_mode == MonitorMode::SEND_RET)
            {
                ApplyLineMonitor(in, out, size, &monitor[0][0]);
            }

            // apply resample gain as appropriate (and envelope in/out of that situation)
            if(in_source == InputSource::RESAMPLE)
            {
                for(size_t i = 0; i < size; i++)
                {
                    daisysp::fonepole(resamp_env_, resamp_env_target_, .001f);

                    out[0][i] = monitor[0][i] = resamp_env_ * out[0][i];
                    out[1][i] = monitor[1][i] = resamp_env_ * out[1][i];
        
                    input_env_follower.Process((monitor[0][i] + monitor[1][i]) * .2f);

                    if(monitor_mode == MonitorMode::BOTH)
                    {
                        out[2][i] = out[0][i];
                        out[3][i] = out[1][i];
                    }
                }
            }
            else
            {
                ApplyEnvelopeFollower(size, &monitor[0][0]);
            }


            /** main gain control */
            for(size_t i = 0; i < size; i++)
            {
                fonepole(mgain_, mgain_target_, .001f);
                fonepole(ingain_, ingain_target_, .001f);
                fonepole(final_lim_, final_lim_target_, .001f);

                // apply headphone and main gain
                out[0][i] *= kHpGain * mgain_;
                out[1][i] *= kHpGain * mgain_;

                // apply lineout and main gain
                out[2][i] *= kLineOutGain * mgain_;
                out[3][i] *= kLineOutGain * mgain_;

                // compress
                const float thresh = 1.f / (10.f * final_lim_ + 4.f);
                const float ratio = 1.f + final_lim_ * final_lim_ * 7.f;
                const float makeup = .9f + final_lim_ * .6f;
                const float pregain = 7.f * final_lim_ + 1.f;
                out[0][i] = lim_hp_l_.ProcessComp(out[0][i], pregain, thresh, ratio, makeup);
                out[1][i] = lim_hp_r_.ProcessComp(out[1][i], pregain, thresh, ratio, makeup);
                out[2][i] = lim_line_l_.ProcessComp(out[2][i], pregain, thresh, ratio, makeup);
                out[3][i] = lim_line_r_.ProcessComp(out[3][i], pregain, thresh, ratio, makeup);

                output_env_follower.Process((out[0][i] + out[1][i]));
            }

            duck_ = nullptr; // it pointed into this block's stack
        }

        inline void SetInputMonitor(bool monitor) 
        {
            input_monitor = monitor;
            
            if(in_source == InputSource::RESAMPLE)
                resamp_env_target_ = monitor ? ingain_target_ : 1.f;
        }

        inline float GetVUSample(VUTarget target)
        { 
            if(target == VUTarget::VU_INPUT)
                return input_env_follower.GetLastSamp();
            else if(target == VUTarget::VU_OUTPUT)
                return output_env_follower.GetLastSamp();

            return 0.f;
        }


        void ProcessKeyReqs()
        {
            if (!request_fifo.IsEmpty())
            {
                KeyRequest req = request_fifo.PopFront();
                
                /* SING: keys play harmony voices of the live
                   input instead of samples. transpose_nn_ is the key's
                   distance from the middle C in semitones. */
                if (req.type_ == KeyRequest::Type::START)
                    harmonizer.NoteOn(req.key_, req.transpose_nn_);
                else if (req.type_ == KeyRequest::Type::STOP)
                    harmonizer.NoteOff(req.key_);
            }
        }

        void Prepare()
        {
            ProcessKeyReqs();
        }

        /* SING: the pitch detector's expensive half, from the main loop */
        void UpdatePitch() { pitch_.Update(); }

        /* SING: the sung pitch, for the key lights; note is a fractional
           MIDI note number, valid while it returns true */
        bool SungNote(float &note) const
        {
            note = pitch_.Note();
            return pitch_.Voiced();
        }

        /* SING: a key contact changed (raw, before debouncing) */
        void KeyContact() { harmonizer.KeyContact(); }

        /* SING: voice gate on/off (menu) */
        void ToggleVoiceGate() { harmonizer.SetGateOn(!harmonizer.GateOn()); }
        bool VoiceGate() const { return harmonizer.GateOn(); }

        /* SING: audio callback load, measured in chompi_main.cpp */
        daisy::CpuLoadMeter cpu_meter;

        /* SING: knobs 1-3 drive the harmonizer */
        void SetTranspose(float val) { harmonizer.SetTranspose(val); }
        void SetDoubler(float val) { harmonizer.SetDoubler(val); }
        bool IsHarmonyKeyHeld(int key) { return harmonizer.Held(key); }
        void SetLatch(bool on) { harmonizer.SetLatch(on); }
        bool IsLatched() { return harmonizer.Latched(); }
        void SetSpread(float val) { harmonizer.SetSpread(val); }

        void SetAttack(float val) { harmonizer.SetAttack(val); }

        void SetDecay(float val) { harmonizer.SetRelease(val); }

        // returns the value for the encoder tracking
        void SetGain(float val) { harmonizer.SetLevel(val); }

        void SetInputGain(float gain) 
        {
            ingain_target_ = gain; 
         
            if(input_monitor && in_source == InputSource::RESAMPLE)
                resamp_env_target_ = ingain_target_;
        }
        
        inline void SetMainGain(float gain) { mgain_target_ = gain; }
        inline void SetFinalComp(float comp) { final_lim_target_ = comp; }
        inline float GetFinalComp() { return final_lim_target_; }

        inline void SetReverb(float val) { dly_amt_target_ = reverb_amt_target_ = 1.3f * logf(val + 1.f); }
        inline void SetDelayFeedback(float val) { dly_feedback_target_ = val * .9f; }
        inline void SetDelayTime(float val) {
            dly_time_target_ = .99f * powf(val, 3.f) * kMaxDelayTime + 450;

            reverb_time_target_ = val;
            reverb_time_target_ = fclamp(reverb_time_target_, .05, .97);
        }

        inline void SetWarble(float val) { warble_.SetFreq(val); }

        inline void SetFilter(float val) { cutoff_target_ = val; }
        inline void SetFilterResonance(float val) { res_target_ = val; }
        inline void SetSaturate(float val) 
        { 
            val = logf(1.7f * val + 1.f); 
            saturate_amt_target_ = val * 13.f + 1.f; 
            // saturate_amt_target_ *= .5f; 
        
        }

        inline bool GetLooperRecordArm() { return looper.GetRecordArm(); }
        inline bool GetLooperIsEmpty() { return looper.GetIsEmpty(); }
        inline float GetLooperPosition() { return looper.GetPosition(); }
        inline float GetLooperPitch() { return looper.GetPitch(); }

        inline void LooperRecordButton(bool rising) { looper.RecordButton(rising); }
        inline void LooperPlayButton(bool rising) { looper.PlayButton(rising); }

        inline void ToggleLooperRecord() { looper.ToggleRecord(); }
        inline void ToggleLooperPlaying() { looper.TogglePlaying(); }
        inline bool GetLooperReverse() { return looper.GetReverse(); }

        void ResetLooperPitchQuant()
        {
            looper_fifth = false;
            looper_encoder_chunk = 0.f;
        }

        float SetLooperPitchQuantized(int16_t turns, float enc_pos)
        {
            if (IsLooperPlaying())
            {
                looper_encoder_chunk += turns * .25f;
                if(looper_encoder_chunk >= 1.f || looper_encoder_chunk <= -1.f)
                {
                    // get current semi
                    looper_encoder_chunk = round(looper_encoder_chunk);

                    float pitch = GetLooperPitch();
                    float orig_pitch = pitch;

                    // snap to fifths and octaves
                    // calculate the consts via 2^(x/12) e.g. 2^(-5/12) for down 5 semis
                    float mul;
                    if(pitch > 0.f)
                    {
                        if(looper_fifth)
                            mul = looper_encoder_chunk < 0 ? .667419927085f : 1.33483985417f;
                        else
                            mul = looper_encoder_chunk < 0 ? .749153538438f : 1.49830707688f;
                    }
                    else
                    {
                        if(looper_fifth)
                            mul = looper_encoder_chunk < 0 ? 1.33483985417f : .667419927085f;
                        else
                            mul = looper_encoder_chunk < 0 ? 1.49830707688f : .749153538438f;                            
                    }

                    // snap to next note, return of out of bounds
                    pitch *= mul;
                    if(pitch > 2.f || pitch < -2.f)
                        return enc_pos;

                    looper_fifth = !looper_fifth;
                    looper_encoder_chunk = 0.f;

                    // handle reverse
                    if(pitch < .0625 && pitch > -.0625)
                    {
                        // we weren't already in the turn-around zone
                        if(orig_pitch > .0625 || orig_pitch < -.0625)
                        {
                            looper_fifth = !looper_fifth;
                            pitch = orig_pitch;
                            pitch *= -1.f;
                        }
                        // we were already in the zone, and we're headed over the middle
                        else if((GetLooperReverse() && turns > 0) || (!GetLooperReverse() && turns < 0))
                        {
                            looper_fifth = !looper_fifth;
                            pitch = orig_pitch;
                            pitch *= -1.f;
                        }
                    }

                    SetLooperPitch(pitch);

                    return pitch * .25f + .5f;

                }
            }

            return enc_pos;
        }

        inline void SetLooperPitchFree(float val)
        {
            SetLooperPitch(val * 4.f - 2.f); // -2 - 2            
        }

        inline void SetLooperPitch(float val) { looper.SetPitch(val); }
        inline void SetLooperScrub(float scrub) { looper.SetScrub(scrub); }
        inline float GetLooperScrub() { return looper.GetScrub(); }

        inline bool IsLooperPlaying() { return looper.IsPlaying(); };
        inline bool IsLooperRecording() { return looper.IsRecording(); };
        inline bool IsLooperFirstRecording() { return looper.IsFirstRecording(); };
        inline bool IsLooperRecordArmed() { return looper.GetRecordArm(); };


        inline void IncrementLooperDubGain(float gain) { looper.IncrementDubGain(gain); }
        inline float GetLooperDubGain() { return looper.GetDubGain(); }

        inline void LooperOpenFile() { looper.OpenFile(); }

        daisy::FIFO<KeyRequest, 32> request_fifo;

        // these should really be in a struct defined in the presets file
        float cubbi_pitch;
        float cubbi_start;
        float cubbi_end;
        float cubbi_attack;
        float cubbi_decay;
        bool cubbi_autoloop;
        bool cubbi_sustain;
        float cubbi_gain;
        float cubbi_pan;

        void StopAllVoices(size_t = 100) { harmonizer.AllOff(); }

        // otherwise, they are post looper
        void SetFxPreLooper(bool pre)
        {
            if(pre != fx_pre_loop)
            {
                looper.FXEnvelope();
                fx_env_target_ = 0.f;
            }
        }

        inline bool GetFxPreLooper() { return fx_pre_loop; }

        inline InputSource GetInputSource() { return in_source; }

        void SetInputSource(InputSource source)
        {
            in_source = source;
        }

        /** SING: headphones -> all outputs -> off (the dry voice) */
        inline void IncrementMonitorMode()
        {
            monitor_mode = monitor_mode == MonitorMode::HP     ? MonitorMode::BOTH
                         : monitor_mode == MonitorMode::BOTH   ? MonitorMode::OFF
                                                               : MonitorMode::HP;
        }
        inline MonitorMode GetMonitorMode() { return monitor_mode; }


        // helper for stuck key hack
    private:
 // .0833 = 1/12

        /** the looper */
        LooperEngine looper;

        float encoder_chunk = 0.f; // chunk up the quantized pitch controls
        float looper_encoder_chunk = 0.f;
        bool looper_fifth;
        
        /** env followers for VU meters */
        EnvFollower input_env_follower;
        EnvFollower output_env_follower;

        /** Sampling Bits */

        chompi::Limiter lim_hp_l_;
        chompi::Limiter lim_hp_r_;
        chompi::Limiter lim_line_l_;
        chompi::Limiter lim_line_r_;



        bool input_monitor;



        float ingain_, ingain_target_;
        float resamp_env_, resamp_env_target_;
        float mgain_, mgain_target_;
        float final_lim_, final_lim_target_;

        /** FX */
        MicFilter mic_filter_;
        const float *duck_ = nullptr; // SING: this block's mic ducking
        chompi::PitchDetector pitch_;
        static constexpr int   kGateHoldBlocks = 150;   // ~150 ms at 48-sample blocks
        static constexpr float kGateLevel      = .014f; // input peak that opens it, ~-37 dB
        static constexpr float kGateEnvFall    = .93f;  // per block: ~14 ms to fall by 1/e
        int   gate_hold_ = 0;
        float gate_env_  = 0.f;

        /* SING: built-in mic into the harmonies, kMicPreDelay samples late */
        static constexpr int kMicDelayLen = 256;  // power of two
        static constexpr int kMicPreDelay = 96;   // 2 ms
        float mic_delay_[kMicDelayLen] = {};
        int   mic_delay_w_ = 0;
        DjFilter filter_;
        daisysp::Reverb* reverb_;
        chompi::InterpolatedDelayLine del_;
        Warble warble_;
        daisysp::DcBlock dcblock_mic_in_;
        daisysp::DcBlock dcblock_line_in_l_;
        daisysp::DcBlock dcblock_line_in_r_;
        daisysp::DcBlock dcblock_fx_l_;
        daisysp::DcBlock dcblock_fx_r_;


        float dly_time_, dly_time_target_;
        float dly_feedback_, dly_feedback_target_;
        float reverb_amt_, reverb_amt_target_;
        float reverb_time_, reverb_time_target_;
        float dly_amt_, dly_amt_target_;
        float saturate_amt_, saturate_amt_target_;
        float cutoff_, cutoff_target_;
        float res_, res_target_;
        
        bool fx_pre_loop;
        float fx_env_, fx_env_target_;


        InputSource in_source;
        MonitorMode monitor_mode;

        /** No copy, no assign */
        Engine(const Engine &);
        void operator=(const Engine &);
    };

} // namespace chompi