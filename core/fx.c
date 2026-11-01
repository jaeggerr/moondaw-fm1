/* SPDX-License-Identifier: GPL-3.0-only */
#include "fx.h"

#include "song.h"
#include "tables.h"

/* The lines hold the signal at HALF scale, in 16 bits: a bus is a sum
 * of tracks and goes past full scale, and a clipped feedback loop is
 * heard for seconds. Multiplies round toward zero (C division), never
 * down: a line that rounds down settles on -1 and never dies. */
static int16_t sat16(int32_t v)
{
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

static int32_t mul15(int32_t a, int32_t b) { return a * b / 32768; }

/* =========================================================== reverb ===
 * A feedback delay network: two allpass diffusers into eight lines, each
 * damped by a one-pole, mixed through a Hadamard matrix. */
#define LINES 8
#define APS 2
#define AP_G 16384      /* 0.5 */
#define WET_GAIN 14746  /* 0.45 */

/* moondaw's lengths at 48 kHz, scaled to 44.1 as its reverb_init does. */
static const uint16_t line_len[LINES] = { 1026, 1156, 1268, 1417, 1581, 1715, 1886, 2123 };
static const uint16_t line_at[LINES] = { 0, 1026, 2182, 3450, 4867, 6448, 8163, 10049 };
static const uint16_t ap_len[APS] = { 221, 403 };
static const uint16_t ap_at[APS] = { 12172, 12393 };
#define REVERB_MEM 12796

static struct {
    int16_t mem[REVERB_MEM];
    uint16_t pos[LINES], appos[APS];
    int32_t lp[LINES];
    uint8_t size, damp;
    bool active;
} rv
#ifdef PLAT_FM1
    __attribute__((section(".pool")))
#endif
    ;

void reverb_set(uint8_t size, uint8_t damp)
{
    rv.size = size > 127 ? 127 : size;
    rv.damp = damp > 127 ? 127 : damp;
}

bool reverb_ringing(void) { return rv.active; }

static void reverb_flush(void)
{
    for (int i = 0; i < REVERB_MEM; i++)
        rv.mem[i] = 0;
    for (int k = 0; k < LINES; k++)
        rv.lp[k] = 0;
    rv.active = false;
}

void reverb_process(const int32_t *in, int32_t *out, int frames)
{
    const uint16_t *fb = REVERB_FB_Q16[rv.size];
    int32_t damp = REVERB_DAMP_Q15[rv.damp], peak = 0;

    for (int i = 0; i < frames; i++) {
        int32_t x = in ? in[i] / 2 : 0, wet[LINES], v[LINES], l, r, m;

        x = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
        for (int a = 0; a < APS; a++) {
            int16_t *p = &rv.mem[ap_at[a] + rv.appos[a]];
            int32_t d = *p, w = x + d / 2;
            *p = sat16(w);
            x = d - w / 2;
            if (++rv.appos[a] >= ap_len[a])
                rv.appos[a] = 0;
        }
        m = x < 0 ? -x : x;
        if (m > peak)
            peak = m;
        for (int k = 0; k < LINES; k++) {
            int32_t s = rv.mem[line_at[k] + rv.pos[k]];
            rv.lp[k] = s + mul15(damp, rv.lp[k] - s);
            wet[k] = s; /* heard undamped: the first arrival is bright */
            /* lp is a one-pole of int16 samples, so |lp| <= 32768, and 32768 x 65535
             * fits an int32: the same quotient as in 64 bits, a third the work */
            v[k] = rv.lp[k] * (int32_t)fb[k] / 65536;
        }
        for (int span = 1; span < LINES; span <<= 1)
            for (int k = 0; k < LINES; k++)
                if ((k & span) == 0) {
                    int32_t a = v[k], b = v[k + span];
                    v[k] = a + b;
                    v[k + span] = a - b;
                }
        for (int k = 0; k < LINES; k++) {
            bool neg = k == 2 || k == 4 || k == 5 || k == 7;
            rv.mem[line_at[k] + rv.pos[k]] = sat16(v[k] + (neg ? -x : x));
            if (++rv.pos[k] >= line_len[k])
                rv.pos[k] = 0;
        }
        l = wet[0] + wet[2] - wet[4] - wet[6];
        r = wet[1] - wet[3] + wet[5] - wet[7];
        out[2 * i] += mul15(l * 2, WET_GAIN);
        out[2 * i + 1] += mul15(r * 2, WET_GAIN);
        m = (l < 0 ? -l : l) + (r < 0 ? -r : r);
        if (m > peak)
            peak = m;
    }
    /* Silence in, silence in the lines: the room is empty, and the next
     * note must not find a fossil of the last. */
    if (peak == 0)
        reverb_flush();
    else
        rv.active = true;
}

/* ============================================================ delay ===
 * Two lines that feed each other -- the repeats ping-pong -- each
 * darkened by a one-pole at 4 kHz on the way round. */
#define ECHO_CAP (SCRATCH_BYTES / 4) /* frames per line: 32768, 743 ms */
#define ECHO_DAMP 14234              /* 1 - exp(-2 pi 4000 / 44100), Q15 */

static struct {
    int16_t *line[2];
    int32_t pos, len, target;
    int32_t lp[2], fb;
    bool active;
} ec;

void echo_set(uint8_t ticks, uint8_t feedback, int tick_samples)
{
    int32_t want = (ticks > 96 ? 96 : ticks) * (tick_samples > 0 ? tick_samples : 1);
    ec.fb = (feedback > 127 ? 127 : feedback) * 27853 / 127; /* up to 0.85 */
    if (want < 1)
        want = 1;
    if (want > ECHO_CAP - 1)
        want = ECHO_CAP - 1;
    ec.target = want;
    if (ec.len <= 1)
        ec.len = want;
}

bool echo_ringing(void) { return ec.active; }

static void echo_clear(void)
{
    for (int i = 0; i < ECHO_CAP; i++)
        ec.line[0][i] = ec.line[1][i] = 0;
    ec.lp[0] = ec.lp[1] = 0;
    ec.active = false;
}

void echo_process(const int32_t *in, int32_t *out, int frames)
{
    int32_t peak = 0;

    for (int i = 0; i < frames; i++) {
        int32_t l, r, src, at;

        /* A tempo change walks the read head there: a thousandth of the
         * way each sample, a tape slowing down rather than a jump. */
        if (ec.len != ec.target) {
            int32_t d = ec.target - ec.len;
            ec.len += d > 0 ? (d + 999) / 1000 : -((-d + 999) / 1000);
        }
        at = ec.pos - ec.len;
        if (at < 0)
            at += ECHO_CAP;
        l = ec.line[0][at];
        r = ec.line[1][at];
        src = in ? in[i] / 2 : 0;
        ec.lp[0] += mul15(ECHO_DAMP, r - ec.lp[0]);
        ec.lp[1] += mul15(ECHO_DAMP, l - ec.lp[1]);
        ec.line[0][ec.pos] = sat16(src + mul15(ec.lp[0], ec.fb));
        ec.line[1][ec.pos] = sat16(mul15(ec.lp[1], ec.fb));
        if (++ec.pos >= ECHO_CAP)
            ec.pos = 0;
        out[2 * i] += l * 2;
        out[2 * i + 1] += r * 2;
        if (l < 0)
            l = -l;
        if (r < 0)
            r = -r;
        if (l > peak)
            peak = l;
        if (r > peak)
            peak = r;
    }
    if (in)
        ec.active = true;
    else if (peak == 0)
        echo_clear();
}

/* Only player_init calls it: from the flash, as player.c's main-loop side,
 * RAMTEXT being full (firmware/app.ld). */
#ifdef PLAT_FM1
__attribute__((section(".flash_text")))
#endif
void fx_init(void)
{
    ec.line[0] = song_scratch();
    ec.line[1] = ec.line[0] + ECHO_CAP;
    ec.pos = 0;
    ec.len = ec.target = 1;
    ec.fb = 0;
    echo_clear();
    for (int k = 0; k < LINES; k++)
        rv.pos[k] = 0;
    for (int a = 0; a < APS; a++)
        rv.appos[a] = 0;
    reverb_set(64, 64);
    reverb_flush();
}
