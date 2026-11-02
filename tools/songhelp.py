#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""proto/song-help.html -- SONG wearing the help. Drawn first, then built.

Nothing here is firmware. The measurements are SONG's own (a 16 px head,
then rows of 28, GRID_ROW in app/grid.h) read off a picture of the real
screen, proto/song-help/song.png; the overlay is drawn over them.

The shape was settled on 02/11 -- a foot of two rows of six, not a frame
on four sides -- and built the same evening (app/coat.h). The drawings
below are what was argued from; what the machine then changed is said at
the foot of the page.

    python3 tools/songhelp.py            # writes proto/song-help.html
    python3 tools/songhelp.py --inline F # the same, pictures carried inside
"""
import base64
import io
import pathlib
import sys

W = 240
HEAD, ROW = 16, 28
GUTTER = 46                       # the phrase numbers' column
COLW = (W - GUTTER) / 6.0
TRACKS = ["KICK", "SNARE", "BASS", "CHOR", "LEAD", "ROBO"]
CELLS = [["--", "--", "00", "00", "--", "00"],
         ["00", "00", "01", "01", "--", "--"],
         ["01", "00", "01", "01", "00", "01"],
         ["--", "--", "00", "00", "--", "00"],
         ["01", "00", "01", "01", "01", "02"],
         ["00", "00", "02", "01", "00", "--"],
         ["--", "--", "00", "00", "01", "03"],
         ["01", "00", "02", "01", "01", "03"]]

BG, TEXT, DIM, ACC, ON_ACC = "#000000", "#FFFFFF", "#5A6470", "#7FD962", "#0B1409"
TITLE, TRACK, FAINT = "#62C8D9", "#282828", "#4C4C4C"

# The twelve, in the panel's own order: two rows of six, as under the thumb.
PANEL = [["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"],
         ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"]]

# Six characters a cell, at CONDENSED 12 in 40 px. PATTERN and SECTION do not
# fit side by side -- the first drawing had them touching -- so they are OPEN
# and SECT. here. The abbreviation is the thing to shoot down, not the layout.
ON_CELL = {"FX": "MIXER", "SEL": "OPEN", "ENV": "SECT.", "EDIT": "KEYS", "GLO": "PROJ.",
           "HOME": "BACK", "SAVE": "WRITE", "PLAY": "FROM 03"}
ON_NUMBER = {"FX": "MIXER", "SEL": "STEPS", "ENV": "SECT.", "LFO": "DELETE", "EDIT": "KEYS",
             "GLO": "PROJ.", "HOME": "BACK", "SAVE": "WRITE", "SEQ": "COPY", "PLAY": "FROM 03"}
BLACK_KEYS = [("OP1", "COPY"), ("OP2", "PASTE"), ("OP3", "CUT"),
              ("OP4", "CLEAR"), ("OP5", "SELECT"), ("OP6", "LOOP"), ("MONO", "SOLO")]


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;")


def txt(x, y, s, fill, cls="c20", anchor="middle"):
    return (f'<text x="{x:.1f}" y="{y:.1f}" text-anchor="{anchor}" class="{cls}" '
            f'fill="{fill}">{esc(s)}</text>')


def grid(rows, cur_row, cur_col):
    """The working view: the head of track names, then `rows` phrase rows.
    cur_col is None when the cursor sits on the phrase number itself."""
    o = [f'<rect x="0" y="0" width="{W}" height="{HEAD + rows * ROW}" fill="{BG}"/>']
    for i, t in enumerate(TRACKS):
        o.append(txt(GUTTER + COLW * (i + 0.5), 12, t, TEXT if i == (cur_col or 0) and cur_col is not None else DIM, "c12"))
    o.append(f'<line x1="{GUTTER - 1}" y1="{HEAD}" x2="{GUTTER - 1}" y2="{HEAD + rows * ROW}" stroke="{TRACK}"/>')
    for r in range(rows):
        y, here = HEAD + r * ROW, r == cur_row
        if here:
            o.append(f'<rect x="0" y="{y + 1}" width="{W}" height="{ROW - 2}" fill="{ACC}"/>')
        ink = ON_ACC if here else TEXT
        o.append(txt(22, y + 21, "%02d" % r, ink, "c20"))
        for i, v in enumerate(CELLS[r]):
            o.append(txt(GUTTER + COLW * (i + 0.5), y + 21, v, ink if v != "--" else (ON_ACC if here else DIM)))
        if here and cur_col is not None:  # the cell under the cursor, boxed
            x = GUTTER + COLW * cur_col
            o.append(f'<rect x="{x + 3:.1f}" y="{y + 3}" width="{COLW - 6:.1f}" height="{ROW - 6}" '
                     f'fill="none" stroke="{ON_ACC}" stroke-width="1.5"/>')
        if here and cur_col is None:
            o.append(f'<rect x="3" y="{y + 3}" width="38" height="{ROW - 6}" fill="none" '
                     f'stroke="{ON_ACC}" stroke-width="1.5"/>')
    return o


def footer_buttons(words):
    """Two rows of six at the foot: the panel's own geography, so the cell
    you read is the button under your thumb. Dark = nothing here."""
    o, top = [], W - 2 * ROW
    o.append(f'<line x1="0" y1="{top}" x2="{W}" y2="{top}" stroke="{TRACK}"/>')
    for r, row in enumerate(PANEL):
        for c, name in enumerate(row):
            x, y, w = c * (W / 6.0), top + r * ROW, W / 6.0
            said = words.get(name)
            o.append(f'<rect x="{x:.1f}" y="{y}" width="{w:.1f}" height="{ROW}" fill="none"/>')
            o.append(txt(x + w / 2, y + 11, name, TITLE if said else FAINT, "c10"))
            if said:
                o.append(txt(x + w / 2, y + 24, said, TEXT, "c12"))
    return o


def footer_keys():
    """EDIT held: the twelve are not what the hand is about to press, so the
    foot gives the black keys instead, and the two EDIT answers to besides."""
    o, top = [], W - 2 * ROW
    o.append(f'<line x1="0" y1="{top}" x2="{W}" y2="{top}" stroke="{TRACK}"/>')
    w = W / 7.0
    for i, (k, said) in enumerate(BLACK_KEYS):
        x = i * w
        o.append(txt(x + w / 2, top + 11, k, ACC, "c10"))
        o.append(txt(x + w / 2, top + 24, said, TEXT, "c12"))
    o.append(txt(6, top + ROW + 19, "REC  metronome", DIM, "c12", "start"))
    o.append(txt(W - 6, top + ROW + 19, "SELECT  speed 25-100%", DIM, "c12", "end"))
    return o


def panel(rows, cur_row, cur_col, foot):
    o = [f'<svg class="scr" viewBox="0 0 {W} {W}" xmlns="http://www.w3.org/2000/svg">',
         f'<rect x="0" y="0" width="{W}" height="{W}" fill="{BG}"/>']
    o += grid(rows, cur_row, cur_col)
    o += foot
    return "\n".join(o) + "\n</svg>"


STYLE = """<meta charset="utf-8">
<title>SONG, wearing the help</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;600&family=Barlow+Condensed:wght@500;600;700&display=swap">
<style>
:root{ --page:#070A0F; --rule:#1B2231; --prose:#8E99A8; --prose-hi:#D6DCE4;
  --dim:#5A6470; --accent:#7FD962; --title:#62C8D9; --mute:#9E5555;
  --mono:'IBM Plex Mono', ui-monospace, Menlo, monospace;
  --cond:'Barlow Condensed','Helvetica Neue',Arial,sans-serif;
  --sf:-apple-system, BlinkMacSystemFont,'SF Pro Text',system-ui,sans-serif; color-scheme:dark; }
