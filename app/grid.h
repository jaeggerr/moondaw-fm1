/* SPDX-License-Identifier: GPL-3.0-only */
/* The grid every screen of the road is drawn as, since 28/10 (NOTES.md):
 * SONG, PHRASE, PATTERN. CONDENSED 20 on rows of 28 px, eight rows
 * under a line of small column names; the row's number fixed at the left,
 * the columns to the right of a rule; the cursor's row a green band, the
 * cursor's cell hollowed out of it.
 *
 * One grammar goes with it: SELECT the row, KNOB 2 the column (left of
 * the first one, the row's number), KNOB 3 the value under the cursor,
 * SEL one level down, GLO one level up, HOME home. And KNOB 1 the row as
 * well (29/10): KNOB 1 and 2 move -- the row first, every screen has rows,
 * only the grids have columns -- KNOB 3 and 4 change, the right hand alone
 * moving the cursor; SELECT stays, for long runs. */
#ifndef GRID_H
#define GRID_H

#include <stdbool.h>
#include <stdint.h>

enum {
    GRID_ROW = 28, GRID_ROWS = 8, GRID_HEAD = 16,
    GRID_NUM_X = 14,  /* the row's number */
    GRID_CELL_X = 50, /* the first column */
    GRID_RIGHT = 234, /* columns end here: the scroll bar is to the right */
};

void grid_clear(void);
int grid_y(int vis);    /* top of visible row `vis` */
int grid_base(int vis); /* its baseline */

/* A column's name, small, in the line above the rows; white when the
 * cursor is in that column. */
void grid_head(int x, int w, const char *s, bool on);
/* A track's number as a column's name: green while it plays alone (solo),
 * red while it is muted. */
void grid_track_head(int x, int w, int track, bool on);
/* Whether a track is heard: not muted, and no other alone. Its cells grey
 * when it is not. */
bool grid_heard(int track);
/* The small words at the top left, over the numbers: where one is, or
 * what just happened (SAVED, POOL FULL). */
void grid_corner(const char *a, uint16_t ca, const char *b, uint16_t cb);
/* The colour of a word said there: SAVED green, UNDO / REDO amber, the end
 * of the journal grey, a failure red. */
uint16_t grid_said_ink(const char *s);

/* The cursor's row: the green band, `w` wide from the left edge. */
void grid_band(int vis, int w);
/* The row's number; `on` when the cursor is on it (hollowed), `ink` its colour otherwise. */
void grid_num(int vis, int n, bool band, bool on, uint16_t ink);
/* A cell's text, centred in [x, x + w); `on` when the cursor is on it. */
void grid_cell(int vis, int x, int w, const char *s, bool band, bool on, uint16_t ink);
/* The colour a cell's text takes: on the band, hollowed, or as it is. */
uint16_t grid_ink(bool band, bool on, uint16_t ink);
void grid_hollow(int vis, int x, int w);
/* The triangle of what plays, left of the number. */
void grid_playing(int vis, bool band);
/* The rule between the numbers and the columns, for one row: a screen
 * whose rows are not all alike draws it row by row and leaves it out
 * where it would cross something (SONG's section markers). */
void grid_rule(int vis);
/* The scroll bar, when `n` rows do not fit. */
void grid_bar(int top, int n);
/* Both, for the screens whose rows are all alike. */
void grid_frame(int top, int n);

/* A play sign, 10 x 12 from its top left corner: drawn round, filled while it plays. */
void grid_play_sign(int x, int y, uint16_t c, bool filled);
/* While a song plays and the hand is still: *first so that row `play` is
 * shown, a page at a time (the playhead at the top of the new page). The
 * hand moving gives the view back to the cursor (grid_follow). */
enum { GRID_FOLLOW_IDLE = 90 }; /* frames the hand is still before the view follows: 1.5 s */
void grid_follow_play(int *first, int play, int n, int shown);
/* Keeps *cur in [0, n) and *first so that *cur is one of `shown`. */
void grid_follow(int *cur, int *first, int n, int shown);
/* As hex, two digits. */
char *grid_hex2(char *b, int v);

#endif
