/** @file launcher_main.cpp
 *  @brief CHOMPI firmware launcher.
 *
 *  Power on, and the launcher lights one key for every firmware it finds on the
 *  SD card. Press a key and that firmware starts. Power cycle to come back here.
 *
 *  Installing it is an ordinary firmware update: the launcher's own CHOMPI.bin
 *  goes in the root of the card, and the firmwares it offers go in /FIRMWARE.
 *  The stock bootloader skips directories when it hunts for a .bin to install,
 *  so it only ever sees the launcher, and QSPI keeps holding the launcher no
 *  matter which firmware was last run.
 *
 *  A firmware can also arrive over USB MIDI (see midi_upload.h and
 *  PROTOCOL.md). It is written into the slot the host names, then read back
 *  off the card and started exactly as if its key had been pressed -- send it
 *  while the picker is up, or while any fault is showing.
 */
#include "daisy_seed.h"
#include "hardware.h"
#include "chainload.h"
#include "midi_upload.h"
#include "fatfs.h"
#include <cstring>
#include <cstdio>
#include <strings.h>
#include <cstdarg>

using namespace daisy;
using namespace chompi;

/** Where the firmwares live. Invisible to the bootloader because it is a
 *  directory, which is exactly why this works. */
static const char *kFirmwareDir = "FIRMWARE";

/** One per white key, which is as many as the picker can show. */
static constexpr int kMaxSlots = 15;

/** White keys, left to right. These are the slot keys. */
static const Hardware::SwId kSlotKey[kMaxSlots] = {
    Hardware::SwId::KEY_1,  Hardware::SwId::KEY_2,  Hardware::SwId::KEY_3,
    Hardware::SwId::KEY_4,  Hardware::SwId::KEY_5,  Hardware::SwId::KEY_6,
    Hardware::SwId::KEY_7,  Hardware::SwId::KEY_8,  Hardware::SwId::KEY_9,
    Hardware::SwId::KEY_10, Hardware::SwId::KEY_11, Hardware::SwId::KEY_12,
    Hardware::SwId::KEY_13, Hardware::SwId::KEY_14, Hardware::SwId::KEY_15,
};

/** The SMT LED sitting under each of those keys (from TAPE/WAVE's led_map). */
static const int kSlotLed[kMaxSlots] = {
    24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10,
};

Hardware      hw;
SdmmcHandler  sdmmc;
FatFSInterface fsi;

/** Staging buffer for the chosen image. SDRAM is 64MB and an app is ~250KB, so
 *  there is no reason to be clever: read the whole file in, check it, then jump. */
static uint8_t DSY_SDRAM_BSS fw_image[0x80000];

struct Slot
{
    char name[64];
    uint32_t size;
};

static Slot slots[kMaxSlots];
static int  slot_count = 0;

MidiUpload upload;

/** Whether the card is mounted. An upload remounts it if not, so a card
 *  inserted after power-on still works. */
static bool card_ok = false;

/* ------------------------------------------------------------------------
 *  Logging
 *
 *  There is no debugger on this unit, so the card is the only way to see what
 *  happened. The log is built in RAM and written out in one shot.
 *
 *  The first attempt at this appended a line at a time and produced a 0-byte
 *  file -- the per-line reopen was failing silently and taking the evidence
 *  with it. Buffering means exactly one f_open/f_write/f_close per flush, and
 *  the log cannot be lost to the same failure it is supposed to be reporting. */

/* Every FIL below is static, deliberately.
 *
 *  A FIL carries its own 512-byte sector buffer, and FatFS hands that buffer
 *  straight to the SD driver's DMA. The stack lives in DTCMRAM (MSP starts at
 *  0x20020000), and DTCM is not reachable by DMA on the STM32H7 -- so a FIL
 *  declared as a local silently breaks every read and write through it, while
 *  directory operations keep working because those use the FATFS object's own
 *  window buffer, which is a global.
 *
 *  This is why every FIL in the stock firmware is a class member rather than a
 *  local (OptionsManager, FileCopier, SampleReader, ui.h -- all of them).
 *  Static puts ours in .bss alongside them.
 *
 *  Not reentrant, which is fine here: one thing happens at a time. */

static const char *kLogFile = "FIRMWARE/launcher_log.txt";
static char __attribute__((aligned(32))) log_buf[8192];
static size_t      log_len = 0;

