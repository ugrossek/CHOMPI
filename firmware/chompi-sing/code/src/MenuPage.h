#include "hardware.h"
#include "DSPEngine.h"
#include "temp_led_stuff.h"

namespace chompi
{
    class MenuPage : public daisy::UiPage
    {
    public:



        void Init(Hardware *hw, Engine *fx, float** enc_arr, const float** def_arr,
            uint8_t* page, bool split_delay)
        {
            hw_ = hw;
            fx_ = fx;
            enc_values = enc_arr;
            enc_defaults = def_arr;
            knob_page = page;
            
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
        
            /* SING: play = mode, loop = freeze, big wheel = time wheel, as on
               the normal page */
            {
                const float *c = SingCharacterColour(fx_->ChordModeIndex());
                SetPthLedFloat(7, c[0], c[1], c[2]);
                const float fz = !fx_->CanFreeze() ? 0.f : fx_->Frozen() ? 1.f : .08f;
                SetPthLedFloat(8, sing_warm[0] * fz, sing_warm[1] * fz, sing_warm[2] * fz);
                const float back = fx_->CanFreeze() && fx_->Frozen() ? fx_->ScrubPosition() : 0.f;
                const float now_ = fx_->CanFreeze() && fx_->Frozen() ? 1.f - back : 0.f;
                SetPthLedFloat(5, sing_red[0] * back, sing_red[1] * back, sing_red[2] * back);
                SetPthLedFloat(6, sing_warm[0] * now_, sing_warm[1] * now_, sing_warm[2] * now_);
            }

            // shift encoder display
            if(chompi_key_pressed)
            {
                // FX
                if(fx_reset)
                {
                    SetPthLedFloat(4, 1.f, 1.f, 1.f);
                }
                else if(knob_page[3] == 0) // space: echo time
                {
                    SetPthLedFloat(4, delay_time, delay_time, delay_time);
                }
                else // filter: resonance
                {
                    SetPthLedFloat(4, resonance, resonance, resonance);
                }

                if(input_toggled)
                {
                    switch(fx_->GetMonitorMode())
                    {
                        /* SING: headphones warm white, all outputs red,
                           off very dim */
                        case MonitorMode::BOTH:
                            r = sing_red[0];
                            g = sing_red[1];
                            b = sing_red[2];
                            break;
                        case MonitorMode::HP:
                            r = sing_warm[0];
                            g = sing_warm[1];
                            b = sing_warm[2];
                            break;
                        case MonitorMode::OFF:
                            r = sing_warm[0] * .06f;
                            g = sing_warm[1] * .06f;
                            b = sing_warm[2] * .06f;
                            break;
                        case MonitorMode::SEND_RET:
                        default:
                            r = yellow[0];
                            g = yellow[1];
                            b = yellow[2];
                            break;
                    }
                }
                else if(knob_page[5] == 0)
                {
                    r = g = b = 0.f; // SING: no second control on volume
                }
                else
                {
                    // SING: compression, warm white -> red, brighter as it goes up
                    SingMix(sing_warm, sing_red, final_comp, r, g, b);
                    const float lvl = final_comp * .9f + .1f;
                    r *= lvl;
                    g *= lvl;
                    b *= lvl;
                }

                SetPthLedFloat(9, r, g, b);

                // knob 1: transpose (fifths and octaves here), or dark on page 2
                if(knob_page[0] == 0)
                    SingTransposeColour(enc_values[0][0], r, g, b);
                else
                    r = g = b = 0.f;
                if(knob_reset_[0])
                    r = g = b = 1.f;
                SetPthLedFloat(1, r, g, b);
                // knobs 2, 3: width, doubler (white, brighter as they go up);
                // white while being reset
                for(int k = 1; k <= 2; k++)
                {
                    const float v = knob_reset_[k] ? 1.f : .12f + .88f * enc_values[2][k];
                    SetPthLedFloat(k + 1, sing_warm[0] * v, sing_warm[1] * v, sing_warm[2] * v);
                }

                // the big wheel's LEDs: envelope, left short, right long
                {
                    const float e = enc_values[2][4];
                    const float sh = .08f + .92f * (1.f - e), lg = .08f + .92f * e;
                    SetPthLedFloat(5, sing_warm[0] * sh, sing_warm[1] * sh, sing_warm[2] * sh);
                    SetPthLedFloat(6, sing_warm[0] * lg, sing_warm[1] * lg, sing_warm[2] * lg);
                }
            }

            // SING: no preset keys (TAPE: save / copy / erase)
            // KEY_23, KEY_24: dark, they play (freeze and the character are on loop and play)
            SetSmtLedFloat(7, 0.f, 0.f, 0.f);
            SetSmtLedFloat(8, 0.f, 0.f, 0.f);
            SetSmtLedFloat(9, 0.f, 0.f, 0.f); // KEY_25 plays

            // SING: no looper, so no FX pre / post choice
            SetSmtLedFloat(5, 0.f, 0.f, 0.f);
            SetSmtLedFloat(6, 0.f, 0.f, 0.f);
        
            // KEY_18-20 play: no input choice here
            SetSmtLedFloat(2, 0.f, 0.f, 0.f);
            SetSmtLedFloat(3, 0.f, 0.f, 0.f);
            SetSmtLedFloat(4, 0.f, 0.f, 0.f);

            if(!show_cpu_)
            {
                /* the chompi-layer value just turned, as a white bar for a
                   moment (mix: from the middle outwards) */
                const bool show = shown_layer_knob_ >= 0 && System::GetNow() - shown_layer_t_ < 1500;
                float lo = 0.f, hi = 0.f;
                if(show)
                {
                    const float v = enc_values[2][shown_layer_knob_];
                    lo = shown_layer_knob_ == 5 ? fminf(v, .5f) : 0.f;
                    hi = shown_layer_knob_ == 5 ? fmaxf(v, .5f) : v;
                }
                for (int w = 0; w < 15; w++)
                {
                    const float k0 = w / 15.f, k1 = (w + 1) / 15.f;
                    const float l  = show && hi > k0 && lo < k1 ? .45f : 0.f;
                    SetSmtLedFloat(24 - w, sing_warm[0] * l, sing_warm[1] * l, sing_warm[2] * l);
                }
            }
            else
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
                        SetSmtLedFloat(24 - w, sing_warm[0], sing_warm[1], sing_warm[2]);
                    else if (w / 15.f < avg)
                        SetSmtLedFloat(24 - w, sing_red[0] * .25f, sing_red[1] * .25f, sing_red[2] * .25f);
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

            /* SING: the chompi layer, every knob's second control; their
               values live in row 2 of enc_values (knob 4's in delay_time and
               resonance, knob 6's compression in final_comp, as in TAPE) */
            auto layer = [&](int k) -> float & {
                enc_values[2][k] = fclamp(enc_values[2][k] + inc, 0.f, 1.f);
                shown_layer_knob_ = k;
                shown_layer_t_    = System::GetNow();
                return enc_values[2][k];
            };
            switch(encoderID)
            {
                case 1: fx_->SetSpread(layer(1)); break;  // width
                case 2: fx_->SetDoubler(layer(2)); break; // doubler
                case 3:
                    if(page == 0) // space: echo time
                    {
                        delay_time = fclamp(delay_time + inc, 0.f, 1.f);
                        fx_->SetDelayTime(delay_time);
                    }
                    else // filter: resonance
                    {
                        resonance = fclamp(resonance + inc, 0.f, 1.f);
                        fx_->SetFilterResonance(resonance);
                    }
                    break;
                case 4: fx_->SetEnvelope(layer(4)); break; // envelope
                case 5:
                    if(page == 1) // compression
                    {
                        final_comp = fclamp(final_comp + inc, 0.f, 1.f);
                        fx_->SetFinalComp(final_comp);
                        input_toggled = false;
                    }
                    break;
                default: break;
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


            /* SING: knobs 1-3 pressed here reset the knob and its chompi layer,
               like knob 4 does the effects; the ring is white while held */
            case static_cast<uint16_t>(Hardware::SwId::ENC_4_SW): // knob 1
                knob_reset_[0] = rising;
                if(rising) // pitch back to 0
                {
                    ResetKnob(0);
                    fx_->SetTranspose(enc_values[0][0]);
                }
                break;

            // reset the looper pitch via fall through
            case ENC_5_SW:
                return false;

            case static_cast<uint16_t>(Hardware::SwId::ENC_1_SW): // knob 2
                knob_reset_[1] = rising;
                if(rising) // size, width
                {
                    ResetKnob(1);
                    fx_->SetSize(enc_values[0][1]);
                    fx_->SetSpread(enc_values[2][1]);
                }
                break;

            case static_cast<uint16_t>(Hardware::SwId::ENC_2_SW): // knob 3
                knob_reset_[2] = rising;
                if(rising) // character, doubler
                {
                    ResetKnob(2);
                    fx_->SetCharacter(enc_values[0][2]);
                    fx_->SetDoubler(enc_values[2][2]);
                }
                break;

            case static_cast<uint16_t>(Hardware::SwId::ENC_6_SW): // volume
                break; // SING: no dry voice, so no monitor mode to choose

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
                    fx_->SetFilter(enc_values[1][3]); // SING: filter is page 2

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

            // KEY_18-20 play: the input follows the jack (TAPE chose it here)


            case static_cast<uint16_t>(Hardware::SwId::KEY_21): // TAPE: fx pre looper
            case static_cast<uint16_t>(Hardware::SwId::KEY_22): // TAPE: fx post looper
            {
                return false; // SING: no looper; the keys play

                break;
            }


            // KEY_23, KEY_24: play (freeze and the character are on loop and play)

            // KEY_25 plays (the voice gate is in options.json)

            // white keys, play and loop
            default:
                // SING: the keys keep playing harmonies while the menu is open,
                // and play (mode) and loop (freeze) work as on the normal page
                return false;
            }

            return true;
        }

