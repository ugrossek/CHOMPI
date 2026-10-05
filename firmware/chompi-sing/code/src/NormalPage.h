#pragma once

#include "hardware.h"
#include "DSPEngine.h"
#include "temp_led_stuff.h"

namespace chompi
{
        static const uint8_t cc_map[3][6] = {
            {20, 21, 22, 23, 24, 25},
            {28, 29, 30, 31, 0, 32},
            {0, 0, 0, 33, 0, 0}};

        static const uint8_t key_map[40] = {
            0x01, /**< ENC_1_SW  page */
            0x02, /**< ENC_2_SW page */
            0x03, /**< ENC_3_SW page */
            0x00, /**< ENC_4_SW page */ /** TODO: was CC 16 out */
            0x04, /**< ENC_5_SW page */
            0x15, /**< KEY_26 chompi cc */
            0x00, /**< SW_TOG skip */
            0x31, /**< KEY_16 */
            0x32, /**< KEY_2 */
            0x34, /**< KEY_3 */
            0x35, /**< KEY_4 */
            0x37, /**< KEY_5 */
            0x33, /**< KEY_17 */
            0x36, /**< KEY_18 */
            0x38, /**< KEY_19 */
            0x30, /**< KEY_1 */
            0x39, /**< KEY_6 */
            0x3b, /**< KEY_7 */
            0x3c, /**< KEY_8 */
            0x3e, /**< KEY_9 */
            0x40, /**< KEY_10 */
            0x3a, /**< KEY_20 */
            0x3d, /**< KEY_21 */
            0x3f, /**< KEY_22 */
            0x41, /**< KEY_11 */
            0x43, /**< KEY_12 */
            0x45, /**< KEY_13 */
            0x47, /**< KEY_14 */
            0x48, /**< KEY_15 */
            0x42, /**< KEY_23 */
            0x44, /**< KEY_24 */
            0x46, /**< KEY_25 */
            0x05, /**< ENC_6_SW page */
            0x17, /**< KEY_27 play cc */
            0x18, /**< KEY_28 loop cc */
            0x00, /**< NC_1 skip */
            0x00, /**< NC_2 skip */
            0x00, /**< NC_3 skip */
            0x00, /**< NC_4 skip */
            0x00, /**< NC_5 skip */
        };

        static const uint8_t led_map[40]{
            2,    /**< ENC_1_SW  page */
            3,    /**< ENC_2_SW page */
            4,    /**< ENC_3_SW page */
            1,    /**< ENC_4_SW cc */
            0,    /**< ENC_5_SW page */
            0,    /**< KEY_26 chompi cc */
            0x00, /**< SW_TOG skip */
            0,    /**< KEY_16 */
            23,   /**< KEY_2 */
            22,   /**< KEY_3 */
            21,   /**< KEY_4 */
            20,   /**< KEY_5 */
            1,    /**< KEY_17 */
            2,    /**< KEY_18 */
            3,    /**< KEY_19 */
            24,   /**< KEY_1 */
            19,   /**< KEY_6 */
            18,   /**< KEY_7 */
            17,   /**< KEY_8 */
            16,   /**< KEY_9 */
            15,   /**< KEY_10 */
            4,    /**< KEY_20 */
            5,    /**< KEY_21 */
            6,    /**< KEY_22 */
            14,   /**< KEY_11 */
            13,   /**< KEY_12 */
            12,   /**< KEY_13 */
            11,   /**< KEY_14 */
            10,   /**< KEY_15 */
            7,    /**< KEY_23 */
            8,    /**< KEY_24 */
            9,    /**< KEY_25 */
            9,    /**< ENC_6_SW page */
            7,    /**< KEY_27 play cc */
            8,    /**< KEY_28 loop cc */
            0x00, /**< NC_1 skip */
            0x00, /**< NC_2 skip */
            0x00, /**< NC_3 skip */
            0x00, /**< NC_4 skip */
            0x00, /**< NC_5 skip */
        };

    // .00787 ~= what midi was. 1 / 127
    static const float kEncoderFineStep = .003f;
    static const float kEncoderCoarseStep = .01f;
    static const float kRecDim = .7f;

    uint8_t led_pth_cache[kNumPthLeds][3]; /**< RGB data */
    uint8_t led_smt_cache[kNumSmtLeds][3]; /**< RGB data */