static void Log(const char *fmt, ...)
{
    if (log_len + 256 >= sizeof(log_buf))
        return; /* full; keep the earliest lines, they are the interesting ones */

    int n = snprintf(log_buf + log_len, sizeof(log_buf) - log_len, "[%7lu] ",
                     (unsigned long)System::GetNow());
    if (n < 0)
        return;
    log_len += n;

    va_list args;
    va_start(args, fmt);
    n = vsnprintf(log_buf + log_len, sizeof(log_buf) - log_len, fmt, args);
    va_end(args);
    if (n < 0)
        return;
    log_len += n;

    log_len += snprintf(log_buf + log_len, sizeof(log_buf) - log_len, "\r\n");
}

/** Where this boot's log starts in the file. Earlier boots are kept ahead of
 *  it, so a run that ends in a hang -- which never gets to write again --
 *  can still be read after the next one. Starts over once the file is big.
 *
 *  Always on a sector boundary, with the gap padded out. Writing from the
 *  middle of a sector lost bytes at every following sector boundary. */
static FSIZE_t log_base = 0;

static void LogStartRun()
{
    static FILINFO info;
    static FIL     f;

    if (f_stat(kLogFile, &info) != FR_OK || info.fsize >= 64 * 1024)
    {
        log_base = 0;
        return;
    }

    log_base = (info.fsize + 511) & ~(FSIZE_t)511;
    if (log_base == info.fsize)
        return;

    static char pad[512];
    memset(pad, '\n', sizeof(pad));
    if (f_open(&f, kLogFile, FA_OPEN_ALWAYS | FA_WRITE) != FR_OK)
        return;
    UINT put = 0;
    if (f_lseek(&f, info.fsize) == FR_OK)
        f_write(&f, pad, (UINT)(log_base - info.fsize), &put);
    f_close(&f);
}

/** DIAGNOSTIC: a debug firmware leaves a record of how far it got, and of any
 *  crash, in backup SRAM, which survives the reset that brings us back here. */
struct CrashRec
{
    uint32_t magic, stage, pc, lr, psr, cfsr, hfsr, mmfar, bfar;
};

static void LogCrashRecord()
{
    System::InitBackupSram();
    volatile CrashRec *rec = (volatile CrashRec *)0x38800100;

    Log("reset cause RCC_RSR=0x%08lX", (unsigned long)RCC->RSR);
    if ((rec->magic & 0xFFFF0000) == 0xB0070000)
        Log("RECORD: firmware did not finish startup, last stage %lu",
            (unsigned long)rec->stage);
    else if ((rec->magic & 0xFFFF0000) == 0x600D0000)
        Log("RECORD: firmware finished startup, stage %lu",
            (unsigned long)rec->stage);
    else if ((rec->magic & 0xFFFF0000) == 0xFA170000)
        Log("RECORD: FAULT at stage %lu: pc=0x%08lX lr=0x%08lX psr=0x%08lX "
            "cfsr=0x%08lX hfsr=0x%08lX mmfar=0x%08lX bfar=0x%08lX",
            (unsigned long)rec->stage, (unsigned long)rec->pc,
            (unsigned long)rec->lr, (unsigned long)rec->psr,
            (unsigned long)rec->cfsr, (unsigned long)rec->hfsr,
            (unsigned long)rec->mmfar, (unsigned long)rec->bfar);
    else
        Log("RECORD: none (magic 0x%08lX)", (unsigned long)rec->magic);
    rec->magic = 0;
}

/** Write the whole log out.
 *
 *  Deliberately NOT FA_CREATE_ALWAYS. That truncates on open, so a flush whose
 *  write then fails leaves an empty file -- which is exactly how the first
 *  version of this destroyed the evidence it was collecting. Open without
 *  truncating, overwrite from the start, and only cut the file back once the
 *  write has actually succeeded. A failed flush then leaves the previous
 *  contents intact. */
static void LogFlush()
{
    static FIL f;
    if (f_open(&f, kLogFile, FA_OPEN_ALWAYS | FA_WRITE) != FR_OK)
        return;
    if (f_lseek(&f, log_base) != FR_OK)
    {
        f_close(&f);
        return;
    }

    UINT put = 0;
    if (f_write(&f, log_buf, log_len, &put) == FR_OK && put == log_len)
        f_truncate(&f); /* drop any tail left from a longer previous flush */

    f_close(&f); /* closing is what actually commits it */
}

/** Case-insensitive test for a trailing ".bin". */
static bool HasBinExtension(const char *name)
{
    const size_t len = strlen(name);
    if (len < 5) /* at minimum "x.bin" */
        return false;
    const char *ext = name + len - 4;
    return (ext[0] == '.') && (ext[1] == 'b' || ext[1] == 'B') &&
           (ext[2] == 'i' || ext[2] == 'I') && (ext[3] == 'n' || ext[3] == 'N');
}

