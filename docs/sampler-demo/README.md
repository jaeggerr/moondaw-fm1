# SMPL-DEMO — five sampled instruments in one song

`songs/SMPL-DEMO.trk` plays nothing but samples: a TR-909 kit (kick,
snare, closed hat, clap), a Precision bass, the Iowa piano, a marimba and
a kalimba. Fourteen samples, 126 KB of bank, 32 of the store's 73
sectors.
Am - F - C - G at 96 BPM, four arrangement steps of four bars: the three
melodic instruments alone, then bass and kick, then the full kit, then a
variation. 40 s.

`SMPL-DEMO.mp3` is that song as the FM-1's own core renders it, through
the same reverb and delay the device runs (MP3 of the WAV `fm_render`
writes; the WAV itself is not kept in the repository).

## The bank it wants

The twelve files, all from the moondaw checkout (`../moondaw`):

    instruments/tr909/tr9_kick.wav  tr9_snare.wav  tr9_chh.wav  tr9_clap.wav
    instruments/pbass/pbas_e2.wav  pbas_e3.wav
    instruments/piano9/pf_c3.wav  pf_gb3.wav  pf_c4.wav  pf_gb4.wav  pf_c5.wav
    instruments/marimba/marimba_c4.wav  marimba_c5.wav
    instruments/kalimba/kalimba_c5.wav

A song names a sample by the last part of its path without `.wav`, so
`smp 04 pbass/pbas_e2.wav` plays the bank's `pbas_e2` wherever it came
from.

## To hear it on the Mac

    M=../moondaw
    python3 tools/fm1_samples.py pack build/demo-bank.bin \
        $M/instruments/tr909/tr9_{kick,snare,chh,clap}.wav \
        $M/instruments/pbass/pbas_e{2,3}.wav \
        $M/instruments/piano9/pf_{c3,gb3,c4,gb4,c5}.wav \
        $M/instruments/marimba/marimba_c{4,5}.wav \
        $M/instruments/kalimba/kalimba_c5.wav
    cc -std=c11 -O2 tests/fm_render.c core/song.c core/trk.c core/synth.c \
        core/fx.c core/player.c core/drum.c core/sampler.c core/bank.c \
        core/store.c -o build/fm_render
    build/fm_render docs/sampler-demo/songs/SMPL-DEMO.trk \
        docs/sampler-demo/SMPL-DEMO.wav 40.5 --bank build/demo-bank.bin
    ffmpeg -y -i docs/sampler-demo/SMPL-DEMO.wav -q:a 2 \
        docs/sampler-demo/SMPL-DEMO.mp3

## To hear it on the FM-1

The bank replaces whatever bank the device holds, and it is taken only on
HOME:

    python3 tools/fm1_samples.py add $M/instruments/tr909/tr9_{kick,snare,chh,clap}.wav \
        $M/instruments/pbass/pbas_e{2,3}.wav $M/instruments/piano9/pf_{c3,gb3,c4,gb4,c5}.wav \
        $M/instruments/marimba/marimba_c{4,5}.wav $M/instruments/kalimba/kalimba_c5.wav
    python3 tools/fm1_send.py docs/sampler-demo/songs/SMPL-DEMO.trk

## What the demo cannot hide

- **A second a sample**, cut with a 5 ms fade. The piano, the bass, the
  marimba and the kalimba are all longer than that in moondaw; what
  plays here is their first second. Short notes do not notice, a held
  one does.
- **Normalised to -1 dBFS**, every sample alike: the hat came out as loud
  as the kick, and the balance in this song is the track volumes' work
  (`trk 0 vol 58 ... trk 2 vol 48`), not the files'. A kit that sounds
  balanced in moondaw will not here.
- **One-shots**: `begin`, `loop` and `ping` are carried in the file and
  not played, so pbass' loop points do nothing -- its two samples are
  one second of a plucked note, which is what a bass needs anyway.
- **How far a zone stretches its sample is audible.** The piano had
  three zones at first and some notes were pitched a whole tritone from
  their sample: he heard a cheap Casio, and he was right -- transposing
  a piano that far moves its whole attack spectrum in one block, so the
  pitch is correct and the timbre is not. Five zones (moondaw's spacing,
  `pf_gb3` and `pf_gb4` added) keep every note within three semitones,
  and the piano solo render differs from the old one by as much as the
  signal itself. The marimba is still two zones, up to five semitones,
  and is next if it starts to bother him -- `marimba_gb4`, 11 KB.
