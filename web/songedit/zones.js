// SPDX-License-Identifier: GPL-3.0-only
//
// The zone map: the one thing the 240x240 panel can never show well and a
// browser can. A SMPL instrument's zones drawn over the keyboard, so where
// each sample answers -- and where it never will -- is read at a glance
// instead of counted out of a column of note names.
//
// After moondaw's web/ui/zones.js, which settled what has to be on the
// picture: the C of each octave so a range is placed without counting, every
// zone a block, the cursor's own block picked out, the root marked on it, LO
// and HI as handles one can drag, and -- the point of the whole drawing --
// the keys an EARLIER zone answers first, which are the part of a zone that
// will never sound. The FM-1's model is its own, so none of that code is
// here; the lessons are.
//
// Three bands, bottom to top: the keyboard, which plays; the zones; and the
// handles with the octave labels.

import { NOTE_MIN, NOTE_MAX, noteName, zonesOf, zoneShadow } from "./trk.js";

const SPAN = NOTE_MAX + 1 - NOTE_MIN; // 116 keys: C-0 to G-9, what a file writes
const BLACK = [0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0];
const TOP = 15;      // the handle tabs and the octave labels
const KEYS = 26;     // the keyboard at the foot
const GRAB = 7;      // pixels within which a press takes a handle

const isBlack = (n) => BLACK[n % 12] === 1;
const clampx = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

export class ZoneRuler {
  // canvas: the <canvas>. hooks: {onZones, onCursor, onNote, bankHas}
  //   onZones()        the zone list changed (redraw the table, mark dirty)
  //   onCursor(i)      another zone is under the cursor
  //   onNote(note)     a key was pressed: play it on this instrument
  //   bankHas(smpIdx)  false when the bank has not got that sample: the
  //                    block is drawn hollow, because it is silent
  constructor(canvas, hooks = {}) {
    this.cv = canvas;
    this.hooks = hooks;
    this.song = null;
    this.inst = -1;
    this.cursor = -1;   // an index into song.zones, not a row of this instrument
    this.held = -1;     // the note the pointer is holding down
    this.drag = null;
    const ro = new ResizeObserver(() => this.draw());
    ro.observe(canvas);
    this.wire();
  }

  show(song, inst, cursor) {
    this.song = song;
    this.inst = inst;
    const mine = song ? zonesOf(song, inst) : [];
    this.cursor = cursor !== undefined ? cursor : (mine.length ? mine[0] : -1);
    this.draw();
  }

  // --------------------------------------------------------- the pointer ---

  geom() {
    const W = this.cv.clientWidth, H = this.cv.clientHeight;
    return { W, H, x: (n) => ((n - NOTE_MIN) / SPAN) * W, keyTop: H - KEYS };
  }

  noteAt(px) {
    const { W } = this.geom();
    return Math.max(NOTE_MIN, Math.min(NOTE_MAX, NOTE_MIN + Math.floor((px / W) * SPAN)));
  }

