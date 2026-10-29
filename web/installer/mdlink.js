// SPDX-License-Identifier: GPL-3.0-only
//
// MOONDAW's song link (app/link.h) over WebMIDI, the part tools/fm1_send.py
// uses for --ping, --list and --get: enough to save every song on the FM-1
// to this computer before the firmware is replaced. And a ZIP writer (stored,
// no compression) to hand them over as one file.
//
// A message is F0 7D 4D 44 <cmd> <payload, 8 bits packed in 7> F7; the host
// sends one and waits for its answer (the firmware holds a single SysEx).

const SIG = [0x7D, 0x4D, 0x44];
export const PING = 5, INFO = 6, READ = 7, ANSWER = 0x7F;

export const STATUS = {
  1: "the FM-1 is busy: go back to its MENU and try again",
  4: "a message arrived out of turn",
  6: "the FM-1 could not read its flash",
  8: "the FM-1 did not understand the message",
  9: "nothing in that slot",
};

// fm1_send.py pack7: a byte of high bits, then up to seven 7-bit bytes
export function pack7(data) {
  const out = [];
  for (let i = 0; i < data.length; i += 7) {
    const g = data.slice(i, i + 7);
    let hi = 0;
    g.forEach((b, j) => { hi |= (b >> 7) << j; });
    out.push(hi, ...g.map((b) => b & 0x7F));
  }
  return out;
}

export function unpack7(s) {
  const out = [];
  for (let i = 0; i < s.length; i += 8) {
    const g = s.slice(i, i + 8);
    for (let j = 1; j < g.length; j++) out.push(g[j] | (((g[0] >> (j - 1)) & 1) << 7));
  }
  return out;
}

export const message = (cmd, raw = []) => [0xF0, ...SIG, cmd, ...pack7(Array.from(raw)), 0xF7];

const u32le = (v) => [v & 0xFF, (v >>> 8) & 0xFF, (v >>> 16) & 0xFF, (v >>> 24) & 0xFF];

function linkError(st) {
  const e = new Error(STATUS[st] || `the FM-1 refused (status ${st})`);
  e.code = st === 1 ? "busy" : "link";
  return e;
}

export class SongLink {
  // link: fm1ota.js Link (send, read, drain)
  constructor(link, timeoutMs = 1500) { this.link = link; this.timeout = timeoutMs; }

  // one message and its answer: {status, seq, extra, tail}
  async ask(cmd, raw = [], tries = 4) {
    for (let t = 0; t < tries; t++) {
      this.link.drain();
      if (!this.link.send(message(cmd, raw))) break;
      const end = Date.now() + this.timeout;
      for (;;) {
        const left = end - Date.now();
        if (left <= 0) break;
        const m = await this.link.read(left);
        if (!m) break;
        if (m.length < 6 || m[1] !== SIG[0] || m[2] !== SIG[1] || m[3] !== SIG[2] || m[4] !== ANSWER) continue;
        const a = unpack7(m.slice(5, -1));
        if (a.length >= 8 && a[0] === cmd)
          return { status: a[1], seq: a[2] | (a[3] << 8), extra: (a[4] | (a[5] << 8) | (a[6] << 16) | (a[7] << 24)) >>> 0, tail: a.slice(8) };
      }
    }
    const e = new Error("no answer from the FM-1's song link (is MOONDAW running on it?)");
    e.code = "noanswer";
    throw e;
  }

  // -> {version, free}
  async ping() {
    const a = await this.ask(PING);
    return { version: a.seq, free: a.extra };
  }

  // what the device holds: [{slot, name, size}]
  async list() {
    const out = [];
    for (let slot = 0, slots = 1; slot < slots; slot++) {
      const a = await this.ask(INFO, [slot]);
      slots = a.seq;
      if (a.status === 0) out.push({ slot, name: String.fromCharCode(...a.tail), size: a.extra });
      else if (a.status !== 9) throw linkError(a.status);
    }
    return out;
  }

  async fetch(slot, onBytes = () => {}) {
    const data = [];
    for (;;) {
      const a = await this.ask(READ, [slot, ...u32le(data.length)]);
      if (a.status) throw linkError(a.status);
      if (a.extra !== data.length || a.seq !== a.tail.length) throw new Error("the FM-1 answered for another piece");
      if (!a.seq) break;
      for (const b of a.tail) data.push(b);
      onBytes(data.length);
    }
    return new Uint8Array(data);
  }
}

// ------------------------------------------------------------------- ZIP ---

const CRC = new Uint32Array(256);
for (let i = 0; i < 256; i++) {
  let c = i;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
  CRC[i] = c >>> 0;
}
export function crc32(d) {
  let c = 0xFFFFFFFF;
  for (let i = 0; i < d.length; i++) c = CRC[(c ^ d[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

// files: [{name, data: Uint8Array}] -> a ZIP archive (stored), Uint8Array
export function zip(files, date = new Date()) {
  const u16 = (v) => [v & 0xFF, (v >> 8) & 0xFF];
  const time = (date.getHours() << 11) | (date.getMinutes() << 5) | (date.getSeconds() >> 1);
  const day = ((Math.max(date.getFullYear(), 1980) - 1980) << 9) | ((date.getMonth() + 1) << 5) | date.getDate();
  const parts = [], central = [];
  let off = 0;
  for (const f of files) {
    const name = [...f.name].map((c) => c.charCodeAt(0) & 0x7F);
    const crc = crc32(f.data), n = f.data.length;
    const common = [...u16(20), ...u16(0), ...u16(0), ...u16(time), ...u16(day), ...u32le(crc), ...u32le(n), ...u32le(n), ...u16(name.length), ...u16(0)];
    const local = [...u32le(0x04034B50), ...common, ...name];
    central.push(...u32le(0x02014B50), ...u16(20), ...common, ...u16(0), ...u16(0), ...u16(0), ...u32le(0), ...u32le(off), ...name);
    parts.push(new Uint8Array(local), f.data);
    off += local.length + n;
  }
  const end = [...u32le(0x06054B50), ...u16(0), ...u16(0), ...u16(files.length), ...u16(files.length),
    ...u32le(central.length), ...u32le(off), ...u16(0)];
  parts.push(new Uint8Array(central), new Uint8Array(end));
  const out = new Uint8Array(parts.reduce((s, p) => s + p.length, 0));
  let at = 0;
  for (const p of parts) { out.set(p, at); at += p.length; }
  return out;
}
