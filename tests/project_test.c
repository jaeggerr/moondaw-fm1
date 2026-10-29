/* SPDX-License-Identifier: GPL-3.0-only */
/* PROJECT's hands without a screen: each setting under KNOB 2, its stops,
 * the name typed letter by letter, and UNDO. */
#include <stdio.h>
#include <string.h>

#include "../app/app.h"
#include "../app/project.h"
#include "../app/undo.h"
#include "../core/player.h"
#include "../core/trk.h"

Song app_song;
char app_name[STORE_NAME + 1] = "KIDS";
int app_slot = -1;
bool app_dirty;
static int saves;
void app_mark_dirty(void) { app_dirty = true; }
int app_save(void) { return saves++, 0; }
int app_not_carried(void) { return 0; }

void plat_blit(int x, int y, int w, int h, const uint16_t *px) { (void)x; (void)y; (void)w; (void)h; (void)px; }
void plat_led_btn(int b, int on) { (void)b; (void)on; }
void plat_led_glow(int b, int on) { (void)b; (void)on; }

static const char trk[] = "version 3\nbpm 120\nticks 6\nmeter 4 4\ntracks 8\n"
                          "pat t0 00 len 16\n00 C-4\narr 00 steps auto\nt0 00\n";

static PlatInput in;
static int fails, last;
static void go(void) { undo_frame(in.ms); last = project_frame(&in); memset(in.enc, 0, sizeof in.enc); in.ms += 2000; }
static void enc(int e, int d) { in.enc[e] = (int8_t)d; go(); }
static int press(int b) { int r; in.btns = 1u << b; go(); r = last; in.btns = 0; go(); return r; }
#define CHECK(what, cond) do { if (!(cond)) { printf("FAIL  %s\n", what); fails++; } } while (0)
#define S app_song

int main(void)
{
    trk_load(&app_song, trk, sizeof trk - 1, NULL);
    player_init(&app_song);
    undo_reset(&app_song);
    project_open();
    go();

    enc(PE_SELECT, 1);
    enc(PE_K2, 20);
    CHECK("BPM", S.bpm == 140 && app_dirty);
    enc(PE_SELECT, 1);
    enc(PE_K2, 100);
    CHECK("SWING stops at 75", S.swing == SWING_MAX);
    enc(PE_SELECT, 1);
    enc(PE_K2, -1);
    CHECK("METER: beats in a bar, 3 for a waltz", S.beats_per_bar == 3);
    enc(PE_SELECT, 1);
    enc(PE_K2, -1);
    CHECK("STEP: steps a beat, 3 for triplets", S.steps_per_beat == 3);
    enc(PE_SELECT, 1);
    enc(PE_K2, 100);
    CHECK("TICKS stops at its most", S.ticks_per_step == TICKS_MAX);
    enc(PE_SELECT, 1);
    enc(PE_K2, -100);
    CHECK("TRACKS stops at one", S.num_tracks == 1);
    enc(PE_ALGO, -1);
    CHECK("ALGO to the left takes the last setting back", S.num_tracks == 8);
    enc(PE_ALGO, 1);
    CHECK("and to the right does it again", S.num_tracks == 1);
    enc(PE_ALGO, 1);
    CHECK("past the last, nothing", S.num_tracks == 1);
    enc(PE_ALGO, -1);
    CHECK("and back", S.num_tracks == 8);

    enc(PE_SELECT, -10);       /* NAME */
    press(PB_SEL);
    enc(PE_K1, 4);
    enc(PE_K2, 2);             /* space -> A -> B */
    CHECK("GLO leaves the name as it was", press(PB_GLO) && !strcmp(app_name, "KIDS"));
    press(PB_SEL);
    enc(PE_K1, 4);
    enc(PE_K2, 2);
    press(PB_SEL);
    CHECK("SEL keeps it", !strcmp(app_name, "KIDSB"));
    press(PB_SEL);
    enc(PE_K1, 6);
    enc(PE_K2, 1);             /* an A two places past the end: the gap becomes spaces */
    press(PB_SEL);
    CHECK("a letter past the end pulls spaces in", !strcmp(app_name, "KIDSB A"));
    press(PB_SEL);
    enc(PE_K1, 6);
    enc(PE_K2, -1);            /* the A back to a space: trailing spaces are not kept */
    press(PB_SEL);
    CHECK("the spaces at the end are not kept", !strcmp(app_name, "KIDSB"));

    press(PB_SAVE);
    CHECK("SAVE saves", saves == 1);
    CHECK("FX goes to the MIXER", press(PB_FX) == PROJECT_MIXER);
    CHECK("GLO leaves", press(PB_GLO) == PROJECT_BACK);
    printf("project test: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
