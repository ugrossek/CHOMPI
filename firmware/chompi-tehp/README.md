# TEHP

TAPE, with pitch that keeps its time.

TEHP is CHOMPI's TAPE firmware with one change: changing pitch no longer changes speed. In TAPE,
every pitch change works like a tape speed change, so a note played an octave up is also twice as
short, and a looper pitched down plays at half tempo. In TEHP a pitch shifter does the pitch, and
everything keeps its length and tempo. Since alpha 2 it also works the other way round: **length**
stretches samples and the loop in time without changing their pitch.

> **Alpha 2.** Tested on one unit by one person. Expect rough edges; feedback is very welcome.

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

## Length (new in alpha 2)

Length plays a sample or the loop slower or faster without changing its pitch, from ½× (half
as long) to 4× (four times as long). Pitch and length are independent, and every key still
plays the sample at the same length, whatever its pitch.

- **Samples: knob 1, page 3** (press knob 1 to page through pitch, pan and length). Turning it
  changes the length smoothly; with **CHOMPI held** it moves in steps: ½, ¾, 1, 1.5, 2, 3 and
  4×, like the pitch knob's fifths and octaves (it follows the `"Pitch Quantize In Shift Menu"`
  option the same way). CHOMPI + knob 1 press sets it back to 1×. The LED goes pink (shorter),
  purple (1×) and blue (longer). In CUBBI mode it sets the latest key's sample, otherwise all.
- **Looper: the loop wheel (knob 5) now has two pages, pitch and length.** Press it to switch
  between them. Both turn smoothly, and in steps with CHOMPI held (pitch in fifths and octaves,
  length in the steps above). **CHOMPI + press resets the page you're on**; a plain press no
  longer resets the loop pitch as it does in TAPE. On the length page the two LEDs work like a
  needle, as on the pitch page: both dim purple at 1×, the left one towards pink when shorter,
  the right one towards blue when longer.
- **Saved with the sample** in `presets.json`, after TAPE's own values, so TAPE reads the file
  as before and ignores the length. If TAPE saves its presets, the lengths go back to 1×.
- **Overdubs** into a stretched loop record what you hear, like overdubs into a pitched loop.

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

- Notes shifted up start about 30–45 ms late: the shifter needs some audio first. Long and
  high notes start a little later still.
- Very high pitches (above three octaves up) stop rising.
- **CPU:** 7 keys at once, or the looper and 6 keys. With more, the audio crackles.
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

For length, a key feeds its sample into the shifter unchanged, one frame every `length` output
samples, and the shifter reads it at the pitch ratio. So the shifter only ever does the pitch,
and its grains are the same stretch of sample at any length. The looper's tape runs at
1 / length, and its output shifter turns the tape speed back into the pitch you set.

To make room for the extra work, TEHP runs its audio in blocks of 48 samples (TAPE: 24), and a
few effect calculations run once per block instead of once per sample. They sound the same.

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
