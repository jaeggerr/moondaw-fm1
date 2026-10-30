// SPDX-License-Identifier: GPL-3.0-only
//
// The editor page's glue: the file in, the instrument and zone editors, the
// audition, the file out. trk.js holds the format, zones.js the map, bank.js
// the bank, audio.js the engine; this is only the wiring and what the page
// says.
//
// Two rules run through it. The song is the one truth: every edit changes the
// Song and the page is redrawn from it, never the other way round -- so what
// the text area shows is what a save writes, always. And the page says what
// the FM-1 will do, not what would be convenient: an instrument no step plays
// is not written, a zone naming a sample the bank has not got is silent, and
// both are shown as such rather than quietly tidied away.

import {
  trkLoad, trkSave, instrumentsUsed, samplesUsed, zonesOf, zoneAdd, zoneRemove, zoneMove, zoneShadow,
  smpIntern, noteName, noteFromName, engineName, engineTimbreMax, ENGINE_NAMES,
  ENGINE_SAMPLE, ENGINE_VOX, ENGINE_DRUM, ENGINE_PLUCK, SONG_MAX_INSTR, SONG_ZONE_MAX,
  INSTR_MAX_LAYERS, SONG_SMP_MAX, NOTE_MIN, NOTE_MAX, VIB_PHASE_FREE,
  TRK_ERR_VERSION, TRK_ERR_PATCH,
} from "./trk.js";
import { ZoneRuler } from "./zones.js";
import { bankFromFile, bankFind, SMP_RATE } from "./bank.js";
import { Audition } from "./audio.js";
import { Updater } from "../installer/fm1ota.js";
import { SongLink } from "../installer/mdlink.js";

const $ = (id) => document.getElementById(id);
const hex2 = (v) => v.toString(16).toUpperCase().padStart(2, "0");
const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

let song = null;        // the one truth
let loaded = "";        // the file as it was opened, to say whether it changed
let fileName = "song.trk";
let inst = -1;          // the instrument under the cursor
let bank = null;        // {size, samples, bytes?} -- bytes only from a packed file
let bankWhere = "";
let cursorZone = -1;
let aud = null;
let midi = null, busy = false;

// --------------------------------------------------------------- the bank ---

// What the device would resolve a song name to: core/bank.c's rules, run here
// by bank.js so a zone can be marked without a round trip.
const bankHasName = (name) => !bank || bankFind(bank.samples, name) >= 0;
const bankHas = (smp) => {
  const name = song?.smp[smp]?.name || "";
  return name ? bankHasName(name) : false;
};

function showBank() {
  if (!bank) {
    $("bankinfo").textContent = "No bank known: the page cannot say which zones are silent, and nothing will sound.";
    $("bankinfo").className = "small";
    $("bankdetails").classList.add("hide");
    return;
  }
  const kb = (bank.size / 1024).toFixed(0);
  const sounds = bank.bytes ? "" : " · the index only: load the packed bank file to hear anything";
  $("bankinfo").textContent = `${bank.samples.length} samples, ${kb} KB, from ${bankWhere}${sounds}`;
  $("bankinfo").className = "small";
  $("banklist").innerHTML = "";
  for (const s of bank.samples) {
    const d = document.createElement("div");
    d.textContent = `${s.name.padEnd(24, " ")} ${(s.frames / SMP_RATE).toFixed(2)} s`;
    $("banklist").append(d);
  }
  $("bankdetails").classList.remove("hide");
}

$("bankfile").addEventListener("change", async () => {
  const f = $("bankfile").files[0];
  if (!f) return;
  try {
    bank = bankFromFile(new Uint8Array(await f.arrayBuffer()));
    bankWhere = f.name;
    showBank();
    if (aud?.ready) await aud.loadBank(bank.bytes);
    render();
  } catch (e) {
    $("bankinfo").textContent = `${f.name}: ${e.message}`;
    $("bankinfo").className = "small bad";
  }
});

