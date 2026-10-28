#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A live prototype on the FM-1's panel: a list SELECT walks, SEL marks, KNOB 2 turns
the value of the row under the cursor, GLO says it would go back. HOME leaves.

    python3 tools/protos/list_demo.py            # the FM-1 on USB-MIDI
    python3 tools/protos/list_demo.py --udp      # the simulator

A starting point to copy for a screen being designed: everything drawn here is the
script's, nothing is built into the firmware.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from fm1_proto import Canvas  # noqa: E402

ROW, ROWS, HEAD = 28, 8, 16
items = [["KICK", 100], ["SNARE", 90], ["HATS", 64], ["BASS", 110], ["CHORDS", 80], ["LEAD", 96],
         ["PAD", 70], ["FX", 40], ["VOX", 88], ["PERC", 60]]
state = {"cur": 0, "top": 0, "marked": set(), "said": ""}


def frame(c, ev):
    s = state
    s["cur"] = max(0, min(len(items) - 1, s["cur"] + ev.turns.get("SELECT", 0)))
    s["top"] = min(max(s["top"], s["cur"] - ROWS + 1), s["cur"])
    if "K2" in ev.turns:
        items[s["cur"]][1] = max(0, min(127, items[s["cur"]][1] + ev.turns["K2"]))
    if "SEL" in ev.pressed:
        s["marked"] ^= {s["cur"]}
    s["said"] = "GLO: back" if "GLO" in ev.pressed else ""

    c.clear("black")
    c.text(4, HEAD - 3, s["said"] or "PROTOTYPE", "amber" if s["said"] else "grey", "12")
    c.text(150, HEAD - 3, "LEVEL", "white" if "K2" in ev.turns else "grey", "12")
    for v in range(ROWS):
        i = s["top"] + v
        if i >= len(items):
            break
        y = HEAD + v * ROW
        here = i == s["cur"]
        if here:
            c.fill(2, y + 1, 236, ROW - 2, "green")
        if i in s["marked"]:
            c.fill(5, y + 6, 3, ROW - 12, "#0B1409" if here else "cyan")
        ink = "#0B1409" if here else "white"
        c.text(14, y + 22, items[i][0], ink)
        c.text(150, y + 22, items[i][1], ink)
    c.show()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--udp", nargs="?", const=19780, type=int)
    ap.add_argument("--port")
    a = ap.parse_args()
    c = Canvas(udp=a.udp, port=a.port)
    c.keep_glo()
    c.run(lambda ev: frame(c, ev))
    print("the hand left the prototype")


if __name__ == "__main__":
    main()
