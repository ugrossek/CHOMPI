#include "hardware.h"
#include "ui_utils.h"
#include "NormalPage.h"
#include "MenuPage.h"
#include "TestPage.h"
#include "BootPage.h"
#include "NoSDPage.h"
#include "RainbowWavePage.h"
#include "DSPEngine.h"
// #include "StereoDelayEffect.h"

namespace chompi
{

    static const float enc_defaults[3][6] = {
        /* SING, knobs 1-3: transpose (.5 = 0), spread, doubler /
           harmony volume, attack, release -- must match Harmonizer::Init */
        {.5f, 0.f, 0.f, 0.f, .75f, .84f},  // page 1
        {.75f, .1f, .5f, 0.f, 0.f, .75f},  // page 2
        {0.f, .5f, .5f, .5f, 0.f, 0.f},   // page 3; SING: metal off; robot size, character (.5 = as sung, sawtooth)
    };

    static const uint8_t midi2key[49] = {
        44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
        32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
        15, 7, 8, 12, 9, 10, 13, 11,
        14, 16, 21, 17, 18, 22, 19, 23,
        20, 24, 29, 25, 30, 26, 31, 27, 28
    };

    static const uint8_t encoder_map[6] = {1, 2, 3, 0, 4, 5};
    static const uint32_t kMaxFileWriteChunkSize = 1024;

    enum CanvasIds
    {
        canvasLedDisplay = 0,
        NUM_CANVASES
    };

    enum UiMode
    {
        MIDI,
    };

    class UserInterface
    {
    public:
        void Init(Hardware *hw, Engine *fx,
                    uint8_t ch_in, uint8_t ch_out, bool pitch_shift_quant, bool split_delay)
        {
            hw_ = hw;
            fx_ = fx;

            midi_in_ch = ch_in;

            /** Describe UI special controls - if any */
            daisy::UI::SpecialControlIds specialControlIds; /**< None here */
            /** Canvas Descriptor */
            daisy::UiCanvasDescriptor ledDisplayDescriptor;
            ledDisplayDescriptor.id_ = canvasLedDisplay;
            ledDisplayDescriptor.handle_ = nullptr;
            ledDisplayDescriptor.updateRateMs_ = 16; /**< 30Hz */
            ledDisplayDescriptor.clearFunction_ = ClearLeds;
            ledDisplayDescriptor.flushFunction_ = FlushLeds;

            /** Init */
            ui.Init(event_queue,
                    specialControlIds,
                    {ledDisplayDescriptor},
                    canvasLedDisplay);

            normal_page_.Init(hw_, fx_, enc_rows, 
                def_rows, knob_page, ch_out, pitch_shift_quant, split_delay);
            ui.OpenPage(normal_page_);

            boot_page_.Init(hw_, fx_);
            ui.OpenPage(boot_page_);

            menu_page_.Init(hw_, fx_, enc_rows, def_rows, knob_page,
                pitch_shift_quant, split_delay);

            test_page_.Init(hw_, fx_);

            uint16_t state = 0;
            for (int i = 0; i < 512; i++)
            {
                hw->ProcessAllControls();
                daisy::System::DelayUs(500);
                state += !hw_->button_sr.State(static_cast<int>(Hardware::SwId::SW_TOG));
            }

            // state should be debounced
            if (state >= 480)
                event_queue.AddButtonPressed(static_cast<int>(Hardware::SwId::SW_TOG), 1);
        }

        inline bool InTestMode() { return test_page_.IsActive(); }
        void TestMode()
        {
            ui.OpenPage(test_page_);
            normal_page_.SetInitIgnore(false);
        }

        void NoSDCard()
        {
            menu_page_.NoSDCard();
        }

        void NoSDCardAnimation(bool run)
        {
            if(run)
            {
                ui.ClosePage(menu_page_);
                ui.ClosePage(test_page_);
                ui.OpenPage(no_sd_page_);
            }
            else
                ui.ClosePage(no_sd_page_);
        }

        void RainbowWave() 
        { 
            if(!test_page_.IsActive()) 
                ui.OpenPage(rainbow_page_);
        }
        inline bool InRainbows() { return rainbow_page_.IsActive(); }

        void StopBootAnimation()
        {
            ui.ClosePage(boot_page_);
        }

        bool key_cc[2];
        void ProcessMidi()
        {
            daisy::MidiEvent event;
            while(hw_->GetMidi(event))
            {
                // only accept input on channel 1
                if(event.channel != midi_in_ch)
                    continue;

                switch(event.type)
                {
                    case NoteOn:
                    {
                        int key = event.data[0];
                        key -= 24;
                        if(key > 48|| key < 0)
                            break;

                        if(!test_page_.IsActive()) {
                            /* SING: semitones from C3 (key 8), so a MIDI note
                               sounds at its own pitch */
                            fx_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, 
                                key - 24, midi2key[key], event.data[1] + 1));
                        }
                        else {
                            event_queue.AddButtonPressed(midi2key[key], 1, true);
                        }
                    }
                    break;
                    case NoteOff:
                    {
                        int key = event.data[0];
                        key -= 24;
                        if(key > 48|| key < 0)
                            break;

                        fx_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 
                            key - 24, midi2key[key], event.data[1] + 1));

