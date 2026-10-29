# SPDX-License-Identifier: GPL-3.0-only
"""Drum engine prototypes, offline, for listening: four candidates and the
current MOONDAW drums, the same kit on the same grid.

    python3 tools/drums/drums.py            (from the repository root)

Writes docs/drums-demo/*.wav. Floats, sample by sample: what each engine
does, not how the FM-1 would compute it (see docs/drums-demo/README.md).

Every candidate is written here from the technique, not copied:
  A FELUCCA  the design of Felucca's drum_voice.c (GPL-3.0-only): damped
             sines with swept pitch, a click and a drive; a six-square
             metal source for the hats
  B ANALOG   Mutable Instruments Plaits/Peaks' analog models (MIT): a
             resonant filter struck by a pulse (kick, tom), five modes
             plus noise (snare), the 808's six squares (hats)
  C SYNTH    Plaits' synthetic models (MIT), 909-like: a shaped sine
             under an FM envelope, two sines over band noise, ring-
             modulated square/saw pairs for the hats
  D CHIP+    the current engines with what they lack: a pitch envelope
             and a decay computed per sample, an LFSR's noise high-passed
"""
import math
import os
import struct
import subprocess
import sys

import numpy as np

FS = 44100                      # SYNTH_FS
ROW = 60.0 / 120 / 4            # a 16th at 120 bpm: the .trk's row
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "docs", "drums-demo")
rng = np.random.default_rng(1234)


# ------------------------------------------------------------ helpers ---
def tau_k(tau_s):
    """Per-sample factor of an exponential decay of time constant tau."""
    return math.exp(-1.0 / (tau_s * FS))


class Svf:
    """Trapezoidal state-variable filter (stmlib's topology)."""

    def __init__(self, f_hz, q):
        self.set(f_hz, q)
        self.s1 = self.s2 = 0.0

    def set(self, f_hz, q):
        f = min(f_hz / FS, 0.497)
        self.g = math.tan(math.pi * f)
        self.r = 1.0 / q
        self.h = 1.0 / (1.0 + self.r * self.g + self.g * self.g)

    def run(self, x):
        g = self.g
        hp = (x - self.r * self.s1 - g * self.s1 - self.s2) * self.h
        bp = g * hp + self.s1
        self.s1 = g * hp + bp
        lp = g * bp + self.s2
        self.s2 = g * bp + lp
        return lp, bp, hp


class OnePole:
    def __init__(self, f_hz):
        self.c = 1.0 - math.exp(-2 * math.pi * f_hz / FS)
        self.y = 0.0

    def lp(self, x):
        self.y += self.c * (x - self.y)
        return self.y

    def hp(self, x):
        return x - self.lp(x)


def soft(x):
    return math.tanh(x)


def white(n):
    return rng.uniform(-1.0, 1.0, n)


def N(sec):
    return int(sec * FS)


# metal: six square waves (the 808's oscillators, 205..800 Hz)
METAL_808 = (205.3, 304.4, 369.6, 522.7, 540.0, 800.0)


def squares(freqs, n):
    t = np.arange(n) / FS
    out = np.zeros(n)
    for f in freqs:
        out += np.where((t * f) % 1.0 < 0.5, 1.0, -1.0)
    return out / len(freqs)


# ======================================================== A FELUCCA ===
def a_kick():
    n = N(0.6)
    y = np.zeros(n)
    f0 = 50.0
    ph = 0.0
    e_att, e_sw = 1.0, 1.0           # +2 octaves (tau 1.5 ms), then +1.5 octaves after a 12 ms hold
    k_att, k_sw = tau_k(0.0015), tau_k(0.030)
    amp, k_amp = 1.0, tau_k(0.090)
    hold = N(0.012)
    click_n = N(0.0007)              # two cycles of a 2.9 kHz sine under a raised cosine
    for i in range(n):
        f = f0 * (1 + 3.0 * e_att + 1.83 * e_sw)
        ph += f / FS
        e_att *= k_att
        if i >= hold:
            e_sw *= k_sw
            amp *= k_amp
        p = 2 * math.pi * ph
        b = math.sin(p) + 0.25 * math.sin(2 * p) + 0.10 * math.sin(3 * p)
        x = b * amp
        if i < click_n:
            w = 0.5 - 0.5 * math.cos(2 * math.pi * i / click_n)
            x += 0.6 * w * math.sin(2 * math.pi * 2900 * i / FS)
        y[i] = soft(1.8 * x) * 0.55
    return y


