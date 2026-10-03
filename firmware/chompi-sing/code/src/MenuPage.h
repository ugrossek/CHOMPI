#include "hardware.h"
#include "DSPEngine.h"
#include "temp_led_stuff.h"
#include "FileCopier.h"

namespace chompi
{
    class MenuPage : public daisy::UiPage
    {
    public:

        enum class PresetMode
        {
            NONE = 0,
            ERASE_SEL,
            ERASING,
            COPY_SRC,
            COPY_DEST,
            COPYING,
            SAVE_SEL,
            SAVING,
            LAST,
        };


        void Init(Hardware *hw, Engine *fx, FileCopier *copier, float** enc_arr, const float** def_arr,
            uint8_t* page, PresetManager* pre, bool ps_quant, bool split_delay)
        {
            hw_ = hw;
            fx_ = fx;
            copier_ = copier;
            presets_ = pre;
            enc_values = enc_arr;
            enc_defaults = def_arr;
            knob_page = page;
            
            quantized_pitch_ = ps_quant;
            split_delay_ = split_delay;

            key_color = &purple[0];

            last_blink = System::GetNow();

            chompi_key_pressed = false;

            preset_mode = PresetMode::NONE;

            input_toggled = false;

            final_comp = 0.f;
            delay_time = .5f;
            resonance = 0.f;
            warble = 0.f;
            fx_->SetFinalComp(final_comp);
            fx_->SetFilterResonance(resonance);
            fx_->SetDelayTime(delay_time);
            fx_->SetWarble(warble);

            ss_bank = fx_->GetBank();
        }