    static const float white[3] = {1.f, 1.f, 1.f};
    static const float red[3] = {1.f, 0.f, 0.f};
    static const float orange[3] = {1.f, .6f, .24f};
    static const float yellow[3] = {1.f, .95f, 0.05f};
    static const float green[3] = {0.f, 1.f, 0.f};
    static const float teal[3] = {.14f, 1.f, .92f};
    static const float med_blue[3] = {0.f, .84f, 1.f};
    static const float blue[3] = {0.f, 0.f, 1.f};
    static const float purple[3] = {.58f, .05f, 1.f};
    static const float pink[3] = {1.f, .36f, .62f};
    static const float dark_orange[3] = {.77f, .38f, .06f};
    static const float yellow_green[3] = {.706f, 1.f, 0.f};

    /* SING: "warm stage" palette, to tell it apart from TAPE */
    static const float sing_magenta[3] = {1.f, .12f, .47f};
    static const float sing_coral[3]   = {1.f, .42f, .30f};
    static const float sing_amber[3]   = {1.f, .55f, 0.f};
    static const float sing_gold[3]    = {1.f, .78f, .10f};
    static const float sing_rose[3]    = {1.f, .45f, .60f};
    static const float sing_warm[3]    = {1.f, .85f, .65f};

    /** knob 1 transpose ring: warm white at 0, coral below, gold above,
     *  brighter the further out */
    static inline void SingTransposeColour(float v, float &r, float &g, float &b);

    /** colour between a and b at t, into r/g/b */
    static inline void SingMix(const float *a, const float *b, float t,
                               float &r, float &g, float &bl)
    {
        r  = a[0] + (b[0] - a[0]) * t;
        g  = a[1] + (b[1] - a[1]) * t;
        bl = a[2] + (b[2] - a[2]) * t;
    }

    static inline void SingTransposeColour(float v, float &r, float &g, float &b)
    {
        const float semis = (v - .5f) * 24.f;
        if (fabsf(semis) < .15f)
        {
            r = sing_warm[0]; g = sing_warm[1]; b = sing_warm[2];
            return;
        }
        const float *c   = semis < 0.f ? sing_coral : sing_gold;
        const float  lvl = .35f + .65f * fminf(fabsf(semis) / 12.f, 1.f);
        r = c[0] * lvl; g = c[1] * lvl; b = c[2] * lvl;
    }

    static const uint8_t knob_num_pages[6] = {2, 2, 2, 3, 1, 2};

    class NormalPage : public daisy::UiPage
    {
      private:
        /* SING: knobs 1-3, two pages each (press to switch), laid out like
         * TAPE (page 1 = sound, page 2 = level and envelope):
         *    knob 1: transpose | harmony volume
         *    knob 2: spread    | attack
         *    knob 3: doubler   | release
         *  The defaults in ui.h and Harmonizer::Init must match. */
        void SingKnob(int knob, int page, float v)
        {
            switch(knob * 2 + page)
            {
                case 0: fx_->SetTranspose(v); break;
                case 1: fx_->SetGain(v); break;
                case 2: fx_->SetSpread(v); break;
                case 3: fx_->SetAttack(v); break;
                case 4: fx_->SetDoubler(v); break;
                case 5: fx_->SetDecay(v); break;
                default: break;
            }
        }