  wire() {
    const local = (e) => {
      const r = this.cv.getBoundingClientRect();
      return [e.clientX - r.left, e.clientY - r.top];
    };
    this.cv.addEventListener("pointerdown", (e) => {
      if (!this.song || this.inst < 0) return;
      const [px, py] = local(e);
      const g = this.geom();
      this.cv.setPointerCapture(e.pointerId);
      // The keyboard at the foot plays; it never moves a zone.
      if (py >= g.keyTop) {
        this.held = this.noteAt(px);
        this.drag = { kind: "key" };
        this.hooks.onNote?.(this.held);
        this.draw();
        return;
      }
      const z = this.cursor >= 0 ? this.song.zones[this.cursor] : null;
      if (z && z.inst === this.inst) {
        const xl = g.x(z.lo), xh = g.x(z.hi + 1);
        if (Math.abs(px - xl) <= GRAB) { this.drag = { kind: "lo" }; return; }
        if (Math.abs(px - xh) <= GRAB) { this.drag = { kind: "hi" }; return; }
      }
      // Anywhere else: the zone under the press becomes the cursor's.
      const n = this.noteAt(px);
      for (const i of zonesOf(this.song, this.inst)) {
        const o = this.song.zones[i];
        if (n >= o.lo && n <= o.hi) {
          this.cursor = i;
          this.hooks.onCursor?.(i);
          this.draw();
          return;
        }
      }
    });
    this.cv.addEventListener("pointermove", (e) => {
      if (!this.drag) return;
      const [px, py] = local(e);
      const n = this.noteAt(px);
      if (this.drag.kind === "key") {
        if (n !== this.held && py >= this.geom().keyTop) {
          this.held = n;
          this.hooks.onNote?.(n);
          this.draw();
        }
        return;
      }
      const z = this.song.zones[this.cursor];
      if (this.drag.kind === "lo" && n !== z.lo) {
        z.lo = Math.min(n, z.hi);
        this.hooks.onZones?.();
        this.draw();
      } else if (this.drag.kind === "hi" && n !== z.hi) {
        z.hi = Math.max(n, z.lo);
        this.hooks.onZones?.();
        this.draw();
      }
    });
    const up = () => {
      if (this.drag?.kind === "key") { this.held = -1; this.hooks.onNoteOff?.(); this.draw(); }
      this.drag = null;
    };
    this.cv.addEventListener("pointerup", up);
    this.cv.addEventListener("pointercancel", up);
  }

  // ---------------------------------------------------------- the drawing ---

  css(name) { return getComputedStyle(this.cv).getPropertyValue(name).trim(); }

