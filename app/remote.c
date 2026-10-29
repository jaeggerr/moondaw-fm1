/* SPDX-License-Identifier: GPL-3.0-only */
#include "remote.h"

#include "../core/player.h"
#include "app.h"
#include "canvas.h"
#include "gfx.h"
#include "link.h"
#include "ui.h"
#include "undo.h"

#define song (&app_song)

/* What the computer asked the hands to do, played in over the next frames. */
static uint32_t pressed, keys;
static int hold;
static int8_t turns[PLAT_ENCS];
static uint16_t last_seq = 0xFFFF;

void remote_input(PlatInput *in)
{
    in->btns |= pressed; /* held one frame: a press, then the release */
    pressed = 0;
    if (hold > 0) {
        in->keys |= keys;
        hold--;
    }
    for (int e = 0; e < PLAT_ENCS; e++) {
        int v = in->enc[e] + turns[e];
        in->enc[e] = (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v);
        turns[e] = 0;
    }
}

/* ----------------------------------------------------------- answers --- */
static bool same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static uint8_t *put(uint8_t *t, uint8_t *end, const char *s)
{
    while (*s && t < end)
        *t++ = (uint8_t)*s++;
    return t;
}

static uint8_t *put_num(uint8_t *t, uint8_t *end, const char *key, int v)
{
    char d[12];
    t = put(t, end, key);
    if (v < 0) {
        t = put(t, end, "-");
        v = -v;
    }
    return put(put(t, end, ui_dec(d, v)), end, " ");
}

/* One line of words a person -- or a model -- reads as it is. */
static uint32_t status(uint8_t *tail, bool reset)
{
    uint8_t *t = tail, *end = tail + LINK_READ_MAX;
    t = put(put(put(t, end, "screen="), end, app_screen()), end, " ");
    t = put(put(put(t, end, "song="), end, app_name), end, " ");
    t = put_num(t, end, "dirty=", app_dirty);
    t = put_num(t, end, "slot=", app_slot);
    t = put_num(t, end, "playing=", player_playing());
    t = put_num(t, end, "phrase=", player_entry());
    t = put_num(t, end, "step=", player_row());
    t = put_num(t, end, "cpu=", app_cpu);
    t = put_num(t, end, "max=", app_worst);
    t = put_num(t, end, "bpm=", song->bpm);
    t = put_num(t, end, "phrases=", song->arrange_len);
    t = put_num(t, end, "tracks=", song->num_tracks);
    t = put_num(t, end, "free=", song_pool_free(song));
    if (reset)
        app_worst = 0;
    return (uint32_t)(t - tail);
}

/* The platform's record of the last start: the FM-1 points it at firmware.c's;
 * elsewhere it stays null. */
const uint32_t *remote_boot_info;

static uint8_t *put_hex(uint8_t *t, uint8_t *end, const char *key, uint32_t v)
{
    char d[9];
    for (int i = 0; i < 8; i++)
        d[i] = "0123456789ABCDEF"[v >> (28 - 4 * i) & 15];
    d[8] = 0;
    return put(put(put(t, end, key), end, d), end, " ");
}

/* Why the machine last started, its last crash, and the second core. */
static uint32_t boot(uint8_t *tail)
{
    uint8_t *t = tail, *end = tail + LINK_READ_MAX;
    const uint32_t *b = remote_boot_info;
    if (!b)
        return (uint32_t)(put(t, end, "no record here") - tail);
    t = put_hex(t, end, "rst=", b[0] << 24 | b[1]);
    t = put_num(t, end, "crashes=", (int)b[2]);
    t = put_hex(t, end, "vec=", b[3]);
    t = put_hex(t, end, "pc=", b[4]);
    t = put_num(t, end, "at_ms=", (int)b[5]);
    t = put_num(t, end, "cpu1=", (int)b[6]);
    t = put_num(t, end, "gave_up=", (int)b[7]);
    t = put_num(t, end, "jobs=", (int)b[8]);
    t = put_num(t, end, "stack=", (int)b[9]);
    return (uint32_t)(t - tail);
}