/** Collect up to kMaxSlots .bin files from /FIRMWARE, sorted by name so the key
 *  order stays put as firmwares are added and removed. Prefix them 01_, 02_ and
 *  so on to pin them to particular keys. */
static void ScanFirmwares()
{
    DIR     dir;
    FILINFO info;

    slot_count = 0;

    FRESULT res = f_opendir(&dir, kFirmwareDir);
    Log("f_opendir(\"%s\") -> %d", kFirmwareDir, (int)res);
    if (res != FR_OK)
        return;

    int seen = 0;
    while (slot_count < kMaxSlots)
    {
        res = f_readdir(&dir, &info);
        if (res != FR_OK)
        {
            Log("f_readdir -> %d (stopping)", (int)res);
            break;
        }
        if (info.fname[0] == 0)
            break; /* end of directory */

        seen++;
        Log("  entry: \"%s\" attrib=0x%02X size=%lu", info.fname,
            (unsigned)info.fattrib, (unsigned long)info.fsize);

        if (info.fattrib & (AM_HID | AM_DIR))
        {
            Log("    skip: hidden or directory");
            continue;
        }
        if (!HasBinExtension(info.fname))
        {
            Log("    skip: not .bin");
            continue;
        }
        /* Anything that cannot be a valid image is not worth a key. */
        if (info.fsize < 8 || info.fsize > sizeof(fw_image))
        {
            Log("    skip: size out of range (max %lu)",
                (unsigned long)sizeof(fw_image));
            continue;
        }

        strncpy(slots[slot_count].name, info.fname, sizeof(slots[0].name) - 1);
        slots[slot_count].name[sizeof(slots[0].name) - 1] = '\0';
        slots[slot_count].size = (uint32_t)info.fsize;
        slot_count++;
        Log("    ACCEPTED as slot %d", slot_count);
    }
    f_closedir(&dir);
    Log("directory walk saw %d entries, accepted %d", seen, slot_count);

    /* Insertion sort: tiny list, and it keeps the ordering obvious. */
    for (int i = 1; i < slot_count; i++)
    {
        Slot key = slots[i];
        int  j   = i - 1;
        while (j >= 0 && strcasecmp(slots[j].name, key.name) > 0)
        {
            slots[j + 1] = slots[j];
            j--;
        }
        slots[j + 1] = key;
    }
}

/* Settings are each firmware's own business.
 *
 *  An earlier version of this launcher shuffled options.json and presets.json
 *  in and out of the card root, because all three stock firmwares kept them
 *  there in mutually incompatible formats. They now chdir into their own
 *  directory at startup instead, so their settings never collide and there is
 *  nothing here to manage.
 */


/** Bounce buffer for the firmware read.
 *
 *  Two separate reasons this cannot read straight into fw_image:
 *
 *  1. SDMMC's DMA will not land data in SDRAM at all. The stock firmware
 *     bounces through internal RAM for the same reason -- see
 *     FileStreamingManager's workspace_buffer and the resigned comment above
 *     it ("the copy-to/from-the-workspace is a disappointing extra step").
 *
 *  2. When a read is a whole number of sectors, FatFS hands the user buffer
 *     straight to disk_read as a multi-sector burst (ff.c: `disk_read(fs->drv,
 *     rbuff, sect, cc)`). A 32-sector burst never completes here and eats the
 *     full 30-second SD_TIMEOUT. One sector at a time is the same shape of
 *     transfer the directory reads use, and those work.
 *
 *  Aligned because it is a DMA target, and because sd_diskio.c does its cache
 *  maintenance on 32-byte boundaries around whatever address it is given.
 *
 *  4K rather than one sector: every transfer carries a fixed overhead (two
 *  cache maintenance calls and a card-state poll), and at 512 bytes a 240K
 *  image needs 470 of them. Safe now that the FIL buffers are out of DTCM. */
static constexpr uint32_t kChunkSize = 4096;
static uint8_t __attribute__((aligned(32))) bounce_buf[kChunkSize];

