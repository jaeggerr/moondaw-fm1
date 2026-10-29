#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""HELP (29/10): from HOME's root, a list of topics -- one a screen, GESTURES,
EFFECTS -- the cursor already on the screen one came from (HOME held from
PATTERN, then HELP: PATTERN's page), so no button is needed for a help of
where one is. SEL opens a page, SELECT or KNOB 1 scrolls it, HOME goes back.

PRESETS turns between two ways of setting a page:
  A  one line a gesture, all in CONDENSED 20: the control white, its
     meaning grey -- as everything else; fewer lines a screen
  B  the control in 20 on its line, the meaning under it in 14: more said,
     smaller

    python3 tools/protos/help.py            # the FM-1 on USB-MIDI
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from fm1_proto import Canvas  # noqa: E402

ROW, ON_ACC = 28, "#0B1409"
TOPICS = ["HOME", "SONG", "SECTION", "PATTERN", "SYLLABLE", "INSTRUMENT", "MIXER", "PROJECT", "SETTINGS",
          "GESTURES", "EFFECTS"]
PATTERN = [
    ("SELECT", "step, onto its number"),
    ("KNOB 1", "step"),
    ("KNOB 2", "column"),
    ("KNOB 3", "value"),
    ("KNOB 4", "effect's type"),
    ("SEQ FX ENV", "notes, effects, instr."),
    ("ENV twice", "the instrument"),
    ("SEL on FX", "the syllable"),
    ("SEL on INS", "choose instrument"),
    ("keys", "write in NOTES, else play"),
    ("OCT- OCT+", "keyboard's octave"),
    ("EDIT", "selection, in and out"),
    ("EDIT held", "black keys: commands"),
    ("OP1 OP2 OP3", "copy, paste, cut"),
    ("OP4", "clear"),
    ("OP6", "loop the selection"),
    ("MONO", "solo; held: mute"),
    ("ALGO", "undo left, redo right"),
    ("PLAY", "the phrase round"),
    ("GLO", "the project"),
    ("HOME", "back; held: home"),
]
S = {"style": 0, "page": None, "cur": TOPICS.index("PATTERN"), "top": 0, "scroll": 0}


def frame(c, ev):
    d = ev.turns.get("SELECT", 0) + ev.turns.get("K1", 0)
    if "PRESETS" in ev.turns:
        S["style"] ^= 1
        S["scroll"] = 0
    if S["page"] is None:
        S["cur"] = max(0, min(len(TOPICS) - 1, S["cur"] + d))
        S["top"] = min(max(S["top"], S["cur"] - 6), S["cur"])
        if "SEL" in ev.pressed:
            S["page"], S["scroll"] = TOPICS[S["cur"]], 0
    else:
        per = 7 if S["style"] == 0 else 4
        S["scroll"] = max(0, min(len(PATTERN) - per, S["scroll"] + d))
        if "HOME" in ev.pressed:
            S["page"] = None
    c.clear("black")
    if S["page"] is None:
        c.text(6, 14, "HELP", "cyan", "12")
        for v in range(7):
            i = S["top"] + v
            if i >= len(TOPICS):
                break
            y, here = 18 + v * ROW, i == S["cur"]
            if here:
                c.fill(2, y + 1, 236, ROW - 2, "green")
            c.text(14, y + 22, TOPICS[i], ON_ACC if here else ("white" if TOPICS[i] == "PATTERN" else "grey"))
        c.text(6, 236, "on PATTERN: where you came from", "grey", "12")
    else:
        page = PATTERN
        c.text(6, 14, "HELP  " + S["page"], "cyan", "12")
        c.text(240 - 40, 14, "A" if S["style"] == 0 else "B", "grey", "12")
        if S["style"] == 0:
            for v in range(7):
                i = S["scroll"] + v
                if i >= len(page):
                    break
                y = 22 + v * 30
                c.text(8, y + 20, page[i][0], "white", "20")
                c.text(8 + 104, y + 20, page[i][1], "grey", "14")
        else:
            for v in range(4):
                i = S["scroll"] + v
                if i >= len(page):
                    break
                y = 22 + v * 52
                c.text(10, y + 20, page[i][0], "white", "20")
                c.text(10, y + 42, page[i][1], "grey", "20")
        n = len(page)
        per = 7 if S["style"] == 0 else 4
        h = 210 * per // n
        c.fill(236, 22, 2, 210, "#282828")
        c.fill(236, 22 + 210 * S["scroll"] // n, 2, h, "grey")
    c.show()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--udp", nargs="?", const=19780, type=int)
    a = ap.parse_args()
    c = Canvas(udp=a.udp)
    c.run(lambda ev: frame(c, ev))


if __name__ == "__main__":
    main()
