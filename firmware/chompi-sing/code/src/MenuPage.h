#include "hardware.h"
#include "DSPEngine.h"
#include "temp_led_stuff.h"

namespace chompi
{
    class MenuPage : public daisy::UiPage
    {
    public:



        void Init(Hardware *hw, Engine *fx, float** enc_arr, const float** def_arr,
            uint8_t* page, bool ps_quant, bool split_delay)
        {
            hw_ = hw;
            fx_ = fx;
            enc_values = enc_arr;
            enc_defaults = def_arr;
            knob_page = page;
            
            quantized_pitch_ = ps_quant;
            split_delay_ = split_delay;


            last_blink = System::GetNow();

            chompi_key_pressed = false;


            input_toggled = false;

            final_comp = 0.f;
            delay_time = .5f;
            resonance = 0.f;
            warble = 0.f;
            fx_->SetFinalComp(final_comp);
            fx_->SetFilterResonance(resonance);
            fx_->SetDelayTime(delay_time);
            fx_->SetWarble(warble);

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
            if (chompi_key_pressed)
            {
                r = .67f;
                g = 0.f;
                b = 1.f;
            }
            else
            {
                r = g = b = 0.f;
            }
            SetPthLedFloat(0, r, g, b);
        
            // play / overdub keys
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
                        case MonitorMode::OFF: // SING: dry voice off
                            r = .15f;
                            g = b = 0.f;
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

                // knob 1: transpose (fifths and octaves here), or dark on page 2
                if(knob_page[0] == 0)
                    SingTransposeColour(enc_values[0][0], r, g, b);
                else
                    r = g = b = 0.f;
                SetPthLedFloat(1, r, g, b);
            }

            // SING: no preset keys (TAPE: save / copy / erase)
            SetSmtLedFloat(7, 0.f, 0.f, 0.f);
            // KEY_24: chord mode, keys gold, top note coral, relative dim
            switch(fx_->ChordModeIndex())
            {
                case 0: SetSmtLedFloat(8, sing_gold[0], sing_gold[1], sing_gold[2]); break;
                case 1: SetSmtLedFloat(8, sing_coral[0], sing_coral[1], sing_coral[2]); break;
                default: SetSmtLedFloat(8, sing_gold[0] * .08f, sing_gold[1] * .08f, sing_gold[2] * .08f); break;
            }
            // KEY_25: voice gate, rose when on
            if(fx_->VoiceGate())
                SetSmtLedFloat(9, sing_rose[0], sing_rose[1], sing_rose[2]);
            else
                SetSmtLedFloat(9, sing_rose[0] * .08f, sing_rose[1] * .08f, sing_rose[2] * .08f);

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

            /* SING: audio CPU load on the white keys, low C = 0, high C =
               100%: a dim bar for the average, one bright key for the peak
               of the last 2 s. White key w (0 = low C) is LED 24 - w. */
            {
                const uint32_t now = System::GetNow();
                const float avg = fx_->cpu_meter.GetAvgCpuLoad();
                if (now - cpu_peak_t_ > 2000)
                {
                    cpu_peak_    = fx_->cpu_meter.GetMaxCpuLoad();
                    cpu_peak_t_  = now;
                    fx_->cpu_meter.Reset();
                }
                const float peak = fmaxf(cpu_peak_, fx_->cpu_meter.GetMaxCpuLoad());
                const int   peak_key = int(fminf(peak, .9999f) * 15.f);
                for (int w = 0; w < 15; w++)
                {
                    if (w == peak_key)
                        SetSmtLedFloat(24 - w, sing_gold[0], sing_gold[1], sing_gold[2]);
                    else if (w / 15.f < avg)
                        SetSmtLedFloat(24 - w, sing_coral[0] * .25f, sing_coral[1] * .25f, sing_coral[2] * .25f);
                    else
                        SetSmtLedFloat(24 - w, 0.f, 0.f, 0.f);
                }
            }
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

            /* SING: knob 1 (transpose page) jumps through fifths and octaves
               here, like TAPE's quantised pitch; one step per detent */
            if(encoderID == 0 && page == 0)
            {
                static const float kSteps[] = {-12.f, -7.f, 0.f, 7.f, 12.f};
                const float cur = (enc_values[0][0] - .5f) * 24.f;
                float next = cur;
                if(turns > 0)
                {
                    for(float st : kSteps)
                        if(st > cur + .01f) { next = st; break; }
                }
                else if(turns < 0)
                {
                    for(int k = 4; k >= 0; k--)
                        if(kSteps[k] < cur - .01f) { next = kSteps[k]; break; }
                }
                enc_values[0][0] = next / 24.f + .5f;
                fx_->SetTranspose(enc_values[0][0]);
                return true;
            }

            /* the other pages of knobs 1-3 have no second-level function
               (in TAPE they moved the sample window etc.) */
            if(encoderID <= 2)
                return true;

            float r, g, b;
            {
                if (encoderID == 3)
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
                if(rising) // SING: back to no transpose / default volume
                {
                    if(knob_page[0] == 0)
                    {
                        enc_values[0][0] = .5f;
                        fx_->SetTranspose(.5f);
                    }
                    else
                    {
                        enc_values[1][0] = enc_defaults[1][0];
                        fx_->SetGain(enc_values[1][0]);
                    }
                }
                break;

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


            case static_cast<uint16_t>(Hardware::SwId::KEY_23): // TAPE: erase
                break;

            case static_cast<uint16_t>(Hardware::SwId::KEY_24): // SING: chord mode
                if(rising)
                    fx_->CycleChordMode();
                break;

            case static_cast<uint16_t>(Hardware::SwId::KEY_25): // SING: voice gate
                if(rising)
                    fx_->ToggleVoiceGate();
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
                fx_->ResetLooperPitchQuant();
            }

            input_toggled = false;

        }

        inline void SetSwitchState(bool state) { switch_state = state; }

        /** the menu stays open while the chompi key is held (SING: the toggle
         *  switch is the dry voice here, so it must not close the menu) */
        bool IsClosable()
        { 
            if(System::GetNow() - blink_startt > 1000 && !chompi_key_pressed)
            {
                if(!quantized_pitch_)
                {
                    fx_->ResetLooperPitchQuant();
                }
                return true;
            }
            return false;
        }

        bool switch_state = false;

        /* SING: CPU peak of the last 2 s window, see Draw() */
        float    cpu_peak_   = 0.f;
        uint32_t cpu_peak_t_ = 0;
        bool no_sd_card_ = false;
        inline void NoSDCard() { no_sd_card_ = true; }

    private:
        Hardware *hw_;
        Engine *fx_;
        float** enc_values;
        const float** enc_defaults;
        uint8_t* knob_page;

        bool quantized_pitch_;
        bool split_delay_;


        bool input_toggled;

        bool chompi_key_pressed = false;
        bool blink_state = true;
        uint32_t blink_startt;
        uint32_t last_blink;

        float delay_time, resonance, warble;
        float final_comp;

        bool fx_reset = false;
        bool pitch_reset = false;

    };
} // namespace chompi
