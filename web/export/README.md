# The export page

A song out of the FM-1 and into audio: one WAV per track, or the whole thing
mixed. Published at [moondaw.com/export/](https://moondaw.com/export/).

The division it rests on: **the machine keeps the song, the computer renders
it.** The FM-1 plays a song in real time and only just, has 512 KB of RAM, and
speaks through a link that carries 120 bytes an answer — so it hands over the
`.trk` and nothing else. The browser has the engine already, since
`web/songedit/` compiles `core/player.c` and the engines to wasm. What comes
out of the file is therefore what came out of the speaker, and `test/render.mjs`
measures that rather than claiming it.

| file | what it is |
| --- | --- |
| `index.html` | the page, its two languages, and the wiring |
| `render.js` | the render: the engine driven faster than a sound card, and the whole job |
| `stems.js` | which tracks come out, what the files are called, and what "dry" means |
| `wav.js` | a WAV around the PCM, and the reader the test uses |
| `worker.js` | the job, off the page's thread |
| `build.sh` | assembles `build/export/` with the modules it borrows |
| `test/render.mjs` | the page's own modules against `tests/fm_render.c`, sample for sample |

## Running it

The page is **not** served from this directory — it borrows four modules that
are not its own, so `build.sh` assembles a flat directory where every import is
a sibling, the way `platform/web/` does:

```sh
make export                                    # build/export/
python3 -m http.server -b 127.0.0.1 -d . 8795
```

Then <http://127.0.0.1:8795/build/export/>. WebMIDI needs Chrome or Edge and a
secure context (`localhost` is one); a `.trk` dropped on the page needs
neither, and no browser is needed at all to run the test.

What it borrows: `trk.js` and `audition.wasm` from `web/songedit/`, `mdlink.js`
and `fm1ota.js` from `web/installer/`. The link is theirs byte for byte — this
page adds no protocol of its own.

## What it writes

Three choices, one at a time:

- **the tracks as they sound** — a solo render per track, its level, pan,
  reverb and echo in it, tails and all;
- **the tracks dry** — the same, the sends taken out of the *song* first (the
  faders' REV and ECH at zero, the REV and ECH step effects dropped). A render
  of another song, not an engine told to skip a stage;
- **the mixdown** — one file, master and limiter included.

Only a track the arrangement plays and that strikes a note gets a file.
16-bit stereo at 44100, no conversion anywhere. Every file of a job is the same
length — the arrangement's own at the least, plus what still rings — so a DAW
gets them in step. The tracks arrive in a `.zip`, the mixdown on its own.

One reservation the page states itself: the master limiter is on the mixdown
and not on a stem, so the stems sum to a little above the mixdown exactly where
the mixdown was being held down.

## How it is known

```sh
make export && node web/export/test/render.mjs      # also part of `make test`
```

`tests/fm_render.c` is the same engine built natively, and it renders a song
the same way. The test holds the page's output against it **sample for sample**
over the arrangement, for the mixdown and for a solo — past the arrangement's
end the two part on purpose, the native one looping while the page lets the
tail die. It also asks that the stems correlate with the mixdown above 0.99 and
sit within a dB of its level, that a track taken out of the arrangement loses
its file while the others keep their numbers, that dry really is dry, and —
where `build/demo-bank.bin` has been packed (`docs/sampler-demo/README.md`) —
that a sampled song is silent without a bank and plays with one.

Rendering twice into one wasm instance does **not** give the same bytes: the
master limiter's gain survives `player_init`. Every pass gets a fresh instance
for that reason, which costs about a millisecond and is what makes each file
reproducible on its own.

## What it needed from the firmware

One export, `aud_play_once` in `web/songedit/audition.c`: the arrangement once,
so the sequencer says when a render ends. The length of a phrase is the
player's arithmetic — swing, ticks, auto cycles, the references between phrases
— and a second copy of it in JavaScript would drift from this one.
