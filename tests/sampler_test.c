/* SPDX-License-Identifier: GPL-3.0-only */
/* SMPL: the sample bank and its voice (core/bank.h, core/sampler.h).
 *   1. ADPCM: a signal coded and decoded comes back close (SNR), and a
 *      voice at its root plays the decoded frames exactly;
 *   2. the bank: written into a store whose free sectors are scattered,
 *      found by every spelling of a song's name, read in place across
 *      its sectors; a damaged one is no bank; slot 32 beside .LAST;
 *   2b. a sample taken out of the bank: the hole closes in the flash the
 *      bank already holds, what came down plays the frames it had, and
 *      the last one takes the file with it;
 *   3. `smp`, `zone` and `fSMP` read and written back, twice the same;
 *   4. a SMPL voice: an octave up rings half as long, a zone plays, an
 *      fSMP plays its entry, a name the bank lacks is silent, the bank
 *      held stops every voice;
 *   5. with a bank from tools/fm1_samples.py: its decoder and this one
 *      agree to the frame, and KIDS plays its 909 samples.
 *
 *   sampler_test [BANK.bin WAVDIR SONG.trk]    exit 1 if any fails */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/bank.h"
#include "../core/player.h"
#include "../core/sampler.h"
#include "../core/trk.h"
#include "ramflash.h"

static int fails;
#define CHECK(what, ok)                                                     \
    do {                                                                    \
        if (!(ok)) {                                                        \
            printf("sampler_test: FAIL %s\n", what);                       \
            fails++;                                                        \
        }                                                                   \
    } while (0)

#define FS 44100

/* --------------------------------------------- the coder, as the tool's --- */
static const int16_t STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408,
    449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
};

static uint8_t encode_one(int16_t pred, uint8_t idx, int x)
{
    int diff = x - pred, step = STEP[idx];
    uint8_t code = 0;
    if (diff < 0)
        code = 8, diff = -diff;
    if (diff >= step)
        code |= 4, diff -= step;
    if (diff >= step >> 1)
        code |= 2, diff -= step >> 1;
    if (diff >= step >> 2)
        code |= 1;
    return code;
}

/* frames -> whole blocks; returns the bytes */
static uint32_t encode(const int16_t *x, uint32_t n, uint8_t *out)
{
    uint8_t idx = 0;
    uint32_t o = 0;
    for (uint32_t at = 0; at < n; at += SMP_BLOCK_FRAMES) {
        int16_t pred = x[at];
        uint8_t *b = out + o;
        memset(b, 0, SMP_BLOCK);
        b[0] = (uint8_t)pred, b[1] = (uint8_t)((uint16_t)pred >> 8), b[2] = idx;
        for (uint32_t k = 1; k < SMP_BLOCK_FRAMES && at + k < n; k++) {
            uint8_t c = encode_one(pred, idx, x[at + k]);
            smp_decode(&pred, &idx, c);
            b[3 + (k - 1) / 2] |= (uint8_t)((k - 1) & 1 ? c << 4 : c);
        }
        o += SMP_BLOCK;
    }
    return o;
}

static uint32_t decode(const uint8_t *b, uint32_t frames, int16_t *out)
{
    uint32_t n = 0;
    for (; n < frames; b += SMP_BLOCK) {
        int16_t pred = (int16_t)(b[0] | b[1] << 8);
        uint8_t idx = b[2];
        out[n++] = pred;
        for (uint32_t k = 1; k < SMP_BLOCK_FRAMES && n < frames; k++)
            out[n++] = smp_decode(&pred, &idx, (uint8_t)(k & 1 ? b[3 + (k - 1) / 2] & 15 : b[3 + (k - 1) / 2] >> 4));
    }
    return n;
}

/* ----------------------------------------------------- a bank, built --- */
static uint8_t bankfile[200000];

typedef struct {
    const char *name;
    const int16_t *pcm;
    uint32_t frames;
} Src;

