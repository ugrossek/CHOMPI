import json, os
S = os.path.dirname(os.path.abspath(__file__))
man = json.load(open(f"{S}/out/manifest.json")); cpu = json.load(open(f"{S}/out/cpu.json")); clips = json.load(open(f"{S}/site/clips.json"))
# key, id, engine, short, what it is, knobs (strike bright decay | shimmer feel prep)
SND = [
 (1, "kick", "Drum", "Kick", "A sine that falls in pitch, two octaves under the key, with a beater's click: the 808's kick.", [.55,.30,.35,0,.20,.15]),
 (2, "tom", "Drum", "Tom", "The same drum one octave under the key, with the head's second mode ringing.", [.45,.20,.35,.45,.25,.10]),
 (3, "snare", "Noise", "Snare", "Noise through a band tuned to the key, a head under it.", [.60,.45,.25,.10,.30,0]),
 (4, "clap", "Noise", "Clap", "Three quick claps and a tail, in a band around 1 kHz.", [.30,.35,.35,.10,.35,.85]),
 (5, "hat", "Metal", "Hi-hat", "Six square waves at the 808's odd ratios, in a high band.", [.50,.85,.08,1,.30,0]),
 (6, "cowbell", "Metal", "Cowbell", "Two of the six squares, the 808's 540 and 800 Hz pair, in a middle band.", [.30,.35,.25,0,.25,0]),
 (7, "blip", "Chip", "Blip", "A game console's pulse with a jump up in pitch at the start, its volume in 16 steps.", [.60,.50,.12,0,.20,0]),
 (8, "acid", "Acid", "Acid", "A saw through a resonant lowpass that snaps open and closes: the 303.", [.70,.25,.30,0,.30,.75]),
 (9, "fmpiano", "FM", "FM piano", "Two sines at 1 : 1, a tine's knock on the attack and a chorus.", [.55,.25,.50,.40,.30,.20]),
 (10, "fmbell", "FM", "FM bell", "Two sines at 1 : 3.5, so the partials don't line up: a bell, long.", [.60,.30,.75,.30,.25,.55]),
 (11, "chiplead", "Chip", "Chip lead", "A square wave with vibrato that comes in after a moment.", [.15,.45,.45,.60,.25,0]),
 (12, "stab", "Acid", "Saw stab", "Two saws a little apart through the acid's filter, gentler.", [.40,.55,.35,.80,.30,.20]),
 (13, "grit", "Grit", "Grit", "FM with feedback, a wave folder and a bit crusher: lo-fi.", [.35,.55,.35,.40,.30,.30]),
 (14, "fmbass", "FM", "FM bass", "Two sines at 1 : ½, short and round.", [.50,.35,.30,0,.25,.05]),
]
byid = {m["id"]: m for m in man}
out = []
for key, sid, eng, short, desc, kn in SND:
    m = byid["solo-" + sid]; l0 = (m["len"] - 1.2) / 4
    note = m["note"]; note = note[0].upper() + note[1:]
    if note.lower().startswith("then "): note = note[5].upper() + note[6:]
    out.append(dict(id="solo-" + sid, key=key, engine=eng, title=m["title"], short=short, desc=desc, then=note,
                    dur=clips["solo-" + sid]["dur"], half=2 * l0, cpu=cpu[sid], knobs=kn))
for pid in ["piece-machine", "piece-console", "piece-two-sets"]:
    m = byid[pid]
    out.append(dict(id=pid, piece=True, title=m["title"], dur=clips[pid]["dur"], players=m["players"]))
data = dict(clips=out, gangsa=cpu["gangsa"])
html = open(f"{S}/page.html").read().replace("/*DATA*/null", json.dumps(data, ensure_ascii=False))
open(f"{S}/site/circles-set-2.html", "w").write(html)
print(len(out), "cards,", len(html) // 1024, "KB page")