        void Draw(const daisy::UiCanvasDescriptor &canvasDescriptor) override
        {
            /** PTH leds */
            float r, g, b;
            uint32_t now = System::GetNow();

            if(now - last_blink > 250)
            {
                blink_state = !blink_state;
                last_blink = now;
            }

            // chompi key
            if (chompi_key_pressed && preset_mode == PresetMode::NONE)
            {
                r = .67f;
                g = 0.f;
                b = 1.f;
            }
            else if (
                    (preset_mode == PresetMode::SAVE_SEL
                    || preset_mode == PresetMode::ERASE_SEL
                    || preset_mode == PresetMode::COPY_DEST)
                    && selected_slot != kSlotNone)
            {
                r = blink_state;
                g = b = 0.f;
            }
            else if (preset_mode == PresetMode::SAVING 
                        || preset_mode == PresetMode::COPYING 
                        || preset_mode == PresetMode::ERASING)
            {
                r = g = b = blink_state;
            }
            else
            {
                r = g = b = 0.f;
            }
            SetPthLedFloat(0, r, g, b);
        
            // play / overdub keys
            if(preset_mode == PresetMode::NONE)
            {
                const float gain = fx_->GetLooperDubGain();
                SetPthLedFloat(7, gain, gain, gain);
                SetPthLedFloat(8, gain, gain, gain);

                if(!fx_->GetLooperIsEmpty())
                {
                    float idx = enc_values[0][4] < .5f ? enc_values[0][4] * 2.f : (1.f - enc_values[0][4]) * 2.f; // 0 - 1 - 0
                    int led_on = enc_values[0][4] > .5f ? 6 : 5;
                    int led_off = enc_values[0][4] > .5f ? 5 : 6;

                    r = color_quad_xfade(med_blue[0], green[0], yellow[0], red[0], idx);
                    g = color_quad_xfade(med_blue[1], green[1], yellow[1], red[1], idx);
                    b = color_quad_xfade(med_blue[2], green[2], yellow[2], red[2], idx);

                    SetPthLedFloat(led_on, r, g, b);

                    if (idx > .8f)
                    {
                        float dim = (idx - .8f) * 5.f;

                        r = color_xfade(0.f, red[0], dim);
                        g = color_xfade(0.f, red[1], dim);
                        b = color_xfade(0.f, red[2], dim);

                        SetPthLedFloat(led_off, r, g, b);
                    }
                    else
                    {
                        SetPthLedFloat(led_off, 0.f, 0.f, 0.f);
                    }
                }
            }

            else if (preset_mode == PresetMode::COPY_SRC
                || preset_mode == PresetMode::COPY_DEST)
            {
                SetPthLedFloat(5, 0.f, 0.f, 0.f);
                SetPthLedFloat(6, 0.f, 0.f, 0.f);                

                if(selected_slot == 16)
                {
                    SetPthLedFloat(7, blue[0], blue[1], blue[2]);
                    SetPthLedFloat(8, blue[0], blue[1], blue[2]);
                }
                else if(copy_src == 16)
                {
                    SetPthLedFloat(7, green[0], green[1], green[2]);
                    SetPthLedFloat(8, green[0], green[1], green[2]);
                }
                else if(!blink_state)
                {
                    SetPthLedFloat(7, 0.f, 0.f, 0.f);
                    SetPthLedFloat(8, 0.f, 0.f, 0.f);
                }
                else if(fx_->GetLooperIsEmpty() && preset_mode == PresetMode::COPY_DEST)
                {
                    SetPthLedFloat(7, .4f, .4f, .4f);
                    SetPthLedFloat(8, .4f, .4f, .4f);

                }
                else if(!fx_->GetLooperIsEmpty())
                {
                    SetPthLedFloat(7, .4f * pink[0], .4f * pink[1], .4f * pink[2]);
                    SetPthLedFloat(8, .4f * pink[0], .4f * pink[1], .4f * pink[2]);
                }
                // unselected, invalid destination
                else
                {
                    SetPthLedFloat(7, 0.f, 0.f, 0.f);
                    SetPthLedFloat(8, 0.f, 0.f, 0.f);
                }
            }
            else
            {
                SetPthLedFloat(5, 0.f, 0.f, 0.f);
                SetPthLedFloat(6, 0.f, 0.f, 0.f);                

                SetPthLedFloat(7, 0.f, 0.f, 0.f);
                SetPthLedFloat(8, 0.f, 0.f, 0.f);                
            }

            // shift encoder display
            if(chompi_key_pressed)
            {
                // FX
                if(fx_reset)
                {
                    SetPthLedFloat(4, 1.f, 1.f, 1.f);
                }
                else if(knob_page[3] == 0) // magic
                {
                    SetPthLedFloat(4, delay_time, delay_time, delay_time);
                }
                else if(knob_page[3] == 1) // lofi
                {
                    SetPthLedFloat(4, warble, warble, warble);
                }
                else // filter
                {
                    SetPthLedFloat(4, resonance, resonance, resonance);
                }

                if(input_toggled)
                {
                    switch(fx_->GetMonitorMode())
                    {
                        case MonitorMode::BOTH:
                            r = blue[0];
                            g = blue[1];
                            b = blue[2];
                            break;
                        case MonitorMode::HP:
                            r = orange[0];
                            g = orange[1];
                            b = orange[2];
                            break;
                        case MonitorMode::SEND_RET:
                        default:
                            r = yellow[0];
                            g = yellow[1];
                            b = yellow[2];
                            break;
                    }
                }
                else
                {
                    // headphone os gain
                    // float idx = final_comp;

                    r = med_blue[0] * (final_comp * .9f + .1f);
                    g = med_blue[1] * (final_comp * .9f + .1f);
                    b = med_blue[2] * (final_comp * .9f + .1f);

                    // r = color_xfade(yellow[0], purple[0], final_comp);
                    // g = color_xfade(yellow[1], purple[1], final_comp);
                    // b = color_xfade(yellow[2], purple[2], final_comp);
                }

                SetPthLedFloat(9, r, g, b);

                SetPthLedFloat(1, 0.f, 0.f, 0.f); // SING: knob 1 has no menu function
            }

            // SING: no preset keys (TAPE: save / copy / erase)
            SetSmtLedFloat(7, 0.f, 0.f, 0.f);
            SetSmtLedFloat(8, 0.f, 0.f, 0.f);
            SetSmtLedFloat(9, 0.f, 0.f, 0.f);

            // FX pre / post looper
            int led_sel = fx_->GetFxPreLooper() ? 5 : 6;
            int led_off = fx_->GetFxPreLooper() ? 6 : 5;
            SetSmtLedFloat(led_sel, yellow[0], yellow[1], yellow[2]);
            SetSmtLedFloat(led_off, 0.f, 0.f, 0.f);
        
            // Input select
            led_sel = 2;
            led_sel += static_cast<int>(fx_->GetInputSource());
            SetSmtLedFloat(2, 0.f, 0.f, 0.f);
            SetSmtLedFloat(3, 0.f, 0.f, 0.f);
            SetSmtLedFloat(4, 0.f, 0.f, 0.f);
            SetSmtLedFloat(led_sel, pink[0], .7f * pink[1], .7f * pink[2]);

            // SING: no slots or banks on the keys
            for (uint8_t i = 1; i < 16; i++)
                SetSmtLedFloat(25 - i, 0.f, 0.f, 0.f);
            SetSmtLedFloat(0, 0.f, 0.f, 0.f);
            SetSmtLedFloat(1, 0.f, 0.f, 0.f);

            // ========   send the data   =========
            fill_led_data();
        }


        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            uint8_t page = knob_page[encoderID];
            float inc = turns * kEncoderCoarseStep;

