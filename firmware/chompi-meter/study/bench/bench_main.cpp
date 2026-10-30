// CIRCLES sound study: how much of the CHOMPI each new voice costs.
//
// A test firmware, not an instrument. At power-on it times every voice of the
// study (../instruments.h) on the CHOMPI's own core, at C3, C4 and C5, for the
// first half second of a note, next to today's mallet voice (dsp/Mallet.h),
// the room (dsp/room.h) and a player's place on the circle (dsp/Circle.h).
// Then it waits for bench.html over USB-MIDI and sends the results.
//
// Timing: the core's cycle counter around blocks of 48 samples (as the audio
// callback runs them), interrupts off inside a block. No audio runs meanwhile.
// The voices run from cached SRAM, CIRCLES' audio path from ITCM: the mallet
// is timed the same way, so the ratios hold; the absolute cycles are a little
// on the safe side.

#include "daisysp.h"
#include "board/hardware.h"
#include "board/temp_led_stuff.h"
#include "../instruments.h"
#include "../../dsp/Mallet.h"
#include "../../dsp/Presets.h"
#include "../../dsp/room.h"
#include "../../dsp/Circle.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

using namespace daisy;
using namespace chompi;

Hardware hw;

// A heap for the voices' delay lines (std::vector), in plain SRAM.
static uint8_t __attribute__((aligned(8))) heap_mem[64 * 1024];
static size_t heap_used;
extern "C" void* _sbrk(ptrdiff_t incr) {
    if (incr < 0 ? size_t(-incr) > heap_used : size_t(incr) > sizeof(heap_mem) - heap_used) {
        errno = ENOMEM;
        return (void*)-1;
    }
    void* prev = heap_mem + heap_used;
    heap_used += incr;
    return prev;
}

// The linker script can place code in ITCM (sinf, expf ...): copy it there first.
extern "C" uint32_t _itcm_text_vma, _itcm_text_lma, _itcm_text_size;
static void CopyItcmText() {
    const uint32_t* src = &_itcm_text_lma;
    uint32_t* dst = &_itcm_text_vma;
    for (uint32_t i = 0; i < uint32_t(&_itcm_text_size) / 4; i++) dst[i] = src[i];
    __DSB();
    __ISB();
}
__attribute__((section(".preinit_array"), used)) static void (*const copy_itcm_text)() = CopyItcmText;

static DSY_SDRAM_BSS float room_mem[meter::Room::kMemory];

static constexpr int kBlock = 48;
static float sr = 48000.f;
static float Hz(float midi) { return 440.f * std::pow(2.f, (midi - 69.f) / 12.f); }

// ---- Results, as text lines for the page ------------------------------------
static constexpr int kLines = 80, kLineLen = 96;
static char lines[kLines][kLineLen];
static int nlines = 0;
static void Line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void Line(const char* fmt, ...) {
    if (nlines >= kLines) return;
    va_list a;
    va_start(a, fmt);
    vsnprintf(lines[nlines++], kLineLen, fmt, a);
    va_end(a);
}

struct Timing { uint32_t strike, worst; float avg; };
static volatile float sink;

// One voice, one note: the strike itself, then half a second in blocks.
template <class V, class F>
static Timing TimeBlocks(V& v, F&& tick, uint32_t strike_cycles) {
    const int blocks = int(.5f * sr / kBlock);
    uint64_t total = 0;
    uint32_t worst = 0;
    for (int b = 0; b < blocks; ++b) {
        float acc = 0.f;
        __disable_irq();
        const uint32_t t0 = DWT->CYCCNT;
        for (int i = 0; i < kBlock; ++i) acc += tick();
        const uint32_t dt = DWT->CYCCNT - t0;
        __enable_irq();
        sink = sink + acc;
        total += dt;
        worst = std::max(worst, dt);
    }
    return {strike_cycles, worst, float(total) / float(blocks * kBlock)};
}

template <class V>
static void Bench(const char* name, sd::Knobs k) {
    float sum = 0.f;
    uint32_t worst = 0, strike = 0;
    for (float midi : {48.f, 60.f, 72.f}) {
        V* v = new V();
        v->Init(sr, 7);
        uint32_t t0 = DWT->CYCCNT;
        v->Strike(Hz(midi), .8f, k);
        const uint32_t st = DWT->CYCCNT - t0;
        const Timing t = TimeBlocks(*v, [v] { return v->V::Tick(); }, st);
        sum += t.avg;
        worst = std::max(worst, t.worst);
        strike = std::max(strike, t.strike);
        delete v;
    }
    // name, tenths of a cycle per sample (average of three notes; the libc has no %f), worst block, strike
    Line("v,%s,%lu,%lu,%lu", name, (unsigned long)(sum / 3.f * 10.f), (unsigned long)worst, (unsigned long)strike);
}

