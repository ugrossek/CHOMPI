# Spectrograms of the rendered clips, as PNG (log frequency, dB), to look at.
import sys, struct, zlib, wave, numpy as np
def png(path, img):
    h, w, _ = img.shape
    raw = b"".join(b"\x00" + img[y].astype(np.uint8).tobytes() for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
def load(p):
    w = wave.open(p); n = w.getnframes(); x = np.frombuffer(w.readframes(n), dtype=np.int16).reshape(-1, 2) / 32768.0
    return x, w.getframerate()
for p in sys.argv[1:]:
    x, sr = load(p); m = x.mean(1)
    N, hop = 4096, 256
    win = np.hanning(N)
    frames = (len(m) - N) // hop
    W, H = min(frames, 1400), 360
    idx = np.linspace(0, frames - 1, W).astype(int)
    S = np.array([np.abs(np.fft.rfft(m[i * hop:i * hop + N] * win)) for i in idx]).T
    f = np.fft.rfftfreq(N, 1 / sr)
    fy = np.geomspace(40, 16000, H)[::-1]
    S = np.array([np.interp(fy, f, S[:, j]) for j in range(W)]).T
    db = 20 * np.log10(S + 1e-9); db = np.clip((db - db.max() + 80) / 80, 0, 1)
    c = np.stack([db ** .7 * 255, db ** 1.6 * 230, (db ** 3) * 150 + (1 - db) * 30], -1)
    # octave lines at C
    for cf in [65.4 * 2 ** k for k in range(8)]:
        y = int(np.argmin(np.abs(fy - cf))); c[y, :, :] = c[y, :, :] * .5 + 60
    png(p.replace(".wav", ".png"), c)
