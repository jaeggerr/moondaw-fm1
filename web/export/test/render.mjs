// SPDX-License-Identifier: GPL-3.0-only
//
// The export page's claim, measured: that the WAVs it writes are the song the
// FM-1 plays, cut into tracks, and that the pieces still add up.
//
// The central check is not a tolerance. tests/fm_render.c is the SAME engine
// built natively, and it renders a song the same way -- player_init, the
// mutes, play, blocks of player_render. So the page's output and its output
// are held against each other SAMPLE FOR SAMPLE over the arrangement, which
// is where the two can be compared (past its end the native one loops round
// while the page lets the tail ring). Nothing in between resamples, dithers
// or converts, so anything but zero differences is a bug in one of them.
//
// It runs the ASSEMBLED page's own modules (build/export/, put there by
// web/export/build.sh) rather than copies, so what is tested is what
// moondaw.com serves. No browser: the engine is wasm and node runs wasm.
//
//   make export && node web/export/test/render.mjs     (also part of `make test`)
import { readFileSync, existsSync, mkdirSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, "..", "..", "..");
const built = join(root, "build", "export");
if (!existsSync(join(built, "audition.wasm"))) {
  console.error("render: no build/export — run `make export` first");
  process.exit(1);
}
const { job, Engine } = await import(join(built, "render.js"));
const { wav, unwav } = await import(join(built, "wav.js"));
const { tracksUsed, names, safe, dry } = await import(join(built, "stems.js"));
const { trkLoad, trkSave, instrumentsUsed, PATTERN_NONE, ENGINE_SAMPLE, SONG_MAX_TRACKS,
        STEP_MAX_FX, TRACK_MAX_PATTERNS, FX_NAMES, songStep } = await import(join(built, "trk.js"));

const wasmFile = readFileSync(join(built, "audition.wasm"));
const wasm = () => wasmFile.buffer.slice(wasmFile.byteOffset, wasmFile.byteOffset + wasmFile.byteLength);
const native = join(root, "build", "fm_render");

let fails = 0, checks = 0;
function ok(cond, what, detail = "") {
  checks++;
  if (cond) return true;
  fails++;
  console.log(`  FAIL  ${what}${detail ? "  — " + detail : ""}`);
  return false;
}
const db = (x) => 20 * Math.log10(Math.max(x, 1e-12));
function rms(pcm, from = 0, to = pcm.length) {
  let s = 0;
  for (let i = from; i < to; i++) s += pcm[i] * pcm[i];
  return Math.sqrt(s / Math.max(1, to - from)) / 32768;
}

// ---------------------------------------------------------------- the WAV ---
{
  const pcm = new Int16Array([0, 0, 1000, -1000, 32767, -32768]);
  const b = wav(pcm, 44100);
  ok(String.fromCharCode(...b.slice(0, 4)) === "RIFF", "wav: RIFF");
  ok(String.fromCharCode(...b.slice(8, 12)) === "WAVE", "wav: WAVE");
  const d = new DataView(b.buffer);
  ok(d.getUint16(20, true) === 1, "wav: uncompressed PCM");
  ok(d.getUint16(22, true) === 2, "wav: stereo");
  ok(d.getUint32(24, true) === 44100, "wav: 44100 Hz");
  ok(d.getUint16(34, true) === 16, "wav: 16 bits");
  ok(d.getUint32(28, true) === 44100 * 4, "wav: the byte rate");
  ok(d.getUint32(4, true) === b.length - 8, "wav: the RIFF length");
  ok(d.getUint32(40, true) === pcm.length * 2, "wav: the data length");
  const back = unwav(b);
  ok(back.rate === 44100 && back.pcm.length === pcm.length &&
     back.pcm.every((v, i) => v === pcm[i]), "wav: the samples come back");
}

