/* SPDX-License-Identifier: GPL-3.0-only */
#include "loop.h"

#include "../core/player.h"

static int state, of_entry, a, b;

void loop_clear(void)
{
    state = LOOP_NONE;
    player_set_loop(0, 0);
}

void loop_keep(int entry)
{
    if (state != LOOP_NONE && entry != of_entry)
        loop_clear();
}

void loop_press(int entry, int row)
{
    loop_keep(entry);
    if (state == LOOP_NONE) {
        of_entry = entry;
        a = b = row;
        state = LOOP_ONE_END;
    } else if (state == LOOP_ONE_END) {
        if (row < a) { /* the end set above the start: they trade places */
            b = a;
            a = row;
        } else {
            b = row;
        }
        state = LOOP_SET;
        player_set_loop(a, b - a + 1);
    } else {
        loop_clear();
    }
}

int loop_state(void) { return state; }
int loop_from(void) { return a; }
int loop_to(void) { return b; }
bool loop_has(int row) { return state != LOOP_NONE && row >= a && row <= b; }
