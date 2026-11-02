/* SPDX-License-Identifier: GPL-3.0-only */
#include "grid.h"

#include "../core/player.h"
#include "undo.h"

#include "../platform/platform.h"
#include "theme.h"

void grid_clear(void) { gfx_fill(0, 0, PLAT_W, PLAT_H, C_BG, 255); }
int grid_y(int vis) { return GRID_HEAD + vis * GRID_ROW; }
int grid_base(int vis) { return grid_y(vis) + 2 + FONTS[F_COND20].ascent; }

void grid_head(int x, int w, const char *s, bool on)
{
    gfx_text_mid(F_COND12, x, w, GRID_HEAD - 3, s, on ? C_TEXT : C_DIM);
}

/* What a track makes you hear, worked out of the song itself: the
 * instrument its steps name most often, counted over the blocks the
 * arrangement actually gives it. A track has no instrument in the model
 * -- the step does (core/song.h) -- and a name assigned by hand would be
 * a second truth to keep, lying the moment the content changes, so there
 * is none: the header reads the content.
 *
 * Only the blocks the arrangement names are counted. A block written and
 * not arranged is not heard, and must not name the column.
 *
 * Worked out once and kept, because the screen is drawn whole every
 * frame and this walks the pool: eight tracks of sixteen blocks of a
 * hundred and twenty-eight steps. It is thrown away when the song is
 * written to at all (undo_edits), which covers every edit, every undo,
 * and a song loaded in its place. */
static const Song *head_song;
static uint32_t head_edits;
static uint8_t head_instr[SONG_MAX_TRACKS];

static void head_build(const Song *s)
{
    bool used[SONG_MAX_TRACKS][TRACK_MAX_PATTERNS];
    for (int t = 0; t < SONG_MAX_TRACKS; t++)
        for (int p = 0; p < TRACK_MAX_PATTERNS; p++)
            used[t][p] = false;
    for (int i = 0; i < s->arrange_len; i++)
        for (int t = 0; t < SONG_MAX_TRACKS; t++) {
            uint8_t p = s->arrangement[i].pattern[t];
            if (p < TRACK_MAX_PATTERNS)
                used[t][p] = true;
        }
    for (int t = 0; t < SONG_MAX_TRACKS; t++) {
        uint16_t n[SONG_MAX_INSTR];
        int best = -1;
        for (int i = 0; i < SONG_MAX_INSTR; i++)
            n[i] = 0;
        for (int p = 0; p < TRACK_MAX_PATTERNS; p++) {
            if (!used[t][p])
                continue;
            for (int r = 0; r < s->patterns[t][p].length; r++) {
                uint8_t in = song_step(s, t, p, r)->instrument;
                if (in < SONG_MAX_INSTR && n[in] < 0xFFFF)
                    n[in]++;
            }
        }
        for (int i = 0; i < SONG_MAX_INSTR; i++)
            if (n[i] && (best < 0 || n[i] > n[best]))
                best = i;
        head_instr[t] = best < 0 ? INSTR_NONE : (uint8_t)best;
    }
    head_song = s;
    head_edits = undo_edits();
}

const char *grid_track_instr(const Song *s, int track)
{
    uint8_t i;
    if (s != head_song || undo_edits() != head_edits)
        head_build(s);
    i = head_instr[track];
    return i == INSTR_NONE || !s->instruments[i].name[0] ? 0 : s->instruments[i].name;
}

void grid_track_head(int x, int w, const Song *s, int track, bool on)
{
    char num[3] = { (char)('1' + track), 0, 0 }, cut[INSTR_NAME_LEN];
    const char *name = grid_track_instr(s, track);
    uint16_t ink = player_soloed(track) ? C_ACCENT : player_muted(track) ? C_MUTE : on ? C_TEXT : C_DIM;
    /* The column is 31 px and the head 16, so a name of seven letters
     * does not fit at this size and the tail goes. The tail is the one
     * to lose: it is the variant, not the family -- CHOR can only be
     * CHORD, and the 909 of SNR909 is the kit, not the drum. A smaller
     * face would hold the whole word, and was weighed on the panel on
     * 01/11: under a grid set in 20 the line goes thin and reads worse
     * than a word cut. */
    if (name) {
        int k = 0;
        while (name[k] && k < INSTR_NAME_LEN - 1) {
            cut[k] = name[k], cut[k + 1] = 0;
            if (gfx_text_w(F_COND12, cut) > w - 2) {
                cut[k] = 0;
                break;
            }
            k++;
        }
        name = cut[0] ? cut : 0;
    }
    gfx_text_mid(F_COND12, x, w, GRID_HEAD - 3, name ? name : num, ink);
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

/* PATTERN's FX and INS views put the notes off the page, and a row's
 * effects -- or its instrument -- read very differently depending on
 * whether anything is struck on it. The number is the only thing left of
 * the step there, so it carries the answer: a rule under the digits, one
 * px under the baseline, in the ink the digits already have -- not a
 * colour of its own, which would read as a fourth kind of row. */
void grid_num_rule(int vis, int n, bool band, bool on, uint16_t ink)
{
    char s[3];
    gfx_fill(GRID_NUM_X, grid_base(vis) + 1, gfx_text_w(F_COND20, grid_hex2(s, n)), 1, grid_ink(band, on, ink), 255);
}

void grid_cell(int vis, int x, int w, const char *s, bool band, bool on, uint16_t ink)
{
    if (on)
        grid_hollow(vis, x, w);
    gfx_text_mid(F_COND20, x, w, grid_base(vis), s, grid_ink(band, on, ink));
}

void grid_playing(int vis, bool band)
{
    /* Amber, not cyan, while the listening speed is below 100 % (02/11):
     * what plays says that it is not at the song's tempo. The corner has
     * no room left on any of the three screens, and a slowed song nobody
     * is told about is a song that sounds wrong for no reason. */
    bool slow = player_speed() < 100;
    int y = grid_y(vis) + 8;
    uint16_t ink = band ? (slow ? C_SEL : C_ON_ACC) : slow ? C_FX : C_TITLE;
    for (int k = 0; k < 6; k++)
        gfx_fill(4 + k, y + k, 1, 12 - 2 * k, ink, 255);
}

void grid_rule(int vis) { gfx_fill(GRID_CELL_X - 3, grid_y(vis), 1, GRID_ROW, C_DIM, 255); }

void grid_bar_rows(int top, int n, int rows)
{
    if (n > rows) {
        int h = rows * GRID_ROW * rows / n, y = GRID_HEAD + rows * GRID_ROW * top / n;
        gfx_fill(PLAT_W - 4, GRID_HEAD, 2, rows * GRID_ROW, C_TRACK, 255);
        gfx_fill(PLAT_W - 4, y, 2, h < 4 ? 4 : h, C_DIM, 255);
    }
}

void grid_bar(int top, int n) { grid_bar_rows(top, n, GRID_ROWS); }

void grid_frame(int top, int n)
{
    int rows = n < GRID_ROWS ? n : GRID_ROWS;
    for (int v = 0; v < rows; v++)
        grid_rule(v);
    grid_bar(top, n);
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
