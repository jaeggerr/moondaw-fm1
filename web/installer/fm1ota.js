// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Adapted for MOONDAW: the same steps, in the same order, as tools/fm1_install.py.
//
// The FM-1 USB-MIDI update over WebMIDI. Step 1: the running firmware
// (official, Felucca or MOONDAW) reads parts of the package and stages the
// update loader. Step 2: the loader reads the whole package and writes it.
// In both steps the device asks (cmd 0x30 read requests on the logical image)
// and we answer. Then the FM-1 restarts and the installed identity is checked.
//
// Kept byte for byte equal to fm1_install.py: tools/installer_compare.py runs
// both against the same simulated FM-1 and compares every message sent.

export const HS_QUERY = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
export const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
export const FINISH_CHECK = 0xE0000000, FINISH_WRITE = 0xF0000000;
const MAXDATA = 512;
// never probe other gear: only ports whose name looks like an FM-1 (fm1_install.py PORT_RE, + moondaw)
export const PORT_RE = /fm-1|fm1|felucca|moondaw|ota|composite|sinco|usb-midi/i;

// milliseconds (fm1_install.py DELAY); the tests shorten them
export const DELAY = {
  open: 300, start: 2000, reply: 10, loader: 3000, reboot: 3000, retry: 1000,
  poll: 1000, hs: 1000, idleCheck: 8000, idleWrite: 180000, waitLoader: 30000, waitReboot: 40000,
};

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// error codes, as fm1_install.py's: notfound, model, lost, stopped, badreq, noloader, noreturn, mismatch
export function fail(code, msg, detail = "") { const e = new Error(msg); e.code = code; e.detail = detail; return e; }

// ------------------------------------------------------------ wire format ---

export function pack7(data) {
  const out = []; let acc = 0, nb = 0;
  for (const b of data) {
    acc |= b << nb; nb += 8;
    while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
  }
  if (nb) out.push(acc & 0x7F);
  return out;
}

export function unpack7(s) {
  const out = []; let acc = 0, nb = 0;
  for (const b of s) {
    acc |= (b & 0x7F) << nb; nb += 7;
    while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; }
  }
  return out;
}

export function response(addr, data, fl = 0) {
  const n = data.length;
  const body = [0x00, 0x59, 0x30, (n + 8) & 0xFF, ((n + 8) >> 8) & 0xFF, 0, fl,
    addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
    n & 0xFF, (n >> 8) & 0xFF, 0];
  for (const b of data) body.push(b);
  let s = 0;
  for (let i = 6; i < body.length; i++) s += body[i];
  body.push(~s & 0xFF);
  return [0xF0, ...pack7(body), 0xF7];
}

