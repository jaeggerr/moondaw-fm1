// SPDX-License-Identifier: GPL-3.0-only
// The README's pictures, taken from the firmware rather than drawn:
// build/web/moondaw.wasm is the C the machine runs, and web_fb is its panel.
//
//     make web && node tools/readme_shots.mjs <song.trk>
//
// Writes docs/readme/. Needs ffmpeg.
import { readFileSync, mkdirSync } from "node:fs";
import { spawnSync } from "node:child_process";

const OUT = "docs/readme";
const SCALE = 2; /* the panel is 240 pixels square: doubled, by nearest neighbour */
const W = 240, GIF_FPS = 15, GIF_SECONDS = 9;
const BTN = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"];
const SELECT = 0, KNOB2 = 4;

const { instance } = await WebAssembly.instantiate(readFileSync("build/web/moondaw.wasm"), {});
const ex = instance.exports;
if (ex._initialize) ex._initialize();
ex.web_flash_erase();
ex.web_boot();

const FRAME = Math.round(ex.web_sample_rate() / 30 / 128);
const run = (frames = 1) => { for (let i = 0; i < frames * FRAME; i++) ex.web_half(); };
const press = (name) => {
  ex.web_btns(1 << BTN.indexOf(name));
  run(2);
  ex.web_btns(0);
  run(6);
};
const turn = (role, steps) => { ex.web_enc(role, steps); run(6); };

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
const ffmpeg = (input, filter, file) => {
  const r = spawnSync("ffmpeg", ["-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
    "-s", `${W}x${W}`, "-r", String(GIF_FPS), "-i", "-", "-vf", filter, "-frames:v", "100000", `${OUT}/${file}`],
    { input, stdio: ["pipe", "inherit", "inherit"] });
  if (r.status) throw new Error("ffmpeg failed on " + file);
};
const NEAREST = `scale=${W * SCALE}:${W * SCALE}:flags=neighbor`;
const shot = (name) => ffmpeg(rgb(), NEAREST, name + ".png");

mkdirSync(OUT, { recursive: true });
run(8);
const song = readFileSync(process.argv[2]);
new Uint8Array(ex.memory.buffer, ex.web_song_buf(), song.length).set(song);
new Uint8Array(ex.memory.buffer, ex.web_song_name(), 5).set(new TextEncoder().encode("SONG\0"));
if (!ex.web_song_load(song.length)) throw new Error("the song did not load");
run(8);

press("SEL");                   /* CONTINUE -> SONG */
turn(SELECT, 3);
shot("song");
press("SEL");                   /* a row -> its SECTION */
shot("section");
press("FX");                    /* FX on SECTION is the MIXER */
shot("mixer");
press("HOME");
turn(KNOB2, 2);                 /* the third track, a melody */
press("SEL");                   /* a block -> PATTERN */
shot("pattern");
press("FX");                    /* the same block's effects */
shot("effects");
press("ENV");                   /* its instruments */
press("ENV");                   /* the one under the cursor */
shot("instrument");
press("HOME");
press("HOME");
turn(KNOB2, -2);

press("PLAY");
const frames = [];
const perFrame = Math.round(ex.web_sample_rate() / GIF_FPS / 128);
for (let f = 0; f < GIF_FPS * GIF_SECONDS; f++) {
  for (let i = 0; i < perFrame; i++) ex.web_half();
  frames.push(rgb());
}
ffmpeg(Buffer.concat(frames),
  `${NEAREST},split[a][b];[a]palettegen=max_colors=32:stats_mode=full[p];[b][p]paletteuse=dither=none`,
  "playing.gif");
