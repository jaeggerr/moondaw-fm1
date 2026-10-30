/* SPDX-License-Identifier: GPL-3.0-only
 * Boot, interrupts and main loop after Felucca's felucca.c and main.c:
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MOONDAW on the FM-1: one compilation unit, as Felucca is (its HAL is
 * header-only and its drivers are written to be #included in order).
 *
 * This file is the FM-1 backend of platform/platform.h. Above it, app/
 * and core/ know nothing of the machine and are compiled on their own,
 * as ordinary C; below it, hal/ and the drivers are Felucca's (see
 * UPSTREAM.md for what was changed in them).
 *
 * The FELUCCA_* switches keep their names because usb.c, ota.c and the
 * loader read them. */
#define PLAT_FM1 1
#define FELUCCA_FLASH 1
#define FELUCCA_OTA 1            /* the M-UPGRADE entry: without it nothing can replace this firmware over USB */
#ifndef FELUCCA_OTA_DRYRUN
#define FELUCCA_OTA_DRYRUN 0
#endif
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
#define FELUCCA_UART 0
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_900"    /* package identity (build.py: the .fwsc marker string) */
#endif

/* The second core renders half of the tracks (felucca-bsp's worker, fm1_cpu1.h; 28/10: one core did not hold the
 * chorus of DAFT-PUNK-EN). A failure -- a misread, a fault, a job late -- holds it until power-off and the first
 * core renders everything, as before: never a restart for it. MOONDAW_CPU1=0 builds with one core. */
#ifndef MOONDAW_CPU1
#define MOONDAW_CPU1 1
#endif
#define FM1_CPU1 MOONDAW_CPU1
#define FM1_CPU1_FALLBACK 1

#define HALF_FRAMES 128          /* I2S half buffer: 2.9 ms */
#define FS 44100
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */

/* ------------------------------------------------------------- HAL --- */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_uart.h"
#include "fm1_lcd_hw.h"
#if FM1_CPU1
#include "fm1_cpu1.h"
#endif

static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR below) */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));

/* ------------------------------------------- base, display, the app --- */
#include "libc.c"
#include "lcd.c"
#include "../../platform/platform.h"
#include "../../platform/midi_parse.h"
#include "../../app/term.h"      /* the crash and update screens draw with the app's text cells */
#include "../../app/splash.h"    /* and the mark, into the window the panel's wake-up already costs */

/* ------------------------------------------------- USB, audio, panel --- */
#include "usb.c"
#include "audio.c"
#include "panel.c"

/* ------------------------------------------------- flash, the update --- */
#include "fm1_flash.h"
#include "storage_hw.c"
#include "ota.c"
#include "ota_hw.c"

