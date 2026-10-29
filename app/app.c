/* SPDX-License-Identifier: GPL-3.0-only */
/* The tracker on the three backends: one song in memory, and the screens
 * that take turns in front of it.
 *
 *   HOME     (menu.c)      where the machine opens: the projects, by name
 *   SONG     (songview.c)  the arrangement: phrases down, tracks across
 *   SECTION  (phrase.c)    one phrase, step by step
 *   PATTERN  (pattern.c)   one block of one track, in three views
 *   INSTRUMENT (instrument.c)  one sound, from the step that calls it
 *   MIXER    (mixer.c)     the tracks' levels and sends, the returns: FX
 *   PROJECT  (project.c)   name, tempo, swing, meter, step, ticks, tracks: EDIT
 *
 * The road down is HOME -> SONG -> SECTION -> PATTERN, SEL a level down,
 * GLO a level up, HOME home from anywhere (NOTES.md, 28/10). INSTRUMENT
 * still has the 10/10 look. */
#include "app.h"

#include "../core/player.h"
#include "../core/trk.h"
#include "demo_trk.h"
#include "gfx.h"
#include "ui.h"
#include "instrument.h"
#include "menu.h"
#include "mixer.h"
#include "edit.h"
#include "link.h"
#include "loop.h"
#include "phrase.h"
#include "undo.h"
#include "pattern.h"
#include "project.h"
#include "canvas.h"
#include "remote.h"
#include "songview.h"

Song app_song
#ifdef PLAT_FM1
    __attribute__((section(".pool")))
#endif
    ;
char app_name[STORE_NAME + 1] = "DEMO";
int app_slot = -1;
bool app_dirty;
int app_cpu, app_block, app_worst;

static TrkReport report;       /* what the last load could not carry */
static volatile uint8_t ready; /* the audio side renders only while set */
static enum { AT_MENU, AT_SONG, AT_PHRASE, AT_PATTERN, AT_INSTRUMENT, AT_MIXER, AT_PROJECT, AT_CANVAS } screen;
static int mixer_from, project_from, pattern_from, canvas_from; /* the screen GLO leads back to */

void app_mark_dirty(void) { app_dirty = true; }

const char *app_screen(void)
{
    static const char *const name[] = { "HOME", "SONG", "SECTION", "PATTERN", "INSTRUMENT", "MIXER", "PROJECT",
                                        "CANVAS" };
    return name[screen];
}

void app_show_canvas(void)
{
    if (screen == AT_CANVAS)
        return;
    canvas_from = screen;
    screen = AT_CANVAS;
    canvas_open();
}

/* GLO on the CANVAS: back to what was there, as it was where the screen
 * can be resumed, from its start where it cannot. */
static void leave_canvas(void)
{
    screen = canvas_from;
    if (screen == AT_MENU)
        menu_open();
    else if (screen == AT_PHRASE)
        phrase_resume();
    else if (screen == AT_PATTERN || screen == AT_INSTRUMENT)
        screen = AT_PATTERN, pattern_resume();
    else if (screen == AT_MIXER)
        mixer_open("", songview_entry());
    else if (screen == AT_PROJECT)
        project_open();
    else
        songview_open(false);
}

int app_not_carried(void)
{
    return report.samples + report.zones + report.ref_lines + report.previews + report.shaping +
           report.syllables + report.smp_fx + report.steps_cut;
}

static void set_name(const char *name)
{
    int i = 0;
    for (; name && name[i] && i < STORE_NAME; i++)
        app_name[i] = name[i] >= 'a' && name[i] <= 'z' ? (char)(name[i] - 32) : name[i];
    app_name[i] = '\0';
}