/* Where the run before this start was when it ended (firmware.c, the trail): the main loop's stage and its ms at
 * its last turn, the ms of the last audio half, whether a half was being rendered, the worst one, how many came
 * late, the track each core rendered (player_trail), the last block of sample read. */
static uint32_t trail(uint8_t *tail)
{
    uint8_t *t = tail, *end = tail + LINK_READ_MAX;
    const uint32_t *b = remote_boot_info;
    if (!b)
        return (uint32_t)(put(t, end, "no record here") - tail);
    t = put_num(t, end, "stage=", (int)b[10]);
    t = put_num(t, end, "loop_ms=", (int)b[11]);
    t = put_num(t, end, "audio_ms=", (int)b[12]);
    t = put_num(t, end, "in_audio=", (int)b[13]);
    t = put_num(t, end, "max_us=", (int)b[14]);
    t = put_num(t, end, "late=", (int)b[15]);
    t = put_hex(t, end, "track=", b[16]);
    t = put_num(t, end, "smp=", (int)b[17]);
    return (uint32_t)(t - tail);
}

/* The last crash's registers (firmware/hal/fm1_irq.h): the caller, EMU_MSG (bit 3: the stack out of its
 * window), DBG_MSG (bit 13: a write into a guard band; 4, 5, 16..21: unmapped; 12: the pc out of the code), the
 * stack pointer, the last four jumps the trace saw. */
static uint32_t crash(uint8_t *tail)
{
    uint8_t *t = tail, *end = tail + LINK_READ_MAX;
    const uint32_t *b = remote_boot_info;
    if (!b)
        return (uint32_t)(put(t, end, "no record here") - tail);
    t = put_hex(t, end, "rets=", b[18]);
    t = put_hex(t, end, "emu=", b[19]);
    t = put_hex(t, end, "dbg=", b[20]);
    t = put_hex(t, end, "sp=", b[21]);
    t = put(t, end, "etm=");
    for (int k = 0; k < 4; k++)
        t = put_hex(t, end, "", b[22 + k]);
    return (uint32_t)(t - tail);
}

/* The worst audio block's time, stage by stage, in microseconds (core/player.h): the
 * line HOME draws while a song plays, and whether the second core is taking its share. */
static uint32_t profile(uint8_t *tail, bool reset)
{
    static const char *const key[PROF_N] = { "T=", "O=", "V=", "C=", "R=", "E=", "W=" };
    uint8_t *t = tail, *end = tail + LINK_READ_MAX;
    uint32_t p[PROF_N + 1];
    player_profile(p);
    t = put_num(t, end, "cpu1=", player_run_other != 0);
    for (int k = 0; k < PROF_N; k++)
        t = put_num(t, end, key[k], (int)p[k]);
    t = put_num(t, end, "block=", (int)p[PROF_N]);
    t = put_num(t, end, "max=", app_worst);
    if (reset) {
        player_profile_reset();
        app_worst = 0;
    }
    return (uint32_t)(t - tail);
}

/* ------------------------------------------------------------ writes --- */
static bool track_ok(int t) { return t >= 0 && t < song->num_tracks; }
static bool block_ok(int b) { return b >= 0 && b < TRACK_MAX_PATTERNS; }

/* A step written whole, as the file has it: four notes, an instrument,
 * three effects. UNDO takes it back like a hand's. */
static int set_step(const uint8_t *p, uint32_t n)
{
    Step *st;
    if (n != 3 + 11 || !track_ok(p[0]) || !block_ok(p[1]) || p[2] >= PATTERN_MAX_STEPS)
        return LINK_BADMSG;
    undo_step(p[0], p[1], p[2]);
    if (!(st = song_step_w(song, p[0], p[1], p[2])))
        return LINK_FULL;
    for (int k = 0; k < STEP_MAX_NOTES; k++)
        st->notes[k] = p[3 + k];
    st->instrument = p[7];
    for (int k = 0; k < STEP_MAX_FX; k++) {
        st->fx[k].cmd = p[8 + 2 * k] < FX_SYL ? p[8 + 2 * k] : FX_NONE;
        st->fx[k].val = st->fx[k].cmd ? fx_clamp_val(st->fx[k].cmd, song->ticks_per_step, p[9 + 2 * k]) : 0;
    }
    if (p[2] >= song->patterns[p[0]][p[1]].length) /* a step past the end makes the block longer */
        song->patterns[p[0]][p[1]].length = (uint16_t)(p[2] + 1);
    app_mark_dirty();
    return LINK_OK;
}

