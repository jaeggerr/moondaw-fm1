#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Writing drums without reading note names (30/10): three ways the same bar
can sit on the panel, to be chosen on the panel.

A kit's note already names a drum in the firmware -- core/drum.c's
drum_note_abbr(), written and never called. The names here are read from
that table, so the mockup cannot drift from the machine.

    A  NAMED STRIP   today's NOTES view, 4 slots x 8 steps, but a kit's
                     cell reads the drum (BD SD CH) instead of its pitch
                     (C-2 D-2 F#2). Amber: a note off the drum's design
                     pitch, which the name alone cannot tell.
    B  DRUM GRID     the 8 lanes down the side, one bar across. The row
                     IS the drum, so there is no name to read.
    C  DRUM GRID 2   the same, two bars at once, grouped in fours.

    KNOB 1 / SELECT  A, B, C
    KNOB 2           the step; KNOB 3 the slot (A) or the lane (B, C)
    a key            puts the lane under the cursor on that step, or takes
                     it off (B, C). A step holds four notes and no more:
                     the fifth has nowhere to go and the corner says so.
    GLO / HOME       back

    python3 tools/protos/drum_names.py          # the FM-1 on USB-MIDI
    python3 tools/protos/drum_names.py --udp    # the simulator
"""
import argparse
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from fm1_proto import Canvas  # noqa: E402
import fm1_send as link  # noqa: E402

ROW, HEAD, NUM_X, CELL_X, RIGHT = 28, 16, 14, 50, 234  # app/grid.h
ROWS, SLOT_W = 8, 38                                   # a note cell in PATTERN
ON_ACC = "#0B1409"                                     # C_ON_ACC, drawn on the cursor


def lane_names():
    """The 8 lanes' names, out of core/drum.c's own table."""
    src = (link.ROOT / "core" / "drum.c").read_text()
    body = src[src.index("N[DV_NLANE]"):]
    return re.findall(r'"([A-Z]{2})"', body[:body.index("}")])


def advances():
    """CONDENSED 20's advance per character, out of app/fonts.h's glyph table:
    the cells are centred here as gfx_text_mid centres them."""
    src = (link.ROOT / "app" / "fonts.h").read_text()
    body = src[src.index("f_cond20_g"):]
    body = body[body.index("{") + 1:body.index("};")]
    adv = [int(g.split(",")[3]) for g in re.findall(r"\{([^{}]+)\}", body)]
    return {chr(32 + i): a for i, a in enumerate(adv)}


ADV = {}


def text_w(s):
    return sum(ADV.get(c, 9) for c in s)


# --- the bar: which steps each lane hits, and the ones off design pitch ---
# Two bars of 16. Step 16 is deliberately full: BD CH TM CB, four notes, the
# wall a step runs into.
HITS = {
    0: [0, 6, 10, 16, 22, 26, 29],                                    # BD
    1: [4, 12, 20, 28],                                               # SD
    2: [12, 28],                                                      # CP
    3: [0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30],   # CH
    4: [14, 30],                                                      # OH
    5: [11, 16, 27],                                                  # TM
    6: [19],                                                          # RS
    7: [16, 24],                                                      # CB
}
TUNED = {(5, 11), (5, 27)}  # a tom put up a few semitones: the name cannot say it
STEPS = 32


def lanes_of(step):
    """The lanes hitting that step, in lane order -- what the step's four note
    slots hold, packed as the firmware packs them."""
    return [ln for ln in range(8) if step in HITS[ln]]


def grid_y(v):
    return HEAD + v * ROW


def base(v):
    return grid_y(v) + 22  # grid_base: +2 and CONDENSED 20's ascent


def corner(c, left, right, right_ink="white"):
    """The header's two ends, as grid_corner draws them: where one is on the
    left, what the view is on the right. CONDENSED 12 is near enough 6 px a
    character to set the right edge by."""
    c.text(4, 12, left, "cyan", "12")
    c.text(RIGHT - len(right) * 6, 12, right, right_ink, "12")


def frame_rect(c, x, y, w, h, colour):
    c.fill(x, y, w, 1, colour).fill(x, y + h - 1, w, 1, colour)
    c.fill(x, y, 1, h, colour).fill(x + w - 1, y, 1, h, colour)


# ------------------------------------------------------------ A: the strip ---
def draw_strip(c, st, lane, full):
    top = max(0, min(st - 3, STEPS - ROWS))
    corner(c, "01  A", "FULL" if full else "DRUM", "red" if full else "white")
    for s in range(4):
        h = str(s + 1)
        c.text(CELL_X + s * SLOT_W + (SLOT_W - text_w(h)) // 2, 12, h,
               "white" if s == lane else "grey", "12")
    for v in range(ROWS):
        step = top + v
        if step >= STEPS:
            break
        here = lanes_of(step)
        c.text(NUM_X, base(v), "%02X" % step, "white" if step % 4 == 0 else "grey")
        for s in range(4):
            on = step == st and s == lane
            if on:
                c.fill(CELL_X + s * SLOT_W + 1, grid_y(v) + 3, SLOT_W - 2, ROW - 6, "green")
            name = "---"
            ink = "grey"
            if s < len(here):
                ln = here[s]
                tuned = (ln, step) in TUNED
                name = NAMES[ln] + ("+" if tuned else "")
                ink = "amber" if tuned else "white"
            if on:
                ink = ON_ACC
            c.text(CELL_X + s * SLOT_W + (SLOT_W - text_w(name)) // 2, base(v), name, ink)


# ------------------------------------------------------------- B, C: a grid ---
def draw_grid(c, st, lane, full, bars):
    """bars 1: 16 steps across. bars 2: 32, in groups of four."""
    n = 16 * bars
    first = 0 if bars == 2 else (st // 16) * 16
    if bars == 1:
        pitch, cw, x0, gap = 11, 9, 52, 0
    else:
        pitch, cw, x0, gap = 5, 4, 52, 3
    at = lambda s: x0 + (s // 4) * (4 * pitch + gap) + (s % 4) * pitch

    # The view's name is short on purpose: the panel showed "DRUM 1 BAR"
    # starting at x 174 and the fourth beat's number sitting at 184, under it.
    # The left corner already carries the variant's letter.
    corner(c, "01  %s" % ("B" if bars == 1 else "C"),
           "FULL" if full else ("1 BAR" if bars == 1 else "2 BARS"),
           "red" if full else "white")
    for s in range(n):
        if s % 4:
            continue
        mark = str(s // 4 + 1) if bars == 1 else (str(s // 16 + 1) if s % 16 == 0 else "")
        if mark:
            c.text(at(s), 12, mark, "grey", "12")
        c.fill(at(s) - 2, HEAD, 1, ROWS * ROW, "#282828")  # C_TRACK: the beat
    for ln in range(8):
        y = grid_y(ln)
        c.text(4, base(ln), NAMES[ln], "white" if ln == lane else "grey")
        for s in range(n):
            step = first + s
            x, hit = at(s), step in HITS[ln]
            if ln == lane and step == st:
                frame_rect(c, x - 1, y + 5, cw + 2, 18, "green")
            if hit:
                tuned = (ln, step) in TUNED
                c.fill(x, y + 7, cw, 14, "amber" if tuned else "white")
            else:
                c.fill(x, y + 13, cw, 2, "#282828")


NAMES = []
VIEWS = ("A", "B", "C")


def main():
    global ADV, NAMES
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--udp", nargs="?", const=19780, type=int)
    a = ap.parse_args()
    ADV, NAMES = advances(), lane_names()
    if len(NAMES) != 8:
        sys.exit("core/drum.c's N[DV_NLANE] is not eight names any more: %r" % (NAMES,))
    c = Canvas(udp=a.udp)
    state = {"view": 1, "step": 0, "lane": 0, "full": False}

    def frame(ev):
        state["view"] = max(0, min(state["view"] + ev.turns.get("SELECT", 0)
                                   + ev.turns.get("K1", 0), len(VIEWS) - 1))
        state["step"] = max(0, min(state["step"] + ev.turns.get("K2", 0), STEPS - 1))
        top = 3 if state["view"] == 0 else 7
        state["lane"] = max(0, min(state["lane"] + ev.turns.get("K3", 0), top))
        if ev.keys_down and state["view"]:  # a key writes in the grid
            ln, s = state["lane"], state["step"]
            state["full"] = False
            if s in HITS[ln]:
                HITS[ln].remove(s)
            elif len(lanes_of(s)) >= 4:
                state["full"] = True  # four notes already: the fifth has nowhere to go
            else:
                HITS[ln].append(s)
        c.clear("black")
        if state["view"] == 0:
            draw_strip(c, state["step"], min(state["lane"], 3), state["full"])
        else:
            draw_grid(c, state["step"], state["lane"], state["full"], state["view"])
        c.show()

    c.run(frame)


if __name__ == "__main__":
    main()
