// Checks docs/chompi-midi.js: the encoding against PROTOCOL.md's example, and
// (given a device path, e.g. the pty from the host harness) a full upload.
//   node web-client.test.mjs [/dev/pts/N image.bin slot name]
import assert from "node:assert/strict";
import fs from "node:fs";
import tty from "node:tty";
import * as P from "../../../docs/chompi-midi.js";

const hex = (s) => s.trim().split(/\s+/).map((h) => parseInt(h, 16));
const img = Uint8Array.from(hex("00 00 02 20 01 19 00 24 FF 80"));

assert.equal(P.crc32(img), 0x8966E0AF);
assert.deepEqual(P.message(P.BEGIN, [...P.put7(10, 4), 5, 8, ...Buffer.from("TAPE-DEV")]),
  hex("F0 7D 43 48 02 0A 00 00 00 05 08 54 41 50 45 2D 44 45 56 F7"));
assert.deepEqual(P.message(P.DATA, [...P.put7(0, 4), ...P.pack7(img.subarray(0, 8))]),
  hex("F0 7D 43 48 03 00 00 00 00 00 00 00 02 20 01 19 00 00 24 F7"));
assert.deepEqual(P.message(P.DATA, [...P.put7(8, 4), ...P.pack7(img.subarray(8))]),
  hex("F0 7D 43 48 03 08 00 00 00 03 7F 00 F7"));
assert.deepEqual(P.message(P.END, P.put7(P.crc32(img), 5)), hex("F0 7D 43 48 04 2F 41 1B 4B 08 F7"));
assert.equal(P.get7(hex("08 57 0E 00")), 240520);
console.log("encoding matches PROTOCOL.md");

const [dev, file, slot, name] = process.argv.slice(2);
if (dev) {
  const fd = fs.openSync(dev, "r+");
  let buf = [], handler = () => {};
  const port = { send: (b) => fs.writeSync(fd, Buffer.from(b)), onMessage: (fn) => { handler = fn; } };
  const rx = new tty.ReadStream(fd);
  rx.setRawMode(true);
  rx.on("data", (chunk) => {
    for (const b of chunk) {
      if (b === 0xF0) buf = [];
      buf.push(b);
      if (b === 0xF7) handler(Uint8Array.from(buf));
    }
  });
  const l = new P.Launcher(port);
  try {
    const info = await l.ping();
    console.log("ping", info);
    const image = new Uint8Array(fs.readFileSync(file));
    let last = -1;
    await l.upload(image, Number(slot), name, info.maxChunk, (ph, d, t) => {
      const pct = Math.floor((100 * d) / t);
      if (ph === "store" || pct >= last + 25) { console.log(ph, d, t); last = pct; }
    });
    console.log("upload OK");
  } catch (e) {
    console.log("upload failed:", e.constructor.name, e.message);
  }
  process.exit(0);
}
