// Client side of the CHOMPI launcher's USB-MIDI upload protocol.
// Specified in firmware/chompi-launcher/PROTOCOL.md; this mirrors midi-send.py.
//
// Transport-agnostic: give it a `port` with send(bytes) and onMessage(fn), so
// the same code runs against Web MIDI in the page and a pty in the tests.

export const PROTOCOL_VERSION = 1;

const HEADER = [0xF0, 0x7D, 0x43, 0x48];
export const PING = 0x01, BEGIN = 0x02, DATA = 0x03, END = 0x04, RUN = 0x05, LIST = 0x06, CLEAR = 0x07;
/** Where the test launchers 1.4.0 - 1.4.2 had them, before 05 went to RUN. */
const OLD_LIST = 0x05, OLD_CLEAR = 0x06;
const REPLY = 0x40;

/** PING's optional features byte; launchers before it have none of these. */
export const FEATURE = { LIST: 1, CLEAR: 2, STAY: 4, LAUNCHER: 8, RUN: 16 };
/** BEGIN's slot for a new launcher (FEATURE.LAUNCHER). */
export const LAUNCHER_SLOT = 127;
const END_STAY = 1;

export const STATUS = {
  0: "OK",
  1: "The launcher didn't understand a message.",
  2: "A piece of the firmware arrived out of order.",
  3: "That file is too big to be a CHOMPI firmware.",
  4: "The firmware got damaged on the way. Send it again.",
  5: "That file isn't a CHOMPI firmware.",
  6: "The launcher lost track of the upload. Send it again.",
  7: "The upload ended before all of it arrived. Send it again.",
  8: "The launcher doesn't have that slot.",
  9: "The launcher refused that name.",
  10: "CHOMPI has no SD card, or can't read it. Insert one and send again.",
  11: "Writing to the SD card failed. Check the card and send again.",
  12: "The SD card is full. Make some room on it and send again.",
  13: "That file isn't a CHOMPI launcher.",
  14: "There's another .bin file at the top level of the SD card, which CHOMPI would install instead of the launcher. Remove it, then try again.",
};

export const NAME_RE = /^[A-Z0-9_-]{1,16}$/;

export class ProtocolError extends Error {}

export function put7(value, n) {
  const out = [];
  for (let i = 0; i < n; i++) out.push(Math.floor(value / 2 ** (7 * i)) & 0x7F);
  return out;
}

export function get7(bytes) {
  let v = 0;
  for (let i = bytes.length - 1; i >= 0; i--) v = v * 128 + (bytes[i] & 0x7F);
  return v;
}

/** Each group of up to 7 bytes, preceded by a byte of their top bits. */
export function pack7(raw) {
  const out = [];
  for (let g = 0; g < raw.length; g += 7) {
    const group = raw.subarray(g, g + 7);
    let hi = 0;
    group.forEach((b, i) => { hi |= ((b >> 7) & 1) << i; });
    out.push(hi);
    group.forEach((b) => out.push(b & 0x7F));
  }
  return out;
}

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