$("bankread").addEventListener("click", () => run(async () => {
  say("Looking for the FM-1…");
  const { dev } = await findDevice();
  try {
    if (dev.id.loader) throw new Error("the FM-1 is in update mode: finish the update first");
    const sl = new SongLink(dev.link);
    const { version } = await sl.ping();
    if (version < 5) throw new Error(`the FM-1 speaks version ${version} of the song link; the bank needs 5`);
    const idx = await sl.bankIndex();
    if (!idx) { say("The FM-1 has no sample bank: every SMPL zone on it is silent.", "bad"); return; }
    bank = idx;
    bankWhere = "the FM-1";
    showBank();
    render();
    say(`The FM-1's bank: ${idx.samples.length} samples, ${(idx.size / 1024).toFixed(0)} KB.`, "good");
  } finally { dev.link.close(); }
}));

// --------------------------------------------------------------- the song ---

function openText(text, name) {
  const { err, song: s, report } = trkLoad(text);
  if (err) {
    $("songinfo").textContent = "";
    $("songwarn").textContent = err === TRK_ERR_VERSION
      ? `${name}: no "version 3" line. Older files go through moondaw first.`
      : err === TRK_ERR_PATCH ? `${name}: a patch file, which this page does not apply.`
      : `${name}: could not be read (${err}).`;
    return;
  }
  song = s;
  fileName = name;
  loaded = trkSave(song);
  const used = instrumentsUsed(song);
  inst = song.instruments.findIndex((in_, i) => used[i] && in_.engine === ENGINE_SAMPLE);
  if (inst < 0) inst = used.indexOf(true);
  cursorZone = -1;
  $("sendname").value = name.replace(/\.trk$/i, "").toUpperCase().slice(0, 12);
  $("editor").classList.remove("hide");

  // What the FM-1's loader dropped: the page is the honest place to say it,
  // because the file on disk still has it and the file this page writes will
  // not. trkcheck prints the same counts.
  const lost = [];
  if (report.tracksInFile > 8) lost.push(`${report.tracksInFile} tracks in the file, 8 on the FM-1`);
  if (report.stepsCut) lost.push(`${report.stepsCut} steps on a track past the eighth`);
  if (report.stepsLost) lost.push(`${report.stepsLost} steps the pool had no room for`);
  if (report.zones) lost.push(`${report.zones} zone lines malformed or past the 128`);
  if (report.samples) lost.push(`${report.samples} smp lines past the table`);
  if (report.refLines) lost.push(`${report.refLines} reference-record lines`);
  if (report.previews) lost.push(`${report.previews} preview lines`);
  if (report.shaping) lost.push(`${report.shaping} instruments carrying drive/amp/tone`);
  if (report.syllables) lost.push(`${report.syllables} syllables the table had no room for`);
  if (report.silentInstr) lost.push(`${report.silentInstr} instruments on PLUCK, which the FM-1 names and does not play`);
  $("songwarn").textContent = lost.length
    ? "The FM-1 does not carry: " + lost.join("; ") + ". Saving from here drops them."
    : "";
  render();
}

async function openFile(f) {
  openText(await f.text(), f.name);
  if (song && aud?.ready) await reloadEngine();
}

$("file").addEventListener("change", () => { const f = $("file").files[0]; if (f) openFile(f); });
const drop = $("drop");
for (const ev of ["dragenter", "dragover"])
  drop.addEventListener(ev, (e) => { e.preventDefault(); drop.classList.add("over"); });
for (const ev of ["dragleave", "drop"])
  drop.addEventListener(ev, () => drop.classList.remove("over"));
drop.addEventListener("drop", (e) => {
  e.preventDefault();
  const f = e.dataTransfer.files[0];
  if (f) openFile(f);
});
window.addEventListener("dragover", (e) => e.preventDefault());
window.addEventListener("drop", (e) => {
  if (drop.contains(e.target)) return;
  e.preventDefault();
  const f = e.dataTransfer.files[0];
  if (f) openFile(f);
});
window.addEventListener("beforeunload", (e) => {
  if (song && trkSave(song) !== loaded) { e.preventDefault(); e.returnValue = ""; }
});

// ---------------------------------------------------------- the instruments ---

