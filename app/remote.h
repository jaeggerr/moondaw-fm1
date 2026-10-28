/* SPDX-License-Identifier: GPL-3.0-only */
/* The machine driven from a computer (link.h, version 2): its status read,
 * the hands' gestures played in, and the song in memory written into
 * directly -- steps, cells, lengths -- so that a computer, or a model on it,
 * can play, measure and compose on the device. Nothing here runs until a
 * message arrives. tools/fm1_ctl.py is the other end. */
#ifndef REMOTE_H
#define REMOTE_H

#include "../platform/platform.h"

void remote_init(void); /* answers link.h's control commands from now on */
/* Once a frame, before any screen: what the computer pressed, held and
 * turned is added to what the hands did. */
void remote_input(PlatInput *in);

#endif
