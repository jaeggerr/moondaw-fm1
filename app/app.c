/* SPDX-License-Identifier: GPL-3.0-only */
/* The tracker on the three backends. Two screens so far:
 *
 *   - a provisional one that only PLAYS (this file, in text cells):
 *     PLAY starts and stops the song, the first eight keys mute the
 *     tracks, SELECT picks a phrase and ALGORITHM a track;
 *   - PATTERN (pattern.c), the first screen of the real interface,
 *     opened from here with EDIT on that phrase and that track.
 *
 * SONG and PHRASE will take this file's place as the way in. */
#include "../core/player.h"
#include "../core/store.h"
#include "../core/trk.h"
#include "../platform/platform.h"
#include "demo_trk.h"
#include "pattern.h"
#include "term.h"

static Song song
#ifdef PLAT_FM1
    __attribute__((section(".pool")))
#endif
    ;
static TrkReport report;
static char song_name[20] = "DEMO";
static volatile uint8_t ready; /* the audio side renders only while set */
static uint32_t prev_btns, prev_keys;
static int start_entry, sel_track;
static bool in_pattern;

static void put_report(int y, const char *what, int n)
{
    if (!n)
        return;
    term_num(1, y, n, 4, C_AMBER, C_BLACK);
    term_put(6, y, what, C_DIM, C_BLACK);
}

static void draw_static(void)
{
    int y = 17;
    term_clear();
    term_put(0, 0, " MOONDAW FM-1                 ", C_BLACK, C_CYAN);
    term_put(1, 2, song_name, C_WHITE, C_BLACK);
    term_put(1, 4, "BPM", C_DIM, C_BLACK);
    term_put(1, 5, "PHRASE", C_DIM, C_BLACK);
    term_put(1, 6, "ROW", C_DIM, C_BLACK);
    term_put(1, 7, "STEPS", C_DIM, C_BLACK);
    term_put(1, 13, "PLAY song   keys 1-8 mute", C_DIM, C_BLACK);
    term_put(1, 14, "SELECT phrase ALGORITHM track", C_DIM, C_BLACK);
    term_put(1, 15, "EDIT opens its pattern", C_DIM, C_BLACK);
    /* What a save from this machine would not carry. */
    put_report(y, "sample lines", report.samples + report.zones), y += !!(report.samples + report.zones);
    put_report(y, "syllables", report.syllables), y += !!report.syllables;
    put_report(y, "silent instruments", report.silent_instr), y += !!report.silent_instr;
    put_report(y, "drive/amp/voice fields", report.shaping), y += !!report.shaping;
    put_report(y, "reference lines", report.ref_lines), y += !!report.ref_lines;
    put_report(y, "steps cut (tracks 9+)", report.steps_cut), y += !!report.steps_cut;
    put_report(y, "STEPS LOST: POOL FULL", report.steps_lost);
}

int app_load(const char *trk, uint32_t len, const char *name)
{
    TrkReport r;
    int i = 0;

    ready = 0;
    if (trk_load(&song, trk, len, &r) != TRK_OK) {
        ready = 1; /* the song in memory is untouched */
        return 0;
    }
    report = r;
    player_init(&song);
    for (; name && name[i] && i < (int)sizeof(song_name) - 1; i++)
        song_name[i] = name[i] >= 'a' && name[i] <= 'z' ? (char)(name[i] - 32) : name[i];
    if (i)
        song_name[i] = '\0';
    start_entry = sel_track = 0;
    in_pattern = false;
    term_invalidate();
    draw_static();
    ready = 1;
    return 1;
}

/* One song lives in the machine for now, in slot 0; a list of projects
 * comes with the MENU screen. Saving stops the song first: on the FM-1
 * an erase holds every interrupt for its 50 ms, and the audio with it. */
#define SONG_SLOT 0

static int flash_sink(void *ctx, const char *data, uint32_t n)
{
    (void)ctx;
    return store_save_put(data, n);
}

int app_save(void)
{
    int rc;

    player_stop();
    ready = 0;
    rc = store_save_begin(SONG_SLOT, song_name);
    if (rc == STORE_OK && trk_write(&song, flash_sink, 0) < 0)
        rc = STORE_ERR_FULL;
    if (rc == STORE_OK)
        rc = store_save_commit();
    else
        store_mount(); /* what a failed save wrote is free space again */
    ready = 1;
    return rc;
}

