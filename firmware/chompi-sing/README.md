# SING

A live harmonizer for CHOMPI, built on TAPE.

Sing into the built-in mic (or plug a mic or line source into the input jack) and hold keys:
every held key adds a copy of your voice, pitch-shifted by that key's distance from the
**middle C**. Hold C and E and you hear a third; C, E and G give a major chord. The 25 keys
cover one octave down to one octave up, up to 7 voices at a time. Or flip the toggle switch
to **latch**: hold a chord, let go, and it keeps following your voice while your hands are
free for the knobs. Looper and effects work as in TAPE, on your voice and the harmonies.

**An external mic on the input jack is strongly recommended.** The built-in mic sits on the
same board as the keys and picks up their clicks.

| Knob | Page 1 | Page 2 (press the knob) |
|---|---|---|
| 1 | **transpose** all harmonies, continuous, −12…+12 semitones | **harmony volume** (starts at 75%) |
| 2 | **spread**: voices, ordered by pitch, go alternately left and right | **attack**, 2 ms…500 ms |
| 3 | **doubler**: chorus and slight detune, for a thicker voice | **release**, 20 ms…3 s |
| 4–6 | as in TAPE: effects, looper, output volume / input gain | |

- **Toggle switch: latch.** In the position that turns TAPE's mic monitor off, voices keep
  sounding after you let go of the keys. Press a sounding key again to drop that note;
  switching back releases them all.
- **Chompi key: menu**, as in TAPE. Extra in SING:
  - knob 1 jumps transpose through fifths and octaves (−12, −7, 0, +7, +12); pressing it
    resets transpose to 0 (on page 2: the harmony volume to its default),
  - pressing knob 6 sets where your unshifted voice goes: headphones (orange), all outputs
    (blue) or off (dim red).
- **Your dry voice** is always on, routed as set in the menu.
- **Lights:** held keys magenta, the middle C dim amber. Turn a knob and the white keys show
  its position for a moment (transpose: from the middle C outwards). The knob 1 ring is warm
  white with no transpose, coral below, gold above.

Every start begins from the defaults; SING does not save knob settings.

Tips:
- **Use headphones.** The built-in mic and speaker feed back.
- Knob 6, page 2 (input gain) sets how hot your voice goes into the harmonies.
- With the built-in mic, SING dips the mic for 35 ms after every key change, which reduces
  the key clicks but doesn't remove them. A mic on the input jack avoids them entirely.

## How it works

[`code/src/Harmonizer.h`](code/src/Harmonizer.h): each held key runs the input through its own
copy of the WSOLA pitch shifter from the
[TAPE pitch-shift experiment](https://github.com/ugrossek/CHOMPI/tree/true-pitch-shift/firmware/chompi-tape#pitch-shift-experiment-this-fork)
([`code/src/PitchShifter.h`](code/src/PitchShifter.h)), at a ratio of 2^(semitones/12).

## Install

With the [multi-firmware launcher](https://github.com/sfaber02/CHOMPI/releases/latest) v1.1 or
later, send the `.bin` to a free slot from <https://ugrossek.github.io/CHOMPI/>. On a stock card,
put it in the card root as the only `.bin`, like any firmware update.

SING keeps its files (`options.json`) in a `/SING` folder on the card if there is one,
otherwise in the card root.

## Known limits

- About 30–45 ms of latency on voices shifted up; voices on the middle C have none.
- Key clicks from the built-in mic are reduced, not gone.
- The knob rings stay lit, as the dry voice is always on. On some units LEDs can be heard
  as a faint whine through the built-in mic.
- Tested on one unit.

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