def a_snare():
    n = N(0.5)
    y = np.zeros(n)
    nz = white(n)
    bp = Svf(4500, 0.9)
    hpf = OnePole(7000)
    p0 = p1 = 0.0
    eg, kg = 1.0, tau_k(0.010)        # the shells' small glide down
    es, ks = 1.0, tau_k(0.045)        # the shells
    en, kn = 1.0, tau_k(0.070)        # the noise's body
    ek, kk = 1.0, tau_k(0.004)        # the spike
    for i in range(n):
        f = 180.0 * (1 + 0.25 * eg)
        eg *= kg
        p0 += f / FS
        p1 += 1.6 * f / FS
        sh = (math.sin(2 * math.pi * p0) + 0.6 * math.sin(2 * math.pi * p1)) * es
        es *= ks
        _, b, _ = bp.run(nz[i])
        h = hpf.hp(nz[i])
        x = 0.55 * sh + b * (0.9 * en + 1.2 * ek) + 0.35 * h * ek
        en *= kn
        ek *= kk
        y[i] = soft(1.3 * x) * 0.6
    return y


def clap_multi(n, f_band, q, teeth, gap, tooth_tau, tail_tau, bright, shape="exp"):
    """A clap: band noise struck `teeth` times, `gap` apart, then a tail."""
    y = np.zeros(n)
    nz = white(n)
    bp = Svf(f_band, q)
    hpf = OnePole(3000)
    g_gap = N(gap)
    tail_at = teeth * g_gap
    kt, ka = tau_k(tooth_tau), tau_k(tail_tau)
    et, ea = 0.0, 0.0
    for i in range(n):
        if i < tail_at and i % g_gap == 0:
            et = 1.0
        if i == tail_at:
            ea = 0.7
        if shape == "saw" and i < tail_at:
            et = max(0.0, 1.0 - (i % g_gap) / g_gap)
        else:
            et *= kt
        ea *= ka
        _, b, _ = bp.run(nz[i])
        x = b * (et + ea) * 2.2 + bright * hpf.hp(nz[i]) * (et + 0.3 * ea)
        y[i] = x
    return y


def a_clap():
    return clap_multi(N(0.6), 1100, 1.6, 3, 0.010, 0.0035, 0.060, 0.15) * 0.55


def hats(metal, n, bp_f, bp_q, hp_f, sizzle, tau, noise=0.0):
    y = np.zeros(n)
    bp = Svf(bp_f, bp_q)
    hp1 = OnePole(hp_f)
    hp2 = OnePole(13000)
    nz = white(n)
    a, k = 1.0, tau_k(tau)
    rise = N(0.0003)
    for i in range(n):
        m = metal[i] + noise * nz[i]
        _, b, _ = bp.run(m)
        x = hp1.hp(b) + sizzle * hp2.hp(m)
        r = min(1.0, (i + 1) / rise)
        y[i] = x * a * r
        a *= k
    return y


def a_hat(open_):
    n = N(0.7 if open_ else 0.25)
    m = squares(METAL_808, n)
    if open_:
        return hats(m, n, 7100, 0.8, 8800, 0.05, 0.098) * 1.6
    return hats(m, n, 7100, 0.8, 12000, 0.35, 0.016) * 1.8


def a_tom(f0):
    n = N(0.7)
    y = np.zeros(n)
    nz = white(n)
    lp = OnePole(900)
    ph = 0.0
    a, k = 1.0, tau_k(0.110)
    es, kst = 1.0, tau_k(0.006)
    for i in range(n):
        ph += f0 * (1 + 0.33 * a * a) / FS      # the pitch follows amplitude squared
        x = math.sin(2 * math.pi * ph) * a + 0.25 * lp.lp(nz[i]) * es * 3
        a *= k
        es *= kst
        y[i] = soft(1.2 * x) * 0.6
    return y


