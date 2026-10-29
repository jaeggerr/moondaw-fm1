/* SPDX-License-Identifier: GPL-3.0-only */
/* MOONDAW's colours, in one place: every screen names a colour by what it
 * means, never by its value, so changing the look is changing a line here.
 * (app/type.c, the type specimen, keeps its own: it is a measuring tool.) */
#ifndef THEME_H
#define THEME_H

#include "gfx.h"

/* Black and pure white: the most contrast the panel has, chosen on it on 28/10 over
 * the blue-black of the 10/10 screens, which made the type stand out less. */
#define C_BG     GFX_RGB(0x00, 0x00, 0x00) /* the screen */
#define C_TEXT   GFX_RGB(0xFF, 0xFF, 0xFF)
#define C_DIM    GFX_RGB(0x5A, 0x64, 0x70) /* what is there but not the point: + NEW, empty */
#define C_ACCENT GFX_RGB(0x7F, 0xD9, 0x62) /* the cursor, and what plays */
#define C_ON_ACC GFX_RGB(0x0B, 0x14, 0x09) /* anything drawn on the cursor */
#define C_TITLE  GFX_RGB(0x62, 0xC8, 0xD9) /* where one is: the open project, the phrase playing */
#define C_FX     GFX_RGB(0xF0, 0xBE, 0x5A) /* effects; and what is not saved */
#define C_MUTE   GFX_RGB(0x9E, 0x55, 0x55)
#define C_BEAT   GFX_RGB(0x91, 0x9A, 0x9C)
#define C_PLAY   GFX_RGB(0x24, 0x3A, 0x28)
#define C_SEL    GFX_RGB(0x6E, 0x55, 0x18)
#define C_WHITE  GFX_RGB(0xFF, 0xFF, 0xFF)
#define C_TRACK  GFX_RGB(0x28, 0x28, 0x28) /* a scroll bar's groove */
#define C_SAMPLE GFX_RGB(0xC0, 0x84, 0xE8) /* the sample bank, on SYSTEM */
#define C_POP    GFX_RGB(0x1A, 0x20, 0x29) /* a panel opened over the screen */
/* The SYLLABLE screen's keyboard, drawn as the keys lie under the hand. */
#define C_KEY      GFX_RGB(0xD8, 0xD8, 0xD8) /* a white key that types */
#define C_KEY_ON   GFX_RGB(0x00, 0x00, 0x00) /* its sign */
#define C_KEY_NONE GFX_RGB(0x3A, 0x3A, 0x3A) /* a white key that types nothing on this layer */
#define C_KEY_B    GFX_RGB(0x2C, 0x6E, 0x7A) /* a black key: a vowel */
#define C_KEY_BOFF GFX_RGB(0x14, 0x14, 0x14) /* a black key that types nothing */
#define C_WELL     GFX_RGB(0x10, 0x18, 0x20) /* the frame the syllable is shown in */
#define C_WHEEL    GFX_RGB(0x1F, 0x3A, 0x12) /* under the wheel's sign */
#define C_SLOT     GFX_RGB(0x1A, 0x1A, 0x1A) /* a slot without the cursor */

#endif