            // we're receiving a knob position via CC
            if(stepsPerRevolution > 0)
                return false; // fall through to normalpage

            /* SING: knobs 1-3 have no second-level function (in TAPE they
               moved the sample window etc.); swallow them so the menu does
               not also change the normal page */
            if(encoderID <= 2)
                return true;

            float r, g, b;
            if(preset_mode == PresetMode::NONE)
            {
                if(encoderID == 0)
                {
                    if(page == 0) // stepped pitch
                    {
                        if(quantized_pitch_)
                            enc_values[0][0] = fx_->SetGlobalPitchQuantized(turns, enc_values[0][0]);                                
                        else
                        {
                            enc_values[page][encoderID] += turns * kEncoderFineStep;
                            enc_values[page][encoderID] = fclamp(enc_values[page][encoderID], 0.f, 1.f);
                            fx_->SetGlobalPitchFree(enc_values[0][0]);
                        } 
                    }
                    else if(page == 1) // pan
                    {
                        fx_->SetPan(fx_->GetPan() + inc);
                    }

                    DumpValuePresets();
                }
                // move sample window (both start and end pos)
                else if ( (encoderID == 1 && page == 0)
                         || (encoderID == 2 && page == 0)
                )
                {
                    // positive and negative get blocked from moving out of bounds
                    if( !(turns > 0 && enc_values[0][2] + inc > 1.f) // positive clip
                        && !(turns < 0 && enc_values[0][1] + inc < 0.f) ) // negative clip
                    {
                        enc_values[0][1] += inc;
                        enc_values[0][2] += inc;
                        fx_->SetStartPointForce(enc_values[0][1]);
                        fx_->SetEndPointForce(enc_values[0][2]);

                        DumpValuePresets();
                    }

                    // start point
                    r = color_xfade(yellow[0], orange[0], enc_values[0][1]);
                    g = color_xfade(yellow[1], orange[1], enc_values[0][1]);
                    b = color_xfade(yellow[2], orange[2], enc_values[0][1]);
                    SetPthLedFloat(2, r, g, b);

                    // end point
                    r = color_xfade(orange[0], red[0], enc_values[0][2]);
                    g = color_xfade(orange[1], red[1], enc_values[0][2]);
                    b = color_xfade(orange[2], red[2], enc_values[0][2]);
                    SetPthLedFloat(3, r, g, b);
                    knob_page[1] = knob_page[2] = 0;
                }
                // set both attack and decay at once
                else if ( (encoderID == 1 && page == 1)
                         || (encoderID == 2 && page == 1)
                )
                {
                    float val = encoderID == 1 ? enc_values[1][1] : enc_values[1][2];
                    val = fclamp(val + inc, 0.f, 1.f);
                    fx_->SetAttack(val);
                    fx_->SetDecay(val);

                    DumpValuePresets();

                    // att.
                    r = color_xfade(purple[0] * .2f, purple[0], val);
                    g = color_xfade(purple[1] * .2f, purple[1], val);
                    b = color_xfade(purple[2] * .2f, purple[2], val);
                    SetPthLedFloat(2, r, g, b);
                    SetPthLedFloat(3, r, g, b);

                    enc_values[1][1] = val;
                    enc_values[1][2] = val;
                    knob_page[1] = knob_page[2] = 1;
                }
                else if (encoderID == 3)
                {
                    if(page == 0) // magic
                    {
                        delay_time += inc;
                        delay_time = fclamp(delay_time, 0.f, 1.f);
                        fx_->SetDelayTime(delay_time);
                    }
                    else if (page == 1) // lofi
                    {
                        warble += inc;
                        warble = fclamp(warble, 0.f, 1.f);
                        fx_->SetWarble(warble);
                    }
                    else if(page == 2) // filter
                    {
                        resonance += inc;
                        resonance = fclamp(resonance, 0.f, 1.f);
                        fx_->SetFilterResonance(resonance);
                    }
                }
                else if(encoderID == 4)
                {
                    if(quantized_pitch_)
                        enc_values[0][4] = fx_->SetLooperPitchQuantized(turns, enc_values[0][4]);
                    else
                    {
                        enc_values[0][4] += turns * kEncoderFineStep;
                        enc_values[0][4] = fclamp(enc_values[0][4], 0.f, 1.f);
                        fx_->SetLooperPitchFree(enc_values[0][4]);
                    }

                    float idx = enc_values[0][4] < .5f ? enc_values[0][4] * 2.f : (1.f - enc_values[0][4]) * 2.f; // 0 - 1 - 0
                    r = color_quad_xfade(med_blue[0], green[0], yellow[0], red[0], idx);
                    g = color_quad_xfade(med_blue[1], green[1], yellow[1], red[1], idx);
                    b = color_quad_xfade(med_blue[2], green[2], yellow[2], red[2], idx);
                    SetPthLedFloat(5, r, g, b);
                    SetPthLedFloat(6, r, g, b);
                }               
                else if(encoderID == 5)
                {
                    final_comp += inc;
                    final_comp = fclamp(final_comp, 0.f, 1.f);
                    fx_->SetFinalComp(final_comp);
                    input_toggled = false;
                }
            }


