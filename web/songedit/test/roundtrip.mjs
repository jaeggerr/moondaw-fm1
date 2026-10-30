// SPDX-License-Identifier: GPL-3.0-only
//
// trk.js against core/trk.c, on every .trk this computer has: the page must
// write what the FM-1 writes, byte for byte, or it damages songs quietly.
// The same idea as tools/installer_compare.py for the installer.
//
//   node web/songedit/test/roundtrip.mjs [FILE.trk...]
//
// Three things are asked of each file:
//
//   1 canonical   trk.js load+save == build/trkdump (core/trk.c load+save)
//   2 stable      loading that output and saving it again gives the same bytes
//   3 lossless    an edited song, written by trk.js, comes back out of
//                 build/trkdump unchanged -- so nothing the page writes is
//                 dropped on the way through the device's own loader
//
// build/trkdump is built here when it is missing or older than the core, as
// tools/fm1_send.py does: a stale one would reduce songs by the old rules.

import { execFileSync } from "node:child_process";
import { readFileSync, statSync, existsSync, mkdirSync, readdirSync } from "node:fs";
import { dirname, join, basename } from "node:path";
import { fileURLToPath } from "node:url";

import {
  trkLoad, trkSave, zonesOf, zoneAdd, zoneRemove, zoneMove, smpIntern, instrumentsUsed,
  ENGINE_SAMPLE, ENGINE_VOX, GAIN_MAX_DB, SONG_MAX_INSTR, NOTE_MIN, NOTE_MAX,
} from "../trk.js";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..", "..", "..");
const TRKDUMP = join(ROOT, "build", "trkdump");

function buildTrkdump() {
  const srcs = ["tests/trkdump.c", "core/song.c", "core/song.h", "core/trk.c", "core/trk.h"]
    .map((f) => join(ROOT, f));
  const fresh = existsSync(TRKDUMP) &&
    srcs.every((f) => statSync(f).mtimeMs <= statSync(TRKDUMP).mtimeMs);
  if (fresh) return;
  mkdirSync(join(ROOT, "build"), { recursive: true });
  execFileSync("cc", ["-std=c11", "-O2", join(ROOT, "tests/trkdump.c"),
    join(ROOT, "core/song.c"), join(ROOT, "core/trk.c"), "-o", TRKDUMP], { stdio: "inherit" });
}

// The .trk files of the repository, and moondaw's own songs beside it.
function corpus() {
  const dirs = ["docs/sampler-demo/songs", "tests", "tools/drums"].map((d) => join(ROOT, d));
  dirs.push(join(ROOT, "..", "moondaw", "songs"));
  const out = [];
  for (const d of dirs) {
    if (!existsSync(d)) continue;
    for (const f of readdirSync(d).sort()) if (f.endsWith(".trk")) out.push(join(d, f));
  }
  return out;
}

const canonical = (path) => execFileSync(TRKDUMP, [path], { encoding: "latin1", maxBuffer: 1 << 24 });

// Where two texts part, as a line and the two lines themselves.
function firstDiff(a, b) {
  const la = a.split("\n"), lb = b.split("\n");
  for (let i = 0; i < Math.max(la.length, lb.length); i++)
    if (la[i] !== lb[i]) return `line ${i + 1}\n    C  : ${JSON.stringify(la[i])}\n    js : ${JSON.stringify(lb[i])}`;
  return "the same lines, another length";
}

// A song the page could have made: touch every field the editor touches, so
// the check covers what it writes and not only what it read.
function edit(song) {
  let touched = 0;
  const used = instrumentsUsed(song);
  for (let i = 0; i < SONG_MAX_INSTR; i++) {
    if (!used[i]) continue;
    const inn = song.instruments[i];
    inn.attack = (inn.attack + 7) & 0xFF;
    inn.release = (inn.release + 3) & 0xFF;
    inn.gain = Math.min(GAIN_MAX_DB, inn.gain + 1);
    inn.level = inn.level ? 0 : -3;
    inn.cutoff = inn.cutoff === 255 ? 200 : 255;
    inn.reso = (inn.reso + 9) & 0x7F;
    inn.name = (inn.name || "X").slice(0, 6) + "Z";
    if (inn.engine === ENGINE_VOX) inn.tract = 90;
    if (inn.engine !== ENGINE_SAMPLE) continue;
    touched++;
    const mine = zonesOf(song, i);
    if (mine.length > 1) {
      // Swap a sample, move a zone, add one, drop one: the four things the
      // zone table does.
      song.zones[mine[0]].smp = song.zones[mine[1]].smp;
      song.zones[mine[0]].tune = -7;
      song.zones[mine[1]].gain = -5;
      zoneMove(song, mine[1], mine[0]);
    }
    const at = zoneAdd(song, i, { lo: NOTE_MIN, hi: 40, root: 36, smp: smpIntern(song, "added/new_one.wav") });
    if (at !== null && mine.length > 2) zoneRemove(song, at);
  }
  return touched;
}

buildTrkdump();
const files = process.argv.length > 2 ? process.argv.slice(2) : corpus();
if (!files.length) {
  console.log("roundtrip: no .trk found, nothing checked");
  process.exit(0);
}

