// SPDX-License-Identifier: GPL-3.0-only
//
// The .trk song file in the browser: core/trk.c and the part of core/song.c it
// leans on, ported line for line. The point is not to be idiomatic JavaScript,
// it is to read and write exactly what the FM-1 reads and writes -- a song
// loaded here and saved again must come out byte for byte as build/trkdump
// gives it, or the page would quietly damage songs. test/roundtrip.mjs holds
// both against every .trk of the repository, the way tools/installer_compare.py
// holds fm1ota.js against fm1_install.py.
//
// So: the C's integer truncation is kept (u8, i8), the order of the loader's
// tests is kept, and a field absent from a line leaves what was there before
// it, as the C struct does. Where a comment here says "as moondaw", it is
// repeating core/trk.c's own.

// ------------------------------------------------------------- the model ---

export const SONG_MAX_TRACKS = 8;
export const STEP_MAX_NOTES = 4;
export const STEP_MAX_FX = 3;
export const PATTERN_MAX_STEPS = 128;
export const PATTERN_DEF_STEPS = 16;
export const TRACK_MAX_PATTERNS = 16;
export const SONG_MAX_INSTR = 64;
export const SONG_ARRANGE_LEN = 128;
export const SONG_MAX_SECTIONS = SONG_ARRANGE_LEN;
export const INSTR_MAX_LAYERS = 4;
export const INSTR_NAME_LEN = 8;  // 7 + NUL
export const SONG_NAME_LEN = 12;
export const ENTRY_CYCLE_MAX = 1024;

export const CHUNK_STEPS = 16;
export const POOL_CHUNKS = 375;
export const PATTERN_CHUNKS = PATTERN_MAX_STEPS / CHUNK_STEPS;
export const CHUNK_NONE = 0xFFFF;

export const SONG_SYL_LEN = 6;    // 5 characters + NUL
export const SONG_SYL_MAX = 255;

export const VOX_TRACT_MIN = 50, VOX_TRACT_MAX = 150;
export const VOX_ACCENT_FR = 0, VOX_ACCENT_EN = 1;

export const SWING_MIN = 50, SWING_MAX = 75;
export const BPM_MIN = 20, BPM_MAX = 400;
export const METER_MAX = 16, TICKS_MAX = 24;

export const NOTE_NONE = 0xFF, NOTE_OFF = 0xFE, NOTE_MIN = 12, NOTE_MAX = 127;
export const INSTR_NONE = 0xFF;
export const PATTERN_NONE = 0xFF, PATTERN_HOLD = 0xFE;
export const SECTION_NONE = 0xFF;
export const VOL_FULL = 127;

export const SONG_SMP_MAX = 64;
export const SONG_SMP_NAME = 48; // 47 + NUL
export const SONG_ZONE_MAX = 128;
export const ZONE_TUNE_MAX = 100;
export const SMP_LOOP_OFF = 0, SMP_LOOP_FWD = 1, SMP_LOOP_PING = 2;

export const FX_NONE = 0;
export const FX_NAMES = [
  "---", "DLY", "CUT", "GAT", "RET", "STR", "VOL", "VEL", "PAN", "REV",
  "ECH", "DUT", "BPM", "TSP", "SLI", "VIB", "SWP", "SCP", "ARP", "TRM",
  "FLT", "RES", "WAV", "RMP", "CRS", "RAT", "SYL", "SMP", "PRB",
];
export const FX_COUNT = FX_NAMES.length;
export const FX_SYL = FX_NAMES.indexOf("SYL");
export const FX_SMP = FX_NAMES.indexOf("SMP");
export const FX_RMP = FX_NAMES.indexOf("RMP");

export const ENGINE_PULSE = 0, ENGINE_NOISE = 1, ENGINE_WAVE = 2,
  ENGINE_SAMPLE = 3, ENGINE_VOX = 4, ENGINE_PLUCK = 5, ENGINE_DRUM = 6;
export const ENGINE_NAMES = ["PULSE", "NOISE", "WAVE", "SMPL", "VOX", "PLUCK", "DRUM"];
export const ENGINE_COUNT = ENGINE_NAMES.length;
const TIMBRE_MAX = [3, 1, 4, 0, 4, 2, 9];

export const ENTRY_PHRASE = 0, ENTRY_REF = 1;
export const REF_PHRASE = 0, REF_SECTION = 1;

export const FILTER_OPEN = 255, FILTER_ENV_NONE = 64;
export const GAIN_MIN_DB = -64, GAIN_MAX_DB = 24;
export const VIB_PHASE_FREE = -1;

export const TRK_OK = 0, TRK_ERR_VERSION = -1, TRK_ERR_PATCH = -2;

// The C's widths. Every value that lands in the struct goes through one of
// these, so a `cutoff 300` in a file becomes 44 here as it does there.
const u8 = (v) => v & 0xFF;
const i8 = (v) => (v << 24) >> 24;
const i16 = (v) => (v << 16) >> 16;
const clampi = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

export const engineName = (e) => ENGINE_NAMES[e < ENGINE_COUNT ? e : 0];
export const engineTimbreMax = (e) => TIMBRE_MAX[e < ENGINE_COUNT ? e : 0];
export const fxName = (c) => FX_NAMES[c < FX_COUNT ? c : 0];

export function emptyStep() {
  return {
    notes: [NOTE_NONE, NOTE_NONE, NOTE_NONE, NOTE_NONE],
    instrument: INSTR_NONE,
    fx: [{ cmd: FX_NONE, val: 0 }, { cmd: FX_NONE, val: 0 }, { cmd: FX_NONE, val: 0 }],
  };
}
const copyStep = (d, s) => {
  for (let i = 0; i < STEP_MAX_NOTES; i++) d.notes[i] = s.notes[i];
  d.instrument = s.instrument;
  for (let i = 0; i < STEP_MAX_FX; i++) { d.fx[i].cmd = s.fx[i].cmd; d.fx[i].val = s.fx[i].val; }
};

export function stepIsEmpty(st) {
  for (let i = 0; i < STEP_MAX_NOTES; i++) if (st.notes[i] !== NOTE_NONE) return false;
  if (st.instrument !== INSTR_NONE) return false;
  for (let i = 0; i < STEP_MAX_FX; i++) if (st.fx[i].cmd !== FX_NONE) return false;
  return true;
}

