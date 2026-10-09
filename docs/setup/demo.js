/* Demo mode (?demo): a pretend CHOMPI in the browser, for trying the page without one.
   ?demo&old = a launcher too old for the page; ?demo&failslot=N = writing key N fails. */
(() => {
  const P = new URLSearchParams(location.search);
  if (!P.has("demo")) return;
  const OLD = P.has("old");
  const card = JSON.parse(P.get("card") || "null") || {
    "01_TAPE.bin": 240620, "02_TEMPO.bin": 263624, "03_WAVE.bin": 200664,
    "15_USB_STORAGE.bin": 91392, "HMMM.bin": 150600, "launcher_log.txt": 100 };
  window.__card = card;
  const failSlot = +(P.get("failslot") || 0);
  function keys() {
    const k = Array(15).fill(null);
    const bins = Object.keys(card).filter((f) => /\.bin$/i.test(f)).sort((a, b) => a.toLowerCase() < b.toLowerCase() ? -1 : 1);
    const rest = [];
    for (const f of bins) { const m = /^(\d\d)_/.exec(f); const n = m ? +m[1] : 0;
      if (n >= 1 && n <= 15 && !k[n - 1]) k[n - 1] = f; else rest.push(f); }
    let g = 0; for (const f of rest) { while (g < 15 && k[g]) g++; if (g < 15) k[g] = f; }
    return k;
  }
  const g7 = (a) => a.reduceRight((v, b) => v * 128 + b, 0);
  const p7 = (v, n) => Array.from({ length: n }, (_, i) => Math.floor(v / 128 ** i) & 0x7f);
  let up = null;
  const input = { name: "CHOMPI", manufacturer: "x", state: "connected", type: "input", onmidimessage: null, open: async () => {} };
  const reply = (cmd, p) => setTimeout(() => input.onmidimessage && input.onmidimessage({ data: new Uint8Array([0xF0,0x7D,0x43,0x48,cmd|0x40,...p,0xF7]) }), 3);
  const output = { name: "CHOMPI", manufacturer: "x", state: "connected", type: "output", open: async () => {},
    send(m) {
      const cmd = m[4], b = m.slice(5, -1);
      if (cmd === 1) return reply(1, OLD ? [0,1,...p7(256,4),15] : [0,1,...p7(256,4),15,7]);
      if (cmd === 2) { const n = b[5]; up = { size: g7(b.slice(0,4)), slot: b[4], name: String.fromCharCode(...b.slice(6,6+n)), got: 0 }; return reply(2,[0]); }
      if (cmd === 3) { const off = g7(b.slice(0,4)); const L = b.length - 4; const raw = L - Math.ceil(L/8); up.got = Math.max(up.got, off+raw); return reply(3,[0,...p7(up.got,4)]); }
      if (cmd === 4) {
        if (OLD && b.length !== 5) return reply(4,[1]);
        if (up.slot === failSlot) return setTimeout(() => reply(4,[11]), 200);
        const pre = String(up.slot).padStart(2,"0") + "_";
        for (const f of Object.keys(card)) if (f.startsWith(pre)) delete card[f];
        card[pre + up.name + ".bin"] = up.size;
        return setTimeout(() => reply(4,[0]), 300);
      }
      if (OLD) return reply(cmd,[1]);
      if (cmd === 5) { const f = keys()[b[0]-1]; if (!f) return reply(5,[0,b[0],0,0,0,0,0]);
        return reply(5,[0,b[0],f.length,...[...f].map(c=>c.charCodeAt(0)),...p7(card[f],4)]); }
      if (cmd === 6) { const pre = String(b[0]).padStart(2,"0")+"_"; const on = keys()[b[0]-1];
        for (const f of Object.keys(card)) if (f.startsWith(pre) || f === on) delete card[f]; return reply(6,[0]); }
    } };
  const access = { inputs: new Map([["i", input]]), outputs: new Map([["o", output]]), onstatechange: null };
  navigator.requestMIDIAccess = async () => access;
  const q = navigator.permissions.query.bind(navigator.permissions);
  navigator.permissions.query = async (d) => d.name === "midi" ? { state: "granted" } : q(d);
})();