/** IEEE CRC-32, the same as zlib.crc32(). */
export function crc32(bytes) {
  let c = 0xFFFFFFFF;
  for (let i = 0; i < bytes.length; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

export function message(cmd, payload = []) {
  return [...HEADER, cmd, ...payload, 0xF7];
}

/** "TAPE" for a CHOMPI.bin from chompi-tape; otherwise the file's own name. */
export function defaultName(fileName) {
  const stem = fileName.replace(/\.[^.]*$/, "");
  return stem.toUpperCase().replace(/[^A-Z0-9_-]/g, "-").slice(0, 16) || "FIRMWARE";
}

export class Launcher {
  /** @param port {send(bytes), onMessage(fn)}  @param log optional (dir, bytes) => void */
  constructor(port, log = () => {}) {
    this.port = port;
    this.log = log;
    this.waiting = null;
    this.cmd = { LIST, CLEAR }; // ping() switches to the old numbers if needed
    port.onMessage((bytes) => this.#receive(bytes));
  }

  #receive(bytes) {
    this.log("in", bytes);
    const w = this.waiting;
    if (!w || bytes.length < 6) return;
    for (let i = 0; i < 4; i++) if (bytes[i] !== HEADER[i]) return;
    if (bytes[4] !== (w.cmd | REPLY)) return; // not the reply we are waiting for
    const body = bytes.slice(5, -1);
    if (w.match && !w.match(body)) return; // a late reply to an earlier resend
    this.waiting = null;
    clearTimeout(w.timer);
    w.resolve(body);
  }

  #once(cmd, payload, timeout, match) {
    return new Promise((resolve) => {
      const timer = setTimeout(() => { this.waiting = null; resolve(null); }, timeout);
      this.waiting = { cmd, resolve, timer, match };
      const msg = message(cmd, payload);
      this.log("out", msg);
      this.port.send(msg);
    });
  }

  /** Send and wait for the reply, resending on silence. Null if none came.
   *  match(reply), if given, skips replies it rejects. */
  async call(cmd, payload = [], { timeout = 300, retries = 10, match = null } = {}) {
    for (let i = 0; i < retries; i++) {
      const reply = await this.#once(cmd, payload, timeout, match);
      if (reply) return reply;
    }
    return null;
  }

  static #check(reply) {
    if (!reply) throw new ProtocolError("CHOMPI stopped answering. Is the launcher still showing?");
    if (reply[0] !== 0) throw new ProtocolError(STATUS[reply[0]] || `The launcher answered with status ${reply[0]}.`);
  }

  /** Who is there. Throws if nobody is, or if it speaks another version. */
  async ping({ retries = 3 } = {}) {
    const r = await this.call(PING, [], { retries });
    if (!r) throw new ProtocolError("No answer. Turn CHOMPI on so the launcher is showing, then try again.");
    const version = r[1];
    if (version !== PROTOCOL_VERSION || r.length < 7)
      throw new ProtocolError(`This CHOMPI's launcher speaks protocol ${version}; this page speaks ${PROTOCOL_VERSION}. Put the newer launcher on the card.`);
    const features = r.length > 7 ? r[7] : 0;
    const launcherVersion = r.length > 10 ? [...r.slice(8, 11)].join(".") : null;
    const v = launcherVersion ? launcherVersion.split(".").map(Number) : [0, 0, 0];
    const oldNumbers = features & (FEATURE.LIST | FEATURE.CLEAR) && v[0] * 1e6 + v[1] * 1e3 + v[2] < 1004003;
    this.cmd = oldNumbers ? { LIST: OLD_LIST, CLEAR: OLD_CLEAR } : { LIST, CLEAR };
    return { version, maxChunk: get7(r.slice(2, 6)), slots: r[6], features, launcherVersion };
  }

  /** LIST and CLEAR replies name their slot; failures are the status only. */
  static #forSlot(slot) {
    return (r) => r[0] !== 0 || r.length < 2 || r[1] === slot;
  }

  /** What is on a key: { file, size }, file "" if the key is empty. */
  async list(slot) {
    const r = await this.call(this.cmd.LIST, [slot], { timeout: 2000, retries: 3, match: Launcher.#forSlot(slot) });
    Launcher.#check(r);
    const n = r[2];
    const file = String.fromCharCode(...r.slice(3, 3 + n));
    return { file, size: n ? get7(r.slice(3 + n, 7 + n)) : 0 };
  }

  /** Empty a key. With `file` (what list() reported there), only that file
   *  goes: if the key holds something else by now, nothing is removed.
   *  Returns whether something was removed (true from older launchers). */
  async clear(slot, file = "") {
    const name = [...file].map((c) => c.charCodeAt(0) & 0x7F);
    const payload = name.length ? [slot, name.length, ...name] : [slot];
    const r = await this.call(this.cmd.CLEAR, payload, { timeout: 2000, retries: 3, match: Launcher.#forSlot(slot) });
    Launcher.#check(r);
    return r.length < 3 || r[2] === 1;
  }

  /** Start the firmware on a key, as pressing it would (FEATURE.RUN; on the
   *  test launchers 1.4.0 - 1.4.2, 05 is LIST). Throws if the key is empty.
   *  No reply most likely means it started before the reply got out. */
  async run(slot) {
    const r = await this.call(RUN, [slot], { timeout: 1000, retries: 2 });
    if (r) Launcher.#check(r);
  }

  /**
   * Upload, store in a slot, and let the launcher start it.
   * onProgress(phase, done, total): phase is "send" or "store".
   * stay: only store it, the launcher keeps showing the picker (FEATURE.STAY).
   * Returns how long it took: { sendMs, storeMs }.
   * end: false sends everything but no END, so nothing reaches the card and
   * the next BEGIN discards it. For measuring the transfer alone.
   */
  async upload(image, slot, name, maxChunk, onProgress = () => {}, { stay = false, end = true } = {}) {
    if (!NAME_RE.test(name)) throw new ProtocolError("Names are 1–16 of A–Z, 0–9, - and _.");
    const nameBytes = [...name].map((c) => c.charCodeAt(0));
    Launcher.#check(await this.call(BEGIN, [...put7(image.length, 4), slot, nameBytes.length, ...nameBytes]));

    const t0 = performance.now();
    let offset = 0;
    while (offset < image.length) {
      const chunk = image.subarray(offset, offset + maxChunk);
      const reply = await this.call(DATA, [...put7(offset, 4), ...pack7(chunk)]);
      Launcher.#check(reply);
      offset = get7(reply.slice(1, 5)); // the launcher says how far it got
      onProgress("send", offset, image.length);
    }

    // The reply comes once the card write is done. One resend covers an END
    // that got lost; if it was the reply that got lost, the launcher has
    // already started the firmware and the resend goes unanswered too.
    const t1 = performance.now();
    if (!end) return { sendMs: t1 - t0, storeMs: 0 };
    onProgress("store", 0, image.length);
    const flags = stay ? [END_STAY] : [];
    const reply = await this.call(END, [...put7(crc32(image), 5), ...flags], { timeout: 10000, retries: 2 });
    if (!reply) throw new ProtocolError(stay
      ? "No answer after writing. The firmware was probably stored; check CHOMPI."
      : "No answer after writing. The firmware was probably stored and started; check CHOMPI.");
    Launcher.#check(reply);
    return { sendMs: t1 - t0, storeMs: performance.now() - t1 };
  }
}