function renderSong() {
  const out = trkSave(song);
  const used = instrumentsUsed(song);
  const n = used.filter(Boolean).length;
  const smpl = used.filter((u, i) => u && song.instruments[i].engine === ENGINE_SAMPLE).length;
  // What the writer puts down, not what the table holds: a name no zone of a
  // played instrument reaches is dropped, and this line is about the file.
  const named = samplesUsed(song, used).filter(Boolean).length;
  const changed = out !== loaded;
  $("songinfo").innerHTML = `<strong>${fileName}</strong> · ${out.length} bytes as the FM-1 writes it · ` +
    `${n} instruments played (${smpl} SMPL) · ${song.nzones} zones · ${named} sample names · ` +
    `${song.bpm} BPM · ${song.arrangeLen} phrases` + (changed ? ' · <span class="bad">changed</span>' : " · unchanged");
  $("outtext").value = out;
  return out;
}

function renderInstList() {
  const used = instrumentsUsed(song);
  const box = $("instlist");
  box.innerHTML = "";
  for (let i = 0; i < SONG_MAX_INSTR; i++) {
    const in_ = song.instruments[i];
    // An instrument the file will not carry and nobody named: not worth a
    // button. One with a name or zones is shown even unused, so a song that
    // keeps a spare instrument does not look empty.
    const mine = zonesOf(song, i);
    if (!used[i] && !in_.name && !mine.length) continue;
    const b = document.createElement("button");
    b.className = (i === inst ? "on " : "") + (used[i] ? "" : "unused");
    b.textContent = `${hex2(i)} ${(in_.name || "—").padEnd(7, " ")} ${engineName(in_.engine)}` +
      (in_.engine === ENGINE_SAMPLE ? ` ${mine.length}z` : "");
    b.title = used[i] ? "played by a step: written to the file"
      : "no step plays it: the FM-1's writer leaves it out of the file";
    b.addEventListener("click", () => { inst = i; cursorZone = -1; render(); });
    box.append(b);
  }
}

// The fields, bound one way: the input's value goes through `set`, which
// clamps it as core/trk.c clamps, and then the page is drawn from the song.
function bindNum(id, get, set, { lo, hi }) {
  const el = $(id);
  const commit = () => {
    const v = parseInt(el.value, 10);
    if (!Number.isFinite(v)) { el.value = get(); return; }
    set(clamp(v, lo, hi));
    touched();
  };
  el.addEventListener("change", commit);
  el.addEventListener("blur", commit);
  return el;
}

function renderInst() {
  if (inst < 0 || !song) { $("inst").classList.add("hide"); return; }
  $("inst").classList.remove("hide");
  const in_ = song.instruments[inst];
  const used = instrumentsUsed(song)[inst];
  $("insthead").textContent = `4 · Instrument ${hex2(inst)}${in_.name ? " " + in_.name : ""}` +
    (used ? "" : " — not played by any step, so not written to the file");
  const set = (id, v) => { if ($(id) !== document.activeElement) $(id).value = v; };
  set("i-name", in_.name);
  $("i-engine").value = String(in_.engine);
  const tmax = engineTimbreMax(in_.engine);
  $("i-timbre").max = String(tmax);
  $("i-timbre").disabled = tmax === 0;
  $("i-timbre").value = in_.timbre;
  for (const [id, v] of [["i-gain", in_.gain], ["i-level", in_.level], ["i-attack", in_.attack],
    ["i-decay", in_.decay], ["i-sustain", in_.sustain], ["i-release", in_.release],
    ["i-cutoff", in_.cutoff], ["i-reso", in_.reso], ["i-filtenv", in_.filtEnv], ["i-filtvel", in_.filtVel]])
    set(id, v);
  // VIB_PHASE_FREE is -1 in the struct and the word "free" in the file: the
  // number keeps its last value under the box, so unticking it says something.
  $("i-vibfree").checked = in_.vibPhase === VIB_PHASE_FREE;
  $("i-vibphase").disabled = in_.vibPhase === VIB_PHASE_FREE;
  if (in_.vibPhase !== VIB_PHASE_FREE) set("i-vibphase", in_.vibPhase);
  $("voxrow").classList.toggle("hide", in_.engine !== ENGINE_VOX);
  set("i-tract", in_.tract || 100);
  $("i-accent").value = String(in_.accent);
  $("drumrow").classList.toggle("hide", in_.engine !== ENGINE_DRUM);
  for (let k = 0; k < 4; k++) set(`i-drum${k}`, in_.drum[k] + 64);
  $("zonebox").classList.toggle("hide", in_.engine !== ENGINE_SAMPLE);
}