# ========================================================= B ANALOG ===
def diode(x):
    if x >= 0:
        return x
    x *= 2.0
    return 0.7 * x / (1.0 + abs(x))


def b_resonator_drum(f0_hz, tone, decay, attack_fm, self_fm, accent, dur, noise=0.0):
    """Plaits' AnalogBassDrum, after its description: a band-pass of high Q
    struck by a 1 ms pulse; an attack FM and a self FM bend its pitch."""
    n = N(dur)
    y = np.zeros(n)
    f0 = f0_hz / FS
    scale = 0.001 / f0
    q = 1500.0 * 2 ** (decay * 80 / 12)
    tone_f = min(4 * f0 * 2 ** (tone * 108 / 12), 1.0)
    leak = 0.08 * (tone + 0.25)
    pulse_left, fm_left = N(0.001), N(0.006)
    pulse_decay, pulse_filt, retrig_t = 0.0002 * FS, 0.0001 * FS, 0.05 * FS
    height = 3.0 + 7.0 * accent
    pulse_st = pulse_lp = fm_lp = retrig = lp_out = tone_lp = 0.0
    s1 = s2 = 0.0
    nz = white(n)
    nlp = OnePole(2500)
    for i in range(n):
        if pulse_left:
            pulse_left -= 1
            pulse = height if pulse_left else height - 1.0
            pulse_st = pulse
        else:
            pulse_st *= 1 - 1 / pulse_decay
            pulse = pulse_st
        pulse_lp += (pulse - pulse_lp) / pulse_filt
        pulse = diode((pulse - pulse_lp) + pulse * 0.044)
        if fm_left:
            fm_left -= 1
            fmp = 1.0
            retrig = 0.0 if fm_left else -0.8
        else:
            fmp = 0.0
            retrig *= 1 - 1 / retrig_t
        fm_lp += (fmp - fm_lp) / pulse_filt
        punch = 0.7 + diode(10 * lp_out - 1)
        f = f0 * (1 + fm_lp * 1.7 * attack_fm + punch * 0.08 * self_fm)
        f = min(max(f, 0.0), 0.4)
        g = math.tan(math.pi * f)
        r = 1.0 / (1.0 + q * f)
        h = 1.0 / (1 + r * g + g * g)
        x = (pulse - retrig * 0.2) * scale
        hp = (x - r * s1 - g * s1 - s2) * h
        bp = g * hp + s1
        s1 = g * hp + bp
        lp_out = g * bp + s2
        s2 = g * bp + lp_out
        tone_lp += tone_f * (pulse * leak + bp - tone_lp)
        y[i] = tone_lp + noise * nlp.lp(nz[i]) * math.exp(-i / (0.03 * FS))
    return y


def b_kick():
    return b_resonator_drum(52, 0.6, 0.55, 0.6, 0.4, 0.8, 0.8) * 1.0


