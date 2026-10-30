// meter: an isorhythmic CHOMPI firmware, written from scratch.
// First hardware version: the keys play mallet sounds on 15 channels.
//
// Start-up, power management and USB hand-over follow the CHOMPI's own
// firmware (board/hardware.h); everything musical lives in Meter.h.

#include "daisysp.h"
#include "board/hardware.h"
#include "board/temp_led_stuff.h"
#include "Meter.h"
#include "Link.h"
#include "../dsp/room.h"

#include <cerrno>
#include <cstddef>

using namespace daisy;
using namespace chompi;

Hardware hw;
meter::Instrument instrument;
meter::Link page_link;
meter::Room room_rv;
static float DSY_SDRAM_BSS room_mem[meter::Room::kMemory];
// The figures' versions, as raw memory: nothing may be constructed in SDRAM
// before it is set up; Instrument::Init sets the rings up.
alignas(8) static uint8_t DSY_SDRAM_BSS versions_mem[sizeof(meter::Versions)];
// The study voices' delay lines, a piece for each voice of the pool.
static float DSY_SDRAM_BSS voice_mem[meter::kVoices * meter::AnyVoice::kArenaFloats];

/* Wet level of the room at room .5, about 10 dB under the dry sound (measured
 * on an impulse). */
static constexpr float kRoomWet = .77f;

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

/* Output limiter: above kLimitLevel the sum is scaled down. */
static constexpr float kLimitLevel = .6f, kLimitAttack = .2f, kLimitRelease = .99996f;
static float limit_env = 0.f;

/* USB. The CHOMPI's data lines run through a switch shared with the MP2722
 * charger, which needs them for a moment to work out what kind of port it is
 * plugged into; until the Daisy takes them back, the host sees no MIDI device.
 * This is the launcher's version of TAPE's handshake (chompi-launcher,
 * ServiceUsbSwitch), which fixed three ways TAPE's left USB dark: a charger
 * read that never lands (TAPE waits forever), a port that is never identified
 * (a Mac's USB-C reads "unknown" every time, and TAPE lends the lines out
 * again every ~230 ms), and a second interrupt that never comes. */
static bool     prev_int_state = true, usb_handoff = false, usb_lent = false;
static uint32_t usb_handoff_t = 0, usb_lent_t = 0, usb_quiet_t = 0;

/* Every charger register, waiting for the I2C transfer to land; false if it
 * did not within half a second. */
static bool ChargerRead()
{
    hw.MpReadAll();
    const uint32_t started = System::GetNow();
    while(!hw.read_ready && System::GetNow() - started < 500)
        System::Delay(1);
    return hw.read_ready;
}

/* At power-on: clear the charger's pending status, give it a moment, let it
 * detect the port, then take the lines. */
static void UsbTakeOver()
{
    ChargerRead();
    System::Delay(100);
    hw.usb_sw.Write(false);        // give USB to the charger
    System::Delay(1);
    hw.MpWrite(0x0a, 0B00100100);  // auto DPDM
    System::Delay(1);
    hw.usb_sw.Write(true);         // take it back
    hw.USBMidiActive(true);
}

/* The charger raised its interrupt: a cable came or went. If it does not know
 * the port yet, lend it the lines and force a fresh detection; once it does,
 * or once a forced detection comes back empty, or 1.5 s later at the latest,
 * take them back (the host enumerates either way). */
static void ServiceUsbSwitch(uint32_t now)
{
    const bool int_state = hw.mpc_int.Read();
    const bool interrupt = !int_state && prev_int_state;
    prev_int_state       = int_state;

    if(interrupt)
    {
        if(!ChargerRead())
            prev_int_state = true; // stale registers: look again next pass
        else if((hw.mp_buff_[0] & 0B11110000) == 0 && usb_lent)
        {
            hw.usb_sw.Write(true); // our forced detection found nothing
            hw.USBMidiActive(true);
            usb_lent    = false;
            usb_quiet_t = now + 5000;
        }
        else if((hw.mp_buff_[0] & 0B11110000) == 0 && int32_t(now - usb_quiet_t) < 0)
        {
            // still settling from the last round: the lines stay with us
        }
        else if((hw.mp_buff_[0] & 0B11110000) == 0) // DPDM_STAT unknown
        {
            hw.USBMidiActive(false);
            hw.usb_sw.Write(false);
            usb_handoff   = true;
            usb_handoff_t = now;
            usb_lent      = true;
            usb_lent_t    = now;
        }
        else
        {
            hw.usb_sw.Write(true);
            hw.USBMidiActive(true);
            usb_lent = false;
        }
    }

    if(usb_handoff && now - usb_handoff_t > 1)
    {
        usb_handoff = false;
        hw.MpWrite(0x0a, 0B00110100); // force DPDM
    }

    if(usb_lent && now - usb_lent_t > 1500)
    {
        hw.usb_sw.Write(true);
        hw.USBMidiActive(true);
        usb_lent    = false;
        usb_quiet_t = now + 5000;
    }
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    const uint32_t start = DWT->CYCCNT;

    hw.ProcessAllControls();
    instrument.Controls(hw, size);

    const float room = instrument.Room();
    if(room != room_set)
    {
        room_set = room;
        room_rv.SetDecay(room);
    }
    /* The room knob also sets how much: none at 0, twice the matched level at 1. */
    const float wet = 2.f * room * kRoomWet;

    float l[64], r[64], sl[64], sr[64];
    std::fill(l, l + size, 0.f);
    std::fill(r, r + size, 0.f);
    std::fill(sl, sl + size, 0.f);
    std::fill(sr, sr + size, 0.f);
    instrument.Process(l, r, sl, sr, size);

    const float vol = instrument.Volume();
    for(size_t i = 0; i < size; i++)
    {
        float wl, wr;
        room_rv.Process(sl[i], sr[i], &wl, &wr);
        l[i] += wet * wl;
        r[i] += wet * wr;

        /* Limiter: many long notes add up, so the sum is turned down smoothly
         * (fast down, about half a second back up) before it can distort. */
        const float peak = std::max(fabsf(l[i]), fabsf(r[i]));
        limit_env = peak > limit_env ? limit_env + kLimitAttack * (peak - limit_env)
                                     : limit_env * kLimitRelease;
        const float g = limit_env > kLimitLevel ? kLimitLevel / limit_env : 1.f;
        const float a = tanhf(l[i] * g), b = tanhf(r[i] * g);
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
    instrument.Init(sr, reinterpret_cast<meter::Versions*>(versions_mem), voice_mem);
    room_rv.Init(sr, room_mem);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // cycle counter for the load guard
    DWT->LAR = 0xC5ACCE55;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    hw.StartAudio(AudioCallback);

    UsbTakeOver();

    uint32_t now = System::GetNow(), ledt = now, batt = now;

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

        page_link.Poll(hw, instrument, now);

        if(now - batt > 20)
        {
            hw.LowBatteryLockoutCheck();
            batt = now;
        }
        ServiceUsbSwitch(now);

        System::DelayUs(10);
    }
}