static void BenchMallet(const char* name, int preset) {
    float sum = 0.f;
    uint32_t worst = 0, strike = 0;
    for (float midi : {48.f, 60.f, 72.f}) {
        meter::MalletVoice* v = new meter::MalletVoice();
        v->Init(sr, 7);
        uint32_t t0 = DWT->CYCCNT;
        v->Strike(Hz(midi), meter::kMalletPresets[preset].p, .8f);
        const uint32_t st = DWT->CYCCNT - t0;
        const Timing t = TimeBlocks(*v, [v] { return v->Process(); }, st);
        sum += t.avg;
        worst = std::max(worst, t.worst);
        strike = std::max(strike, t.strike);
        delete v;
    }
    Line("m,%s,%lu,%lu,%lu", name, (unsigned long)(sum / 3.f * 10.f), (unsigned long)worst, (unsigned long)strike);
}

static void BenchFixed() {
    // The room: per sample, always running.
    meter::Room room;
    room.Init(sr, room_mem);
    sd::Rng r;
    uint64_t total = 0;
    const int blocks = 200;
    for (int b = 0; b < blocks; ++b) {
        float acc = 0.f, l, rr;
        __disable_irq();
        const uint32_t t0 = DWT->CYCCNT;
        for (int i = 0; i < kBlock; ++i) { room.Process(r.B() * .1f, r.B() * .1f, &l, &rr); acc += l + rr; }
        total += DWT->CYCCNT - t0;
        __enable_irq();
        sink = sink + acc;
    }
    Line("f,room,%lu", (unsigned long)(float(total) * 10.f / (blocks * kBlock)));
    // A player's place on the circle: per sample, while the player sounds.
    meter::Seat seat;
    seat.Init(sr);
    seat.Set(.2f, 1.f);
    seat.Snap();
    float in[kBlock], l[kBlock], rr[kBlock], sl[kBlock], sr2[kBlock];
    total = 0;
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < kBlock; ++i) { in[i] = r.B() * .1f; l[i] = rr[i] = sl[i] = sr2[i] = 0.f; }
        __disable_irq();
        const uint32_t t0 = DWT->CYCCNT;
        seat.Process(in, l, rr, sl, sr2, kBlock);
        total += DWT->CYCCNT - t0;
        __enable_irq();
        sink = sink + l[3];
    }
    Line("f,seat,%lu", (unsigned long)(float(total) * 10.f / (blocks * kBlock)));
}

static void RunAll() {
    Line("h,%lu,%lu,%d", (unsigned long)SystemCoreClock, (unsigned long)sr, kBlock);
    BenchMallet("Marimba", 1);
    BenchMallet("Bronze", 3);
    BenchMallet("Gong", 6);
    BenchMallet("Muffled", 9);
    BenchFixed();
    using namespace sd;
    auto K = [](float a, float b, float c, float d, float e, float f) { return Knobs{a, b, c, d, e, f}; };
    // Each at its own setting (as in the study), and once more with what costs most.
    Bench<Gangsa>("Gangsa", K(.70f, .55f, .60f, .75f, .30f, .30f));
    Bench<Mbira>("Mbira", K(.55f, .50f, .55f, .00f, .30f, .50f));
    Bench<Mbira>("Mbira +twin", K(.55f, .50f, .55f, .60f, .30f, .50f));
    Bench<Amadinda>("Amadinda", K(.70f, .50f, .40f, .10f, .35f, .30f));
    Bench<Siku>("Siku", K(.60f, .45f, .30f, .30f, .35f, .15f));
    Bench<Kora>("Kora", K(.55f, .55f, .50f, .30f, .30f, .25f));
    Bench<Voices>("Voices", K(.50f, .50f, .25f, .30f, .35f, .40f));
    Bench<Voices>("Voices x3", K(.50f, .50f, .25f, 1.f, .35f, .40f));
    Bench<Hands>("Hands", K(.50f, .50f, .30f, .30f, .40f, .20f));
    Bench<Hands>("Hands x4", K(.50f, .50f, .30f, 1.f, .40f, .20f));
    Bench<Bowed>("Bowed glass", K(.25f, .45f, .50f, .30f, .30f, .00f));
    Bench<Bowl>("Singing bowl", K(.45f, .50f, .60f, .35f, .20f, .00f));
    Bench<Bowl>("Bowl rubbed", K(.45f, .50f, .60f, .35f, .20f, .75f));
    Bench<Piano>("Pulse piano", K(.50f, .50f, .50f, .10f, .25f, .00f));
    Bench<Piano>("Piano bolt", K(.50f, .50f, .50f, .10f, .25f, .70f));
    Bench<Hang>("Hang", K(.40f, .50f, .55f, .30f, .25f, .00f));
    Bench<Tape>("Tape", K(.30f, .40f, .50f, .30f, .40f, .30f));
    Line("e,%d", nlines + 1);
}