static uint32_t build(const Src *s, int n)
{
    uint32_t at = BANK_HEAD + BANK_ENTRY * (uint32_t)n;
    memset(bankfile, 0, sizeof bankfile);
    memcpy(bankfile, "SMP1", 4);
    bankfile[4] = (uint8_t)n;
    while ((at + STORE_NAME) % SMP_BLOCK)
        at++;
    for (int i = 0; i < n; i++) {
        uint8_t *e = bankfile + BANK_HEAD + BANK_ENTRY * i;
        strncpy((char *)e, s[i].name, BANK_NAME_LEN - 1);
        memcpy(e + BANK_NAME_LEN, &s[i].frames, 4);
        memcpy(e + BANK_NAME_LEN + 4, &at, 4);
        at += encode(s[i].pcm, s[i].frames, bankfile + at);
    }
    return at;
}

static int put(int slot, const char *name, const uint8_t *data, uint32_t n)
{
    if (store_save_begin(slot, name) != STORE_OK || store_save_put((const char *)data, n) != STORE_OK)
        return -1;
    return store_save_commit();
}

/* A slot's bytes back out of the store. */
static uint32_t grab(int slot, uint8_t *out, uint32_t cap)
{
    uint32_t n = 0;
    int32_t got;

    if (store_load_begin(slot) != STORE_OK)
        return 0;
    while (n + 4096 <= cap && (got = store_load_get((char *)out + n, 4096)) > 0)
        n += (uint32_t)got;
    return n;
}

/* ---------------------------------------------------------- a voice --- */
static int32_t buf[FS * 4];

/* how long voice `v` rings, in frames out */
static int rings(SmpVoice *v, int max)
{
    int n = 0;
    while (v->playing && n < max) {
        smp_render(v, buf, 128);
        n += 128;
    }
    return n;
}

static double energy(const int32_t *y, int n)
{
    double e = 0;
    for (int i = 0; i < n; i++)
        e += (double)y[i] * y[i];
    return e;
}

static char text[1 << 17];
static uint32_t at;
static int sink(void *ctx, const char *d, uint32_t n)
{
    (void)ctx;
    if (at + n >= sizeof text)
        return -1;
    memcpy(text + at, d, n);
    at += n;
    return 0;
}

static Song song, back;

static double play(const Song *s, int seconds, int solo)
{
    static int32_t pcm[256];
    double e = 0;
    player_init((Song *)s);
    if (solo >= 0)
        player_toggle_solo(solo);
    player_play_song(0, false);
    for (int i = 0; i < FS * seconds; i += 128) {
        memset(pcm, 0, sizeof pcm);
        player_render_raw(pcm, 128);
        e += energy(pcm, 256);
    }
    return e;
}

