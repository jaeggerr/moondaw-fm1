#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Two ways to write a VOX syllable on the FM-1, one syllable between them:
PRESETS turns from one to the other, as the hand is at ease (29/10).

  A  KEYS    the keyboard types: consonants on the white keys in their QWERTY
             order (R T P, S D F G J K L, Z C V B N M), vowels on the black;
             OCT+ the second layer (Q H X +, and the rarer vowels), LFO rubs
             the last sign out, ARP marks it weak (.)
  B  WHEEL   five slots: KNOB 1 the slot, KNOB 2 turns its sign through the
             alphabet in its order (letters, digits, + .), the sound of each
             written beside it; LFO empties the slot -- the namer's way
(A third, C, built the syllable from an onset, a vowel and a coda: set aside.)

SEL takes it, GLO goes back (both only say so: nothing is written), HOME
leaves. The real screen would sing each change on the step's note; the
canvas has no voice, so this one is silent.

    python3 tools/protos/syllables.py            # the FM-1 on USB-MIDI
    python3 tools/protos/syllables.py --udp      # the simulator
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from fm1_proto import KEY0, NAMES, Canvas  # noqa: E402

ON_ACC, GROOVE = "#0B1409", "#282828"
MAX = 5
VOWELS = "AEIOUY7296305148"
CONSONANTS = "PBTDKGFVSZMNLRHCJXQ+"
SOUND = {"A": "a  papa", "E": "e  ete", "I": "i  lit", "O": "o  eau", "U": "ou  loup", "Y": "u  tu",
         "7": "e  sept", "2": "eu  deux", "9": "eur  neuf", "6": "o  pomme", "3": "an  trente", "0": "on  bon",
         "5": "in  cinq", "1": "i  bit (EN)", "4": "a  cat (EN)", "8": "oo  book (EN)", "R": "r  francais",
         "H": "h  hello", "C": "ch  chat", "J": "j  jour", "X": "th  thin (EN)", "Q": "r  right (EN)",
         "+": "th  the (EN)", ".": "weak: softer, towards eu"}
for c in "PBTDKGFVSZMNL":
    SOUND.setdefault(c, c.lower())

# A: the keyboard, F-3 to G-5 (27 keys, 16 white, 11 black), two layers
WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]
BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]
LAYERS = [
    (dict(zip(WHITE, "RTPSDFGJKLZCVBNM")), dict(zip(BLACK, "AEIOUY72963"))),
    (dict(zip(WHITE, "QHX+")), dict(zip(BLACK, "05148"))),
]
# B: the wheel's order -- empty, then the alphabet's: letters, digits, + and .
WHEEL = ["-"] + sorted(c for c in VOWELS + CONSONANTS if c.isalpha()) + sorted(c for c in VOWELS if c.isdigit()) \
    + ["+", "."]
S = {"proto": 0, "said": "", "slots": ["M", "A", "-", "-", "-"], "at": 1, "layer": 0, "last": ""}


def key_index(name):
    note = NAMES.index(name[:2]) + 12 * (int(name[2:]) + 1)
    return note - KEY0


def syllable():
    return "".join(c for c in S["slots"] if c != "-")


# --------------------------------------------------------------- the hands
def hands_a(ev):
    slots = S["slots"]
    if "OCT+" in ev.pressed or "OCT-" in ev.pressed:
        S["layer"] ^= 1
    for name in ev.keys_down:
        k = key_index(name)
        white, black = LAYERS[S["layer"]]
        sign = white.get(k) or black.get(k)
        free = [i for i in range(MAX) if slots[i] == "-" and all(c == "-" for c in slots[i:])]
        if sign and free:
            slots[free[0]] = sign
            S["last"] = sign
    if "LFO" in ev.pressed:
        used = [i for i in range(MAX) if slots[i] != "-"]
        if used:
            slots[used[-1]] = "-"
    if "ARP" in ev.pressed:
        text = syllable()
        text = text.replace(".", "") if "." in text else ("." + text)[:MAX]
        S["slots"] = list(text.ljust(MAX, "-"))
        S["last"] = "."


