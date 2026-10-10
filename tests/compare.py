#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""compare.py REF.f32 FM.i32 [--max-db=N] : how far the FM-1 core is from moondaw.

Two measures, because free-running oscillators cannot be compared sample for sample over a
whole song: one clock of difference in a period and two identical sounds are out of phase
ten seconds later.

  wave   the difference of the two waveforms, relative to the reference. Meaningful on a
         short render; it is what catches a wrong level or a note a tick late.
  spec   the difference of their short-time spectra (magnitudes, 46 ms windows), which does
         not see phase. This is the one the verdict is on, default -20 dB.
"""
import sys, math
import numpy as np

args = [a for a in sys.argv[1:] if not a.startswith("--")]
max_db = -20.0
for a in sys.argv[1:]:
    if a.startswith("--max-db="):
        max_db = float(a.split("=")[1])
ref = np.fromfile(args[0], dtype="<f4").astype(np.float64)
fm = np.fromfile(args[1], dtype="<i4").astype(np.float64) / 32768.0
n = min(len(ref), len(fm)) // 2 * 2
ref, fm = ref[:n].reshape(-1, 2), fm[:n].reshape(-1, 2)
db = lambda x: 10 * math.log10(x) if x > 0 else -200.0
pr, pf = float(np.mean(ref ** 2)), float(np.mean(fm ** 2))
if pr == 0 or pf == 0:
    print(f"SILENT  ref {db(pr):.1f} dB, fm {db(pf):.1f} dB")
    sys.exit(1)
wave = db(float(np.mean((ref - fm) ** 2)) / pr)

def spec(x):
    w, hop = 2048, 1024
    win = np.hanning(w)
    out = []
    for ch in range(2):
        frames = np.lib.stride_tricks.sliding_window_view(x[:, ch], w)[::hop]
        out.append(np.abs(np.fft.rfft(frames * win, axis=1)))
    return np.concatenate(out)

sr, sf = spec(ref), spec(fm)
spec_db = db(float(np.sum((sr - sf) ** 2)) / float(np.sum(sr ** 2)))
ok = spec_db <= max_db and abs(db(pf) - db(pr)) < 1.0
print(f"{'ok  ' if ok else 'FAIL'}  level ref {db(pr):6.1f} dB, fm {db(pf):6.1f} dB   "
      f"wave {wave:6.1f} dB   spec {spec_db:6.1f} dB   ({n // 2 / 44100:.0f} s)")
sys.exit(0 if ok else 1)
