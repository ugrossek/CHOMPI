#include "hardware.h"
#include "temp_led_stuff.h"
#include "ui.h"
#include "daisysp.h"
#include "fatfs.h"
#include "diskio.h"
#include "DSPEngine.h"
#include "RamBuffer.h"
#include "InterpolatedDelayLine.h"
#include "OptionsManager.h"
#include <cerrno>
#include <cstddef>

#define DSY_DTCMRAM_BSS __attribute__((section(".dtcmram_bss")))

using namespace daisy;
using namespace chompi;

Hardware hw;
UserInterface ui;

SdmmcHandler sdmmc;
FatFSInterface fsi;
Engine engine;
OptionsManager options;

daisysp::Reverb DSY_DTCMRAM_BSS reverb;
chompi::InterpolatedDelayLine::AudioSample DSY_SDRAM_BSS del_mem[kMaxDelayTime];

RamBufferMemory loop_buff;
int16_t DSY_SDRAM_BSS loop_mem[kMaxRamBuffSize]; 


// pitch shifter delay lines, one per voice
float DSY_SDRAM_BSS chompi::shift_mem[kMaxPoly][chompi::kShiftBufFrames * 2];
int16_t DSY_DTCMRAM_BSS chompi::shift_ana[kMaxPoly][chompi::kShiftAnaLen];

/* The harmonizer runs per sample in the audio callback, so it lives in DTCM,
 *  the fastest RAM. Harmonizer::Init() sets everything, so the missing
 *  zeroing there does not matter. */
chompi::Harmonizer<kMaxPoly> DSY_DTCMRAM_BSS harmonizer;
float DSY_SDRAM_BSS chompi::chorus_mem[2][chompi::kChorusLen];

/* The heap, for the few things that malloc -- chiefly the USB serial port,
 *  which calloc's ~550 bytes when a computer configures the device.
 *
 *  newlib's own _sbrk hands out whatever SRAM .bss leaves over, and does not
 *  stop at the end of RAM. The pitch shifter left 432 bytes there, so with USB
 *  plugged in that allocation ran off the end and TAPE faulted at startup.
 *  This one draws from its own block in D2 RAM instead, which leaves the .bss
 *  layout untouched, and refuses rather than overruns. */
static uint8_t __attribute__((section(".d2_bss"), aligned(8))) heap_mem[32 * 1024];

/* Also in D2, so that not even this shifts anything in .bss. D2 is not
 *  zeroed at start-up: main() clears it before anything can allocate
 *  (nothing mallocs before main). */
static size_t __attribute__((section(".d2_bss"))) heap_used;

extern "C" void *_sbrk(ptrdiff_t incr)
{
    size_t &used = heap_used;
    if (incr < 0 ? (size_t)-incr > used : (size_t)incr > sizeof(heap_mem) - used)
    {
        errno = ENOMEM;
        return (void *)-1;
    }
    void *prev = heap_mem + used;
    used += incr;
    return prev;
}

daisysp::Oscillator osc;

// CpuLoadMeter meter;
uint32_t sd_checkt;
// bool log_batt;
bool booting = true;
bool rainbow_done = false;


/** breakdown:
 *  Inputs:
 *  Channel 1 - Microphone
 *  Channel 2 - X
 *  Channel 3 - Aux L
 *  Channel 3 - Aux R
 *
 *  Outputs:
 *  Channel 1 - Headphone L
 *  Channel 2 - Headphone R
 *  Channel 3 - Master L
 *  Channel 4 - Master R
 */