  draw() {
    const cv = this.cv;
    const { W, H, x, keyTop } = this.geom();
    if (W <= 0 || H <= 0) return;
    const dpr = window.devicePixelRatio || 1;
    cv.width = Math.round(W * dpr);
    cv.height = Math.round(H * dpr);
    const c = cv.getContext("2d");
    c.setTransform(dpr, 0, 0, dpr, 0, 0);
    c.clearRect(0, 0, W, H);

    const fg = this.css("--fg") || "#000";
    const dim = this.css("--dim") || "#777";
    const line = this.css("--line") || "#ddd";
    const bad = this.css("--bad") || "#b00020";
    const bg = this.css("--bg") || "#fff";
    const kbw = this.css("--kbw") || "#fff";
    const kbb = this.css("--kbb") || "#333";
    const band = { top: TOP, h: keyTop - TOP - 2 };
    const mine = this.song ? zonesOf(this.song, this.inst) : [];
    const cur = this.song && this.cursor >= 0 && this.song.zones[this.cursor]?.inst === this.inst
      ? this.song.zones[this.cursor] : null;

    // The keyboard first, under everything: the black keys shaded, so the
    // ruler reads as a keyboard and a range lands on a key one can name.
    for (let n = NOTE_MIN; n <= NOTE_MAX; n++) {
      const x0 = x(n), w = Math.max(1, x(n + 1) - x(n));
      c.fillStyle = n === this.held ? bad : isBlack(n) ? kbb : kbw;
      c.fillRect(x0, keyTop, Math.max(1, w - 1), KEYS);
    }
    c.fillStyle = line;
    c.fillRect(0, keyTop, W, 1);

    // The C of each octave, so a range is placed without counting. The label
    // is the FM-1's own spelling: C-4 is note 60, as note_name() writes it.
    c.font = '10px ui-monospace, Menlo, monospace';
    c.textAlign = "left";
    c.textBaseline = "top";
    for (let n = NOTE_MIN; n <= NOTE_MAX; n += 12) {
      const xo = Math.round(x(n));
      c.fillStyle = line;
      c.fillRect(xo, 0, 1, keyTop);
      c.fillStyle = dim;
      c.fillText(noteName(n), xo + 3, 2);
    }

    // Every other zone of this instrument a plain block, with a hairline of
    // ground between neighbours so a keymap with no gap still reads as
    // several. A zone whose sample the bank has not got is drawn hollow: it
    // is silent on the device, and the page's one job is to say so.
    const blockOf = (z) => [x(z.lo) + 1, Math.max(1, x(z.hi + 1) - x(z.lo) - 2)];
    for (const i of mine) {
      const z = this.song.zones[i];
      if (z === cur) continue;
      const [x0, w] = blockOf(z);
      const have = this.hooks.bankHas ? this.hooks.bankHas(z.smp) : true;
      if (have) {
        c.fillStyle = dim;
        c.globalAlpha = 0.22;
        c.fillRect(x0, band.top, w, band.h);
        c.globalAlpha = 1;
      } else {
        c.setLineDash([3, 3]);
      }
      c.strokeStyle = dim;
      c.lineWidth = 1;
      c.strokeRect(x0 + 0.5, band.top + 0.5, Math.max(1, w - 1), band.h - 1);
      c.setLineDash([]);
    }
    // The cursor's, solid: it is the whole reason the picture is here --
    // where THIS zone reaches.
    if (cur) {
      const [x0, w] = blockOf(cur);
      const have = this.hooks.bankHas ? this.hooks.bankHas(cur.smp) : true;
      if (have) {
        c.fillStyle = dim;
        c.globalAlpha = 0.7;
        c.fillRect(x0, band.top, w, band.h);
        c.globalAlpha = 1;
      } else {
        c.setLineDash([3, 3]);
      }
      c.strokeStyle = fg;
      c.lineWidth = 2;
      c.strokeRect(x0 + 1, band.top + 1, Math.max(1, w - 2), band.h - 2);
      c.setLineDash([]);
      c.lineWidth = 1;
    }

    // The shadows last, over everything -- including the cursor's own block,
    // whose dead keys are exactly what one wants to see while moving it.
    for (const i of mine)
      for (const n of zoneShadow(this.song, i)) {
        c.fillStyle = bad;
        c.globalAlpha = 0.55;
        c.fillRect(x(n), band.top, Math.max(1, x(n + 1) - x(n)), band.h);
        c.globalAlpha = 1;
      }

    if (!cur) {
      c.fillStyle = dim;
      c.textAlign = "center";
      c.textBaseline = "middle";
      c.fillText(mine.length ? "no zone under the cursor" : "no zones: this instrument is silent",
        W / 2, band.top + band.h / 2);
      return;
    }

    // The root, a line with a head on the block: the note the sample sounds
    // as recorded. A head, so it is not read as a third handle.
    const rx = Math.round((x(cur.root) + x(cur.root + 1)) / 2);
    c.fillStyle = fg;
    c.fillRect(rx - 1, band.top, 2, band.h);
    c.beginPath();
    c.moveTo(rx - 4, band.top);
    c.lineTo(rx + 4, band.top);
    c.lineTo(rx, band.top + 6);
    c.closePath();
    c.fill();
    c.font = 'bold 9px ui-monospace, Menlo, monospace';
    c.textAlign = "center";
    c.textBaseline = "bottom";
    c.fillText(`ROOT ${noteName(cur.root)}`, clampx(rx, 34, W - 34), band.top + band.h - 3);

    // The handles: a line the width of the band, and a tab to grab.
    const xl = Math.round(x(cur.lo)), xh = Math.round(x(cur.hi + 1));
    c.textBaseline = "middle";
    for (const [xx, lab, dx] of [[xl, `LO ${noteName(cur.lo)}`, -47], [xh, `HI ${noteName(cur.hi)}`, 1]]) {
      c.fillStyle = fg;
      c.fillRect(xx - 1, 0, 2, keyTop);
      const tx = clampx(xx + dx, 0, W - 46);
      c.fillRect(tx, 0, 46, TOP - 2);
      c.fillStyle = bg;
      c.fillText(lab, tx + 23, (TOP - 2) / 2);
    }
  }
}

