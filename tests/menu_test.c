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
int plat_audio_load(int *worst) { *worst = -1; return -1; }
int plat_sysex_get(const uint8_t **p, uint32_t *n) { (void)p; (void)n; return 0; }
void plat_sysex_done(void) {}
int plat_sysex_send(const uint8_t *p, uint32_t n) { (void)p; (void)n; return -1; }

static PlatInput in;
static int fails;
static void go(void) { app_frame(&in); memset(in.enc, 0, sizeof in.enc); }
static void press(int b) { in.btns = 1u << b; go(); in.btns = 0; go(); }
static void sel(int d) { in.enc[PE_SELECT] = (int8_t)d; go(); }
static void key(int k) { in.keys = 1u << k; go(); in.keys = 0; go(); }
/* HOME -> SONG on the open project, then the block of its first cell. */
static void into(void) { press(PB_SEL); press(PB_SEQ); }
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
    press(PB_HOME);
    CHECK("SAVE gives it a place", app_slot >= 0 && songs_in_flash() == 1 && !app_dirty);

    sel(100);                 /* the last row: + NEW */
    press(PB_SEL);
    CHECK("NEW makes SONG 01, in memory only",
          !strcmp(app_name, "SONG 01") && app_slot == -1 && songs_in_flash() == 1);
    press(PB_HOME);
    press(PB_SEL);
    CHECK("an unsaved new song has its row, and SEL on it goes back in",
          !strcmp(app_name, "SONG 01") && app_slot == -1);
    press(PB_SEQ);
    key(16);                  /* A4 on its first step */
    CHECK("writing marks the song as changed", app_dirty);
    press(PB_SAVE);
    press(PB_HOME);           /* from PATTERN, straight home */
    CHECK("a second song beside the first", songs_in_flash() == 2 && !app_dirty);

    sel(-1);                  /* rows by name: DEMO, SONG 01, + NEW; the cursor was on SONG 01 */
    press(PB_SEL);
    CHECK("SEL on another row opens it", !strcmp(app_name, "DEMO"));

    press(PB_SEQ);
    key(3);
    press(PB_HOME);
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

    press(PB_SEQ);
    key(20);
    press(PB_HOME);
    sel(-1);
    press(PB_SEL);
    press(PB_SAVE);           /* SAVE answers the other way */
    CHECK("SAVE writes the changes, then opens", !strcmp(app_name, "DEMO") && songs_in_flash() == 2);
    press(PB_HOME);
    sel(1);
    press(PB_SEL);
    CHECK("and they were written", song_step(&app_song, 0, 0, 0)->notes[0] == 73);

    app_init();               /* off and on */
    go();
    CHECK("the machine comes back on the song that was open",
          !strcmp(app_name, "SONG 01") && app_slot >= 0);

    printf("menu test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
