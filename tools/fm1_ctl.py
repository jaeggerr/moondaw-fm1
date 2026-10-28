#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Drive MOONDAW on the FM-1 from this computer: read what it is doing, press
its buttons, turn its knobs, play its keys, and write into the song in memory.

    tools/fm1_ctl.py status [--reset]          what plays, CPU and the worst block, the screen
    tools/fm1_ctl.py status --profile [--reset]   the worst block stage by stage (us), the second core
    tools/fm1_ctl.py status --boot             why it last started (P33 << 24 | PWR_CON), its last crash, CPU1
    tools/fm1_ctl.py press SEL [PLAY ...]      buttons, as printed (OCT- OCT+ too)
    tools/fm1_ctl.py turn K2 -3                a knob: SELECT ALGO PRESETS K1 K2 K3 K4
    tools/fm1_ctl.py key C-4 [--hold 20]       a key of the keyboard (F-3 .. G-5), held N frames
    tools/fm1_ctl.py play [PHRASE] | loop PHRASE | stop
    tools/fm1_ctl.py open NAME [--force]       a song from the device's flash into memory
    tools/fm1_ctl.py new [--force]             an empty song in memory
    tools/fm1_ctl.py step T BLOCK ROW 'C-4+E-4+G-4 i03 fVOL40 fREV20'
    tools/fm1_ctl.py step T BLOCK ROW --        empties the step
    tools/fm1_ctl.py cell PHRASE T BLOCK|--    which block track T plays in a phrase (made if new)
    tools/fm1_ctl.py length T BLOCK STEPS
    tools/fm1_ctl.py save
    tools/fm1_ctl.py canvas clear [COLOUR]     a screen of the computer's own, GLO leaves it
    tools/fm1_ctl.py canvas fill X Y W H COLOUR
    tools/fm1_ctl.py canvas text X Y 'WORDS' [--font 20|14|12|mono] [--colour COLOUR]
    tools/fm1_ctl.py canvas line X0 Y0 X1 Y1 COLOUR
    tools/fm1_ctl.py canvas image FILE.png     a picture, 240 x 240, sent a row at a time

Tracks are counted from 1 as on the screens; phrases, blocks, rows,
instruments and effect values are hex, as on the screens and in .trk
files; STEPS and the knob's turn are decimal. A colour is #RRGGBB or one of the
theme's words: black white grey green cyan amber red. On the canvas, y
of a text is its baseline; a picture's pixels are drawn as they arrive
and do not survive a later fill, text or line. A step is written as a
.trk line writes it, without its row: notes joined by +, OFF, i<hex>,
f<EFFECT><hex>. Whole songs go with tools/fm1_send.py, then `open`.

Every edit is the hands' own: UNDO (ARP) takes it back, and nothing is
in the flash until `save`. `open` and `new` refuse to drop unsaved
changes unless --force is given.