// core/song.c song_init: the defaults a field absent from the file keeps.
export function songInit() {
  const s = {
    bpm: 120, ticksPerStep: 6, stepsPerBeat: 4, beatsPerBar: 4,
    numTracks: SONG_MAX_TRACKS, arrangeLen: 1, sectionCount: 0,
    reverbSize: 64, reverbDamp: 64, delayTime: 18, delayFb: 60, swing: SWING_MIN,
    trackInit: [], instruments: [], arrangement: [], sections: [],
    patterns: [], pool: [], poolUsed: new Uint8Array((POOL_CHUNKS + 7) >> 3),
    syl: new Array(SONG_SYL_MAX).fill(""), smp: [], zones: [], nzones: 0,
  };
  for (let t = 0; t < SONG_MAX_TRACKS; t++) {
    s.trackInit.push({ volume: 100, pan: 0, send: 0, echo: 0 });
    s.patterns.push([]);
    for (let p = 0; p < TRACK_MAX_PATTERNS; p++)
      s.patterns[t].push({ length: PATTERN_DEF_STEPS, chunk: new Array(PATTERN_CHUNKS).fill(CHUNK_NONE) });
  }
  for (let k = 0; k < POOL_CHUNKS; k++) {
    const c = [];
    for (let i = 0; i < CHUNK_STEPS; i++) c.push(emptyStep());
    s.pool.push(c);
  }
  // A neutral sustained pulse everywhere.
  for (let i = 0; i < SONG_MAX_INSTR; i++)
    s.instruments.push({
      engine: ENGINE_PULSE, timbre: 2,
      attack: 0, decay: 0, sustain: 255, release: 10,
      cutoff: FILTER_OPEN, reso: 0, filtEnv: FILTER_ENV_NONE, filtVel: FILTER_ENV_NONE,
      gain: 0, level: 0, vibPhase: 0, tract: 0, accent: VOX_ACCENT_FR,
      drum: [0, 0, 0, 0], nlayers: 0, layers: [], name: "",
    });
  for (let i = 0; i < SONG_ARRANGE_LEN; i++) {
    s.arrangement.push({
      pattern: new Array(SONG_MAX_TRACKS).fill(PATTERN_NONE),
      level: new Array(SONG_MAX_TRACKS).fill(VOL_FULL),
      steps: 0, name: "", kind: ENTRY_PHRASE, refKind: REF_PHRASE, refTarget: 0,
      section: SECTION_NONE,
    });
    s.sections.push("");
  }
  for (let i = 0; i < SONG_SMP_MAX; i++)
    s.smp.push({ name: "", begin: 0, loopStart: 0, loopEnd: 0, loop: SMP_LOOP_OFF });
  for (let i = 0; i < SONG_ZONE_MAX; i++)
    s.zones.push({ inst: INSTR_NONE, lo: 0, hi: 0, root: 0, smp: 0, tune: 0, gain: 0 });
  return s;
}

// A deep copy, so the page can keep what was loaded and compare against it.
export function songCopy(s) {
  const d = songInit();
  for (const k of ["bpm", "ticksPerStep", "stepsPerBeat", "beatsPerBar", "numTracks",
    "arrangeLen", "sectionCount", "reverbSize", "reverbDamp", "delayTime", "delayFb",
    "swing", "nzones"]) d[k] = s[k];
  s.trackInit.forEach((t, i) => Object.assign(d.trackInit[i], t));
  s.instruments.forEach((in_, i) => {
    Object.assign(d.instruments[i], in_, { drum: in_.drum.slice(), layers: in_.layers.map((l) => ({ ...l })) });
  });
  s.arrangement.forEach((e, i) => Object.assign(d.arrangement[i], e,
    { pattern: e.pattern.slice(), level: e.level.slice() }));
  d.sections = s.sections.slice();
  d.syl = s.syl.slice();
  s.smp.forEach((e, i) => Object.assign(d.smp[i], e));
  s.zones.forEach((z, i) => Object.assign(d.zones[i], z));
  for (let t = 0; t < SONG_MAX_TRACKS; t++)
    for (let p = 0; p < TRACK_MAX_PATTERNS; p++) {
      d.patterns[t][p].length = s.patterns[t][p].length;
      d.patterns[t][p].chunk = s.patterns[t][p].chunk.slice();
    }
  for (let k = 0; k < POOL_CHUNKS; k++)
    for (let i = 0; i < CHUNK_STEPS; i++) copyStep(d.pool[k][i], s.pool[k][i]);
  d.poolUsed = s.poolUsed.slice();
  return d;
}

// ------------------------------------------------------------- the pool ---

const EMPTY_STEP = emptyStep();
const addrOk = (t, p, r) => t >= 0 && t < SONG_MAX_TRACKS && p >= 0 &&
  p < TRACK_MAX_PATTERNS && r >= 0 && r < PATTERN_MAX_STEPS;

export function songStep(s, track, pat, row) {
  if (!addrOk(track, pat, row)) return EMPTY_STEP;
  const c = s.patterns[track][pat].chunk[(row / CHUNK_STEPS) | 0];
  return c === CHUNK_NONE ? EMPTY_STEP : s.pool[c][row % CHUNK_STEPS];
}

// Takes a chunk from the pool the first time; null when the pool is empty.
export function songStepW(s, track, pat, row) {
  if (!addrOk(track, pat, row)) return null;
  const slot = (row / CHUNK_STEPS) | 0;
  const pt = s.patterns[track][pat];
  if (pt.chunk[slot] === CHUNK_NONE) {
    let k = 0;
    while (k < POOL_CHUNKS && (s.poolUsed[k >> 3] >> (k % 8)) & 1) k++;
    if (k === POOL_CHUNKS) return null;
    s.poolUsed[k >> 3] |= 1 << (k % 8);
    for (let i = 0; i < CHUNK_STEPS; i++) copyStep(s.pool[k][i], EMPTY_STEP);
    pt.chunk[slot] = k;
  }
  return s.pool[pt.chunk[slot]][row % CHUNK_STEPS];
}

function chunkRelease(s, pt, slot) {
  const c = pt.chunk[slot];
  if (c === CHUNK_NONE) return;
  s.poolUsed[c >> 3] &= ~(1 << (c % 8)) & 0xFF;
  pt.chunk[slot] = CHUNK_NONE;
}

export function songPatternClear(s, track, pat) {
  if (!addrOk(track, pat, 0)) return;
  const pt = s.patterns[track][pat];
  for (let c = 0; c < PATTERN_CHUNKS; c++) chunkRelease(s, pt, c);
  pt.length = PATTERN_DEF_STEPS;
}

// Blank as moondaw means it: nothing written within the block's length.
export function songPatternBlank(s, track, pat) {
  const pt = s.patterns[track][pat];
  for (let r = 0; r < pt.length; r++) if (!stepIsEmpty(songStep(s, track, pat, r))) return false;
  return true;
}

export function songPoolFree(s) {
  let n = 0;
  for (let k = 0; k < POOL_CHUNKS; k++) n += ((s.poolUsed[k >> 3] >> (k % 8)) & 1) ? 0 : 1;
  return n;
}

// --------------------------------------------------------- the syllables ---

function sylNamed(s, idx) {
  for (let k = 0; k < POOL_CHUNKS; k++) {
    if (!((s.poolUsed[k >> 3] >> (k % 8)) & 1)) continue;
    for (let i = 0; i < CHUNK_STEPS; i++)
      for (let f = 0; f < STEP_MAX_FX; f++)
        if (s.pool[k][i].fx[f].cmd === FX_SYL && s.pool[k][i].fx[f].val === idx) return true;
  }
  return false;
}

// The index, or -1. Text past 5 characters is cut, as moondaw cuts.
export function songSylIntern(s, text) {
  let freeAt = -1;
  if (!text) return -1;
  for (let i = 0; i < SONG_SYL_MAX; i++) {
    const have = s.syl[i];
    let k = 0;
    while (k < SONG_SYL_LEN - 1 && k < text.length && have[k] === text[k]) k++;
    if (have && (k === SONG_SYL_LEN - 1 || (k >= text.length && k >= have.length))) return i;
    if (freeAt < 0 && !have) freeAt = i;
  }
  for (let i = 0; i < SONG_SYL_MAX && freeAt < 0; i++)
    if (!sylNamed(s, i)) { s.syl[i] = ""; freeAt = i; }
  if (freeAt < 0) return -1;
  s.syl[freeAt] = text.slice(0, SONG_SYL_LEN - 1);
  return freeAt;
}

export const songSyl = (s, idx) => (idx < SONG_SYL_MAX ? s.syl[idx] : "");

// ----------------------------------------------------------- the effects ---

