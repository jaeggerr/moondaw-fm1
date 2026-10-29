#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Two ways to select steps in PATTERN, to copy, paste and clear them, to try by
hand (29/10). PRESETS turns from one to the other; the block and the clipboard
are the same in both. The buttons PATTERN leaves free: EDIT, LFO (and ARP, REC).

  A  KNOB 4   turning KNOB 4 selects steps from the cursor down, whole steps
              (all four notes and the rest); back to 0, none
  B  EDIT     EDIT drops an anchor where the cursor is; SELECT and KNOB 1 then
              stretch a rectangle from it, steps and columns; GLO lets it go

In both: EDIT on a selection COPIES it (and lets it go), ARP PASTES what was
copied at the cursor, LFO CLEARS the selection (or the step under the cursor).
SELECT moves, KNOB 1 the column, as in PATTERN. HOME leaves.

    python3 tools/protos/selection.py            # the FM-1 on USB-MIDI
    python3 tools/protos/selection.py --udp      # the simulator
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from fm1_proto import Canvas  # noqa: E402

ROW, ROWS, HEAD, NUM_X, CELL_X, CELL_W = 28, 7, 16, 14, 50, 38
ON_ACC, SEL_INK, GROOVE = "#0B1409", "#6E5518", "#282828"
LEN = 16
# a block: four note slots a step, "---" empty
block = [["---"] * 4 for _ in range(LEN)]
for r, n in {0: "C-4", 2: "E-4", 4: "G-4", 6: "E-4", 8: "C-4", 9: "D-4", 10: "E-4", 12: "G-4"}.items():
    block[r][0] = n
block[0][1], block[8][1] = "E-4", "G-4"

S = {"proto": 0, "row": 0, "top": 0, "col": 0, "sel": 0, "anchor": None, "clip": None, "said": ""}


def selection():
    """The selected rectangle (r0, r1, c0, c1), inclusive, or None."""
    if S["proto"] == 0:
        if not S["sel"]:
            return None
        return S["row"], min(LEN - 1, S["row"] + S["sel"] - 1), 0, 3
    if S["anchor"] is None:
        return None
    ar, ac = S["anchor"]
    return min(ar, S["row"]), max(ar, S["row"]), min(ac, S["col"]), max(ac, S["col"])


def hands(ev):
    p = S["proto"]
    S["row"] = max(0, min(LEN - 1, S["row"] + ev.turns.get("SELECT", 0)))
    S["col"] = max(0, min(3, S["col"] + ev.turns.get("K1", 0)))
    if p == 0 and "K4" in ev.turns:
        S["sel"] = max(0, min(LEN - S["row"], S["sel"] + ev.turns["K4"]))
    if p == 0:
        S["sel"] = min(S["sel"], LEN - S["row"])
    sel = selection()
    if "EDIT" in ev.pressed:
        if sel:  # copy, and let the selection go
            r0, r1, c0, c1 = sel
            S["clip"] = [block[r][c0:c1 + 1] for r in range(r0, r1 + 1)]
            S["said"] = f"COPIED {r1 - r0 + 1}"
            S["sel"], S["anchor"] = 0, None
        elif p == 1:
            S["anchor"] = (S["row"], S["col"])
    if "ARP" in ev.pressed and S["clip"]:  # paste at the cursor: from its column (B) or the first (A)
        c0 = S["col"] if p == 1 else 0
        for i, line in enumerate(S["clip"]):
            for j, v in enumerate(line):
                if S["row"] + i < LEN and c0 + j < 4:
                    block[S["row"] + i][c0 + j] = v
        S["said"] = f"PASTED {len(S['clip'])}"
    if "LFO" in ev.pressed:
        r0, r1, c0, c1 = sel or (S["row"], S["row"], S["col"], S["col"])
        for r in range(r0, r1 + 1):
            for c in range(c0, c1 + 1):
                block[r][c] = "---"
        S["said"] = f"CLEARED {r1 - r0 + 1}"
        S["sel"], S["anchor"] = 0, None
    if "GLO" in ev.pressed:
        S["sel"], S["anchor"] = 0, None
    S["top"] = min(max(S["top"], S["row"] - ROWS + 1), S["row"])


def draw(c):
    sel = selection()
    c.clear("black")
    c.text(4, HEAD - 3, "T1", "cyan", "12")
    c.text(20, HEAD - 3, "NOTES", "white", "12")
    for k in range(4):
        c.text(CELL_X + k * CELL_W + 16, HEAD - 3, str(k + 1), "white" if k == S["col"] else "grey", "12")
    for v in range(ROWS):
        r = S["top"] + v
        y = HEAD + v * ROW
        band = r == S["row"]
        if sel and sel[0] <= r <= sel[1]:  # the selection, under everything
            c.fill(CELL_X + sel[2] * CELL_W, y, (sel[3] - sel[2] + 1) * CELL_W, ROW, SEL_INK)
        if band:
            x = CELL_X + S["col"] * CELL_W
            c.fill(0, y + 1, 46, ROW - 2, "green")
            c.fill(x, y, CELL_W, 2, "green")
            c.fill(x, y + ROW - 2, CELL_W, 2, "green")
            c.fill(x, y, 2, ROW, "green")
            c.fill(x + CELL_W - 2, y, 2, ROW, "green")
        c.text(NUM_X, y + 22, f"{r:02X}", ON_ACC if band else ("white" if r % 4 == 0 else "grey"))
        for k in range(4):
            n = block[r][k]
            c.text(CELL_X + k * CELL_W + 5, y + 21, n, "white" if n != "---" else "grey", "14")
    c.line(47, HEAD, 47, HEAD + ROWS * ROW, GROOVE)
    c.line(0, HEAD + ROWS * ROW + 1, 239, HEAD + ROWS * ROW + 1, GROOVE)
    # the foot: what is selected, what was said, what is in the clipboard
    if S["said"]:
        c.text(6, 234, S["said"], "amber", "14")
    elif S["proto"] == 0:
        c.text(6, 234, f"{sel[1] - sel[0] + 1} STEPS" if sel else "K4 SELECTS", "white" if sel else "grey", "14")
    else:
        c.text(6, 234, f"{sel[1] - sel[0] + 1} x {sel[3] - sel[2] + 1}" if sel else "EDIT ANCHORS",
               "white" if sel else "grey", "14")
    c.text(96, 234, "A  KNOB 4" if S["proto"] == 0 else "B  ANCHOR", "cyan", "12")
    if S["clip"]:
        c.text(240 - 50, 234, f"CLIP {len(S['clip'])}", "amber", "14")
    c.show()


def frame(c, ev):
    if "PRESETS" in ev.turns:
        S["proto"] = (S["proto"] + ev.turns["PRESETS"]) % 2
        S["sel"], S["anchor"] = 0, None
    S["said"] = ""
    hands(ev)
    draw(c)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--udp", nargs="?", const=19780, type=int)
    a = ap.parse_args()
    c = Canvas(udp=a.udp)
    c.keep_glo()
    c.run(lambda ev: frame(c, ev))


if __name__ == "__main__":
    main()