static bool same_name(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

/* Whatever just became the song in memory: the player starts over on
 * it, and the hand is back at the MENU. */
static void adopt(int slot, bool dirty)
{
    player_init(&app_song);
    loop_clear();
    edit_clip_reset(); /* its instrument numbers were the last song's */
    undo_reset(&app_song); /* another song: nothing of the last one to take back */
    app_slot = slot;
    app_dirty = dirty;
    screen = AT_MENU;
    ready = 1;
    menu_open();
}

/* ------------------------------------------------------------ flash --- */
/* The last slot holds one byte: which song was open. So the machine
 * comes back on what was being worked on, not on the first of the list. */
#define LAST_SLOT (STORE_SLOTS - 1)

static void remember(int slot)
{
    char b = (char)slot;
    char was;
    if (store_load_begin(LAST_SLOT) == STORE_OK && store_load_get(&was, 1) == 1 && was == b)
        return; /* already says so: an erase saved */
    if (store_save_begin(LAST_SLOT, ".LAST") == STORE_OK && store_save_put(&b, 1) == STORE_OK)
        store_save_commit();
}

static int flash_sink(void *ctx, const char *data, uint32_t n)
{
    (void)ctx;
    return store_save_put(data, n);
}

/* Saving stops the song first: on the FM-1 an erase holds every
 * interrupt for its 50 ms, and the audio with it. */
int app_save(void)
{
    int rc, slot = app_slot;

    for (int s = 0; slot < 0 && s < APP_SONG_SLOTS; s++)
        if (!store_has(s))
            slot = s;
    if (slot < 0)
        return STORE_ERR_FULL;
    player_stop();
    ready = 0;
    rc = store_save_begin(slot, app_name);
    if (rc == STORE_OK && trk_write(&app_song, flash_sink, 0) < 0)
        rc = STORE_ERR_FULL;
    if (rc == STORE_OK)
        rc = store_save_commit();
    else
        store_mount(); /* what a failed save wrote is free space again */
    if (rc == STORE_OK) {
        app_slot = slot;
        app_dirty = false;
        remember(slot);
    }
    ready = 1;
    return rc;
}

int app_open_slot(int slot)
{
    static char piece[512];
    char name[STORE_NAME + 1];
    uint32_t bytes;
    int32_t n;
    TrkReport r;

    if (slot < 0 || slot >= APP_SONG_SLOTS || store_info(slot, name, &bytes) != STORE_OK ||
        store_load_begin(slot) != STORE_OK)
        return STORE_ERR_NONE;
    ready = 0; /* the working copy is the delay's memory */
    trk_begin(&r);
    while ((n = store_load_get(piece, sizeof piece)) > 0)
        trk_feed(piece, (uint32_t)n);
    if (n < 0 || trk_end(&app_song) != TRK_OK) {
        ready = 1;
        return STORE_ERR_CORRUPT; /* the song in memory is untouched */
    }
    report = r;
    set_name(name);
    remember(slot);
    adopt(slot, false);
    return STORE_OK;
}

void app_new(void)
{
    char name[STORE_NAME + 1] = "SONG 01", other[STORE_NAME + 1];
    uint32_t bytes;

    /* The first SONG nn no song in flash carries. */
    for (int n = 1; n < 100; n++) {
        bool taken = false;
        name[5] = (char)('0' + n / 10);
        name[6] = (char)('0' + n % 10);
        for (int s = 0; s < APP_SONG_SLOTS; s++)
            taken |= store_info(s, other, &bytes) == STORE_OK && same_name(other, name);
        if (!taken)
            break;
    }
    ready = 0;
    song_init(&app_song);
    for (uint32_t i = 0; i < sizeof report; i++)
        ((uint8_t *)&report)[i] = 0;
    /* One block on the first track, so there is somewhere to write. */
    app_song.arrangement[0].pattern[0] = 0;
    set_name(name);
    adopt(-1, false);
}

/* -------------------------------------------------------- the host --- */
/* A .trk handed over by the backend. It takes the place in flash of the
 * song of the same name, if there is one, and is not written until SAVE. */
int app_load(const char *trk, uint32_t len, const char *name)
{
    char other[STORE_NAME + 1];
    uint32_t bytes;
    int slot = -1;
    TrkReport r;

    ready = 0;
    if (trk_load(&app_song, trk, len, &r) != TRK_OK) {
        ready = 1;
        return 0;
    }
    report = r;
    if (name && name[0])
        set_name(name);
    for (int s = 0; s < APP_SONG_SLOTS; s++)
        if (store_info(s, other, &bytes) == STORE_OK && same_name(other, app_name))
            slot = s;
    adopt(slot, true);
    return 1;
}

void app_init(void)
{
    char last;
    int slot = -1;

    remote_init();
    store_mount();
    if (store_load_begin(LAST_SLOT) == STORE_OK && store_load_get(&last, 1) == 1)
        slot = last;
    for (int s = 0; !store_has(slot) && s < APP_SONG_SLOTS; s++)
        if (store_has(s))
            slot = s;
    if (app_open_slot(slot) == STORE_OK)
        return;
    /* An empty flash: the built-in song, which lives nowhere until saved. */
    ready = 0;
    trk_load(&app_song, demo_trk, sizeof(demo_trk) - 1, &report);
    set_name("DEMO");
    adopt(-1, false);
}

static bool link_sending;

/* A song is coming in from the computer, or going out to it: the screen
 * says so and the hands wait. Nothing to press -- pulling the cable is the way out, and
 * it leaves the flash as it was. */
static void draw_receiving(void)
{
    char a[8], b[8];
    int w = APP_INNER - 40, x, done = link_size() ? (int)((uint64_t)link_got() * w / link_size()) : 0;

    gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255);
    gfx_text_mid(F_COND14, 0, PLAT_W, 96, link_sending ? "SENDING" : "RECEIVING", C_TITLE);
    gfx_text_mid(F_COND12, 0, PLAT_W, 114, link_name(), C_TEXT);
    gfx_slab(APP_X0 + 20, 124, w, 6, C_TEXT, 26);
    gfx_slab(APP_X0 + 20, 124, done, 6, C_ACCENT, 255);
    ui_dec(a, (int)(link_got() / 1024));
    ui_dec(b, (int)((link_size() + 1023) / 1024));
    x = (PLAT_W - gfx_text_w(F_MONO8, a) - gfx_text_w(F_MONO8, b) - gfx_text_w(F_MONO8, " / ") -
         gfx_text_w(F_MONO8, " KB")) / 2;
    x = gfx_text(F_MONO8, x, 144, a, C_TEXT);
    x = gfx_text(F_MONO8, x, 144, " / ", C_DIM);
    x = gfx_text(F_MONO8, x, 144, b, C_TEXT);
    gfx_text(F_MONO8, x, 144, " KB", C_DIM);
}

