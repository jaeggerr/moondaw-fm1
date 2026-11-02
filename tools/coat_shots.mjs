// SPDX-License-Identifier: GPL-3.0-only
// The pictures for proto/coat.html, taken from the firmware rather than drawn:
// build/web/moondaw.wasm is the C the machine runs, and web_fb is its panel.
//
//     make web && node tools/coat_shots.mjs <song.trk>
//
// Writes proto/coat/. Needs ffmpeg.
import { readFileSync, mkdirSync } from "node:fs";
import { spawnSync } from "node:child_process";

const OUT = "proto/coat";
const W = 240, SCALE = 2;
const BTN = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"];
const SELECT = 0, KNOB2 = 4, KNOB3 = 5;

const { instance } = await WebAssembly.instantiate(readFileSync("build/web/moondaw.wasm"), {});
const ex = instance.exports;
if (ex._initialize) ex._initialize();
ex.web_flash_erase();
ex.web_boot();

const FRAME = Math.round(ex.web_sample_rate() / 30 / 128);
const run = (n = 1) => { for (let i = 0; i < n * FRAME; i++) ex.web_half(); };
const mask = (...n) => n.reduce((m, x) => m | (1 << BTN.indexOf(x)), 0);
const press = (...n) => { ex.web_btns(mask(...n)); run(2); ex.web_btns(0); run(6); };
const down = (...n) => { ex.web_btns(mask(...n)); run(4); };
const up = () => { ex.web_btns(0); run(4); };
const turn = (r, s) => { ex.web_enc(r, s); run(6); };
const chord = () => { ex.web_btns(mask("OCT-", "OCT+")); run(3); ex.web_btns(0); run(8); };

const rgb = () => {
  const fb = new Uint16Array(ex.memory.buffer, ex.web_fb(), W * W);
  const out = Buffer.alloc(W * W * 3);
  let o = 0;
  for (const p of fb) {
    out[o++] = Math.round((p >> 11) * 255 / 31);
    out[o++] = Math.round(((p >> 5) & 63) * 255 / 63);
    out[o++] = Math.round((p & 31) * 255 / 31);
  }
  return out;
};
const shot = (name) => {
  const r = spawnSync("ffmpeg", ["-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
    "-s", `${W}x${W}`, "-i", "-", "-vf", `scale=${W * SCALE}:${W * SCALE}:flags=neighbor`,
    `${OUT}/${name}.png`], { input: rgb(), stdio: ["pipe", "inherit", "inherit"] });
  if (r.status) throw new Error("ffmpeg failed on " + name);
};

mkdirSync(OUT, { recursive: true });
run(8);
const song = readFileSync(process.argv[2]);
new Uint8Array(ex.memory.buffer, ex.web_song_buf(), song.length).set(song);
new Uint8Array(ex.memory.buffer, ex.web_song_name(), 5).set(new TextEncoder().encode("SONG\0"));
if (!ex.web_song_load(song.length)) throw new Error("the song did not load");
run(8);

press("SEL");                 /* CONTINUE -> SONG */
turn(SELECT, 3);
shot("song-off");             /* eight rows, the coat off */
chord();
turn(KNOB2, -1);              /* left of the first column: the phrase number */
shot("song-number");          /* SEQ COPY, LFO DELETE */
turn(KNOB2, 1);
shot("song-cell");            /* one column right: the two go dark, SEL changes */
down("EDIT");
shot("keys");                 /* the black keys, three and two a row */
up();
press("EDIT");                /* out of the CAPS LOCK the tap left on */

press("GLO");                 /* the PROJECT */
turn(SELECT, 2);
turn(KNOB3, -4);
shot("project-speed");        /* SPEED under BPM, the foot saying its gesture */
turn(SELECT, 1);
turn(KNOB3, 1);
shot("project-metro");
press("HOME");

turn(KNOB2, 1);               /* a melody track */
press("SEL");                 /* -> PATTERN */
shot("pattern-notes");
press("FX");
shot("pattern-fx");           /* SEL wakes as SYLLABLE, small but true */
press("ENV");
shot("pattern-ins");          /* SEL and ENV both OPEN */
press("SEQ");
press("HOME");
press("FX");                  /* the MIXER */
shot("mixer");                /* six cells of twelve are dashes */
console.log(OUT + "/: song-off song-number song-cell keys project-speed project-metro "
            + "pattern-notes pattern-fx pattern-ins mixer");
