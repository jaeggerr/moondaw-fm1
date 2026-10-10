#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""drift.py REF.f32 FM.i32 [WINDOW_S] : the difference window by window, to see WHEN two renders part."""
import sys, array, math
r = array.array("f"); r.frombytes(open(sys.argv[1], "rb").read())
m = array.array("i"); m.frombytes(open(sys.argv[2], "rb").read())
w = int(float(sys.argv[3]) * 44100) if len(sys.argv) > 3 else 22050
n = min(len(r), len(m)) // 2
for a in range(0, n, w):
    sr = sd = sf = 0.0
    for i in range(a, min(a + w, n)):
        x, y = r[2 * i], m[2 * i] / 32768.0
        sr += x * x; sf += y * y; sd += (x - y) ** 2
    rel = 10 * math.log10(sd / sr) if sr > 0 and sd > 0 else -200
    lv = lambda v: 10 * math.log10(v / w) if v > 0 else -200
    print(f"{a / 44100:6.2f}s  ref {lv(sr):6.1f}  fm {lv(sf):6.1f}  diff {rel:6.1f} dB")