const RAMPED = ["VOL", "VEL", "PAN", "REV", "ECH", "FLT", "RES", "CRS", "RAT"].map((n) => FX_NAMES.indexOf(n));
export const fxRampSlot = (cmd) => RAMPED.indexOf(cmd);

// RMP reads the slots to its right, so two of one setter parted by an RMP are
// both heard. Anything else twice on a row is a cell that never plays.
export function fxConflicts(st, slot, cmd) {
  if (cmd === FX_NONE || slot < 0 || slot >= STEP_MAX_FX) return false;
  const partable = cmd === FX_RMP || fxRampSlot(cmd) >= 0;
  for (let i = 0; i < STEP_MAX_FX; i++) {
    if (i === slot || st.fx[i].cmd !== cmd) continue;
    let parted = false;
    const lo = i < slot ? i : slot, hi = i < slot ? slot : i;
    for (let k = lo + 1; partable && k < hi; k++) {
      const c = st.fx[k].cmd;
      if (cmd === FX_RMP ? fxRampSlot(c) >= 0 : c === FX_RMP) parted = true;
    }
    if (!parted) return true;
  }
  return false;
}

export function fxFromName(s, i = 0) {
  for (let c = 1; c < FX_COUNT; c++)
    if (s[i] === FX_NAMES[c][0] && s[i + 1] === FX_NAMES[c][1] && s[i + 2] === FX_NAMES[c][2]) return c;
  return FX_NONE;
}

// -------------------------------------------------------------- the names ---

const NOTE_LETTERS = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"];

export function noteName(note) {
  let oct = ((note / 12) | 0) - 1;
  oct = oct < 0 ? 0 : oct > 9 ? 9 : oct;
  return NOTE_LETTERS[note % 12] + oct;
}

export function noteFromName(s, i = 0) {
  const SEMI = [9, 11, 0, 2, 4, 5, 7]; // A..G
  if (s[i] === "O" && s[i + 1] === "F" && s[i + 2] === "F") return NOTE_OFF;
  let code = s.charCodeAt(i);
  if (code >= 97 && code <= 103) code -= 32;
  if (!(code >= 65 && code <= 71)) return -1;
  let st = SEMI[code - 65];
  if (s[i + 1] === "#") st++;
  else if (s[i + 1] !== "-") return -1;
  const d = s.charCodeAt(i + 2);
  if (!(d >= 48 && d <= 57)) return -1;
  const n = (d - 48 + 1) * 12 + st;
  return n < NOTE_MIN || n > NOTE_MAX ? -1 : n;
}

// str_word: the name, then the end of the string or a blank.
export function engineFromName(s, i = 0) {
  for (let e = 0; e < ENGINE_COUNT; e++) {
    const w = ENGINE_NAMES[e];
    if (!s.startsWith(w, i)) continue;
    const c = s[i + w.length];
    if (c === undefined || c === " " || c === "\t") return e;
  }
  return -1;
}

// ------------------------------------------------------------- the reader ---

const isWs = (c) => c === " " || c === "\t";
const skipWs = (s, i) => { while (isWs(s[i])) i++; return i; };

function hexval(c) {
  if (c === undefined) return -1;
  const n = c.charCodeAt(0);
  if (n >= 48 && n <= 57) return n - 48;
  if (n >= 97 && n <= 102) return n - 87;
  if (n >= 65 && n <= 70) return n - 55;
  return -1;
}

function hex2(s, i = 0) {
  const hi = hexval(s[i]), lo = hi < 0 ? -1 : hexval(s[i + 1]);
  return lo < 0 ? -1 : hi * 16 + lo;
}

// Both leave `i` after the number and report false without moving it when
// there is none -- what a %d and a %x that failed do. The cap on the running
// total is the C's: it saturates rather than overflowing.
function rdInt(s, i) {
  let q = skipWs(s, i);
  const neg = s[q] === "-";
  if (s[q] === "-" || s[q] === "+") q++;
  let c = s.charCodeAt(q);
  if (!(c >= 48 && c <= 57)) return { ok: false, v: 0, i };
  let n = 0;
  for (; ; q++) {
    c = s.charCodeAt(q);
    if (!(c >= 48 && c <= 57)) break;
    if (n < 100000000) n = n * 10 + (c - 48);
  }
  return { ok: true, v: neg ? -n : n, i: q };
}

function rdHex(s, i) {
  let q = skipWs(s, i);
  if (hexval(s[q]) < 0) return { ok: false, v: 0, i };
  let n = 0;
  for (; hexval(s[q]) >= 0; q++) if (n < 0x1000000) n = n * 16 + hexval(s[q]);
  return { ok: true, v: n, i: q };
}

// "<kw><int>" anywhere in the line.
function field(t, kw, from = 0) {
  const p = t.indexOf(kw, from);
  if (p < 0) return { ok: false, v: 0 };
  return rdInt(t, p + kw.length);
}

const copyName = (src, cap) => {
  let i = 0;
  while (i < src.length && i < cap - 1) i++;
  return src.slice(0, i);
};

// Cuts the comment and the blanks at both ends. A '#' opens a comment at the
// start of the line or after a blank; inside a token it is an accidental.
export function cleanLine(line) {
  for (let c = 0; c < line.length; c++)
    if (line[c] === "#" && (c === 0 || isWs(line[c - 1]))) { line = line.slice(0, c); break; }
  let t = 0;
  while (isWs(line[t])) t++;
  let end = line.length;
  while (end > t && (isWs(line[end - 1]) || line[end - 1] === "\r" || line[end - 1] === "\n")) end--;
  return line.slice(t, end);
}

function parseInst(t, inn, rep) {
  inn.engine = ENGINE_PULSE;
  inn.cutoff = FILTER_OPEN;
  inn.reso = 0;
  inn.filtEnv = FILTER_ENV_NONE;
  inn.filtVel = FILTER_ENV_NONE;
  inn.gain = 0;
  inn.vibPhase = 0;
  inn.tract = 0;
  inn.accent = VOX_ACCENT_FR;
  inn.drum = [0, 0, 0, 0];
  inn.nlayers = 0;
  inn.layers = [];

  let p = t.indexOf("engine ");
  if (p >= 0) {
    const e = engineFromName(t, p + 7);
    inn.engine = u8(e < 0 ? ENGINE_PULSE : e);
  }
  p = t.indexOf("timbre ");
  if (p < 0) p = t.indexOf("duty ");
  if (p >= 0) {
    let q = p;
    while (q < t.length && !isWs(t[q])) q++;
    const r = rdInt(t, q);
    if (r.ok) inn.timbre = u8(r.v < 0 ? 0 : r.v);
  }
  if (inn.timbre > engineTimbreMax(inn.engine)) inn.timbre = engineTimbreMax(inn.engine);
  p = t.indexOf("adsr ");
  if (p >= 0) {
    const e = [];
    let q = p + 5, r;
    for (let k = 0; k < 4; k++) {
      r = rdInt(t, q);
      if (!r.ok) break;
      e.push(r.v);
      q = r.i;
    }
    if (e.length === 4) {
      inn.attack = u8(e[0]);
      inn.decay = u8(e[1]);
      inn.sustain = u8(e[2]);
      inn.release = u8(e[3]);
    }
  }
  p = t.indexOf("filter ");
  if (p >= 0) {
    const f = [];
    let q = p + 7;
    while (f.length < 4) {
      const r = rdInt(t, q);
      if (!r.ok) break;
      f.push(r.v);
      q = r.i;
    }
    if (f.length >= 3) {
      inn.cutoff = u8(f[0]);
      inn.reso = u8(f[1]);
      inn.filtEnv = u8(f[2] > 127 ? 127 : f[2] & 0x7F);
      if (f.length >= 4) inn.filtVel = u8(f[3] > 127 ? 127 : f[3] & 0x7F);
    }
  }
  let r = field(t, "gain ");
  if (r.ok) inn.gain = i8(clampi(r.v, GAIN_MIN_DB, GAIN_MAX_DB));
  r = field(t, "level ");
  if (r.ok) inn.level = i8(clampi(r.v, -64, 24));
  p = t.indexOf("vibphase ");
  if (p >= 0) {
    if (t.startsWith("free", p + 9)) inn.vibPhase = VIB_PHASE_FREE;
    else {
      r = field(t, "vibphase ", p);
      if (r.ok) inn.vibPhase = i16(clampi(r.v, 0, 255));
    }
  }
  r = field(t, "tract ");
  if (r.ok) inn.tract = u8(clampi(r.v, VOX_TRACT_MIN, VOX_TRACT_MAX));
  p = t.indexOf("accent ");
  if (p >= 0) inn.accent = t.startsWith("EN", p + 7) ? VOX_ACCENT_EN : VOX_ACCENT_FR;
  p = t.indexOf("drum ");
  if (p >= 0) {
    // TUNE TONE DECAY SNAP, 0..127, 64 as designed
    let q = p + 5;
    for (let k = 0; k < 4; k++) {
      const d = rdInt(t, q);
      if (!d.ok) break;
      inn.drum[k] = i8(clampi(d.v, 0, 127) - 64);
      q = d.i;
    }
  }
  p = t.indexOf("name ");
  if (p >= 0) inn.name = copyName(t.slice(p + 5), INSTR_NAME_LEN);

  if (rep) {
    rep.silentInstr += inn.engine === ENGINE_PLUCK ? 1 : 0;
    rep.shaping += (t.includes("drive ") || t.includes("amp ") || t.includes("tone ")) ? 1 : 0;
  }
}