            return true;
        }

        void DumpValuePresets()
        {
            size_t mode = static_cast<size_t>(fx_->GetVoiceMode());
            size_t bank = fx_->GetBank();
            size_t slot = fx_->GetVoiceSlot();

            presets_->SetValue(enc_values[0][0], mode, bank, slot, 0);
            presets_->SetValue(enc_values[0][1], mode, bank, slot, 1);
            presets_->SetValue(enc_values[0][2], mode, bank, slot, 2);
            presets_->SetValue(enc_values[1][1], mode, bank, slot, 3);
            presets_->SetValue(enc_values[1][2], mode, bank, slot, 4);
            presets_->SetValue(fx_->GetAutoLoop(), mode, bank, slot, 5);
            presets_->SetValue(fx_->GetSustainActive(), mode, bank, slot, 6);

            presets_->SetValue(enc_values[1][0], mode, bank, slot, 7);
            presets_->SetValue(fx_->GetPan(), mode, bank, slot, 8);        
        }

        void SetVoiceSlot(size_t slot)
        {
            // fx_->SetVoiceSlot(slot, true);

            size_t mode = static_cast<size_t>(fx_->GetVoiceMode());
            size_t bank = fx_->GetBank();

            if(!presets_->IsValid(mode, bank, slot))
            {
                enc_values[0][0] = enc_defaults[0][0];
                enc_values[0][1] = enc_defaults[0][1];
                enc_values[0][2] = enc_defaults[0][2];
                enc_values[1][0] = enc_defaults[1][0];
                enc_values[1][1] = enc_defaults[1][1];
                enc_values[1][2] = enc_defaults[1][2];
                fx_->SetAutoLoop(true);
                fx_->SetSustainActive(true);
                fx_->SetPan(.5f);
            }
            else
            {
                enc_values[0][0] = presets_->GetValue(mode, bank, slot, 0);
                enc_values[0][1] = presets_->GetValue(mode, bank, slot, 1);
                enc_values[0][2] = presets_->GetValue(mode, bank, slot, 2);
                enc_values[1][1] = presets_->GetValue(mode, bank, slot, 3);
                enc_values[1][2] = presets_->GetValue(mode, bank, slot, 4);
                fx_->SetAutoLoop(presets_->GetValue(mode, bank, slot, 5));
                fx_->SetSustainActive(presets_->GetValue(mode, bank, slot, 6));

                enc_values[1][0] = presets_->GetValue(mode, bank, slot, 7);
                fx_->SetPan(presets_->GetValue(mode, bank, slot, 8));
            }

            // TODO: clean up pitch code repetition
            float val = enc_values[0][0];
            val = val < .5f ? (.5f - val) * -2.f : (val - .5f) * 2.f; // 1 - 0 - 1

            float inv = val < 0.f ? -1.f : 1.f;
            float pitch;
            if(fabsf(val) < .33f) // .01x - .5x
                pitch = val * 1.484848f + .01f * inv;
            else if (fabsf(val) < .66f ) // .5x - 1x
                pitch = (val - .33f * inv) * 1.515151 + .5f * inv;
            else // 1x - 2x
                pitch = (val - .66 * inv) * 2.941176 + 1.f * inv;

            fx_->SetGlobalPitch(fabsf(pitch));
            fx_->SetReverse(pitch < 0.f);

            fx_->SetStartPointForce(enc_values[0][1]);
            fx_->SetEndPointForce(enc_values[0][2]);

            fx_->SetGain(enc_values[1][0]);
            fx_->SetAttack(enc_values[1][1]);
            fx_->SetDecay(enc_values[1][2]);

            fx_->SetVoiceSlot(slot, true);
        }