*,*::before,*::after{ box-sizing:border-box; }
body{ margin:0; background:var(--page); color:var(--prose); font-family:var(--sf);
  font-size:15px; line-height:1.55; -webkit-font-smoothing:antialiased; }
.wrap{ max-width:880px; margin:0 auto; padding-inline:20px; padding-block:52px 96px; }
.eyebrow{ font-family:var(--mono); font-size:11px; font-weight:600; letter-spacing:.18em;
  text-transform:uppercase; color:var(--dim); }
h1{ margin:16px 0 0; font-family:var(--cond); font-weight:700; text-transform:uppercase;
  font-size:clamp(36px,6vw,56px); line-height:.98; color:var(--prose-hi); }
h1 em{ font-style:normal; color:var(--accent); }
.lede{ max-width:64ch; margin:18px 0 0; font-size:16px; }
.lede strong, p b{ color:var(--prose-hi); font-weight:600; }
code{ font-family:var(--mono); font-size:.9em; color:var(--title); }
h2{ margin:0; font-family:var(--cond); font-weight:700; text-transform:uppercase;
  font-size:30px; color:var(--prose-hi); line-height:1; }
h2 em{ font-style:normal; color:var(--accent); }
section{ margin-top:72px; }
section > p{ max-width:64ch; margin:14px 0 0; }
.tag{ font-family:var(--mono); font-size:10px; font-weight:600; letter-spacing:.16em;
  text-transform:uppercase; color:var(--title); display:block; margin-bottom:8px; }
