/* SPDX-License-Identifier: GPL-3.0-only */
/* DRUM: Felucca's drum voices as MOONDAW's engine (core/drum.c).
 *   1. every drum of every kit sounds, under full scale, without DC, and
 *      rings out by itself (drum_rings goes false);
 *   2. a hit struck again renders the same samples, whatever the length
 *      of the renders around it;
 *   3. the closed hat chokes the open one, on its track only;
 *   4. TUNE moves the kick's pitch, DECAY its length;
 *   5. a song: `engine DRUM`, its timbre and `drum` knobs read and
 *      written back the same, and played through the player;
 *   6. the cost: host time a sample with eight lanes ringing.
 *
 *   drum_test        exit 1 if any fails */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../core/drum.h"
#include "../core/player.h"
#include "../core/trk.h"

static int fails;
#define CHECK(what, ok)                                                     \
    do {                                                                    \
        if (!(ok)) {                                                        \
            printf("drum_test: FAIL %s\n", what);                          \
            fails++;                                                        \
        }                                                                   \
    } while (0)

#define FS 44100
#define LONG (FS * 16) /* a cymbal rings ~10 s to -84 dB */
static int32_t buf[LONG], buf2[LONG];
static const int8_t KNOB0[DRUM_KNOBS] = { 0, 0, 0, 0 };

/* one hit of `note`, rendered in blocks of `blk` samples; returns where it went quiet */
static int hit(uint8_t kit, uint8_t note, const int8_t *knob, int32_t *y, int n, int blk)
{
    int quiet = -1;
    drum_init();
    drum_hit(0, kit, note, 100, knob);
    memset(y, 0, (size_t)n * sizeof *y);
    for (int i = 0; i < n; i += blk) {
        int c = n - i < blk ? n - i : blk;
        drum_render(0, y + i, c);
        if (quiet < 0 && !drum_rings(0))
            quiet = i + c;
    }
    return quiet;
}

/* the dominant period of y[a..b), by the zero crossings going up */
static double freq(const int32_t *y, int a, int b)
{
    int first = -1, last = -1, n = 0;
    for (int i = a + 1; i < b; i++)
        if (y[i - 1] < 0 && y[i] >= 0) {
            if (first < 0)
                first = i;
            last = i;
            n++;
        }
    return n > 1 ? (double)(n - 1) * FS / (last - first) : 0;
}

static double energy(const int32_t *y, int n)
{
    double e = 0;
    for (int i = 0; i < n; i++)
        e += (double)y[i] * y[i];
    return e;
}

static Song song, back;
static char text[1 << 16];
static uint32_t at;
static int sink(void *ctx, const char *d, uint32_t n)
{
    char *t = ctx;
    if (at + n >= sizeof text)
        return -1;
    memcpy(t + at, d, n);
    at += n;
    return 0;
}

