/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ROOM_H
#define ROOM_H

#include <stdbool.h>

#include "../platform/platform.h"

/* FLASH FULL, over whatever screen SAVE was pressed on (01/11): a corner can
 * be missed, and whoever missed it turns the machine off with the work gone.
 * A save writes the new file beside the old, so a song can stop fitting beside
 * itself; this is the way out that keeps the changes.
 *
 *   the window  what happened, in full      SEL  the projects   HOME  leaves, not saved
 *   the list    the other projects and what each takes
 *               SELECT, KNOB 1  the project   SEL  delete it    HOME  the window
 *   the question  are you sure              SEL  delete it      HOME  the list
 *
 * Deleted, back to the list for another, until the song fits: then it is saved
 * at once, SAVE having been asked for already, and the window says so. The open
 * project is not listed: deleting its old file to make room for the new one would
 * give up what a save promises, and the sample bank is not a project. */

/* Called by app_save when the flash is full. Open already: nothing. */
void room_open(void);
/* One frame. True while it holds the screen, so the screen under it runs not at
 * all; it lets go once the hand is off everything, so that the press that closed
 * it is not read as a press by the screen under it. */
bool room_frame(const PlatInput *in);
bool room_active(void);

#endif