// -------------------------------------------------------------- the names ---
{
  ok(safe("bass drum") === "BASS-DRUM", "safe: a space", safe("bass drum"));
  ok(safe("Lé Ad!") === "L-AD", "safe: what a file name cannot hold", safe("Lé Ad!"));
  ok(safe("--kick--") === "KICK", "safe: no dash at either end", safe("--kick--"));
  ok(safe("") === "", "safe: nothing is nothing");
  const used = [{ track: 0, instr: 1, name: "KICK" }, { track: 4, instr: 2, name: "" }];
  const n = names("serial", "stems", used);
  ok(n.zip === "SERIAL-stems.zip", "names: the zip", n.zip);
  ok(n.files[0] === "SERIAL-1-KICK.wav", "names: number and instrument", n.files[0]);
  ok(n.files[1] === "SERIAL-5.wav", "names: an instrument with no name", n.files[1]);
  ok(names("serial", "dry", used).zip === "SERIAL-dry.zip", "names: the dry zip");
  ok(names("serial", "mix", used).single === "SERIAL.wav", "names: the mixdown");
  ok(names("", "mix", used).single === "SONG.wav", "names: a song with no name");
}

// --------------------------------------------------------------- the songs ---
const songs = ["tests/dpen-chorus.trk", "tests/vox-styles.trk", "docs/drums-demo/songs/KIDS-DRUM.trk"];
const engine = await Engine.load(wasm());
mkdirSync(join(root, "build", "export-test"), { recursive: true });

