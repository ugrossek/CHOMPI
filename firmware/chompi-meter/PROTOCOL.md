# meter over USB-MIDI

A page on a computer (the tutorial, later a display) talks to meter over
USB-MIDI with SysEx. Every message, both ways, is

    F0 7D 4D 54 <cmd> <payload...> F7

7D is the MIDI non-commercial ID, 4D 54 is "MT". Payload bytes are 7-bit.
Values marked q7 are 0..1 as 0..127.

meter only sends after a hello and stops 3 s after the last one, so the page
says hello about once a second. Sends can get lost: meter repeats its state at
least every 0.5 s and every channel at least every 2 s, changed or not.

## Page to meter

| cmd | name     | payload |
|-----|----------|---------|
| 01  | hello    | — (meter answers 41, then sends everything) |
| 02  | hint     | 5 bytes, 7 bits each, low first: a bit per control id; those controls breathe in white |
| 03  | channel  | c, preset, octave+2, ratio, level q7, pan q7, muted, count, then count × (phase lo, phase hi, note) |
| 04  | ensemble | selected channel, L0 ms lo, L0 ms hi (0: keep), running |
| 05  | start    | — |

- **channel** sets a channel: its sound from the preset (the knobs' changes go),
  its loop's notes (phase 0..16383 over the loop, note without the octave),
  the loop from its start. Up to 32 notes.
- **ensemble** selects a channel, sets the tempo and play, and restarts every
  loop together. The all-mode offsets, ensemble ratio, level and stereo field go
  back to neutral.
- **start** is power-on: every channel on its own sound, no loops. The volume
  and the room stay as they are.

A lesson sets up with start, a channel message for each player, then ensemble.

## meter to page

| cmd | name    | payload |
|-----|---------|---------|
| 41  | hello   | version (1), channels (15), presets (10) |
| 42  | state   | see below |
| 43  | channel | c, preset, octave+2, ratio, pending+1, level q7, pan q7, strike, brightness, decay, shimmer, feel, buzz (q7), count, count × (phase lo, phase hi, note) |
| 44  | event   | kind, id, value |
| 45  | clock   | L0's place, then each channel's place in its loop (15 ×), each 0..16383 as lo, hi; about 10 a second |

**state:** selected, flags, ensemble ratio, pending ensemble ratio+1, L0 ms lo,
L0 ms hi, volume q7, room q7, width q7 (0..2), the ensemble's level q7 (0..2), the all-mode
offsets strike, brightness, decay, shimmer, feel, buzz (q7, −.5..+.5), muted
(3 bytes, a bit per channel), listened to (3 bytes), held (CHOMPI 1, play 2,
loop 4), load q7 (share of the audio callback's time), voices sounding, voices
cut by the load guard so far (mod 128).

Flags: running 1, switch on all 2, sounds on the keys 4, octave on the keys 8,
meter on the keys 16, reboot armed 32.

**event** kinds: 1 press, 2 release, 3 turn (value: clicks + 64), 4 switch
(value 1: all), 5 L0 beat (value: count mod 64), 6 note (id: channel, value:
the note sounding, octave included; played by hand or by the loop).

## Control ids

| id | control |
|----|---------|
| 0–24 | the keys, low to high (white keys 1–15 are ids 0 2 4 5 7 9 11 12 14 16 17 19 21 23 24) |
| 25 | CHOMPI |
| 26 | play |
| 27 | loop |
| 28–31 | K1–K4 |
| 32 | wheel |
| 33 | K6 |
| 34 | switch |

Ratios (index): 1/3, 1/2, 2/3, 3/4, 1, 5/4, 4/3, 3/2, 2, 3.
Presets: Wood, Marimba, Vibes, Bronze, Glass, Bell, Gong, Felt, Snare bar, Muffled.
