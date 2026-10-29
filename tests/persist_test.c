/* SPDX-License-Identifier: GPL-3.0-only */
/* The whole road, through the app: boot on an empty flash, write a note
 * in PATTERN, SAVE, "switch off", boot again, and the note is there. */
#include <stdio.h>
#include <string.h>

#include "../core/trk.h"
#include "../platform/platform.h"

static uint8_t nor[PLAT_FLASH_SECTORS * PLAT_FLASH_SECTOR];
int plat_flash_read(uint32_t o, void *d, uint32_t n) { memcpy(d, nor + o, n); return 0; }
int plat_flash_erase(uint32_t s) { memset(nor + s * PLAT_FLASH_SECTOR, 0xFF, PLAT_FLASH_SECTOR); return 0; }
int plat_flash_prog(uint32_t o, const void *d, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        nor[o + i] &= ((const uint8_t *)d)[i];
    return 0;
}
void plat_blit(int x, int y, int w, int h, const uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
void plat_led_key(int k, int on) { (void)k; (void)on; }
void plat_led_btn(int b, int on) { (void)b; (void)on; }
void plat_led_glow(int b, int on) { (void)b; (void)on; }
void plat_led_level(int l, int v) { (void)l; (void)v; }
int plat_audio_load(int *worst) { *worst = -1; return -1; }
int plat_sysex_get(const uint8_t **p, uint32_t *n) { (void)p; (void)n; return 0; }
void plat_sysex_done(void) {}
int plat_sysex_send(const uint8_t *p, uint32_t n) { (void)p; (void)n; return -1; }

static PlatInput in;
static void go(void) { app_frame(&in); memset(in.enc, 0, sizeof in.enc); }
static void press(int b) { in.btns = 1u << b; go(); in.btns = 0; go(); }
/* HOME held: the root menu from anywhere (a press is a step back). */
static void home(void) { in.btns = 1u << PB_HOME; for (int i = 0; i < 40; i++) go(); in.btns = 0; go(); }

/* What is in slot 0, read the way the app reads it. */
#include "../core/store.h"
#include "../app/app.h"
#include "../app/ui.h"
static Song probe;
static int note_on_flash(void)
{
    char piece[300];
    int32_t n;
    if (store_mount() || store_load_begin(0))
        return -1;
    trk_begin(NULL);
    while ((n = store_load_get(piece, sizeof piece)) > 0)
        trk_feed(piece, (uint32_t)n);
    if (n < 0 || trk_end(&probe) != TRK_OK)
        return -2;
    return song_step(&probe, 0, probe.arrangement[0].pattern[0], 5)->notes[0];
}

int main(void)
{
    int before, after, again;

    memset(nor, 0xFF, sizeof nor);
    app_init();                 /* empty flash: the built-in song */
    go();
    before = note_on_flash();   /* nothing saved yet */
    press(PB_SEL);              /* HOME: open it, SONG */
    press(PB_EDIT);             /* PATTERN, phrase 0 track 1 */
    in.enc[PE_K1] = 5;          /* KNOB 1: the column kept (SELECT would go to the number) */
    go();
    in.keys = 1u << 16;         /* A4 on step 05 */
    go();
    in.keys = 0;
    go();
    press(PB_SAVE);
    after = note_on_flash();

    app_init();                 /* the machine switched off and on */
    go();
    press(PB_SEL);
    press(PB_EDIT);
    press(PB_SAVE);             /* saved again, untouched: still there */
    again = note_on_flash();

    /* SETTINGS: from HOME's root menu, KNOB 3 the value, kept across a restart */
    home();
    in.enc[PE_K1] = 2;          /* CONTINUE, PROJECTS, SETTINGS */
    go();
    press(PB_SEL);
    {
        bool there = !strcmp(app_screen(), "SETTINGS"), kept;
        in.enc[PE_K3] = -1;     /* LEDS: GUIDE -> STATE */
        go();
        in.enc[PE_K1] = 1;      /* LIGHT */
        go();
        in.enc[PE_K3] = 2;      /* 50 -> 75 % */
        go();
        press(PB_HOME);         /* a step back: HOME's root */
        there = there && !strcmp(app_screen(), "HOME");
        ui_leds = UI_LEDS_GUIDE, ui_light = PLAT_LED_50;
        app_init();             /* off and on */
        kept = ui_leds == UI_LEDS_STATE && ui_light == PLAT_LED_75 && !strcmp(app_name, "DEMO");
        printf("persist test: before %d, after SAVE %d, after a restart and a second SAVE %d; settings %s -> %s\n",
               before, after, again, there && kept ? "kept" : "LOST",
               before < 0 && after == 69 && again == 69 && there && kept ? "ok" : "FAIL");
        return before < 0 && after == 69 && again == 69 && there && kept ? 0 : 1;
    }
}
