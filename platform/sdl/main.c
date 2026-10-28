/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 on a desk: the 240x240 screen and the panel laid out as on
 * the machine -- MASTER, SELECT, PRESETS, ALGORITHM and the two OCT
 * buttons left of the screen, KNOB 1-4 and two rows of six buttons
 * right of it, the 27 keys under everything -- worked from the keyboard
 * and the mouse. The layout matters: the screens draw the twelve
 * buttons at their place.
 *
 * Keys are named by their place on a US keyboard; it is the PLACE that
 * counts (scancodes), so an AZERTY keyboard works the same way.
 *
 *   buttons   1 2 3 4 5 6   = FX SEL ENV LFO EDIT GLO     two rows of six,
 *             Q W E R T Y   = HOME SAVE ARP SEQ PLAY REC  as on the machine
 *             A and ' are OCT- and OCT+; SPACE is PLAY too; or click
 *   keys      Z S X D C F V B H N J M , L . ; /     F3..A4 (the home row: sharps)
 *             7 U I 9 O 0 P [ = ]                   A#4..G5
 *   encoders  wheel over a knob, or drag it up and down;
 *             the arrows are SELECT (up, down) and ALGORITHM (left, right)
 *
 *   moondaw-fm1 [SONG.trk]   opens that song instead of the one saved in the
 *                            simulated flash (moondaw-fm1.flash, in the
 *                            current directory; MOONDAW_FLASH names another)
 *   --selftest OUT.bmp [SONG.trk [PHRASE [TRACK]]]
 *                            no window, no sound card: presses PLAY and
 *                            holds A4, checks the picture and that there
 *                            is sound, writes the screen; then opens
 *                            SONG, moves to that phrase and track, and
 *                            PATTERN on it: OUT-song.bmp, OUT-pattern.bmp */
#include <SDL.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../platform.h"
#include "../udp_sysex.h"
#include "../../app/font/font8x8_basic.h"

#define CV_W 600
#define CV_H 312
#define SCALE 2
#define LCD_X 112
#define LCD_Y 8

static uint32_t canvas[CV_W * CV_H]; /* ARGB8888 */
static uint16_t lcd[PLAT_W * PLAT_H];
static uint32_t led_keys, led_btns;

static uint32_t kb_keys, kb_btns, mouse_keys, mouse_btns;
static int enc_acc[PLAT_ENCS];
static int master = 700;

/* ------------------------------------------------------ platform --- */
void plat_blit(int x, int y, int w, int h, const uint16_t *px)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if (x + i >= 0 && x + i < PLAT_W && y + j >= 0 && y + j < PLAT_H)
                lcd[(y + j) * PLAT_W + x + i] = px[j * w + i];
}

int plat_audio_load(int *worst)
{
    *worst = -1;
    return -1;
}

void plat_led_key(int key, int on)
{
    led_keys = on ? led_keys | 1u << key : led_keys & ~(1u << key);
}

void plat_led_btn(int btn, int on)
{
    led_btns = on ? led_btns | 1u << btn : led_btns & ~(1u << btn);
}

/* The flash: a file beside the program, read at start and rewritten on
 * every change. MOONDAW_FLASH names another; the selftest keeps it in
 * memory. Programming only clears bits, as on the chip. */
static uint8_t nor[PLAT_FLASH_SECTORS * PLAT_FLASH_SECTOR];
static const char *nor_path;

static void nor_write(void)
{
    FILE *f = nor_path ? fopen(nor_path, "wb") : NULL;
    if (f) {
        fwrite(nor, 1, sizeof nor, f);
        fclose(f);
    }
}

static void nor_open(const char *path)
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    memset(nor, 0xFF, sizeof nor);
    if (f) {
        size_t n = fread(nor, 1, sizeof nor, f);
        (void)n;
        fclose(f);
    }
    nor_path = path;
}

int plat_flash_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > sizeof nor || n > sizeof nor - off)
        return -1;
    memcpy(dst, nor + off, n);
    return 0;
}

