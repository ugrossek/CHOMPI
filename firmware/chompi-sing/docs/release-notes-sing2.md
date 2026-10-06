<!-- Draft for the SING 2 beta release on GitHub. Not published yet. -->

SING turns CHOMPI into an instrument led by your voice. Press keys and sing, talk or make any sound: CHOMPI plays the keys' notes with your voice. Simple enough for children: press a key, say "hello", and it sings.

![SING cheat sheet](https://raw.githubusercontent.com/ugrossek/CHOMPI/sing/firmware/chompi-sing/docs/cheatsheet.png)

**New in SING 2**

- **Two characters on the play button.** **Robot** (magenta): a vocoder, the Kraftwerk kind; the pitch you sing doesn't matter, and talking, whispering and noises all work. **Human** (gold): your real voice, moved onto the keys' notes, whatever you sing.
- **The keys play their own notes.** Press C-E-G, hear C-E-G. Key 8 is C3.
- **Freeze on the loop button:** hold the sound you're making and keep playing chords with it.
- **Time wheel on the big wheel:** go back through the last 2 seconds, slowly or scratching.
- **Latch follows your voice:** latch a chord and sing a melody, the chord moves with you.
- **Robot size and character** on knobs 2 and 3: monster to mouse, soft to whisper.
- **Metal** (ring modulator, knob 1 page 3) and **Speak & Spell** (knob 4 page 2).
- **A tuner on the keys:** the note you sing lights up.
- **Options** in `/SING/options.json`: Latch Follows Voice, Voice Gate, Show CPU.
- The looper is gone (its buttons and wheel went to freeze, characters and the time wheel).

Full controls: the cheat sheet above and the [README](https://github.com/ugrossek/CHOMPI/tree/sing/firmware/chompi-sing).

**Install:** with the [multi-firmware launcher](https://github.com/sfaber02/CHOMPI/releases/latest) v1.1 or later, send `CHOMPI_SING_2_beta.bin` to a free slot from <https://ugrossek.github.io/CHOMPI/>. On a stock card, put it in the card root as the only `.bin`.

**Use headphones**, and an **external mic on the input jack** if you can: the built-in mic picks up key clicks.

**Beta:** expect rough edges; feedback is welcome.

SHA-256: `<fill in at release>`

Written together with Claude, Anthropic's AI assistant, and tested on one CHOMPI. Unofficial, experimental software, provided as-is with no warranty; use it at your own risk.