let bad = 0, smpl = 0;
for (const path of files) {
  const name = basename(path);
  const src = readFileSync(path, "latin1");
  const want = canonical(path);
  const { err, song, report } = trkLoad(src);
  if (err || !song) {
    console.log(`${name.padEnd(24)} FAIL  trk.js refused it (${err})`);
    bad++;
    continue;
  }
  const got = trkSave(song);
  if (got !== want) {
    console.log(`${name.padEnd(24)} FAIL  canonical: ${firstDiff(want, got)}`);
    bad++;
    continue;
  }
  // 2: stable through trk.js itself
  const again = trkLoad(got);
  if (again.err || trkSave(again.song) !== got) {
    console.log(`${name.padEnd(24)} FAIL  not stable: ${firstDiff(got, again.err ? "" : trkSave(again.song))}`);
    bad++;
    continue;
  }
  // 3: an edited song survives the FM-1's own loader untouched
  const touched = edit(song);
  const edited = trkSave(song);
  const { err: e2, song: s2 } = trkLoad(edited);
  const back = e2 ? "" : trkSave(s2);
  if (back !== edited) {
    console.log(`${name.padEnd(24)} FAIL  edited song not lossless: ${firstDiff(edited, back)}`);
    bad++;
    continue;
  }
  smpl += touched;
  console.log(`${name.padEnd(24)} ok    ${String(want.length).padStart(6)} B, ${report.chunksUsed} chunks, ` +
    `${report.zones ? `${report.zones} zones dropped, ` : ""}${touched} SMPL instrument${touched === 1 ? "" : "s"} edited`);
}

// The edited songs go through the C loader too, not only trk.js's: that is the
// claim the device cares about. One file is enough to prove the path, and
// SMPL-DEMO is the one with zones to move.
const demo = files.find((f) => basename(f) === "SMPL-DEMO.trk");
if (demo) {
  const { song } = trkLoad(readFileSync(demo, "latin1"));
  edit(song);
  const edited = trkSave(song);
  const tmp = join(ROOT, "build", "songedit-edited.trk");
  execFileSync("sh", ["-c", `cat > ${JSON.stringify(tmp)}`], { input: edited });
  const through = canonical(tmp);
  if (through !== edited) {
    console.log(`SMPL-DEMO edited         FAIL  core/trk.c changed it: ${firstDiff(through, edited)}`);
    bad++;
  } else {
    console.log(`SMPL-DEMO edited         ok    ${edited.length} B through core/trk.c unchanged`);
  }
}


// ---------------------------------------------------------------------------
// bank.js bankFind against core/bank.c's, through audition.wasm. The two exist
// because the page must mark a silent zone without asking the engine, and the
// engine must resolve the same names the page marked; a difference between
// them would show as a zone the page calls good and the FM-1 plays as silence.
// A forged index is enough: bank_find reads names, never blocks.
function forgeBank(names) {
  const HEAD = 8, ENTRY = 32, NAME = 24, BLOCK = 255, FRAMES = 505;
  const head = HEAD + ENTRY * names.length;
  let at = 0;
  while (at < head || (at + 12) % BLOCK !== 0) at++;          // core/bank.c entry_ok
  const size = at + BLOCK * names.length;
  const b = new Uint8Array(size);
  b.set([0x53, 0x4D, 0x50, 0x31, names.length]);
  names.forEach((n, i) => {
    const e = HEAD + i * ENTRY;
    for (let k = 0; k < n.length && k < NAME - 1; k++) b[e + k] = n.charCodeAt(k);
    new DataView(b.buffer).setUint32(e + NAME, FRAMES, true);
    new DataView(b.buffer).setUint32(e + NAME + 4, at + i * BLOCK, true);
  });
  return b;
}

async function checkBankFind() {
  const wasmPath = join(ROOT, "build", "songedit", "audition.wasm");
  if (!existsSync(wasmPath)) {
    console.log("bank_find                skipped (build/songedit/audition.wasm: run web/songedit/build.sh)");
    return 0;
  }
  const { bankFind } = await import("../bank.js");
  const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), {});
  const ex = instance.exports;
  const names = ["tr9_kick", "pf_c4", "PF_C4b", "a", "marimba_c5", "x".repeat(23)];
  const bytes = forgeBank(names);
  new Uint8Array(ex.memory.buffer, ex.aud_bank_buf(), bytes.length).set(bytes);
  const mounted = ex.aud_bank_mount(bytes.length);
  if (mounted !== names.length) {
    console.log(`bank_find                FAIL  the forged bank mounted ${mounted} of ${names.length}`);
    return 1;
  }
  const samples = names.map((name) => ({ name, frames: 505 }));
  // The cases that matter: the folder, the extension, the case, a name too
  // long for the bank, an empty one, and a near miss.
  const asked = [
    "tr9_kick", "tr909/tr9_kick.wav", "TR909/TR9_KICK.WAV", "deep/er/tr9_kick.WaV",
    "tr9_kick.wave", "tr9_kic", "piano9/pf_c4.wav", "pf_c4b", "PF_C4B.wav", "a",
    "", ".wav", "x".repeat(23), "x".repeat(24), "marimba_c5.wav.wav", "a/b\\c/pf_c4.wav",
  ];
  let bad = 0;
  for (const q of asked) {
    const js = bankFind(samples, q);
    const nb = new Uint8Array(ex.memory.buffer, ex.aud_name_buf(), 48);
    nb.fill(0);
    for (let i = 0; i < q.length && i < 47; i++) nb[i] = q.charCodeAt(i) & 0xFF;
    const c = ex.aud_bank_find();
    if (js !== c) {
      console.log(`bank_find                FAIL  ${JSON.stringify(q)}: bank.js ${js}, core/bank.c ${c}`);
      bad++;
    }
  }
  if (!bad) console.log(`bank_find                ok    ${asked.length} names, bank.js and core/bank.c agree`);
  return bad;
}

bad += await checkBankFind();

console.log(`${files.length} file${files.length === 1 ? "" : "s"}, ${smpl} SMPL instruments edited, ${bad} failure${bad === 1 ? "" : "s"}`);
process.exit(bad ? 1 : 0);