def b_snare():
    """Plaits' AnalogSnareDrum: five modes (1, 2, 3.18, 4.16, 5.62 x f0) rung
    by a pulse, and half-wave noise through a band-pass at 16 x f0."""
    n = N(0.5)
    y = np.zeros(n)
    f0, tone, decay, snappy, accent = 190 / FS, 0.55, 0.45, 0.65, 0.8
    decay_xt = decay * (1 + decay * (decay - 1))
    q = 2000 * 2 ** (decay_xt * 84 / 12)
    noise_k = 1 - 0.0017 * 2 ** (-decay * (50 + snappy * 10) / 12)
    leak = snappy * (2 - snappy) * 0.1
    snappy = min(max(snappy * 1.1 - 0.05, 0), 1)
    ratios = (1.0, 2.0, 3.18, 4.16, 5.62)
    modes = []
    for j, rr in enumerate(ratios):
        f = min(f0 * rr, 0.499)
        s = Svf(f * FS, 1 + f * (q if j == 0 else q * 0.25))
        modes.append(s)
    t = tone * 1.5 if tone < 2 / 3 else None
    if t is not None:
        gain = [1.5 + (1 - t) ** 2 * 4.5, 2 * t + 0.15, 0, 0, 0]
    else:
        t = (tone - 2 / 3) * 3
        gain = [1.5 - t * 0.5, 2.15 - t * 0.7, t, t * t, t ** 4]
    nf = Svf(min(f0 * 16, 0.499) * FS, 1 + min(f0 * 16, 0.499) * 1.5)
    pulse_left = N(0.001)
    height = 3 + 7 * accent
    pulse_st = pulse_lp = 0.0
    env = 2.0
    nz = white(n)
    for i in range(n):
        if pulse_left:
            pulse_left -= 1
            pulse = height if pulse_left else height - 1
            pulse_st = pulse
        else:
            pulse_st *= 1 - 1 / (0.0001 * FS)
            pulse = pulse_st
        pulse_lp += 0.75 * (pulse - pulse_lp)
        shell = 0.0
        for j in range(5):
            ex = (pulse - pulse_lp) + 0.006 * pulse if j == 0 else 0.026 * pulse
            _, b, _ = modes[j].run(ex)
            shell += gain[j] * (b + ex * leak)
        shell = soft(shell)
        x = max(nz[i], 0.0)
        env *= noise_k
        _, nb, _ = nf.run(x * env * snappy * 2)
        y[i] = nb + shell * (1 - snappy)
    return y * 1.4


def b_clap():
    """The 808's clap: band noise near 1 kHz under three sawtooth teeth,
    then a separate, longer tail."""
    return clap_multi(N(0.6), 1000, 2.5, 3, 0.011, 0.004, 0.090, 0.05, shape="saw") * 0.55


def b_hat(open_):
    """Plaits' HiHat (its first model): the six squares at 2 x f0, a resonant
    band-pass, a swing VCA (asymmetric clip) under one decay, a high-pass."""
    n = N(0.7 if open_ else 0.25)
    f0 = 205.3
    ratios = (1.0, 1.304, 1.466, 1.787, 1.932, 2.536)
    m = squares([2 * f0 * r for r in ratios], n) * 2.0
    tone, decay, noisiness = 0.82, (0.62 if open_ else 0.22), 0.25
    env_k = 1 - 0.003 * 2 ** (-decay * 84 / 12)
    env = (1.5 + 0.5 * (1 - decay)) * 0.9
    cutoff = min(150 * 2 ** (tone * 72 / 12), 16000)
    bp = Svf(cutoff, 3 + 3 * tone)
    hp = Svf(cutoff, 0.5)
    noise_f = (f0 / FS) * (16 + 16 * (1 - noisiness ** 2))
    nclk, nsamp = 0.0, 0.0
    y = np.zeros(n)
    for i in range(n):
        _, b, _ = bp.run(m[i])
        nclk += noise_f
        if nclk >= 1:
            nclk -= 1
            nsamp = rng.uniform() - 0.5
        s = b + noisiness ** 2 * (nsamp - b)
        env *= env_k
        s *= 4.0 if s > 0 else 0.1
        s = s / (1 + abs(s))
        s = (s + 0.1) * env
        _, _, h = hp.run(s)
        y[i] = h
    return y * 0.9


def b_tom(f0):
    return b_resonator_drum(f0, 0.5, 0.5, 0.25, 0.3, 0.8, 0.8, noise=0.03) * 0.9