$("i-name").addEventListener("input", () => {
  song.instruments[inst].name = $("i-name").value.slice(0, 7);
  touched();
});
$("i-engine").addEventListener("change", () => {
  const in_ = song.instruments[inst];
  in_.engine = parseInt($("i-engine").value, 10);
  // core/trk.c clamps the timbre to the engine on the way in, so a file
  // written here with a timbre the engine has not got would come back
  // changed: clamp it where it is set instead.
  in_.timbre = Math.min(in_.timbre, engineTimbreMax(in_.engine));
  touched();
});

// ------------------------------------------------------------- the zones ---

let ruler = null;

function renderZones() {
  const in_ = song.instruments[inst];
  if (in_.engine !== ENGINE_SAMPLE) return;
  const mine = zonesOf(song, inst);
  if (cursorZone < 0 || !mine.includes(cursorZone)) cursorZone = mine.length ? mine[0] : -1;
  ruler.show(song, inst, cursorZone);

  const tb = $("zonetable").querySelector("tbody");
  tb.innerHTML = "";
  mine.forEach((zi, row) => {
    const z = song.zones[zi];
    const name = song.smp[z.smp].name;
    const silent = !name || !bankHasName(name);
    const tr = document.createElement("tr");
    tr.className = (silent ? "silent " : "") + (zi === cursorZone ? "cursor" : "");
    tr.addEventListener("pointerdown", () => { cursorZone = zi; render(); });

    const td = (child) => { const c = document.createElement("td"); if (child) c.append(child); tr.append(c); return c; };
    td().textContent = String(row + 1);

    // The sample: the song's own table, plus whatever the bank has that the
    // table does not name yet -- choosing one takes a free slot of the table.
    const sel = document.createElement("select");
    const add = (value, label, group) => {
      const o = document.createElement("option");
      o.value = value;
      o.textContent = label;
      (group || sel).append(o);
    };
    for (let i = 0; i < SONG_SMP_MAX; i++)
      if (song.smp[i].name) add("s" + i, `${hex2(i)} ${song.smp[i].name}` + (bankHasName(song.smp[i].name) ? "" : " — not in the bank"));
    if (bank) {
      const g = document.createElement("optgroup");
      g.label = "from the bank";
      let any = false;
      for (const s of bank.samples)
        if (!song.smp.some((e) => e.name && bankFind(bank.samples, e.name) === bankFind(bank.samples, s.name))) {
          add("b" + s.name, s.name, g);
          any = true;
        }
      if (any) sel.append(g);
    }
    add("other", "another name…");
    sel.value = "s" + z.smp;
    if (silent) sel.className = "bad";
    sel.addEventListener("change", () => {
      const v = sel.value;
      // "sNN" an entry of the song's table; "b<name>" one of the bank's,
      // which takes a free entry; "other" a name typed in. Anything else is
      // not a choice this page offered, and changes nothing.
      let want = -1;
      if (v === "other") {
        const typed = prompt("The sample's file name, as the song carries it:", name);
        if (typed === null) { sel.value = "s" + z.smp; return; }
        want = smpIntern(song, typed.trim());
      } else if (v[0] === "s" && /^s\d+$/.test(v)) {
        want = parseInt(v.slice(1), 10);
      } else if (v[0] === "b") {
        want = smpIntern(song, v.slice(1));
      }
      if (want < 0 || want >= SONG_SMP_MAX) {
        sel.value = "s" + z.smp;
        if (v === "other" || v[0] === "b") alert("The song's sample table holds 64 names and is full.");
        return;
      }
      z.smp = want;
      touched();
    });
    td(sel);

    for (const f of ["lo", "hi", "root"]) {
      const inp = document.createElement("input");
      inp.className = "note";
      inp.value = noteName(z[f]);
      const commit = () => {
        const n = noteFromName(inp.value.trim().toUpperCase().padEnd(3, "-"));
        if (n < NOTE_MIN || n > NOTE_MAX) { inp.className = "note bad"; return; }
        z[f] = n;
        if (z.lo > z.hi) { if (f === "lo") z.hi = z.lo; else z.lo = z.hi; }
        touched();
      };
      inp.addEventListener("change", commit);
      inp.addEventListener("blur", commit);
      td(inp);
    }
    for (const [f, lo, hi] of [["tune", -100, 100], ["gain", -64, 24]]) {
      const inp = document.createElement("input");
      inp.className = "num";
      inp.type = "number";
      inp.min = String(lo);
      inp.max = String(hi);
      inp.value = z[f];
      const commit = () => {
        const v = parseInt(inp.value, 10);
        if (Number.isFinite(v)) { z[f] = clamp(v, lo, hi); touched(); } else inp.value = z[f];
      };
      inp.addEventListener("change", commit);
      inp.addEventListener("blur", commit);
      td(inp);
    }

    // What this zone really answers: its span, less what an earlier one takes.
    const shadow = zoneShadow(song, zi).length;
    const span = z.hi - z.lo + 1;
    const c = td();
    c.textContent = shadow ? `${span - shadow} of ${span}` : String(span);
    c.title = shadow ? `${shadow} of its keys are answered by an earlier zone and will never sound` : "";
    if (shadow) c.className = "bad";

    const ops = td();
    const op = (label, title, fn, on = true) => {
      const b = document.createElement("button");
      b.className = "tiny ghost";
      b.textContent = label;
      b.title = title;
      b.disabled = !on;
      b.addEventListener("click", (e) => { e.stopPropagation(); fn(); });
      ops.append(b, " ");
    };
    op("↑", "higher priority", () => { zoneMove(song, zi, mine[row - 1]); cursorZone = mine[row - 1]; touched(); }, row > 0);
    op("↓", "lower priority", () => { zoneMove(song, zi, mine[row + 1]); cursorZone = mine[row + 1]; touched(); }, row < mine.length - 1);
    op("✕", "remove this zone", () => { zoneRemove(song, zi); cursorZone = -1; touched(); });
    tb.append(tr);
  });

  const full = song.nzones >= SONG_ZONE_MAX;
  $("zoneadd").disabled = full;
  $("zonenote").textContent = full
    ? `The song holds ${SONG_ZONE_MAX} zones, which is all the FM-1 carries.`
    : mine.length ? `${song.nzones} of ${SONG_ZONE_MAX} zones in the song.`
      : "No zones: this instrument plays nothing. Add one over the whole keyboard for a one-shot.";
}


