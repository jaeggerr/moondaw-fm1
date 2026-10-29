/* SPDX-License-Identifier: GPL-3.0-only */
/* HOME and several songs, through the app and a simulated flash: save,
 * new, open with SEL, the question about changes, HOME from anywhere,
 * and what the machine comes back on. */
#include <stdio.h>
#include <string.h>

#include "../app/app.h"

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
static int fails;
static void go(void) { app_frame(&in); memset(in.enc, 0, sizeof in.enc); }
static void press(int b) { in.btns = 1u << b; go(); in.btns = 0; go(); }
/* HOME held: the root menu from anywhere (a press is a step back). */
static void home(void) { in.btns = 1u << PB_HOME; for (int i = 0; i < 40; i++) go(); in.btns = 0; go(); }
static void sel(int d) { in.enc[PE_SELECT] = (int8_t)d; go(); }
static void key(int k) { in.keys = 1u << k; go(); in.keys = 0; go(); }
/* HOME's root -> PROJECTS, the list; its cursor on the open project. */
static void projects(void) { sel(-10); sel(1); press(PB_SEL); }
/* HOME -> SONG on the open project, then the block of its first cell. */
static void into(void) { press(PB_SEL); press(PB_EDIT); go(); }
static int songs_in_flash(void)
{
    int n = 0;
    for (int s = 0; s < APP_SONG_SLOTS; s++)
        n += store_has(s);
    return n;
}
#define CHECK(what, cond) do { if (!(cond)) { printf("FAIL  %s\n", what); fails++; } } while (0)
int main(void)
{
    memset(nor, 0xFF, sizeof nor);
    app_init();
    go();
    CHECK("an empty flash opens on the built-in song, which lives nowhere",
          !strcmp(app_name, "DEMO") && app_slot == -1 && songs_in_flash() == 0);

    press(PB_SEL);            /* the cursor starts on it: into SONG */
    press(PB_SAVE);
    home();
    CHECK("SAVE gives it a place", app_slot >= 0 && songs_in_flash() == 1 && !app_dirty);

    projects();
    sel(100);                 /* the last row: + NEW */
    press(PB_SEL);
    CHECK("NEW makes SONG 01, in memory only",
          !strcmp(app_name, "SONG 01") && app_slot == -1 && songs_in_flash() == 1);
    home();
    press(PB_SEL);
    CHECK("an unsaved new song has its row, and SEL on it goes back in",
          !strcmp(app_name, "SONG 01") && app_slot == -1);
    press(PB_EDIT);           /* PATTERN opens at EDIT's release: a frame for it */
    go();
    key(16);                  /* A4 on its first step */
    CHECK("writing marks the song as changed", app_dirty);
    press(PB_SAVE);
    home();           /* from PATTERN, straight home */
    CHECK("a second song beside the first", songs_in_flash() == 2 && !app_dirty);

    projects();
    sel(-1);                  /* rows by name: DEMO, SONG 01, + NEW; the cursor was on SONG 01 */
    press(PB_SEL);
    CHECK("SEL on another row opens it", !strcmp(app_name, "DEMO"));

    press(PB_EDIT);           /* PATTERN opens at EDIT's release: a frame for it */
    go();
    key(3);
    home();
    projects();
    sel(1);
    press(PB_SEL);
    CHECK("opening another song over changes asks first", !strcmp(app_name, "DEMO") && app_dirty);
    sel(1);                   /* anything else: stay */
    CHECK("and anything but an answer is stay", !strcmp(app_name, "DEMO") && app_dirty);
    press(PB_SEL);            /* the cursor stayed on SONG 01: asked again */
    press(PB_SEL);            /* SEL again: lose the changes */
    CHECK("SEL answers: open it, the changes gone",
          !strcmp(app_name, "SONG 01") && !app_dirty &&
          song_step(&app_song, 0, 0, 0)->notes[0] == 69);

    press(PB_EDIT);           /* PATTERN opens at EDIT's release: a frame for it */
    go();
    key(20);
    home();
    projects();
    sel(-1);
    press(PB_SEL);
    press(PB_SAVE);           /* SAVE answers the other way */
    CHECK("SAVE writes the changes, then opens", !strcmp(app_name, "DEMO") && songs_in_flash() == 2);
    home();
    projects();
    sel(1);
    press(PB_SEL);
    CHECK("and they were written", song_step(&app_song, 0, 0, 0)->notes[0] == 73);

    home();
    projects();
    sel(-1);                  /* DEMO */
    press(PB_PLAY);
    CHECK("PLAY on another project opens it, to play it", !strcmp(app_name, "DEMO") && !app_dirty);
    sel(1);                   /* still in PROJECTS: the next row is SONG 01 (on the root it would be SETTINGS) */
    press(PB_SEL);
    CHECK("and the hand stays in PROJECTS", !strcmp(app_name, "SONG 01"));
    home();
    projects();
    sel(-1);
    press(PB_PLAY);           /* DEMO again, playing, the hand on it */
    press(PB_SEQ);            /* still HOME: SEQ does nothing here */
    CHECK("and the hand stays on HOME", !strcmp(app_name, "DEMO"));
    press(PB_SEL);
    press(PB_EDIT);           /* PATTERN opens at EDIT's release: a frame for it */
    go();
    key(5);
    home();
    projects();
    sel(1);
    press(PB_PLAY);
    CHECK("over unsaved changes PLAY asks first", !strcmp(app_name, "DEMO") && app_dirty);
    press(PB_SEL);
    CHECK("SEL answers: the other one is opened, to play", !strcmp(app_name, "SONG 01") && !app_dirty);

    /* EDIT on a project: RENAME, DUPLICATE, DELETE */
    home();
    projects();               /* the cursor on the open one, SONG 01 */
    {
        int n = songs_in_flash();
        press(PB_EDIT);
        sel(1);               /* DUPLICATE */
        press(PB_SEL);
        press(PB_SEL);        /* the name it offers, SONG 01 2, kept */
        CHECK("DUPLICATE writes a copy, the open one now", songs_in_flash() == n + 1 && !strcmp(app_name, "SONG 01 2"));
        press(PB_EDIT);
        sel(2);               /* DELETE */
        press(PB_SEL);
        press(PB_HOME);       /* asked: kept */
        CHECK("DELETE asks first, HOME keeps it", songs_in_flash() == n + 1);
        press(PB_SEL);
        press(PB_SEL);        /* yes */
        CHECK("and SEL deletes it, the song kept in memory", songs_in_flash() == n && app_slot == -1);
        sel(-10);             /* the panel closed by the delete: the list */
        sel(1);               /* SONG 01 again: asked over the deleted copy, SEL lets it go */
        press(PB_SEL);
        press(PB_SEL);
        CHECK("and SONG 01 opens again", !strcmp(app_name, "SONG 01") && app_slot >= 0);
    }

    home();                   /* HELP, from the root: and back */
    sel(-10);
    sel(3);
    press(PB_SEL);
    CHECK("HELP opens from the root", !strcmp(app_screen(), "HELP"));
    press(PB_SEL);            /* a page */
    sel(100);                 /* scrolled to its end */
    press(PB_HOME);           /* the list */
    press(PB_HOME);           /* the root */
    CHECK("and HOME goes back to the root, through the list", !strcmp(app_screen(), "HOME"));

    app_init();               /* off and on */
    go();
    CHECK("the machine comes back on the song that was open",
          !strcmp(app_name, "SONG 01") && app_slot >= 0);

    printf("menu test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
