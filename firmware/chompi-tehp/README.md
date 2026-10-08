# TEHP

TAPE, with pitch that keeps its time.

TEHP is CHOMPI's TAPE firmware with one change: changing pitch no longer changes speed. In TAPE,
every pitch change works like a tape speed change, so a note played an octave up is also twice as
short, and a looper pitched down plays at half tempo. In TEHP a pitch shifter does the pitch, and
everything keeps its length and tempo.

> **Alpha.** Tested on one unit by one person. Expect rough edges; feedback is very welcome.

## What's different from TAPE

- **Keys:** every note keeps the sample's length, whatever key you play.
- **Pitch knob (knob 1):** changes the pitch of the samples without changing their speed.
- **Looper pitch:** the loop keeps its length and tempo, so it stays in time with other gear
  while you pitch it in fifths and octaves (or freely). It still glides to a new pitch like
  TAPE does; with `"Tape Slew On": false` it jumps.
- **Overdubbing while the loop is pitched** records what you hear: on the next pass the new
  layer is at the pitch you played it, in tune with the loop.
- **Still tape:** scrubbing with the big wheel, starting and stopping the looper, reset, wow &
  flutter and delay-time changes behave as in TAPE.

Everything else (controls, samples, presets, effects, MIDI) is TAPE's. See TAPE's manual.

## Install

With the [multi-firmware launcher](https://github.com/sfaber02/CHOMPI/releases/latest) v1.1 or
later, send `TEHP.bin` to a free slot from <https://ugrossek.github.io/CHOMPI/>. On a stock card,
put it in the card root as the only `.bin`, like any firmware update.

**TEHP shares TAPE's files**: samples, `options.json` and `presets.json`. Like the launcher's
TAPE, it uses the `/TAPE` folder when there is one, otherwise the card root. Switching between
TAPE and TEHP keeps your sounds and settings, and a setting changed in one applies to both.

TEHP doesn't need the `_double` copies TAPE keeps of every sample. When you record or copy a
sample in TEHP, TAPE rebuilds its copy the next time it starts, which takes a moment.

## Tips

- **No tape intro on play:** set `"Tape Slew On": false` in `options.json`. The looper then
  starts and stops at once, and its pitch steps without gliding (in TAPE too).

## Known limits

- Notes shifted up start about 30–45 ms late: the shifter needs some audio first.
- Very high pitches (above about two octaves up) stop rising.
- The shifter splices the sound into short pieces: chords and dense loops can warble slightly
  and drum hits can double at large intervals. With many notes at high pitches, some splices
  are less carefully placed.
- Overdubs while the loop is pitched can land slightly late (up to about 90 ms).
- The free (unquantized) looper pitch near the middle of its range gives a very low drone at
  normal speed, where TAPE slows almost to a stop.
- Tested on one unit.

## How it works

[`code/src/PitchShifter.h`](code/src/PitchShifter.h) is a WSOLA-style time-domain shifter: one
read tap moves through a short delay line at the pitch ratio, and when it runs out of room it
jumps back by about a grain, to where the waveform best lines up with what's playing
(cross-correlation on a decimated copy), with a short crossfade. Each key voice has one; the
looper has one on its output and one at the inverse ratio on its overdub input. The delay lines
sit in D2 RAM, which is much faster than SDRAM; in SDRAM, playing several keys made the audio
click.

## Building

Toolchain: GNU Arm Embedded 10.3-2021.10, as for TAPE. TEHP uses TAPE's prebuilt libraries
from `../../../chompi-tape/code/libs` rather than a copy (override with
`make CHOMPI_LIBS=/path/to/libs`):

```bash
cd code/src
PATH=/path/to/gcc-arm-none-eabi-10.3-2021.10/bin:$PATH make -j8
```

Output is `build/TEHP.bin`.

## Written with AI

TEHP was written together with Claude, Anthropic's AI assistant, and tested on one CHOMPI.
Unofficial, experimental software, provided as-is with no warranty (see
[`LICENSE`](../../LICENSE)); use it at your own risk.
