# Bluetooth on the FM-1: what the radio can do

03/11/2026. Research only: no code touched, nothing installed, the radio was
never turned on. This supersedes the Bluetooth section of
[`audio-entree.md`](audio-entree.md), which was written on 29/10 and is wrong on
two points (last section).

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

## 6. A trap if a companion tool is ever written on the Mac

Measured on 03/11/2026 with a signed probe and a phone scanner: **macOS silently
drops manufacturer-specific data** from `CBPeripheralManager` advertising.
`startAdvertising` returns no error, `isAdvertising` goes true, nothing is logged
— and the bytes never go on air. Only the local name and the service UUIDs do.

So a Mac cannot stand in for a BLE device that has to be recognised by its
advertisement. Linux with BlueZ can; so can the FM-1, since Optimist builds its
advertising bytes by hand.

## 7. Corrections to `audio-entree.md`

That note (29/10) says three things that no longer hold:

1. "A GPL-3.0 firmware like MOONDAW cannot link [JieLi's stack]" — **wrong**:
   the SDK is Apache-2.0, which is GPL-3.0 compatible. The obstacles are
   technical, not legal.
2. "Writing a classic Bluetooth stack and a BR/EDR driver from scratch […] is a
   matter of months, with no guarantee at all" — **it has been done since**, and
   it runs on hardware.
3. Its conclusion "Bluetooth: do nothing for audio" still stands for *audio in*,
   but the wider "do nothing" deserves reopening.

## 8. What is not settled

- **The real cost on an FM-1 is unmeasured**: neither the BLE interrupt
  durations nor the software AES-CCM. The 36 µs figure is from an emulator.
- **The analog side is unmeasured**: transmit power, sensitivity, drift.
- One boot that never came back (10/10), not reproduced.
- Every figure here from Optimist and fm1-dock is **their** measurement, read in
  their repositories, not repeated here.
- No official AC791N datasheet was found; the radio specifications come from the
  SDK's README, i.e. from the chip maker, which is solid but not a signed
  document.

## Sources

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