for (const rel of songs) {
  const path = join(root, rel);
  if (!existsSync(path)) { console.log(`  skip  ${rel}`); continue; }
  const trk = readFileSync(path, "latin1");
  const { err, song } = trkLoad(trk);
  if (!ok(err === 0 && song, `${rel}: trk.js reads it`)) continue;

  const used = tracksUsed(song);
  const mix = await job({ wasm: engine, trk, mode: "mix", songName: "X" });
  const played = engine.playedFrames;                 // before the tail
  const wet = await job({ wasm: engine, trk, mode: "stems", songName: "X" });
  console.log(`  ${rel}  ${(mix.frames / mix.rate).toFixed(1)}s  ${used.length} track(s): ` +
    used.map((u) => (u.track + 1) + (u.name ? ":" + u.name : "")).join(" "));

  ok(mix.files.length === 1 && mix.files[0].name === "X.wav", `${rel}: one mixdown`);
  ok(wet.files.length === used.length, `${rel}: one WAV per track that plays`,
     `${wet.files.length} vs ${used.length}`);
  ok(wet.files.every((f) => f.data.length === wet.files[0].data.length),
     `${rel}: every stem the same length`);
  ok(mix.frames > mix.rate, `${rel}: longer than a second`, `${mix.frames} frames`);
  ok(played > 0 && played <= mix.frames, `${rel}: the arrangement has a length`, String(played));

  const M = unwav(mix.files[0].data).pcm;
  const S = wet.files.map((f) => unwav(f.data).pcm);
  ok(db(rms(M)) > -40, `${rel}: the mixdown is not silence`, `${db(rms(M)).toFixed(1)} dB`);

  // THE CHECK: the same song, the same engine, built natively. Over the
  // arrangement, every sample.
  if (existsSync(native)) {
    const out = join(root, "build", "export-test", "native.wav");
    execFileSync(native, [path, out, String(played / mix.rate)]);
    const N = unwav(readFileSync(out)).pcm;
    const n = Math.min(N.length, played * 2);
    let differ = 0, first = -1, worst = 0;
    for (let i = 0; i < n; i++) {
      const d = Math.abs(M[i] - N[i]);
      if (d) { differ++; if (first < 0) first = i; if (d > worst) worst = d; }
    }
    ok(n > mix.rate, `${rel}: the native render came out`, `${n / 2} frames`);
    ok(differ === 0, `${rel}: the page's mixdown IS tests/fm_render.c's, sample for sample`,
       `${differ} of ${n} differ, first at frame ${first >> 1}, worst ${worst}`);

    // And a stem is the native renderer's solo of that track, the same way.
    if (used.length) {
      const t = used[used.length - 1].track;
      execFileSync(native, [path, out, String(played / mix.rate), String(t)]);
      const NS = unwav(readFileSync(out)).pcm;
      const sn = Math.min(NS.length, played * 2, S[used.length - 1].length);
      let sd = 0;
      for (let i = 0; i < sn; i++) if (S[used.length - 1][i] !== NS[i]) sd++;
      ok(sd === 0, `${rel}: and a stem IS its solo, sample for sample`, `${sd} of ${sn} differ`);
    }
  } else {
    console.log("  skip  the native comparison (no build/fm_render)");
  }

  // A stem that could sound and does not would be a routing bug. A track
  // whose instrument is SMPL is left out of this: no bank is mounted here, so
  // the device itself would play it silent (core/bank.c: a zone naming a
  // sample the bank has not got is silent, never an error).
  const iu = instrumentsUsed(song);
  S.forEach((s, i) => {
    const u = used[i];
    const sampled = u.instr >= 0 && song.instruments[u.instr].engine === ENGINE_SAMPLE;
    if (sampled) return;
    ok(db(rms(s)) > -60, `${rel}: ${wet.files[i].name} is not silence`, `${db(rms(s)).toFixed(1)} dB`);
  });
  let instrs = 0;
  for (const u of iu) if (u) instrs++;
  ok(instrs > 0, `${rel}: the song names instruments`);

  // The pieces add up. Not to the bit, and on purpose: the master limiter is
  // on the mixdown and must NOT be on a stem -- a limiter reacting to one
  // track would squash it for a peak the whole never had. So this asks for
  // the SHAPE (a correlation, which a gain change leaves alone) and for a
  // level within a couple of dB: a stem missing, doubled or at the wrong
  // fader fails both.
  const n = Math.min(M.length, ...S.map((s) => s.length));
  let sqM = 0, sqSum = 0, dot = 0;
  for (let i = 0; i < n; i++) {
    let sum = 0;
    for (const s of S) sum += s[i];
    sqM += M[i] * M[i]; sqSum += sum * sum; dot += sum * M[i];
  }
  const corr = dot / Math.sqrt(Math.max(1, sqM * sqSum));
  const level = 10 * Math.log10(Math.max(1, sqSum) / Math.max(1, sqM));
  ok(corr > 0.99, `${rel}: the stems add up to the mixdown`, `correlation ${corr.toFixed(5)}`);
  ok(Math.abs(level) < 2, `${rel}: and at the mixdown's level`, `${level.toFixed(2)} dB apart`);

  // Dry: the sends gone from the song that is rendered, nothing else touched.
  const d = dry(song);
  let sends = 0, revFx = 0, wasWet = 0;
  for (let t = 0; t < SONG_MAX_TRACKS; t++) {
    sends += d.trackInit[t].send + d.trackInit[t].echo;
    wasWet += song.trackInit[t].send + song.trackInit[t].echo;
  }
  for (let t = 0; t < SONG_MAX_TRACKS; t++)
    for (let p = 0; p < TRACK_MAX_PATTERNS; p++)
      for (let r = 0; r < d.patterns[t][p].length; r++)
        for (let k = 0; k < STEP_MAX_FX; k++) {
          const c = songStep(d, t, p, r).fx[k].cmd;
          if (c === FX_NAMES.indexOf("REV") || c === FX_NAMES.indexOf("ECH")) revFx++;
        }
  ok(sends === 0, `${rel}: dry has no track send left`, String(sends));
  ok(revFx === 0, `${rel}: dry has no REV or ECH step left`, String(revFx));
  let keptElse = 0;
  for (let t = 0; t < SONG_MAX_TRACKS; t++)
    if (d.trackInit[t].volume !== song.trackInit[t].volume || d.trackInit[t].pan !== song.trackInit[t].pan) keptElse++;
  ok(keptElse === 0, `${rel}: dry keeps every fader and pan`, String(keptElse));

  const dryJob = await job({ wasm: engine, trk, mode: "dry", songName: "X" });
  ok(dryJob.files.length === used.length, `${rel}: dry gives the same tracks`);
  ok(dryJob.files.every((f) => f.data.length === dryJob.files[0].data.length),
     `${rel}: every dry stem the same length`);
  if (wasWet > 0) {
    // A song that sends: taking the sends away has to be audible on a track
    // that can sound at all, or nothing was taken away.
    let k = used.findIndex((u, i) => db(rms(S[i])) > -60 &&
      (song.trackInit[u.track].send + song.trackInit[u.track].echo) > 0);
    if (k < 0) k = used.findIndex((u, i) => db(rms(S[i])) > -60);
    if (k >= 0) {
      const D = unwav(dryJob.files[k].data).pcm, W = S[k];
      const m = Math.min(D.length, W.length);
      let sq = 0;
      for (let i = 0; i < m; i++) { const x = D[i] - W[i]; sq += x * x; }
      const apart = db(Math.sqrt(sq / m) / 32768);
      ok(apart > -80, `${rel}: dry is not the wet one again`, `${apart.toFixed(1)} dB apart`);
    }
  }

  // The song decides which tracks come out: a track the arrangement stops
  // naming loses its file, and the others keep their numbers.
  if (used.length > 1) {
    const cut = used[used.length - 1].track;
    const less = trkLoad(trk).song;
    for (let i = 0; i < less.arrangeLen; i++) less.arrangement[i].pattern[cut] = PATTERN_NONE;
    const after = await job({ wasm: engine, trk: trkSave(less), mode: "stems", songName: "X" });
    ok(after.files.length === used.length - 1, `${rel}: a track out of the arrangement loses its WAV`,
       `${after.files.length} vs ${used.length - 1}`);
    ok(!after.files.some((f) => f.name === wet.files[wet.files.length - 1].name),
       `${rel}: and it is the right one that went`);
    ok(after.files[0].name === wet.files[0].name, `${rel}: the others keep their numbers`);
  }
}