static char *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    char *b;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    rewind(f);
    b = malloc((size_t)*n + 1);
    if (fread(b, 1, (size_t)*n, f) != (size_t)*n)
        *n = 0;
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    static int16_t a[SMP_RATE], b[SMP_RATE / 2], c[3000], dec[SMP_RATE];
    static uint8_t coded[SMP_RATE];
    char what[160];
    uint32_t n;

    for (int i = 0; i < SMP_RATE; i++) /* a falling tone with a little noise, half a second loud */
        a[i] = (int16_t)(20000 * exp(-3.0 * i / SMP_RATE) * sin(2 * M_PI * 220 * i / SMP_RATE) +
                         (rand() % 2001 - 1000) * exp(-8.0 * i / SMP_RATE));
    for (int i = 0; i < SMP_RATE / 2; i++)
        b[i] = (int16_t)(12000 * sin(2 * M_PI * 1000 * i / SMP_RATE));
    for (int i = 0; i < 3000; i++)
        c[i] = (int16_t)(i & 64 ? 9000 : -9000);

    /* 1. the codec */
    {
        double sig = 0, err = 0;
        n = encode(a, SMP_RATE, coded);
        CHECK("44 blocks for a second", n == 44 * SMP_BLOCK);
        decode(coded, SMP_RATE, dec);
        for (int i = 0; i < SMP_RATE; i++)
            sig += (double)a[i] * a[i], err += (double)(a[i] - dec[i]) * (a[i] - dec[i]);
        snprintf(what, sizeof what, "ADPCM round trip, SNR %.1f dB", 10 * log10(sig / err));
        CHECK(what, 10 * log10(sig / err) > 20);
        printf("sampler_test: ADPCM round trip, SNR %.1f dB\n", 10 * log10(sig / err));
    }

    /* 2. the bank in a store whose free sectors are scattered */
    {
        static uint8_t junk[3 * STORE_PAYLOAD];
        Src src[3] = { { "tone", a, SMP_RATE }, { "Beep", b, SMP_RATE / 2 }, { "click", c, 3000 } };
        uint8_t map[SMP_MAX_SECTORS];
        uint32_t size = build(src, 3), frames;
        char nm[BANK_NAME_LEN], last[2] = { 7, 0 };
        int k;

        ramflash_blank();
        store_mount();
        memset(junk, 'x', sizeof junk);
        for (int s = 0; s < 6; s++)
            put(s, "JUNK", junk, sizeof junk);
        store_delete(1);
        store_delete(3);
        put(31, ".LAST", (const uint8_t *)last, 2);
        CHECK("the bank is written to slot 32", put(BANK_SLOT, BANK_FILE, bankfile, size) == STORE_OK);
        store_mount();
        k = store_map(BANK_SLOT, map, SMP_MAX_SECTORS);
        CHECK("its sectors are not one run", k > 3 && (map[1] != map[0] + 1 || map[2] != map[1] + 1 ||
                                                        map[k - 1] != map[0] + k - 1));
        CHECK(".LAST is still slot 31", store_has(31) && store_has(BANK_SLOT) && store_has(0) && !store_has(1));
        CHECK("the bank mounts", bank_mount() == STORE_OK && bank_count() == 3 && bank_bytes() == size);
        CHECK("an entry reads", bank_entry(1, nm, &frames) && !strcmp(nm, "Beep") && frames == SMP_RATE / 2);
        CHECK("bare name", bank_find("tone") == 0);
        CHECK("with .wav and a folder", bank_find("tr909/beep.WAV") == 1);
        CHECK("any case", bank_find("CLICK.wav") == 2);
        CHECK("a missing name", bank_find("dir/kick.wav") < 0 && bank_find("") < 0 && bank_find("ton") < 0);

        /* a voice at its root reads every frame, across the sectors */
        {
            SmpVoice v;
            bool same = true;
            int32_t g = (int32_t)(((65536u >> 4) * 16000u) >> 15);
            song_init(&song);
            strcpy(song.smp[5].name, "samples/tone.wav");
            bank_resolve(&song);
            decode(bankfile + *(uint32_t *)(bankfile + BANK_HEAD + BANK_NAME_LEN + 4), SMP_RATE, dec);
            smp_init(&v);
            CHECK("the voice starts", smp_start(&v, 5, 60, 60, 0, 65536));
            for (int i = 0; i < SMP_RATE * 2; i += 128) {
                memset(buf, 0, 128 * sizeof *buf);
                smp_render(&v, buf, 128);
                for (int j = 0; j < 128 && i + j < SMP_RATE * 2; j += 2)
                    same &= buf[j] == (dec[(i + j) / 2] * g) >> 12;
            }
            CHECK("at its root a voice plays the decoded frames, every other frame", same);
        }

        /* a bank that does not read is no bank */
        {
            bankfile[0] = 'X';
            put(BANK_SLOT, BANK_FILE, bankfile, size);
            store_mount();
            CHECK("a damaged bank is none", bank_mount() != STORE_OK && bank_count() == 0 && bank_find("tone") < 0);
            bankfile[0] = 'S';
            put(BANK_SLOT, BANK_FILE, bankfile, size);
            store_mount();
            bank_mount();
        }
    }

    /* 2b. a sample out of the bank: the hole closed in place */
    {
        static uint8_t after[sizeof bankfile];
        Src src[3] = { { "tone", a, SMP_RATE }, { "Beep", b, SMP_RATE / 2 }, { "click", c, 3000 } };
        uint32_t size = build(src, 3), frames, old_at[3], blocks[3], room, got, at0, at1;
        char nm[BANK_NAME_LEN];

        for (int i = 0; i < 3; i++) {
            memcpy(&old_at[i], bankfile + BANK_HEAD + BANK_ENTRY * i + BANK_NAME_LEN + 4, 4);
            blocks[i] = (src[i].frames + SMP_BLOCK_FRAMES - 1) / SMP_BLOCK_FRAMES * SMP_BLOCK;
        }
        ramflash_blank();
        store_mount();
        CHECK("the bank of three is there", put(BANK_SLOT, BANK_FILE, bankfile, size) == STORE_OK);
        store_mount();
        bank_mount();
        room = store_free();
        CHECK("the middle sample goes", bank_delete(1) == STORE_OK);
        CHECK("the bank is one shorter", bank_count() == 2);
        CHECK("the first is as it was",
              bank_entry(0, nm, &frames) && !strcmp(nm, "tone") && frames == SMP_RATE);
        CHECK("the last kept its name and its length",
              bank_entry(1, nm, &frames) && !strcmp(nm, "click") && frames == 3000);
        CHECK("the one between is gone, the others answer in its place",
              bank_find("beep") < 0 && bank_find("tone") == 0 && bank_find("click") == 1);
        CHECK("the file lost exactly that sample's blocks", bank_bytes() == size - blocks[1]);
        CHECK("and the flash got the room back", store_free() > room);

        got = grab(BANK_SLOT, after, sizeof after);
        memcpy(&at0, after + BANK_HEAD + BANK_NAME_LEN + 4, 4);
        memcpy(&at1, after + BANK_HEAD + BANK_ENTRY + BANK_NAME_LEN + 4, 4);
        CHECK("the file on flash is the shorter one", got == size - blocks[1]);
        CHECK("it still opens as a bank of two", !memcmp(after, "SMP1", 4) && after[4] == 2);
        CHECK("what was before the hole did not move at all", at0 == old_at[0]);
        CHECK("what was after it came down by the hole", at1 == old_at[2] - blocks[1]);
        CHECK("and the blocks themselves are the same bytes",
              !memcmp(after + at0, bankfile + old_at[0], blocks[0]) &&
                  !memcmp(after + at1, bankfile + old_at[2], blocks[2]));

        {   /* and the voice reads them where they are now */
            SmpVoice v;
            bool same = true;
            int32_t g = (int32_t)(((65536u >> 4) * 16000u) >> 15);
            song_init(&song);
            strcpy(song.smp[5].name, "click.wav");
            bank_resolve(&song);
            decode(bankfile + old_at[2], 3000, dec);
            smp_init(&v);
            CHECK("a voice starts on the sample that moved", smp_start(&v, 5, 60, 60, 0, 65536));
            for (int i = 0; i < 3000 * 2; i += 128) {
                memset(buf, 0, 128 * sizeof *buf);
                smp_render(&v, buf, 128);
                for (int j = 0; j < 128 && i + j < 3000 * 2; j += 2)
                    same &= buf[j] == (dec[(i + j) / 2] * g) >> 12;
            }
            CHECK("and plays the frames it had before it came down", same);
        }

        CHECK("the first can go as well",
              bank_delete(0) == STORE_OK && bank_count() == 1 && bank_find("click") == 0);
        CHECK("and the last takes the file with it",
              bank_delete(0) == STORE_OK && bank_count() == 0 && !store_has(BANK_SLOT));
        CHECK("a sample that is not there is no error", bank_delete(0) == STORE_ERR_NONE);
        /* the three back, and entry 05 the tone again: what the rest of
         * this test plays is the song section 2 left behind */
        song_init(&song);
        strcpy(song.smp[5].name, "samples/tone.wav");
        put(BANK_SLOT, BANK_FILE, bankfile, size);
        store_mount();
        bank_mount();
    }

    /* 3. smp, zone and fSMP, read and written back */
    {
        static const char src[] =
            "version 3\nbpm 120\nticks 6\nmeter 4 4\ntracks 2\n"
            "smp 00 dir/tone.wav\n"
            "smp 01 beep.wav begin 100 loop 200 4000 ping\n"
            "smp 02 missing.wav loop 10 20\n"
            "smp 03 nobody.wav\n"
            "inst 00 engine SMPL adsr 0 0 255 12 gain 3 name TONE\n"
            "zone 00 C-0 B-3 root C-4 smp 00\n"
            "zone 00 C-4 G-9 root C-5 smp 02 tune -8 gain 4\n"
            "zone 01 C-0 G-9 root C-4 smp 01\n"
            "zone 00 C-0 G-9 root C-4 smp 7F\n"
            "inst 01 engine PULSE adsr 0 0 255 12 name P\n"
            "pat t0 00 len 16\n00 C-4 i00\n04 C-5\n08 C-4 fSMP01\n0C C-4 fSMP02\n"
            "pat t1 00 len 16\n00 C-3 i01\n"
            "arr 00 steps auto\nt0 00\nt1 00\n";
        TrkReport r;
        static char once[1 << 17];
        uint32_t len1;

        CHECK("the song reads", trk_load(&song, src, sizeof src - 1, &r) == TRK_OK);
        CHECK("the table", !strcmp(song.smp[0].name, "dir/tone.wav") && song.smp[1].begin == 100 &&
                               song.smp[1].loop == SMP_LOOP_PING && song.smp[1].loop_start == 200 &&
                               song.smp[1].loop_end == 4000 && song.smp[2].loop == SMP_LOOP_FWD);
        CHECK("the zones, in order", song.nzones == 3 && song.zones[0].hi == 59 && song.zones[1].root == 72 &&
                                         song.zones[1].tune == -8 && song.zones[1].gain == 4 &&
                                         song.zones[2].inst == 1);
        CHECK("a zone past the table is dropped and counted", r.zones == 1);
        CHECK("fSMP is kept", song_step(&song, 0, 0, 8)->fx[0].cmd == FX_SMP &&
                                  song_step(&song, 0, 0, 8)->fx[0].val == 1);
        CHECK("song_zone picks the first that holds", song_zone(&song, 0, 60)->smp == 2 &&
                                                          song_zone(&song, 0, 40)->smp == 0 &&
                                                          !song_zone(&song, 2, 60));
        at = 0;
        CHECK("it writes", trk_write(&song, sink, 0) > 0);
        text[at] = 0;
        len1 = at;
        memcpy(once, text, at + 1);
        CHECK("smp lines", strstr(text, "\nsmp 00 dir/tone.wav\n") &&
                               strstr(text, "\nsmp 01 beep.wav begin 100 loop 200 4000 ping\n") &&
                               strstr(text, "\nsmp 02 missing.wav loop 10 20\n"));
        CHECK("an entry nothing plays is not written", !strstr(text, "nobody"));
        CHECK("zones under their instrument",
              strstr(text, "name TONE\nzone 00 C-0 B-3 root C-4 smp 00\nzone 00 C-4 G-9 root C-5 smp 02 tune -8 gain 4\n"));
        CHECK("no zone under an instrument that is not SMPL", !strstr(text, "zone 01"));
        CHECK("fSMP written", strstr(text, "\n08 C-4 fSMP01\n") && strstr(text, "\n0C C-4 fSMP02\n"));
        CHECK("and reads back", trk_load(&back, text, at, NULL) == TRK_OK);
        at = 0;
        trk_write(&back, sink, 0);
        CHECK("the same the second time", at == len1 && !memcmp(text, once, at));
        if (fails)
            printf("%s", once);
    }

    /* 4. the voice, through the player */
    {
        SmpVoice v;
        int root, up, down;
        double e_zone, e_fsmp, e_missing, e_held;
        static const char src[] =
            "version 3\nbpm 120\nticks 6\nmeter 4 4\ntracks 1\n"
            "smp 00 dir/tone.wav\nsmp 01 Beep.wav\nsmp 02 kick.wav\n"
            "inst 00 engine SMPL adsr 0 0 255 12 name T\nzone 00 C-0 G-9 root C-4 smp 00\n"
            "inst 01 engine SMPL adsr 0 0 255 12 name M\nzone 01 C-0 G-9 root C-4 smp 02\n"
            "pat t0 00 len 64\n00 C-4 i00\n"
            "pat t0 01 len 64\n00 C-4 i01 fSMP01\n"
            "pat t0 02 len 64\n00 C-4 i01\n"
            "arr 00 steps auto\nt0 00\n";
        static char s1[sizeof src], s2[sizeof src];

        smp_init(&v);
        smp_start(&v, 5, 60, 60, 0, 65536); /* the song of 2. named it entry 05 */
        root = rings(&v, FS * 8);
        smp_start(&v, 5, 72, 60, 0, 65536);
        up = rings(&v, FS * 8);
        smp_start(&v, 5, 48, 60, 0, 65536);
        down = rings(&v, FS * 8);
        snprintf(what, sizeof what, "an octave up rings half as long (%d, %d, %d)", root, up, down);
        CHECK(what, root > FS - 256 && root < FS + 256 && up > FS / 2 - 256 && up < FS / 2 + 256 &&
                        down > 2 * FS - 256 && down < 2 * FS + 256);

        CHECK("the song reads", trk_load(&song, src, sizeof src - 1, NULL) == TRK_OK);
        bank_resolve(&song);
        e_zone = play(&song, 1, -1);
        memcpy(s1, src, sizeof src);
        *strstr(s1, "t0 00\n") = 0, strcat(s1, "t0 01\n");
        trk_load(&song, s1, (uint32_t)strlen(s1), NULL);
        bank_resolve(&song);
        e_fsmp = play(&song, 1, -1);
        memcpy(s2, src, sizeof src);
        *strstr(s2, "t0 00\n") = 0, strcat(s2, "t0 02\n");
        trk_load(&song, s2, (uint32_t)strlen(s2), NULL);
        bank_resolve(&song);
        e_missing = play(&song, 1, -1);
        snprintf(what, sizeof what, "a zone sounds, fSMP too, a missing name not (%.3g %.3g %.3g)", e_zone,
                 e_fsmp, e_missing);
        CHECK(what, e_zone > 1e10 && e_fsmp > 1e10 && e_missing == 0);

        trk_load(&song, src, sizeof src - 1, NULL);
        bank_resolve(&song);
        player_init(&song);
        player_play_song(0, false);
        {
            static int32_t pcm[256];
            for (int i = 0; i < 20; i++)
                player_render_raw(pcm, 128);
            bank_hold();
            e_held = 0;
            for (int i = 0; i < FS / 2; i += 128) {
                memset(pcm, 0, sizeof pcm);
                player_render_raw(pcm, 128);
                if (i > FS / 10)
                    e_held += energy(pcm, 256);
            }
        }
        snprintf(what, sizeof what, "the bank held, the voices stop (%.3g)", e_held);
        CHECK(what, e_held < 1e6);
        bank_mount();
    }

    /* 5. a bank from the tool, and a song of moondaw's */
    if (argc == 4) {
        long len = 0, sl = 0;
        char *bank = slurp(argv[1], &len), *trk = slurp(argv[3], &sl);
        char nm[BANK_NAME_LEN];
        uint32_t frames;
        double with, without;

        CHECK("the tool's bank and the song read", bank && trk && len > 0 && sl > 0);
        if (bank && trk) {
            ramflash_blank();
            store_mount();
            put(BANK_SLOT, BANK_FILE, (const uint8_t *)bank, (uint32_t)len);
            store_mount();
            CHECK("the tool's bank mounts", bank_mount() == STORE_OK && bank_count() > 0);
            for (int i = 0; bank_entry(i, nm, &frames); i++) {
                char path[512];
                long wl = 0;
                char *w;
                uint32_t off;
                memcpy(&off, bank + BANK_HEAD + BANK_ENTRY * i + BANK_NAME_LEN + 4, 4);
                snprintf(path, sizeof path, "%s/%s.wav", argv[2], nm);
                w = slurp(path, &wl);
                decode((const uint8_t *)bank + off, frames, dec);
                snprintf(what, sizeof what, "%s: the tool's decoder and this one agree", nm);
                CHECK(what, w && wl == 44 + 2 * (long)frames && !memcmp(w + 44, dec, 2 * frames));
                free(w);
            }
            CHECK("KIDS reads", trk_load(&song, trk, (uint32_t)sl, NULL) == TRK_OK);
            bank_resolve(&song);
            with = play(&song, 16, 0);
            bank_hold();
            without = play(&song, 16, 0);
            snprintf(what, sizeof what, "KIDS' first track plays its samples (%.3g, %.3g without)", with, without);
            CHECK(what, with > 1e13 && with > 100 * without);
            printf("sampler_test: KIDS t0 with the bank %.3g, without %.3g\n", with, without);
        }
        free(bank);
        free(trk);
    }

    printf("sampler_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
