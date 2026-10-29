/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA memory map.
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * RAMTEXT holds .ram_text (code that must run while the flash is busy: the
 * flash driver), then .ram_audio (MOONDAW, 28/10: the audio core -- synth,
 * player, fx -- run from RAM: from XIP the cache misses cost the audio
 * interrupt most of its block; what of them only the main loop runs is
 * .flash_text, left in XIP), and the second core's idle loop (.c1_text, so
 * that an idle CPU1 never fetches from the flash). It sits at the TOP of the SRAM, under the
 * stacks: a direct call reaches +-4 MiB, and 0x01C00000 is just over 4 MiB
 * from the XIP code -- from here the whole image is in reach both ways.
 * (Until 28/10 RAMTEXT was 0x01C00000..0x01C06000 and the 8 KiB after it was
 * kept for RAM-run debug; those 32 KiB are now unused.)
 * RAM  : .data/.bss (72 KiB): what the audio and key-scan interrupts touch, small variables
 * POOL : big zero-initialised buffers (FX lines, canvases, the parts' engine memory, the main loop's work
 *        buffers), section .pool. The same SRAM: the RAM / POOL boundary only splits the budget (1.5: 96 / 336
 *        KiB -> 72 / 360 KiB); tools/build.py reads both lengths from here
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    RAMTEXT (rwx): ORIGIN = 0x01C6C000, LENGTH = 0x8000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 72K
    POOL   (rw)  : ORIGIN = 0x01C1A000, LENGTH = 0x52000
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(EXCLUDE_FILE(*core_synth.o *core_player.o *core_fx.o) .text*)
              *(.flash_text*)  /* the audio core's main-loop side (player.c) */
              *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .ram_audio : { _ra_start = .; *core_synth.o(.text*) *core_player.o(.text*) *core_fx.o(.text*)
                   _c1_start = .; KEEP(*(.c1_text*)) _c1_end = .;  /* the second core's loop (fm1_cpu1.h) */
                   . = ALIGN(4); _ra_end = .; } > RAMTEXT AT > XIP
    _ra_load = LOADADDR(.ram_audio);
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