        bool OnButton(uint16_t buttonID,
                uint8_t numberOfPresses,
                bool isRetriggering) override
        {
            if(isRetriggering)
                return true;

            bool rising = numberOfPresses == 1;
            switch (buttonID)
            {
            // NO CONNECT, SKIP THESE
            case static_cast<uint16_t>(Hardware::SwId::NC_1): // fall through
            case static_cast<uint16_t>(Hardware::SwId::NC_2): // fall through
            case static_cast<uint16_t>(Hardware::SwId::NC_3): // fall through
            case static_cast<uint16_t>(Hardware::SwId::NC_4): // fall through
            case static_cast<uint16_t>(Hardware::SwId::NC_5): // fall through
            // case static_cast<uint16_t>(Hardware::SwId::NC_6): // caught in ui.h
                break;


            case static_cast<uint16_t>(Hardware::SwId::ENC_4_SW): // knob 1
                break; // SING: nothing to reset (TAPE: sample pitch / pan)

            // reset the looper pitch via fall through
            case ENC_5_SW:
                return false;

            case static_cast<uint16_t>(Hardware::SwId::ENC_1_SW): // knob 2
            case static_cast<uint16_t>(Hardware::SwId::ENC_2_SW): // knob 3
                break; // SING: no autoloop / sustain toggles

            case static_cast<uint16_t>(Hardware::SwId::ENC_6_SW): // volume
                if(rising)
                {
                    fx_->IncrementMonitorMode();
                    input_toggled = true;
                }
            break;

            case static_cast<uint16_t>(Hardware::SwId::ENC_3_SW): // magic wand
            {
                fx_reset = rising;
                if(rising)
                {
                    enc_values[0][3] = enc_defaults[0][3];
                    enc_values[1][3] = enc_defaults[1][3];
                    enc_values[2][3] = enc_defaults[2][3];

                    fx_->SetReverb(enc_values[0][3]);
                    fx_->SetDelayFeedback(enc_values[0][3]);
                    fx_->SetSaturate(enc_values[1][3]);
                    fx_->SetFilter(enc_values[2][3]);

                    delay_time = .5f;
                    resonance = 0.f;
                    warble = 0.f;

                    fx_->SetFilterResonance(resonance);
                    fx_->SetDelayTime(delay_time);
                    fx_->SetWarble(warble);

                    if (split_delay_) {
                        enc_values[0][3] = .5f;
                    }
                }
            }

            case static_cast<uint16_t>(Hardware::SwId::SW_TOG): // toggle (no longer used, here for safety)
                break;

            case static_cast<uint16_t>(Hardware::SwId::KEY_26): // chompi
                chompi_key_pressed = rising; // the menu closes on its release
                break;

            case static_cast<uint16_t>(Hardware::SwId::KEY_16): // TAPE: banks
            case static_cast<uint16_t>(Hardware::SwId::KEY_17):
                break;

            case static_cast<uint16_t>(Hardware::SwId::KEY_18): // mic in, fall through
            case static_cast<uint16_t>(Hardware::SwId::KEY_19): // aux in, fall through
            case static_cast<uint16_t>(Hardware::SwId::KEY_20): // resample
            {
                if(rising)
                {
                    InputSource source;
                    if(buttonID == static_cast<uint16_t>(Hardware::SwId::KEY_18))
                        source = InputSource::MIC;
                    else if(buttonID == static_cast<uint16_t>(Hardware::SwId::KEY_19))
                        source = InputSource::LINE_IN;
                    else
                        source = InputSource::RESAMPLE;

                    fx_->SetInputSource(source);
                }
                else if(!rising)
                    return false; // note off falls through

                break;
            }


            case static_cast<uint16_t>(Hardware::SwId::KEY_21): // fx pre looper, fall through
            case static_cast<uint16_t>(Hardware::SwId::KEY_22): // fx post looper
            {
                if(rising)
                {
                    fx_->SetFxPreLooper(buttonID == static_cast<uint16_t>(Hardware::SwId::KEY_21));
                }
                else if(!rising)
                    return false; // note off falls through

                break;
            }


            case static_cast<uint16_t>(Hardware::SwId::KEY_23): // TAPE: erase,
            case static_cast<uint16_t>(Hardware::SwId::KEY_24): // copy,
            case static_cast<uint16_t>(Hardware::SwId::KEY_25): // save presets
                break;

            // white keys and play/pause
            default:
                // play pause, overdub gain setting
                if(buttonID == 33 || buttonID == 34)
                {
                    const float gain = buttonID == 33 ? -.1f : .1f;
                    fx_->IncrementLooperDubGain(gain);
                    break;
                }
                // SING: the keys keep playing harmonies while the menu is open
                return false;
            }

            return true;
        }

