/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 core, offline: a song to raw int32 stereo (Q15, before the
 * clamp), or to a 16-bit WAV as the device would put it out.
 *
 *   fm_render SONG.trk OUT.i32 SECONDS
 *   fm_render SONG.trk OUT.wav SECONDS
 *   ... [SOLO_TRACK]
 *   ... --bank BANK.bin      (tools/fm1_samples.py pack: what SMPL plays)
 *
 * Without --bank there is no bank at all and every SMPL instrument is
 * silent, as the device is before one is sent. With one, it is written
 * into a flash in RAM as the store holds it on the device, so the voice
 * reads it the same way, over the same sectors (core/bank.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/bank.h"
#include "../core/player.h"
#include "../core/store.h"
#include "../core/trk.h"
#include "ramflash.h"

static Song song;
static char text[1 << 20];
static char bankfile[PLAT_FLASH_SECTORS * PLAT_FLASH_SECTOR];

static void le(FILE *f, unsigned long v, int n)
{
    while (n--) {
        fputc((int)(v & 0xFF), f);
        v >>= 8;
    }
}

/* The bank into slot BANK_SLOT of a blank store, then mounted: what the
 * link does on the device, without the link. */
static int bank_from(const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f)
        return 1;
    n = fread(bankfile, 1, sizeof bankfile, f);
    fclose(f);
    ramflash_blank();
    if (store_mount() != STORE_OK)
        return 1;
    if (store_save_begin(BANK_SLOT, BANK_FILE) != STORE_OK ||
        store_save_put(bankfile, (uint32_t)n) != STORE_OK || store_save_commit() != STORE_OK)
        return 1;
    return store_mount() != STORE_OK || bank_mount() != STORE_OK;
}

int main(int argc, char **argv)
{
    const char *pos[5] = {0};
    const char *bank = NULL;
    int np = 0;
    long want, done = 0;
    int wav;
    FILE *f;

    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--bank") && i + 1 < argc)
            bank = argv[++i];
        else if (np < 5)
            pos[np++] = argv[i];
    }
    if (np < 4)
        return 2;
    f = fopen(pos[1], "rb");
    if (!f)
        return 1;
    size_t n = fread(text, 1, sizeof text, f);
    fclose(f);
    if (trk_load(&song, text, (uint32_t)n, NULL) != TRK_OK)
        return 1;
    if (bank) {
        if (bank_from(bank)) {
            fprintf(stderr, "fm_render: %s is no bank\n", bank);
            return 1;
        }
        bank_resolve(&song);
    }
    want = (long)(atof(pos[3]) * 44100);
    wav = strlen(pos[2]) > 4 && !strcmp(pos[2] + strlen(pos[2]) - 4, ".wav");
    f = fopen(pos[2], "wb");
    if (!f)
        return 1;
    if (wav) {
        fwrite("RIFF", 1, 4, f); le(f, 36 + (unsigned long)want * 4, 4);
        fwrite("WAVEfmt ", 1, 8, f); le(f, 16, 4); le(f, 1, 2); le(f, 2, 2);
        le(f, 44100, 4); le(f, 44100 * 4, 4); le(f, 4, 2); le(f, 16, 2);
        fwrite("data", 1, 4, f); le(f, (unsigned long)want * 4, 4);
    }
    player_init(&song);
    for (int t = 0; np > 4 && t < SONG_MAX_TRACKS; t++)
        if (t != atoi(pos[4]))
            player_toggle_mute(t);
    player_play_song(0, false);
    while (done < want) {
        static int32_t raw[128 * 2];
        static int16_t pcm[128 * 2];
        int c = want - done < 128 ? (int)(want - done) : 128;
        if (wav) {
            player_render(pcm, c);
            fwrite(pcm, 2, (size_t)c * 2, f);
        } else {
            player_render_raw(raw, c);
            fwrite(raw, 4, (size_t)c * 2, f);
        }
        done += c;
    }
    fclose(f);
    return 0;
}