/* ------------------------------------------------------ the platform --- */
void plat_blit(int x, int y, int w, int h, const uint16_t *px)
{
    lcd_blit((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, px);
    lcd_sync();                  /* the DMA reads px: done before the caller has it back */
}

/* The song store: Felucca's main data area and nothing else. storage_hw.c
 * checks every erase and write against FL_STORE_OK again; the bound here
 * is the first of two. Nothing is touched when the flash chip is not
 * the one expected. */
#define STORE_BASE FL_DATA_LO
typedef char store_fits[FL_DATA_LO + PLAT_FLASH_SECTORS * PLAT_FLASH_SECTOR <= FL_DATA_HI ? 1 : -1];

static int store_range(uint32_t off, uint32_t n)
{
    return flash_ok && off <= PLAT_FLASH_SECTORS * PLAT_FLASH_SECTOR &&
           n <= PLAT_FLASH_SECTORS * PLAT_FLASH_SECTOR - off;
}

int plat_flash_read(uint32_t off, void *dst, uint32_t n)
{
    return store_range(off, n) ? st_read(STORE_BASE + off, dst, n) : -1;
}

const uint8_t *plat_flash_map(void)
{
    return flash_ok ? fm1_xip_ptr(STORE_BASE) : 0; /* plain since fl_plain_window_init, at boot */
}

int plat_flash_erase(uint32_t sector)
{
    return store_range(sector * PLAT_FLASH_SECTOR, PLAT_FLASH_SECTOR)
               ? st_erase(STORE_BASE + sector * PLAT_FLASH_SECTOR) : -1;
}

int plat_flash_prog(uint32_t off, const void *src, uint32_t n)
{
    return store_range(off, n) ? st_prog(STORE_BASE + off, src, n) : -1;
}

/* Songs from the computer (app/link.c) come in through the updater's
 * SysEx slot. A message that is ours is COPIED out and the slot freed at
 * once, here, in the main loop: whatever the app does or does not do
 * with it, the updater's own messages are never held up behind it --
 * M-UPGRADE is one of the ways back in. */
static uint8_t link_msg[640];
static uint32_t link_len;

static void link_poll(void)
{
    const uint8_t *p;
    uint32_t n;
    if (!ota_frame_get(&p, &n))
        return;
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x4Du || p[2] != 0x44u)
        return;                                 /* the updater's: ota_service takes it */
    for (uint32_t i = 0; i < n; i++)
        link_msg[i] = p[i];
    link_len = n;
    ota_frame_done();
}

int plat_sysex_get(const uint8_t **p, uint32_t *n)
{
    if (!link_len)
        return 0;
    *p = link_msg;
    *n = link_len;
    return 1;
}

void plat_sysex_done(void) { link_len = 0; }

int plat_sysex_send(const uint8_t *p, uint32_t n)
{
    static uint8_t wire[2 + 160];
    if (n > sizeof wire - 2u)
        return -1;
    wire[0] = 0xF0;
    for (uint32_t i = 0; i < n; i++)
        wire[1 + i] = p[i];
    wire[1 + n] = 0xF7;
    return ota_wire_send(wire, n + 2u);
}

int plat_audio_load(int *worst)
{
    uint32_t half_us = HALF_FRAMES * 1000000u / FS, mx = audio_max_us;
    audio_max_us = 0;
    *worst = (int)(mx * 100u / half_us);
    return (int)(audio_cpu_q8 * 100u / 256u);
}

void plat_led_key(int key, int on) { fm1_led_key(14u + (uint32_t)key, on); }
void plat_led_btn(int btn, int on) { fm1_led_key(panel.btn[btn], on); }

/* An LED's bit in one of hal/fm1_input.h's sets, by its stock id. */
static void led_bit(uint8_t *set, uint32_t id, int on)
{
    for (uint32_t p = 0; p < FM1_NCOL; p++)
        for (uint32_t r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                if (on)
                    set[p] |= (uint8_t)(1u << r);
                else
                    set[p] &= (uint8_t)~(1u << r);
            }
}

/* fm1_led_key's glow: the same LED, in fm1_led_dim. */
void plat_led_glow(int btn, int on) { led_bit(fm1_led_dim, panel.btn[btn], on); }

void plat_led_level(int led, int level)
{
    uint32_t id = led < PLAT_BTNS ? panel.btn[led] : 14u + (uint32_t)(led - PLAT_BTNS);
    if (led < 0 || led >= PLAT_BTNS + PLAT_KEYS)
        return;
    led_bit(fm1_led, id, level == PLAT_LED_LIT);
    led_bit(fm1_led_dim, id, level == PLAT_LED_GLOW || level == PLAT_LED_MID); /* mid rides on the glow */
    led_bit(fm1_led_mid, id, level == PLAT_LED_MID);
    led_bit(fm1_led_breath, id, level == PLAT_LED_BREATH);
    for (int k = 0; k < 4; k++)
        led_bit(fm1_led_duty[k], id, level == PLAT_LED_50 + k);
}

static int32_t master_knob = 512 * 16;   /* the pot x16, smoothed per frame (Felucca's master.c) */

