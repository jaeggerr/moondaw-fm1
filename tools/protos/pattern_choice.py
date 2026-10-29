#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The SONG cell's block chooser, redrawn to say what it is: a full screen
titled CHOOSE PATTERN, no grid behind it, and a play triangle on every row,
filled on the one PLAY is playing. SELECT walks, PLAY plays (again: stops),
SEL takes it, GLO goes back. HOME leaves.

    python3 tools/protos/pattern_choice.py            # the FM-1 on USB-MIDI
    python3 tools/protos/pattern_choice.py --udp      # the simulator
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from fm1_proto import Canvas  # noqa: E402

ROW, ROWS, TOP = 28, 7, 24
ON_ACC = "#0B1409"
rows = [("--", "silent"), ("00", ""), ("01", ""), ("02", ""), ("03", ""), ("04", ""), ("05", "new")]
state = {"cur": 1, "top": 0, "playing": -1, "in_place": 1, "said": ""}


def triangle(c, x, y, colour, filled):
    """A play sign, 10 wide and 12 high, its top left corner at x, y."""
    if filled:
        for i in range(10):
            h = 12 - (12 * i) // 10
            c.line(x + i, y + (12 - h) // 2, x + i, y + (12 - h) // 2 + h - 1, colour)
    else:
        c.line(x, y, x, y + 11, colour)
        c.line(x, y, x + 9, y + 6, colour)
        c.line(x, y + 11, x + 9, y + 5, colour)


def frame(c, ev):
    s = state
    s["cur"] = max(0, min(len(rows) - 1, s["cur"] + ev.turns.get("SELECT", 0)))
    s["top"] = min(max(s["top"], s["cur"] - ROWS + 1), s["cur"])
    if "PLAY" in ev.pressed:
        s["playing"] = -1 if s["playing"] == s["cur"] else s["cur"]
    s["said"] = ""
    if "SEL" in ev.pressed:
        s["in_place"], s["playing"] = s["cur"], -1
        s["said"] = "TAKEN"
    if "GLO" in ev.pressed:
        s["playing"], s["said"] = -1, "BACK TO SONG"

    c.clear("black")
    c.text(6, 17, "CHOOSE PATTERN", "cyan", "14")
    c.text(234 - 62, 17, "PHR 00 / TRK 1", "grey", "12")
    c.line(0, TOP - 3, 239, TOP - 3, "#282828")
    for v in range(ROWS):
        i = s["top"] + v
        if i >= len(rows):
            break
        y, here = TOP + v * ROW, i == s["cur"]
        num, word = rows[i]
        if here:
            c.fill(4, y + 1, 232, ROW - 2, "green")
        if i == s["in_place"]:
            c.fill(6, y + 6, 3, ROW - 12, ON_ACC if here else "cyan")
        ink = ON_ACC if here else ("grey" if num == "--" or word == "new" else "white")
        c.text(16, y + 22, num, ink)
        if word:
            c.text(46, y + 21, word, ink, "14")
        if num != "--":
            play = ON_ACC if here else ("green" if i == s["playing"] else "grey")
            triangle(c, 214, y + 8, play, i == s["playing"])
    c.text(6, 236, s["said"] or "SEL", "amber" if s["said"] else "white", "14")
    if not s["said"]:
        c.text(30, 236, "TAKE", "grey", "14")
        c.text(96, 236, "PLAY", "white", "14")
        c.text(128, 236, "LISTEN", "grey", "14")
    c.text(234 - 58, 236, "GLO", "white", "14")
    c.text(234 - 30, 236, "BACK", "grey", "14")
    c.show()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--udp", nargs="?", const=19780, type=int)
    a = ap.parse_args()
    c = Canvas(udp=a.udp)
    c.keep_glo()
    c.run(lambda ev: frame(c, ev))


if __name__ == "__main__":
    main()