int plat_flash_erase(uint32_t sector)
{
    if (sector >= PLAT_FLASH_SECTORS)
        return -1;
    memset(nor + sector * PLAT_FLASH_SECTOR, 0xFF, PLAT_FLASH_SECTOR);
    nor_write();
    return 0;
}

int plat_flash_prog(uint32_t off, const void *src, uint32_t n)
{
    if (off > sizeof nor || n > sizeof nor - off)
        return -1;
    for (uint32_t i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    nor_write();
    return 0;
}

/* --------------------------------------------------------- panel --- */
static const char *const btn_label[PLAT_BTNS] = {
    "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
    "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+",
};
static const char *const knob_label[PLAT_ENCS + 1] = {
    "SEL", "ALG", "PRE", "K1", "K2", "K3", "K4", "VOL",
};
/* F3..G5: which of the 27 are black keys. */
static const uint8_t is_black[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };

typedef struct { int x, y, w, h; } Box;

/* Two rows of six right of the screen, FX..GLO over HOME..REC; the two
 * OCT buttons under the left knobs. */
static Box btn_box(int b)
{
    if (b >= PB_OCTDN)
        return (Box){ 8 + (b - PB_OCTDN) * 50, 92, 46, 16 };
    return (Box){ 368 + (b % 6) * 38, 62 + (b / 6) * 20, 36, 16 };
}
/* k: the seven encoders in PE_* order, then MASTER. */
static Box knob_box(int k)
{
    static const Box left[3] = { { 58, 10, 46, 32 }, { 58, 48, 46, 32 }, { 8, 48, 46, 32 } };
    if (k == PLAT_ENCS)
        return (Box){ 8, 10, 46, 32 };
    if (k < 3)
        return left[k];
    return (Box){ 368 + (k - 3) * 57, 10, 53, 32 };
}
static Box key_box(int k) { return (Box){ 16 + k * 21, 262, 20, 44 }; }
static bool in_box(Box b, int x, int y)
{
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

static void fill(Box b, uint32_t c)
{
    for (int j = b.y; j < b.y + b.h; j++)
        for (int i = b.x; i < b.x + b.w; i++)
            if (i >= 0 && i < CV_W && j >= 0 && j < CV_H)
                canvas[j * CV_W + i] = c;
}

static void text(int x, int y, const char *s, uint32_t c)
{
    for (; *s; s++, x += 8)
        for (int r = 0; r < 8; r++)
            for (int b = 0; b < 8; b++)
                if ((font8x8_basic[*s & 0x7F][r] >> b) & 1 && x + b < CV_W)
                    canvas[(y + r) * CV_W + x + b] = c;
}

static void paint(uint32_t keys, uint32_t btns)
{
    fill((Box){ 0, 0, CV_W, CV_H }, 0xFF202428);
    fill((Box){ LCD_X - 2, LCD_Y - 2, PLAT_W + 4, PLAT_H + 4 }, 0xFF000000);
    for (int i = 0; i < PLAT_W * PLAT_H; i++) {
        uint16_t p = lcd[i];
        canvas[(LCD_Y + i / PLAT_W) * CV_W + LCD_X + i % PLAT_W] =
            0xFF000000u | (uint32_t)((p >> 11) * 255 / 31) << 16 |
            (uint32_t)(((p >> 5) & 63) * 255 / 63) << 8 |
            (uint32_t)((p & 31) * 255 / 31);
    }
    for (int b = 0; b < PLAT_BTNS; b++) {
        Box x = btn_box(b);
        bool down = (btns >> b) & 1u, lit = (led_btns >> b) & 1u;
        fill(x, down ? 0xFF8090A0 : 0xFF3A4048);
        text(x.x + 2, x.y + 4, btn_label[b], lit ? 0xFFFFC040 : 0xFFB0B8C0);
    }
    for (int k = 0; k <= PLAT_ENCS; k++) {
        Box x = knob_box(k);
        char v[8];
        fill(x, 0xFF2C3238);
        text(x.x + 2, x.y + 3, knob_label[k], 0xFF8090A0);
        snprintf(v, sizeof v, "%4d", k < PLAT_ENCS ? enc_acc[k] : master);
        text(x.x + 6, x.y + 19, v, 0xFFE0E8F0);
    }
    for (int k = 0; k < PLAT_KEYS; k++) {
        Box x = key_box(k);
        bool black = is_black[(5 + k) % 12];
        bool down = (keys >> k) & 1u;
        fill(x, down ? 0xFF60E070 : black ? 0xFF101214 : 0xFFE8ECF0);
        if ((led_keys >> k) & 1u)
            fill((Box){ x.x + 3, x.y + 3, 4, 4 }, 0xFFFF4040);
    }
}

/* ------------------------------------------------------ keyboard --- */
static const SDL_Scancode note_sc[PLAT_KEYS] = {
    SDL_SCANCODE_Z, SDL_SCANCODE_S, SDL_SCANCODE_X, SDL_SCANCODE_D,
    SDL_SCANCODE_C, SDL_SCANCODE_F, SDL_SCANCODE_V, SDL_SCANCODE_B,
    SDL_SCANCODE_H, SDL_SCANCODE_N, SDL_SCANCODE_J, SDL_SCANCODE_M,
    SDL_SCANCODE_COMMA, SDL_SCANCODE_L, SDL_SCANCODE_PERIOD,
    SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_SLASH,
    SDL_SCANCODE_7, SDL_SCANCODE_U, SDL_SCANCODE_I, SDL_SCANCODE_9,
    SDL_SCANCODE_O, SDL_SCANCODE_0, SDL_SCANCODE_P,
    SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_EQUALS, SDL_SCANCODE_RIGHTBRACKET,
};
/* No function keys: macOS keeps F11 and wants fn for the others. */
static const SDL_Scancode btn_sc[PLAT_BTNS] = {
    SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
    SDL_SCANCODE_5, SDL_SCANCODE_6,
    SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R,
    SDL_SCANCODE_T, SDL_SCANCODE_Y,
    SDL_SCANCODE_A, SDL_SCANCODE_APOSTROPHE,
};

static void on_key(SDL_Scancode sc, bool down)
{
    for (int k = 0; k < PLAT_KEYS; k++)
        if (note_sc[k] == sc)
            kb_keys = down ? kb_keys | 1u << k : kb_keys & ~(1u << k);
    for (int b = 0; b < PLAT_BTNS; b++)
        if (btn_sc[b] == sc || (b == PB_PLAY && sc == SDL_SCANCODE_SPACE))
            kb_btns = down ? kb_btns | 1u << b : kb_btns & ~(1u << b);
}

static int knob_at(int x, int y)
{
    for (int k = 0; k <= PLAT_ENCS; k++)
        if (in_box(knob_box(k), x, y))
            return k;
    return -1;
}

static int8_t enc_step[PLAT_ENCS];

static void turn(int knob, int steps)
{
    if (knob < 0)
        return;
    if (knob == PLAT_ENCS) {
        master += steps * 16;
        master = master < 0 ? 0 : master > 1023 ? 1023 : master;
    } else {
        enc_step[knob] = (int8_t)(enc_step[knob] + steps);
        enc_acc[knob] += steps;
    }
}

static void frame(uint32_t ms)
{
    PlatInput in = { .keys = kb_keys | mouse_keys, .btns = kb_btns | mouse_btns,
                     .master = (uint16_t)master, .ms = ms };
    memcpy(in.enc, enc_step, sizeof enc_step);
    memset(enc_step, 0, sizeof enc_step);
    app_frame(&in);
    paint(in.keys, in.btns);
}

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    app_audio((int16_t *)stream, len / 4);
}

/* ------------------------------------------------------ selftest --- */
static bool load_song(const char *path, SDL_AudioDeviceID dev);

static int selftest(const char *out, const char *song, int entry, int track)
{
    static int16_t pcm[PLAT_FS * 2];
    int lit = 0, peak = 0;
    double sum = 0;

    nor_open(SDL_getenv("MOONDAW_FLASH")); /* read if named, never written back */
    nor_path = NULL;
    app_init();
    if (song && !load_song(song, 0))
        return 1;
    frame(0);
    kb_keys = 1u << 16; /* A4 */
    kb_btns = 1u << PB_PLAY;
    for (int f = 1; f < 4; f++)
        frame((uint32_t)f * 16u);
    app_audio(pcm, PLAT_FS);
    frame(1000);
    for (int i = 0; i < PLAT_FS; i++) {
        sum += (double)pcm[2 * i] * pcm[2 * i];
        if (abs(pcm[2 * i]) > peak)
            peak = abs(pcm[2 * i]);
    }
    for (int i = 0; i < PLAT_W * PLAT_H; i++)
        lit += lcd[i] != 0;

    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormatFrom(
        canvas, CV_W, CV_H, 32, CV_W * 4, SDL_PIXELFORMAT_ARGB8888);
    if (sf) {
        SDL_SaveBMP(sf, out);
        SDL_FreeSurface(sf);
    }
    /* Then into PATTERN, as the hand would: SEL opens the project, OPEN (EDIT), three
     * steps down, one field right, and its picture beside the first. */
    kb_keys = 0;
    kb_btns = 0;
    frame(1016);
    kb_btns = 1u << PB_SEL; /* HOME -> SONG */
    frame(1032);
    kb_btns = 0;
    turn(PE_SELECT, entry);
    turn(PE_ALGO, track);
    frame(1040);
    frame(1041);
    sf = SDL_CreateRGBSurfaceWithFormatFrom(canvas, CV_W, CV_H, 32, CV_W * 4,
                                            SDL_PIXELFORMAT_ARGB8888);
    if (sf) {
        char path[512];
        snprintf(path, sizeof path, "%.*s-song.bmp", (int)strlen(out) - 4, out);
        SDL_SaveBMP(sf, path);
        SDL_FreeSurface(sf);
    }
    /* The MIXER from SONG, and back. */
    kb_btns = 1u << PB_GLO;
    frame(1042);
    kb_btns = 0;
    turn(PE_SELECT, 2);
    frame(1042);
    frame(1042);
    sf = SDL_CreateRGBSurfaceWithFormatFrom(canvas, CV_W, CV_H, 32, CV_W * 4,
                                            SDL_PIXELFORMAT_ARGB8888);
    if (sf) {
        char path[512];
        snprintf(path, sizeof path, "%.*s-mixer.bmp", (int)strlen(out) - 4, out);
        SDL_SaveBMP(sf, path);
        SDL_FreeSurface(sf);
    }
    kb_btns = 1u << PB_HOME;
    frame(1042);
    kb_btns = 0;
    frame(1042);
    /* PHRASE: all the way left to the phrase's number, OPEN, and back. */
    turn(PE_ALGO, -8);
    kb_btns = 1u << PB_EDIT;
    frame(1042);
    kb_btns = 0;
    turn(PE_SELECT, 3);
    turn(PE_ALGO, 3);
    frame(1042);
    kb_btns = 1u << PB_SEL; /* LOOP: one end here, the other five steps down */
    frame(1042);
    kb_btns = 0;
    turn(PE_SELECT, 5);
    frame(1042);
    kb_btns = 1u << PB_SEL;
    frame(1042);
    kb_btns = 0;
    turn(PE_SELECT, -4);
    frame(1042);
    frame(1042);
    sf = SDL_CreateRGBSurfaceWithFormatFrom(canvas, CV_W, CV_H, 32, CV_W * 4,
                                            SDL_PIXELFORMAT_ARGB8888);
    if (sf) {
        char path[512];
        snprintf(path, sizeof path, "%.*s-phrase.bmp", (int)strlen(out) - 4, out);
        SDL_SaveBMP(sf, path);
        SDL_FreeSurface(sf);
    }
    kb_btns = 1u << PB_HOME;
    frame(1042);
    kb_btns = 0;
    frame(1042);
    turn(PE_ALGO, track);
    frame(1042);
    kb_btns = 1u << PB_EDIT; /* SONG -> PATTERN, on that cell */
    frame(1042);
    kb_btns = 0;
    frame(1043);
    turn(PE_SELECT, 3);
    turn(PE_ALGO, 1);
    frame(1048);
    kb_btns = 1u << PB_SEL; /* LOOP here too: steps 03 to 07 */
    frame(1048);
    kb_btns = 0;
    turn(PE_SELECT, 4);
    frame(1048);
    kb_btns = 1u << PB_SEL;
    frame(1048);
    kb_btns = 0;
    turn(PE_SELECT, -4);
    frame(1048);
    frame(1064);
    sf = SDL_CreateRGBSurfaceWithFormatFrom(canvas, CV_W, CV_H, 32, CV_W * 4,
                                            SDL_PIXELFORMAT_ARGB8888);
    if (sf) {
        char path[512];
        snprintf(path, sizeof path, "%.*s-pattern.bmp", (int)strlen(out) - 4, out);
        SDL_SaveBMP(sf, path);
        SDL_FreeSurface(sf);
    }
    /* The list of effects: KNOB 1 turned on FX1 of the first step. */
    turn(PE_SELECT, -3);
    turn(PE_ALGO, 4);
    frame(1064);
    turn(PE_K1, 1);
    frame(1064);
    turn(PE_K1, -1); /* and back: the song is as it was, the list still open */
    frame(1064);
    sf = SDL_CreateRGBSurfaceWithFormatFrom(canvas, CV_W, CV_H, 32, CV_W * 4,
                                            SDL_PIXELFORMAT_ARGB8888);
    if (sf) {
        char path[512];
        snprintf(path, sizeof path, "%.*s-fx.bmp", (int)strlen(out) - 4, out);
        SDL_SaveBMP(sf, path);
        SDL_FreeSurface(sf);
    }
    turn(PE_SELECT, 3);
    turn(PE_ALGO, -4);
    frame(1064);
    /* SELECT: a corner here, the other three steps down and one field right. */
    kb_btns = 1u << PB_EDIT;
    frame(1064);
    kb_btns = 0;
    turn(PE_SELECT, 3);
    turn(PE_ALGO, 1);
    frame(1064);
    frame(1064);
    sf = SDL_CreateRGBSurfaceWithFormatFrom(canvas, CV_W, CV_H, 32, CV_W * 4,
                                            SDL_PIXELFORMAT_ARGB8888);
    if (sf) {
        char path[512];
        snprintf(path, sizeof path, "%.*s-select.bmp", (int)strlen(out) - 4, out);
        SDL_SaveBMP(sf, path);
        SDL_FreeSurface(sf);
    }
    kb_btns = 1u << PB_EDIT;
    frame(1064);
    kb_btns = 0;
    turn(PE_SELECT, -3);
    turn(PE_ALGO, -1);
    frame(1064);
    /* And from there the instrument of that step: INSTR, the ENV button. */
    kb_btns = 1u << PB_ENV;
    frame(1080);
    kb_btns = 0;
    frame(1096);
    frame(1112);
    sf = SDL_CreateRGBSurfaceWithFormatFrom(canvas, CV_W, CV_H, 32, CV_W * 4,
                                            SDL_PIXELFORMAT_ARGB8888);
    if (sf) {
        char path[512];
        snprintf(path, sizeof path, "%.*s-instr.bmp", (int)strlen(out) - 4, out);
        SDL_SaveBMP(sf, path);
        SDL_FreeSurface(sf);
    }
    int rms = (int)SDL_sqrt(sum / PLAT_FS);
    bool ok = lit > 1000 && rms > 200 && peak < 32767;
    printf("selftest: %d lit pixels, 1 s of sound at rms %d, peak %d -> %s\n",
           lit, rms, peak, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static bool load_song(const char *path, SDL_AudioDeviceID dev)
{
    static char text[1 << 20];
    FILE *f = fopen(path, "rb");
    const char *name = strrchr(path, '/');
    char label[20];
    size_t n;
    bool ok;

    if (!f)
        return false;
    n = fread(text, 1, sizeof text, f);
    fclose(f);
    snprintf(label, sizeof label, "%s", name ? name + 1 : path);
    if (strrchr(label, '.'))
        *strrchr(label, '.') = '\0';
    if (dev)
        SDL_LockAudioDevice(dev); /* the song changes under the player */
    ok = app_load(text, (uint32_t)n, label);
    if (dev)
        SDL_UnlockAudioDevice(dev);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "--selftest"))
        return selftest(argv[2], argc > 3 ? argv[3] : NULL, argc > 4 ? atoi(argv[4]) : 0,
                        argc > 5 ? atoi(argv[5]) : 0);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("MOONDAW FM-1", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, CV_W * SCALE,
                                       CV_H * SCALE, SDL_WINDOW_ALLOW_HIGHDPI);
    /* Whole pixels, drawn as squares. Without the flag macOS stretches a
     * low-density window over a dense screen and smears every edge, and
     * the antialiased type of the real screens turns to fog. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, CV_W, CV_H);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, CV_W, CV_H);

    SDL_AudioSpec want = { .freq = PLAT_FS, .format = AUDIO_S16SYS, .channels = 2,
                           .samples = 256, .callback = audio_cb };
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (!dev)
        SDL_Log("no sound: %s", SDL_GetError());

    nor_open(SDL_getenv("MOONDAW_FLASH") ? SDL_getenv("MOONDAW_FLASH") : "moondaw-fm1.flash");
    app_init();
    udp_sysex_open(); /* songs from tools/fm1_send.py --udp */
    if (argc == 2 && !load_song(argv[1], dev))
        SDL_Log("%s: not a version 3 .trk", argv[1]);
    SDL_PauseAudioDevice(dev, 0);

    int drag = -1, drag_y = 0;
    for (bool run = true; run;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                run = false;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                    run = false;
                if (e.type == SDL_KEYDOWN) {
                    SDL_Scancode sc = e.key.keysym.scancode;
                    turn(PE_SELECT, sc == SDL_SCANCODE_DOWN ? 1 : sc == SDL_SCANCODE_UP ? -1 : 0);
                    turn(PE_ALGO, sc == SDL_SCANCODE_RIGHT ? 1 : sc == SDL_SCANCODE_LEFT ? -1 : 0);
                }
                if (!e.key.repeat)
                    on_key(e.key.keysym.scancode, e.type == SDL_KEYDOWN);
                break;
            case SDL_MOUSEBUTTONDOWN:
                for (int k = 0; k < PLAT_KEYS; k++)
                    if (in_box(key_box(k), e.button.x, e.button.y))
                        mouse_keys |= 1u << k;
                for (int b = 0; b < PLAT_BTNS; b++)
                    if (in_box(btn_box(b), e.button.x, e.button.y))
                        mouse_btns |= 1u << b;
                drag = knob_at(e.button.x, e.button.y);
                drag_y = e.button.y;
                break;
            case SDL_MOUSEBUTTONUP:
                mouse_keys = mouse_btns = 0;
                drag = -1;
                break;
            case SDL_MOUSEMOTION:
                if (drag >= 0 && abs(e.motion.y - drag_y) >= 4) {
                    turn(drag, (drag_y - e.motion.y) / 4);
                    drag_y = e.motion.y;
                }
                break;
            case SDL_MOUSEWHEEL: {
                int mx, my;
                float lx, ly;
                SDL_GetMouseState(&mx, &my);
                SDL_RenderWindowToLogical(ren, mx, my, &lx, &ly);
                turn(knob_at((int)lx, (int)ly), e.wheel.y);
                break;
            }
            }
        }
        frame(SDL_GetTicks());
        SDL_UpdateTexture(tex, NULL, canvas, CV_W * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }
    SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return 0;
}
