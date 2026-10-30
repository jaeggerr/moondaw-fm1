// SPDX-License-Identifier: GPL-3.0-only
//
// MOONDAW's song link (app/link.h) over WebMIDI, the part tools/fm1_send.py
// uses for --ping, --list, --get and sending a song, and the bank index
// tools/fm1_samples.py list reads: enough to save every song on the FM-1 to
// this computer before the firmware is replaced, and to put one back. And a
// ZIP writer (stored, no compression) to hand them over as one file.
//
// A message is F0 7D 4D 44 <cmd> <payload, 8 bits packed in 7> F7; the host
// sends one and waits for its answer (the firmware holds a single SysEx).

const SIG = [0x7D, 0x4D, 0x44];
export const BEGIN = 1, DATA = 2, END = 3, CANCEL = 4, PING = 5, INFO = 6, READ = 7, ANSWER = 0x7F;
export const DATA_MAX = 240;   // app/link.h LINK_DATA_MAX
export const BANK_SLOT = 32;   // core/bank.h: the sample bank's slot of the store

// app/link.h LINK_*, with fm1_send.py's words.
export const STATUS = {
  1: "the FM-1 is busy: go back to its MENU and try again",
  2: "a song of that name is already on the FM-1 (ask to replace it)",
  3: "not enough room in the FM-1's flash",
  4: "a message arrived out of turn",
  5: "the song arrived damaged; nothing was kept",
  6: "the FM-1 could not read or write its flash",
  7: "that song is open on the FM-1 with unsaved changes: save it there first",
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
  async ask(cmd, raw = [], tries = 4, timeoutMs = this.timeout) {
    for (let t = 0; t < tries; t++) {
      this.link.drain();
      if (!this.link.send(message(cmd, raw))) break;
      const end = Date.now() + timeoutMs;
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

  // A song onto the FM-1, as tools/fm1_send.py sends it: BEGIN with the
  // length, the checksum and the name, the bytes in pieces, then END -- the
  // new song becomes the file only at END, so a cable pulled halfway leaves
  // the flash as it was. -> the bytes the FM-1 kept.
  async put(data, name, { replace = false } = {}, onBytes = () => {}) {
    const nm = [...name.toUpperCase().slice(0, 12)].map((c) => c.charCodeAt(0) & 0x7F);
    const head = [replace ? 1 : 0, ...u32le(data.length), ...u32le(crc32(data)), ...nm];
    let a = await this.ask(BEGIN, head);
    if (a.status) throw linkError(a.status);
    try {
      for (let seq = 0, at = 0; at < data.length; seq++, at += DATA_MAX) {
        a = await this.ask(DATA, [seq & 0xFF, (seq >> 8) & 0xFF, ...data.subarray(at, at + DATA_MAX)]);
        if (a.status) throw linkError(a.status);
        onBytes(Math.min(at + DATA_MAX, data.length));
      }
    } catch (e) {
      try { await this.ask(CANCEL, [], 1); } catch (_) { /* the FM-1 is gone: nothing to cancel */ }
      throw e;
    }
    a = await this.ask(END, [], 2, 5000);
    if (a.status) throw linkError(a.status);
    return a.extra;
  }

  // The sample bank's index, over the link: core/bank.h's header and its
  // entries, which is all the editor needs to know which sounds the FM-1
  // holds. The whole bank is 160 KB and never read here.
  // -> {size, samples: [{name, frames}]} or null when there is no bank.
  async bankIndex() {
    const HEAD = 8, ENTRY = 32, NAME_LEN = 24;      // core/bank.h
    const a = await this.ask(INFO, [BANK_SLOT]);
    if (a.status === 9) return null;
    if (a.status) throw linkError(a.status);
    const size = a.extra;
    const head = [];
    while (head.length < HEAD || head.length < HEAD + ENTRY * head[4]) {
      const r = await this.ask(READ, [BANK_SLOT, ...u32le(head.length)]);
      if (r.status) throw linkError(r.status);
      if (!r.seq) break;
      for (const b of r.tail) head.push(b);
    }
    // The FM-1 stops sending: it is free for what comes next (fm1_samples.py).
    try { await this.ask(CANCEL, [], 1); } catch (_) { /* nothing to cancel */ }
    if (head.length < HEAD || head[0] !== 0x53 || head[1] !== 0x4D || head[2] !== 0x50 || head[3] !== 0x31)
      throw new Error("the FM-1's bank slot does not begin with SMP1: it is not a sample bank");
    const samples = [];
    for (let i = 0; i < head[4]; i++) {
      const e = HEAD + i * ENTRY;
      if (e + ENTRY > head.length) break;
      let n = 0;
      while (n < NAME_LEN && head[e + n]) n++;
      const frames = (head[e + NAME_LEN] | (head[e + NAME_LEN + 1] << 8) |
        (head[e + NAME_LEN + 2] << 16) | (head[e + NAME_LEN + 3] << 24)) >>> 0;
      samples.push({ name: String.fromCharCode(...head.slice(e, e + n)), frames });
    }
    return { size, samples };
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