/** Read a firmware into the staging buffer in SDRAM, a sector at a time. */
static bool LoadImage(const Slot &slot, uint32_t *out_len)
{
    char path[80];
    snprintf(path, sizeof(path), "%s/%s", kFirmwareDir, slot.name);

    static FIL file;
    FRESULT    res = f_open(&file, path, FA_OPEN_EXISTING | FA_READ);
    Log("f_open(\"%s\") -> %d", path, (int)res);
    if (res != FR_OK)
        return false;

    /* Commit before the read starts. If the read hangs rather than failing,
       this is the last thing that will ever reach the card, and knowing the
       open succeeded narrows it to the transfer itself. */
    Log("starting read of %lu bytes in %lu-byte chunks",
        (unsigned long)slot.size, (unsigned long)kChunkSize);
    LogFlush();

    const uint32_t started = System::GetNow();
    uint32_t offset = 0;
    uint32_t chunk  = 0;
    bool     ok     = true;

    while (offset < slot.size)
    {
        uint32_t want = slot.size - offset;
        if (want > sizeof(bounce_buf))
            want = sizeof(bounce_buf);

        UINT got = 0;
        res = f_read(&file, bounce_buf, want, &got);

        /* Noisy for the first few so a working read is visible in the log too,
           then only on trouble -- a 470-chunk read would bury everything. */
        if (chunk < 3)
            Log("  chunk %lu: f_read(%lu) -> %d, got %lu",
                (unsigned long)chunk, (unsigned long)want, (int)res,
                (unsigned long)got);

        if (res != FR_OK || got != want)
        {
            Log("  FAIL at chunk %lu offset %lu: res=%d want=%lu got=%lu",
                (unsigned long)chunk, (unsigned long)offset, (int)res,
                (unsigned long)want, (unsigned long)got);
            ok = false;
            break;
        }

        memcpy(fw_image + offset, bounce_buf, got);
        offset += got;
        chunk++;

        /* Fill the keybed left to right as the image comes in. Without this a
           slow read and a dead unit look exactly the same from the outside. */
        const int lit = (int)((offset * kMaxSlots) / slot.size);
        for (int k = 0; k < kMaxSlots; k++)
            SetSmtLedFloat(kSlotLed[k], k < lit ? .9f : 0.f,
                           k < lit ? .9f : 0.f, k < lit ? .9f : 0.f);
        fill_led_data();
    }

    f_close(&file);
    Log("read loop: %lu/%lu bytes in %lu chunks, %lu ms",
        (unsigned long)offset, (unsigned long)slot.size, (unsigned long)chunk,
        (unsigned long)(System::GetNow() - started));

    if (!ok || offset != slot.size)
        return false;

    *out_len = offset;
    return true;
}

/** Keybed filling left to right in amber while an upload goes onto the card,
 *  then in white as LoadImage() reads it back to start it. */
static void DrawWriteProgress(uint32_t done, uint32_t total)
{
    const int lit = (int)((done * kMaxSlots) / total);
    for (int k = 0; k < kMaxSlots; k++)
        SetSmtLedFloat(kSlotLed[k], k < lit ? .9f : 0.f, k < lit ? .45f : 0.f,
                       0.f);
    fill_led_data();
}

/** Write the uploaded image into its slot: /FIRMWARE/NN_NAME.bin.
 *
 *  Goes to a temporary file first, which is read back and compared before any
 *  existing NN_* file is removed, so a failed write never costs the firmware
 *  already in the slot. On success *out describes the new file, ready for
 *  LoadImage(). */
