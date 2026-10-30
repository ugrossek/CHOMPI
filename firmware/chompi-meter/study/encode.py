import sys, wave, numpy as np, lameenc, struct, zlib, glob, os, json
def png(path, img):
    h, w, _ = img.shape
    raw = b"".join(b"\x00" + img[y].astype(np.uint8).tobytes() for y in range(h))
    ch = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + ch(b"IDAT", zlib.compress(raw, 9)) + ch(b"IEND", b""))
out = {}
for p in sorted(glob.glob("out/*.wav")):
    name = os.path.basename(p)[:-4]
    w = wave.open(p); n = w.getnframes(); raw = w.readframes(n)
    enc = lameenc.Encoder(); enc.set_bit_rate(160); enc.set_in_sample_rate(48000); enc.set_channels(2); enc.set_quality(2)
    mp3 = enc.encode(raw) + enc.flush()
    open(f"site/a/{name}.mp3", "wb").write(mp3)
    x = np.frombuffer(raw, dtype=np.int16).reshape(-1, 2) / 32768.0
    m = x.mean(1); N, hop = 4096, 192
    win = np.hanning(N); frames = (len(m) - N) // hop
    W, H = 960, 200
    idx = np.linspace(0, frames - 1, W).astype(int)
    S = np.array([np.abs(np.fft.rfft(m[i * hop:i * hop + N] * win)) for i in idx]).T
    f = np.fft.rfftfreq(N, 1 / 48000); fy = np.geomspace(50, 14000, H)[::-1]
    S = np.array([np.interp(fy, f, S[:, j]) for j in range(W)]).T
    db = 20 * np.log10(S + 1e-9); db = np.clip((db - db.max() + 72) / 72, 0, 1)
    # bronze on night: dark ground, warm mids, pale hot
    r = 18 + 237 * db ** .85; g = 17 + 215 * db ** 1.7; b = 24 + 40 * db ** .9 + 160 * db ** 4
    png(f"site/s/{name}.png", np.stack([r, g, b], -1))
    out[name] = {"dur": round(n / 48000, 2), "kb": len(mp3) // 1024}
json.dump(out, open("site/clips.json", "w"))
print(sum(v["kb"] for v in out.values()), "KB mp3 total;", len(out), "clips")
