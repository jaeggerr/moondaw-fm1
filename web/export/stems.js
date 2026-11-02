// SPDX-License-Identifier: GPL-3.0-only
//
// Which tracks come out, what their files are called, and what "dry" means.
// Everything here reads a song trk.js has already parsed and touches no DOM
// and no engine, which is why the test can hold it against real songs.
//
// It imports ./trk.js, the module build.sh puts BESIDE it: this directory is
// assembled into build/export/ and run from there, never served from the
// source tree -- the same rule as platform/web/.
import {
  SONG_MAX_TRACKS, TRACK_MAX_PATTERNS, SONG_MAX_INSTR, STEP_MAX_FX, STEP_MAX_NOTES,
  NOTE_NONE, NOTE_OFF, FX_NAMES, FX_NONE, songStep, songCopy,
} from "./trk.js";

const FX_REV = FX_NAMES.indexOf("REV"), FX_ECH = FX_NAMES.indexOf("ECH");

// The blocks a phrase of the arrangement really plays on this track. A
// pattern a phrase does not name is not rendered even when it holds steps --
// the device would not play it either, so a stem must not carry it.
function playedPatterns(s, track) {
  const used = new Set();
  for (let i = 0; i < s.arrangeLen; i++) {
    const p = s.arrangement[i].pattern[track];
    if (p < TRACK_MAX_PATTERNS) used.add(p);
  }
  return [...used];
}

const strikes = (st) => {
  for (let i = 0; i < STEP_MAX_NOTES; i++)
    if (st.notes[i] !== NOTE_NONE && st.notes[i] !== NOTE_OFF) return true;
  return false;
};

// A track is rendered when the arrangement plays a block of it that strikes a
// note. A track muted everywhere, or playing nothing but note-offs, would
// give a file of silence: his call (02/11) is that it gives no file at all.
// -> [{ track, instr, name, notes }], in track order. `notes` counts the
// steps that strike in the blocks the arrangement plays, which is what the
// page shows beside each file.
export function tracksUsed(s) {
  const out = [];
  for (let t = 0; t < SONG_MAX_TRACKS && t < s.numTracks; t++) {
    const count = new Array(SONG_MAX_INSTR).fill(0);
    let notes = 0;
    for (const p of playedPatterns(s, t))
      for (let r = 0; r < s.patterns[t][p].length; r++) {
        const st = songStep(s, t, p, r);
        if (!strikes(st)) continue;
        notes++;
        if (st.instrument < SONG_MAX_INSTR) count[st.instrument]++;
      }
    if (!notes) continue;
    let instr = -1;
    for (let i = 0; i < SONG_MAX_INSTR; i++)
      if (count[i] && (instr < 0 || count[i] > count[instr])) instr = i;
    out.push({ track: t, instr, name: instrName(s, instr), notes });
  }
  return out;
}

// The instrument that strikes most on the track names its file, so an import
// is recognisable without being listened to. An instrument with no name --
// the FM-1 allows it -- names nothing, and the number stands alone.
function instrName(s, instr) {
  const nm = instr >= 0 ? (s.instruments[instr].name || "") : "";
  return safe(nm);
}

// A file name a ZIP, a DAW and three operating systems all accept: the
// FM-1's own alphabet (it has no lower case on the panel) and nothing else.
export function safe(text) {
  const up = String(text).toUpperCase();
  let out = "";
  for (const c of up) out += /[A-Z0-9#-]/.test(c) ? c : (out.endsWith("-") ? "" : "-");
  return out.replace(/^-+|-+$/g, "");
}

// What the files are called. `song` is the name the FM-1 keeps for the slot
// (12 characters, its own alphabet), or the dropped file's stem.
//   stems  SERIAL-stems.zip   -> SERIAL-1-KICK.wav, SERIAL-3-BASS.wav
//   dry    SERIAL-dry.zip     -> the same names: a zip holds one version
//   mix    SERIAL.wav
export function names(song, mode, used) {
  const base = safe(song) || "SONG";
  if (mode === "mix") return { single: `${base}.wav` };
  return {
    zip: `${base}-${mode === "dry" ? "dry" : "stems"}.zip`,
    files: used.map((u) => `${base}-${u.track + 1}${u.name ? "-" + u.name : ""}.wav`),
  };
}

// A song without its sends: the track faders' REV and ECH at zero, and the
// step effects that would have put them back taken out. Returns a COPY --
// the page keeps the song it received, since that is the one it may send
// back to the machine.
//
// Only these two are touched. An instrument's filter, envelope and gain are
// what the sound IS and stay; the reverb and the delay are the room it was
// put in, and a stem is what goes into someone else's room.
export function dry(song) {
  const s = songCopy(song);
  for (let t = 0; t < SONG_MAX_TRACKS; t++) { s.trackInit[t].send = 0; s.trackInit[t].echo = 0; }
  for (let t = 0; t < SONG_MAX_TRACKS; t++)
    for (let p = 0; p < TRACK_MAX_PATTERNS; p++)
      for (let r = 0; r < s.patterns[t][p].length; r++) {
        const st = songStep(s, t, p, r);
        let hit = false;
        for (let k = 0; k < STEP_MAX_FX; k++)
          if (st.fx[k].cmd === FX_REV || st.fx[k].cmd === FX_ECH) hit = true;
        if (!hit) continue;
        // songStep gives the pooled step itself, and a chunk is shared between
        // the blocks that hold the same steps: writing it here is the point.
        for (let k = 0; k < STEP_MAX_FX; k++)
          if (st.fx[k].cmd === FX_REV || st.fx[k].cmd === FX_ECH) { st.fx[k].cmd = FX_NONE; st.fx[k].val = 0; }
      }
  return s;
}