function parseLayer(rest) {
  const p = rest.indexOf("engine ");
  if (p < 0) return null;
  const e = engineFromName(rest, p + 7);
  if (e < 0 || e === ENGINE_SAMPLE) return null;
  const l = { engine: u8(e), timbre: 0, transpose: 0, detune: 0, gain: 0 };
  let r = field(rest, "timbre ");
  if (r.ok) l.timbre = u8(r.v < 0 ? 0 : r.v);
  if (l.timbre > engineTimbreMax(l.engine)) l.timbre = engineTimbreMax(l.engine);
  r = field(rest, "transpose ");
  if (r.ok) l.transpose = i8(clampi(r.v, -48, 48));
  r = field(rest, "detune ");
  if (r.ok) l.detune = i8(clampi(r.v, -100, 100));
  r = field(rest, "gain ");
  if (r.ok) l.gain = i8(clampi(r.v, GAIN_MIN_DB, GAIN_MAX_DB));
  return l;
}

// `smp NN name [begin N] [loop A B [ping]]`, past the index. Absent is zero,
// as moondaw reads it: a slot taken again starts clean.
function parseSample(t, from, e) {
  let p = skipWs(t, from);
  let i = 0;
  while (t[p + i] !== undefined && !isWs(t[p + i]) && i < SONG_SMP_NAME - 1) i++;
  e.name = t.slice(p, p + i);
  e.begin = 0;
  e.loopStart = 0;
  e.loopEnd = 0;
  e.loop = SMP_LOOP_OFF;
  const rest = t.slice(p + i);
  let q = rest.indexOf("begin ");
  if (q >= 0) {
    const a = rdInt(rest, q + 6);
    if (a.ok && a.v >= 0) e.begin = a.v >>> 0;
  }
  q = rest.indexOf("loop ");
  if (q >= 0) {
    const a = rdInt(rest, q + 5);
    const b = a.ok ? rdInt(rest, a.i) : { ok: false };
    if (a.ok && b.ok && a.v >= 0 && b.v > a.v) {
      e.loopStart = a.v >>> 0;
      e.loopEnd = b.v >>> 0;
      e.loop = rest.indexOf("ping", b.i) >= 0 ? SMP_LOOP_PING : SMP_LOOP_FWD;
    }
  }
}

// `lo hi root R smp NN [tune T] [gain G]`, past the instrument. A zone that
// does not read whole is dropped whole: half a keymap entry could take notes
// it was never given.
function parseZone(t, from) {
  let p = skipWs(t, from);
  const lo = noteFromName(t, p);
  p = skipWs(t, p + 3);
  const hi = noteFromName(t, p);
  p = skipWs(t, p + 3);
  if (lo < 0 || lo === NOTE_OFF || hi < 0 || hi === NOTE_OFF || lo > hi || !t.startsWith("root ", p))
    return null;
  p = skipWs(t, p + 5);
  const rt = noteFromName(t, p);
  p = skipWs(t, p + 3);
  if (rt < 0 || rt === NOTE_OFF || !t.startsWith("smp ", p)) return null;
  const si = rdHex(t, p + 4);
  if (!si.ok || si.v >= SONG_SMP_MAX) return null;
  const z = { inst: INSTR_NONE, lo, hi, root: rt, smp: u8(si.v), tune: 0, gain: 0 };
  const rest = t.slice(si.i);
  let r = field(rest, "tune ");
  if (r.ok) z.tune = i8(clampi(r.v, -ZONE_TUNE_MAX, ZONE_TUNE_MAX));
  r = field(rest, "gain ");
  if (r.ok) z.gain = i8(clampi(r.v, GAIN_MIN_DB, GAIN_MAX_DB));
  return z;
}

// One step line into `st`; returns its row, or -1. The row is the whole truth
// of its line: what the line does not say is empty.
function parseStep(line, st, song, rep) {
  let len = 0;
  while (line[len] !== undefined && !isWs(line[len])) len++;
  const row = len === 1 ? hexval(line[0]) : hex2(line);
  if (row < 0 || row >= PATTERN_MAX_STEPS) return -1;
  copyStep(st, EMPTY_STEP);

  let nfx = 0;
  for (let tok = len; ;) {
    tok = skipWs(line, tok);
    if (line[tok] === undefined) break;
    let n = 0;
    while (line[tok + n] !== undefined && !isWs(line[tok + n])) n++;
    const end = tok + n;
    if (line[tok] === "i" && n >= 3) {
      const v = hex2(line, tok + 1);
      if (v >= 0 && v < SONG_MAX_INSTR) st.instrument = u8(v);
    } else if (line[tok] === "'") {
      // 'L2: the text goes into the song's table, the slot holds its index.
      const syl = line.slice(tok + 1, tok + Math.min(n, SONG_SYL_LEN));
      const idx = songSylIntern(song, syl);
      if (idx < 0 && syl && rep) rep.syllables++;
      if (idx >= 0 && nfx < STEP_MAX_FX && !fxConflicts(st, nfx, FX_SYL)) {
        st.fx[nfx].cmd = FX_SYL;
        st.fx[nfx].val = u8(idx);
        nfx++;
      }
    } else if (line[tok] === "f" && n >= 6) {
      let cmd = fxFromName(line, tok + 1);
      let val = hex2(line, tok + 4);
      if (cmd === FX_SYL) cmd = FX_NONE;
      if (cmd === FX_SMP && val >= SONG_SMP_MAX) val = SONG_SMP_MAX - 1;
      if (cmd !== FX_NONE && val >= 0 && nfx < STEP_MAX_FX && !fxConflicts(st, nfx, cmd)) {
        st.fx[nfx].cmd = cmd;
        st.fx[nfx].val = u8(val);
        nfx++;
      }
    } else {
      let q = tok, ni = 0;
      while (ni < STEP_MAX_NOTES && end - q >= 3) {
        const nt = noteFromName(line, q);
        if (nt < 0) break;
        st.notes[ni++] = u8(nt);
        q += 3;
        if (line[q] !== "+") break;
        q++;
      }
    }
    tok = end;
  }
  return row;
}

