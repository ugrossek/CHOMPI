# CHOMPI — TAPE v2.0 Firmware

The flagship sampler firmware for **CHOMPI**: the main firmware every CHOMPI ships with.

---

## Firmware description

TAPE is a 7-voice sampler and varispeed tape looper. Samples stream from the microSD card; the
looper and sample buffer record into SDRAM. Seven streaming voices, varispeed playback,
tape-style looper, delay and reverb, and MIDI in and out over TRS and USB.

## SING prototype (this branch)

SING turns CHOMPI into a live harmonizer. Sing into the built-in mic (or plug a mic or
line source into the input jack) and hold keys: every held key adds a copy of your voice,
pitch-shifted by that key's distance from the **middle C**. Hold C and E and you hear a
third; C, E and G give a major chord. The 25 keys cover one octave down to one octave up,
up to 7 voices at a time. The keys no longer play samples.

Your unshifted (dry) voice is TAPE's own mic monitor, so the **toggle switch** turns it on
and off. Looper and effects work as in TAPE, now on the harmonies.

| Knob | Page 1 | Page 2 (press the knob) |
|---|---|---|
| 1 | harmony volume | spread: low notes left, high notes right |
| 2 | transpose all harmonies, −12…+12 semitones (middle = none) | attack, 2 ms…500 ms |
| 3 | doubler: chorus and slight detune, for a thicker voice | release, 20 ms…3 s |
| 4–6 | as in TAPE: effects, looper, output volume / input gain | |

Held keys light magenta; the middle C glows dim amber and the outer Cs dimmer.

Tips:
- **Use headphones.** The built-in mic and speaker feed back.
- Knob 6, page 2 (input gain) sets how hot your voice goes into the harmonies.
- The built-in mic sits on the same board as the keys and hears their clicks. SING dips the
  mic for 35 ms after every key change, which helps but doesn't remove them. A mic on the
  input jack avoids them entirely.

How it works: [`code/src/Harmonizer.h`](code/src/Harmonizer.h). Each held key runs the input
through its own copy of the pitch shifter described below, at a ratio of 2^(semitones/12).
The voices reuse the sample voices' shifter buffers.

Install: with the [multi-firmware launcher](https://github.com/sfaber02/CHOMPI/releases/latest)
v1.1 or later, send the `.bin` to a free slot from <https://ugrossek.github.io/CHOMPI/>. On a
stock card, put it in the card root as the only `.bin`, like any firmware update.

Known limits:
- About 30–45 ms of latency on voices shifted up; voices on the middle C have none.
- Key clicks from the built-in mic are reduced, not gone.
- In the dry-monitor position of the toggle switch, the knob rings and C markers are off,
  as in TAPE.
- RAM is full: SING's voices live in DTCM, and `.bss` has a few hundred bytes left.
- A prototype, tested on one unit.

## Pitch-shift experiment (this fork)

In stock TAPE, a higher pitch plays the sample faster and a lower pitch plays it slower, like
a tape. In this fork a voice always streams from the card at normal speed, and a pitch shifter
changes the pitch without changing the speed. It replaces the `_double` file switching, which
frees about 4 KB of code space.

How it works: each voice has a delay line in SDRAM. A read tap moves through it at the pitch
ratio. When the tap runs out of room it jumps back by about one grain, to the offset where the
waveform best lines up with what is playing (cross-correlation, WSOLA-style), and crossfades.
The search runs on a decimated mono copy of the signal in fast RAM and is spread over many
samples. All voices share a per-block search budget so the audio callback isn't overloaded.
See [`code/src/PitchShifter.h`](code/src/PitchShifter.h).

A ready-to-flash build is on the [Releases page](../../../../releases/latest).

Known limits:
- About 30–45 ms of onset latency when shifting up.
- With many notes at high ratios, later voices fall back to unaligned splices (the shared
  budget ran out), which sounds rougher.
- The looper's pitch control is unchanged and still varispeed.
- Tested on one unit, by ear. The first boot after flashing hung once and worked on retry;
  the cause is unknown.
- Flash headroom is tight: about 2.5 KB of code and 430 B of SRAM data remain.

Also fixed: the `Limiter.h` include case, so the firmware builds on Linux.

### No warranty

This is unofficial, experimental software, provided as-is without warranty of any kind (see
[`LICENSE`](../../LICENSE)). You use it at your own risk. It is loaded from the SD card and
never touches the bootloader. Keep a copy of the original firmware binary so you can put it
back, and if a unit ever won't boot, USB DFU mode lives in the processor's ROM and can't be
overwritten: <https://flash.daisy.audio>.

## Building

Toolchain: GNU Arm Embedded 10.3-2021.10. Newer compilers overflow the firmware's SRAM region
and fail at the link step.

Warning: This firmware is pretty much at capacity, with only 376 bytes of SRAM space remaining.
This means that any additional tweaks or features will very likely require sacrificing something
to free up the necessary code space.

## Repository layout

```
code/src/                 the firmware
code/libs/                vendored libDaisy, DaisySP, coreJSON (MIT)
code/Chompi_Bootloader/   the bootloader this firmware is loaded by
code/bms_test/            standalone battery-management bring-up example
bin/                      bootloader binary and install script
```

## SD card layout

The card holds the firmware binary, the sample banks, and two JSON files: `options.json`
(global settings) and `presets.json` (per-slot knob positions). Samples are named
`<instrument>_<bank><slot>.wav` — 48 kHz, 16-bit stereo. (Stock TAPE also expects a matching
`_double` variant for high-pitched playback; this fork no longer uses it.)

## Support Guidelines

This is a discontinuation open-source release. As such, this repo is intended to be a permanent
source for files and documentation, and will likely not be receiving updates in the future. If you wish
to customize your own project, we recommend cloning this repo into your own GitHub.

## Community

Even though this version of CHOMPI is now discontinued, the CLUB is expanding. If you want to
discuss this project, share your creations, see what other users have made on their CHOMPI, feel
free to check out the CHOMPI Open Source channel on the Chase Bliss Discord.

## License

MIT — see [`LICENSE`](../../LICENSE) at the root of this repo. [`THIRD_PARTY.md`](../../THIRD_PARTY.md)
lists the work this builds on. The CHOMPI name and marks are not covered by the license — see
[`TRADEMARKS.md`](../../TRADEMARKS.md).
