#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""PATTERN's INS view, as Jaeger put it on 29/10: on the left the instrument
the step names, or -- where it names none; on the right the name of the
instrument that plays, white where the step names it, grey where it keeps
the one in force. A step without a note shows -- alone. SELECT walks; HOME
leaves.

    python3 tools/protos/ins_column.py            # the FM-1 on USB-MIDI
    python3 tools/protos/ins_column.py --udp      # the simulator
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from fm1_proto import Canvas  # noqa: E402

ROW, HEAD = 28, 16
ON_ACC = "#0B1409"
INSTR = {0: "PIANO", 1: "BASS"}
# per step: the instrument it names (None: none) and whether it plays a note
steps = [(0, True), (None, True), (None, False), (None, True), (1, True), (None, True), (None, False),
         (None, True)]
state = {"cur": 0}


def frame(c, ev):
    s = state
    s["cur"] = max(0, min(len(steps) - 1, s["cur"] + ev.turns.get("SELECT", 0)))
    c.clear("black")
    c.text(4, HEAD - 3, "T1", "cyan", "12")
    c.text(20, HEAD - 3, "INS", "white", "12")
    c.text(118, HEAD - 3, "INSTRUMENT", "white", "12")
    in_force = 0
    for r, (named, sounds) in enumerate(steps):
        if named is not None:
            in_force = named
        y, here = HEAD + r * ROW, r == s["cur"]
        base = y + 22
        if here:
            c.fill(0, y + 1, 46, ROW - 2, "green")
            c.fill(52, y + 1, 182, ROW - 2, "#16240F")
            c.fill(50, y, 186, 2, "green")
            c.fill(50, y + ROW - 2, 186, 2, "green")
            c.fill(50, y, 2, ROW, "green")
            c.fill(234, y, 2, ROW, "green")
        c.text(14, base, f"{r:02X}", ON_ACC if here else ("white" if r % 4 == 0 else "grey"))
        if named is not None:
            c.text(60, base, f"{named:02X}", "white")
            c.text(92, base, INSTR[named], "white")
        else:
            c.text(60, base, "--", "grey")
            if sounds:
                c.text(92, base, INSTR[in_force], "grey")
    c.line(47, HEAD, 47, 239, "#282828")
    c.show()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--udp", nargs="?", const=19780, type=int)
    a = ap.parse_args()
    c = Canvas(udp=a.udp)
    c.run(lambda ev: frame(c, ev))


if __name__ == "__main__":
    main()