function entryToPhrase(e) {
  if (e.kind === ENTRY_REF) {
    e.kind = ENTRY_PHRASE;
    e.refKind = REF_PHRASE;
    e.refTarget = 0;
  }
}

// Renumbers the sections in the order they appear and drops the ones no phrase
// carries. As moondaw's song_sections_normalize.
export function songSectionsNormalize(s) {
  const map = new Array(SONG_MAX_SECTIONS).fill(SECTION_NONE);
  const kept = new Array(SONG_MAX_SECTIONS).fill("");
  let n = 0, open = SECTION_NONE;

  for (let i = 0; i < s.arrangeLen; i++) {
    const e = s.arrangement[i];
    const old = e.section;
    if (old >= s.sectionCount || (old !== open && map[old] !== SECTION_NONE)) {
      e.section = SECTION_NONE;
      open = SECTION_NONE;
      continue;
    }
    if (map[old] === SECTION_NONE) {
      map[old] = n;
      kept[n] = s.sections[old];
      n++;
    }
    e.section = map[old];
    open = old;
  }
  s.sections = kept;
  s.sectionCount = n;
  // The references, second: a section target moves with the renumbering, and
  // one whose target is gone -- or is itself a reference, a chain -- becomes an
  // empty phrase rather than a dangling number.
  const wasRef = [];
  for (let i = 0; i < s.arrangeLen; i++) wasRef[i] = s.arrangement[i].kind === ENTRY_REF;
  for (let i = 0; i < s.arrangeLen; i++) {
    const e = s.arrangement[i];
    if (!wasRef[i]) continue;
    let dead;
    if (e.refKind === REF_SECTION) {
      const t = e.refTarget;
      dead = t >= SONG_MAX_SECTIONS || map[t] === SECTION_NONE;
      if (!dead) e.refTarget = map[t];
    } else {
      dead = e.refTarget >= s.arrangeLen || e.refTarget === i || wasRef[e.refTarget];
    }
    if (dead) {
      e.kind = ENTRY_PHRASE;
      e.refKind = REF_PHRASE;
      e.refTarget = 0;
      e.steps = 0;
      for (let t = 0; t < SONG_MAX_TRACKS; t++) {
        e.pattern[t] = PATTERN_NONE;
        e.level[t] = VOL_FULL;
      }
    }
  }
}

export function newReport() {
  return {
    tracksInFile: SONG_MAX_TRACKS, silentInstr: 0, samples: 0, zones: 0, refLines: 0,
    previews: 0, shaping: 0, syllables: 0, smpFx: 0, stepsCut: 0, stepsLost: 0, chunksUsed: 0,
  };
}

const CTX_NONE = 0, CTX_PAT = 1, CTX_CUT = 2, CTX_ARR = 3, CTX_SEC = 4;

