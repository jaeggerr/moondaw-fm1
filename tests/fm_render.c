/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 core, offline: a song to raw int32 stereo (Q15, before the
 * clamp), or to a 16-bit WAV as the device would put it out.
 *
 *   fm_render SONG.trk OUT.i32 SECONDS
 *   fm_render SONG.trk OUT.wav SECONDS
 *   ... [SOLO_TRACK] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/player.h"
#include "../core/trk.h"

static Song song;
static char text[1 << 20];

static void le(FILE *f, unsigned long v, int n)
{
    while (n--) {
        fputc((int)(v & 0xFF), f);
        v >>= 8;
    }
}

int main(int argc, char **argv)
{
    if (argc < 4)
        return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 1;
    size_t n = fread(text, 1, sizeof text, f);
    fclose(f);
    if (trk_load(&song, text, (uint32_t)n, NULL) != TRK_OK)
        return 1;
    long want = (long)(atof(argv[3]) * 44100), done = 0;
    int wav = strlen(argv[2]) > 4 && !strcmp(argv[2] + strlen(argv[2]) - 4, ".wav");
    f = fopen(argv[2], "wb");
    if (!f)
        return 1;
    if (wav) {
        fwrite("RIFF", 1, 4, f); le(f, 36 + (unsigned long)want * 4, 4);
        fwrite("WAVEfmt ", 1, 8, f); le(f, 16, 4); le(f, 1, 2); le(f, 2, 2);
        le(f, 44100, 4); le(f, 44100 * 4, 4); le(f, 4, 2); le(f, 16, 2);
        fwrite("data", 1, 4, f); le(f, (unsigned long)want * 4, 4);
    }
    player_init(&song);
    for (int t = 0; argc > 4 && t < SONG_MAX_TRACKS; t++)
        if (t != atoi(argv[4]))
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