/* A phrase's cell. A phrase past the last is made, empty, with the ones
 * between. */
static int set_cell(const uint8_t *p, uint32_t n)
{
    SongEntry *e;
    if (n != 3 || p[0] >= SONG_ARRANGE_LEN || !track_ok(p[1]) || (p[2] != PATTERN_NONE && !block_ok(p[2])))
        return LINK_BADMSG;
    if (p[0] >= song->arrange_len) {
        undo_bytes(&song->arrange_len, 1);
        undo_bytes(&song->arrangement[song->arrange_len], (uint32_t)(p[0] + 1 - song->arrange_len) * sizeof(SongEntry));
        while (song->arrange_len <= p[0]) {
            SongEntry *f = &song->arrangement[song->arrange_len++];
            for (int t = 0; t < SONG_MAX_TRACKS; t++)
                f->pattern[t] = PATTERN_NONE, f->level[t] = VOL_FULL;
            f->steps = 0;
            f->name[0] = 0;
            f->kind = ENTRY_PHRASE;
            f->section = SECTION_NONE;
        }
    }
    e = &song->arrangement[p[0]];
    if (e->kind == ENTRY_REF)
        return LINK_BADMSG;
    undo_bytes(&e->pattern[p[1]], 1);
    e->pattern[p[1]] = p[2];
    app_mark_dirty();
    return LINK_OK;
}

static int set_length(const uint8_t *p, uint32_t n)
{
    int len;
    if (n != 4 || !track_ok(p[0]) || !block_ok(p[1]))
        return LINK_BADMSG;
    len = p[2] | p[3] << 8;
    if (len < 1 || len > PATTERN_MAX_STEPS)
        return LINK_BADMSG;
    undo_bytes(&song->patterns[p[0]][p[1]].length, sizeof(uint16_t));
    song->patterns[p[0]][p[1]].length = (uint16_t)len;
    app_mark_dirty();
    return LINK_OK;
}

/* A band of the screen, run-length coded, and the bytes of it from `offset`. */
static int screen(const uint8_t *p, uint32_t n, uint32_t *extra, uint8_t *tail, uint32_t *tail_n)
{
    const uint16_t *px = n == 3 ? gfx_band(p[0]) : 0;
    uint32_t offset, at = 0;
    if (!px)
        return LINK_BADMSG;
    offset = (uint32_t)(p[1] | p[2] << 8);
    for (int i = 0; i < PLAT_W * 16;) {
        uint8_t run[3];
        int k = 1;
        while (i + k < PLAT_W * 16 && px[i + k] == px[i] && k < 255)
            k++;
        run[0] = (uint8_t)k, run[1] = (uint8_t)px[i], run[2] = (uint8_t)(px[i] >> 8);
        for (int j = 0; j < 3; j++, at++)
            if (at >= offset && at < offset + LINK_READ_MAX)
                tail[at - offset] = run[j];
        i += k;
    }
    *extra = at;
    *tail_n = at > offset ? (at - offset < LINK_READ_MAX ? at - offset : LINK_READ_MAX) : 0;
    return LINK_OK;
}

/* The LEDs the computer holds (LED): a level each, NONE where the app lights it. */
enum { LEDS = PLAT_BTNS + PLAT_KEYS, NONE = 0xFF };
static uint8_t held[LEDS];
static bool holding;

void remote_leds(void)
{
    if (holding)
        for (int i = 0; i < LEDS; i++)
            if (held[i] != NONE)
                plat_led_level(i, held[i]);
}

