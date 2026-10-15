SING turns CHOMPI into an instrument led by your voice. Press keys and sing, talk or make any sound: CHOMPI plays the keys' notes with your voice.

![SING cheat sheet](https://raw.githubusercontent.com/ugrossek/CHOMPI/sing/firmware/chompi-sing/docs/cheatsheet.png)

**Beta 3 is a bug fix**

- **Line in works.** With a recorder, mixer or mic preamp on the input jack, the voice was very quiet and the input gain hardly helped: SING mixed the left and right channels, and on CHOMPI they cancel each other out. SING now takes the left channel only, so stereo and mono cables both work.

Everything else is as in [beta 2](https://github.com/ugrossek/CHOMPI/releases/tag/sing-beta-2). Full controls: the cheat sheet above and the [README](https://github.com/ugrossek/CHOMPI/tree/sing/firmware/chompi-sing).

**Install:** with the [multi-firmware launcher](https://github.com/sfaber02/CHOMPI-MULTI-FIRMWARE/releases/latest) v1.1 or later, send `CHOMPI_SING_beta_3.bin` to a free slot from <https://ugrossek.github.io/CHOMPI/>. On a stock card, put it in the card root as the only `.bin`.

**Use headphones**, and an **external mic on the input jack** if you can: the built-in mic picks up key clicks.

**Beta:** expect rough edges; feedback is welcome.

SHA-256: `0145b9aff91e26d594d86bd52621107503fecf83b2e92099b5036aac4e791e7c`

Written together with Claude, Anthropic's AI assistant, and tested on one CHOMPI. Unofficial, experimental software, provided as-is with no warranty; use it at your own risk.
