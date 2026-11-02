// SPDX-License-Identifier: GPL-3.0-only
//
// The render: the FM-1's own core, driven as fast as the computer can instead
// of as fast as a sound card.
//
// Why here and not on the device. The FM-1 renders a song in real time and
// only just -- the chorus of one demo already overran its audio block (see
// core/player.h's profiler). Writing eight WAVs of it would mean rendering it
// nine times over, holding megabytes it has 512 KB for, and sending them out
// through a MIDI pipe that carries 120 bytes an answer. So the machine gives
// up the one thing it is good at keeping, the .trk, and the browser does the
// arithmetic. The engine is the SAME C (web/songedit/audition.c -> wasm), so
// what comes out of the file is what came out of the speaker.
//
// Nothing is resampled and nothing is converted: aud_render gives interleaved
// 16-bit stereo at 44100, which is what a WAV holds.
//
// The modules it imports sit beside it in build/export/ (see build.sh).
import { trkLoad, trkSave, TRK_OK, SONG_MAX_TRACKS } from "./trk.js";
import { tracksUsed, names, dry } from "./stems.js";
import { wav } from "./wav.js";

export const BLOCK = 1024;        // audition.c RENDER_MAX
const QUIET = 8;                  // of 32768: under this a block is silence
const QUIET_BLOCKS = 4;           // ~93 ms of it before the tail is cut
const TAIL_MAX = 8;               // seconds of tail, whatever still rings
const JOB_MAX = 20 * 60;          // seconds: a runaway song stops here
const LEADIN_MAX = 64;            // blocks before PLAY must have taken hold

export const MODES = ["stems", "dry", "mix"];

export class Engine {
  // The module is compiled ONCE and instantiated ONCE PER PASS -- a cold
  // machine for every file. That is not thrift turned down, it is the only
  // honest render: the master limiter's gain, like anything else the engine
  // keeps between blocks, is not reset by player_init, so a second song
  // rendered into the same instance starts with the first one's gain still
  // pulled down. The test caught it: the same song rendered twice into one
  // instance differed from frame 36 onward. Instantiating again costs about a
  // millisecond and makes every WAV reproducible on its own.
  static async load(wasmBytes) {
    const module = await WebAssembly.compile(wasmBytes);
    const probe = await Engine.spawn(module);
    if (!probe.aud_play_once)
      throw new Error("this audition.wasm is older than the export page: rebuild it (make export)");
    return new Engine(module, probe.aud_rate());
  }

  static async spawn(module) {
    const instance = await WebAssembly.instantiate(module, {});
    const ex = instance.exports;
    if (ex._initialize) ex._initialize();
    return ex;
  }

  constructor(module, rate) {
    this.module = module;
    this.rate = rate;
    this.bankBytes = null;
    this.bankCount = 0;
    // Frames up to the end of the arrangement in the last pass, the tail not
    // counted. The test renders the same song with tests/fm_render.c -- the
    // same C, native -- and holds the two against each other over exactly
    // these frames, where the native one has not yet looped round.
    this.playedFrames = 0;
  }

  // The packed sample bank, so sampled instruments sound instead of going
  // silently missing. Kept, and mounted again on every pass, since every pass
  // gets a machine of its own.
  setBank(bytes) {
    this.bankBytes = bytes && bytes.length ? bytes : null;
    return this;
  }

  mount(ex) {
    const b = this.bankBytes;
    if (!b) return 0;
    if (b.length > ex.aud_bank_cap()) throw new Error("that bank is larger than the FM-1's flash can hold");
    new Uint8Array(ex.memory.buffer, ex.aud_bank_buf(), b.length).set(b);
    this.bankCount = ex.aud_bank_mount(b.length);
    return this.bankCount;
  }