static MidiUpload::Status StoreUpload(Slot *out)
{
    static const char *kTemp = "FIRMWARE/upload.tmp";
    static FIL         file;

    const uint32_t len = upload.Total();

    if (!card_ok)
    {
        const FRESULT res = f_mount(&fsi.GetSDFileSystem(), fsi.GetSDPath(), 1);
        Log("store: card was not mounted, f_mount -> %d", (int)res);
        if (res != FR_OK)
            return MidiUpload::NO_CARD;
        card_ok = true;
    }

    FRESULT res = f_mkdir(kFirmwareDir);
    if (res != FR_OK && res != FR_EXIST)
    {
        Log("store: f_mkdir -> %d", (int)res);
        return MidiUpload::WRITE_FAILED;
    }

    /* Write. Through the bounce buffer, as SDMMC's DMA cannot reach SDRAM. */
    const uint32_t started = System::GetNow();
    res = f_open(&file, kTemp, FA_CREATE_ALWAYS | FA_WRITE);
    Log("store: f_open(\"%s\") -> %d", kTemp, (int)res);
    if (res != FR_OK)
        return MidiUpload::WRITE_FAILED;

    bool ok = true;
    for (uint32_t off = 0; ok && off < len; off += kChunkSize)
    {
        const UINT want = len - off < kChunkSize ? len - off : kChunkSize;
        UINT       put  = 0;
        memcpy(bounce_buf, fw_image + off, want);
        res = f_write(&file, bounce_buf, want, &put);
        if (res != FR_OK || put != want)
        {
            Log("store: f_write at %lu -> %d, put %lu of %lu",
                (unsigned long)off, (int)res, (unsigned long)put,
                (unsigned long)want);
            ok = false;
        }
        DrawWriteProgress(off + want, len);
    }
    res = f_close(&file);
    if (res != FR_OK)
        Log("store: f_close -> %d", (int)res);
    if (!ok || res != FR_OK)
        return MidiUpload::WRITE_FAILED;

    /* Read back and compare: a card that silently drops writes would
       otherwise leave a slot that fails only the next time it is chosen. */
    res = f_open(&file, kTemp, FA_OPEN_EXISTING | FA_READ);
    for (uint32_t off = 0; ok && res == FR_OK && off < len; off += kChunkSize)
    {
        const UINT want = len - off < kChunkSize ? len - off : kChunkSize;
        UINT       got  = 0;
        res = f_read(&file, bounce_buf, want, &got);
        if (res != FR_OK || got != want || memcmp(bounce_buf, fw_image + off, want))
        {
            Log("store: readback differs at %lu (res %d, got %lu)",
                (unsigned long)off, (int)res, (unsigned long)got);
            ok = false;
        }
    }
    f_close(&file);
    if (!ok || res != FR_OK)
        return MidiUpload::WRITE_FAILED;
    Log("store: %lu bytes written and verified in %lu ms", (unsigned long)len,
        (unsigned long)(System::GetNow() - started));

    /* Clear the slot: every NN_* file, whatever its name. Collected first,
       since deleting while the directory is being read is asking for trouble. */
    char prefix[8];
    snprintf(prefix, sizeof(prefix), "%02u_", (unsigned)upload.Slot());

    static char doomed[8][_MAX_LFN + 1];
    int         n_doomed = 0;
    DIR         dir;
    FILINFO     info;
    if (f_opendir(&dir, kFirmwareDir) == FR_OK)
    {
        while (n_doomed < 8 && f_readdir(&dir, &info) == FR_OK && info.fname[0])
        {
            if (!(info.fattrib & AM_DIR) && strncmp(info.fname, prefix, 3) == 0)
            {
                strncpy(doomed[n_doomed], info.fname, sizeof(doomed[0]) - 1);
                doomed[n_doomed][sizeof(doomed[0]) - 1] = '\0';
                n_doomed++;
            }
        }
        f_closedir(&dir);
    }
    for (int i = 0; i < n_doomed; i++)
    {
        static char path[sizeof(doomed[0]) + 16];
        snprintf(path, sizeof(path), "%s/%s", kFirmwareDir, doomed[i]);
        res = f_unlink(path);
        Log("store: replacing %s, f_unlink -> %d", path, (int)res);
        if (res != FR_OK)
            return MidiUpload::WRITE_FAILED;
    }

    snprintf(out->name, sizeof(out->name), "%s%s.bin", prefix, upload.Name());
    out->size = len;

    char path[80];
    snprintf(path, sizeof(path), "%s/%s", kFirmwareDir, out->name);
    res = f_rename(kTemp, path);
    Log("store: f_rename -> \"%s\" -> %d", path, (int)res);
    if (res != FR_OK)
        return MidiUpload::WRITE_FAILED;

    return MidiUpload::OK;
}

/** Breathing white on every populated key. Unpopulated keys stay dark, so the
 *  number of lit keys is the number of firmwares you have. */
static void DrawPicker(uint32_t now)
{
    /* ~2 s cycle, never fully dark so a slot is always readable. */
    const float phase = (float)(now % 2000) / 2000.f;
    const float tri   = phase < .5f ? phase * 2.f : (1.f - phase) * 2.f;
    const float level = .15f + tri * .65f;

    for (int i = 0; i < kNumSmtLeds; i++)
        SetSmtLed(i, 0, 0, 0);
    for (int i = 0; i < kNumPthLeds; i++)
        SetPthLed(i, 0, 0, 0);

    for (int i = 0; i < slot_count; i++)
        SetSmtLedFloat(kSlotLed[i], level, level, level);

    fill_led_data();
}

/** Confirmation flash on the chosen key, so a press always feels acknowledged
 *  even though loading is quick. */
