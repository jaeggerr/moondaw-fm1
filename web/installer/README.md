# The web installer

A static page that puts MOONDAW on an FM-1 from Chrome or Edge, over
WebMIDI: no Python, no command line. It can also save the songs on the
FM-1 to the computer, and put the official V15 back.

It is Felucca's web installer, adapted: the same update protocol, now kept
step for step and byte for byte equal to `tools/fm1_install.py`.

| File | What it is |
| --- | --- |
| `index.html` | the page |
| `fm1ota.js` | the update over WebMIDI: the JS twin of `tools/fm1_install.py` |
| `fm1pkg.js` | `.fwsc` packages: identity, the image the device reads, what may be installed |
| `mdlink.js` | MOONDAW's song link (`app/link.h`): ping, list, read; a ZIP writer |
| `make_site.py` | the site to publish: the page, the package, its manifest, the licences |
| `test/harness.mjs` | the JS against a simulated FM-1, for `tools/installer_compare.py` (not published) |

## Serving it

The page needs a secure context for WebMIDI: `https://`, or `localhost` /
`127.0.0.1`. Opening `index.html` as a file does not work (ES modules).

To try it with the package you just built:

```
./build.sh
python3 web/installer/make_site.py build/moondaw.fwsc 0.1-dev /tmp/moondaw-site
python3 -m http.server -b 127.0.0.1 -d /tmp/moondaw-site 8793
```

and open <http://127.0.0.1:8793/> in Chrome. Serving `web/installer/`
itself works too (`python3 -m http.server -d web/installer 8793`): then no
package comes with the page and the user picks a `.fwsc` (the console shows
one 404, for `firmware/manifest.json`; that is how the page finds out).

### A release

```
python3 web/installer/make_site.py build/moondaw.fwsc 1.0 site --source https://…/moondaw-fm1
```

`site/` is then published as it is (GitHub Pages, any static host):

```
site/index.html, fm1ota.js, fm1pkg.js, mdlink.js
site/firmware/moondaw-1.0.fwsc
site/firmware/manifest.json      {name, version, product, file, size, sha256, source}
site/firmware/LICENSE, LICENSES/
```

The package lives **on the same site as the page**: the page reads it with
`fetch()`, and a GitHub Releases asset, served from another host without
CORS headers, cannot be read that way. The release can still attach the
same `.fwsc` for people who use `tools/fm1_install.py`. The page checks the
downloaded package against the manifest's SHA-256 and identity.

`make_site.py` refuses a package whose identity is not MOONDAW's
(`FM-1_933` and the version) or that does not carry Felucca's update loader (no vendor files are
ever published). The package holds three JieLi SDK files under Apache-2.0:
`LICENSES/` travels with it. `--source` is the link to the source code that
GPL-3.0 asks for; without it the page shows no source link.

## Using it

1. Plug the FM-1 straight into the computer with a data cable, switch it on,
   close other apps that use MIDI.
2. **Connect** (optional): Chrome asks for MIDI with SysEx; the page shows
   what the FM-1 runs (`FM-1_015` official V15, `FM-1_917` Felucca 1.7,
   `FM-1_933…` MOONDAW, `FM-1_900` MOONDAW 0.1, `ota-…` an update that was
   cut off).
3. **Install MOONDAW**: one confirmation, then about a minute. The page
   follows the FM-1 through its restart into the update loader and back,
   and checks that it then reports the package's own identity.

An install that was cut off (cable pulled, page closed) leaves the FM-1 in
its update loader; pressing Install again finishes it, as `fm1_install.py`
does.

- **Save the songs on the FM-1**: with MOONDAW on its MENU screen, every song
  is read over the song link and saved as `moondaw-songs-DATE.zip` (one
  `.trk` per song, as `tools/fm1_send.py --get` writes them).
- **Return to the official firmware**: the user picks the unmodified V15
  (`FM-1_015.fwsc`, 699 956 bytes; in M-VAVE's `m-vave/fm1_open` repository,
  `sdk/firmware/`, since m-vave.com now serves V16). Only that exact file
  (SHA-256 pinned, as in `fm1_install.py`) is accepted. If the FM-1 runs
  MOONDAW and the songs were not saved from the page, it asks once more:
  the official firmware's storage covers the flash where MOONDAW keeps them.

### What the browser cannot do: the rescue

An FM-1 that does not start has no USB-MIDI, so no web page reaches it.
After two failed starts (or OCT− and OCT+ held 5 s on MOONDAW) the chip
enters its mask-ROM update mode, `WL80UBOOT1.00` (4C4A:8057). In that mode
it is a **USB mass-storage** device: WebUSB refuses that class, and the
operating system's disk driver holds it anyway. `fm1_rescue.py`
(felucca-bsp, `tools/`) gets there with pyusb and sudo, after detaching
macOS's driver; FM-1-transporter with a XIAO RP2040 is the last resort.
The page only says so. (The SysEx soft key `F0 22 24 35 7D F7`, which sends
a running MOONDAW into that mode for `fm1_rescue.py`, is never sent by the
page; the install uses the update command `F0 22 24 35 7F F7`, as
`fm1_install.py`.)

## What changed from Felucca's page

- MOONDAW's names, identity and credits, and the skin of moondaw.com: its
  ground, its fonts, its wordmark and its header. The page is published at
  <https://moondaw.com/install/>, and someone who follows the button there
  should not feel they have left the site.