// Loads `text`. -> {err, song, report}; song is null on error, as trk_load
// leaves *s untouched.
export function trkLoad(text, rep = newReport()) {
  const tmp = songInit();
  tmp.arrangeLen = 0;
  const ld = {
    ctx: CTX_NONE, err: 0, versionOk: false,
    cur: -1, curT: 0, curP: 0, secNamed: -1, curSec: SECTION_NONE,
  };
  const st = emptyStep();

  for (const raw of text.split("\n")) {
    const t = cleanLine(raw);
    if (!t || ld.err) continue;
    let r, i1, i2;

    if (t.startsWith("version ") && (r = rdInt(t, 8)).ok) {
      if (r.v !== 3) { ld.err = TRK_ERR_VERSION; continue; }
      ld.versionOk = true;
    } else if (t.startsWith("patch ")) {
      ld.err = TRK_ERR_PATCH;
    } else if (t.startsWith("bpm ") && (r = field(t, "bpm ")).ok) {
      tmp.bpm = clampi(r.v, BPM_MIN, BPM_MAX);
    } else if (t.startsWith("ticks ") && (r = field(t, "ticks ")).ok) {
      tmp.ticksPerStep = clampi(r.v, 1, TICKS_MAX);
    } else if (t.startsWith("meter ") && (i1 = rdInt(t, 6)).ok && (i2 = rdInt(t, i1.i)).ok) {
      if (i1.v >= 1 && i1.v <= METER_MAX) tmp.stepsPerBeat = i1.v;
      if (i2.v >= 1 && i2.v <= METER_MAX) tmp.beatsPerBar = i2.v;
    } else if (t.startsWith("tracks ") && (r = field(t, "tracks ")).ok) {
      if (r.v >= 1 && r.v <= 16) {
        rep.tracksInFile = r.v;
        tmp.numTracks = r.v > SONG_MAX_TRACKS ? SONG_MAX_TRACKS : r.v;
      }
    } else if (t.startsWith("swing ") && (r = field(t, "swing ")).ok) {
      tmp.swing = clampi(r.v, SWING_MIN, SWING_MAX);
    } else if (t.startsWith("delay ") && (i1 = rdInt(t, 6)).ok && (i2 = rdInt(t, i1.i)).ok) {
      tmp.delayTime = clampi(i1.v, 0, 96);
      tmp.delayFb = clampi(i2.v, 0, 127);
    } else if (t.startsWith("reverb ") && (i1 = rdInt(t, 7)).ok && (i2 = rdInt(t, i1.i)).ok) {
      tmp.reverbSize = clampi(i1.v, 0, 127);
      tmp.reverbDamp = clampi(i2.v, 0, 127);
    } else if (t.startsWith("refbpm ") || t.startsWith("refstart ") ||
               t.startsWith("refbar ") || t.startsWith("ref ")) {
      rep.refLines++;
    } else if (t.startsWith("inst ") && (r = rdHex(t, 5)).ok) {
      if (r.v >= 0 && r.v < SONG_MAX_INSTR) parseInst(t, tmp.instruments[r.v], rep);
      ld.ctx = CTX_NONE;
    } else if (t.startsWith("smp ") && (r = rdHex(t, 4)).ok) {
      if (r.v >= 0 && r.v < SONG_SMP_MAX && isWs(t[r.i])) parseSample(t, r.i, tmp.smp[r.v]);
      else rep.samples++;
      ld.ctx = CTX_NONE;
    } else if (t.startsWith("lay ") && (r = rdHex(t, 4)).ok) {
      if (t[r.i] !== undefined && r.v >= 0 && r.v < SONG_MAX_INSTR) {
        const ly = parseLayer(t.slice(r.i));
        const inn = tmp.instruments[r.v];
        if (ly && inn.nlayers < INSTR_MAX_LAYERS) { inn.layers[inn.nlayers++] = ly; }
      }
      ld.ctx = CTX_NONE;
    } else if (t.startsWith("preview ")) {
      rep.previews++;
      ld.ctx = CTX_NONE;
    } else if (t.startsWith("zone ") && (r = rdHex(t, 5)).ok) {
      const z = r.v >= 0 && r.v < SONG_MAX_INSTR && tmp.nzones < SONG_ZONE_MAX
        ? parseZone(t, r.i) : null;
      if (z) {
        z.inst = u8(r.v);
        Object.assign(tmp.zones[tmp.nzones++], z);
      } else {
        rep.zones++;
      }
      ld.ctx = CTX_NONE;
    } else if (t.startsWith("trk ") && trkLine(t, tmp)) {
      ld.ctx = CTX_NONE;
    } else if (t.startsWith("pat ")) {
      ld.ctx = CTX_NONE;
      if (t.startsWith("t", 4) && (i1 = rdInt(t, 5)).ok && (i2 = rdHex(t, i1.i)).ok) {
        const len = field(t, " len ");
        if (len.ok && len.v >= 1 && len.v <= PATTERN_MAX_STEPS &&
            i1.v >= 0 && i2.v >= 0 && i2.v < TRACK_MAX_PATTERNS) {
          if (i1.v >= SONG_MAX_TRACKS) {
            ld.ctx = i1.v < 16 ? CTX_CUT : CTX_NONE;
          } else {
            ld.curT = i1.v;
            ld.curP = i2.v;
            songPatternClear(tmp, ld.curT, ld.curP);
            tmp.patterns[ld.curT][ld.curP].length = len.v;
            ld.ctx = CTX_PAT;
          }
        }
      }
    } else if (t.startsWith("sec") && isWs(t[3])) {
      let p = skipWs(t, 3);
      ld.secNamed = -1;
      ld.ctx = CTX_NONE;
      if (t.startsWith("---", p)) {
        ld.curSec = SECTION_NONE;
      } else if ((r = rdHex(t, p)).ok && r.v >= 0 && r.v < SONG_MAX_SECTIONS) {
        ld.secNamed = r.v;
        ld.curSec = r.v;
        if (r.v >= tmp.sectionCount) tmp.sectionCount = r.v + 1;
        ld.ctx = CTX_SEC;
      }
    } else if (ld.ctx === CTX_SEC && t.startsWith("name ")) {
      let p = skipWs(t, 5);
      let txt = t.slice(p);
      if (txt === "---") txt = "";
      if (ld.secNamed >= 0) tmp.sections[ld.secNamed] = copyName(txt, SONG_NAME_LEN + 1);
    } else if (t.startsWith("arr ") && (r = rdHex(t, 4)).ok &&
               (() => {
                 let p = skipWs(t, r.i);
                 if (!t.startsWith("ref ", p)) return false;
                 p = skipWs(t, p + 4);
                 if (!((t.startsWith("sec", p) || t.startsWith("arr", p)) && isWs(t[p + 3]))) return false;
                 ld.refAt = p;
                 return true;
               })()) {
      const toSec = t[ld.refAt] === "s";
      i2 = rdHex(t, ld.refAt + 3);
      i1 = r.v;
      if (i2.ok && i1 >= 0 && i1 < SONG_ARRANGE_LEN && i2.v >= 0 && i2.v < SONG_ARRANGE_LEN && i2.v !== i1) {
        const e = tmp.arrangement[i1];
        for (let k = 0; k < SONG_MAX_TRACKS; k++) {
          e.pattern[k] = PATTERN_NONE;
          e.level[k] = VOL_FULL;
        }
        e.name = "";
        e.kind = ENTRY_REF;
        e.refKind = toSec ? REF_SECTION : REF_PHRASE;
        e.refTarget = u8(i2.v);
        const p = skipWs(t, i2.i);
        e.steps = 0;
        if (!toSec && t.startsWith("steps ", p)) {
          const v = rdInt(t, p + 6);
          if (v.ok && v.v >= 0 && v.v <= ENTRY_CYCLE_MAX) e.steps = v.v;
        }
        e.section = ld.curSec;
        if (i1 >= tmp.arrangeLen) tmp.arrangeLen = i1 + 1;
      }
      ld.ctx = CTX_NONE;
    } else if (t.startsWith("arr ") && (r = rdHex(t, 4)).ok) {
      ld.ctx = CTX_NONE;
      const p = skipWs(t, r.i);
      if (r.v >= 0 && r.v < SONG_ARRANGE_LEN && t.startsWith("steps ", p)) {
        const e = tmp.arrangement[r.v];
        const q = skipWs(t, p + 6);
        ld.cur = r.v;
        ld.ctx = CTX_ARR;
        entryToPhrase(e);
        for (let k = 0; k < SONG_MAX_TRACKS; k++) e.pattern[k] = PATTERN_NONE;
        let v = 0;
        if (!t.startsWith("auto", q)) {
          const n = rdInt(t, q);
          if (n.ok) v = n.v;
        }
        e.steps = clampi(v, 0, 0xFFFF);
        e.section = ld.curSec;
        if (ld.cur >= tmp.arrangeLen) tmp.arrangeLen = ld.cur + 1;
      }
    } else if (ld.ctx === CTX_ARR && t.startsWith("name ")) {
      let txt = t.slice(skipWs(t, 5));
      if (txt === "---") txt = "";
      tmp.arrangement[ld.cur].name = copyName(txt, SONG_NAME_LEN + 1);
    } else if (ld.ctx === CTX_ARR && t[0] === "t" && (i1 = rdInt(t, 1)).ok) {
      const e = tmp.arrangement[ld.cur];
      const q = skipWs(t, i1.i);
      const on = t.startsWith("on", q) && (t[q + 2] === undefined || isWs(t[q + 2]));
      let pi;
      if (i1.v < 0 || i1.v >= SONG_MAX_TRACKS) {
        /* a track this machine does not have */
      } else if (!on && (pi = rdHex(t, i1.i)).ok) {
        if (pi.v < TRACK_MAX_PATTERNS) {
          entryToPhrase(e);
          e.pattern[i1.v] = u8(pi.v);
          const vol = field(t, "vol ", pi.i);
          if (vol.ok) e.level[i1.v] = clampi(vol.v, 0, VOL_FULL);
        }
      } else if (on) {
        entryToPhrase(e);
        e.pattern[i1.v] = PATTERN_HOLD;
        const vol = field(t, "vol ", q);
        if (vol.ok) e.level[i1.v] = clampi(vol.v, 0, VOL_FULL);
      } else if (t.includes("---")) {
        e.pattern[i1.v] = PATTERN_NONE;
        e.level[i1.v] = VOL_FULL;
      }
    } else if (ld.ctx === CTX_CUT && hexval(t[0]) >= 0) {
      rep.stepsCut++;
    } else if (ld.ctx === CTX_PAT && hexval(t[0]) >= 0) {
      const row = parseStep(t, st, tmp, rep);
      if (row >= 0 && !stepIsEmpty(st)) {
        const w = songStepW(tmp, ld.curT, ld.curP, row);
        if (w) copyStep(w, st);
        else rep.stepsLost++;
      }
    }
  }

  if (ld.err) return { err: ld.err, song: null, report: rep };
  if (!ld.versionOk) return { err: TRK_ERR_VERSION, song: null, report: rep };
  if (tmp.arrangeLen < 1) {
    tmp.arrangeLen = 1;
    tmp.arrangement[0].steps = 0;
  }
  // A section reference is a section of its own: one row, the pointer.
  for (let i = 0; i < tmp.arrangeLen; i++) {
    const e = tmp.arrangement[i];
    if (e.kind !== ENTRY_REF || e.refKind !== REF_SECTION) continue;
    if (e.section === SECTION_NONE) {
      if (tmp.sectionCount < SONG_MAX_SECTIONS) e.section = tmp.sectionCount++;
      continue;
    }
    for (let j = 0; j < tmp.arrangeLen; j++)
      if (j !== i && tmp.arrangement[j].section === e.section) tmp.arrangement[j].section = SECTION_NONE;
  }
  songSectionsNormalize(tmp);
  rep.chunksUsed = POOL_CHUNKS - songPoolFree(tmp);
  return { err: TRK_OK, song: tmp, report: rep };
}

