import sys, wave, struct, zlib, glob, os, json
import numpy as np, lameenc
S = os.path.dirname(os.path.abspath(__file__))
os.makedirs(f"{S}/site/a", exist_ok=True); os.makedirs(f"{S}/site/s", exist_ok=True)
def png(path, img):
    h, w, _ = img.shape
    raw = b"".join(b"\x00" + img[y].astype(np.uint8).tobytes() for y in range(h))
    ch = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + ch(b"IDAT", zlib.compress(raw, 9)) + ch(b"IEND", b""))
out = {}
for p in sorted(glob.glob(f"{S}/out/*.wav")):
    name = os.path.basename(p)[:-4]
    w = wave.open(p); n = w.getnframes(); raw = w.readframes(n)
    enc = lameenc.Encoder(); enc.set_bit_rate(160); enc.set_in_sample_rate(48000); enc.set_channels(2); enc.set_quality(2)
    mp3 = enc.encode(raw) + enc.flush()
    open(f"{S}/site/a/{name}.mp3", "wb").write(mp3)
    x = np.frombuffer(raw, dtype=np.int16).reshape(-1, 2) / 32768.0
    m = x.mean(1); N, hop = 2048, 128
    win = np.hanning(N); frames = (len(m) - N) // hop
    W, H = 900, 160
    idx = np.linspace(0, frames - 1, W).astype(int)
    Sp = np.array([np.abs(np.fft.rfft(m[i * hop:i * hop + N] * win)) for i in idx]).T
    f = np.fft.rfftfreq(N, 1 / 48000); fy = np.geomspace(40, 16000, H)[::-1]
    Sp = np.array([np.interp(fy, f, Sp[:, j]) for j in range(W)]).T
    db = 20 * np.log10(Sp + 1e-9); db = np.clip((db - db.max() + 70) / 70, 0, 1)
    # ink on the panel: quiet is the panel's grey, loud is ink, the loudest the key's orange
    bg = np.array([228, 230, 233.]); ink = np.array([26, 28, 32.]); hot = np.array([226, 84, 27.])
    t = (db ** 1.25)[..., None]
    img = bg * (1 - t) + ink * t
    h = np.clip((db - .82) / .18, 0, 1)[..., None]
    img = img * (1 - h) + hot * h
    png(f"{S}/site/s/{name}.png", img)
    out[name] = {"dur": round(n / 48000, 2), "kb": len(mp3) // 1024}
json.dump(out, open(f"{S}/site/clips.json", "w"))
print(sum(v["kb"] for v in out.values()), "KB mp3 total;", len(out), "clips")
