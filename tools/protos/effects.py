#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""HELP > EFFECTS as the other MOONDAW apps have it (29/10): a list of the
effects, each with its short line, and SEL on one opens its whole page --
moondaw's own text (fx_help in moondaw's src/sequencer/song.c), read from
there, wrapped to the panel in CONDENSED 20. SELECT or KNOB 1 moves in the
list and scrolls the page, KNOB 2 goes to the previous or next effect from
inside a page, HOME back.

    python3 tools/protos/effects.py [--moondaw ../moondaw]
"""
import argparse
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from fm1_proto import Canvas  # noqa: E402

ROW, ON_ACC, CHAR_W, WIDTH = 28, "#0B1409", 9, 222  # CONDENSED 20 is about 9 px a character


def c_table(src, name):
    """The strings of a C table, adjacent literals joined, NULL as None: the
    tokens read one by one, so a comma inside a string is a comma."""
    body = src[src.index(name):]
    body = body[body.index("{") + 1:]
    out, cur, i = [], None, 0
    while True:
        ch = body[i]
        if body.startswith("/*", i):
            i = body.index("*/", i) + 2
        elif ch == '"':
            j = i + 1
            while body[j] != '"':
                j += 2 if body[j] == "\\" else 1
            cur = (cur or "") + body[i + 1:j].encode().decode("unicode_escape")
            i = j + 1
        elif body.startswith("NULL", i):
            cur, i = None, i + 4
            out.append(None)
            cur = "\0skip"
        elif ch in ",}":
            if cur != "\0skip" and cur is not None:
                out.append(cur)
            cur = None
            if ch == "}":
                return out
            i += 1
        else:
            i += 1


def wrap(text, width_chars):
    lines = []
    for para in text.split("\n"):
        line = ""
        for word in para.split():
            if line and len(line) + 1 + len(word) > width_chars:
                lines.append(line)
                line = word
            else:
                line = f"{line} {word}" if line else word
        lines.append(line)
        lines.append("")  # a paragraph's air
    return lines[:-1]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--udp", nargs="?", const=19780, type=int)
    ap.add_argument("--moondaw", default=str(pathlib.Path(__file__).resolve().parents[3] / "moondaw"))
    a = ap.parse_args()
    src = (pathlib.Path(a.moondaw) / "src/sequencer/song.c").read_text()
    names, descs, helps = c_table(src, "fx_names["), c_table(src, "fx_descs["), c_table(src, "fx_helps[")
    fx = [(n, d, h) for n, d, h in zip(names, descs, helps) if n != "---"]
    S = {"cur": 0, "top": 0, "page": None, "scroll": 0}

    def frame(c, ev):
        d = ev.turns.get("SELECT", 0) + ev.turns.get("K1", 0)
        if S["page"] is None:
            S["cur"] = max(0, min(len(fx) - 1, S["cur"] + d))
            S["top"] = min(max(S["top"], S["cur"] - 6), S["cur"])
            if "SEL" in ev.pressed:
                S["page"], S["scroll"] = S["cur"], 0
        else:
            if "K2" in ev.turns:
                S["page"] = max(0, min(len(fx) - 1, S["page"] + ev.turns["K2"]))
                S["cur"], S["scroll"] = S["page"], 0
            lines = wrap(fx[S["page"]][2] or "(no page yet)", WIDTH // CHAR_W)
            S["scroll"] = max(0, min(max(0, len(lines) - 7), S["scroll"] + d))
            if "HOME" in ev.pressed:
                S["page"] = None
        c.clear("black")
        if S["page"] is None:
            c.text(6, 14, "HELP  EFFECTS", "cyan", "12")
            for v in range(7):
                i = S["top"] + v
                if i >= len(fx):
                    break
                y, here = 18 + v * ROW, i == S["cur"]
                if here:
                    c.fill(2, y + 1, 236, ROW - 2, "green")
                c.text(12, y + 22, fx[i][0], ON_ACC if here else "white")
                c.text(64, y + 21, fx[i][1], ON_ACC if here else "grey", "14")
            c.text(6, 236, "SEL  the whole page", "grey", "12")
        else:
            name, desc, text = fx[S["page"]]
            c.text(6, 14, "HELP  EFFECTS", "cyan", "12")
            c.text(8, 42, name, "amber")
            c.text(60, 41, desc, "grey", "14")
            c.line(0, 50, 239, 50, "#282828")
            lines = wrap(text or "(no page yet)", WIDTH // CHAR_W)
            for v in range(7):
                i = S["scroll"] + v
                if i >= len(lines):
                    break
                if lines[i]:
                    c.text(8, 74 + v * 26, lines[i], "white")
            if len(lines) > 7:
                h = 182 * 7 // len(lines)
                c.fill(236, 56, 2, 182, "#282828")
                c.fill(236, 56 + 182 * S["scroll"] // len(lines), 2, h, "grey")
        c.show()

    c = Canvas(udp=a.udp)
    c.run(lambda ev: frame(c, ev))


if __name__ == "__main__":
    main()
