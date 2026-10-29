/* SPDX-License-Identifier: GPL-3.0-only */
#include "grid.h"

#include "../core/player.h"

#include "../platform/platform.h"
#include "theme.h"

void grid_clear(void) { gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255); }
int grid_y(int vis) { return GRID_HEAD + vis * GRID_ROW; }
int grid_base(int vis) { return grid_y(vis) + 2 + FONTS[F_COND20].ascent; }

void grid_head(int x, int w, const char *s, bool on)
{
    gfx_text_mid(F_COND12, x, w, GRID_HEAD - 3, s, on ? C_TEXT : C_DIM);
}

void grid_track_head(int x, int w, int track, bool on)
{
    char s[3] = { (char)('1' + track), 0, 0 };
    gfx_text_mid(F_COND12, x, w, GRID_HEAD - 3, s,
                 player_soloed(track) ? C_ACCENT : player_muted(track) ? C_MUTE : on ? C_TEXT : C_DIM);
}

bool grid_heard(int track)
{
    bool any = false;
    for (int t = 0; t < SONG_MAX_TRACKS; t++)
        any |= player_soloed(t);
    return !player_muted(track) && (!any || player_soloed(track));
}

void grid_corner(const char *a, uint16_t ca, const char *b, uint16_t cb)
{
    int x = gfx_text(F_COND12, 4, GRID_HEAD - 3, a, ca);
    if (b)
        gfx_text(F_COND12, x + 4, GRID_HEAD - 3, b, cb);
}

uint16_t grid_said_ink(const char *s)
{
    return s[0] == 'S' ? C_ACCENT : s[0] == 'U' || s[0] == 'R' ? C_FX : s[0] == 'N' && s[1] == 'O' && s[2] == ' ' ? C_DIM : C_MUTE;
}

void grid_band(int vis, int w)
{
    gfx_slab(2, grid_y(vis) + 1, w, GRID_ROW - 2, C_ACCENT, 255);
}

uint16_t grid_ink(bool band, bool on, uint16_t ink)
{
    return on ? C_ACCENT : band ? C_ON_ACC : ink;
}

void grid_hollow(int vis, int x, int w)
{
    gfx_slab(x + 1, grid_y(vis) + 3, w - 2, GRID_ROW - 6, C_BG, 255);
}

void grid_num(int vis, int n, bool band, bool on, uint16_t ink)
{
    char s[3];
    if (on)
        grid_hollow(vis, GRID_NUM_X - 7, GRID_CELL_X - GRID_NUM_X + 3);
    gfx_text(F_COND20, GRID_NUM_X, grid_base(vis), grid_hex2(s, n), grid_ink(band, on, ink));
}

void grid_cell(int vis, int x, int w, const char *s, bool band, bool on, uint16_t ink)
{
    if (on)
        grid_hollow(vis, x, w);
    gfx_text_mid(F_COND20, x, w, grid_base(vis), s, grid_ink(band, on, ink));
}

void grid_playing(int vis, bool band)
{
    int y = grid_y(vis) + 8;
    for (int k = 0; k < 6; k++)
        gfx_fill(4 + k, y + k, 1, 12 - 2 * k, band ? C_ON_ACC : C_TITLE, 255);
}

void grid_frame(int top, int n)
{
    int rows = n < GRID_ROWS ? n : GRID_ROWS;
    gfx_fill(GRID_CELL_X - 3, GRID_HEAD, 1, rows * GRID_ROW, C_DIM, 255);
    if (n > GRID_ROWS) {
        int h = GRID_ROWS * GRID_ROW * GRID_ROWS / n, y = GRID_HEAD + GRID_ROWS * GRID_ROW * top / n;
        gfx_fill(PLAT_W - 4, GRID_HEAD, 2, GRID_ROWS * GRID_ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, h < 4 ? 4 : h, C_DIM, 255);
    }
}

void grid_follow(int *cur, int *first, int n, int shown)
{
    *cur = *cur >= n ? n - 1 : *cur;
    *cur = *cur < 0 ? 0 : *cur;
    if (*cur < *first)
        *first = *cur;
    if (*cur >= *first + shown)
        *first = *cur - shown + 1;
    if (*first > (n > shown ? n - shown : 0))
        *first = n > shown ? n - shown : 0;
    if (*first < 0)
        *first = 0;
}

char *grid_hex2(char *b, int v)
{
    b[0] = "0123456789ABCDEF"[v >> 4 & 15];
    b[1] = "0123456789ABCDEF"[v & 15];
    b[2] = 0;
    return b;
}

void grid_play_sign(int x, int y, uint16_t c, bool filled)
{
    if (!filled) {
        gfx_line(x, y, x, y + 11, c);
        gfx_line(x, y, x + 9, y + 6, c);
        gfx_line(x, y + 11, x + 9, y + 5, c);
        return;
    }
    for (int i = 0; i < 10; i++) {
        int h = 12 - 12 * i / 10;
        gfx_fill(x + i, y + (12 - h) / 2, 1, h, c, 255);
    }
}

void grid_follow_play(int *first, int play, int n, int shown)
{
    if (play < 0 || (play >= *first && play < *first + shown))
        return;
    *first = play;
    if (*first > (n > shown ? n - shown : 0))
        *first = n > shown ? n - shown : 0;
}