/* MIDI IN: a keyboard's notes, from the two ways in into the frame the app
 * reads (PlatInput.midi) -- the cable (usb.c's ring) and the 3.5 mm MIDI
 * jack (UART1 at 31250 baud, DMA into trs_ring; hal/fm1_uart.h). A parser
 * each, because running status belongs to one stream and these two know
 * nothing of each other, and one frame for both: a note plays the same
 * whichever way it came in.
 *
 * It runs at the top of the loop because the USB ring has to be emptied
 * whatever listens -- a full one NAKs EP1, and the update's SysEx comes in
 * through EP1 -- and what one frame cannot hold lets every note go rather
 * than leave one hanging (midi_parse.h). */
static MidiParse midi_usb, midi_trs;

#define TRS_RING 256u                   /* 31250 baud is 47 bytes a frame: room to spare */
static volatile uint8_t trs_ring[TRS_RING] __attribute__((aligned(16)));
static uint32_t trs_r;

static void midi_drain(void)
{
    uint32_t n;
    while (mi_r != mi_w) {
        uint32_t pkt;
        RING_PUBLISH();                 /* the ISR (producer) can preempt us: */
        pkt = midi_in_q[mi_r % MQ];     /* slot read strictly between the index checks */
        RING_PUBLISH();
        mi_r++;
        midi_parse_usb(&midi_usb, pkt);
    }
    if (midi_in_overflow) {             /* the ring filled: a broken stream, and nothing stays down */
        midi_parse_release(&midi_usb);
        midi_in_overflow = 0;
    }
    n = fm1_uart1_rx_take();            /* the jack: what the DMA wrote since the last frame */
    fm1_uart1_rx_sync();                /* it wrote the ring behind the cache's back */
    if (n >= TRS_RING) {                /* a frame late enough to lose bytes: let go rather than hang */
        midi_parse_release(&midi_trs);
        trs_r += n;
        return;
    }
    while (n--) {
        uint8_t b = trs_ring[trs_r % TRS_RING];
        trs_r++;
        midi_parse_bytes(&midi_trs, &b, 1u);
    }
}

static void input_read(PlatInput *in)
{
    uint32_t i, raw = fm1_in.buttons;
    int32_t a = fm1_adc_read(FM1_ADC_MASTER);
    if (a >= 0)
        master_knob += (a * 16 - master_knob) / 8;
    in->keys = fm1_in.notes;
    in->btns = 0;
    for (i = 0; i < NB; i++)
        if (raw & (1u << panel.btn[i]))
            in->btns |= 1u << i;
    for (i = 0; i < NE; i++) {
        int32_t s = panel_enc(i);
        in->enc[i] = (int8_t)(s > 127 ? 127 : s < -127 ? -127 : s);
    }
    in->master = (uint16_t)(master_knob / 16);
    in->ms = fm1_ms;
    in->midi_n = 0;                   /* the notes midi_drain took at the top of the loop, the cable's first */
    for (i = 0; i < midi_usb.n && in->midi_n < PLAT_MIDI_MAX; i++)
        in->midi[in->midi_n++] = midi_usb.q[i];
    for (i = 0; i < midi_trs.n && in->midi_n < PLAT_MIDI_MAX; i++)
        in->midi[in->midi_n++] = midi_trs.q[i];
    midi_usb.n = midi_trs.n = 0;
}

/* ---------------------------------------------------- interrupts --- */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];
extern uint32_t _ra_start[], _ra_end[], _ra_load[];
/* the player's stopwatch and its second core (core/player.h), absent from the bring-up build */
extern uint32_t (*player_clock)(void) __attribute__((weak));
extern int (*player_run_other)(void (*fn)(uint32_t), uint32_t arg) __attribute__((weak));
extern int (*player_wait_other)(void) __attribute__((weak));
/* the trail's audio side (core/player.c, core/sampler.c, .noinit here): the track each core renders and its stage,
 * the last block of sample read */