/* Back from MIXER or PROJECT to the screen that opened it, as it was left. */
static void back_to(int from)
{
    screen = from;
    if (screen == AT_PATTERN)
        pattern_resume();
    else if (screen == AT_PHRASE)
        phrase_resume();
    else
        loop_clear(), songview_open(false); /* the loop was a phrase's */
}

void app_frame(const PlatInput *real)
{
    PlatInput mine = *real; /* the hands, and what the computer adds to them */
    const PlatInput *in = &mine;
    int entry, track, worst;

    remote_input(&mine);
    player_set_master(in->master);
    app_cpu = plat_audio_load(&worst);
    app_block = worst;
    if (worst > app_worst)
        app_worst = worst;

    int link_state;

    undo_frame(in->ms);
    switch (link_state = link_frame(in->ms, screen == AT_MENU)) {
    case LINK_SENDING:
    case LINK_RECEIVING:
        link_sending = link_state == LINK_SENDING;
        gfx_frame(draw_receiving);
        return;
    case LINK_DONE:
        /* it replaced the open song (which had no unsaved change, or it
         * would have been refused): what is in memory is the old one */
        if (link_slot() == app_slot)
            app_open_slot(app_slot);
        menu_open();
        break;
    default:
        break;
    }

    {   /* HOME leads home from every screen, whatever that screen was doing */
        static uint32_t prev_btns;
        bool home = (in->btns & ~prev_btns) >> PB_HOME & 1u;
        prev_btns = in->btns;
        if (home && screen != AT_MENU) {
            if (screen == AT_SONG)
                songview_cancel(); /* a block being previewed goes back to what it was */
            player_note_off();
            song_pool_compact(&app_song);
            screen = AT_MENU;
            loop_clear();
            menu_open();
        }
    }

    if (screen == AT_CANVAS) {
        if (!canvas_frame(in))
            leave_canvas();
    } else if (screen == AT_MIXER) {
        switch (mixer_frame(in)) {
        case MIXER_BACK:
            back_to(mixer_from);
            break;
        case MIXER_PROJECT: /* across, and GLO still goes back where both came from */
            screen = AT_PROJECT;
            project_from = mixer_from;
            project_open();
            break;
        }
    } else if (screen == AT_PROJECT) {
        switch (project_frame(in)) {
        case PROJECT_BACK:
            back_to(project_from);
            break;
        case PROJECT_MIXER:
            screen = AT_MIXER;
            mixer_from = project_from;
            mixer_open("PRJ", project_from == AT_PHRASE    ? phrase_entry()
                              : project_from == AT_PATTERN ? pattern_entry()
                                                           : songview_entry());
            break;
        }
    } else if (screen == AT_PHRASE) {
        switch (phrase_frame(in)) {
        case PHRASE_BACK:
            screen = AT_SONG;
            loop_clear(), songview_open(false); /* the loop was a phrase's */
            break;
        case PHRASE_PATTERN:
            if (pattern_open(&app_song, phrase_entry(), phrase_track())) {
                pattern_from_phrase(phrase_row());
                pattern_from = AT_PHRASE;
                screen = AT_PATTERN;
            }
            break;
        case PHRASE_MIXER:
            screen = AT_MIXER;
            mixer_from = AT_PHRASE;
            mixer_open("PHR", phrase_entry());
            break;
        default:
            break;
        }
    } else if (screen == AT_INSTRUMENT) {
        if (!instrument_frame(in)) {
            screen = AT_PATTERN;
            pattern_resume();
        }
    } else if (screen == AT_PATTERN) {
        switch (pattern_frame(in)) {
        case PATTERN_BACK: /* GLO: back where it was opened from */
            if (pattern_from == AT_PHRASE) {
                screen = AT_PHRASE;
                phrase_resume();
                break;
            }
            /* fallthrough */
        case PATTERN_SONG:
            screen = AT_SONG;
            loop_clear(), songview_open(false); /* the loop was a phrase's */
            break;
        case PATTERN_INSTR:
            screen = AT_INSTRUMENT;
            instrument_open(pattern_instr(), pattern_entry());
            break;
        default:
            break;
        }
    } else if (screen == AT_SONG) {
        switch (songview_frame(in, &entry, &track)) {
        case SONGVIEW_MIXER:
            screen = AT_MIXER;
            mixer_from = AT_SONG;
            mixer_open("SONG", songview_entry());
            break;
        case SONGVIEW_PROJECT:
            screen = AT_PROJECT;
            project_from = AT_SONG;
            project_open();
            break;
        case SONGVIEW_PATTERN:
            if (pattern_open(&app_song, entry, track)) {
                pattern_from = AT_SONG;
                screen = AT_PATTERN;
            }
            break;
        case SONGVIEW_PHRASE:
            screen = AT_PHRASE;
            phrase_open(entry, track);
            break;
        default:
            break;
        }
    } else if (menu_frame(in)) {
        screen = AT_SONG;
        songview_open(true);
    }
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