// ----------------------------------------------- the sampled song, if any ---
// The demo bank is 126 KB of samples that live in ../moondaw and is not in
// the repository (docs/sampler-demo/README.md packs it). Where it has been
// packed, the sampled road is walked too: without a bank those tracks are
// silent, with one they are not.
{
  const demo = join(root, "docs", "sampler-demo", "songs", "SMPL-DEMO.trk");
  const bankFile = join(root, "build", "demo-bank.bin");
  if (existsSync(demo) && existsSync(bankFile)) {
    const trk = readFileSync(demo, "latin1");
    const bank = readFileSync(bankFile);
    const bare = await job({ wasm: wasm(), trk, mode: "mix", songName: "SMPL" });
    const full = await job({ wasm: wasm(), trk, bank, mode: "mix", songName: "SMPL" });
    const B = unwav(bare.files[0].data).pcm, F = unwav(full.files[0].data).pcm;
    ok(db(rms(B)) < -60, "a sampled song with no bank is silent", `${db(rms(B)).toFixed(1)} dB`);
    ok(db(rms(F)) > -40, "and with the bank it plays", `${db(rms(F)).toFixed(1)} dB`);
    const st = await job({ wasm: wasm(), trk, bank, mode: "stems", songName: "SMPL" });
    ok(st.files.length > 0, "and it gives stems", String(st.files.length));
    console.log(`  SMPL-DEMO.trk with the bank  ${st.files.length} track(s)`);
  } else {
    console.log("  skip  the sampled song (no build/demo-bank.bin — docs/sampler-demo/README.md packs it)");
  }
}

// -------------------------------------------------------- what it refuses ---
{
  let said = "";
  try { await job({ wasm: engine, trk: "hello, I am not a song\n", mode: "mix" }); }
  catch (e) { said = e.message; }
  ok(/not a song/.test(said), "a file that is not a song is refused", said);

  said = "";
  try { await job({ wasm: engine, trk: readFileSync(join(root, songs[0]), "latin1"), mode: "sideways" }); }
  catch (e) { said = e.message; }
  ok(/unknown mode/.test(said), "an unknown mode is refused", said);
}

console.log(`render: ${checks - fails}/${checks} checks`);
process.exit(fails ? 1 : 0);