static void FlashChoice(int index)
{
    for (int pulse = 0; pulse < 3; pulse++)
    {
        for (int i = 0; i < kNumSmtLeds; i++)
            SetSmtLed(i, 0, 0, 0);
        SetSmtLedFloat(kSlotLed[index], 1.f, 1.f, 1.f);
        fill_led_data();
        System::Delay(60);

        SetSmtLed(kSlotLed[index], 0, 0, 0);
        fill_led_data();
        System::Delay(60);
    }

    /* Leave it lit through the load. Dark would be indistinguishable from a
       dead unit, and a slow read is exactly when you most want to know the
       difference. */
    SetSmtLedFloat(kSlotLed[index], 1.f, 1.f, 1.f);
    fill_led_data();
}

/** Why the launcher gave up. Each gets its own colour so the fault is readable
 *  off the panel without pulling the card. */
enum class Fault
{
    NoCard,       /**< red     -- no card, or it would not mount */
    NoFirmwares,  /**< amber   -- card mounted, /FIRMWARE is empty */
    ReadFailed,   /**< magenta -- the image would not read off the card */
    BadImage,     /**< blue    -- image read, but its vector table is wrong */
    Internal,     /**< white   -- the trampoline does not fit; cannot happen */
};

static void ServiceUsb(uint32_t now);
static void DrawUploadProgress();

/** Slow pulse, and the unit stays here rather than jumping into nothing.
 *  Commits the log on the way in -- this is where the evidence matters most.
 *
 *  Still listens on USB: a firmware sent over MIDI needs neither a card nor
 *  anything on it, so this is also the way out of a bad card. */
[[noreturn]] static void ErrorLoop(Fault fault)
{
    Log("STOP: fault %d", (int)fault);
    LogFlush();

    float r = .8f, g = 0.f, b = 0.f;
    switch (fault)
    {
        case Fault::NoCard:      r = .8f; g = 0.f;  b = 0.f;  break;
        case Fault::NoFirmwares: r = .8f; g = .35f; b = 0.f;  break;
        case Fault::ReadFailed:  r = .8f; g = 0.f;  b = .8f;  break;
        case Fault::BadImage:    r = 0.f; g = .2f;  b = .9f;  break;
        case Fault::Internal:    r = .8f; g = .8f;  b = .8f;  break;
    }

    uint32_t tick = 0;
    while (true)
    {
        const uint32_t now = System::GetNow();
        ServiceUsb(now);

        if (now - tick < 16)
            continue;
        tick = now;

        if (upload.Active(now))
        {
            DrawUploadProgress();
        }
        else
        {
            const float phase = (float)(now % 1200) / 1200.f;
            const float tri   = phase < .5f ? phase * 2.f : (1.f - phase) * 2.f;

            for (int i = 0; i < kNumSmtLeds; i++)
                SetSmtLed(i, 0, 0, 0);
            for (int i = 0; i < kNumPthLeds; i++)
                SetPthLedFloat(i, tri * r, tri * g, tri * b);

            fill_led_data();
        }
        hw.LowBatteryLockoutCheck();
    }
}

/** Stop everything that writes to memory on its own before the handover. The
 *  LED driver runs off a DMA chain that would otherwise still be firing while
 *  the trampoline overwrites SRAM. */
static void ShutdownPeripherals()
{
    hw.LedsOff();
    StopLeds();
    System::Delay(20); /* let the last DMA transfer retire */

    /* Leave USB properly: the host sees the device go, and the data lines go
       back to the charger, as they are at power-on. Otherwise the reset cuts
       a live connection off mid-stream and the incoming firmware starts up
       underneath it. */
    UsbHandle usb;
    usb.DeInit(UsbHandle::FS_EXTERNAL);
    hw.usb_sw.Write(false);
    System::Delay(100);

    /* libDaisy's SDMMC driver has no teardown call, but unmounting ends the
       last transfer, and the peripheral does nothing further unless asked. */
    f_mount(nullptr, "", 0);
    System::Delay(5);

    /* The same teardown the stock bootloader performs before it starts an
       application (ChompiDeInitCallback in the v6.4 bootloader's main.cpp).
       Without it the incoming firmware inherits live peripherals and DMA
       streams underneath its own initialisation.
     *
     *  seed.DeInit() runs dsy_dma_deinit(), disables both caches and deinits
     *  the timer behind System::GetNow() -- so nothing below may call
     *  System::Delay(), and this has to be the last thing we do. It leaves
     *  SDRAM alone (that teardown is commented out in libDaisy), so the
     *  staged image survives for the trampoline to copy. */
    hw.seed.StopAudio();
    hw.seed.DeInit();
}