int main(void)
{
    static const uint8_t notes[] = { 36, 35, 37, 38, 39, 41, 42, 45, 46, 48, 49, 50, 51, 56, 60, 75, 81 };
    char what[96];

    /* 1. every drum of every kit */
    for (uint8_t kit = 0; kit < DRUM_KITS; kit++)
        for (unsigned k = 0; k < sizeof notes; k++) {
            int32_t peak = 0;
            double sum = 0;
            int quiet = hit(kit, notes[k], KNOB0, buf, LONG, 128);
            for (int i = 0; i < LONG; i++) {
                int32_t a = buf[i] < 0 ? -buf[i] : buf[i];
                peak = a > peak ? a : peak;
                sum += buf[i];
            }
            snprintf(what, sizeof what, "%s note %d sounds", drum_kit_name(kit), notes[k]);
            CHECK(what, peak > 1000);
            snprintf(what, sizeof what, "%s note %d under full scale (%d)", drum_kit_name(kit), notes[k], (int)peak);
            CHECK(what, peak < 32767 / 2);
            snprintf(what, sizeof what, "%s note %d without DC", drum_kit_name(kit), notes[k]);
            CHECK(what, sum / LONG > -0.01 * peak && sum / LONG < 0.01 * peak);
            snprintf(what, sizeof what, "%s note %d rings out", drum_kit_name(kit), notes[k]);
            CHECK(what, quiet > 0);
        }

    /* 2. the same hit, rendered by 128 and by uneven pieces */
    hit(0, 38, KNOB0, buf, FS, 128);
    hit(0, 38, KNOB0, buf2, FS, 37);
    CHECK("a hit renders the same in any blocks", !memcmp(buf, buf2, FS * sizeof *buf));
    hit(2, 36, KNOB0, buf, FS, 128);
    hit(2, 36, KNOB0, buf2, FS, 128);
    CHECK("a hit struck again renders the same", !memcmp(buf, buf2, FS * sizeof *buf));

    /* 3. the choke: an open hat on two tracks, a closed one on the first */
    {
        double open_alone, open_choked, other;
        int n = FS / 2, at0 = FS / 10 + FS / 50; /* 20 ms after the closed hat */

        drum_hit(0, 0, 46, 100, KNOB0);
        memset(buf, 0, sizeof buf);
        drum_render(0, buf, n);
        open_alone = energy(buf + at0 + FS / 10, n - at0 - FS / 10);

        drum_init();
        drum_hit(0, 0, 46, 100, KNOB0);
        drum_hit(1, 0, 46, 100, KNOB0);
        memset(buf, 0, sizeof buf);
        memset(buf2, 0, sizeof buf2);
        drum_render(0, buf, FS / 10);
        drum_render(1, buf2, FS / 10);
        drum_hit(0, 0, 42, 100, KNOB0);
        drum_render(0, buf + FS / 10, n - FS / 10);
        drum_render(1, buf2 + FS / 10, n - FS / 10);
        open_choked = energy(buf + at0 + FS / 10, n - at0 - FS / 10);
        other = energy(buf2 + at0 + FS / 10, n - at0 - FS / 10);
        CHECK("the closed hat chokes the open one", open_choked < open_alone * 1e-4);
        CHECK("on its own track only", other > open_alone * 0.5);
    }

    /* 4. the knobs */
    {
        int8_t up[DRUM_KNOBS] = { 32, 0, 0, 0 }, longer[DRUM_KNOBS] = { 0, 0, 40, 0 };
        int q0 = hit(0, 36, KNOB0, buf, LONG, 128);
        double f0 = freq(buf, FS / 5, FS * 2 / 5);
        int q1;
        double f1;
        hit(0, 36, up, buf, LONG, 128);
        f1 = freq(buf, FS / 5, FS * 2 / 5);
        snprintf(what, sizeof what, "TUNE +32 raises the kick (%.1f -> %.1f Hz)", f0, f1);
        CHECK(what, f1 > f0 * 1.3);
        q1 = hit(0, 36, longer, buf, LONG, 128);
        snprintf(what, sizeof what, "DECAY +40 makes the kick longer (%d -> %d)", q0, q1);
        CHECK(what, q1 > q0 * 3 / 2);
    }

    /* 5. a song */
    {
        static const char src[] =
            "version 3\nbpm 120\nticks 6\nmeter 4 4\ntracks 2\n"
            "inst 00 engine DRUM timbre 7 adsr 0 0 255 20 drum 70 50 64 90 name KIT\n"
            "pat t0 00 len 16\n00 C-2 i00\n04 D-2\n08 C-2+F#2\n0C D-2\n"
            "arr 00 steps auto\nt0 00\n";
        int32_t peak = 0;
        CHECK("the song reads", trk_load(&song, src, sizeof src - 1, NULL) == TRK_OK);
        CHECK("engine DRUM", song.instruments[0].engine == ENGINE_DRUM);
        CHECK("its kit", song.instruments[0].timbre == 7);
        CHECK("its knobs", song.instruments[0].drum[0] == 6 && song.instruments[0].drum[1] == -14 &&
                               song.instruments[0].drum[2] == 0 && song.instruments[0].drum[3] == 26);
        at = 0;
        CHECK("the song writes", trk_write(&song, sink, text) > 0);
        text[at] = 0;
        CHECK("as it was written", strstr(text, "engine DRUM timbre 7") && strstr(text, " drum 70 50 64 90"));
        CHECK("and reads back", trk_load(&back, text, at, NULL) == TRK_OK &&
                                    !memcmp(&back.instruments[0], &song.instruments[0], sizeof back.instruments[0]));
        player_init(&song);
        player_play_song(0, false);
        for (int i = 0; i < FS * 2; i += 128) {
            static int16_t pcm[256];
            player_render(pcm, 128);
            for (int k = 0; k < 256; k++)
                peak = abs(pcm[k]) > peak ? abs(pcm[k]) : peak;
        }
        snprintf(what, sizeof what, "the song plays its drums (peak %d)", (int)peak);
        CHECK(what, peak > 3000 && peak < 32767);
    }

    /* 6. the cost */
    {
        static const uint8_t kit8[] = { 36, 38, 39, 42, 46, 45, 37, 56 };
        clock_t t0;
        double secs;
        long frames = 0;
        drum_init();
        t0 = clock();
        while (clock() - t0 < CLOCKS_PER_SEC / 2) {
            for (unsigned k = 0; k < sizeof kit8; k++)
                drum_hit((int)k, 0, kit8[k], 100, KNOB0);
            for (int i = 0; i < FS / 4; i += 128) {
                memset(buf, 0, 128 * sizeof *buf);
                for (int t = 0; t < 8; t++)
                    drum_render(t, buf, 128);
                frames += 128;
            }
        }
        secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("drum_test: eight lanes struck every 0.25 s: %.0f ns a sample on this host (%.2f %% of real time)\n",
               secs * 1e9 / frames, 100.0 * secs * FS / frames);
    }

    printf("drum_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