# ========================================================== C SYNTH ===
def c_kick():
    """Plaits' SyntheticBassDrum: a sine held at its peak for 1.3 ms, then
    an FM envelope over its pitch; a triangle-shaped sine through a
    'transistor' VCA, a click and a little noise for the attack."""
    n = N(0.7)
    y = np.zeros(n)
    f0 = 48 / FS
    tone, decay, dirt, fm_amt, fm_dec, accent = 0.45, 0.7, 0.2, 0.7, 0.3, 0.8
    decay *= decay
    fm_dec *= fm_dec
    dirt *= max(1 - 8 * f0, 0)
    fm_k = 1 - 1 / (0.008 * (1 + fm_dec * 4) * FS)
    body_k = 1 - 1 / (0.02 * FS) * 2 ** (-decay * 60 / 12)
    tr_k = 1 - 1 / (0.005 * FS)
    tone_f = min(4 * f0 * 2 ** (tone * 108 / 12), 1.0)
    fm = 1.0
    body = tr = 0.3 + 0.7 * accent
    body_w, fm_w = N(0.001), N(0.0013)
    phase = phase_noise = 0.0
    b_lp = t_lp = f_lp = tone_lp = 0.0
    c_lp = c_hp = 0.0
    csvf = Svf(5000, 2.0)
    a_lp = a_hp = 0.0
    for i in range(n):
        phase_noise += 0.002 * (rng.uniform() - 0.5 - phase_noise)
        if fm_w:
            fm_w -= 1
            phase = 0.25
        else:
            fm *= fm_k
            phase += min(f0 * (1 + fm_amt * 3.5 * f_lp), 0.5)
            phase %= 1.0
        if body_w:
            body_w -= 1
        else:
            body *= body_k
            tr *= tr_k
        b_lp += 0.1 * (body - b_lp)
        t_lp += 0.1 * (tr - t_lp)
        f_lp += 0.1 * (fm - f_lp)
        p = (phase + phase_noise * dirt) % 1.0
        tri = (p if p < 0.5 else 1 - p) * 4 - 1
        ds = 2 * tri / (1 + abs(tri))
        clean = math.sin(2 * math.pi * (p + 0.75))
        s = ds + (1 - dirt) * (clean - ds)
        v = (s - 0.6) * b_lp
        vca = 3 * v / (2 + abs(v)) + b_lp * 0.3
        cin = 0.0 if body_w else 1.0
        c_lp += (cin - c_lp) * (0.5 if cin > c_lp else 0.1)
        c_hp += 0.04 * (c_lp - c_hp)
        click, _, _ = csvf.run(c_lp - c_hp)
        a_lp += 0.05 * (rng.uniform() - a_lp)
        a_hp += 0.005 * (a_lp - a_hp)
        trans = click + (a_lp - a_hp)
        mix = -vca - trans * t_lp * tone
        tone_lp += tone_f * (mix - tone_lp)
        y[i] = tone_lp
    y -= np.mean(y[-200:])               # the VCA's offset (Plaits removes it downstream)
    hp = OnePole(20)
    return np.array([hp.hp(v) for v in y]) * 0.75


def c_snare():
    """Plaits' SyntheticSnareDrum: two shaped sines (f, 1.47 f) under an FM
    envelope, and white noise between a high-pass at 10 f0 and a low-pass
    at 35 f0, held 40 ms before it decays."""
    n = N(0.5)
    y = np.zeros(n)
    f0, fm_amt, decay, snappy, accent = 200 / FS, 0.3, 0.45, 0.55, 0.8
    decay_xt = decay * (1 + decay * (decay - 1))
    fm_amt *= fm_amt
    drum_k = 1 - 1 / (0.015 * FS) * 2 ** ((-decay_xt * 72 - fm_amt * 12 + snappy * 7) / 12)
    snare_k = 1 - 1 / (0.01 * FS) * 2 ** ((-decay * 60 - snappy * 7) / 12)
    fm_k = 1 - 1 / (0.007 * FS)
    snappy = min(max(snappy * 1.1 - 0.05, 0), 1)
    d_lvl, s_lvl = math.sqrt(1 - snappy), math.sqrt(snappy)
    s_hp = Svf(10 * f0 * FS, 0.7)
    s_lp = Svf(min(35 * f0, 0.5) * FS, 0.5 + 2 * snappy)
    d_lp = Svf(3 * f0 * FS, 0.7)
    s_amp = d_amp = 0.3 + 0.7 * accent
    fm = 1.0
    hold = N(0.04 + decay * 0.03)
    p0 = p1 = 0.0

    def dsin(p):
        tri = (p if p < 0.5 else 1 - p) * 4 - 1.3
        return 2 * tri / (1 + abs(tri))

    for i in range(n):
        d_amp *= drum_k
        if hold:
            hold -= 1
        else:
            s_amp *= snare_k
        fm *= fm_k
        f = f0 * (1 + fm_amt * 4 * fm)
        p0 = (p0 + f) % 1.0
        p1 = (p1 + f * 1.47) % 1.0
        d = -0.1 + dsin(p0) * 0.6 + dsin(p1) * 0.25
        d, _, _ = d_lp.run(d * d_amp * d_lvl)
        nzv = rng.uniform()
        s, _, _ = s_lp.run(nzv)
        _, _, s = s_hp.run(s)
        s = (s + 0.1) * (s_amp + fm) * s_lvl
        y[i] = s + d
    hp = OnePole(40)
    return np.array([hp.hp(v) for v in y]) * 1.1