$("zoneadd").addEventListener("click", () => {
  // Over the whole keyboard and rooted at C-4: what a one-shot wants, and
  // what app/instrument.c makes on the machine, so the two agree.
  const at = zoneAdd(song, inst, { lo: NOTE_MIN, hi: NOTE_MAX, root: 60, smp: pickSample() });
  if (at === null) return;
  cursorZone = at;
  touched();
});

// A sample for a new zone: the first the song names, else the first of the
// bank, else slot 0 with no name -- which the writer leaves out, as it should.
function pickSample() {
  for (let i = 0; i < SONG_SMP_MAX; i++) if (song.smp[i].name) return i;
  if (bank?.samples.length) { const at = smpIntern(song, bank.samples[0].name); if (at >= 0) return at; }
  return 0;
}

// ------------------------------------------------------------- the layers ---

function renderLayers() {
  const in_ = song.instruments[inst];
  const tb = $("laytable").querySelector("tbody");
  tb.innerHTML = "";
  for (let l = 0; l < in_.nlayers; l++) {
    const ly = in_.layers[l];
    const tr = document.createElement("tr");
    const td = (child) => { const c = document.createElement("td"); if (child) c.append(child); tr.append(c); return c; };
    td().textContent = String(l + 1);
    const sel = document.createElement("select");
    ENGINE_NAMES.forEach((n, e) => {
      if (e === ENGINE_SAMPLE) return;  // the loader refuses a SMPL layer
      const o = document.createElement("option");
      o.value = String(e);
      o.textContent = n;
      sel.append(o);
    });
    sel.value = String(ly.engine);
    sel.addEventListener("change", () => {
      ly.engine = parseInt(sel.value, 10);
      ly.timbre = Math.min(ly.timbre, engineTimbreMax(ly.engine));
      touched();
    });
    td(sel);
    for (const [f, lo, hi] of [["timbre", 0, engineTimbreMax(ly.engine)], ["transpose", -48, 48],
      ["detune", -100, 100], ["gain", -64, 24]]) {
      const inp = document.createElement("input");
      inp.className = "num";
      inp.type = "number";
      inp.min = String(lo);
      inp.max = String(hi);
      inp.value = ly[f];
      const commit = () => {
        const v = parseInt(inp.value, 10);
        if (Number.isFinite(v)) { ly[f] = clamp(v, lo, hi); touched(); } else inp.value = ly[f];
      };
      inp.addEventListener("change", commit);
      inp.addEventListener("blur", commit);
      td(inp);
    }
    const b = document.createElement("button");
    b.className = "tiny ghost";
    b.textContent = "✕";
    b.title = "remove this layer";
    b.addEventListener("click", () => {
      in_.layers.splice(l, 1);
      in_.nlayers--;
      touched();
    });
    td(b);
    tb.append(tr);
  }
  $("layadd").disabled = in_.nlayers >= INSTR_MAX_LAYERS;
}