        /** white key 0..14 left to right for a MIDI note of the keyboard
         *  (48..72), or -1 for a black key */
        static int WhiteKeyIndex(int note)
        {
            static const int8_t kWhite[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
            const int n = note - 48;
            if (n < 0 || n > 24)
                return -1;
            const int w = kWhite[n % 12];
            return w < 0 ? -1 : w + 7 * (n / 12);
        }

        static void SingKnobColour(int knob, int page, float v, float &r, float &g, float &b)
        {
            float lvl = 1.f;
            switch(knob * 2 + page)
            {
                case 0: SingTransposeColour(v, r, g, b); return;
                case 2: SingMix(sing_amber, sing_coral, v, r, g, b); lvl = .15f + .85f * v; break;  // spread
                case 4: SingMix(sing_rose, sing_magenta, v, r, g, b); lvl = .15f + .85f * v; break; // doubler
                default: SingMix(sing_warm, sing_gold, v, r, g, b); lvl = .2f + .8f * v; break;     // volume, attack, release
            }
            r *= lvl; g *= lvl; b *= lvl;
        }

      public:
    public:
        uint32_t init_time;
        bool init_ignore = true;

        void Init(Hardware *hw, Engine *fx, float** enc_arr, const float** def_arr, 
                    uint8_t* page, uint8_t midi_out_channel, bool ps_quant, bool split_delay)
        {
            hw_ = hw;
            fx_ = fx;
            enc_values = enc_arr;
            enc_defaults = def_arr;
            knob_page = page;

            midi_channel = midi_out_channel;

            quantized_pitch_ = !ps_quant;
            split_delay_ = split_delay;

            for (int knob = 0; knob < 6; knob++)
            {
                for (int page = 0; page < 3; page++)
                {
                    enc_values[page][knob] = enc_defaults[page][knob];
                }
            }

            if (split_delay_) {
                enc_values[0][3] = .5f;
            }

            
            fx_->SetInputGain(.75f);


            for (int i = 0; i < kNumSmtLeds; i++)
                SetSmtLed(i, 0, 0, 0);
            for (int i = 0; i < kNumPthLeds; i++)
                SetPthLed(i, 0, 0, 0);

            // 8mm leds
            SetPthLedFloat(0, 0.f, 0.f, 0.f);
            SetPthLedFloat(1, green[0], green[1], green[2]);
            SetPthLedFloat(2, 0.f, 0.f, 0.f);
            SetPthLedFloat(3, 0.f, 0.f, 0.f);
            SetPthLedFloat(4, 0.f, 0.f, 0.f);
            SetPthLedFloat(9, 0.f, 0.f, 0.f);

            // 5mm leds
            SetPthLedFloat(5, 1.f, 1.f, 1.f);
            SetPthLedFloat(6, 1.f, 1.f, 1.f);
            SetPthLedFloat(7, 1.f, 1.f, 1.f);
            SetPthLedFloat(8, 1.f, 1.f, 1.f);

            for (int i = 0; i < kNumSmtLeds; i++)
                SetSmtLed(i, 0, 0, 0);
            // SetSmtLedFloat(i, .1f, .1f, .0f);
            // SetSmtLedFloat(0, 0.f, 0.f, 0.f);
            // SetSmtLedFloat(9, 0.f, 0.f, 0.f);
            // SetSmtLedFloat(10, 0.f, 0.f, 0.f);

            init_time = System::GetNow();
        }

        uint32_t last_arm_blink;
        bool arm_blink = true;


        void ResetSmtLeds()
        {
            for(size_t i = 0; i < 25; i++)
            {
                SetSmtLed(i, 0.f, 0.f, 0.f);
            }
        }

        void CacheLeds()
        {
            std::copy(&led_pth_data[0][0], &led_pth_data[0][0] + kNumPthLeds * 3, &led_pth_cache[0][0]);
            std::copy(&led_smt_data[0][0], &led_smt_data[0][0] + kNumSmtLeds * 3, &led_smt_cache[0][0]);
        }

        void RefreshLeds()
        {
            std::copy(&led_pth_cache[0][0], &led_pth_cache[0][0] + kNumPthLeds * 3, &led_pth_data[0][0]);
            std::copy(&led_smt_cache[0][0], &led_smt_cache[0][0] + kNumSmtLeds * 3, &led_smt_data[0][0]);
        }

        void Draw(const daisy::UiCanvasDescriptor &canvasDescriptor) override
        {
            uint32_t now = System::GetNow();

            if(fx_->CheckReset())
            {
                enc_values[0][4] = enc_defaults[0][4];
                fx_->ResetLooperPitchQuant();
            }

            // ignore the first 1500 ms of inputs. Hack to stop random button presses on boot for now.
            if (init_ignore)
            {
                if (now - init_time > 1500)
                {
                    init_ignore = false;
                }
            }

            /* SING: held keys magenta; the middle C dim amber and
               the outer Cs dimmer, for orientation. Idle markers stay off
               while the mic is monitored, as in TAPE. */
            /* SING: for a moment after a knob turn, the white keys show its
               position as a bar (transpose: from the middle C outwards) */
            const uint32_t shown_age = System::GetNow() - shown_t_;
            const bool  show_value = shown_t_ != 0 && shown_age < kShowMs;
            float       bar_lo = 0.f, bar_hi = 0.f, bar_lvl = 0.f;
            if (show_value)
            {
                const float v = enc_values[shown_page_][shown_knob_];
                if (shown_knob_ == 0 && shown_page_ == 0) // transpose
                {
                    bar_lo = v < .5f ? v : .5f;
                    bar_hi = v < .5f ? .5f : v;
                }
                else
                {
                    bar_lo = 0.f;
                    bar_hi = v;
                }
                const float fade = shown_age < kShowMs - 400 ? 1.f
                                   : (kShowMs - shown_age) / 400.f;
                bar_lvl = .45f * fade;
            }

            /* SING: the key of the note being sung, folded into the
               keyboard's two octaves: warm white when in tune (within 15
               cents), coral when flat, gold when sharp. Held over short
               unvoiced gaps so it doesn't flicker. */
            float sung;
            const uint32_t now_ms = System::GetNow();
            if (fx_->SungNote(sung))
            {
                sung_note_ = sung;
                sung_t_    = now_ms;
            }
            int   sung_key = -1;
            float sr = 0.f, sg = 0.f, sb = 0.f;
            if (sung_t_ != 0 && now_ms - sung_t_ < kSungHoldMs)
            {
                int m = int(lroundf(sung_note_));
                const float cents = (sung_note_ - m) * 100.f;
                while (m < 48)
                    m += 12;
                while (m > 72)
                    m -= 12;
                sung_key = m;
                const float *c = fabsf(cents) < 15.f ? sing_warm
                                 : cents < 0.f      ? sing_coral
                                                    : sing_gold;
                sr = c[0] * .8f;
                sg = c[1] * .8f;
                sb = c[2] * .8f;
            }

            for(size_t i = 7; i < (25 + 7); i++)
            {
                const bool c_key = key_map[i] % 12 == 0;
                const int  w     = WhiteKeyIndex(key_map[i]); // 0..14, -1 black
                if (fx_->IsHarmonyKeyHeld(i))
                    SetSmtLedFloat(led_map[i], sing_magenta[0], sing_magenta[1], sing_magenta[2]);
                else if (key_map[i] == sung_key)
                    SetSmtLedFloat(led_map[i], sr, sg, sb);
                else if (show_value)
                {
                    /* key w covers [w/15, (w+1)/15); lit if the bar reaches into it */
                    const float k0 = w / 15.f, k1 = (w + 1) / 15.f;
                    const bool  on = w >= 0 && bar_hi > k0 && bar_lo < k1;
                    if (on)
                        SetSmtLedFloat(led_map[i], sing_gold[0] * bar_lvl, sing_gold[1] * bar_lvl, sing_gold[2] * bar_lvl);
                    else
                        SetSmtLed(led_map[i], 0, 0, 0);
                }
                else if (c_key)
                {
                    const float dim = key_map[i] == 60 ? .3f : .1f;
                    SetSmtLedFloat(led_map[i], sing_amber[0] * dim, sing_amber[1] * dim, sing_amber[2] * dim);
                }
                else
                    SetSmtLed(led_map[i], 0, 0, 0);
            }

            // =========   encoders   =========
            for (int i = 0; i < 6; i++)
            {
                uint8_t page = knob_page[i];
                float value = enc_values[page][i];

                float r = 0.f; 
                float g = 0.f;
                float b = 0.f;
                switch (i)
                {
                case 0: // SING: knobs 1-3, see SingKnob()
                case 1:
                case 2:
                {
                    SingKnobColour(i, page, value, r, g, b);
                    SetPthLedFloat(i + 1, r, g, b);
                    break;
                }
                case 3: // magic
                {
                    if (page == 0) // reverb / delay
                    {   
                        if (split_delay_) {
                            if (value < .5) {
                                fx_->SetReverb(0.f);
                                fx_->SetDelayFeedback((.5f - value) * 2.f);
                            }
                            else {
                                fx_->SetReverb((value - .5f) * 2.f);
                                fx_->SetDelayFeedback(0.f);
                            }

                            // SING: delay gold <- centre -> reverb magenta
                            if (value < .5f) SingMix(sing_warm, sing_gold, (.5f - value) * 2.f, r, g, b);
                            else             SingMix(sing_warm, sing_magenta, (value - .5f) * 2.f, r, g, b);
                        }
                        else {
                            fx_->SetReverb(value);
                            fx_->SetDelayFeedback(value);

                            SingMix(sing_gold, sing_magenta, value, r, g, b); // SING
                        }
                    }
                    else if (page == 1) // lofi
                    {
                        fx_->SetSaturate(value);
                        SingMix(sing_amber, red, value, r, g, b); // SING: lofi
                    }
                    else // filter
                    {
                        fx_->SetFilter(value);
                        SingMix(sing_coral, sing_warm, value, r, g, b); // SING: filter
                    }


                    SetPthLedFloat(4, r, g, b);

                    break;
                }
                case 4: // transport
                {                    
                    if(fx_->GetLooperIsEmpty())
                    {
                        SetPthLedFloat(5, 0.f, 0.f, 0.f);
                        SetPthLedFloat(6, 0.f, 0.f, 0.f);
                    }
                    else if(fx_->IsLooperPlaying())
                    {
                        float idx = value < .5f ? value * 2.f : (1.f - value) * 2.f; // 0 - 1 - 0
                        int led_on = value > .5f ? 6 : 5;
                        int led_off = value > .5f ? 5 : 6;

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

                        value = value * 4.f - 2.f; // -2 - 2
                    }
                    else
                    {
                        float scrub = fx_->GetLooperScrub() * .5f;
                        int led = 5 + (scrub > 0.f);

                        scrub = fabsf(scrub);
                        SetPthLedFloat(led, scrub, scrub, scrub);
                    }

                    break;
                }
                case 5: // gain
                {
                    if(batt_display && System::GetNow() - batt_hold > 2000)
                    {
                        const float* color = &green[0];

                        switch(hw_->GetBatteryLevel())
                        {
                            case Hardware::BatteryLevel::FULL:
                                color = &white[0];
                            break;
                            case Hardware::BatteryLevel::HIGH:
                                color = &green[0];
                            break;
                            case Hardware::BatteryLevel::MEDIUM:
                                color = &yellow[0];
                            break;
                            case Hardware::BatteryLevel::LOW:
                                color = &red[0];
                            break;
                            default:
                            break;
                        }

                        r = color[0];
                        g = color[1];
                        b = color[2];
                    }
                    else if (page == 0)
                    {
                        float vu_sample = fx_->GetVUSample(VUTarget::VU_OUTPUT);

                        r = value * color_quad_xfade(.1f, green[0], yellow[0], pink[0], vu_sample);
                        g = value * color_quad_xfade(.1f, green[1], yellow[1], pink[1], vu_sample);
                        b = value * color_quad_xfade(.1f, green[2], yellow[2], pink[2], vu_sample);

                        fx_->SetMainGain(value);
                    }
                    else
                    {
                        r = color_xfade(blue[0], red[0], value);
                        g = color_xfade(blue[1], red[1], value);
                        b = color_xfade(blue[2], red[2], value);

                        fx_->SetInputGain(value);
                    }
                    SetPthLedFloat(9, r, g, b);
                    break;
                }
                default:
                    break;
                }
            }

            /** PTH leds */
            float r, g, b;
            // play key
            if(fx_->GetLooperIsEmpty() && !fx_->IsLooperRecordArmed())
            {
                r = g = b = 0.f;
            }
            else if(fx_->IsLooperRecordArmed())
            {
                r = g = b = 1.f;
            }
            else if(fx_->IsLooperFirstRecording() && fx_->IsLooperRecording())
            {
                r = teal[0];
                g = teal[1];
                b = teal[2];
            }
            else if(fx_->IsLooperPlaying())
            {
                float position = 1.f - fx_->GetLooperPosition();
                r = teal[0] * position;
                g = teal[1] * position;
                b = teal[2] * position;
            }
            else // we're paused
            {
                float position = 1.f - fx_->GetLooperPosition();
                r = position;
                g = position;
                b = position;
            }

            SetPthLedFloat(led_map[33], r, g, b);

            // loop key
            if(fx_->GetLooperIsEmpty() && !fx_->IsLooperRecordArmed())
            {
                r = g = b = 0.f;
            }
            else if(fx_->IsLooperRecordArmed())
            {
                if(now - last_arm_blink > 300)
                {
                    arm_blink = !arm_blink;
                    last_arm_blink = now;
                }

                r = arm_blink ? 1.f : 0.f;
                g = 0.f;
                b = 0.f;
            }
            else if(fx_->IsLooperFirstRecording() && fx_->IsLooperRecording())
            {
                r = red[0];
                g = red[1];
                b = red[2];
            }
            else if(fx_->IsLooperRecording()) // overdub
            {
                float position = fx_->GetLooperPosition();
                r = yellow[0] * position;
                g = yellow[1] * position;
                b = yellow[2] * position;
            }
            else
            {
                float position = fx_->GetLooperPosition();
                r = position;
                g = position;
                b = position;
            }

            SetPthLedFloat(led_map[34], r, g, b);

            // chompi key
            fx_->SetInputMonitor(true); // SING: dry voice per monitor mode (menu, knob 6)
            if (fx_->IsLatched()) // SING: latch on
            {
                r = sing_magenta[0];
                g = sing_magenta[1];
                b = sing_magenta[2];
            }
            else // input level, as TAPE shows while monitoring
            {
                float vu_sample = fx_->GetVUSample(VUTarget::VU_INPUT);
                r = color_quad_xfade(.1f, green[0], yellow[0], pink[0], vu_sample);
                g = color_quad_xfade(.1f, green[1], yellow[1], pink[1], vu_sample);
                b = color_quad_xfade(.1f, green[2], yellow[2], pink[2], vu_sample);
            }

            SetPthLedFloat(led_map[5], r, g, b);

            // ========   send the data   =========
            fill_led_data();
        }

        bool OnButton(uint16_t buttonID,
                      uint8_t numberOfPresses,
                      bool isRetriggering) override
        {
            if (init_ignore)
                return false;

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

            // encoder clicks, toggle pages
            case static_cast<uint16_t>(Hardware::SwId::ENC_1_SW): // fall through
            case static_cast<uint16_t>(Hardware::SwId::ENC_2_SW): // fall through
            case static_cast<uint16_t>(Hardware::SwId::ENC_3_SW): // fall through
            case static_cast<uint16_t>(Hardware::SwId::ENC_4_SW): // fall through
            {
                if(!rising)
                {
                    uint8_t knob = key_map[buttonID];
                    knob_page[knob]++;
                    knob_page[knob] %= knob_num_pages[knob];
                }
                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::ENC_6_SW):
            {
                if(!rising && System::GetNow() - batt_hold < 2000)
                {
                    uint8_t knob = key_map[buttonID];
                    knob_page[knob]++;
                    knob_page[knob] %= knob_num_pages[knob];
                }

                batt_hold = System::GetNow();
                batt_display = rising;

                break;
            }

            // reset the looper pitch
            case ENC_5_SW:
            {
                if (!rising)
                {
                    enc_values[0][4] = enc_defaults[0][4];
                    hw_->SendCC(midi_channel, cc_map[0][4], enc_values[0][4] * 127.f);
                }

                fx_->SetLooperPitch(1.f);
                fx_->ResetLooperPitchQuant();
                break;
            }

            // toggle. We're not using this anymore, just here in case something breaks
            case static_cast<uint16_t>(Hardware::SwId::SW_TOG): // fall through
                // switch_state = rising;
                // if (!rising)
                //     midi_channel = 0;

                // some weirdness results in handling this on edges rather than as pressed
                // for example if you hold the chompi key with the switch up then toggle the sw
                // down, you'll be on ch 1 until you release and repress the chompi key
                // then it will go to ch 2 like it should
                break;

            // CC buttons
            case static_cast<uint16_t>(Hardware::SwId::KEY_27): // play
            {
                last_arm_blink = System::GetNow();
                fx_->LooperPlayButton(rising);    
                hw_->SendCC(midi_channel, 26, rising ? 127 : 0);
                break;
            }
            case static_cast<uint16_t>(Hardware::SwId::KEY_28): // loop
            {
                last_arm_blink = System::GetNow();
                fx_->LooperRecordButton(rising);
                hw_->SendCC(midi_channel, 27, rising ? 127 : 0);
                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::KEY_26):
            {
                chompi_key_pressed = rising;
                hw_->SendCC(midi_channel, key_map[buttonID], rising ? 127 : 0);
 
                break;
            }

            // keys
            default:
                if (rising)
                {
                    // real keypress
                    if(!isRetriggering)
                    {
                        fx_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                            key_map[buttonID] - 60, buttonID, 127.f));
                    }

                    if(fx_->GetLooperRecordArm())
                        fx_->ToggleLooperRecord();

                    // this can take some time, so it must happen last
                    if(!isRetriggering)
                        hw_->SendNoteOn(midi_channel, key_map[buttonID], 127);
                }
                else
                {
                    if(!isRetriggering)
                    {
                        fx_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                            0, buttonID, 127.f));
                        hw_->SendNoteOff(midi_channel, key_map[buttonID], 127);
                    }
                }
                break;
            }

            return true;
        }

        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            if (init_ignore)
                return false;

            uint8_t page = knob_page[encoderID];
            float old_val = enc_values[page][encoderID];


            // overrode this to mean increment vs force knob position (used for CCs)
            if(stepsPerRevolution > 0)
            {
                enc_values[page][encoderID] = turns / 127.f;
            }
            else{
                float inc = turns * kEncoderCoarseStep;

                /* SING: transpose (knob 1, page 1) moves in fine steps,
                   spread and doubler (knobs 2-3, page 1) in coarse ones */
                if(page == 0 && encoderID == 0)
                {
                    inc = turns * kEncoderFineStep; // SING: continuous transpose
                }
                else if(page == 0 && encoderID <= 2)
                {
                }
                else if((encoderID == 0 && page == 0 && quantized_pitch_)
                    || (encoderID == 4 && quantized_pitch_))
                {
                    inc = 0.f;
                }
                else if ((encoderID == 0 && page == 0 && !quantized_pitch_)
                    || (encoderID == 1 && page == 0)
                    || (encoderID == 2 && page == 0)
                    || (encoderID == 4 && !quantized_pitch_))
                {
                    inc = turns * kEncoderFineStep;
                }

                enc_values[page][encoderID] += inc;
            }

            // clip
            enc_values[page][encoderID] = fclamp(enc_values[page][encoderID], 0.f, 1.f);

            if (encoderID <= 2)
            {
                SingKnob(encoderID, page, enc_values[page][encoderID]);
            }
            else if (encoderID == 4)
            {
                if (fx_->IsLooperPlaying())
                {
                    if(quantized_pitch_)
                        enc_values[0][4] = fx_->SetLooperPitchQuantized(turns, enc_values[0][4]);
                    else
                        fx_->SetLooperPitchFree(enc_values[0][4]);
                }
                else
                {
                    enc_values[0][4] = old_val;
                    fx_->SetLooperScrub(turns);
                }  
            }

            /* SING: show where this knob now is on the white keys, briefly */
            shown_knob_  = encoderID;
            shown_page_  = page;
            shown_t_     = System::GetNow();

            if (stepsPerRevolution == 0) {
                hw_->SendCC(midi_channel, cc_map[page][encoderID], enc_values[page][encoderID] * 127);
            }

            return true;
        }

        /** SING: the toggle switch is latch. Compared with the engine on
         *  every call rather than on a change of the switch: the UI starts
         *  before the engine, whose Init() would otherwise undo a latch the
         *  switch was already in at power-on. */
        void SetSwitchState(bool state)
        {
            if (fx_->IsLatched() != state)
                fx_->SetLatch(state);
            switch_state = state; 
        }

        inline void SetInitIgnore(bool ignore) { init_ignore = ignore; }

    private:
        Hardware *hw_;
        Engine *fx_;

        float** enc_values;
        const float** enc_defaults;

        /** todo: these really shouldn't be stored in here
         *  Gonna move them out to a midi engine later
        */ 
        uint8_t midi_channel = 0;
        bool switch_state = false;

        /* SING: the knob last turned, shown on the white keys for a while */
        static constexpr uint32_t kShowMs = 1500;

        /* SING: the sung note last seen, and when */
        static constexpr uint32_t kSungHoldMs = 80;
        float    sung_note_ = 0.f;
        uint32_t sung_t_    = 0;
        int      shown_knob_ = 0, shown_page_ = 0;
        uint32_t shown_t_    = 0;
        bool chompi_key_pressed = false;
        uint8_t* knob_page;
        bool quantized_pitch_;
        bool split_delay_;

        bool batt_display;
        uint32_t batt_hold;
    };

} // namespace chompi