def c_hat(open_):
    """Plaits' second hi-hat: three ring-modulated square/saw pairs
    (200 x 7530, 510 x 8075, 730 x 10500 Hz), a band-pass, a linear VCA
    under a two-stage decay, a high-pass."""
    n = N(0.7 if open_ else 0.25)
    t = np.arange(n) / FS
    m = np.zeros(n)
    for fa, fb in ((200, 7530), (510, 8075), (730, 10500)):
        sq = np.where((t * fa) % 1 < 0.5, 1.0, -1.0)
        saw = 2 * ((t * fb) % 1) - 1
        m += sq * saw
    tone, decay = 0.8, (0.65 if open_ else 0.25)
    env_k = 1 - 0.003 * 2 ** (-decay * 84 / 12)
    cut_k = 1 - 0.0025 * 2 ** (-decay * 36 / 12)
    env = (1.5 + 0.5 * (1 - decay)) * 0.9
    cutoff = min(150 * 2 ** (tone * 72 / 12), 16000)
    bp = Svf(cutoff, 1.0)
    hp = Svf(cutoff, 0.5)
    y = np.zeros(n)
    for i in range(n):
        _, b, _ = bp.run(m[i])
        env *= env_k if env > 0.5 else cut_k
        _, _, h = hp.run(b * env)
        y[i] = h
    return y * 0.7


def c_clap():
    """A digital clap: four teeth of white noise through a 1.2 kHz band, a
    bright high-passed layer, a 120 ms tail."""
    return clap_multi(N(0.6), 1200, 1.2, 4, 0.008, 0.003, 0.110, 0.25) * 0.5


def c_tom(f0):
    """A 909 tom: a sine with a fast pitch drop and a short noise attack."""
    n = N(0.6)
    y = np.zeros(n)
    nz = white(n)
    nf = Svf(3000, 0.8)
    ph = 0.0
    a, k = 1.0, tau_k(0.12)
    ep, kp = 1.0, tau_k(0.035)
    en, kn = 1.0, tau_k(0.008)
    for i in range(n):
        ph += f0 * (1 + 0.6 * ep) / FS
        ep *= kp
        _, b, _ = nf.run(nz[i])
        y[i] = soft(1.3 * (math.sin(2 * math.pi * ph) * a + 0.4 * b * en)) * 0.6
        a *= k
        en *= kn
    return y


# ========================================================== D CHIP+ ===
TRI32 = [((i if i < 16 else 32 - i) / 8.0 - 1.0) for i in range(32)]


class Lfsr:
    """MOONDAW's noise register, clocked at `clock_hz`, held between."""

    def __init__(self, clock_hz, short=False):
        self.reg = 1
        self.tap = 6 if short else 1
        self.step = clock_hz / FS
        self.acc = 0.0
        self.out = 0.0

    def next(self):
        self.acc += self.step
        while self.acc >= 1.0:
            self.acc -= 1.0
            fb = (self.reg ^ (self.reg >> self.tap)) & 1
            self.reg = (self.reg >> 1) | (fb << 14)
            self.out = 1.0 if self.reg & 1 else -1.0
        return self.out


def d_wave(f_of_i, amp_k, n, start=1.0):
    """The WAVE engine's 32-step triangle, its pitch and level moved every
    sample (today: every tick, 20 ms)."""
    y = np.zeros(n)
    ph = 0.0
    a = start
    for i in range(n):
        ph = (ph + f_of_i(i) / FS) % 1.0
        y[i] = TRI32[int(ph * 32)] * a
        a *= amp_k
    return y