// `trk N vol N pan N [rev N] [ech N]`: true when the line was one.
function trkLine(t, tmp) {
  const i1 = rdInt(t, 4);
  if (!i1.ok) return false;
  let p = skipWs(t, i1.i);
  if (!t.startsWith("vol ", p)) return false;
  const i2 = rdInt(t, p + 4);
  if (!i2.ok) return false;
  p = skipWs(t, i2.i);
  if (!t.startsWith("pan ", p)) return false;
  const v = rdInt(t, p + 4);
  if (!v.ok) return false;
  const rev = field(t, "rev "), ech = field(t, "ech ");
  if (i1.v >= 0 && i1.v < SONG_MAX_TRACKS) {
    const ti = tmp.trackInit[i1.v];
    ti.volume = i2.v & 0x7F;
    ti.pan = i8(clampi(v.v, -64, 63));
    ti.send = clampi(rev.ok ? rev.v : 0, 0, 127);
    ti.echo = clampi(ech.ok ? ech.v : 0, 0, 127);
  }
  return true;
}

// ------------------------------------------------------------- the writer ---

const hex2of = (v) => "0123456789ABCDEF"[(v >> 4) & 15] + "0123456789ABCDEF"[v & 15];

function patternUsed(s, track, idx) {
  if (track >= s.numTracks) return false;
  for (let i = 0; i < s.arrangeLen; i++) if (s.arrangement[i].pattern[track] === idx) return true;
  return false;
}

// A block written into is kept even when no phrase plays it.
const patternKept = (s, t, p) => patternUsed(s, t, p) || !songPatternBlank(s, t, p);

// Which instruments a step names: the writer puts down no others, and neither
// does the FM-1's. The page shows this, so an edit cannot vanish unnoticed.
export function instrumentsUsed(s) {
  const used = new Array(SONG_MAX_INSTR).fill(false);
  for (let t = 0; t < SONG_MAX_TRACKS; t++)
    for (let p = 0; p < TRACK_MAX_PATTERNS; p++) {
      if (!patternKept(s, t, p)) continue;
      for (let r = 0; r < s.patterns[t][p].length; r++) {
        const ins = songStep(s, t, p, r).instrument;
        if (ins < SONG_MAX_INSTR) used[ins] = true;
      }
    }
  return used;
}

// The entries something plays, as moondaw writes them: named by a zone of a
// SMPL instrument the song uses, or by an fSMP.
export function samplesUsed(s, instUsed = instrumentsUsed(s)) {
  const used = new Array(SONG_SMP_MAX).fill(false);
  for (let z = 0; z < s.nzones && z < SONG_ZONE_MAX; z++) {
    const zn = s.zones[z];
    if (zn.inst < SONG_MAX_INSTR && instUsed[zn.inst] &&
        s.instruments[zn.inst].engine === ENGINE_SAMPLE && zn.smp < SONG_SMP_MAX)
      used[zn.smp] = true;
  }
  for (let t = 0; t < SONG_MAX_TRACKS; t++)
    for (let p = 0; p < TRACK_MAX_PATTERNS; p++) {
      if (!patternKept(s, t, p)) continue;
      for (let r = 0; r < s.patterns[t][p].length; r++) {
        const st = songStep(s, t, p, r);
        for (let k = 0; k < STEP_MAX_FX; k++)
          if (st.fx[k].cmd === FX_SMP && st.fx[k].val < SONG_SMP_MAX) used[st.fx[k].val] = true;
      }
    }
  return used;
}

function writeStep(w, s, row, st) {
  let written = 0;
  w.push(hex2of(row), " ");
  if (st.notes[0] === NOTE_OFF) {
    w.push("OFF");
  } else {
    for (let i = 0; i < STEP_MAX_NOTES; i++) {
      if (st.notes[i] > NOTE_MAX) continue;
      if (written++) w.push("+");
      w.push(noteName(st.notes[i]));
    }
    if (!written) w.push("---");
  }
  if (st.instrument !== INSTR_NONE) w.push(" i", hex2of(st.instrument));
  for (let i = 0; i < STEP_MAX_FX; i++) {
    if (st.fx[i].cmd === FX_SYL) {
      // In clear, quoted: lyrics are meant to be read, and the index means
      // nothing outside this table.
      const syl = songSyl(s, st.fx[i].val);
      if (syl) w.push(" '", syl);
      continue;
    }
    if (st.fx[i].cmd === FX_NONE || st.fx[i].cmd >= FX_COUNT) continue;
    w.push(" f", fxName(st.fx[i].cmd), hex2of(st.fx[i].val));
  }
  w.push("\n");
}

function writeInst(w, inn) {
  w.push(" engine ", engineName(inn.engine));
  w.push(" timbre " + inn.timbre);
  w.push(" adsr " + inn.attack, " " + inn.decay, " " + inn.sustain, " " + inn.release);
  if (inn.gain) w.push(" gain " + inn.gain);
  if (inn.level) w.push(" level " + inn.level);
  if (inn.cutoff !== FILTER_OPEN || inn.reso || inn.filtEnv !== FILTER_ENV_NONE ||
      inn.filtVel !== FILTER_ENV_NONE) {
    w.push(" filter " + inn.cutoff, " " + inn.reso, " " + inn.filtEnv);
    if (inn.filtVel !== FILTER_ENV_NONE) w.push(" " + inn.filtVel);
  }
  if (inn.vibPhase === VIB_PHASE_FREE) w.push(" vibphase free");
  else if (inn.vibPhase) w.push(" vibphase " + inn.vibPhase);
  if (inn.tract !== 0 && inn.tract !== 100) w.push(" tract " + inn.tract);
  if (inn.accent === VOX_ACCENT_EN) w.push(" accent EN");
  if (inn.drum[0] || inn.drum[1] || inn.drum[2] || inn.drum[3])
    for (let k = 0; k < 4; k++) w.push((k ? " " : " drum ") + (inn.drum[k] + 64));
  if (inn.name) w.push(" name ", inn.name);
  w.push("\n");
}

function writeZone(w, z) {
  w.push("zone ", hex2of(z.inst), " ", noteName(z.lo), " ", noteName(z.hi));
  w.push(" root ", noteName(z.root), " smp ", hex2of(z.smp));
  if (z.tune) w.push(" tune " + z.tune);
  if (z.gain) w.push(" gain " + z.gain);
  w.push("\n");
}