- Two languages, French and English, the way the site's other pages do it:
  the markup ships in French, a table in the page swaps it, and the choice
  is kept under the same `localStorage` key — reading the site in English
  and following the button lands here in English. Felucca's page was
  English only, and so was this one until 31/10. What is NOT translated is
  the machine's own words: the identities, the file names, the log.
- `fm1ota.js` rewritten to follow `fm1_install.py` function for function:
  one `install()` that finishes a cut-off update instead of a separate
  resume, the Windows port pairing, the 0.3 s wait after opening a port,
  printable identities, the progress by address, the same error codes.
- Packages are checked as `fm1_install.py` does without `--force`:
  Felucca's loader marker in them, or the official V15's SHA-256.
- Felucca's patch of an official package (`buildFeluccaPackage`) is gone:
  MOONDAW packages are made from scratch by `tools/fm1pkg_make.py`.
- Felucca's full backup before returning to V15 (Felucca's own archive
  protocol, `fm1backup.js`, which was never copied here) is replaced by
  the MOONDAW song backup.
- No build step: the page loads its modules as they are.

## What is verified, and what is not

Without an FM-1, on 29/10/2026:

- `tools/installer_compare.py [PKG] [--stock FM-1_015.fwsc]` runs
  `fm1_install.py` and the page's JS against the same simulated FM-1 and
  compares **every message the host sends, byte for byte**, with the steps
  reported and the outcome. Eleven cases per package: an install from V15,
  over MOONDAW, finishing a cut-off update, no FM-1, wrong model, the FM-1
  stopping in step 1, a request outside the package, the cable pulled in
  step 2, no loader, no restart, another identity after the install. For
  `build/moondaw.fwsc` that is 6 453 messages (about 3.8 MB); for the
  official V15, 7 333. All identical. The identity and the logical image
  (SHA-256) are the same; so are `parse_identity`, `parse_request` and
  `response` on fixed and 200 random packets (damaged ones included).
  A deliberate one-bit change in the JS is caught.
- The song link: `pack7`/`unpack7` and the messages equal `fm1_send.py`'s;
  a list-and-read session against a simulated link sends the same messages;
  and against the simulator (`build/sim/moondaw-fm1`, the firmware's own
  `app/link.c` over UDP) the page and `fm1_send.py` read the same ten songs,
  100 208 bytes. The ZIP is read back by Python's `zipfile`.
- The page served by `python3 -m http.server`, in Chromium: loads with no
  console error, the bundled package is checked against its manifest, a
  bad file and the V15 picked in the wrong place are refused, the V15 is
  verified in the return section, the layout holds at 375 px. With
  `navigator.requestMIDIAccess` replaced by a simulated FM-1, the Install
  button goes through confirmation, both steps, the restart and "Done", and
  Save songs downloads a ZIP of the songs.

Not verified (needs the FM-1):

- WebMIDI on a real FM-1: port names, Chrome's SysEx permission prompt,
  the timing. `fm1_install.py` installed `hello` and MOONDAW on 28/10, and
  Felucca's own page installed Felucca 1.7 over WebMIDI the same day; this
  page sends the bytes of the first through the browser of the second, but
  has not run itself.
- That Chrome sees the loader's port come back after the restart into the
  update loader, and the firmware after the second restart (the page looks
  for them again every second, as `fm1_install.py`).
- Returning to V15 *from MOONDAW* (done from Felucca on 28/10, not from
  MOONDAW, which runs the same step-1 code).
- The song backup on the device (only against the simulator's `link.c`).
- Installing from V14 (`FM-1_014`): never tried by anyone here; the page
  advises V15 first.
- Edge, Windows (port pairing), Linux.
- `FM-1_900`, which 0.1 went out with, is also the identity of `hello`, of
  Felucca's development builds and of two dozen firmwares on the Firmware
  Hub: nothing that reads it can tell them apart. From 0.2 MOONDAW answers
  with `FM-1_933` and its version (`FM-1_9330002`), a prefix no other
  firmware uses. The update loader keeps Felucca's `ota-FM-1_900`: it is the
  protocol's marker, hosts look for `ota-` and the model, and the rescue
  path is not worth moving for a name.

### The first run on the device

Read-only first, then one write at a time; each line is the next step only
if the one before went as said.

```
python3 tools/fm1_install.py --info                 # FM-1_9330002, port Felucca: MOONDAW, as left
python3 web/installer/make_site.py build/moondaw.fwsc 0.1-test /tmp/moondaw-site
python3 -m http.server -b 127.0.0.1 -d /tmp/moondaw-site 8793
```

In Chrome, <http://127.0.0.1:8793/>:

1. **Connect**: expect `FM-1_9330002 — MOONDAW … · port “Felucca”`. Nothing is
   written by this (the identity query only).
2. FM-1 on MENU, **Save songs**: compare the ZIP with
   `tools/fm1_send.py --get NAME` for one or two songs.
3. **Install MOONDAW** over MOONDAW (what `fm1_install.py` did last).
   Expect "Done: the FM-1 runs FM-1_9330002", and the songs still there.
4. **Return to official V15** with `~/.fm1/stock/FM-1_015.fwsc`. Expect
   `FM-1_015`, port `USB Composite Device`.
5. **Install MOONDAW** from V15.
6. Cut-off: during "Writing", pull the cable; plug it back; Connect shows
   `ota-…`; Install finishes it.

Done on 29/10 (Chrome on macOS, the FM-1 running MOONDAW): steps 1 to 3
went as said. Steps 4 to 6 are still to do.

If anything stops, `tools/fm1_install.py` (same protocol) and
`fm1_rescue.py` (docs/ARRIVEE.md, section 2) are the ways back.