        void OnFocusGained() override
        {
            fx_reset = false;
            pitch_reset = false;
            chompi_key_pressed = true;

            if(quantized_pitch_)
            {
                fx_->ResetGlobalPitchQuant();
                fx_->ResetLooperPitchQuant();
            }

            input_toggled = false;

            ss_bank = fx_->GetBank();
            copy_src = kSlotNone;
            selected_slot = fx_->GetVoiceSlot();
            preset_mode = PresetMode::NONE;
        }

        inline void SetSwitchState(bool state) { switch_state = state; }

        bool IsClosable()
        { 
            if(
                System::GetNow() - blink_startt > 1000                              // animation done AND
                && ((preset_mode == PresetMode::SAVING && !copier_->IsCopying())    // (finished saving OR
                || (preset_mode == PresetMode::COPYING && !copier_->IsCopying())    // finished copying OR
                || (preset_mode == PresetMode::ERASING && !fx_->IsErasing())        // finished erasing OR
                || (preset_mode == PresetMode::NONE && !chompi_key_pressed)         // did nothing OR
                || (preset_mode != PresetMode::COPYING  && preset_mode != PresetMode::SAVING 
                    && preset_mode != PresetMode::ERASING && !switch_state)) // NOT working on a copy/save/erase and the mode switch went up)
            )
            {
                if((preset_mode == PresetMode::SAVING || preset_mode == PresetMode::COPYING) && selected_slot == 16)
                {
                    fx_->LooperOpenFile();
                    enc_values[0][4] = enc_defaults[0][4];
                }
                else if((preset_mode == PresetMode::COPYING || preset_mode == PresetMode::SAVING) && ss_mode != VoiceMode::CUBBI)
                {
                    SetVoiceSlot(selected_slot);
                }
                else if(preset_mode == PresetMode::ERASING
                    && fx_->GetVoiceMode() == VoiceMode::JAMMI
                    && fx_->GetVoiceSlot() == selected_slot)
                {
                    SetVoiceSlot(15);
                }

                if(!quantized_pitch_)
                {
                    fx_->ResetGlobalPitchQuant();
                    fx_->ResetLooperPitchQuant();
                }

                return true;
            }

            return false;
        }

        bool no_sd_card_ = false;
        inline void NoSDCard() { no_sd_card_ = true; }

    private:
        Hardware *hw_;
        Engine *fx_;
        FileCopier *copier_;
        PresetManager* presets_;
        float** enc_values;
        const float** enc_defaults;
        uint8_t* knob_page;

        bool quantized_pitch_;
        bool split_delay_;

        const float* key_color;

        bool input_toggled;

        bool chompi_key_pressed = false;
        bool blink_state = true;
        uint32_t blink_startt;
        uint32_t last_blink;

        float delay_time, resonance, warble;
        float final_comp;

        bool fx_reset = false;
        bool pitch_reset = false;

        uint8_t selected_slot = kSlotNone;
        uint8_t ss_bank = kSlotNone;
        VoiceMode ss_mode = VoiceMode::LAST;
        uint8_t copy_src = kSlotNone;
        uint8_t cs_bank = kSlotNone;
        VoiceMode cs_mode = VoiceMode::LAST;
        bool switch_state;

        PresetMode preset_mode = PresetMode::NONE;
    };
} // namespace chompi