bool line_in_state;
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    engine.cpu_meter.OnBlockStart();

    if(booting && !ui.InTestMode())
    {
        hw.ProcessAllControls();
        ui.GenerateEvents();
        ui.DoEvents();

        for(size_t i = 0; i < size; i++)
        {
            out[0][i] = out[1][i] = out[2][i] = out[3][i] = 0.f;
        }

        engine.cpu_meter.OnBlockEnd();
        return;
    }

    hw.ProcessAllControls();

    /* SING: duck the built-in mic on the first change of any key contact,
       before the debounce (~8 ms) lets the press or release through */
    {
        static bool last_raw[int(Hardware::SwId::SR_LAST)];
        bool changed = false;
        for(int k = 0; k < int(Hardware::SwId::SR_LAST); k++)
        {
            using S = Hardware::SwId; // not the toggle, not unconnected inputs
            if(k == int(S::SW_TOG) || k == int(S::NC_1) || k == int(S::NC_2)
               || k == int(S::NC_3) || k == int(S::NC_4) || k == int(S::NC_5)
               || k == int(S::NC_6))
                continue;
            const bool raw = hw.button_sr.RawState(k);
            changed |= raw != last_raw[k];
            last_raw[k] = raw;
        }
        if(changed)
            engine.KeyContact();
    }

    ui.GenerateEvents();
    engine.Prepare();

    // does this have to happen in the audio callback?
    if(hw.jack_detect.Read() != line_in_state)
    {
        if(hw.jack_detect.Read())
            engine.SetInputSource(InputSource::LINE_IN);
        else
            engine.SetInputSource(InputSource::MIC);
    }
    line_in_state = hw.jack_detect.Read();

    if(ui.InTestMode() && ui.GetToggleState())
    {
        for(size_t i = 0; i < size; i++)
        {
            out[0][i] = out[1][i] = out[2][i] = out[3][i] = osc.Process();
        }
    }
    else
        engine.Process(in, out, size);

    engine.cpu_meter.OnBlockEnd();
}

void ZeroSDRAM()
{
    uint32_t *beg, *end;
    size_t    size_in_words = (1024 * 1024 * 64) / sizeof(uint32_t);
    beg                     = (uint32_t*)0xc0000000;
    end                     = (uint32_t*)(beg + size_in_words);
    std::fill(beg, end, 0);
}

bool no_sd_card = false;
void CheckSDCardMounted()
{
    DSTATUS res = disk_status(0);

    // lost the SD card, must reboot
    if(res != RES_OK)
    {
        no_sd_card = true;
        ui.NoSDCard();
        

        const uint32_t start_time = System::GetNow();
        while(System::GetNow() - start_time < 3000)
        {
            ui.NoSDCardAnimation(true);
            ui.DoEvents();
            System::Delay(1);
        }

        ui.NoSDCardAnimation(false);
    }
}

void SDCallback(void* data)
{
    const uint32_t now = System::GetNow();
    if (now - sd_checkt > 1000 && !no_sd_card && !booting)
    {
        sd_checkt = now;
        CheckSDCardMounted();
    }
    else if(no_sd_card)
        return;

}

uint32_t uit, now;

#if !NO_BATT
uint32_t batt, usbt;
bool prev_int_state = true;
bool usb_handoff = false;
#endif



