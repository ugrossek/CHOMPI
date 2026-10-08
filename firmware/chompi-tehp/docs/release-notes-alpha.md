<!-- Draft for the TEHP alpha release on GitHub. Not published yet. -->

TEHP is CHOMPI's TAPE firmware with one change: **changing pitch no longer changes speed.** In TAPE every pitch change is a tape speed change; in TEHP a pitch shifter does the pitch, and everything keeps its length and tempo.

- **Keys:** every note keeps the sample's length, whatever key you play.
- **Pitch knob:** changes the pitch of the samples, not their speed.
- **Looper pitch:** the loop keeps its tempo, so it stays in time while you pitch it in fifths and octaves.
- **Overdubbing while pitched** records what you hear: the new layer stays in tune with the loop.
- **Still tape:** scrubbing, starting and stopping the looper, reset, wow & flutter.

**Shares TAPE's files:** samples, `options.json` and `presets.json`, in `/TAPE` like the launcher's TAPE (or the card root). Tip: `"Tape Slew On": false` in `options.json` removes the tape intro when you press play.

Details and known limits: [README](https://github.com/ugrossek/CHOMPI/tree/tehp/firmware/chompi-tehp).

**Install:** with the [multi-firmware launcher](https://github.com/sfaber02/CHOMPI/releases/latest) v1.1 or later, send `TEHP.bin` to a free slot from <https://ugrossek.github.io/CHOMPI/>. On a stock card, put it in the card root as the only `.bin`.

**Alpha:** tested on one unit; expect rough edges. Feedback is very welcome.

SHA-256: `<fill in at release>`

Written together with Claude, Anthropic's AI assistant. Unofficial, experimental software, provided as-is with no warranty; use it at your own risk.