$("layadd").addEventListener("click", () => {
  const in_ = song.instruments[inst];
  if (in_.nlayers >= INSTR_MAX_LAYERS) return;
  in_.layers[in_.nlayers++] = { engine: 0, timbre: 0, transpose: 0, detune: 0, gain: 0 };
  touched();
});

// ------------------------------------------------------------ the audition ---

function audSay(text, cls = "small") { $("audinfo").textContent = text; $("audinfo").className = cls; }

let reloadTimer = null;
async function reloadEngine() {
  if (!aud?.ready || !song) return;
  const wasPlaying = aud.state.playing;
  const entry = aud.state.entry;
  const ok = await aud.loadSong(trkSave(song));
  if (!ok) { audSay("The engine refused the song.", "small bad"); return; }
  if (wasPlaying) aud.playSong(entry);
}
const reloadSoon = () => {
  clearTimeout(reloadTimer);
  reloadTimer = setTimeout(reloadEngine, 200);
};

$("start").addEventListener("click", async () => {
  $("start").disabled = true;
  try {
    aud = new Audition({ wasmUrl: "../../build/songedit/audition.wasm", workletUrl: "./worklet.js" });
    aud.onState = (m) => {
      $("stop").disabled = !m.playing;
      audSay(m.playing ? `Playing: phrase ${hex2(m.entry)}, step ${hex2(m.row)}.` : "Stopped.");
    };
    aud.onError = (e) => audSay(e.message, "small bad");
    await aud.start();
    if (bank?.bytes) await aud.loadBank(bank.bytes);
    await aud.loadSong(trkSave(song));
    aud.master(parseInt($("master").value, 10));
    $("start").textContent = "Engine running";
    for (const id of ["playnote", "playsong"]) $(id).disabled = false;
    audSay(bank?.bytes
      ? `The FM-1's core, ${aud.bankCount} samples mounted.`
      : "The FM-1's core is running. No packed bank is loaded, so SMPL instruments are silent — as they would be on a device with an empty bank.");
  } catch (e) {
    $("start").disabled = false;
    audSay(e.message, "small bad");
  }
});

const audNote = () => {
  const n = noteFromName($("audnote").value.trim().toUpperCase().padEnd(3, "-"));
  return n >= NOTE_MIN && n <= NOTE_MAX ? n : 60;
};
$("playnote").addEventListener("pointerdown", () => playNote(audNote()));
$("playnote").addEventListener("pointerup", () => aud?.noteOff());
$("playsong").addEventListener("click", () => { aud?.playSong(0); $("stop").disabled = false; });
$("stop").addEventListener("click", () => { aud?.stop(); aud?.noteOff(); });
$("master").addEventListener("input", () => aud?.master(parseInt($("master").value, 10)));

