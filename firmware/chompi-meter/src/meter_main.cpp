// meter: an isorhythmic CHOMPI firmware, written from scratch.
// First hardware version: the keys play mallet sounds on 15 channels.
//
// Start-up, power management and USB hand-over follow the CHOMPI's own
// firmware (board/hardware.h); everything musical lives in Meter.h.

#include "daisysp.h"
#include "board/hardware.h"
#include "board/temp_led_stuff.h"
#include "Meter.h"
#include "../dsp/reverb.h"

#include <cerrno>
#include <cstddef>

#define DSY_DTCMRAM_BSS __attribute__((section(".dtcmram_bss")))

using namespace daisy;
using namespace chompi;

Hardware hw;
meter::Instrument instrument;
daisysp::Reverb DSY_DTCMRAM_BSS reverb;

/* A bounded heap in D2 RAM, for the few things that malloc (the USB stack).
 * newlib's own _sbrk doesn't stop at the end of RAM. */
static uint8_t __attribute__((section(".d2_bss"), aligned(8))) heap_mem[16 * 1024];
static size_t __attribute__((section(".d2_bss"))) heap_used;

extern "C" void *_sbrk(ptrdiff_t incr)
{
    if(incr < 0 ? (size_t)-incr > heap_used : (size_t)incr > sizeof(heap_mem) - heap_used)
    {
        errno = ENOMEM;
        return (void *)-1;
    }
    void *prev = heap_mem + heap_used;
    heap_used += incr;
    return prev;
}

/* The linker script can place code in ITCM; copy it there before anything runs. */
extern "C" uint32_t _itcm_text_vma, _itcm_text_lma, _itcm_text_size;
static void CopyItcmText()
{
    const uint32_t *src = &_itcm_text_lma;
    uint32_t       *dst = &_itcm_text_vma;
    for(uint32_t i = 0; i < uint32_t(&_itcm_text_size) / 4; i++)
        dst[i] = src[i];
    __DSB();
    __ISB();
}
__attribute__((section(".preinit_array"), used)) static void (*const copy_itcm_text)() = CopyItcmText;

/* Outputs 1-2 are the headphones, 3-4 the line out. */
static constexpr float kHpGain = .5f, kLineGain = .7f;

/* Load guard: when the callback keeps needing more than this share of the
 * block, the oldest voice stops, one every few blocks. */
static constexpr float   kGuardLoad = .9f;
static constexpr int32_t kGuardWait = 8;
static float   guard_load = 0.f;
static int32_t guard_wait = 0;
static float   room_set   = -1.f;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    const uint32_t start = DWT->CYCCNT;

    hw.ProcessAllControls();
    instrument.Controls(hw);

    const float room = instrument.Room();
    if(room != room_set)
    {
        room_set = room;
        reverb.SetAmount(room * room * .8f);
        reverb.SetLowpass(room * .6f + .4f);
        reverb.SetDiffusion(room * .6f);
    }

    float l[64], r[64];
    std::fill(l, l + size, 0.f);
    std::fill(r, r + size, 0.f);
    instrument.Process(l, r, size);

    const float vol = instrument.Volume();
    for(size_t i = 0; i < size; i++)
    {
        reverb.Process(&l[i], &r[i]);
        const float a = tanhf(l[i]), b = tanhf(r[i]);
        out[0][i] = a * kHpGain * vol;
        out[1][i] = b * kHpGain * vol;
        out[2][i] = a * kLineGain * vol;
        out[3][i] = b * kLineGain * vol;
    }

    static const float ticks_per_sample = float(SystemCoreClock) / hw.seed.AudioSampleRate();
    const float load = float(DWT->CYCCNT - start) / (ticks_per_sample * size);
    guard_load += .25f * (load - guard_load);
    instrument.SetLoad(guard_load);
    if(guard_wait > 0)
        guard_wait--;
    else if(guard_load > kGuardLoad)
    {
        instrument.ShedVoice();
        guard_wait = kGuardWait;
    }
}

int main(void)
{
    /* D2 RAM (the heap) needs its clocks before anything touches it. */
    RCC->AHB2ENR |= RCC_AHB2ENR_D2SRAM1EN | RCC_AHB2ENR_D2SRAM2EN | RCC_AHB2ENR_D2SRAM3EN;
    (void)RCC->AHB2ENR;
    heap_used = 0;

    hw.Init();
    hw.MpWrite(0x0c, 0B01010001); // BATT_LOW at 3 V
    hw.MpReadAll();
    for(size_t i = 0; i < 10; i++)
    {
        hw.LowBatteryLockoutCheck();
        System::Delay(10);
    }

    LedSetup();

    /* Clear junk out of the shift registers (0.5 s). Holding CHOMPI, play and
     * loop while switching on puts the CHOMPI to sleep (shipping mode), as in
     * every CHOMPI firmware. */
    uint32_t sleep_state = 0;
    for(int i = 0; i < 5000; i++)
    {
        hw.ProcessAllControls();
        sleep_state += hw.button_sr.State(int(Hardware::SwId::KEY_26))
                       && hw.button_sr.State(int(Hardware::SwId::KEY_27))
                       && hw.button_sr.State(int(Hardware::SwId::KEY_28));
        System::DelayUs(100);
    }
    if(sleep_state > 4000)
        hw.MpWrite(0x08, 0B10111111); // shipping mode

    const float sr = hw.seed.AudioSampleRate();
    instrument.Init(sr);
    reverb.Init(sr);
    reverb.Clear();
    reverb.SetInputGain(.3f);
    reverb.SetTime(.9f);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // cycle counter for the load guard
    DWT->LAR = 0xC5ACCE55;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    hw.StartAudio(AudioCallback);

    /* USB: let the charger detection run, then take the port. */
    hw.usb_sw.Write(false);
    System::Delay(1);
    hw.MpWrite(0x0a, 0B00100100); // auto DPDM
    System::Delay(1);
    hw.usb_sw.Write(true);

    uint32_t now = System::GetNow(), ledt = now, batt = now, usbt = now;
    bool prev_int_state = true, usb_handoff = false;

    while(1)
    {
        now = System::GetNow();

        if(instrument.RebootNow())
        {
            hw.LedsOff();
            System::Delay(20);
            NVIC_SystemReset();
        }

        if(now - ledt >= 16) // about 60 frames a second
        {
            instrument.DrawLeds(hw, (now - ledt) * .001f);
            ledt = now;
        }

        /* Battery and USB, as in the CHOMPI's firmware. */
        const bool int_state = hw.mpc_int.Read();
        const bool interrupt = !int_state && prev_int_state;
        prev_int_state       = int_state;

        if(now - batt > 20)
        {
            hw.LowBatteryLockoutCheck();
            batt = now;
        }

        if(interrupt)
        {
            hw.MpReadAll();
            while(!hw.read_ready)
                System::Delay(1);
            if((hw.mp_buff_[0] & 0B11110000) == 0) // DPDM_STAT unknown
            {
                hw.USBMidiActive(false);
                hw.usb_sw.Write(false); // give USB control
                usb_handoff = true;
                usbt        = now;
            }
            else
            {
                hw.usb_sw.Write(true); // take USB control
                hw.USBMidiActive(true);
            }
        }
        if(usb_handoff && now - usbt > 1)
        {
            usb_handoff = false;
            hw.MpWrite(0x0a, 0B00110100); // FORCEDPDM
        }

        System::DelayUs(10);
    }
}
