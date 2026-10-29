#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The sample bank of MOONDAW on the FM-1: fill it from the computer.

    tools/fm1_samples.py add FILE.wav [...] [--name NAME]
    tools/fm1_samples.py list
    tools/fm1_samples.py remove NAME [...]
    tools/fm1_samples.py get [NAME ...] [-o DIR]
    tools/fm1_samples.py pack OUT.bin FILE.wav [...]     (no device: the bank as a file)
    tools/fm1_samples.py list|get ... --bank FILE.bin     (no device: read that file)

A sample is a short one-shot: mono, 22 050 Hz, IMA ADPCM (four bits a
frame), one second at most. `add` converts any WAV (8, 16, 24 or 32-bit,
float, any rate, any channels): mixed to mono, resampled, the silence cut
at both ends, cut to a second, normalised to -1 dBFS. Its name is the
file's, without .wav, 23 characters at most: a song's `smp 00
tr909/tr9_kick.wav` plays the sample named tr9_kick.

The bank is one file in the device's store, beside the songs: a sample
more is a song less. The device takes it only on HOME. It is rewritten
whole each time; the tool keeps the last one it saw in build/fm1-bank.bin
(build/fm1-bank-udpPORT.bin for a simulator) so that it does not read it
back over the cable when nothing changed.