def d_kick():
    n = N(0.5)
    k_env = tau_k(0.022)
    body = d_wave(lambda i: 48 * (1 + 7 * k_env ** i), tau_k(0.10), n)
    click = np.zeros(n)
    click[:N(0.0015)] = 0.5            # a PULSE edge, 1.5 ms
    lp = OnePole(150)                  # the WAVE's steps softened (the track's filter)
    y = np.array([lp.lp(v) for v in body]) * 1.2 + click
    return np.tanh(1.6 * y) * 0.6


def d_noise(n, clock, tau, hp_hz, short=False, rise=0.0):
    lf = Lfsr(clock, short)
    hp = OnePole(hp_hz)
    y = np.zeros(n)
    a, k = 1.0, tau_k(tau)
    for i in range(n):
        y[i] = hp.hp(lf.next()) * a
        a *= k
    return y


def d_snare():
    n = N(0.4)
    body = d_wave(lambda i: 185 * (1 + 0.4 * tau_k(0.01) ** i), tau_k(0.035), n)
    nz = d_noise(n, 22000, 0.060, 1500)
    return np.tanh(0.6 * body + 0.7 * nz) * 0.6


def d_hat(open_):
    n = N(0.6 if open_ else 0.2)
    return d_noise(n, 22050, 0.090 if open_ else 0.015, 7000, short=False) * 0.55


def d_clap():
    n = N(0.5)
    lf = Lfsr(20000)
    hp = OnePole(800)
    lp = OnePole(2500)
    y = np.zeros(n)
    gap = N(0.010)
    e, k, ea, ka = 0.0, tau_k(0.003), 0.0, tau_k(0.07)
    for i in range(n):
        if i < 3 * gap and i % gap == 0:
            e = 1.0
        if i == 3 * gap:
            ea = 0.7
        y[i] = lp.lp(hp.hp(lf.next())) * (e + ea)
        e *= k
        ea *= ka
    return y * 0.8


def d_tom(f0):
    n = N(0.5)
    body = d_wave(lambda i: f0 * (1 + 0.5 * tau_k(0.04) ** i), tau_k(0.10), n)
    lp = OnePole(f0 * 6)
    return np.array([lp.lp(v) for v in body]) * 0.8


# ============================================================ the kit ===
CANDIDATES = {
    "A-felucca": dict(kick=a_kick, snare=a_snare, clap=a_clap, hat=a_hat, tom=a_tom),
    "B-analog": dict(kick=b_kick, snare=b_snare, clap=b_clap, hat=b_hat, tom=b_tom),
    "C-synth": dict(kick=c_kick, snare=c_snare, clap=c_clap, hat=c_hat, tom=c_tom),
    "D-chipplus": dict(kick=d_kick, snare=d_snare, clap=d_clap, hat=d_hat, tom=d_tom),
}

# the grid of tools/drums/current-kit.trk: 16 rows a bar, 0.125 s a row
BARS = 5
HITS_ALONE = [  # (row from the start, voice)
    (0, "kick"), (8, "snare"),
    (16, "hatc"), (24, "hato"),
    (32, "clap"), (40, "tomh"),
]
BEAT = []
for bar in (3, 4):
    base = bar * 16
    for r in (0, 4, 8, 12):
        BEAT.append((base + r, "kick"))
        BEAT.append((base + r, "hatc"))
        BEAT.append((base + r + 2, "hato"))
    BEAT += [(base + 4, "snare"), (base + 12, "snare")]
BEAT += [(4 * 16 + 12, "clap"), (4 * 16 + 13, "tomh"), (4 * 16 + 14, "toml")]
LEVEL = dict(kick=1.0, snare=0.8, clap=0.7, hatc=0.45, hato=0.4, tomh=0.7, toml=0.7)


def kit_voices(c):
    v = dict(kick=c["kick"](), snare=c["snare"](), clap=c["clap"](),
             hatc=c["hat"](False), hato=c["hat"](True),
             tomh=c["tom"](150.0), toml=c["tom"](110.0))
    for k in v:   # each piece to its level by peak: the comparison is of colours, not of a mix
        v[k] = v[k] / (np.max(np.abs(v[k])) + 1e-9) * LEVEL[k]
    return v