function playNote(note) {
  if (!aud?.ready) { audSay("Start the engine first (section 7).", "small bad"); return; }
  const in_ = song.instruments[inst];
  aud.noteOn(inst, note, in_.engine === ENGINE_VOX ? "LA" : "");
  if (in_.engine === ENGINE_SAMPLE) {
    const mine = zonesOf(song, inst);
    const hit = mine.find((i) => note >= song.zones[i].lo && note <= song.zones[i].hi);
    if (hit === undefined) audSay(`${noteName(note)}: no zone holds it — silence.`, "small bad");
    else {
      const name = song.smp[song.zones[hit].smp].name;
      const ok = name && bankHasName(name);
      audSay(`${noteName(note)} → zone ${mine.indexOf(hit) + 1}, ${name || "no name"}` +
        (ok ? "" : " — the bank has not got it, so it is silent"), ok ? "small" : "small bad");
    }
  } else if (in_.engine === ENGINE_PLUCK) {
    audSay("PLUCK is named by the file and not played by the FM-1: silence.", "small bad");
  }
}

// ---------------------------------------------------------------- the out ---

function say(text, cls = "small") { $("out").textContent = text; $("out").className = cls; }

$("download").addEventListener("click", () => {
  const text = trkSave(song);
  const url = URL.createObjectURL(new Blob([text], { type: "text/plain" }));
  const a = document.createElement("a");
  a.href = url;
  a.download = fileName.replace(/\.trk$/i, "") + ".trk";
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
  loaded = text;
  say(`${a.download}: ${text.length} bytes, as the FM-1 writes it.`, "small good");
  render();
});

async function access() {
  if (midi) return midi;
  if (!navigator.requestMIDIAccess)
    throw new Error("This browser has no WebMIDI. Open the page in Chrome or Edge on a computer.");
  try { midi = await navigator.requestMIDIAccess({ sysex: true }); }
  catch (_) { throw new Error("MIDI with SysEx was not allowed. Allow MIDI for this site (the icon in the address bar), then try again."); }
  return midi;
}

async function findDevice() {
  const up = new Updater(await access());
  const dev = await up.find();
  if (!dev) {
    const names = [...midi.inputs.values()].map((i) => i.name).filter(Boolean);
    throw new Error("FM-1 not found. Check that it is on, that the cable carries data, and that no other app uses MIDI. " +
      `MIDI inputs seen: ${names.length ? names.join(", ") : "none"}.`);
  }
  return { up, dev };
}

async function run(fn) {
  if (busy) return;
  busy = true;
  for (const id of ["send", "bankread", "download"]) $(id).disabled = true;
  try { await fn(); } catch (e) { say(e.message || String(e), "small bad"); } finally {
    busy = false;
    for (const id of ["send", "bankread", "download"]) $(id).disabled = false;
  }
}

$("send").addEventListener("click", () => run(async () => {
  const text = trkSave(song);
  const data = new TextEncoder().encode(text);
  const name = ($("sendname").value || "SONG").toUpperCase().slice(0, 12);
  if (!confirm(`Send ${name} (${data.length} bytes) to the FM-1?\n\n` +
      (($("replace").checked) ? "A song of that name on the device will be replaced.\n\n" : "") +
      "Put the FM-1 on its MENU screen first: it refuses a transfer anywhere else."))
    return;
  say("Looking for the FM-1…");
  $("bar").value = 0;
  const { dev } = await findDevice();
  try {
    if (dev.id.loader) throw new Error("the FM-1 is in update mode: finish the update first");
    const sl = new SongLink(dev.link);
    const { version } = await sl.ping();
    if (version < 1) throw new Error(`the FM-1 speaks version ${version} of the song link`);
    const got = await sl.put(data, name, { replace: $("replace").checked }, (n) => {
      $("bar").value = (n * 100) / data.length;
      say(`Sending ${name}… ${Math.floor((n * 100) / data.length)} %`);
    });
    $("bar").value = 100;
    loaded = text;
    say(`${name}: ${got} bytes on the FM-1.`, "small good");
    render();
  } finally { dev.link.close(); }
}));

// ------------------------------------------------------------------ render ---

// One way: the song changed, so draw the page from it.
function render() {
  if (!song) return;
  renderSong();
  renderInstList();
  renderInst();
  if (inst >= 0) {
    if (song.instruments[inst].engine === ENGINE_SAMPLE) renderZones();
    renderLayers();
  }
}

// An edit: redraw, and tell the engine, so what is heard is what was changed.
function touched() {
  render();
  reloadSoon();
}

