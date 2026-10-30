#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Play MOONDAW on the FM-1 from a MIDI keyboard on this computer.

    tools/fm1_midi.py [--from TEXT] [--to TEXT] [-v]
    tools/fm1_midi.py --list

The FM-1 is a USB device, not a host: a keyboard cannot be plugged into
it. So this computer stands between them, and forwards what the keyboard
sends to the device's own USB-MIDI port.

Notes only, and the two controllers that let everything go (120 all sound
off, 123 all notes off). The knobs' and pads' controllers are not passed
on: they would only fill a ring the update's SysEx shares, and the device
reads nothing but notes for now (platform/midi_parse.h).

What a note plays is the instrument of the screen in front of you on the
device -- where the cursor is -- at the pitch the keyboard sends; the
channel is not read. Ctrl-C ends it, letting every note go.

With no --from, the first port that is not the FM-1 itself is taken; with
no --to, the first that is. --list says what this computer can see.

A keyboard with a MIDI output of its own can go straight into the FM-1's
3.5 mm MIDI jack instead, with no computer at all: the firmware reads
that jack too. It still needs its own power -- MIDI carries none -- and,
where its output is a 5-pin DIN as the MiniLab 3's is, a DIN to 3.5 mm
TRS cable.

MIDI needs python-rtmidi.
"""
import argparse
import sys
import time

DEVICE = ("fm-1", "moondaw", "felucca")
NOTE = ("C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-")


def note_name(n):
    return f"{NOTE[n % 12]}{n // 12 - 1}"


def find(names, want, keys=None, avoid=()):
    """The ports matching --from / --to, else those `keys` names, else the rest."""
    if want:
        return [i for i, n in enumerate(names) if want.lower() in n.lower()]
    if keys:
        return [i for i, n in enumerate(names) if any(k in n.lower() for k in keys)]
    return [i for i, n in enumerate(names) if not any(k in n.lower() for k in avoid)]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--from", dest="src", help="part of the keyboard's port name")
    ap.add_argument("--to", dest="dst", help="part of the FM-1's port name")
    ap.add_argument("--list", action="store_true", help="the MIDI ports of this computer")
    ap.add_argument("-v", "--verbose", action="store_true", help="say every note as it goes")
    a = ap.parse_args()

    try:
        import rtmidi
    except ImportError:
        sys.exit("MIDI needs python-rtmidi:  pip install python-rtmidi")
    inp, out = rtmidi.MidiIn(), rtmidi.MidiOut()
    ins, outs = inp.get_ports(), out.get_ports()

    if a.list:
        print("keyboards (in):  " + ("\n                 ".join(ins) or "(none)"))
        print("devices (out):   " + ("\n                 ".join(outs) or "(none)"))
        return 0

    src = find(ins, a.src, None, DEVICE)
    dst = find(outs, a.dst, DEVICE)
    if not src:
        sys.exit(f"no keyboard on MIDI in. The ports are:\n  " + ("\n  ".join(ins) or "(none)") +
                 "\nName part of one with --from.")
    if not dst:
        sys.exit(f"the FM-1 is not on MIDI out (is it plugged in and on?). The ports are:\n  " +
                 ("\n  ".join(outs) or "(none)") + "\nName part of one with --to.")

    inp.open_port(src[0])
    out.open_port(dst[0])
    inp.ignore_types(sysex=True, timing=True, active_sense=True)
    print(f"{ins[src[0]]}  ->  {outs[dst[0]]}      (Ctrl-C to stop)")

    try:
        while True:
            m = inp.get_message()
            if not m:
                time.sleep(0.001)
                continue
            msg, status = m[0], m[0][0] & 0xF0
            if status in (0x80, 0x90) and len(msg) >= 3:
                out.send_message(msg[:3])
                if a.verbose:
                    on = status == 0x90 and msg[2]
                    print(f"{note_name(msg[1])} {msg[2]:3d}" if on else f"{note_name(msg[1])}  off")
            elif status == 0xB0 and len(msg) >= 3 and msg[1] in (120, 123):
                out.send_message(msg[:3])
    except KeyboardInterrupt:
        pass
    finally:
        out.send_message([0xB0, 123, 0])  # nothing left hanging on the device
    return 0


if __name__ == "__main__":
    sys.exit(main())