        void OnFocusGained() override
        {
            fx_reset = false;
            knob_reset_[0] = knob_reset_[1] = knob_reset_[2] = false;
            pitch_reset = false;
            chompi_key_pressed = true;

            input_toggled = false;

        }

        inline void SetSwitchState(bool state) { switch_state = state; }

        /** the menu stays open while the chompi key is held (SING: the toggle
         *  switch is the dry voice here, so it must not close the menu) */
        bool IsClosable()
        { 
            if(System::GetNow() - blink_startt > 1000 && !chompi_key_pressed)
            {
                return true;
            }
            return false;
        }

        bool switch_state = false;

        /** SING: show the audio load on the white keys (options.json) */
        void SetShowCpu(bool on) { show_cpu_ = on; }
        bool show_cpu_ = false;

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

        bool split_delay_;


        bool input_toggled;

        bool chompi_key_pressed = false;
        bool blink_state = true;
        uint32_t blink_startt;
        uint32_t last_blink;

        float delay_time, resonance, warble;
        float final_comp;

        bool fx_reset = false;
        bool knob_reset_[3] = {false, false, false}; // SING: knobs 1-3 pressed in the menu
        int      shown_layer_knob_ = -1; // SING: the chompi-layer value last turned
        uint32_t shown_layer_t_    = 0;

        /** all three pages of knob k (0..2) back to their defaults */
        void ResetKnob(int k)
        {
            for(int row = 0; row < 3; row++)
                enc_values[row][k] = enc_defaults[row][k];
        }
        bool pitch_reset = false;

    };
} // namespace chompi