--udp talks to a simulator (build/sim/moondaw-fm1, 127.0.0.1:19780)
instead of MIDI. The format is core/bank.h; the protocol app/link.h.
"""
import argparse
import math
import pathlib
import struct
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import fm1_send as link  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent

RATE = 22050
BLOCK, BLOCK_FRAMES = 255, 505        # core/sampler.h
STORE_NAME, STORE_PAYLOAD = 12, 4080  # core/store.h
NAME_LEN, ENTRY, HEAD, MAX = 24, 32, 8, 128  # core/bank.h
BANK_SLOT = 32
MAX_FRAMES = RATE                     # one second
SILENCE_DB, PEAK_DB = -50.0, -1.0

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
        73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408,
        449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
        2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
        9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
        32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


# ------------------------------------------------------------------ codec ---
def decode_one(pred, idx, code):
    """As core/sampler.c smp_decode, to the bit."""
    step = STEP[idx]
    diff = step >> 3
    if code & 4:
        diff += step
    if code & 2:
        diff += step >> 1
    if code & 1:
        diff += step >> 2
    pred = max(-32768, min(32767, pred - diff if code & 8 else pred + diff))
    return pred, max(0, min(88, idx + INDEX[code & 7]))


def encode_one(pred, idx, x):
    diff, code, step = x - pred, 0, STEP[idx]
    if diff < 0:
        code, diff = 8, -diff
    if diff >= step:
        code |= 4
        diff -= step
    if diff >= step >> 1:
        code |= 2
        diff -= step >> 1
    if diff >= step >> 2:
        code |= 1
    return code


def encode(pcm):
    """16-bit frames -> blocks. The first block starts on the index that codes it best."""
    if not pcm:
        return b""
    first = pcm[:BLOCK_FRAMES]
    best, best_err = 0, None
    for i in range(0, 89, 2):
        pred, idx, err = first[0], i, 0
        for x in first[1:]:
            pred, idx = decode_one(pred, idx, encode_one(pred, idx, x))
            err += (x - pred) ** 2
        if best_err is None or err < best_err:
            best, best_err = i, err
    out, idx = bytearray(), best
    for at in range(0, len(pcm), BLOCK_FRAMES):
        chunk = pcm[at:at + BLOCK_FRAMES]
        start = idx
        pred, codes = chunk[0], []
        for x in chunk[1:]:
            c = encode_one(pred, idx, x)
            pred, idx = decode_one(pred, idx, c)
            codes.append(c)
        codes += [0] * (BLOCK_FRAMES - 1 - len(codes))
        out += struct.pack("<hB", chunk[0], start)
        out += bytes(codes[i] | codes[i + 1] << 4 for i in range(0, len(codes), 2))
    return bytes(out)


def decode(data, frames):
    out = []
    for at in range(0, len(data), BLOCK):
        if len(out) >= frames:
            break
        pred, idx = struct.unpack_from("<hB", data, at)
        idx = min(idx, 88)
        out.append(pred)
        for b in data[at + 3:at + BLOCK]:
            for c in (b & 15, b >> 4):
                if len(out) >= frames:
                    break
                pred, idx = decode_one(pred, idx, c)
                out.append(pred)
    return out[:frames]


# ------------------------------------------------------------------- WAVs ---
def read_wav(path):
    """(rate, mono frames as floats in -1..1). PCM 8/16/24/32-bit and float 32/64."""
    b = pathlib.Path(path).read_bytes()
    if b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        raise ValueError("not a WAV file")
    fmt, data, at = None, None, 12
    while at + 8 <= len(b):
        cid, size = b[at:at + 4], struct.unpack_from("<I", b, at + 4)[0]
        body = b[at + 8:at + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", body)
            if fmt[0] == 0xFFFE and len(body) >= 26:  # extensible: the sub-format's tag
                fmt = (struct.unpack_from("<H", body, 24)[0],) + fmt[1:]
        elif cid == b"data":
            data = body
        at += 8 + size + (size & 1)
    if not fmt or data is None:
        raise ValueError("no fmt or data chunk")
    tag, ch, rate, _, align, bits = fmt
    width = bits // 8
    n = len(data) // align
    if tag == 3 and bits in (32, 64):
        raw = struct.unpack_from(f"<{n * ch}{'f' if bits == 32 else 'd'}", data)
    elif tag == 1 and bits == 8:
        raw = [(v - 128) / 128.0 for v in data[:n * ch]]
    elif tag == 1 and bits in (16, 24, 32):
        full = float(1 << (bits - 1))
        raw = [int.from_bytes(data[i * width:(i + 1) * width], "little", signed=True) / full
               for i in range(n * ch)]
    else:
        raise ValueError(f"unsupported WAV (format {tag}, {bits} bits)")
    return rate, [sum(raw[i * ch:(i + 1) * ch]) / ch for i in range(n)]


def resample(x, src, dst):
    """Windowed-sinc low-pass when going down, then linear interpolation."""
    if src == dst or not x:
        return list(x)
    if dst < src:
        fc, half = 0.45 * dst / src, 16
        taps = []
        for k in range(-half, half + 1):
            s = 2 * fc if k == 0 else math.sin(2 * math.pi * fc * k) / (math.pi * k)
            taps.append(s * (0.42 + 0.5 * math.cos(math.pi * k / (half + 1)) +
                             0.08 * math.cos(2 * math.pi * k / (half + 1))))
        g = sum(taps)
        taps = [t / g for t in taps]
        n = len(x)
        x = [sum(taps[j] * x[i + j - half] for j in range(len(taps)) if 0 <= i + j - half < n)
             for i in range(n)]
    out, ratio = [], src / dst
    for i in range(int(len(x) / ratio)):
        p = i * ratio
        k = int(p)
        f = p - k
        out.append(x[k] * (1 - f) + (x[k + 1] if k + 1 < len(x) else 0.0) * f)
    return out


def prepare(path):
    """A WAV as the bank keeps it: 16-bit frames at RATE, trimmed, cut, normalised."""
    rate, x = read_wav(path)
    x = resample(x, rate, RATE)
    peak = max((abs(v) for v in x), default=0.0)
    if peak <= 0:
        raise ValueError("silent")
    floor = peak * 10 ** (SILENCE_DB / 20)
    first = next(i for i, v in enumerate(x) if abs(v) > floor)
    last = len(x) - next(i for i, v in enumerate(reversed(x)) if abs(v) > floor)
    x = x[first:last]
    cut = len(x) > MAX_FRAMES
    x = x[:MAX_FRAMES]
    fade = RATE // 200 if cut else RATE // 1000  # 5 ms where the sound was cut, 1 ms at its own end
    for i in range(min(fade, len(x))):
        x[len(x) - 1 - i] *= i / fade
    g = 10 ** (PEAK_DB / 20) / peak
    return [max(-32768, min(32767, int(round(v * g * 32767)))) for v in x], cut


def write_wav(path, pcm):
    data = struct.pack(f"<{len(pcm)}h", *pcm)
    head = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(data), b"WAVE", b"fmt ", 16, 1, 1,
                       RATE, RATE * 2, 2, 16, b"data", len(data))
    pathlib.Path(path).write_bytes(head + data)


# ------------------------------------------------------------------- bank ---
def sample_name(path):
    stem = pathlib.Path(path).name
    if stem.lower().endswith(".wav"):
        stem = stem[:-4]
    return stem[:NAME_LEN - 1]


def build(samples):
    """[(name, frames, adpcm)] -> the bank file (core/bank.h)."""
    if len(samples) > MAX:
        sys.exit(f"the bank holds {MAX} samples at most")
    at = HEAD + ENTRY * len(samples)
    at += -(at + STORE_NAME) % BLOCK
    index, body = bytearray(b"SMP1" + bytes([len(samples), 0, 0, 0])), bytearray()
    for name, frames, data in samples:
        raw = name.encode("ascii", "replace")[:NAME_LEN - 1]
        index += raw + bytes(NAME_LEN - len(raw)) + struct.pack("<II", frames, at + len(body))
        body += data
    pad = at - len(index)
    return bytes(index) + bytes(pad) + bytes(body)


def parse(bank):
    """The bank file -> [(name, frames, adpcm)]."""
    if not bank:
        return []
    if bank[:4] != b"SMP1":
        sys.exit("the device's bank is not one this tool reads")
    out = []
    for i in range(bank[4]):
        e = HEAD + i * ENTRY
        name = bank[e:e + NAME_LEN].split(b"\0")[0].decode("ascii", "replace")
        frames, at = struct.unpack_from("<II", bank, e + NAME_LEN)
        n = -(-frames // BLOCK_FRAMES) * BLOCK
        out.append((name, frames, bank[at:at + n]))
    return out


def sectors(size):
    """What a file of `size` bytes takes of the store."""
    return -(-(size + STORE_NAME) // STORE_PAYLOAD)


# ----------------------------------------------------------------- device ---
def connect(a):
    port = link.Udp(a.udp) if a.udp else link.Midi(a.port)
    st, ver, free, _ = link.ask(port, link.PING)
    if ver < 5:
        sys.exit(f"the device speaks version {ver} of the link: install a MOONDAW with the sample bank (5)")
    return port, free


def cache(a):
    """The last bank this tool saw on that device: one file per simulator port."""
    return ROOT / "build" / (f"fm1-bank-udp{a.udp}.bin" if a.udp else "fm1-bank.bin")


def device_bank(port, path):
    """The bank on the device, from the cache when its index and size say it is the same."""
    st, _, size, _ = link.ask(port, link.INFO, bytes([BANK_SLOT]))
    if st == 9:
        return b""
    if st:
        sys.exit(link.STATUS.get(st, f"refused ({st})"))
    cached = path.read_bytes() if path.exists() else b""
    head = bytearray()
    while len(head) < HEAD or len(head) < HEAD + ENTRY * head[4]:
        st, n, off, tail = link.ask(port, link.READ, bytes([BANK_SLOT]) + len(head).to_bytes(4, "little"))
        if st:
            sys.exit(link.STATUS.get(st, f"refused ({st})"))
        if not n:
            break
        head += tail
    link.ask(port, link.CANCEL)  # the device stops sending: it is free for what comes next
    want = HEAD + ENTRY * head[4] if len(head) >= HEAD else len(head)
    if len(cached) == size and cached[:want] == bytes(head[:want]):
        return cached
    data = link.fetch(port, BANK_SLOT, "bank", size)
    path.parent.mkdir(exist_ok=True)
    path.write_bytes(data)
    return data


def upload(port, bank, path):
    head = bytes([2]) + len(bank).to_bytes(4, "little") + zlib.crc32(bank).to_bytes(4, "little") + b".SAMPLES"
    st, _, _, _ = link.ask(port, link.BEGIN, head, timeout=8)
    if st:
        sys.exit(link.STATUS.get(st, f"refused ({st})"))
    for seq, at in enumerate(range(0, len(bank), link.DATA_MAX)):
        st, _, _, _ = link.ask(port, link.DATA, seq.to_bytes(2, "little") + bank[at:at + link.DATA_MAX])
        if st:
            link.ask(port, link.CANCEL)
            sys.exit(link.STATUS.get(st, f"refused ({st})"))
        print(f"\rbank: {min(at + link.DATA_MAX, len(bank)) * 100 // len(bank)} %", end="", flush=True)
    st, _, got, _ = link.ask(port, link.END, timeout=5)
    print()
    if st:
        sys.exit(link.STATUS.get(st, f"refused ({st})"))
    path.parent.mkdir(exist_ok=True)
    path.write_bytes(bank)


def kb(n):
    return f"{(n + 1023) // 1024} KB"


def show(samples):
    for name, frames, data in samples:
        print(f"  {name:<23} {frames * 1000 // RATE:>4} ms  {kb(len(data)):>6}")
    total = sum(f for _, f, _ in samples)
    print(f"{len(samples)} sample{'s' if len(samples) != 1 else ''}, {total / RATE:.1f} s")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("cmd", choices=["add", "list", "remove", "get", "pack"])
    ap.add_argument("args", nargs="*")
    ap.add_argument("--name", help="add: the name of the one sample added")
    ap.add_argument("-o", "--out", default=".", help="get: the folder the WAVs go to")
    ap.add_argument("--port", help="part of the MIDI port's name")
    ap.add_argument("--udp", nargs="?", const=19780, type=int, metavar="PORT",
                    help="a simulator on this computer instead of MIDI")
    ap.add_argument("--bank", help="list, get: a bank file (from pack) instead of the device")
    a = ap.parse_intermixed_args()

    def converted(paths):
        if a.name and len(paths) != 1:
            sys.exit("--name names one sample")
        out = []
        for p in paths:
            try:
                pcm, cut = prepare(p)
            except (OSError, ValueError, StopIteration) as e:
                sys.exit(f"{p}: {e or 'silent'}")
            name = (a.name or sample_name(p))[:NAME_LEN - 1]
            print(f"  {name:<23} {len(pcm) * 1000 // RATE:>4} ms{'  (cut to 1 s)' if cut else ''}")
            out.append((name, len(pcm), encode(pcm)))
        return out

    if a.cmd == "pack":
        if len(a.args) < 2:
            ap.error("pack OUT.bin FILE.wav [...]")
        bank = build(converted(a.args[1:]))
        pathlib.Path(a.args[0]).write_bytes(bank)
        print(f"{a.args[0]}: {kb(len(bank))}, {sectors(len(bank))} sectors of the store")
        return

    if a.cmd in ("add", "remove") and not a.args:
        ap.error(f"{a.cmd} needs a file or a name")
    if a.bank and a.cmd not in ("list", "get"):
        ap.error("--bank is for list and get")
    port, free = (None, 0) if a.bank else connect(a)
    have = parse(pathlib.Path(a.bank).read_bytes() if a.bank else device_bank(port, cache(a)))
    old = sum(len(d) for _, _, d in have)

    if a.cmd == "list":
        show(have)
        if not a.bank:
            print(f"device: {kb(free)} free")
        return
    if a.cmd == "get":
        want = [s for s in have if not a.args or s[0] in a.args]
        if a.args and len(want) != len(a.args):
            sys.exit(f"not in the bank: {', '.join(set(a.args) - {s[0] for s in want})}")
        out = pathlib.Path(a.out)
        out.mkdir(parents=True, exist_ok=True)
        for name, frames, data in want:
            write_wav(out / f"{name}.wav", decode(data, frames))
            print(f"  {out / (name + '.wav')}")
        return

    if a.cmd == "add":
        new = converted(a.args)
        names = {n for n, _, _ in new}
        samples = [s for s in have if s[0] not in names] + new
    else:
        gone = [s for s in have if s[0] in a.args]
        if len(gone) != len(a.args):
            sys.exit(f"not in the bank: {', '.join(set(a.args) - {s[0] for s in gone})}")
        samples = [s for s in have if s[0] not in a.args]
    bank = build(samples)
    # in sectors: the old bank goes first when the new one does not fit beside it
    room = (free + STORE_NAME) // STORE_PAYLOAD + (sectors(len(build(have))) if have else 0)
    print(f"device: {kb(free)} free, the bank {kb(old)} in {len(have)} samples")
    if sectors(len(bank)) > room:
        sys.exit(f"the bank would take {kb(len(bank))}: not enough room "
                 f"({kb(room * STORE_PAYLOAD)} with the old bank's)")
    upload(port, bank, cache(a))
    _, _, after, _ = link.ask(port, link.PING)
    show(samples)
    print(f"device: {kb(after)} free (was {kb(free)})")


if __name__ == "__main__":
    main()