// -> {fl, addr, len} or null
export function parseRequest(pkt) {
  if (pkt.length < 2 || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const u = unpack7(pkt.slice(1, -1));
  if (u.length !== 15 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x30) return null;
  let s = 0;
  for (let i = 6; i < 14; i++) s += u[i];
  if ((~s & 0xFF) !== u[14]) return null;
  return { fl: u[6], addr: (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0, len: u[11] | (u[12] << 8) | (u[13] << 16) };
}

// bytes from the device as text: printable ASCII only
const printable = (b) => b.map((c) => (c >= 32 && c < 127 ? String.fromCharCode(c) : "?")).join("");

// -> {text, model, version, loader} or null
export function parseIdentity(pkt) {
  if (pkt.length < 2 || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const d = unpack7(pkt.slice(1, -1));
  if (d.length !== 34 || d[0] !== 0 || d[1] !== 0x59 || d[2] !== 0x11) return null;
  const raw = d.slice(6, 33);
  while (raw.length && raw[raw.length - 1] === 0) raw.pop();
  const text = printable(raw);
  const m = /^([^_]+)_(\d+)$/.exec(text);
  return m ? { text, model: m[1], version: parseInt(m[2], 10), loader: /^ota-/i.test(m[1]) } : null;
}

export const modelOf = (text) => text.split("_")[0].replace(/^ota-/, "");

// what an identity means, as far as this project knows (docs/ARRIVEE.md)
export function describe(id) {
  if (id.loader) return "the update loader: an update was cut off and is waiting to be finished";
  if (id.model !== "FM-1") return "not an FM-1";
  const v = id.version;
  if (v >= 9330000 && v < 9340000) return "MOONDAW";
  if (v === 900) return "MOONDAW 0.1 (or another FM-1_900 build: hello, a Felucca development build)";
  if (v === 917) return "Felucca 1.7";
  if (v > 900 && v < 1000) return "Felucca or another Felucca-based firmware";
  if (v > 0 && v < 100) return `the official M-VAVE firmware V${v}`;
  if (v === 8000014) return "M-VAVE's FM1 Open";
  return "an unknown FM-1 firmware";
}

// ------------------------------------------------------------------ MIDI ---

// one MIDI in/out pair with a SysEx queue
export class Link {
  constructor(input, output) {
    this.input = input; this.output = output; this.q = []; this.waiter = null;
    input.onmidimessage = (ev) => {
      if (ev.data[0] !== 0xF0) return;
      this.q.push(Array.from(ev.data));
      if (this.waiter) { const w = this.waiter; this.waiter = null; w(); }
    };
  }
  get lost() { return this.input.state === "disconnected" || this.output.state === "disconnected"; }
  // false when the port is gone: WebMIDI throws on a disconnected output
  send(bytes) {
    try { this.output.send(bytes); return true; } catch (_) { return false; }
  }
  drain() { this.q.length = 0; }
  async read(ms) {
    if (!this.q.length) {
      let tm;
      await Promise.race([new Promise((r) => { this.waiter = r; }), new Promise((r) => { tm = setTimeout(r, Math.max(ms, 0)); })]);
      clearTimeout(tm);
      this.waiter = null;
    }
    return this.q.shift() || null;
  }
  close() { this.input.onmidimessage = null; }
}

// the output that goes with an input (fm1_install.py pair_output: Windows numbers each port)
function pairOutput(name, outs) {
  const same = outs.filter((o) => o.name === name);
  if (same.length) return same[0];
  const base = (n) => (n || "").replace(/\s+\d+$/, "");
  const like = outs.filter((o) => base(o.name) === base(name));
  if (like.length === 1) return like[0];
  return outs.length === 1 ? outs[0] : null;
}

export async function handshake(link, tries = 3, hsMs = DELAY.hs) {
  link.drain();
  for (let t = 0; t < tries; t++) {
    if (!link.send(HS_QUERY)) return null;
    const end = Date.now() + hsMs;
    for (;;) {
      const left = end - Date.now();
      if (left <= 0) break;
      const p = await link.read(left);
      if (!p) break;
      const id = parseIdentity(p);
      if (id) return id;
    }
  }
  return null;
}

export class Updater {
  constructor(access, delay = {}) {
    this.access = access;
    this.d = { ...DELAY, ...delay };
  }

  // every connected input that may be an FM-1, in order
  candidates() {
    return [...this.access.inputs.values()].filter((i) => i.state !== "disconnected" && PORT_RE.test(i.name || ""));
  }

  // the first matching port that answers the handshake (and want(identity)): {link, id, name}
  async find(want = null) {
    const outs = [...this.access.outputs.values()].filter((o) => o.state !== "disconnected");
    for (const input of this.candidates()) {
      const output = pairOutput(input.name, outs);
      if (!output) continue;
      try { await input.open(); await output.open(); } catch (_) { continue; }
      const link = new Link(input, output);
      await sleep(this.d.open);               // CoreMIDI may drop a message sent just after opening
      const id = await handshake(link, 2, this.d.hs);
      if (id && (!want || want(id))) return { link, id, name: input.name };
      link.close();
    }
    return null;
  }

  async waitFor(want, ms) {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      const dev = await this.find(want);
      if (dev) return dev;
      await sleep(this.d.retry);
    }
    return null;
  }

  // answer read requests until the device asks for `finish` -> {served, finished, lost}
  async serve(link, image, finish, idleMs, progress) {
    let served = 0, last = Date.now();
    for (;;) {
      const pkt = await link.read(this.d.poll);
      if (!pkt) {
        if (link.lost) return { served, finished: false, lost: true };
        if (Date.now() - last > idleMs) return { served, finished: false, lost: false };
        continue;
      }
      const r = parseRequest(pkt);
      if (!r) continue;
      last = Date.now();
      if (r.addr === FINISH_CHECK || r.addr === FINISH_WRITE) {
        link.send(response(r.addr, [0x73, 0x75, 0x63, 0x63, 0x65, 0x73, 0x73, 0]));   // "success\0"
        if (r.addr === finish) return { served, finished: true, lost: false };
        continue;
      }
      if (r.len > MAXDATA || r.addr + r.len > image.length)
        throw fail("badreq", `the device asked for 0x${r.addr.toString(16)}+${r.len}, outside the package`);
      await sleep(this.d.reply);
      if (!link.send(response(r.addr, Array.from(image.subarray(r.addr, r.addr + r.len)), r.fl)))
        return { served, finished: false, lost: true };
      served++;
      progress(served, r.addr + r.len);
    }
  }

  // step 1: the running firmware checks the package and starts the loader
  async check(dev, image, step) {
    step("start", dev.id.text);
    dev.link.send(UPGRADE);
    await sleep(this.d.start);
    const s = await this.serve(dev.link, image, FINISH_CHECK, this.d.idleCheck, (k) => step("check", k));
    dev.link.close();
    if (!s.finished)
      throw fail(s.lost ? "lost" : "stopped", `the FM-1 ${s.lost ? "was disconnected" : "stopped answering"} after ${s.served} requests; nothing was written`);
    step("loader");
    await sleep(this.d.loader);
    const ota = await this.waitFor((i) => i.loader, this.d.waitLoader);
    if (!ota) throw fail("noloader", "the update loader did not appear");
    return ota;
  }

  // step 2: the loader reads and writes the whole package, then restarts
  async write(ota, image, step) {
    step("write", 0);
    ota.link.send(UPGRADE);
    await sleep(this.d.start);
    const s = await this.serve(ota.link, image, FINISH_WRITE, this.d.idleWrite,
      (k, end) => step("write", Math.min(99, Math.floor(end * 100 / image.length))));
    ota.link.close();
    if (!s.finished)
      throw fail(s.lost ? "lost" : "stopped", `the loader ${s.lost ? "was disconnected" : "stopped answering"} after ${s.served} requests`);
    step("write", 100);
  }

  async verify(product, step) {
    step("reboot");
    await sleep(this.d.reboot);
    const back = await this.waitFor((i) => !i.loader, this.d.waitReboot);
    if (!back) throw fail("noreturn", "the FM-1 did not come back");
    back.link.close();
    if (back.id.text !== product) throw fail("mismatch", `written, but the FM-1 reports ${back.id.text} (expected ${product})`, back.id.text);
    step("done", back.id.text);
    return back.id.text;
  }

  // running firmware -> loader -> new firmware; dev from find(). A device already in
  // update mode only needs the write.
  async install(dev, image, product, step = () => {}) {
    if (modelOf(dev.id.text) !== modelOf(product)) {
      dev.link.close();
      throw fail("model", `the device is ${dev.id.text}, the package is for ${product}`, dev.id.text);
    }
    const ota = dev.id.loader ? dev : await this.check(dev, image, step);
    await this.write(ota, image, step);
    return this.verify(product, step);
  }
}