extern volatile uint8_t player_trail[2] __attribute__((weak));
extern volatile uint32_t smp_trail __attribute__((weak));
/* What the link reports (app/remote.c, `fm1_ctl.py status --boot`): why the last start happened -- P33's reset
 * source (bit0 power-on, 1 VDDIO low, 2 watchdog, 3 VCM, 4 long press, 5 1.2 V, 6 soft) and PWR_CON's --, the
 * last crash record, and the second core's state, kept current by the main loop. */
static uint32_t plat_boot_info[26];
extern const uint32_t *remote_boot_info __attribute__((weak));   /* app/remote.c, absent from the bring-up build */
#if FM1_CPU1
static int cpu1_run(void (*fn)(uint32_t), uint32_t arg) { return fm1_cpu1_run(fn, arg); }
static int cpu1_wait(void) { return fm1_cpu1_wait(); }
/* a number for this build: a bar set by another build's failure is lifted (fm1_cpu1_bar_build) */
static uint32_t build_id(void)
{
    const char *s = __DATE__ __TIME__;
    uint32_t h = 2166136261u;
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}
#endif
/* The player's stopwatch. ticks / 24 turned over every 179 s (2^32 ticks of 24 MHz), not at 2^32: a block timed
 * across the turn came out as 178 956 970 us (29/10). Counted on instead, so differences stay right; the audio
 * interrupt is its only caller. */
static uint32_t fm1_us(void)
{
    static uint32_t last, us, rem;
    uint32_t t = fm1_ticks(), d = t - last + rem;
    last = t;
    us += d / FM1_TICKS_PER_US;
    rem = d % FM1_TICKS_PER_US;
    return us;
}

/* TIMER5, 10 kHz, above ALNK0: the key scan keeps its pace while a half
 * buffer renders. Nested in the audio ISR it only scans and counts ms;
 * the USB poll waits for the first tick after the render (Felucca's
 * main.c, where the reasons are written out). */
