TEHP is CHOMPI's TAPE firmware with one change: **changing pitch no longer changes speed.** Alpha 2 adds the other half: **length** stretches samples and the loop in time without changing their pitch.

**New in alpha 2**

- **Length for samples: knob 1, page 3.** From ½× to 4×, smooth when you turn it, in steps (½, ¾, 1, 1.5, 2, 3, 4×) with CHOMPI held. CHOMPI + press sets it back to 1×. Saved with the sample in `presets.json`; TAPE ignores it.
- **Length for the looper: the loop wheel now has two pages, pitch and length.** Press to switch. Both turn smoothly, and in steps with CHOMPI held. CHOMPI + press resets the page you're on. A plain press no longer resets the loop pitch.
- **Loop length LEDs** work like the pitch page's needle: left towards pink is shorter, right towards blue is longer.
- **Pitch now reaches three octaves up** (two in alpha 1).
- **More headroom:** 7 keys at once, or the looper and 6 keys. Audio runs in blocks of 48 samples, and a few effect calculations are cheaper. They sound the same.

**Still the same:** keys keep their length at any pitch, the looper keeps its tempo when pitched, overdubs record what you hear, scrubbing and transport are tape. TEHP shares TAPE's files in `/TAPE` (or the card root).

Details and known limits: [README](https://github.com/ugrossek/CHOMPI/tree/tehp/firmware/chompi-tehp).

**Install:** with the [multi-firmware launcher](https://github.com/sfaber02/CHOMPI/releases/latest) v1.1 or later, send `TEHP.bin` to a free slot from <https://ugrossek.github.io/CHOMPI/>. On a stock card, put it in the card root as the only `.bin`.

**Alpha:** tested on one unit; expect rough edges. Feedback is very welcome.

SHA-256: `4167b646497b8605d162e7325b711adc97f4544dabe1ccbe2ed657b777e6b097`

Written together with Claude, Anthropic's AI assistant. Unofficial, experimental software, provided as-is with no warranty; use it at your own risk.
