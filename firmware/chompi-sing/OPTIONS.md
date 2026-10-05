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

## Candidates

Settings that came up while building SING and are worth making configurable.
None of them exists yet.

| Setting | Choices | Default | Why |
|---|---|---|---|
| Chord octave | **nearest**: each chord note in the octave closest to the sung pitch · **pressed key**: voices in the octave of the key | nearest | Absolute chords. Nearest keeps shifts small (less warble, no chipmunks when singing low and playing high); pressed key is more predictable. |
| Voice gate at power-on | on / off | off | Harmonies only while you sing; switched in the menu (top black key). |
| Voice gate hold | ms | 150 | How long the gate waits in a pause before it closes. |
| Voice gate level | input level | ~−34 dB | What counts as "sound" for opening the gate; depends on the mic and the room. |
| Dry voice "off" at power-on | add to Monitor Position | headphones | SING's own monitor choice, see above. |
| Sung-note light | on / off | on | The key of the note you sing lights up. |
| Value bar on the white keys | on / off | on | Shows a knob's position after turning it. Some people find it busy. |
| Lowest pitch for detection | Hz | 80 | Lower (e.g. 60 Hz) for bass voices, at ~8 ms more delay. |

Add to this list when a choice comes up that different players would make differently.