void fm1_timer5_irq(void)
{
    static uint32_t sub, owed;
    uint32_t t0 = fm1_ticks(), usb_due = sub % 5u == 0u;
    fm1_timer5_ack();
    felucca_dbg.timer_irqs++;
    fm1_input_tick();
    {   /* milliseconds from the 24 MHz TIMER4 (robust to a late tick) */
        static uint32_t last, acc;
        acc += t0 - last;
        last = t0;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
    if (usb_due)
        owed = 1u;                              /* 2 kHz: all USB SIE traffic lives here */
    if (++sub == 10u)
        sub = 0;
    if (felucca_dbg.in_audio) {
        felucca_dbg.nested++;
        t5_nested_ticks += fm1_ticks() - t0;
        return;
    }
    if (owed)
        usb_poll();
    owed = 0;
}
extern void isr_timer5(void);

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    fm1_audio_stop();
    lcd_wake_now();
    term_clear();
    term_put(0, 1, "         MOONDAW CRASH        ", C_WHITE, C_RED);
    hexs(b, c->vec);
    term_put(2, 5, "VEC", C_DIM, C_BLACK);
    term_put(8, 5, b, C_WHITE, C_BLACK);
    hexs(b, c->pc);
    term_put(2, 7, "PC", C_DIM, C_BLACK);
    term_put(8, 7, b, C_WHITE, C_BLACK);
    hexs(b, c->emu);
    term_put(2, 9, "EMU", C_DIM, C_BLACK);
    term_put(8, 9, b, C_WHITE, C_BLACK);
    hexs(b, c->dbg);
    term_put(2, 11, "DBG", C_DIM, C_BLACK);
    term_put(8, 11, b, C_WHITE, C_BLACK);
    hexs(b, c->rets);
    term_put(2, 13, "RETS", C_DIM, C_BLACK);
    term_put(8, 13, b, C_WHITE, C_BLACK);
    term_flush();
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

static void banner(const char *s)
{
    fm1_audio_stop();
    term_clear();
    term_put(10, 14, s, C_CYAN, C_BLACK);
    term_flush();
}

/* ------------------------------------------------------ main loop --- */
static void fm1_main(void)
{
    static PlatInput in;
    uint32_t splash_t0 = 0;
    int splash_done = 0;
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;     /* the expected 1 MiB part, else no flash access at all */
    irq_restore(f);
    if (flash_ok) {
        fl_plain_window_init();
        ota_boot_cleanup();                             /* staging area left by an update */
    }
    lcd_init();
    /* The panel is lit and empty from here until the app's first frame: the
     * mark goes in now and is overwritten when there is something to show.
     * It holds nothing up, and it is the only sign at power-on that this is
     * MOONDAW on the machine and not what came on it (app/splash.h). */
    splash_show();
    splash_t0 = fm1_ticks();                    /* held to SPLASH_HOLD_MS from here, in the loop below */
    if (felucca_dbg.magic != DBG_MAGIC) {
        memset(&felucca_dbg, 0, sizeof felucca_dbg);
        felucca_dbg.magic = DBG_MAGIC;
    } else {    /* the trail of the run before this start: where each side was when it ended */
        plat_boot_info[10] = felucca_dbg.stage;
        plat_boot_info[11] = felucca_dbg.loop_ms;
        plat_boot_info[12] = felucca_dbg.audio_ms;
        plat_boot_info[13] = felucca_dbg.in_audio;
        plat_boot_info[14] = felucca_dbg.max_us;
        plat_boot_info[15] = felucca_dbg.late;
        plat_boot_info[16] = &player_trail ? (uint32_t)player_trail[0] | (uint32_t)player_trail[1] << 8 : 0;
        plat_boot_info[17] = &smp_trail ? smp_trail : 0;
    }
    felucca_dbg.late = 0;
    felucca_dbg.stage = felucca_dbg.loop_ms = felucca_dbg.audio_ms = 0;
    felucca_dbg.boots++;
    felucca_dbg.max_us = 0;
    felucca_dbg.in_audio = 0;                   /* .noinit: a reset inside the audio ISR left it set */
    fm1_input_init();
    fm1_adc_init();
    panel.magic = 0;                            /* no calibration here: always the measured default */
    panel_init();
    plat_boot_info[0] = fm1_boot.p3_rst;
    plat_boot_info[1] = fm1_boot.rst_src;
    if (fm1_crash.magic == FM1_CRASH_MAGIC) {
        plat_boot_info[2] = fm1_crash.count;
        plat_boot_info[3] = fm1_crash.vec;
        plat_boot_info[4] = fm1_crash.pc;
        plat_boot_info[5] = fm1_crash.uptime_ms;
        plat_boot_info[18] = fm1_crash.rets;    /* and the rest of it, for `status --crash` */
        plat_boot_info[19] = fm1_crash.emu;
        plat_boot_info[20] = fm1_crash.dbg;
        plat_boot_info[21] = fm1_crash.sp;
        for (uint32_t k = 0; k < 4u; k++)
            plat_boot_info[22 + k] = fm1_crash.etm[k];
    }
    if (&remote_boot_info)
        remote_boot_info = plat_boot_info;
    if (&player_clock)
        player_clock = fm1_us;
#if FM1_CPU1
    /* The rails as M-VAVE's firmware sets them (under load at the boot loader's, some units misread the flash
     * and the SRAM), then the second core: at power-on, before the audio and the timers, interrupts off. */
    fm1_vdc14_raise(7);
    fm1_sysvdd_raise(15);
    fm1_cpu1_bar_build(build_id());
    {
        uint32_t f = irq_save();
        int ok = fm1_cpu1_start() == 0;
        irq_restore(f);
        if (ok && &player_run_other) {
            player_run_other = cpu1_run;
            player_wait_other = cpu1_wait;
        }
    }
#endif
    app_init();
    (void)fm1_adc_read(FM1_ADC_MASTER);         /* the first conversion after the pin setup: dropped */
    {
        int32_t a = fm1_adc_read(FM1_ADC_MASTER);
        if (a >= 0)
            master_knob = a * 16;               /* the knob as it stands before the first sound */
    }
    audio_init();
    usb_start();
    fm1_uart1_midi_init(trs_ring, TRS_RING);    /* the 3.5 mm MIDI jack, before TIMER5 (PORTH RMW) */
    fm1_timer5_start(isr_timer5, 4);            /* above ALNK0 (3) */
    fm1_guard_lock_top();
    fm1_irq_enable_all();

    /* The trail's stages (felucca_dbg.stage): 1 the loop's top, 2 the link, 3 the updater, 4 the hands, 5 the
     * app's frame, 6 waiting for the next one. */
    for (;;) {
        uint32_t m = fm1_ms;
        felucca_dbg.stage = 1;
        felucca_dbg.loop_ms = m;
        fm1_wdt_feed();
#if FM1_CPU1
        plat_boot_info[6] = fm1_cpu1_on;
        plat_boot_info[7] = fm1_cpu1_gave_up;  /* FM1_CPU1_*: 1 misread, 2 late, 3 barred, 4 fault, 5 silent */
        plat_boot_info[8] = fm1_cpu1_mb.jobs;
        plat_boot_info[9] = fm1_cpu1_stack_used();
#endif
        usb_retry(fm1_ms);
        if (fm1_ms > 30000u && bootguard.pending) {     /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        midi_drain();                           /* MIDI IN: a keyboard's notes, for the frame further down */
        mo_r = mo_w;
        {   /* OCT- + OCT+ held 5 s: enter UBOOT, as Felucca does (the way back in whatever the app is doing) */
            static uint32_t t0;
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if ((fm1_in.buttons & both) != both)
                t0 = fm1_ms;
            else if (fm1_ms - t0 > 5000u) {
                banner("UBOOT");
                usb_detach();
                fm1_delay_ms(30);
                bootguard.pending = 0;          /* intentional reset: not a failed boot */
                fm1_enter_uboot();
            }
        }
        felucca_dbg.stage = 2;
        link_poll();                            /* ours first: it is copied out, never left in the slot */
        felucca_dbg.stage = 3;
        ota_service();                          /* M-UPGRADE handshake */
        if (usb.ota_req) {                      /* M-UPGRADE upgrade command */
            usb.ota_req = 0;
            if (flash_ok)
                ota_session();                  /* returns only if nothing was committed */
            app_init();
        }
        if (usb.uboot_req) {                    /* SysEx F0 22 24 35 7D F7 from the host */
            banner("UBOOT (USB)");
            fm1_delay_ms(20);
            usb_detach();
            fm1_delay_ms(30);
            bootguard.pending = 0;
            fm1_enter_uboot();
        }
        felucca_dbg.ui_frames++;
        felucca_dbg.stage = 4;
        input_read(&in);
        felucca_dbg.stage = 5;
        /* The mark stays its second. Everything above has already run this
         * time round -- the watchdog, USB, the link, the way into UBOOT --
         * so only the first frame waits, and only for what is left of it. */
        if (!splash_done &&
            (uint32_t)(fm1_ticks() - splash_t0) >= SPLASH_HOLD_MS * 1000u * FM1_TICKS_PER_US)
            splash_done = 1;
        if (splash_done)
            app_frame(&in);
        felucca_dbg.stage = 6;
        while (fm1_ms - m < 15u)
            ;                                   /* ~60 frames/s at most */
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 2u) {               /* two boots died in their first 30 s: stay in UBOOT */
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                /* flash driver code that must run from RAM */
    for (s = _ra_load, d = _ra_start; d < _ra_end; s++, d++)
        *d = *s;                                /* the audio core: from RAM, no cache to miss */
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}
