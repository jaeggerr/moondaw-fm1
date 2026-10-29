# SPDX-License-Identifier: GPL-3.0-only
"""A song's drums onto the DRUM engine: each drum instrument becomes a DRUM
kit (its gain, release and name kept: the mix as it was; its filter and
drive dropped), and every note it plays the General MIDI note of its drum,
whatever pitch was written.

    python3 tools/drums/to_drum.py IN.trk OUT.trk 00=36 01=39 0B=tom ...

`tom` spreads the written pitches over the six GM toms (41..50). The rest
of the song is left as it is, byte for byte."""
import re
import sys

NOTES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]


def note_num(t):
    return (int(t[2]) + 1) * 12 + NOTES.index(t[:2])


def note_name(n):
    return NOTES[n % 12] + str(n // 12 - 1)


def tom(n):
    for floor, gm in ((60, 50), (57, 48), (55, 47), (53, 45), (50, 43)):
        if n >= floor:
            return gm
    return 41


def convert(text, mapping):
    out, last, cur_track = [], {}, None
    note_rx = re.compile(r"^[A-G][-#]\d$")
    for line in text.split("\n"):
        m = re.match(r"^inst ([0-9A-F]{2}) ", line)
        if m and m.group(1) in mapping:
            line = re.sub(r"engine \S+ timbre \d+", "engine DRUM timbre 0", line)
            line = re.sub(r" adsr \d+ \d+ \d+ (\d+)", r" adsr 0 0 255 \1", line)
            line = re.sub(r" filter \d+ \d+ \d+( \d+)?", "", line)
            line = re.sub(r" drive \d+", "", line)
            out.append(line)
            continue
        m = re.match(r"^pat t(\d) ", line)
        if m:
            cur_track = m.group(1)
            out.append(line)
            continue
        m = re.match(r"^([0-9A-F]{2}) (\S+)(.*)$", line)
        if cur_track is not None and m:
            row, notes, rest = m.groups()
            i = re.search(r"\bi([0-9A-F]{2})\b", rest)
            if i:
                last[cur_track] = i.group(1)
            inst = last.get(cur_track)
            if inst in mapping and all(note_rx.match(n) for n in notes.split("+")):
                to = mapping[inst]
                gm = [tom(note_num(n)) if to == "tom" else int(to) for n in notes.split("+")]
                notes = "+".join(note_name(g) for g in dict.fromkeys(gm))
                rest = re.sub(r" fSWP[0-9A-F]{2}", "", rest)
                line = f"{row} {notes}{rest}"
            out.append(line)
            continue
        if not line.strip() or line.startswith("arr "):
            cur_track = None if line.startswith("arr ") else cur_track
        out.append(line)
    return "\n".join(out)


def main():
    src, dst, *pairs = sys.argv[1:]
    mapping = dict(p.split("=") for p in pairs)
    with open(src) as f:
        text = f.read()
    with open(dst, "w") as f:
        f.write(convert(text, mapping))


if __name__ == "__main__":
    main()
