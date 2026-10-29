# SPDX-License-Identifier: GPL-3.0-only
"""Screen prototypes on the FM-1's own panel, alive, from a script on this computer.

The machine shows a CANVAS (app/canvas.c): the script draws on it -- rectangles,
text in the machine's fonts, lines, pictures -- and collects the gestures made
on it (buttons, knobs, keys) to answer them and draw again. No firmware is
built for it. HOME leaves the canvas; GLO is the script's.

    from fm1_proto import Canvas
    c = Canvas(udp=None)            # the FM-1 on USB-MIDI; udp=19780 for the simulator
    c.keep_glo()                    # GLO is the prototype's own "back"
    def frame(ev):                  # each time something happened (and once at the start)
        c.clear("black")
        c.text(12, 30, f"turned {ev.turns.get('K2', 0)}")
        c.show()                    # what was drawn goes in as few messages as it can
    c.run(frame)                    # until the hand leaves the canvas

Colours are #RRGGBB or the theme's words (black white grey green cyan amber
red, app/theme.h). y of a text is its baseline. Fonts: "20" "14" "12" (the
condensed face) and "mono" (8 px). The protocol is in app/link.h (version 2).
"""
import time

import fm1_send as link

CANVAS, EVENTS = 0x11, 0x12
CLEAR, FILL, TEXT, LINE, PIXELS, KEEP = range(6)
COLOURS = {"black": 0x000000, "white": 0xFFFFFF, "grey": 0x5A6470, "green": 0x7FD962, "cyan": 0x62C8D9,
           "amber": 0xF0BE5A, "red": 0x9E5555}  # app/theme.h
FONTS = {"20": 0, "14": 1, "12": 2, "mono": 3}
BUTTONS = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"]
KNOBS = ["SELECT", "ALGO", "PRESETS", "K1", "K2", "K3", "K4"]
NAMES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
KEY0 = 53     # the keyboard's first key: F-3
BATCH = 230   # bytes of orders in one message (the firmware takes 258)


def rgb565(c):
    if isinstance(c, int):
        v = c
    elif c.startswith("#"):
        v = int(c[1:], 16)
    elif c.lower() in COLOURS:
        v = COLOURS[c.lower()]
    else:
        raise ValueError(f"no colour {c}: #RRGGBB or {' '.join(COLOURS)}")
    r, g, b = v >> 16, v >> 8 & 0xFF, v & 0xFF
    return ((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3).to_bytes(2, "little")


def key_name(k):
    n = KEY0 + k
    return f"{NAMES[n % 12]}{n // 12 - 1}"


class Events:
    """What the hands did on the canvas since the last look."""

    def __init__(self, raw, shown):
        u32 = lambda i: int.from_bytes(raw[i:i + 4], "little")
        bits = lambda v, names: [names[i] for i in range(len(names)) if v >> i & 1]
        keys = lambda v: [key_name(i) for i in range(27) if v >> i & 1]
        self.pressed = bits(u32(0), BUTTONS)
        self.released = bits(u32(4), BUTTONS)
        self.keys_down, self.keys_up, self.keys_held = keys(u32(8)), keys(u32(12)), keys(u32(16))
        self.turns = {KNOBS[i]: int.from_bytes(raw[20 + i:21 + i], "little", signed=True)
                      for i in range(7) if raw[20 + i]}
        self.shown = shown  # False once the hand left the canvas

    def __bool__(self):
        return bool(self.pressed or self.released or self.keys_down or self.keys_up or self.turns)

    def __repr__(self):
        parts = [f"{k}={v}" for k, v in vars(self).items() if v and k != "shown"]
        return "Events(" + " ".join(parts) + ("" if self.shown else " left") + ")"


class Canvas:
    def __init__(self, udp=None, port=None, link_port=None):
        self.port = link_port or (link.Udp(udp) if udp else link.Midi(port))
        st, version, _, _ = link.ask(self.port, link.PING)
        if version < 2:
            raise SystemExit(f"the device speaks version {version}: install a MOONDAW with the canvas")
        self.orders = []

    # -- drawing: kept here until show() -------------------------------------------
    def _order(self, op, body):
        self.orders.append(bytes([op, len(body)]) + body)
        return self

    def clear(self, colour="black"):
        self.orders = []  # what was not shown yet is drawn over anyway
        return self._order(CLEAR, rgb565(colour))

    def fill(self, x, y, w, h, colour):
        return self._order(FILL, bytes([x, y, w, h]) + rgb565(colour))

    def line(self, x0, y0, x1, y1, colour):
        return self._order(LINE, bytes([x0, y0, x1, y1]) + rgb565(colour))

    def text(self, x, y, words, colour="white", font="20"):
        s = str(words).encode("ascii", "replace")[:63]
        return self._order(TEXT, bytes([x, y, FONTS[str(font)]]) + rgb565(colour) + s)

    def keep_glo(self, keep=True):
        """GLO goes to the script instead of leaving the canvas. Sent at once: a
        later clear() must not drop it, and it opens the canvas."""
        self._send(bytes([KEEP, 1, 1 if keep else 0]))
        return self

    def show(self):
        """Send what was drawn, in as few messages as it fits."""
        msg = b""
        for o in self.orders:
            if len(msg) + len(o) > BATCH:
                self._send(msg)
                msg = b""
            msg += o
        if msg:
            self._send(msg)
        self.orders = []

    def _send(self, body):
        st = link.ask(self.port, CANVAS, body)[0]
        if st:
            raise RuntimeError("the canvas refused an order: its list is full (4 KB), or one is malformed")

    def image(self, path, progress=None):
        """A picture, 240 x 240, sent a row at a time -- about ten seconds. Pixels are
        drawn as they arrive and do not survive a later fill, text or line."""
        from PIL import Image
        im = Image.open(path).convert("RGB")
        if im.size != (240, 240):
            im = im.resize((240, 240))
        px = im.load()
        self.clear("black").show()
        for y in range(240):
            for x0 in (0, 80, 160):  # 80 pixels an order, an order a message
                row = b"".join(rgb565("#%02X%02X%02X" % px[x, y]) for x in range(x0, x0 + 80))
                self._send(bytes([PIXELS, 3 + len(row), x0, y, 80]) + row)
            if progress:
                progress(y + 1)

    # -- the hands ---------------------------------------------------------------------
    def events(self):
        st, _, shown, tail = link.ask(self.port, EVENTS)
        if st or len(tail) < 27:
            raise RuntimeError(link.STATUS.get(st, f"EVENTS refused ({st})"))
        return Events(tail, bool(shown))

    def run(self, frame, fps=30):
        """Calls frame(events) once at the start and then each time the hands did
        something, until they leave the canvas (HOME, or GLO when not kept)."""
        frame(Events(bytes(27), True))
        while True:
            ev = self.events()
            if not ev.shown:
                return
            if ev:
                frame(ev)
            time.sleep(1 / fps)