def hands_b(ev):
    S["at"] = max(0, min(MAX - 1, S["at"] + ev.turns.get("K1", 0)))
    if "K2" in ev.turns:
        i = WHEEL.index(S["slots"][S["at"]])
        S["slots"][S["at"]] = WHEEL[max(0, min(len(WHEEL) - 1, i + ev.turns["K2"]))]
    if "LFO" in ev.pressed:
        S["slots"][S["at"]] = "-"


# ------------------------------------------------------------- the drawing
def head(c, title):
    c.text(6, 17, "SYLLABLE", "cyan", "14")
    c.text(66, 17, "T2 / STEP 04", "grey", "12")
    c.text(234 - 58, 17, title, "white", "12")
    c.line(0, 21, 239, 21, GROOVE)


def big(c, text, y=56):
    """The syllable as the step will show it, in a frame."""
    c.fill(40, y - 30, 160, 40, "#101820")
    shown = "'" + text if text else "'"
    c.text(120 - 6 * len(shown), y, shown, "white")


def hint(c, sign, y):
    if sign in SOUND:
        c.text(12, y, sign, "amber", "14")
        c.text(30, y, SOUND[sign], "grey", "14")


def foot(c, extra=""):
    c.text(6, 236, S["said"] or "SEL", "amber" if S["said"] else "white", "14")
    if not S["said"]:
        c.text(30, 236, "TAKE", "grey", "14")
    if extra:
        c.text(76, 236, extra, "grey", "12")
    c.text(234 - 58, 236, "GLO", "white", "14")
    c.text(234 - 30, 236, "BACK", "grey", "14")


def draw_a(c):
    head(c, "A  KEYS")
    big(c, syllable())
    c.text(206, 56, f"{len(syllable())}/{MAX}", "grey", "12")
    hint(c, S["last"], 90)
    c.text(12, 112, "LAYER 2" if S["layer"] else "LAYER 1", "cyan" if S["layer"] else "grey", "12")
    c.text(64, 112, "OCT+ other layer   LFO rub out   ARP weak", "grey", "12")
    white, black = LAYERS[S["layer"]]
    top, w = 122, 15
    for i, k in enumerate(WHITE):
        x = i * w
        c.fill(x + 1, top, w - 2, 98, "#D8D8D8" if k in white else "#3A3A3A")
        if k in white:
            c.text(x + 4, top + 92, white[k], "black", "14")
    for k in BLACK:
        prev = max(i for i, wk in enumerate(WHITE) if wk < k)
        x = (prev + 1) * w - 5
        c.fill(x, top, 10, 56, "#2C6E7A" if k in black else "#141414")
        if k in black:
            c.text(x + 2, top + 50, black[k], "white", "12")
    foot(c)


def draw_b(c):
    b = S
    head(c, "B  WHEEL")
    big(c, syllable())
    for i, sign in enumerate(b["slots"]):
        x, here = 26 + i * 40, i == b["at"]
        c.fill(x, 80, 32, 40, "green" if here else "#1A1A1A")
        c.text(x + 11, 108, sign, ON_ACC if here else ("grey" if sign == "-" else "white"))
    at = WHEEL.index(b["slots"][b["at"]])
    c.fill(111, 130, 18, 22, "#1F3A12")  # the wheel around the slot's sign, the sign in the middle
    for d in range(-5, 6):
        j = at + d
        if 0 <= j < len(WHEEL):
            c.text(116 + d * 19, 146, WHEEL[j], "green" if d == 0 else ("white" if abs(d) == 1 else "grey"), "14")
    hint(c, b["slots"][b["at"]], 176)
    c.text(12, 204, "KNOB 1 the slot   KNOB 2 its sign   LFO empty", "grey", "12")
    foot(c)


def frame(c, ev):
    S["proto"] = (S["proto"] + ev.turns.get("PRESETS", 0)) % 2
    S["said"] = ""
    [hands_a, hands_b][S["proto"]](ev)
    if "SEL" in ev.pressed:
        S["said"] = "TAKEN '" + syllable()
    if "GLO" in ev.pressed:
        S["said"] = "BACK, NOTHING WRITTEN"
    c.clear("black")
    [draw_a, draw_b][S["proto"]](c)
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