static bool load_from_flash(void)
{
    static char piece[512];
    char name[STORE_NAME + 1];
    uint32_t bytes;
    int32_t n;
    TrkReport r;

    if (store_mount() != STORE_OK || store_info(SONG_SLOT, name, &bytes) != STORE_OK ||
        store_load_begin(SONG_SLOT) != STORE_OK)
        return false;
    ready = 0;
    trk_begin(&r);
    while ((n = store_load_get(piece, sizeof piece)) > 0)
        trk_feed(piece, (uint32_t)n);
    if (n < 0 || trk_end(&song) != TRK_OK) {
        ready = 1;
        return false; /* a sector failed its checksum: the song in memory is untouched */
    }
    report = r;
    player_init(&song);
    for (int i = 0; i <= STORE_NAME; i++)
        song_name[i] = name[i];
    start_entry = sel_track = 0;
    in_pattern = false;
    term_invalidate();
    draw_static();
    ready = 1;
    return true;
}

void app_init(void)
{
    if (!load_from_flash())
        app_load(demo_trk, sizeof(demo_trk) - 1, "DEMO");
}

void app_frame(const PlatInput *in)
{
    uint32_t pressed = in->btns & ~prev_btns, keys = in->keys & ~prev_keys;
    bool playing = player_playing();

    player_set_master(in->master);
    if (in_pattern) {
        if (pattern_frame(in))
            return;
        in_pattern = false; /* HOME: back here, redrawn whole */
        term_invalidate();
        draw_static();
        prev_btns = in->btns;
        prev_keys = in->keys;
        return;
    }
    prev_btns = in->btns;
    prev_keys = in->keys;
    if (pressed & (1u << PB_PLAY)) {
        if (playing)
            player_stop();
        else
            player_play_song(start_entry, false);
    }
    for (int t = 0; t < SONG_MAX_TRACKS; t++)
        if ((keys >> t) & 1u)
            player_toggle_mute(t);
    if (in->enc[PE_SELECT] && !playing) {
        start_entry += in->enc[PE_SELECT];
        start_entry = start_entry < 0 ? 0
                      : start_entry >= song.arrange_len ? song.arrange_len - 1
                      : start_entry;
    }
    sel_track += in->enc[PE_ALGO];
    sel_track = sel_track < 0 ? 0 : sel_track >= song.num_tracks ? song.num_tracks - 1 : sel_track;
    if ((pressed & (1u << PB_EDIT)) && pattern_open(&song, playing ? player_entry() : start_entry, sel_track)) {
        in_pattern = true;
        for (int t = 0; t < PLAT_KEYS; t++)
            plat_led_key(t, 0);
        return;
    }

    term_num(8, 4, song.bpm, 4, C_WHITE, C_BLACK);
    term_num(8, 5, playing ? player_entry() : start_entry, 4,
             playing ? C_GREEN : C_WHITE, C_BLACK);
    term_put(12, 5, "/", C_DIM, C_BLACK);
    term_num(13, 5, song.arrange_len, 3, C_DIM, C_BLACK);
    term_num(8, 6, playing ? player_row() : 0, 4, playing ? C_GREEN : C_DIM, C_BLACK);
    term_num(8, 7, (POOL_CHUNKS - song_pool_free(&song)) * CHUNK_STEPS, 4, C_WHITE, C_BLACK);
    term_put(12, 7, "/", C_DIM, C_BLACK);
    term_num(13, 7, POOL_CHUNKS * CHUNK_STEPS, 4, C_DIM, C_BLACK);
    term_put(20, 4, playing ? "PLAYING" : "STOPPED", playing ? C_GREEN : C_DIM, C_BLACK);
    {   /* the audio interrupt's share of its time: average, and the worst half since last frame */
        static int worst_seen;
        int worst, load = plat_audio_load(&worst);
        if (load >= 0) {
            if (worst > worst_seen || !playing)
                worst_seen = worst;
            term_put(20, 6, "CPU", C_DIM, C_BLACK);
            term_num(23, 6, load, 4, load > 70 ? C_RED : C_WHITE, C_BLACK);
            term_put(20, 7, "MAX", C_DIM, C_BLACK);
            term_num(23, 7, worst_seen, 4, worst_seen > 90 ? C_RED : C_WHITE, C_BLACK);
        }
    }
    for (int t = 0; t < SONG_MAX_TRACKS; t++) {
        bool there = t < song.num_tracks, muted = player_muted(t);
        int fg = !there ? C_DIM : t == sel_track ? C_BLACK : C_TEXT;
        int bg = there && t == sel_track ? C_GREEN : C_BLACK;
        term_putc(1 + t * 3, 10, 'T', fg, bg);
        term_putc(2 + t * 3, 10, (char)('1' + t), fg, bg);
        term_put(1 + t * 3, 11, !there ? "  " : muted ? "--" : "ON",
                 muted ? C_RED : C_GREEN, C_BLACK);
        plat_led_key(t, there && !muted);
    }
    plat_led_btn(PB_PLAY, playing);
    term_flush();
}

void app_audio(int16_t *out, int frames)
{
    if (ready) {
        player_render(out, frames);
        return;
    }
    for (int i = 0; i < frames * 2; i++)
        out[i] = 0;
}