void MainLoop(void* data)
{
    if(booting)
    {
        hw.LowBatteryLockoutCheck();

        /* TAPE converted old sample files here before letting the audio
           start; SING has no samples, so booting ends straight away */
        booting = false;
        return;
    }
    else if(!rainbow_done)
    {
        ui.StopBootAnimation();
        ui.RainbowWave();
        rainbow_done = true;
    }

    // SING: pitch estimate, outside the audio callback
    engine.UpdatePitch();

    // volatile float avg_load = meter.GetAvgCpuLoad();
    // volatile float max_load = meter.GetMaxCpuLoad();
    now = daisy::System::GetNow();

    if (now - uit > 1)
    {
        ui.DoEvents();
        ui.ProcessMidi();
        uit = now;
    }


    // update now to actually be now
    now = daisy::System::GetNow();

    #if !NO_BATT
    // falling edge battery interrupt
    bool int_state = hw.mpc_int.Read();
    bool interrupt = !int_state && prev_int_state;
    prev_int_state = int_state;

    if(ui.InRainbows())
    {
        batt = now;
    }
    else if(now - batt > 20)
    {
        hw.LowBatteryLockoutCheck();
        batt = now;
    }

    if(ui.InTestMode())
    {
        hw.MpReadAll();

        while (!hw.read_ready) {
            System::Delay(1);
        }
        ui.TestPowerCable(hw.mp_buff_[1] >> 5 & 1); //VIN_RDY

        // Normal NTC_MISSING, BATT_MISSING, NTC1_FAULT, and NTC2_FAULT
        ui.TestBMC(hw.mp_buff_[3] == 0); 
    }


    if(interrupt)
    {
        // last_read = now;
        hw.MpReadAll();

        while (!hw.read_ready) {
            System::Delay(1);
        }
        // Unknown or (not started and interrupt)
        uint8_t masked = hw.mp_buff_[0] & 0B11110000; // DPDM_STAT
        if( masked == 0 )
        {
            hw.USBMidiActive(false);
            hw.usb_sw.Write(false); // give USB control
            usb_handoff = true;
            usbt = now;
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
    #endif

    System::DelayUs(10);
}

int main(void)
{
    /* The heap lives in D2 RAM, whose clocks are off after reset until the
       first D2 peripheral is set up (libDaisy only enables them with
       DATA_IN_D2_SRAM). Touching it before hw.Init() faulted -- TAPE hung
       before drawing anything. Enable them first, as the CMSIS template does
       for data in D2. */
    RCC->AHB2ENR |= RCC_AHB2ENR_D2SRAM1EN | RCC_AHB2ENR_D2SRAM2EN
                    | RCC_AHB2ENR_D2SRAM3EN;
    (void)RCC->AHB2ENR; /* read back: the enable has taken effect */
    heap_used = 0;      /* see _sbrk */

    hw.Init();
    // System::Delay(100);

    hw.MpWrite(0x0c, 0B01010001); // set BATT_LOW to 3V, turn on 

    hw.MpReadAll();

    for(size_t i = 0; i < 10; i++)
    {
        hw.LowBatteryLockoutCheck();
        System::Delay(10);
    }

    /** SDMMC Init */
    SdmmcHandler::Config sd_cfg;
    sd_cfg.speed = SdmmcHandler::Speed::FAST;
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_4;
    // sd_cfg.clock_powersave = true;
    sdmmc.Init(sd_cfg);
    fsi.Init(FatFSInterface::Config::MEDIA_SD);
    f_mount(&fsi.GetSDFileSystem(), fsi.GetSDPath(), 1);

    /* Keep SING's files (settings, looper recordings) in /SING, out of the
     *  card root, like the launcher's other firmwares. Every path it opens is
     *  relative. Without the folder it stays in the root. */
    f_chdir("/SING");

    // delete the battery log if it exists
    char filename[32];
    sprintf(filename, ".batt_log.txt");
    f_unlink(filename);

    // macos makes a copy
    sprintf(filename, "._.batt_log.txt");
    f_unlink(filename);


    options.Init();

    LedSetup();
    ui.Init(&hw, &engine,
        options.midi_ch_in, options.midi_ch_out, options.pitch_shift_quantization, options.delay_split);

    hw.StartLowPriorityCallback(SDCallback, 1000);
    engine.cpu_meter.Init(hw.seed.AudioSampleRate(), hw.seed.AudioBlockSize());
    hw.StartAudio(AudioCallback);

    ZeroSDRAM();


    loop_buff.Init(&loop_mem[0]);
    engine.Init(hw.seed.AudioSampleRate(), &reverb, &del_mem[0], 
                &loop_buff, options.tape_slew_on,
                MonitorMode(options.monitor_position));

    osc.Init(hw.seed.AudioSampleRate());
    osc.SetAmp(.2f);

    now = daisy::System::GetNow();
    uit = now;

    #if !NO_BATT
    usbt = now;
    batt = now;
    #endif

    // get any junk out of the SRs, takes .5s
    uint32_t vol_state = 0;
    uint32_t sleep_state = 0;

    for(int i = 0; i < 5000; i++)
    {
        hw.ProcessAllControls();
        vol_state += hw.button_sr.State(int(Hardware::SwId::ENC_6_SW));
        sleep_state += hw.button_sr.State(int(Hardware::SwId::KEY_26))
                        && hw.button_sr.State(int(Hardware::SwId::KEY_27))
                        && hw.button_sr.State(int(Hardware::SwId::KEY_28));

        System::DelayUs(100);
    }

    if(sleep_state > 4000)
        hw.MpWrite(0x08, 0B10111111); // SHIPPING MODE
    else if(vol_state > 4000)
        ui.TestMode();

    hw.usb_sw.Write(false);     // give USB control
    daisy::System::Delay(1); // Wait a sec
    hw.MpWrite(0x0a, 0B00100100); // Auto DPDM
    daisy::System::Delay(1); // Wait a sec
    hw.usb_sw.Write(true);     // take USB control
    

    while (1)
    {
        MainLoop(nullptr);
    }
}