static int set_leds(const uint8_t *p, uint32_t n)
{
    if (n == 0 || n % 2)
        return LINK_BADMSG;
    for (uint32_t k = 0; k < n; k += 2)
        if ((p[k] >= LEDS && p[k] != 0xFF) || (p[k + 1] >= PLAT_LED_LEVELS && p[k + 1] != NONE))
            return LINK_BADMSG;
    for (uint32_t k = 0; k < n; k += 2)
        for (int i = p[k] == 0xFF ? 0 : p[k]; i < (p[k] == 0xFF ? LEDS : p[k] + 1); i++) {
            if (p[k + 1] == NONE && held[i] != NONE)
                plat_led_level(i, PLAT_LED_OFF); /* given back: the app lights it again */
            held[i] = p[k + 1];
        }
    holding = false;
    for (int i = 0; i < LEDS; i++)
        holding |= held[i] != NONE;
    ui_led_guide_again();
    return LINK_OK;
}

/* ------------------------------------------------------------- the hook --- */
static int control(int cmd, const uint8_t *p, uint32_t n, uint32_t *extra, uint8_t *tail, uint32_t *tail_n)
{
    *tail_n = 0;
    switch (cmd) {
    case LINK_CMD_STATUS:
        /* bit 0: start the worst block over; bit 1: the profile line instead; bit 2: the boot record */
        *tail_n = n >= 1 && (p[0] & 16) ? crash(tail)
                  : n >= 1 && (p[0] & 8) ? trail(tail)
                  : n >= 1 && (p[0] & 4) ? boot(tail)
                  : n >= 1 && (p[0] & 2) ? profile(tail, p[0] & 1) : status(tail, n >= 1 && (p[0] & 1));
        return LINK_OK;
    case LINK_CMD_INPUT: {
        uint16_t seq;
        if (n != 2 + 4 + 4 + 1 + PLAT_ENCS)
            return LINK_BADMSG;
        seq = (uint16_t)(p[0] | p[1] << 8);
        if (seq == last_seq)
            return LINK_OK; /* the answer was lost and it came again: already done */
        last_seq = seq;
        pressed |= (uint32_t)p[2] | (uint32_t)p[3] << 8 | (uint32_t)p[4] << 16 | (uint32_t)p[5] << 24;
        keys = (uint32_t)p[6] | (uint32_t)p[7] << 8 | (uint32_t)p[8] << 16 | (uint32_t)p[9] << 24;
        hold = p[10];
        for (int e = 0; e < PLAT_ENCS; e++)
            turns[e] = (int8_t)(turns[e] + (int8_t)p[11 + e]);
        return LINK_OK;
    }
    case LINK_CMD_OPEN:
        if (n != 2 || p[0] >= APP_SONG_SLOTS)
            return LINK_BADMSG;
        if (app_dirty && !(p[1] & 1))
            return LINK_DIRTY;
        return app_open_slot(p[0]) == STORE_OK ? LINK_OK : LINK_NONE;
    case LINK_CMD_NEW:
        if (app_dirty && !(n >= 1 && (p[0] & 1)))
            return LINK_DIRTY;
        app_new();
        return LINK_OK;
    case LINK_CMD_STEP:
        return set_step(p, n);
    case LINK_CMD_CELL:
        return set_cell(p, n);
    case LINK_CMD_LENGTH:
        return set_length(p, n);
    case LINK_CMD_SAVE: /* extra: the slot it went to */
        if (app_save() != 0)
            return LINK_FULL;
        *extra = (uint32_t)app_slot;
        return LINK_OK;
    case LINK_CMD_PLAY:
        if (n != 2 || p[1] >= song->arrange_len)
            return LINK_BADMSG;
        if (p[0] == 0)
            player_stop();
        else if (p[0] == 1)
            player_play_song(p[1], false);
        else
            player_play_entry(p[1]);
        return LINK_OK;
    case LINK_CMD_CANVAS:
        if (n < 1)
            return LINK_BADMSG;
        app_show_canvas();
        return canvas_orders(p, n) ? LINK_OK : LINK_BADMSG;
    case LINK_CMD_EVENTS:
        canvas_events(tail);
        *tail_n = CANVAS_EVENTS;
        *extra = same(app_screen(), "CANVAS"); /* still in front, or the hand left it */
        return LINK_OK;
    case LINK_CMD_LED:
        return set_leds(p, n);
    case LINK_CMD_SCREEN:
        return screen(p, n, extra, tail, tail_n);
    default:
        return LINK_BADMSG;
    }
}

void remote_init(void)
{
    for (int i = 0; i < LEDS; i++)
        held[i] = NONE;
    link_control = control;
}
