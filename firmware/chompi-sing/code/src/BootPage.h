#include "hardware.h"
#include "DSPEngine.h"
#include "temp_led_stuff.h"

namespace chompi
{
    class BootPage : public daisy::UiPage
    {
    public:

        void Init(Hardware* hw, Engine* fx)
        {
            hw_ = hw;
            fx_ = fx;
            RandomColors();
        }

        void RandomColors()
        {
            /* SING: a colour from the "warm stage" palette */
            static const float kWarm[6][3] = {
                {1.f, .12f, .47f}, {1.f, .45f, .60f}, {1.f, .42f, .30f},
                {1.f, .55f, 0.f},  {1.f, .78f, .10f}, {1.f, .85f, .65f},
            };
            const float *c = kWarm[System::GetNow() % 6];
            r = c[0];
            g = c[1];
            b = c[2];
        }

        void Draw(const daisy::UiCanvasDescriptor &canvasDescriptor) override
        {
            bright += bright_inc;
            if(bright > 1.f)
            {
                bright_inc *= -1.f;
            }
            else if(bright < 0.f)
            {
                RandomColors();
                bright_inc *= -1.f;
            }                

            for(size_t i = 0; i < kNumPthLeds; i++)
            {
                SetPthLedFloat(i, r * bright, g * bright, b * bright);
            }

            for(size_t i = 0; i < kNumSmtLeds; i++)
            {
                SetSmtLedFloat(i, r * bright, g * bright, b * bright);
            }

            // ========   send the data   =========
            fill_led_data();
        }

        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            return false; // do nothing
        }

        bool OnButton(uint16_t buttonID,
                uint8_t numberOfPresses,
                bool isRetriggering) override
        {
            return false; // do nothing
        }

    private:
        Hardware* hw_;
        Engine* fx_;
        float r = 0.f;
        float g = 0.f;
        float b = 0.f;
        float bright = 0.f;
        float bright_inc = .01f;
    };
} // namespace chompi
