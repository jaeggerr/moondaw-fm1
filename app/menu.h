/* SPDX-License-Identifier: GPL-3.0-only */
/* HOME, where the machine opens and where the HOME button leads from
 * anywhere: a root menu (29/10) -- CONTINUE the open project, PROJECTS
 * the list by name with the row that makes a new one, SETTINGS, HELP. */
#ifndef MENU_H
#define MENU_H

#include <stdbool.h>

#include "../platform/platform.h"

/* The root's items, and where its cursor starts. */
enum { MENU_CONTINUE, MENU_PROJECTS, MENU_SETTINGS, MENU_SYSTEM, MENU_HELP, MENU_ITEMS };
void menu_open(int item);
/* The projects read again, the hand where it is: another song was loaded
 * from HOME itself. */
void menu_reload(void);
/* One frame. MENU_SONG: SEL opened a project (now the song in memory), or
 * CONTINUE the open one -- the caller goes into it; MENU_TO_SETTINGS. */
enum { MENU_STAY, MENU_SONG, MENU_TO_SETTINGS, MENU_TO_SYSTEM, MENU_TO_HELP, MENU_BACK };
int menu_frame(const PlatInput *in);
/* The root was reached with the song changed: ask before putting it down.
 * MENU_BACK is the answer that sends the hand back into the song. */
void menu_ask_home(void);
/* In PROJECTS' list, no panel or question open: where EDIT does something (the guide). */
bool menu_asking(void); /* the "not saved" question is up */
bool menu_in_list(void);

#endif
