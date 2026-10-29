#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Three ways to write a VOX syllable on the FM-1, to try by hand (29/10).
PRESETS turns from one to the other; each keeps its own syllable.

  A  KEYS    the keyboard types: a sign on each key, consonants on the white
             keys, vowels on the black; OCT+ the second layer (the rarer signs),
             LFO rubs the last sign out, ARP marks it weak (.)
  B  WHEEL   five slots: KNOB 1 the slot, KNOB 2 turns its sign through the
             alphabet (vowels first), LFO empties the slot -- the namer's way
  C  PARTS   a syllable is its onset, its vowel, its coda: KNOB 1, 2 and 3
             each turn one of them, KNOB 4 marks it weak

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
    (dict(zip(WHITE, "PBTDKGFVSZMNLRCJ")), dict(zip(BLACK, "AEIOUY72963"))),
    (dict(zip(WHITE, "HXQ+")), dict(zip(BLACK, "05148"))),
]
# B: the wheel's order -- empty, the vowels, the consonants, the weak mark
WHEEL = ["-"] + list(VOWELS) + list(CONSONANTS) + ["."]
# C: the three parts
ONSETS = ["", "P", "B", "T", "D", "K", "G", "F", "V", "S", "Z", "M", "N", "L", "R", "H", "C", "J", "X", "Q",
          "+", "PL", "PR", "BL", "BR", "TR", "DR", "KL", "KR", "GL", "GR", "FL", "FR", "SP", "ST", "SK", "SM",
          "SN", "SL", "TS", "NY", "W", "Y"]
NUCLEI = list(VOWELS) + ["AI", "AU", "OI", "EI", "OU"]
CODAS = ["", "N", "M", "L", "R", "S", "Z", "T", "D", "K", "P", "F", "V", "G", "NZ", "NT", "ND", "ST", "TS",
         "KS", "LZ", "RZ", "MZ", "NG", "Q", "QZ"]

S = {"proto": 0, "said": "",
     "a": {"text": "", "layer": 0, "last": ""},
     "b": {"slots": ["M", "A", "-", "-", "-"], "at": 1},
     "c": {"on": ONSETS.index("M"), "nu": 0, "co": 0, "weak": False}}


def key_index(name):
    note = NAMES.index(name[:2]) + 12 * (int(name[2:]) + 1)
    return note - KEY0


def syllable():
    p = S["proto"]
    if p == 0:
        return S["a"]["text"]
    if p == 1:
        return "".join(c for c in S["b"]["slots"] if c != "-")
    c = S["c"]
    return (("." if c["weak"] else "") + ONSETS[c["on"]] + NUCLEI[c["nu"]] + CODAS[c["co"]])[:MAX]


# --------------------------------------------------------------- the hands
def hands_a(ev):
    a = S["a"]
    if "OCT+" in ev.pressed or "OCT-" in ev.pressed:
        a["layer"] ^= 1
    for name in ev.keys_down:
        k = key_index(name)
        white, black = LAYERS[a["layer"]]
        sign = white.get(k) or black.get(k)
        if sign and len(a["text"]) < MAX:
            a["text"] += sign
            a["last"] = sign
    if "LFO" in ev.pressed:
        a["text"] = a["text"][:-1]
    if "ARP" in ev.pressed and len(a["text"]) < MAX:
        a["text"] = a["text"].replace(".", "") if "." in a["text"] else "." + a["text"]
        a["last"] = "."


def hands_b(ev):
    b = S["b"]
    b["at"] = max(0, min(MAX - 1, b["at"] + ev.turns.get("K1", 0)))
    if "K2" in ev.turns:
        i = WHEEL.index(b["slots"][b["at"]])
        b["slots"][b["at"]] = WHEEL[max(0, min(len(WHEEL) - 1, i + ev.turns["K2"]))]
    if "LFO" in ev.pressed:
        b["slots"][b["at"]] = "-"


def hands_c(ev):
    c = S["c"]
    for knob, part, n in (("K1", "on", len(ONSETS)), ("K2", "nu", len(NUCLEI)), ("K3", "co", len(CODAS))):
        c[part] = max(0, min(n - 1, c[part] + ev.turns.get(knob, 0)))
    if "K4" in ev.turns:
        c["weak"] = ev.turns["K4"] > 0


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
    a = S["a"]
    head(c, "A  KEYS")
    big(c, a["text"])
    c.text(206, 56, f"{len(a['text'])}/{MAX}", "grey", "12")
    hint(c, a["last"], 90)
    c.text(12, 112, "LAYER 2" if a["layer"] else "LAYER 1", "cyan" if a["layer"] else "grey", "12")
    c.text(64, 112, "OCT+ other layer   LFO rub out   ARP weak", "grey", "12")
    white, black = LAYERS[a["layer"]]
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
    b = S["b"]
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


def draw_c(c):
    s = S["c"]
    head(c, "C  PARTS")
    big(c, syllable())
    cols = (("ONSET", ONSETS, s["on"], "K1"), ("VOWEL", NUCLEI, s["nu"], "K2"), ("CODA", CODAS, s["co"], "K3"))
    for n, (name, items, at, knob) in enumerate(cols):
        x = 8 + n * 76
        c.text(x + 4, 86, name, "cyan", "12")
        c.text(x + 52, 86, knob, "grey", "12")
        for d in (-2, -1, 0, 1, 2):
            j = at + d
            if not 0 <= j < len(items):
                continue
            y = 136 + d * 24
            if d == 0:
                c.fill(x, y - 20, 70, 26, "green")
            c.text(x + 8, y, items[j] or "--", ON_ACC if d == 0 else ("white" if abs(d) == 1 else "grey"),
                   "20" if d == 0 else "14")
    hint(c, NUCLEI[s["nu"]][0], 208)
    c.text(150, 208, "K4 WEAK" if not s["weak"] else "K4 WEAK ON", "amber" if s["weak"] else "grey", "12")
    foot(c)


def frame(c, ev):
    S["proto"] = (S["proto"] + ev.turns.get("PRESETS", 0)) % 3
    S["said"] = ""
    [hands_a, hands_b, hands_c][S["proto"]](ev)
    if "SEL" in ev.pressed:
        S["said"] = "TAKEN '" + syllable()
    if "GLO" in ev.pressed:
        S["said"] = "BACK, NOTHING WRITTEN"
    c.clear("black")
    [draw_a, draw_b, draw_c][S["proto"]](c)
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