// The song as the FM-1 would save it: core/trk.c trk_write, in order.
export function trkSave(s) {
  const w = [];
  w.push("# anbernic tracker song\nversion 3\n");
  w.push("bpm " + s.bpm);
  w.push("\nticks " + s.ticksPerStep);
  w.push("\nmeter " + s.stepsPerBeat, " " + s.beatsPerBar);
  w.push("\ntracks " + s.numTracks);
  w.push("\nreverb " + s.reverbSize, " " + s.reverbDamp);
  if (s.swing > SWING_MIN) w.push("\nswing " + s.swing);
  w.push("\ndelay " + s.delayTime, " " + s.delayFb);
  w.push("\n\n");

  const instUsed = instrumentsUsed(s);
  const smpUsed = samplesUsed(s, instUsed);
  let any = false;
  for (let i = 0; i < SONG_SMP_MAX; i++) {
    const e = s.smp[i];
    if (!smpUsed[i] || !e.name) continue;
    w.push("smp ", hex2of(i), " ", e.name);
    if (e.begin) w.push(" begin " + e.begin);
    if (e.loop !== SMP_LOOP_OFF) {
      w.push(" loop " + e.loopStart, " " + e.loopEnd);
      if (e.loop === SMP_LOOP_PING) w.push(" ping");
    }
    w.push("\n");
    any = true;
  }
  if (any) w.push("\n");

  for (let i = 0; i < SONG_MAX_INSTR; i++) {
    const inn = s.instruments[i];
    if (!instUsed[i]) continue;
    w.push("inst ", hex2of(i));
    writeInst(w, inn);
    // The keymap right under its instrument, in priority order; as moondaw,
    // only a SMPL's, and only zones naming a sample.
    for (let z = 0; z < s.nzones && z < SONG_ZONE_MAX && inn.engine === ENGINE_SAMPLE; z++)
      if (s.zones[z].inst === i && s.smp[s.zones[z].smp % SONG_SMP_MAX].name) writeZone(w, s.zones[z]);
    for (let l = 0; l < inn.nlayers && l < INSTR_MAX_LAYERS; l++) {
      const ly = inn.layers[l];
      w.push("lay ", hex2of(i), " engine ", engineName(ly.engine), " timbre " + ly.timbre);
      if (ly.transpose) w.push(" transpose " + ly.transpose);
      if (ly.detune) w.push(" detune " + ly.detune);
      if (ly.gain) w.push(" gain " + ly.gain);
      w.push("\n");
    }
  }
  w.push("\n");

  for (let t = 0; t < s.numTracks; t++) {
    w.push("trk " + t, " vol " + s.trackInit[t].volume, " pan " + s.trackInit[t].pan);
    if (s.trackInit[t].send) w.push(" rev " + s.trackInit[t].send);
    if (s.trackInit[t].echo) w.push(" ech " + s.trackInit[t].echo);
    w.push("\n");
  }
  for (let t = 0; t < SONG_MAX_TRACKS; t++)
    for (let p = 0; p < TRACK_MAX_PATTERNS; p++) {
      if (!patternKept(s, t, p)) continue;
      w.push("\npat t" + t, " ", hex2of(p), " len " + s.patterns[t][p].length, "\n");
      for (let r = 0; r < s.patterns[t][p].length; r++) {
        const st = songStep(s, t, p, r);
        if (!stepIsEmpty(st)) writeStep(w, s, r, st);
      }
    }

  let secOpen = SECTION_NONE;
  for (let i = 0; i < s.arrangeLen; i++) {
    const e = s.arrangement[i];
    const sec = e.section < s.sectionCount ? e.section : SECTION_NONE;
    if (sec !== secOpen) {
      if (sec === SECTION_NONE) {
        w.push("\nsec ---\n");
      } else {
        w.push("\nsec ", hex2of(sec), "\n");
        if (s.sections[sec]) w.push("name ", s.sections[sec], "\n");
      }
      secOpen = sec;
    }
    w.push("\narr ", hex2of(i));
    if (e.kind === ENTRY_REF) {
      w.push(e.refKind === REF_SECTION ? " ref sec " : " ref arr ", hex2of(e.refTarget));
      if (e.refKind === REF_PHRASE && e.steps > 0) w.push(" steps " + e.steps);
      w.push("\n");
      continue;
    }
    if (e.steps === 0) w.push(" steps auto\n");
    else w.push(" steps " + e.steps, "\n");
    if (e.name) w.push("name ", e.name, "\n");
    for (let t = 0; t < s.numTracks; t++) {
      const pi = e.pattern[t];
      if (pi >= TRACK_MAX_PATTERNS && pi !== PATTERN_HOLD) continue;
      w.push("t" + t);
      if (pi === PATTERN_HOLD) w.push(" on");
      else w.push(" ", hex2of(pi));
      if (e.level[t] !== VOL_FULL) w.push(" vol " + e.level[t]);
      w.push("\n");
    }
  }
  return w.join("");
}

// --------------------------------------------------------------- the zones ---

// The zone of instrument `inst` that holds `note`, or null: the first one
// wins, so the list's order is the priority (core/song.c song_zone).
export function songZone(s, inst, note) {
  for (let i = 0; i < s.nzones && i < SONG_ZONE_MAX; i++) {
    const z = s.zones[i];
    if (z.inst === inst && note >= z.lo && note <= z.hi) return z;
  }
  return null;
}

// The indices, in file order, of the zones belonging to `inst`.
export function zonesOf(s, inst) {
  const out = [];
  for (let i = 0; i < s.nzones && i < SONG_ZONE_MAX; i++) if (s.zones[i].inst === inst) out.push(i);
  return out;
}

// Zone `at` moved to index `to` in the whole list: the list is shared by every
// instrument, and priority is its order, so a move inside one instrument moves
// the row through the shared list.
export function zoneMove(s, at, to) {
  if (at === to || at < 0 || to < 0 || at >= s.nzones || to >= s.nzones) return;
  const kept = { ...s.zones[at] };
  const step = at < to ? 1 : -1;
  for (let i = at; i !== to; i += step) Object.assign(s.zones[i], s.zones[i + step]);
  Object.assign(s.zones[to], kept);
}

// A zone added at the end of `inst`'s run (so it takes the lowest priority of
// that instrument), or null when the list is full.
export function zoneAdd(s, inst, fields = {}) {
  if (s.nzones >= SONG_ZONE_MAX) return null;
  const mine = zonesOf(s, inst);
  const at = mine.length ? mine[mine.length - 1] + 1 : s.nzones;
  for (let i = s.nzones; i > at; i--) Object.assign(s.zones[i], s.zones[i - 1]);
  s.nzones++;
  Object.assign(s.zones[at], {
    inst, lo: NOTE_MIN, hi: NOTE_MAX, root: 60, smp: 0, tune: 0, gain: 0, ...fields,
  });
  return at;
}

export function zoneRemove(s, at) {
  if (at < 0 || at >= s.nzones) return;
  for (let i = at; i < s.nzones - 1; i++) Object.assign(s.zones[i], s.zones[i + 1]);
  Object.assign(s.zones[s.nzones - 1], { inst: INSTR_NONE, lo: 0, hi: 0, root: 0, smp: 0, tune: 0, gain: 0 });
  s.nzones--;
}

// Which notes of `inst`'s zone `at` an earlier zone answers first: those will
// never sound. The ruler draws them muted, as moondaw's zones.js does.
export function zoneShadow(s, at) {
  const z = s.zones[at];
  const out = [];
  for (let n = z.lo; n <= z.hi; n++) {
    let first = -1;
    for (let i = 0; i < s.nzones; i++) {
      const o = s.zones[i];
      if (o.inst === z.inst && n >= o.lo && n <= o.hi) { first = i; break; }
    }
    if (first !== at) out.push(n);
  }
  return out;
}

// A free entry of the sample table for `name`, or the one already holding it.
// The table is the song's, shared by every instrument.
export function smpIntern(s, name) {
  if (!name) return -1; // an entry with no name is a free slot, not a sound
  for (let i = 0; i < SONG_SMP_MAX; i++) if (s.smp[i].name === name) return i;
  for (let i = 0; i < SONG_SMP_MAX; i++)
    if (!s.smp[i].name) {
      Object.assign(s.smp[i], { name, begin: 0, loopStart: 0, loopEnd: 0, loop: SMP_LOOP_OFF });
      return i;
    }
  return -1;
}
