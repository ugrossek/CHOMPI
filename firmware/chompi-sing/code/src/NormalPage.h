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

    /* palette A, "Die Mensch-Maschine": red for the robot and for what
       is played, warm white for the human and for what is in tune */
    static const float sing_red[3]   = {1.f, .05f, .03f};
    static const float sing_warm[3]  = {1.f, .85f, .65f};
    /* the Bauhaus primaries for the knob pages: page 1 red, 2 yellow, 3 blue
       (yellow leans orange, or the LEDs show it greenish) */
    static const float sing_yellow[3] = {1.f, .50f, 0.f};
    static const float sing_blue[3]   = {.08f, .25f, 1.f};

    /* knobs 4 and 6: page 1 red, page 2 blue (yellow is Toy's) */
    static inline const float *SingPageColour(int page)
    {
        return page == 1 ? sing_blue : sing_red;
    }

    /** a knob ring: the page's colour, brighter as the value goes up, never
     *  so dim that the colour is lost on the LEDs' 23 steps */
    static inline void SingPageRing(int page, float v, float &r, float &g, float &b)
    {
        const float *c  = SingPageColour(page);
        const float lvl = .2f + .8f * v;
        r = c[0] * lvl;
        g = c[1] * lvl;
        b = c[2] * lvl;
    }

    /** the play key's colour for a character: Robot red, Human warm white,
     *  Toy yellow */
    static inline const float *SingCharacterColour(int index)
    {
        return index == 0 ? sing_red : index == 1 ? sing_warm : sing_yellow;
    }

    /** knob 1 transpose ring: warm white at 0, red either way, brighter
     *  the further out */
    static inline void SingTransposeColour(float v, float &r, float &g, float &b);

    /** colour between a and b at t, into r/g/b */
    static inline void SingMix(const float *a, const float *b, float t,
                               float &r, float &g, float &bl)
    {
        r  = a[0] + (b[0] - a[0]) * t;
        g  = a[1] + (b[1] - a[1]) * t;
        bl = a[2] + (b[2] - a[2]) * t;
    }

    /** a level meter in palette A: dim warm white when quiet, brighter as it
     *  gets louder, red when it's hot. The knob LEDs have only 23 steps
     *  (temp_led_stuff.h divides by 11): below ~.25 warm white rounds to
     *  something like (2, 1, 1), which shows as a tinted glow, so the meter
     *  never goes darker than that. */
    static inline void SingMeter(float v, float &r, float &g, float &b)
    {
        const float lvl = .27f + .73f * fminf(v * 1.6f, 1.f);
        const float hot = v < .6f ? 0.f : fminf((v - .6f) / .3f, 1.f);
        r = (sing_warm[0] + (sing_red[0] - sing_warm[0]) * hot) * lvl;
        g = (sing_warm[1] + (sing_red[1] - sing_warm[1]) * hot) * lvl;
        b = (sing_warm[2] + (sing_red[2] - sing_warm[2]) * hot) * lvl;
    }

    static inline void SingTransposeColour(float v, float &r, float &g, float &b)
    {
        const float semis = (v - .5f) * 24.f;
        if (fabsf(semis) < .15f)
        {
            r = sing_warm[0]; g = sing_warm[1]; b = sing_warm[2];
            return;
        }
        const float *c   = sing_red; // up or down: red, brighter further out
        const float  lvl = .35f + .65f * fminf(fabsf(semis) / 12.f, 1.f);
        r = c[0] * lvl; g = c[1] * lvl; b = c[2] * lvl;
    }

    /* SING: knobs 1-3 and 5 have one page and a chompi layer (MenuPage);
       knob 4 (space, filter) and knob 6 (volume, input gain) have two */
    static const uint8_t knob_num_pages[6] = {1, 1, 1, 2, 1, 2};

    class NormalPage : public daisy::UiPage
    {
      private:
        /* SING: knobs 1-3, laid out like TAPE (page 1 = sound, page 2 =
         * level and envelope). Each value lives in a row of enc_values:
         *    knob 1: transpose (row 0) | harmony volume (row 1) | metal (row 2)
         *    knob 2: spread (row 0)    | attack (row 1)  | size (row 2)
         *    knob 3: doubler (row 0)   | release (row 1) | character (row 2)
         *  Knobs 2 and 3 show size/character, spread/doubler, attack/release
         *  (three pages) in both characters. The defaults in ui.h and
         *  Harmonizer::Init must match. */
        void SingKnob(int knob, int row, float v)
        {
            if (row != 0)
                return; // the chompi layer is set in MenuPage
            switch(knob)
            {
                case 0: fx_->SetTranspose(v); break;
                case 1: fx_->SetSize(v); break;
                case 2: fx_->SetCharacter(v); break;
                default: break;
            }
        }

        /** the row of enc_values that knob's page shows: knobs 2 and 3 show
         *  size/character, spread/doubler, attack/release, the same in both
         *  characters and with or without latch. Page 2 of knobs 1-3 is then
         *  the ensemble (harmony volume, spread, doubler), page 3 the shape
         *  (metal, attack, release). */
        uint8_t Row(int knob, uint8_t page) const
        {
            (void)knob;
            return page;
        }

        uint8_t Pages(int knob) const { return knob_num_pages[knob]; }

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

        /** knobs 1-3: the colour of the page shown (page 1 red, 2 yellow,
         *  3 blue), brighter as the value goes up; transpose keeps its own
         *  (warm white at 0, red either way) */
        static void SingKnobColour(int knob, int row, int page, float v, float &r, float &g, float &b)
        {
            (void)knob;
            (void)row;
            (void)page;
            /* pitch, size and character are all centred: warm white in the
               middle, red the further out either way */
            SingTransposeColour(v, r, g, b);
        }

      public:
    public:
        uint32_t init_time;
        bool init_ignore = true;

        void Init(Hardware *hw, Engine *fx, float** enc_arr, const float** def_arr, 
                    uint8_t* page, uint8_t midi_out_channel, bool split_delay)
        {
            hw_ = hw;
            fx_ = fx;
            enc_values = enc_arr;
            enc_defaults = def_arr;
            knob_page = page;

            midi_channel = midi_out_channel;

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
            SetPthLedFloat(1, sing_warm[0], sing_warm[1], sing_warm[2]);
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

            // ignore the first 1500 ms of inputs. Hack to stop random button presses on boot for now.
            if (init_ignore)
            {
                if (now - init_time > 1500)
                {
                    init_ignore = false;
                }
            }

            /* SING: held keys red; the middle C dim warm white and
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
               cents), red when off. Held over short
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
                const float *c = fabsf(cents) < 15.f ? sing_warm : sing_red; // in tune / off
                sr = c[0] * .8f;
                sg = c[1] * .8f;
                sb = c[2] * .8f;
            }

            for(size_t i = 7; i < (25 + 7); i++)
            {
                const bool c_key = key_map[i] % 12 == 0;
                const int  w     = WhiteKeyIndex(key_map[i]); // 0..14, -1 black
                if (fx_->IsHarmonyKeyHeld(i))
                    SetSmtLedFloat(led_map[i], sing_red[0], sing_red[1], sing_red[2]);
                else if (key_map[i] == sung_key)
                    SetSmtLedFloat(led_map[i], sr, sg, sb);
                else if (show_value)
                {
                    /* key w covers [w/15, (w+1)/15); lit if the bar reaches into it */
                    const float k0 = w / 15.f, k1 = (w + 1) / 15.f;
                    const bool  on = w >= 0 && bar_hi > k0 && bar_lo < k1;
                    if (on)
                    {
                        const float *c = SingPageColour(shown_disp_page_); // the page turned
                        SetSmtLedFloat(led_map[i], c[0] * bar_lvl, c[1] * bar_lvl, c[2] * bar_lvl);
                    }
                    else
                        SetSmtLed(led_map[i], 0, 0, 0);
                }
                else if (c_key)
                {
                    const float dim = key_map[i] == 60 ? .3f : .1f;
                    SetSmtLedFloat(led_map[i], sing_warm[0] * dim, sing_warm[1] * dim, sing_warm[2] * dim);
                }
                else
                    SetSmtLed(led_map[i], 0, 0, 0);
            }

            // =========   encoders   =========
            for (int i = 0; i < 6; i++)
            {
                if (knob_page[i] >= Pages(i))
                    knob_page[i] = 0; // the mode changed: robot has more pages
                uint8_t page = Row(i, knob_page[i]);
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
                    SingKnobColour(i, page, knob_page[i], value, r, g, b);
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

                            // page 1 red, brighter further out from the centre
                            SingPageRing(0, fabsf(value - .5f) * 2.f, r, g, b);
                        }
                        else {
                            fx_->SetReverb(value);
                            fx_->SetDelayFeedback(value);

                            SingPageRing(0, value, r, g, b); // SING: space, page 1 red
                        }
                    }
                    else // SING: page 2, filter: low-pass left, off in the middle, high-pass right
                    {
                        fx_->SetFilter(value);
                        SingPageRing(1, fabsf(value - .5f) * 2.f, r, g, b);
                    }


                    SetPthLedFloat(4, r, g, b);

                    break;
                }
                case 4: // SING: time wheel
                {
                    if(fx_->CanFreeze() && fx_->Frozen())
                    {
                        /* left LED: how far back, red; right: now, warm white */
                        const float back = fx_->ScrubPosition();
                        SetPthLedFloat(5, sing_red[0] * back, sing_red[1] * back, sing_red[2] * back);
                        SetPthLedFloat(6, sing_warm[0] * (1.f - back), sing_warm[1] * (1.f - back), sing_warm[2] * (1.f - back));
                    }
                    else
                    {
                        SetPthLedFloat(5, 0.f, 0.f, 0.f);
                        SetPthLedFloat(6, 0.f, 0.f, 0.f);
                    }
                    break;
                }
                case 5: // gain
                {
                    if(batt_display && System::GetNow() - batt_hold > 2000)
                    {
                        /* SING: full warm white, high dim white, medium
                           dim red, low red */
                        const float* color = sing_warm;
                        float lvl = 1.f;
                        switch(hw_->GetBatteryLevel())
                        {
                            case Hardware::BatteryLevel::FULL:
                                break;
                            case Hardware::BatteryLevel::HIGH:
                                lvl = .35f;
                                break;
                            case Hardware::BatteryLevel::MEDIUM:
                                color = sing_red;
                                lvl = .3f;
                                break;
                            case Hardware::BatteryLevel::LOW:
                                color = sing_red;
                                break;
                            default:
                                break;
                        }
                        r = color[0] * lvl;
                        g = color[1] * lvl;
                        b = color[2] * lvl;
                    }
                    else if (page == 0)
                    {
                        float vu_sample = fx_->GetVUSample(VUTarget::VU_OUTPUT);

                        /* SING: output level; the volume scales it, but not
                           below the meter's floor (see SingMeter) */
                        SingMeter(vu_sample, r, g, b);
                        const float k = .4f + .6f * value;
                        r *= k;
                        g *= k;
                        b *= k;

                        fx_->SetMainGain(value);
                    }
                    else
                    {
                        SingPageRing(1, value, r, g, b); // SING: input gain, page 2 blue

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
            // SING: play key = the character's colour
            {
                const float *c = SingCharacterColour(fx_->ChordModeIndex());
                SetPthLedFloat(led_map[33], c[0], c[1], c[2]);
            }

            // SING: loop key = freeze: white while frozen, dim in robot mode, off otherwise
            {
                const float lvl = !fx_->CanFreeze() ? 0.f : fx_->Frozen() ? 1.f : .08f;
                SetPthLedFloat(led_map[34], sing_warm[0] * lvl, sing_warm[1] * lvl, sing_warm[2] * lvl);
            }

            // chompi key
            fx_->SetInputMonitor(true); // SING: dry voice per monitor mode (menu, knob 6)
            if (fx_->IsLatched()) // SING: latch on
            {
                r = sing_red[0];
                g = sing_red[1];
                b = sing_red[2];
            }
            else // input level, as TAPE shows while monitoring
            {
                float vu_sample = fx_->GetVUSample(VUTarget::VU_INPUT);
                SingMeter(vu_sample, r, g, b); // SING: input level
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
                    knob_page[knob] %= Pages(knob);
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

            // SING: big wheel press
            case ENC_5_SW:
            {
                /* SING: pressing the big wheel lets a freeze go: back to live */
                if (rising && fx_->Frozen())
                    fx_->ToggleFreeze();
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
            case static_cast<uint16_t>(Hardware::SwId::KEY_27): // SING: play = next mode
            {
                if (rising)
                    fx_->CycleChordMode();
                hw_->SendCC(midi_channel, 26, rising ? 127 : 0);
                break;
            }
            case static_cast<uint16_t>(Hardware::SwId::KEY_28): // SING: loop = freeze (robot)
            {
                if (rising && fx_->CanFreeze())
                    fx_->ToggleFreeze();
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

            uint8_t page = Row(encoderID, knob_page[encoderID]);
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
                else if (encoderID == 1 && page == 0) // spread
                {
                    inc = turns * kEncoderFineStep;
                }
                else if (encoderID == 2 && page == 0) // doubler
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
                /* SING: the time wheel (robot mode): back through the last
                   ~2 s of the voice; the bar on the white keys shows where,
                   full = now */
                if (fx_->CanFreeze() && stepsPerRevolution == 0)
                    fx_->ScrubTime(turns);
                enc_values[0][4] = 1.f - fx_->ScrubPosition();
                (void)old_val;
            }

            /* SING: show where this knob now is on the white keys, briefly */
            shown_knob_  = encoderID;
            shown_page_  = page;
            shown_disp_page_ = knob_page[encoderID];
            shown_t_     = System::GetNow();

            // no CC for rows without one (SING's robot size/character): 0 is bank select
            if (stepsPerRevolution == 0 && cc_map[page][encoderID] != 0) {
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
        int      shown_knob_ = 0, shown_page_ = 0, shown_disp_page_ = 0;
        uint32_t shown_t_    = 0;
        bool chompi_key_pressed = false;
        uint8_t* knob_page;
        bool split_delay_;

        bool batt_display;
        uint32_t batt_hold;
    };

} // namespace chompi