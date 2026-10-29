#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""HOME as a root menu (29/10): CONTINUE the open project, PROJECTS the list,
SETTINGS, HELP (not written yet: grey). SELECT or KNOB 2 moves, SEL says where
it would go. HOME leaves the canvas.

    python3 tools/protos/home_root.py            # the FM-1 on USB-MIDI
    python3 tools/protos/home_root.py --udp      # the simulator
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from fm1_proto import Canvas  # noqa: E402

ROW, ON_ACC = 28, "#0B1409"
ITEMS = [("CONTINUE", "KIDS"), ("PROJECTS", "9"), ("SETTINGS", ""), ("HELP", "")]
S = {"cur": 0, "said": ""}


def frame(c, ev):
    S["cur"] = max(0, min(len(ITEMS) - 1, S["cur"] + ev.turns.get("SELECT", 0) + ev.turns.get("K2", 0)))
    S["said"] = ""
    if "SEL" in ev.pressed:
        name = ITEMS[S["cur"]][0]
        S["said"] = "NOT WRITTEN YET" if name == "HELP" else f"SEL: {name}"
    c.clear("black")
    for i, (name, right) in enumerate(ITEMS):
        y, here, off = 8 + i * ROW, i == S["cur"], name == "HELP"
        if here:
            c.fill(2, y + 1, 236, ROW - 2, "green")
        c.text(14, y + 22, name, ON_ACC if here else ("grey" if off else "white"))
        if right:
            c.text(236 - 9 * len(right) - 6, y + 22, right, ON_ACC if here else "grey")
    c.text(6, 236, S["said"] or "MOONDAW dev", "amber" if S["said"] else "grey", "12")
    c.text(206, 236, "MAX 0", "grey", "12")
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