/** Check the staged image and start it. Shared by both ways in: a key press,
 *  and an upload once it has been stored -- both read it off the card first. */
[[noreturn]] static void Boot(uint32_t length)
{
    const uint32_t msp   = *(const uint32_t *)(fw_image + 0);
    const uint32_t entry = *(const uint32_t *)(fw_image + 4);
    Log("vector table: MSP=0x%08lX entry=0x%08lX", (unsigned long)msp,
        (unsigned long)entry);

    if (!ImageLooksValid(fw_image, length))
    {
        Log("FAIL: vector table rejected");
        ErrorLoop(Fault::BadImage);
    }

    /* Last thing written: if the log ends here, the handover itself is where
       it went wrong. */
    Log("handing over to 0x%08lX -- goodbye", (unsigned long)entry);
    LogFlush(); /* last chance; the next instruction is the jump */

    ShutdownPeripherals();
    ChainLoad(fw_image, length); /* does not return */
}

/** Keybed filling left to right in cyan as an upload arrives -- the same
 *  gauge as a card read, in a colour that says where it is coming from. */
static void DrawUploadProgress()
{
    const uint32_t total = upload.Total();
    const int lit = total ? (int)((upload.Received() * kMaxSlots) / total) : 0;

    for (int i = 0; i < kNumSmtLeds; i++)
        SetSmtLed(i, 0, 0, 0);
    for (int i = 0; i < kNumPthLeds; i++)
        SetPthLed(i, 0, 0, 0);
    for (int k = 0; k < kMaxSlots; k++)
        SetSmtLedFloat(kSlotLed[k], 0.f, k < lit ? .8f : .05f,
                       k < lit ? .8f : .05f);

    fill_led_data();
}

/* ------------------------------------------------------------------------
 *  USB
 *
 *  CHOMPI's USB data lines run through a switch shared with the MP2722
 *  charger, which needs them for a moment to work out what kind of port it is
 *  plugged into. Until the Daisy takes them back, the host never sees a MIDI
 *  device. This is the same handshake TAPE does in its main loop. */

static bool     prev_int_state = true;
static bool     usb_handoff    = false;
static uint32_t usb_handoff_t  = 0;

/** At power-on: let the charger detect the port, then take the lines. */
static void UsbTakeOver()
{
    hw.usb_sw.Write(false);       /* give USB to the charger */
    System::Delay(1);
    hw.MpWrite(0x0a, 0B00100100); /* auto DPDM */
    System::Delay(1);
    hw.usb_sw.Write(true);        /* take it back */
}

/** Charger raised its interrupt: a cable came or went. If it does not yet
 *  know the port type, lend it the lines and force a fresh detection. */
static void ServiceUsbSwitch(uint32_t now)
{
#if !NO_BATT
    const bool int_state = hw.mpc_int.Read();
    const bool interrupt = !int_state && prev_int_state;
    prev_int_state       = int_state;

    if (interrupt)
    {
        hw.MpReadAll();
        /* TAPE waits forever here. A failed I2C read never sets read_ready,
           so give up after a while rather than hang the picker. */
        const uint32_t started = System::GetNow();
        while (!hw.read_ready && System::GetNow() - started < 50)
            System::Delay(1);

        if ((hw.mp_buff_[0] & 0B11110000) == 0) /* DPDM_STAT unknown */
        {
            hw.usb_sw.Write(false);
            usb_handoff   = true;
            usb_handoff_t = now;
        }
        else
        {
            hw.usb_sw.Write(true);
        }
    }

    if (usb_handoff && now - usb_handoff_t > 1)
    {
        usb_handoff = false;
        hw.MpWrite(0x0a, 0B00110100); /* force DPDM */
    }
#endif
}

/** Called as often as the loops can manage: an upload is stop-and-wait, so
 *  every pass that skips this is a chunk not received. Does not return once
 *  an upload completes. */
