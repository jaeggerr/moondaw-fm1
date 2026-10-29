#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LOOP in PATTERN, from the SELECTION (29/10): the selection's steps become
the loop PLAY goes round. No new mode, no new button: EDIT enters the
selection as now, SELECT / KNOB 1 stretch it, and OP5 -- the fifth black key,
after copy paste cut clear -- makes it the LOOP (and lets the selection go).
OP5 with no selection, or on the loop as it is, forgets it. The loop shows as
a cyan rule left of the numbers, its numbers cyan; PLAY plays (the playhead
the green triangle, here only drawn). HOME leaves.

    python3 tools/protos/loop.py            # the FM-1 on USB-MIDI
    python3 tools/protos/loop.py --udp      # the simulator
"""
import argparse
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from fm1_proto import Canvas  # noqa: E402

ROW, ROWS, HEAD, NUM_X, CELL_X, CELL_W = 28, 8, 16, 14, 50, 38
ON_ACC, SEL_INK, GROOVE = "#0B1409", "#6E5518", "#282828"
LEN, STEP_S = 16, 60 / 120 / 4  # sixteenths at 120 bpm
OP5 = "D#4"  # the fifth black key: F#3 G#3 A#3 C#4 D#4
block = [["---"] * 4 for _ in range(LEN)]
for r, n in {0: "C-4", 2: "E-4", 4: "G-4", 6: "E-4", 8: "C-4", 9: "D-4", 10: "E-4", 12: "G-4", 14: "B-3"}.items():
    block[r][0] = n
S = {"row": 0, "top": 0, "col": 0, "selecting": False, "anchor": None, "loop": None, "playing": False,
     "t0": 0.0, "said": ""}


def hands(ev):
    S["said"] = ""
    d = ev.turns.get("SELECT", 0) + ev.turns.get("K1", 0)
    S["row"] = max(0, min(LEN - 1, S["row"] + d))
    S["col"] = max(-1, min(3, S["col"] + ev.turns.get("K2", 0)))
    S["top"] = min(max(S["top"], S["row"] - ROWS + 1), S["row"])
    if "EDIT" in ev.pressed:
        S["selecting"] = not S["selecting"]
        S["anchor"] = S["row"] if S["selecting"] else None
    if "HOME" in ev.pressed and S["selecting"]:
        S["selecting"], S["anchor"] = False, None
    if OP5 in ev.keys_down and S["selecting"]:
        if S["anchor"] is not None:
            r0, r1 = sorted((S["anchor"], S["row"]))
            if S["loop"] == (r0, r1):
                S["loop"], S["said"] = None, "NO LOOP"
            else:
                S["loop"], S["said"] = (r0, r1), f"LOOP {r0:02X}-{r1:02X}"
            S["anchor"] = None
        else:
            S["loop"], S["said"] = None, "NO LOOP"
    if "PLAY" in ev.pressed:
        S["playing"] = not S["playing"]
        S["t0"] = time.time()


def playhead():
    if not S["playing"]:
        return None
    lo, hi = S["loop"] or (0, LEN - 1)
    return lo + int((time.time() - S["t0"]) / STEP_S) % (hi - lo + 1)


def draw(c):
    c.clear("black")
    lp, ph = S["loop"], playhead()
    if S["said"]:
        c.text(4, HEAD - 3, S["said"], "amber", "12")
    elif S["selecting"]:
        c.text(4, HEAD - 3, "SELECT", "amber", "12")
    elif lp:  # the loop, where the view's name is
        c.text(4, HEAD - 3, f"LOOP {lp[0]:02X}-{lp[1]:02X}", "cyan", "12")
    else:
        c.text(4, HEAD - 3, "T1", "cyan", "12")
        c.text(20, HEAD - 3, "NOTES", "white", "12")
    for k in range(4):
        c.text(CELL_X + k * CELL_W + 16, HEAD - 3, str(k + 1), "white" if k == S["col"] else "grey", "12")
    for v in range(ROWS):
        r = S["top"] + v
        y = HEAD + v * ROW
        band = r == S["row"]
        if S["selecting"] and S["anchor"] is not None and min(S["anchor"], S["row"]) <= r <= max(S["anchor"], S["row"]):
            c.fill(2, y, 232, ROW, SEL_INK)
        if band:
            c.fill(2, y + 1, 232, ROW - 2, "green" if not S["selecting"] else "#3C6E30")
        if lp and lp[0] <= r <= lp[1]:  # the loop: a cyan rule at the left edge
            c.fill(1, y + (3 if r == lp[0] else 0), 3, ROW - (3 if r == lp[0] else 0) - (3 if r == lp[1] else 0), "cyan")
        if r == ph:
            for i in range(5):
                c.line(6 + i, y + 9 + i, 6 + i, y + 19 - i, "green" if not band else ON_ACC)
        num_ink = ON_ACC if band else ("cyan" if lp and lp[0] <= r <= lp[1] else "white" if r % 4 == 0 else "grey")
        c.text(NUM_X, y + 22, f"{r:02X}", num_ink)
        for k in range(4):
            n = block[r][k]
            c.text(CELL_X + k * CELL_W + 5, y + 21, n, ON_ACC if band else ("white" if n != "---" else "grey"), "14")
    c.line(47, HEAD, 47, 239, GROOVE)
    c.show()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--udp", nargs="?", const=19780, type=int)
    a = ap.parse_args()
    c = Canvas(udp=a.udp)
    c.keep_glo()
    draw(c)
    last_ph = None
    while True:
        ev = c.events()
        if not ev.shown:
            return
        if ev:
            hands(ev)
        ph = playhead()
        if ev or ph != last_ph:  # the playhead moves on its own
            draw(c)
            last_ph = ph
        time.sleep(1 / 40)


if __name__ == "__main__":
    main()