--udp drives a simulator on this computer (build/sim/moondaw-fm1). The
protocol is in app/link.h (version 2). MIDI needs python-rtmidi.
"""
import argparse
import re
import sys
import time

import fm1_send as link

STATUS, INPUT, OPEN, NEW, STEP, CELL, LENGTH, SAVE, PLAY, CANVAS = range(8, 18)
CLEAR, FILL, TEXT, LINE, PIXELS = range(5)
COLOURS = {"black": 0x000000, "white": 0xFFFFFF, "grey": 0x5A6470, "green": 0x7FD962, "cyan": 0x62C8D9,
           "amber": 0xF0BE5A, "red": 0x9E5555}  # app/theme.h
FONTS = {"20": 0, "14": 1, "12": 2, "mono": 3}


def rgb565(c):
    v = COLOURS.get(c.lower()) if not c.startswith("#") else int(c[1:], 16)
    if v is None:
        sys.exit(f"no colour {c}: #RRGGBB or {' '.join(COLOURS)}")
    r, g, b = v >> 16, v >> 8 & 0xFF, v & 0xFF
    return ((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3).to_bytes(2, "little")


def xy(*v):
    if any(not 0 <= int(n) <= 255 for n in v):
        sys.exit("positions and sizes are 0 to 240")
    return bytes(int(n) for n in v)
BUTTONS = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC",
           "OCT-", "OCT+"]
KNOBS = ["SELECT", "ALGO", "PRESETS", "K1", "K2", "K3", "K4"]
NAMES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
KEY0 = 53  # the keyboard's first key is F-3, unmoved by OCT
NOTE_NONE, NOTE_OFF, INSTR_NONE = 0xFF, 0xFE, 0xFF


def fx_names():
    """The effects in the file's order, read from the firmware's own list."""
    src = (link.ROOT / "core" / "song.c").read_text()
    body = src[src.index("fx_names[FX_COUNT]"):]
    body = body[:body.index("};")]
    return re.findall(r'"([A-Z0-9-]{3})"', body)


def note(t):
    m = re.fullmatch(r"([A-G][-#])(\d)", t.upper())
    if not m:
        sys.exit(f"not a note: {t} (C-4, F#3 ...)")
    return 12 * (int(m.group(2)) + 1) + NAMES.index(m.group(1))


def parse_step(text):
    """'C-4+E-4 i03 fVOL40' -> the 11 bytes of a step."""
    notes, instr, fx = [], INSTR_NONE, []
    names = fx_names()
    for tok in text.split():
        if tok == "--":
            continue
        if tok[0] == "i":
            instr = int(tok[1:], 16)
        elif tok[0] == "f" and len(tok) >= 5:
            if tok[1:4] not in names:
                sys.exit(f"no effect {tok[1:4]}; there are {' '.join(names[1:])}")
            fx.append((names.index(tok[1:4]), int(tok[4:], 16)))
        else:
            notes += [NOTE_OFF if n.upper() == "OFF" else note(n) for n in tok.split("+")]
    if len(notes) > 4 or len(fx) > 3:
        sys.exit("a step holds four notes and three effects at most")
    notes += [NOTE_NONE] * (4 - len(notes))
    fx += [(0, 0)] * (3 - len(fx))
    return bytes(notes + [instr] + [b for f in fx for b in f])


def checked(answer, what="", ok=(0,)):
    st, seq, extra, tail = answer
    if st not in ok:
        sys.exit(link.STATUS.get(st, f"refused ({st})") + (f": {what}" if what else ""))
    return answer


def slot_of(port, name):
    for slot, n, _ in link.songs(port):
        if n.upper() == name.upper():
            return slot
    sys.exit(f"no song {name} on the device (tools/fm1_send.py --list)")


def track(t):
    t = int(t)
    if not 1 <= t <= 8:
        sys.exit("tracks are 1 to 8")
    return t - 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", help="part of the MIDI port's name")
    ap.add_argument("--udp", nargs="?", const=19780, type=int, metavar="PORT",
                    help="a simulator on this computer (default port 19780)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("status"); p.add_argument("--reset", action="store_true", help="start the worst block over")
    p.add_argument("--profile", action="store_true", help="the worst block stage by stage, in microseconds, and the second core")
    p.add_argument("--boot", action="store_true", help="why the machine last started, its last crash, the second core")
    p = sub.add_parser("press"); p.add_argument("buttons", nargs="+")
    p = sub.add_parser("turn"); p.add_argument("knob"); p.add_argument("by", type=int)
    p = sub.add_parser("key"); p.add_argument("notes", nargs="+"); p.add_argument("--hold", type=int, default=8)
    p = sub.add_parser("play"); p.add_argument("phrase", nargs="?", default="0")
    p = sub.add_parser("loop"); p.add_argument("phrase")
    sub.add_parser("stop")
    p = sub.add_parser("open"); p.add_argument("name"); p.add_argument("--force", action="store_true")
    p = sub.add_parser("new"); p.add_argument("--force", action="store_true")
    p = sub.add_parser("step"); p.add_argument("track"); p.add_argument("block"); p.add_argument("row")
    p.add_argument("text")
    p = sub.add_parser("cell"); p.add_argument("phrase"); p.add_argument("track"); p.add_argument("block")
    p = sub.add_parser("length"); p.add_argument("track"); p.add_argument("block"); p.add_argument("steps", type=int)
    sub.add_parser("save")
    p = sub.add_parser("canvas"); p.add_argument("op", choices=["clear", "fill", "text", "line", "image"])
    p.add_argument("args", nargs="*"); p.add_argument("--font", default="20", choices=list(FONTS))
    p.add_argument("--colour", "--color", default="white")
    a = ap.parse_args()

    port = link.Udp(a.udp) if a.udp else link.Midi(a.port)
    st, version, _, _ = link.ask(port, link.PING)
    if version < 2:
        sys.exit(f"the device speaks version {version}: install a MOONDAW with the remote (version 2)")

    if a.cmd == "status":
        print(checked(link.ask(port, STATUS, bytes([(1 if a.reset else 0) | (2 if a.profile else 0) | (4 if a.boot else 0)])))[3]
              .decode("ascii", "replace").strip())
    elif a.cmd in ("press", "turn", "key"):
        pressed, keys, turns, hold = 0, 0, [0] * 7, 0
        if a.cmd == "press":
            for b in a.buttons:
                if b.upper() not in BUTTONS:
                    sys.exit(f"no button {b}; there are {' '.join(BUTTONS)}")
                pressed |= 1 << BUTTONS.index(b.upper())
        elif a.cmd == "turn":
            k = {"PRESET": "PRESETS", "KNOB1": "K1", "KNOB2": "K2", "KNOB3": "K3", "KNOB4": "K4"}.get(a.knob.upper(), a.knob.upper())
            if k not in KNOBS:
                sys.exit(f"no knob {a.knob}; there are {' '.join(KNOBS)}")
            turns[KNOBS.index(k)] = max(-128, min(127, a.by))
        else:
            for n in a.notes:
                k = note(n) - KEY0
                if not 0 <= k < 27:
                    sys.exit(f"{n} is not under the keyboard (F-3 .. G-5, before OCT)")
                keys |= 1 << k
            hold = max(1, min(255, a.hold))
        seq = int(time.time() * 1000) & 0xFFFF
        raw = (seq.to_bytes(2, "little") + pressed.to_bytes(4, "little") + keys.to_bytes(4, "little")
               + bytes([hold]) + bytes(t & 0xFF for t in turns))
        checked(link.ask(port, INPUT, raw))
    elif a.cmd in ("play", "loop", "stop"):
        mode = {"stop": 0, "play": 1, "loop": 2}[a.cmd]
        phrase = 0 if a.cmd == "stop" else int(a.phrase, 16)
        checked(link.ask(port, PLAY, bytes([mode, phrase])), "no such phrase")
    elif a.cmd == "open":
        checked(link.ask(port, OPEN, bytes([slot_of(port, a.name), 1 if a.force else 0])))
    elif a.cmd == "new":
        checked(link.ask(port, NEW, bytes([1 if a.force else 0])))
    elif a.cmd == "step":
        checked(link.ask(port, STEP, bytes([track(a.track), int(a.block, 16), int(a.row, 16)]) + parse_step(a.text)),
                "track, block or row out of range")
    elif a.cmd == "cell":
        block = 0xFF if a.block == "--" else int(a.block, 16)
        checked(link.ask(port, CELL, bytes([int(a.phrase, 16), track(a.track), block])),
                "phrase, track or block out of range")
    elif a.cmd == "length":
        checked(link.ask(port, LENGTH, bytes([track(a.track), int(a.block, 16)]) + a.steps.to_bytes(2, "little")),
                "1 to 128 steps")
    elif a.cmd == "save":
        print(f"saved in slot {checked(link.ask(port, SAVE))[2]}")
    elif a.cmd == "canvas":
        need = {"clear": (0, 1), "fill": (5, 5), "text": (3, 3), "line": (5, 5), "image": (1, 1)}[a.op]
        if not need[0] <= len(a.args) <= need[1]:
            sys.exit(f"canvas {a.op}: see --help")
        if a.op == "clear":
            body = bytes([CLEAR]) + rgb565(a.args[0] if a.args else "black")
        elif a.op == "fill":
            body = bytes([FILL]) + xy(*a.args[:4]) + rgb565(a.args[4])
        elif a.op == "line":
            body = bytes([LINE]) + xy(*a.args[:4]) + rgb565(a.args[4])
        elif a.op == "text":
            words = a.args[2].encode("ascii", "replace")[:63]
            body = bytes([TEXT]) + xy(*a.args[:2]) + bytes([FONTS[a.font]]) + rgb565(a.colour) + words
        if a.op != "image":
            checked(link.ask(port, CANVAS, body), "the canvas's list is full, or the order is malformed")
            return
        try:
            from PIL import Image
        except ImportError:
            sys.exit("a picture needs Pillow:  pip install pillow")
        im = Image.open(a.args[0]).convert("RGB")
        if im.size != (240, 240):
            im = im.resize((240, 240))
        checked(link.ask(port, CANVAS, bytes([CLEAR]) + rgb565("black")))
        px = im.load()
        for y in range(240):
            for x0 in (0, 120):  # a message holds 120 pixels
                row = b"".join(rgb565("#%02X%02X%02X" % px[x, y]) for x in range(x0, x0 + 120))
                checked(link.ask(port, CANVAS, bytes([PIXELS, x0, y, 120]) + row))
            print(f"\r{(y + 1) * 100 // 240} %", end="", flush=True)
        print()


if __name__ == "__main__":
    main()
