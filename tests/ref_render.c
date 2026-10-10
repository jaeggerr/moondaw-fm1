/* SPDX-License-Identifier: GPL-3.0-only */
/* The reference: moondaw's own engine, linked from its build directory,
 * rendering a song at 44.1 kHz with no master limiter, as raw float32
 * stereo. What tests/fm_render.c is compared against.
 *
 *   ref_render SONG.trk OUT.f32 SECONDS [SOLO_TRACK] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/audio.h"
#include "instruments/notes.h"
#include "instruments/samplepool.h"
#include "sequencer/player.h"
#include "sequencer/sequencer.h"
#include "sequencer/song.h"
#include "sequencer/songfile.h"

#define SR 44100

int audio_samplerate(void) { return SR; }
void audio_pause(bool on) { (void)on; }
void audio_tick_now(void) {}
void audio_lock(void) {}
void audio_unlock(void) {}

static float buf[AUDIO_BUFFER_FRAMES * 2];

int main(int argc, char **argv)
{
    if (argc < 4)
        return 2;
    long want = (long)(atof(argv[3]) * SR), done = 0;
    notes_init();
    samplepool_set_dir("/nonexistent");
    samplepool_set_lib_dir("/nonexistent");
    song_init(song_get());
    if (!song_load(song_get(), argv[1]))
        return 1;
    if (!player_init(SR))
        return 1;
    FILE *f = fopen(argv[2], "wb");
    if (!f)
        return 1;
    for (int t = 0; argc > 4 && t < 16; t++)
        if (t != atoi(argv[4]))
            player_toggle_mute(t);
    player_play_song(0);
    player_handle_cmds();
    while (done < want) {
        sequencer_tick();
        player_tick();
        int left = player_tick_samples(SR);
        while (left > 0 && done < want) {
            int c = left < AUDIO_BUFFER_FRAMES ? left : AUDIO_BUFFER_FRAMES;
            if (c > want - done)
                c = (int)(want - done);
            memset(buf, 0, sizeof buf);
            player_render(buf, c);
            fwrite(buf, sizeof(float), (size_t)c * 2, f);
            left -= c;
            done += c;
        }
    }
    fclose(f);
    return 0;
}