static void ServiceUsb(uint32_t now)
{
    ServiceUsbSwitch(now);

    switch (upload.Poll())
    {
        case MidiUpload::Event::None:
        case MidiUpload::Event::Progress: break;

        case MidiUpload::Event::Pinged: Log("usb: ping"); break;

        case MidiUpload::Event::Started:
            Log("usb: upload of %lu bytes started",
                (unsigned long)upload.Total());
            break;

        case MidiUpload::Event::Rejected:
            Log("usb: message rejected, status %d at %lu/%lu bytes",
                (int)upload.LastStatus(), (unsigned long)upload.Received(),
                (unsigned long)upload.Total());
            break;

        case MidiUpload::Event::Complete:
        {
            Log("usb: upload complete, %lu bytes, crc 0x%08lX ok, for slot %u "
                "as \"%s\"", (unsigned long)upload.Total(),
                (unsigned long)upload.Crc(), (unsigned)upload.Slot(),
                upload.Name());

            Slot                     stored;
            const MidiUpload::Status st = StoreUpload(&stored);
            upload.FinishEnd(st);
            if (st != MidiUpload::OK)
            {
                Log("usb: store failed, status %d -- upload kept for a retry",
                    (int)st);
                LogFlush();
                break;
            }

            /* Start it the way a key press would: read back off the card into
               fw_image, then the same Boot(). An image that arrived over USB
               and one picked from the menu take the identical path. */
            System::Delay(20); /* let the END reply leave */
            uint32_t length = 0;
            if (!LoadImage(stored, &length))
            {
                Log("FAIL: could not read back %s", stored.name);
                ErrorLoop(Fault::ReadFailed);
            }
            Boot(length);
        }
    }
}

int main(void)
{
    hw.Init();
    LedSetup();

    /* Before anything that can fail: USB is the way back from a bad card. */
    upload.Init(fw_image, sizeof(fw_image), kMaxSlots);
    UsbTakeOver();

    SdmmcHandler::Config sd_cfg;
    sd_cfg.speed = SdmmcHandler::Speed::FAST;
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_4;
    sdmmc.Init(sd_cfg);

    fsi.Init(FatFSInterface::Config::MEDIA_SD);

    const char   *sd_path = fsi.GetSDPath();
    const FRESULT mres    = f_mount(&fsi.GetSDFileSystem(), sd_path, 1);

    card_ok = (mres == FR_OK);
    if (card_ok)
        LogStartRun();
    Log("==== CHOMPI launcher: boot");
    LogCrashRecord();
    Log("f_mount(\"%s\") -> %d", sd_path ? sd_path : "(null)", (int)mres);

    if (mres != FR_OK)
        ErrorLoop(Fault::NoCard); /* no card, or an unreadable one */

    /* A trampoline too large for its landing site would be truncated, and the
       failure would only show at the handover. Refuse to offer anything. */
    if (!CheckTrampolineFits())
    {
        Log("FATAL: trampoline is %lu bytes, max %lu",
            (unsigned long)TrampolineSize(), (unsigned long)kTrampolineMax);
        ErrorLoop(Fault::Internal);
    }
    Log("trampoline %lu bytes, fits", (unsigned long)TrampolineSize());

    ScanFirmwares();
    Log("scan: %d firmware(s) in /%s", slot_count, kFirmwareDir);
    for (int i = 0; i < slot_count; i++)
        Log("  key %d -> %s (%lu bytes)", i + 1, slots[i].name,
            (unsigned long)slots[i].size);

    if (slot_count == 0)
    {
        Log("nothing to launch");
        ErrorLoop(Fault::NoFirmwares);
    }

    Log("ready -- picker up with %d slot(s)", slot_count);
    LogFlush();

    /* Wait for every key to be released before arming, so that a key still held
       down from power-on does not immediately select a slot. */
    bool     armed = false;
    uint32_t tick  = 0;

    while (true)
    {
        const uint32_t now = System::GetNow();
        ServiceUsb(now);

        /* Everything else at the old pace: the key debounce is tuned for it,
           and the battery check is a blocking I2C transaction. */
        if (now - tick < 8)
            continue;
        tick = now;

        hw.ProcessAllControls();

        /* An upload owns fw_image until it finishes or stalls; a key press now
           would read a card image over the top of it. */
        if (upload.Active(now))
        {
            armed = false;
            DrawUploadProgress();
            hw.LowBatteryLockoutCheck();
            continue;
        }

        bool any_down = false;
        for (int i = 0; i < slot_count; i++)
        {
            if (hw.button_sr.State(static_cast<int>(kSlotKey[i])))
            {
                any_down = true;
                if (armed)
                {
                    uint32_t length = 0;

                    Log("key %d pressed -> %s", i + 1, slots[i].name);
                    FlashChoice(i);

                    if (!LoadImage(slots[i], &length))
                    {
                        Log("FAIL: could not read %s off the card", slots[i].name);
                        ErrorLoop(Fault::ReadFailed);
                    }
                    Log("read %lu bytes into SDRAM", (unsigned long)length);

                    Boot(length);
                }
            }
        }

        if (!any_down)
            armed = true;

        DrawPicker(now);
        hw.LowBatteryLockoutCheck();
    }
}
