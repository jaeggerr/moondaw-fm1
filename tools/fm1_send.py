#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Send a .trk to MOONDAW on the FM-1, over USB-MIDI.

    tools/fm1_send.py SONG.trk [--name NAME] [--replace] [--port TEXT]
    tools/fm1_send.py --ping

The device must be on its MENU. The song is first reduced to what the
FM-1 carries, by the same reader and writer the firmware uses
(tests/trkdump.c, compiled on first use), so what is sent is exactly what
the machine would save. It lands in the flash; nothing on the device is
replaced unless --replace is given.

--udp sends to a simulator on this computer instead of a MIDI port
(build/sim/moondaw-fm1 listens on 127.0.0.1:19780).

The protocol is in app/link.h. MIDI needs python-rtmidi.
"""
import argparse
import pathlib
import socket
import subprocess
import sys
import time
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SIG = bytes([0x7D, 0x4D, 0x44])
BEGIN, DATA, END, CANCEL, PING, ANSWER = 1, 2, 3, 4, 5, 0x7F
DATA_MAX = 240
STATUS = {
    1: "the device is busy: go back to its MENU and try again",
    2: "a song of that name is already on the device (--replace to overwrite it)",
    3: "not enough room in the device's flash",
    4: "a message arrived out of turn",
    5: "the song arrived damaged; nothing was kept",
    6: "the device could not write its flash",
    7: "that song is open on the device with unsaved changes: save it there first",
    8: "the device did not understand the message",
}


def pack7(data):
    out = bytearray()
    for i in range(0, len(data), 7):
        g = data[i:i + 7]
        out.append(sum((b >> 7) << j for j, b in enumerate(g)))
        out += bytes(b & 0x7F for b in g)
    return bytes(out)


def unpack7(s):
    out = bytearray()
    for i in range(0, len(s), 8):
        g = s[i:i + 8]
        out += bytes(b | ((g[0] >> j) & 1) << 7 for j, b in enumerate(g[1:]))
    return bytes(out)


class Udp:
    def __init__(self, port):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.to = ("127.0.0.1", port)

    def send(self, body):
        self.s.sendto(body, self.to)

    def read(self, timeout):
        self.s.settimeout(timeout)
        try:
            return self.s.recvfrom(1024)[0]
        except (socket.timeout, ConnectionError):
            return None


class Midi:
    def __init__(self, want):
        try:
            import rtmidi
        except ImportError:
            sys.exit("MIDI needs python-rtmidi:  pip install python-rtmidi")
        self.out, self.inp = rtmidi.MidiOut(), rtmidi.MidiIn()
        outs, ins = self.out.get_ports(), self.inp.get_ports()
        keys = [want.lower()] if want else ["fm-1", "moondaw", "felucca"]
        o = [i for i, n in enumerate(outs) if any(k in n.lower() for k in keys)]
        i = [i for i, n in enumerate(ins) if any(k in n.lower() for k in keys)]
        if not o or not i:
            names = "\n  ".join(sorted(set(outs + ins))) or "(none)"
            sys.exit(f"no MIDI port matching {' / '.join(keys)}. The ports are:\n  {names}\n"
                     "Name part of one with --port.")
        self.out.open_port(o[0])
        self.inp.open_port(i[0])
        self.inp.ignore_types(sysex=False, timing=True, active_sense=True)
        print(f"port: {outs[o[0]]}")

    def send(self, body):
        self.out.send_message([0xF0, *body, 0xF7])

    def read(self, timeout):
        end = time.time() + timeout
        while time.time() < end:
            m = self.inp.get_message()
            if m and m[0] and m[0][0] == 0xF0 and m[0][-1] == 0xF7:
                return bytes(m[0][1:-1])
            if not m:
                time.sleep(0.002)
        return None


def ask(link, cmd, raw=b"", tries=4, timeout=1.5):
    """One message, and its answer: (status, seq, extra). The device holds a single SysEx at a time."""
    for _ in range(tries):
        link.send(SIG + bytes([cmd]) + pack7(raw))
        end = time.time() + timeout
        while time.time() < end:
            m = link.read(max(0.01, end - time.time()))
            if not m or m[:4] != SIG + bytes([ANSWER]):
                continue
            a = unpack7(m[4:])
            if len(a) == 8 and a[0] == cmd:
                return a[1], a[2] | a[3] << 8, int.from_bytes(a[4:8], "little")
    sys.exit("no answer from the device. Is MOONDAW running on it, and the cable a data cable?")


def as_fm1(path):
    """The file as the FM-1 would save it."""
    tool = ROOT / "build" / "trkdump"
    if not tool.exists():
        tool.parent.mkdir(exist_ok=True)
        subprocess.run(["cc", "-std=c11", "-O2", str(ROOT / "tests/trkdump.c"), str(ROOT / "core/song.c"),
                        str(ROOT / "core/trk.c"), "-o", str(tool)], check=True)
    r = subprocess.run([str(tool), str(path)], capture_output=True)
    if r.returncode:
        sys.exit(f"{path}: not a version 3 .trk")
    return r.stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("song", nargs="?")
    ap.add_argument("--name", help="the name on the device (default: the file's, 12 characters)")
    ap.add_argument("--replace", action="store_true", help="overwrite a song of the same name")
    ap.add_argument("--port", help="part of the MIDI port's name")
    ap.add_argument("--udp", nargs="?", const=19780, type=int, metavar="PORT",
                    help="a simulator on this computer instead of MIDI")
    ap.add_argument("--raw", action="store_true", help="send the file as it is, not reduced")
    ap.add_argument("--ping", action="store_true", help="only ask the device how much room it has")
    a = ap.parse_args()
    if not a.song and not a.ping:
        ap.error("a .trk to send, or --ping")

    link = Udp(a.udp) if a.udp else Midi(a.port)
    st, ver, free = ask(link, PING)
    print(f"device: link version {ver}, {free // 1024} KB free")
    if a.ping:
        return
    if ver != 1:
        sys.exit("this tool speaks version 1 of the link")

    path = pathlib.Path(a.song)
    data = path.read_bytes() if a.raw else as_fm1(path)
    name = (a.name or path.stem).upper()[:12]
    if not a.raw:
        print(f"{path.name}: {path.stat().st_size // 1024} KB on disk, "
              f"{(len(data) + 1023) // 1024} KB as the FM-1 keeps it")
    head = bytes([1 if a.replace else 0]) + len(data).to_bytes(4, "little") + \
        zlib.crc32(data).to_bytes(4, "little") + name.encode("ascii", "replace")
    st, _, _ = ask(link, BEGIN, head)
    if st:
        sys.exit(STATUS.get(st, f"refused ({st})"))
    for seq, at in enumerate(range(0, len(data), DATA_MAX)):
        st, _, _ = ask(link, DATA, seq.to_bytes(2, "little") + data[at:at + DATA_MAX])
        if st:
            ask(link, CANCEL)
            sys.exit(STATUS.get(st, f"refused ({st})"))
        print(f"\r{name}: {min(at + DATA_MAX, len(data)) * 100 // len(data)} %", end="", flush=True)
    st, _, got = ask(link, END, timeout=5)
    print()
    if st:
        sys.exit(STATUS.get(st, f"refused ({st})"))
    print(f"{name}: {got} bytes on the device")


if __name__ == "__main__":
    main()