def sequence(voices):
    n = N(BARS * 16 * ROW + 0.5)
    out = np.zeros(n)
    hato_end = None
    for row, name in sorted(HITS_ALONE + BEAT):
        at = N(row * ROW)
        if name == "hatc" and hato_end is not None and hato_end > at:
            # the closed hat chokes the open one: a 1.5 ms fade, then silence
            fade = N(0.0015)
            ramp = np.linspace(1, 0, fade)
            seg = out_hato[at:at + fade]
            out[at:at + fade] -= seg * (1 - ramp)
            out[at + fade:hato_end] -= out_hato[at + fade:hato_end]
            hato_end = None
        s = voices[name]
        m = min(len(s), n - at)
        out[at:at + m] += s[:m]
        if name == "hato":
            out_hato = np.zeros(n)
            out_hato[at:at + m] = s[:m]
            hato_end = at + m
    return out


def loudness_match(y, ref_rms):
    beat = y[N(3 * 16 * ROW):N(5 * 16 * ROW)]
    y = y * (ref_rms / (np.sqrt(np.mean(beat ** 2)) + 1e-9))
    return np.tanh(y * 1.1) / 1.1 if np.max(np.abs(y)) > 0.98 else y


def write_wav(path, y):
    pcm = np.clip(np.round(y * 32767), -32768, 32767).astype("<i2")
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + 2 * len(pcm)) + b"WAVEfmt ")
        f.write(struct.pack("<IHHIIHH", 16, 1, 1, FS, FS * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", 2 * len(pcm)) + pcm.tobytes())


def read_wav_mono(path):
    with open(path, "rb") as f:
        d = f.read()
    ch = struct.unpack("<H", d[22:24])[0]
    i = d.index(b"data") + 8
    x = np.frombuffer(d[i:], "<i2").astype(float) / 32768
    return x.reshape(-1, ch).mean(axis=1)


def current_kit():
    """The current drums, as the FM-1 core renders them (tests/fm_render.c)."""
    build = os.path.join(ROOT, "build")
    os.makedirs(build, exist_ok=True)
    exe = os.path.join(build, "fm_render")
    src = [os.path.join(ROOT, p) for p in (
        "tests/fm_render.c", "core/song.c", "core/trk.c", "core/synth.c", "core/fx.c", "core/player.c")]
    subprocess.check_call(["cc", "-std=c11", "-O2", *src, "-o", exe])
    raw = os.path.join(build, "current-kit.wav")
    subprocess.check_call([exe, os.path.join(HERE, "current-kit.trk"), raw, str(BARS * 16 * ROW + 0.5)])
    return read_wav_mono(raw)


def main():
    os.makedirs(OUT, exist_ok=True)
    TARGET = 10 ** (-17 / 20)            # the beats at the same RMS, -17 dBFS
    cur = loudness_match(current_kit(), TARGET)
    write_wav(os.path.join(OUT, "0-actuel.wav"), cur)
    beats = [("0-actuel", cur)]
    for name, c in CANDIDATES.items():
        print(name, file=sys.stderr)
        y = loudness_match(sequence(kit_voices(c)), TARGET)
        write_wav(os.path.join(OUT, name + ".wav"), y)
        beats.append((name, y))
    # every beat in a row, the current one first: the A/B by ear
    gap = np.zeros(N(0.6))
    parts = []
    for _, y in beats:
        parts += [y[N(4 * 16 * ROW):N(5 * 16 * ROW + 0.3)], gap]  # the last bar, the fill in it
    both = np.concatenate(parts)
    write_wav(os.path.join(OUT, "comparatif-rythmes.wav"), both)
    # the same through a small speaker, roughly: 12 dB/octave below 200 Hz,
    # one gain for all, so a kick that lives under 200 Hz is heard fading
    small = Svf(200, 0.7)
    hp = np.array([small.run(v)[2] for v in both])
    hp *= 0.9 / np.max(np.abs(hp))
    write_wav(os.path.join(OUT, "comparatif-petit-haut-parleur.wav"), np.tanh(hp * 1.1) / 1.1)


if __name__ == "__main__":
    main()
