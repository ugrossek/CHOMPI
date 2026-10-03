# SING

A live harmonizer for CHOMPI, built on TAPE. **Work in progress:** the sample engine TAPE
uses is still inside and is being taken out.

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

## How it works

[`code/src/Harmonizer.h`](code/src/Harmonizer.h): each held key runs the input through its own
copy of the WSOLA pitch shifter from the
[TAPE pitch-shift experiment](https://github.com/ugrossek/CHOMPI/tree/true-pitch-shift/firmware/chompi-tape#pitch-shift-experiment-this-fork)
([`code/src/PitchShifter.h`](code/src/PitchShifter.h)), at a ratio of 2^(semitones/12).

## Install

With the [multi-firmware launcher](https://github.com/sfaber02/CHOMPI/releases/latest) v1.1 or
later, send the `.bin` to a free slot from <https://ugrossek.github.io/CHOMPI/>. On a stock card,
put it in the card root as the only `.bin`, like any firmware update.

SING keeps its settings and looper recordings in a `/SING` folder on the card if there is one,
otherwise in the card root.

## Known limits

- About 30–45 ms of latency on voices shifted up; voices on the middle C have none.
- Key clicks from the built-in mic are reduced, not gone.
- In the dry-monitor position of the toggle switch, the knob rings and C markers are off,
  as in TAPE.
- RAM is full: SING's voices live in DTCM, and `.bss` has a few hundred bytes left.
- A prototype, tested on one unit.

## Building

Toolchain: GNU Arm Embedded 10.3-2021.10, as for TAPE. SING uses TAPE's prebuilt libraries
from `../../../chompi-tape/code/libs` rather than a copy (override with
`make CHOMPI_LIBS=/path/to/libs`):

```bash
cd code/src
PATH=/path/to/gcc-arm-none-eabi-10.3-2021.10/bin:$PATH make -j8
```

Output is `build/CHOMPI.bin`.

## Written with AI

SING was written together with Claude, Anthropic's AI assistant, and tested on one CHOMPI.
Unofficial, experimental software, provided as-is with no warranty (see
[`LICENSE`](../../LICENSE)); use it at your own risk.