// ---- USB (as CIRCLES does it, see src/meter_main.cpp) -------------------------
static bool prev_int_state = true, usb_handoff = false, usb_lent = false;
static uint32_t usb_handoff_t = 0, usb_lent_t = 0, usb_quiet_t = 0;
static bool ChargerRead() {
    hw.MpReadAll();
    const uint32_t started = System::GetNow();
    while (!hw.read_ready && System::GetNow() - started < 500) System::Delay(1);
    return hw.read_ready;
}
static void UsbTakeOver() {
    ChargerRead();
    System::Delay(100);
    hw.usb_sw.Write(false);
    System::Delay(1);
    hw.MpWrite(0x0a, 0B00100100);
    System::Delay(1);
    hw.usb_sw.Write(true);
    hw.USBMidiActive(true);
}
static void ServiceUsbSwitch(uint32_t now) {
    const bool int_state = hw.mpc_int.Read();
    const bool interrupt = !int_state && prev_int_state;
    prev_int_state = int_state;
    if (interrupt) {
        if (!ChargerRead()) prev_int_state = true;
        else if ((hw.mp_buff_[0] & 0B11110000) == 0 && usb_lent) {
            hw.usb_sw.Write(true); hw.USBMidiActive(true); usb_lent = false; usb_quiet_t = now + 5000;
        } else if ((hw.mp_buff_[0] & 0B11110000) == 0 && int32_t(now - usb_quiet_t) < 0) {
        } else if ((hw.mp_buff_[0] & 0B11110000) == 0) {
            hw.USBMidiActive(false); hw.usb_sw.Write(false);
            usb_handoff = true; usb_handoff_t = now; usb_lent = true; usb_lent_t = now;
        } else {
            hw.usb_sw.Write(true); hw.USBMidiActive(true); usb_lent = false;
        }
    }
    if (usb_handoff && now - usb_handoff_t > 1) { usb_handoff = false; hw.MpWrite(0x0a, 0B00110100); }
    if (usb_lent && now - usb_lent_t > 1500) {
        hw.usb_sw.Write(true); hw.USBMidiActive(true); usb_lent = false; usb_quiet_t = now + 5000;
    }
}

// Page -> bench: F0 7D 42 4E 01 F7 (hello). Bench -> page: F0 7D 42 4E 02 <line> F7, one per line.
static void SendAll() {
    uint8_t b[kLineLen + 8];
    for (int i = 0; i < nlines; ++i) {
        int n = 0;
        b[n++] = 0xF0; b[n++] = 0x7D; b[n++] = 0x42; b[n++] = 0x4E; b[n++] = 0x02;
        for (const char* c = lines[i]; *c && n < int(sizeof b) - 1; ++c) b[n++] = uint8_t(*c) & 127;
        b[n++] = 0xF7;
        hw.usb_midi.SendMessage(b, n);
        System::Delay(4);
    }
}

int main(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_D2SRAM1EN | RCC_AHB2ENR_D2SRAM2EN | RCC_AHB2ENR_D2SRAM3EN;
    (void)RCC->AHB2ENR;
    heap_used = 0;

    hw.Init();
    hw.MpWrite(0x0c, 0B01010001);
    hw.MpReadAll();
    for (size_t i = 0; i < 10; i++) { hw.LowBatteryLockoutCheck(); System::Delay(10); }
    LedSetup();
    sr = hw.seed.AudioSampleRate();

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    RunAll(); // a few seconds

    UsbTakeOver();
    uint32_t batt = System::GetNow();
    while (1) {
        const uint32_t now = System::GetNow();
        bool hello = false;
        while (hw.usb_midi.HasEvents()) {
            MidiEvent e = hw.usb_midi.PopEvent();
            if (e.type == SystemCommon && e.sc_type == SystemExclusive && e.sysex_message_len >= 4
                && e.sysex_data[0] == 0x7D && e.sysex_data[1] == 0x42 && e.sysex_data[2] == 0x4E && e.sysex_data[3] == 0x01)
                hello = true;
        }
        if (hello && hw.usb_midi_active) SendAll();
        if (now - batt > 20) { hw.LowBatteryLockoutCheck(); batt = now; }
        ServiceUsbSwitch(now);
        System::DelayUs(10);
    }
}