  // One pass over the arrangement. `solo` is a track, or -1 for all of them.
  // -> Int16Array, interleaved stereo, from the first frame to the tail's
  // last audible one.
  async pass(trkText, solo = -1, onProgress = () => {}) {
    const ex = await Engine.spawn(this.module);
    this.mount(ex);

    const text = new TextEncoder().encode(trkText);
    if (text.length > ex.aud_song_cap()) throw new Error("that song is larger than the FM-1 can hold");
    new Uint8Array(ex.memory.buffer, ex.aud_song_buf(), text.length).set(text);
    if (!ex.aud_song_load(text.length)) throw new Error("the engine refused that song");

    // The mutes start cleared (player_init), so a stem never inherits the
    // one before it -- as the fresh instance already makes sure.
    if (solo >= 0)
      for (let t = 0; t < SONG_MAX_TRACKS; t++) if (t !== solo) ex.aud_mute(t);
    ex.aud_play_once(0);

    const parts = [];
    let frames = 0, started = false, quiet = 0, tail = 0;
    this.playedFrames = 0;
    for (;;) {
      const pcm = new Int16Array(ex.memory.buffer, ex.aud_render(BLOCK), BLOCK * 2);
      parts.push(pcm.slice());
      frames += BLOCK;

      // aud_playing() only answers once a block has taken the queued PLAY up,
      // so the end is "it played, and then it stopped", never "it is not
      // playing yet".
      const playing = !!ex.aud_playing();
      if (playing) { started = true; tail = 0; this.playedFrames = frames; }
      else if (started) tail += BLOCK;
      else if (frames >= LEADIN_MAX * BLOCK) throw new Error("the song did not start: is its arrangement empty?");

      let peak = 0;
      for (let i = 0; i < pcm.length; i++) { const v = pcm[i] < 0 ? -pcm[i] : pcm[i]; if (v > peak) peak = v; }
      quiet = peak <= QUIET ? quiet + 1 : 0;

      if (started && !playing && (quiet >= QUIET_BLOCKS || tail >= TAIL_MAX * this.rate)) break;
      if (frames >= JOB_MAX * this.rate) break;
      onProgress({ frames, playing, entry: ex.aud_entry() });
    }

    // The silence the tail ended in is not kept -- but never at the cost of
    // the arrangement itself. A song whose last notes die before its last
    // phrase does would otherwise come out shorter than the song is, and a
    // DAW would be handed a file that stops mid-bar. So: the arrangement's
    // own length at the least, and past it only what still rings.
    const played = Math.min(frames, this.playedFrames + BLOCK);
    const sounded = frames - (quiet >= QUIET_BLOCKS ? quiet * BLOCK : 0);
    const keep = Math.max(BLOCK, played, sounded);
    const out = new Int16Array(keep * 2);
    let at = 0;
    for (const p of parts) {
      const n = Math.min(p.length, out.length - at);
      if (n <= 0) break;
      out.set(n === p.length ? p : p.subarray(0, n), at);
      at += n;
    }
    return out;
  }
}

// The whole job, the one the page runs and the one the test runs.
//   mode "stems"  one WAV per track that plays, its sends and their tails in it
//   mode "dry"    the same, the sends taken out of the song first
//   mode "mix"    one WAV of the song, as the machine plays it
// -> { files: [{name, data}], zipName | single, frames, rate, used }
export async function job({ wasm, trk, bank = null, mode = "stems", songName = "SONG", onProgress = () => {} }) {
  if (!MODES.includes(mode)) throw new Error(`unknown mode ${mode}`);
  const { err, song } = trkLoad(trk);
  if (err !== TRK_OK || !song) throw new Error("that file is not a song the FM-1 reads");

  const engine = wasm instanceof Engine ? wasm : await Engine.load(wasm);
  engine.setBank(bank);

  const used = tracksUsed(song);
  if (mode !== "mix" && !used.length) throw new Error("no track of that song plays a note");
  const text = mode === "dry" ? trkSave(dry(song)) : trk;
  const passes = mode === "mix" ? [-1] : used.map((u) => u.track);
  const named = names(songName, mode, used);

  const pcm = [];
  for (let i = 0; i < passes.length; i++) {
    onProgress({ pass: i, passes: passes.length, frames: 0 });
    pcm.push(await engine.pass(text, passes[i], ({ frames }) => onProgress({ pass: i, passes: passes.length, frames })));
  }

  // One length for all of them: a reverb tail that outlives the others must
  // not shorten its neighbours, and eight files dropped into a DAW have to
  // start and end together.
  const frames = Math.max(...pcm.map((p) => p.length / 2));
  const files = pcm.map((p, i) => {
    let full = p;
    if (p.length < frames * 2) { full = new Int16Array(frames * 2); full.set(p); }
    return { name: mode === "mix" ? named.single : named.files[i], data: wav(full, engine.rate) };
  });
  return { files, zipName: named.zip, single: named.single, frames, rate: engine.rate, used };
}