ul.notes{ margin:14px 0 0; padding:0; list-style:none; max-width:68ch;
  display:flex; flex-direction:column; gap:10px; }
ul.notes li{ padding-left:16px; position:relative; font-size:14.5px; }
ul.notes li::before{ content:""; position:absolute; left:0; top:.62em; width:7px; height:2px; background:var(--dim); }
.pair{ display:flex; gap:22px; flex-wrap:wrap; margin-top:26px; align-items:flex-start; }
figure{ margin:0; display:flex; flex-direction:column; gap:10px; }
figcaption{ font-family:var(--mono); font-size:11px; color:var(--dim); line-height:1.5; max-width:300px; }
.scr, figure img{ display:block; width:300px; height:300px; border-radius:4px;
  box-shadow:0 0 0 2px #000, 0 24px 50px -18px rgba(0,0,0,.8); }
figure img{ image-rendering:pixelated; }
.scr text{ font-variant-ligatures:none; }
.scr .c20{ font-family:var(--cond); font-weight:600; font-size:19px; }
.scr .c12{ font-family:var(--cond); font-weight:600; font-size:12px; letter-spacing:.02em; }
.scr .c10{ font-family:var(--cond); font-weight:600; font-size:10px; letter-spacing:.06em; }
</style>
"""


def main():
    def real(name, alt):
        b = pathlib.Path("proto/song-help", name + ".png").read_bytes()
        return ('<figure><img src="data:image/png;base64,%s" alt="%s">'
                % (base64.b64encode(b).decode(), alt))

    today = real("song", "SONG as it is: eight rows of phrases")

    p = [STYLE, '<div class="wrap">',
         '  <div class="eyebrow">MOONDAW &middot; FM-1 &middot; drawn and then built, 2 November 2026</div>',
         '  <h1>SONG, <em>wearing the help</em></h1>',
         '''  <p class="lede">The help stops being a screen and becomes a <strong>coat</strong>: you
  switch it on, the view stays the view, and the foot of the panel says what the twelve buttons do
  <strong>right now</strong> &mdash; which changes as the cursor moves. It stays on until you switch
  it off.</p>
  <p class="lede"><b>These drawings are what it was argued from, and it is now on the machine</b>
  (<code>app/coat.h</code>). They are kept as drawn: the picture on the left below is the real
  SONG of that morning, everything with a foot on it is by hand, over SONG's own measurements
  &mdash; a 16&nbsp;px head and rows of 28 (<code>GRID_ROW</code>). <b>What the panel then changed
  is at the foot of this page</b>, because that is the part no drawing could have told us.</p>''']

    p += ['  <section>', '    <span class="tag">On the machine</span>',
          '    <h2>And here it is <em>for real</em></h2>',
          '''    <p>Out of the firmware the same evening, not drawn. The cursor moves one column and
    three cells change: this is the whole argument in two pictures.</p>''',
          '    <div class="pair">',
          '      ', real("coat-number", "SONG with the foot: the cursor on the phrase number, LFO DELETE and SEQ COPY live"),
          '        <figcaption>On the phrase number. SEL says STEPS; LFO and SEQ are awake, '
          'DELETE and COPY.</figcaption></figure>',
          '      ', real("coat-cell", "SONG with the foot: the cursor on a pattern cell, LFO and SEQ dark"),
          '        <figcaption>One column right, on a cell. SEL says PATTERN; LFO and SEQ have gone '
          'dark, because here they do nothing.</figcaption></figure>',
          '    </div>',
          '    <div class="pair">',
          '      ', real("coat-edit", "SONG with the foot showing the black keys while EDIT is held"),
          '        <figcaption>EDIT held. The twelve step aside for the OP keys, MONO, and EDIT + '
          'SELECT &mdash; true on every screen.</figcaption></figure>',
          '      <div style="flex:1 1 300px;min-width:280px">',
          '''        <p>The rules between the cells are not in the drawings: they were added because
        of what the panel did without them. <b>SEL and ENV, PATTERN and SECTION, ran into one
        word.</b></p>''',
          '      </div>', '    </div>', '  </section>',
          '  <section>', '    <span class="tag">The cost, first &mdash; as it was argued</span>',
          '    <h2>Eight rows <em>become six</em></h2>',
          '''    <p>SONG fills the panel to the pixel: a 16&nbsp;px head and eight rows of 28 make
    exactly 240. There is no spare room, so the help has to <b>take</b> some. Two rows of 28 at the
    foot &mdash; one per row of the panel &mdash; leaves six phrases visible instead of eight.</p>
    <p><b>The foot, and not a frame all round &mdash; chosen 02/11.</b> A column down each side
    would cost a third of the width to hold two labels nobody can read sideways. The foot costs
    less and buys more, because the panel's buttons are <b>two rows of six</b>: draw two rows of
    six and the cell you read sits where the button sits under your thumb. There is nothing to
    look up &mdash; top right on the screen is top right under your hand.</p>''',
          '    <div class="pair">',
          f'      {today}<figcaption>Today. Eight phrases, no room spare.</figcaption></figure>',
          '      <figure>', panel(6, 3, 0, footer_buttons(ON_CELL)),
          '        <figcaption>The help on, the cursor on a pattern cell. Six phrases, and the '
          'twelve named at the foot.</figcaption></figure>',
          '    </div>', '  </section>']

    p += ['  <section>', '    <span class="tag">What makes it worth the two rows</span>',
          '    <h2>It changes <em>under the cursor</em></h2>',
          '''    <p>A fixed list of shortcuts is what we already have, and it is why you were not
    convinced. This is not that: move the cursor off the pattern cell onto the <b>phrase number</b>
    and two dark cells wake up &mdash; SEQ copies the phrase below, LFO deletes it &mdash; while
    SEL stops saying PATTERN and starts saying STEPS. <b>The screen tells you what is possible
    here, not what exists somewhere.</b></p>''',
          '    <div class="pair">',
          '      <figure>', panel(6, 3, 0, footer_buttons(ON_CELL)),
          '        <figcaption>On a cell. SEQ and LFO are dark: they do nothing here.</figcaption></figure>',
          '      <figure>', panel(6, 3, None, footer_buttons(ON_NUMBER)),
          '        <figcaption>On the number. Two cells light, SEL changes its word.</figcaption></figure>',
          '    </div>',
          '''    <p>Dark is information too. A cell that never lights on this screen teaches the
    machine's shape faster than a page of prose: ARP and REC do nothing in SONG, and you learn it
    without being told.</p>''', '  </section>']

    p += ['  <section>', '    <span class="tag">EDIT held</span>',
          '    <h2>The foot <em>changes hands</em></h2>',
          '''    <p>While EDIT is down the twelve are not what you are about to press &mdash; the
    black keys are. So the foot swaps: the six OP keys and MONO, in the order they lie under the
    fingers, and the two things EDIT answers to on the panel. It goes when EDIT goes.</p>
    <p>This is the piece I would build first even if you drop the rest. <b>EDIT held is exactly the
    second you cannot remember whether cut is OP3 or OP4</b>, and it needs no mode at all: you are
    already holding the button that asks for it.</p>''',
          '    <div class="pair">',
          '      <figure>', panel(6, 3, 0, footer_keys()),
          '        <figcaption>EDIT held, over the same SONG. The twelve step aside.</figcaption></figure>',
          '      <div style="flex:1 1 300px;min-width:280px">',
          '''        <ul class="notes">
          <li><b>It could be permanent.</b> Nothing says this bar has to wait for the help to be on.
          Held EDIT is a deliberate gesture; showing the legend every time costs two rows for as
          long as the thumb is down, and no mode to remember.</li>
          <li><b>MONO sits at the right</b> because that is where it lies on the keyboard, well
          past OP6. The order on screen is the order under the hand.</li>
        </ul>''',
          '      </div>', '    </div>', '  </section>']

    p += ['  <section>', '    <span class="tag">Still open</span>',
          '    <h2>Four things <em>to shoot down</em></h2>',
          '''    <ul class="notes">
      <li><b>I kept SEL, against what you said.</b> You called it obvious, and on most screens it
      is &mdash; but in SONG it is the one control that changes meaning four times: a pattern, a
      phrase's steps, what a REF plays, a section's name. Either it earns its cell or we admit the
      cell grid has a hole in it. Your call.</li>
      <li><b>GLO is the project, not the mixer.</b> FX opens the mixer. The example in your message
      had them swapped, and it is worth knowing it was the panel that misled you, not your memory:
      that is an argument for the coat.</li>
      <li><b>Six characters a cell, and the drawing proved it.</b> 40&nbsp;px at CONDENSED 12.
      The first version of this page had SEL say PATTERN and ENV say SECTION, and the two words
      ran into each other on the screen. They are OPEN and SECT. here. <b>SECT. is the failure
      mode</b>, not the layout: an abbreviation nobody reads is worse than a dark cell, and if the
      vocabulary cannot be built at six characters the foot wants three rows of four instead &mdash;
      which costs a third row and breaks the match with the panel. That is the trade to settle on
      the machine.</li>
      <li><b>OCT&minus; and OCT+ are not in the grid.</b> They are not on the two rows of six &mdash;
      they live by the knobs, left of the screen. Putting them in the grid would break the one rule
      that makes it work.</li>
    </ul>''', '  </section>']

    p += ['  <section>', '    <span class="tag">What the panel changed</span>',
          '    <h2>Two things <em>the drawing could not say</em></h2>',
          '''    <ul class="notes">
      <li><b>The twelve read as one word.</b> SEL says PATTERN and ENV says SECTION, side by side,
      each filling its cell: on the panel they ran together. The drawing had warned that six
      characters was the budget &mdash; it had not shown that two full cells touching is unreadable
      even when each word fits. <b>There is a rule between the cells now</b>, which also makes the
      grid read as a grid, which was the point of it.</li>
      <li><b>A word that will not fit drops to CONDENSED 11 before it is given up.</b> PATTERN and
      PROJECT are both over the budget by a hair. The first build dropped them and left two cyan
      names with nothing under them &mdash; a cell saying a button does something and refusing to
      say what, which is the worst cell on the panel. It is still never shortened: past 11 the cell
      goes dark, name and all.</li>
      <li><b>The CAPS LOCK became visible, unasked.</b> EDIT tapped latches the black keys on in
      SONG. The foot follows the latch and not the thumb, so it stays on the OP keys until EDIT is
      tapped again &mdash; a state only the EDIT LED used to say.</li>
    </ul>''', '  </section>',
          '  <section>', '    <span class="tag">The real cost, said plainly</span>',
          '    <h2>Every screen <em>has to keep its word</em></h2>',
          '''    <p>The drawing is the easy half. The hard half is that each screen has to name its
    buttons, and go on naming them correctly as the cursor moves and the code changes. Each screen
    already declares <i>which</i> buttons are live &mdash; that is the mask behind the LEDs
    (<code>ui_led_guide</code>). What does not exist is the <b>word</b>, and the word that changes
    with the cursor.</p>
    <p>That is where this could turn into the over-complicated thing you did not want. A line
    forgotten and the help lies, which is worse than no help. <b>SONG is the one screen that wears
    it</b>, and the others keep their eight rows and show nothing, switched on or not &mdash; on
    purpose, until keeping one screen true turns out to be cheap or a chore.</p>''', '  </section>', '</div>', '']

    out = pathlib.Path("proto/song-help.html")
    html = "\n".join(p)
    io.open(out, "w", encoding="utf-8").write(html)
    print(out)
    if "--inline" in sys.argv:          # the pictures are already carried
        where = pathlib.Path(sys.argv[sys.argv.index("--inline") + 1])
        where.parent.mkdir(parents=True, exist_ok=True)
        io.open(where, "w", encoding="utf-8").write(html)
        print(where)


if __name__ == "__main__":
    main()
