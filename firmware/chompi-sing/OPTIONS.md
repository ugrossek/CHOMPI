# SING options

SING reads `options.json` from its folder on the card (`/SING`) once at power-on,
like TAPE. It never writes settings while running: an earlier try at saving the knobs
during play stalled the unit for seconds, so knobs always start from their defaults.

## Already in `options.json` (inherited from TAPE)

| Name | Values | In SING |
|---|---|---|
| Midi In Channel | 1–16 | channel for incoming notes (they play harmonies) and knob CCs |
| Midi Out Channel | 1–16 | channel for outgoing key notes and knob CCs |
| Monitor Position | 1–3 | where the dry voice goes at power-on. Still TAPE's numbers: 1 headphones, 2 all outputs; SING's "off" can't be chosen here yet |
| Tape Slew On | true/false | looper pitch changes glide (TAPE's looper) |
| Pitch Quantize In Shift Menu | true/false | looper pitch steps in the menu or on the page (TAPE's looper) |
| Split Delay | true/false | TAPE's split delay on the effects knob |
| Record Latch | true/false | from TAPE; SING doesn't use it |
| Latch Follows Voice | true/false | true: while latched, the chord follows your voice (key 8 = your note). false: latched notes stay put |
| Voice Gate | true/false | true: harmonies only while there is a voice or a sound loud enough. false (default): every sound goes through, noises are part of the fun |
| Show CPU | true/false | true: in the menu (chompi key held), the white keys show the audio load, for debugging |

## Candidates

Settings that came up while building SING and are worth making configurable.
None of them exists yet.

| Setting | Choices | Default | Why |
|---|---|---|---|
| Voice gate hold | ms | 150 | How long the gate waits in a pause before it closes. |
| Voice gate level | input level | ~−37 dB | What counts as "sound" for opening the gate; depends on the mic and the room. |
| Dry voice "off" at power-on | add to Monitor Position | headphones | SING's own monitor choice, see above. |
| Sung-note light | on / off | on | The key of the note you sing lights up. |
| Value bar on the white keys | on / off | on | Shows a knob's position after turning it. Some people find it busy. |
| Lowest pitch for detection | Hz | 80 | Lower (e.g. 60 Hz) for bass voices, at ~8 ms more delay. |

Add to this list when a choice comes up that different players would make differently.