                        if (test_page_.IsActive()) {
                            event_queue.AddButtonReleased(midi2key[key]);
                        }
                    }
                    break;
                    case ControlChange:
                    {
                        if(menu_page_.IsActive())
                            break;

                        uint8_t cc = event.data[0];
                        uint8_t val = event.data[1];

                        if(cc >= 20 && cc < 26)
                        {
                            uint8_t knob = cc - 20;

                            event_queue.AddEncoderTurned(knob, val, 1);
                        }
                        else if(cc == 26 || cc == 27)
                        {
                            const uint8_t idx = cc - 26;

                            const bool last = key_cc[idx];

                            // top 1/3 is high, bottom 1/3 is low, middle 1/3 is dead zone
                            if(val > 84)
                                key_cc[idx] = true;
                            else if(val < 42)
                                key_cc[idx] = false;

                            if(!last && key_cc[idx]) // rising edge
                            {
                                event_queue.AddButtonPressed(cc + 7, 1, true);
                            }
                            else if(last && !key_cc[idx]) // falling edge
                            {
                                event_queue.AddButtonReleased(cc + 7);
                            }
                        }
                    }
                    default:
                    break;
                }
            }
        }

        inline bool GetToggleState() { return toggle_state; }

        uint32_t last_force_off;
        void GenerateEvents()
        {

            /** 
                Hack to fix stuck notes for now.
                Every so often sends another message to turn off a voice that has a key assigned
                if that key is in the off state 

                Interferes with midi on :(
            */
            toggle_state = hw_->GetToggleState();

            if(menu_page_.IsActive() && menu_page_.IsClosable())
            {
                ui.ClosePage(menu_page_);
                normal_page_.ResetSmtLeds();
            }

            if(test_page_.IsClosable() && test_page_.IsActive())
            {
                ui.ClosePage(test_page_);
                normal_page_.ResetSmtLeds();
            }

            if(rainbow_page_.IsClosable() && rainbow_page_.IsActive())
            {
                ui.ClosePage(rainbow_page_);
                normal_page_.ResetSmtLeds();
            }


            for (int i = 0; i < static_cast<int>(Hardware::SwId::SR_LAST); i++)
            {
                if (i == ENC_5_SW)
                    continue; // skip this one
                else if(i == static_cast<int>(Hardware::SwId::SW_TOG))
                {
                    // this should be smoothed
                    normal_page_.SetSwitchState(toggle_state);
                    test_page_.SetSwitchState(toggle_state);
                    menu_page_.SetSwitchState(toggle_state);
                }
                else if (hw_->button_sr.FallingEdge(i))
                {
                    // chompi key changes page to MenuPage
                    if (i == static_cast<int>(Hardware::SwId::KEY_26))
                    {
                        // normal_page_.RefreshLeds();
                    }
                    event_queue.AddButtonReleased(i);
                }
                else if (hw_->button_sr.RisingEdge(i))
                {
                    // chompi key opens the menu (SING: whatever the toggle says,
                    // which is latch here)
                    if (i == static_cast<int>(Hardware::SwId::KEY_26)
                        && normal_page_.IsActive()
                        && !boot_page_.IsActive()
                        && !rainbow_page_.IsActive()
                        && !test_page_.IsActive())
                    {
                        ui.OpenPage(menu_page_);
                    }
                    event_queue.AddButtonPressed(i, 1);
                }
            }

            if (hw_->enc[4].FallingEdge())
                event_queue.AddButtonReleased(ENC_5_SW);
            else if (hw_->enc[4].RisingEdge())
                event_queue.AddButtonPressed(ENC_5_SW, 1);

            for (int i = 0; i < static_cast<int>(Hardware::EncoderId::ENC_LAST); i++)
            {
                int inc = hw_->enc[i].Increment();
                if (inc == 1 || inc == -1)
                {
                    uint8_t enc = encoder_map[i];

                    if(enc == 0 || enc == 4) // pitch knobs are finer
                        event_queue.AddEncoderTurned(enc, inc, 0);
                    else
                        event_queue.AddEncoderTurned(enc, inc * 3, 0);
                }
            }
        }

        void DoEvents() { ui.Process(); }

        uint32_t file_len = 0;
        // for some reason this causes clicking if you optimize it higher than this...
        // inline void TrigClear() { normal_page_.TrigClear(); }

    inline void TestPowerCable(bool cable) { test_page_.SetPowerCable(cable); }
    inline void TestBMC(bool good) { test_page_.SetBMCGood(good); }

    // public so we can check IsActive from main
    BootPage boot_page_;
    NormalPage normal_page_;
    MenuPage menu_page_;
    TestPage test_page_;
    NoSDPage no_sd_page_;
    RainbowPage rainbow_page_;
    private:
        daisy::UiEventQueue event_queue;
        daisy::UI ui;
        Hardware *hw_;
        Engine *fx_;

        bool toggle_state;

        uint8_t midi_in_ch;

        // this way both UI pages can interact with it
        float enc_values[3][6];
        float* enc_rows[3] = {enc_values[0], enc_values[1], enc_values[2]};

        const float* def_rows[3] = {enc_defaults[0], enc_defaults[1], enc_defaults[2]};

        uint8_t knob_page[6] = {0, 0, 0, 0, 0, 0};

        // Effect*                                   fx_;
        // PotListener                               listener;
        // daisy::PotMonitor<PotListener, KNOB_LAST> pot_monitor;
    };

} // namespace chompi