(function boot() {
  const sel = $("i-engine");
  ENGINE_NAMES.forEach((n, e) => {
    const o = document.createElement("option");
    o.value = String(e);
    o.textContent = n + (e === ENGINE_PLUCK ? " (named, silent)" : "");
    sel.append(o);
  });
  for (const [id, f, lo, hi] of [
    ["i-timbre", "timbre", 0, 9], ["i-gain", "gain", -64, 24], ["i-level", "level", -64, 24],
    ["i-attack", "attack", 0, 255], ["i-decay", "decay", 0, 255], ["i-sustain", "sustain", 0, 255],
    ["i-release", "release", 0, 255], ["i-cutoff", "cutoff", 0, 255], ["i-reso", "reso", 0, 127],
    ["i-filtenv", "filtEnv", 0, 127], ["i-filtvel", "filtVel", 0, 127], ["i-tract", "tract", 50, 150],
  ]) bindNum(id, () => song.instruments[inst][f], (v) => {
    song.instruments[inst][f] = f === "timbre" ? Math.min(v, engineTimbreMax(song.instruments[inst].engine)) : v;
  }, { lo, hi });
  for (let k = 0; k < 4; k++)
    bindNum(`i-drum${k}`, () => song.instruments[inst].drum[k] + 64,
      (v) => { song.instruments[inst].drum[k] = v - 64; }, { lo: 0, hi: 127 });
  bindNum("i-vibphase", () => song.instruments[inst].vibPhase,
    (v) => { song.instruments[inst].vibPhase = v; }, { lo: 0, hi: 255 });
  $("i-vibfree").addEventListener("change", () => {
    const in_ = song.instruments[inst];
    in_.vibPhase = $("i-vibfree").checked ? VIB_PHASE_FREE : clamp(parseInt($("i-vibphase").value, 10) || 0, 0, 255);
    touched();
  });
  $("i-accent").addEventListener("change", () => {
    song.instruments[inst].accent = parseInt($("i-accent").value, 10);
    touched();
  });

  // ?song=URL and ?bank=URL open a file the page is served beside, which is
  // how the demo link below works and how a browser can be pointed straight
  // at a song. Same-origin only: a cross-origin fetch is refused anyway.
  const q = new URLSearchParams(location.search);
  if (q.get("bank"))
    fetch(q.get("bank")).then((r) => (r.ok ? r.arrayBuffer() : Promise.reject(new Error(`HTTP ${r.status}`))))
      .then(async (b) => {
        bank = bankFromFile(new Uint8Array(b));
        bankWhere = q.get("bank").split("/").pop();
        showBank();
        if (aud?.ready) await aud.loadBank(bank.bytes);
        if (song) render();
      })
      .catch((e) => { $("bankinfo").textContent = `${q.get("bank")}: ${e.message}`; $("bankinfo").className = "small bad"; });
  if (q.get("song"))
    fetch(q.get("song")).then((r) => (r.ok ? r.text() : Promise.reject(new Error(`HTTP ${r.status}`))))
      .then((t) => openText(t, q.get("song").split("/").pop()))
      .catch((e) => { $("songwarn").textContent = `${q.get("song")}: ${e.message}`; });

  ruler = new ZoneRuler($("ruler"), {
    bankHas,
    onZones: () => { renderSong(); reloadSoon(); renderZonesTableOnly(); },
    onCursor: (i) => { cursorZone = i; render(); },
    onNote: (n) => playNote(n),
    onNoteOff: () => aud?.noteOff(),
  });
  showBank();
})();

// While a handle is dragged the table has to follow, but rebuilding it would
// take the pointer's capture away from the canvas: only the cells change.
function renderZonesTableOnly() {
  const mine = zonesOf(song, inst);
  const rows = $("zonetable").querySelectorAll("tbody tr");
  mine.forEach((zi, row) => {
    const tr = rows[row];
    if (!tr) return;
    const z = song.zones[zi];
    const ins = tr.querySelectorAll("input.note");
    ins[0].value = noteName(z.lo);
    ins[1].value = noteName(z.hi);
    ins[2].value = noteName(z.root);
    const shadow = zoneShadow(song, zi).length;
    const span = z.hi - z.lo + 1;
    const c = tr.children[7];
    c.textContent = shadow ? `${span - shadow} of ${span}` : String(span);
    c.className = shadow ? "bad" : "";
  });
}
