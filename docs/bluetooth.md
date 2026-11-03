# Bluetooth on the FM-1: what the radio can do

03/11/2026, in two passes. Sections 1 to 5 are research: read in other people's
repositories, nothing built, the radio not turned on. **Section 6 is not**: late
the same day the radio was turned on, on this FM-1, and what it says was
measured here. This supersedes the Bluetooth section of
[`audio-entree.md`](audio-entree.md), which was written on 29/10 and is wrong on
two points (second to last section).

## In short

| question | answer |
|---|---|
| Can a firmware of ours open any Bluetooth link it likes? | **yes**, BLE and classic both, with no vendor blob |
| Is it limited to Bluetooth MIDI? | **no**. MIDI is one small module on a general stack |
| Is "Bluetooth MIDI" a hardware thing? | **no**. It is a GATT service: software over plain BLE |

The reason all three answers changed since 29/10 is **Optimist**
(<https://github.com/w0ts/optimist>, GPL-3.0 like us): between 8/10 and today it
wrote, from the Bluetooth specifications and with no vendor code, a complete BLE
stack **and** a classic BR/EDR stack with audio out to headphones — both run on
real FM-1s.

## 1. The hardware: two radios, one antenna, and it is wired

- The AC791N ("WL82" in the SDK) is announced as Wi-Fi 802.11b/g/n plus
  **dual-mode Bluetooth V2.1 to V5.0**: classic (BR/EDR) *and* low energy (BLE),
  +15 dBm transmit, −93 dBm sensitivity (`~/fw-AC79_AIoT_SDK/README.md`, the
  chip maker's own text; no signed datasheet was found).
- Both radios exist in the registers: BLE at `0x28000-0x28054`, BR/EDR at
  `0x20000-0x201FF`, a shared analog block at `0x2FC00-0x2FCA0` (Optimist
  `docs/BLE-HW-FACTS.md` §1). **The front end is shared with Wi-Fi**: the
  Bluetooth bring-up calls the Wi-Fi RF init first and cannot skip it (§5.2).
- The antenna is on this board, because it has transmitted and received:
  fm1-dock measured **−45 dBm** on a desk and **188 messages in 20 s** against a
  Mac; Optimist held a Sony WH-1000XM4 streaming. A factory classic Bluetooth
  address is even stored in flash at `0x0E9000` (§11).
- The radio will not start without the **factory trims** kept in the stock VM
  area at flash `0x0E8000`. MOONDAW never writes there (`NOTES.md`, "Rules"), so
  they are intact. The radio's start-up tables are committed inside Optimist, so
  no V15 file is needed to build.
- Felucca, and so MOONDAW, never turn the radio on
  ([`FELUCCA-LICENSING.md`](FELUCCA-LICENSING.md), "Radio"). The only occurrence
  of "bluetooth" in `NOTES.md` is a project name.

## 2. Can we drive it as we like? Yes, without a blob

JieLi's own stack **is** closed: `~/fw-AC79_AIoT_SDK/cpu/wl82/liba/btctrler.a`
(5.16 MB) and `btstack.a` (3.06 MB), with only headers and application examples
in source. But we do not need it, and the licence objection raised on 29/10 was
wrong anyway: the SDK is **Apache-2.0**
(`~/fw-AC79_AIoT_SDK/LICENSE`), which is compatible with GPL-3.0. What really
stands in the way of that stack is technical — a port onto JieLi's RTOS, and
120-200 KiB of code.

Optimist's stack is source, ours to read, and under our own licence. Its layers,
in `firmware/src/ble/`: `ble_ll.c` (link layer), `ble_att.c` (ATT server and the
GATT database), `ble_gattc.c` + `ble_central.c` (GATT client), `ble_smp.c` +
`ble_smp_init.c` (pairing), `ble_scan.c`, `ble_aes.c`, `ble_hw_wl82.c` (the
radio driver), `ble_midi.c` (8.3 KB — the MIDI part is a thin layer on top).

Three facts read out of `btctrler.a` are used and credited (the AFH hop table and
map, the AFH instant, the key-size reduction's octet order); no vendor code was
copied (Optimist `docs/BT-CLASSIC.md` §1).

### What it costs (their measured figures, `tools/builder/costs.json`)

| option | flash | RAM | pool |
|---|---|---|---|
| `BLE` (peripheral + BLE-MIDI) | 28,680 B | 6,412 B | 0 |
| `BLE_BOND` (pairing, encrypted link) | +3,836 B | +456 B | 0 |
| `BLE_CENTRAL` (we connect out) | +19,864 B | +2,172 B | −4 |
| `BT_AUDIO` (A2DP source, classic) | 43,952 B | 6,208 B | 32,080 B |

Against ours: `firmware/app.ld` gives XIP `0x8DFBC` (581,564 B — exactly the
`flash` limit in their file, so the same app slot), RAM 72 KiB, POOL 328 KiB,
and `NOTES.md` counts about 440 KB free in the program's area. The three BLE
options together are roughly 52 KB of flash and 9 KB of RAM: there is room.

### The CPU, which is where the care is needed

- **BLE is safe.** Its two interrupts (IRQ 45 and 29) run at priority 2, **below
  the audio** (IRQ 11, priority 3). A handler takes at most 36 µs in their
  emulator at 96 MHz, once per connection interval (≥ 7.5 ms): under 1 %.
  Unmeasured on a real FM-1 (their §12, U8).
- **Classic Bluetooth cuts in front of the audio.** IRQ 40/41 run at priority
  **5**, above it, because a page answer must be reprogrammed inside its slot
  (`docs/BT-CLASSIC.md` §6.5). They have already seen the failure: with the
  render holding CPU 0 at 100 %, "bt audio was dropping" (§7). The SBC encoder
  alone costs **3.1 %** of a 360 MHz core; AAC (17-20 %) and aptX (24-25 %) do
  not fit at all.
- Our own headroom, since the second core went in on 29/10: DPEN-CHORUS went
  from **MAX 87 % to 57 %**, worst block 1,760 µs of 2,902 (`NOTES.md`).

## 3. Not limited to MIDI

Three ways of seeing it:

1. **In Optimist's stack**, the GATT database is an ordinary list of attributes
   (handles 1 to 15, of which 12-15 are the MIDI service). Adding a service of
   our own is application work. One caveat: that database is **static** and
   never changes while running (`BLE_SC_ON_SUBSCRIBE=0`, because indicating
   Service Changed made a Mac stop discovering the characteristic, 09/10). It is
   decided at build time, not live.
2. **In JieLi's SDK**, same conclusion: the GATT database is a byte array the
   application supplies
   (`~/fw-AC79_AIoT_SDK/apps/common/example/bluetooth/ble/bt_gatt_sever/le_trans_data.h`,
   `profile_data[]`), with 16- **or 128-bit** UUIDs
   (`include_lib/btstack/le/att.h:58`) and an `att_server_change_profile()`
   (`include_lib/btstack/le/ble_api.h:1209`). On the classic side it offers
   A2DP, AVRCP, HFP both ways, SPP, HID, PAN, PBAP, PNP
   (`apps/common/config/bt_profile_config.c`) — including an SPP record with a
   made-up 128-bit UUID, in that same file.
3. **Already done, outside MIDI**: A2DP source, AVRCP, an SDP server and an SBC
   encoder (`firmware/src/bt/`). Measured on an FM-1 on 24-25/10: a WH-1000XM4
   streaming, BLE MIDI to a Mac and their Mirror app, all at once.

## 4. "Bluetooth MIDI" is software, not silicon

BLE-MIDI is a standardised GATT service — a naming convention on top of generic
BLE. Service UUID `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`, characteristic
`7772E5DB-3868-4112-A1A9-F2669D106BF3`. Confirmed three ways: Optimist's
implementation (`docs/BLE-STACK.md` §5, handles 12 and 13-15), Microsoft's
Windows MIDI Services notes, and Espressif's ESP-IDF documentation.

The specification is "MIDI over Bluetooth Low Energy" 1.0, from the MIDI
Association. (It is **not** MIDI-CI, which is a different specification
altogether.)

Nothing in the hardware knows about MIDI: there are BLE baseband registers at
`0x28000-0x28054` and a 324-byte control block in SRAM, and that is all. So once
BLE is reachable, a protocol of our own is a matter of choosing other numbers.
The FM-1 speaks MIDI over Bluetooth because somebody picked those two UUIDs, not
because the chip knows what a note is.

## 5. What this would give MOONDAW

Realistic, and already proven on an FM-1 by others:

1. **A wireless keyboard or app playing MOONDAW.** A Mac or an iPhone connects
   and sends notes, with no cable and no pairing.
2. **MOONDAW going out to find a keyboard by itself**, with no computer in the
   story (central role, +20 KB of flash).
3. **Clock and transport shared** — two FM-1s agreeing on a tempo, or MOONDAW in
   step with a computer. Implemented in Optimist as CLK IN / CLK OUT BLE.
   Over Wi-Fi they went further and ported **Ableton Link**.
4. **The sound in Bluetooth headphones** (A2DP, we transmit). Proven, but this
   is the one that fights the audio ISR for the core, and latency is 150-300 ms
   end to end: fine for listening, unusable for playing.

Ruled out: **receiving** audio from a phone (only the transmit role exists, and
there is no analog input on the board anyway); **LE Audio / LC3**, which needs
Bluetooth 5.2 isochronous channels while the chip is announced at 5.0; two BLE
links at once; Secure Connections.

## 6. Turning it on: measured on this FM-1, 03/11

Everything above was read. This was run. Optimist built `--classic --profile
bt-debug --set BLE=1 --set COMPILER=0` and installed on this machine, with its
own `SYSTEM > BT MIDI > MODE` set to MAN, advertises as `FM1-9662` at **-51 dBm**
and a Mac sees it. So the radio works on this unit, with a firmware of ours, and
the rest of this section is what it took to get there.

**The factory trims really are intact.** The doc above says they must be,
because MOONDAW never writes to `0x0E8000`. Now read: the calibration area is
found at `0x0E8000`, all four records present and passing their checks, and the
radio's start-up program runs every one of its sections (the complete set is
`0000EDFC`) with no bus timeout and no bad operation. Nothing had to be
recovered.

**The radio starts before USB.** This is the one that costs a day if you get it
wrong. Stock starts the radio first, and so does Optimist -- its `main.c` runs
`ble_midi_init()` in the `DUAL_STOCK_NOW` path "before the audio, IRQs still
on", ahead of `usb_start()`. Put `usb_start()` first, as looked prudent to me
(a radio start-up that hangs with no USB yet leaves no way to reinstall), and
the radio programs perfectly, sits in its advertising state, takes its
interrupts at the advertising interval -- and transmits nothing. The boot guard
already covers the case the other order was defending against.

**The supply is more than two levels.** `firmware.c` raises VDC14 to 7 and
SYSVDD to 15; stock's own start-up also hands the 1.4 V rail from the LDO to
the DC-DC, sets `VD14_HD_SEL` and VDDIOM, arms the 2.6 V low-voltage detector,
and biases the **Bluetooth** DC-DC (`P3_ANA_CON7 = 0xA5`, `CON8 = 0x96`).
Optimist has the sequence in `hal/fm1_supply.h` under `FELUCCA_STOCK_SUPPLY`,
with a read-back that says whether it took. Nothing here proves the radio needs
the whole of it, but a regulator the radio has of its own is not a thing to
leave unconfigured.

(Separately, and not about the radio: until 03/11 those two levels were raised
only inside `#if FM1_CPU1`, so `MOONDAW_CPU1=0` -- the fallback build -- ran at
the boot loader's 1.40 V and 1.26 V. One core reads the flash and the SRAM as
much as two. Fixed in `firmware.c`.)

**Do not strip the TIMER5 interrupt.** All of the USB SIE traffic lives there,
enumeration included; `firmware.c` says so and means it literally. A firmware
that drops the USB poll from that ISR boots, draws its screen and does not
exist for the host -- no MIDI port, nothing in `ioreg` -- and the only way back
is UBOOT by hand.

**The CDC console looks dead on macOS.** `usb.c` gates `ep3_tx()` on
`cdc.dtr`, and macOS opens the port without ever sending
`SET_CONTROL_LINE_STATE`, so the boot trace sits in the ring and nothing comes
out. Dropping the gate costs nothing: `ep3_tx()` returns at once while the
previous packet is unconsumed, so writing to a host that is not reading loses
nothing. MOONDAW ships `FELUCCA_CDC 0`, so this only bites whoever turns it on
-- and anyone bringing up a radio will want to.

**Two sizes that are not what they look like.** `BLE_LL_MAX_OCTETS` sizes the
engine's transmit buffers as well as the data length extension, so lowering it
to 27 to avoid untested DLE will silently stop advertising if the advertising
payload is longer than that. And the baseband RAM block the engine reads and
writes is placed by the linker: forcing it to a coarser alignment moved it and
stopped the advertising outright. The engine cares where that block lands, in
some way not written down anywhere found.

**Cost, as built here.** Peripheral role only, no central, no bonding, no
BLE-MIDI: about 13 KB of image over the same firmware without it, on top of the
5,795-byte packed radio start-up program. Optimist's own figures for its full
peripheral role (28.7 KB flash, 6.4 KB RAM) include the parts left out here.

**One thing that did not come out clean.** A firmware of mine that is not
Optimist got the radio advertising and then, on a later boot of the same image,
did not -- with the console showing the start-up landing differently from one
boot to the next (the VCO band search finding a crossing or not, the receive
interrupt firing or staying put). Optimist is steady on this same unit, so a
repeatable bring-up is clearly reachable; whatever makes it repeatable is not
in this document. Worth knowing before anyone budgets a weekend for it.

## 7. A trap if a companion tool is ever written on the Mac

Measured on 03/11/2026 with a signed probe and a phone scanner: **macOS silently
drops manufacturer-specific data** from `CBPeripheralManager` advertising.
`startAdvertising` returns no error, `isAdvertising` goes true, nothing is logged
— and the bytes never go on air. Only the local name and the service UUIDs do.

So a Mac cannot stand in for a BLE device that has to be recognised by its
advertisement. Linux with BlueZ can; so can the FM-1, since Optimist builds its
advertising bytes by hand -- confirmed on air on 03/11 (section 6).

A second trap on the same machine, and this one wastes an evening quietly: **a
bare `python3` cannot scan at all.** It carries no
`NSBluetoothAlwaysUsageDescription`, so CoreBluetooth does not prompt -- it
aborts the process with `SIGABRT` and prints nothing, not even on stderr. The
crash report in `~/Library/Logs/DiagnosticReports` is the only place that names
the reason. Run such a tool from a terminal that has been granted Bluetooth in
System Settings; the responsible app's permission is what counts, and the same
applies to anything else built on bleak.

And when a scanner does run: **do not take the first device it finds.** Several
devices can advertise the same service, and a tool that assumes one will tell
you confidently about the wrong one. Match on something that identifies the
device you mean.

## 8. Corrections to `audio-entree.md`

That note (29/10) says three things that no longer hold:

1. "A GPL-3.0 firmware like MOONDAW cannot link [JieLi's stack]" — **wrong**:
   the SDK is Apache-2.0, which is GPL-3.0 compatible. The obstacles are
   technical, not legal.
2. "Writing a classic Bluetooth stack and a BR/EDR driver from scratch […] is a
   matter of months, with no guarantee at all" — **it has been done since**, and
   it runs on hardware.
3. Its conclusion "Bluetooth: do nothing for audio" still stands for *audio in*,
   but the wider "do nothing" deserves reopening.

## 9. What is not settled

- **The real cost on an FM-1 is unmeasured**: neither the BLE interrupt
  durations nor the software AES-CCM. The 36 µs figure is from an emulator.
- **The analog side is unmeasured**: transmit power, sensitivity, drift.
- One boot that never came back (10/10), not reproduced.
- Every figure here from Optimist and fm1-dock is **their** measurement, read in
  their repositories, not repeated here.
- No official AC791N datasheet was found; the radio specifications come from the
  SDK's README, i.e. from the chip maker, which is solid but not a signed
  document.
- **What makes the bring-up repeatable** is not understood (section 6, last
  paragraph). Optimist's is; a firmware of mine that is not Optimist got the
  radio advertising on one boot and silent on the next, from the same image.
- The placement rule for the baseband RAM block is not written down anywhere
  found; it was only observed that moving it breaks transmission.

## Sources

- **Measured here, 03/11**: section 6, and the second and third traps of section
  7. An FM-1, a Mac, Optimist built and installed, its console read over CDC.
- Local: `~/fw-AC79_AIoT_SDK` (`README.md`, `LICENSE`, `cpu/wl82/liba/btctrler.a`,
  `btstack.a`, `include_lib/btstack/le/att.h`, `ble_api.h`,
  `apps/common/config/bt_profile_config.c`,
  `apps/common/example/bluetooth/ble/bt_gatt_sever/le_trans_data.h`);
  `firmware/app.ld`, `NOTES.md`, [`FELUCCA-LICENSING.md`](FELUCCA-LICENSING.md),
  [`audio-entree.md`](audio-entree.md).
- **Optimist**: <https://github.com/w0ts/optimist> (GPL-3.0) —
  `docs/BLE-STACK.md`, `docs/BLE-HW-FACTS.md`, `docs/BT-CLASSIC.md`,
  `firmware/src/ble/`, `firmware/src/bt/`, `tools/builder/costs.json`.
- **fm1-dock**: <https://github.com/joakimeriksson/fm1-dock> — `PLAN.md`
  ("BLE-MIDI"), `firmware/src/ble/README.md`.
- BLE-MIDI UUIDs: <https://microsoft.github.io/MIDI/kb/ble-midi-transport-architecture/>,
  <https://docs.espressif.com/projects/esp-iot-solution/en/latest/bluetooth/ble_midi.html>.
