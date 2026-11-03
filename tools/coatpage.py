#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""proto/coat.html -- the help worn on the screen. Built, 02-03/11.

Most of the pictures are the machine's own framebuffer (tools/coat_shots.mjs,
through build/web/moondaw.wasm). The two hand-drawn ones are kept on purpose:
they are what the shape was argued from before any of it existed, and the
page says which is which.

    python3 tools/coatpage.py            # writes proto/coat.html
    python3 tools/coatpage.py --inline F # the same, pictures carried inside
"""
import base64
import io
import pathlib
import re
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


def panel(rows, cur_row, cur_col, foot):
    o = [f'<svg class="scr" viewBox="0 0 {W} {W}" xmlns="http://www.w3.org/2000/svg">',
         f'<rect x="0" y="0" width="{W}" height="{W}" fill="{BG}"/>']
    o += grid(rows, cur_row, cur_col)
    o += foot
    return "\n".join(o) + "\n</svg>"


STYLE = """<meta charset="utf-8">
<title>The coat</title>
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


def inline(html):
    """Every picture carried in the page itself, for sending it on alone."""
    def swap(m):
        b = pathlib.Path("proto", m.group(1)).read_bytes()
        return 'src="data:image/png;base64,%s"' % base64.b64encode(b).decode()
    return re.sub(r'src="(coat/[^"]+\.png)"', swap, html)


def real(name, alt):
    """A picture of the machine's own panel (tools/coat_shots.mjs)."""
    return '<figure><img src="coat/%s.png" alt="%s">' % (name, esc(alt))


def fig(body, caption):
    return [body, '        <figcaption>%s</figcaption></figure>' % caption]


def main():
    p = [STYLE, '<div class="wrap">',
         '  <div class="eyebrow">MOONDAW &middot; FM-1 &middot; built 2&ndash;3 November 2026</div>',
         '  <h1>The help, <em>worn on the screen</em></h1>',
         """  <p class="lede">HELP was a screen you left your work to read. The coat is the other
  thing: you switch it on, the view stays the view, and the foot of the panel says what the twelve
  buttons do <strong>right now</strong> &mdash; which changes as the cursor moves. It stays on until
  you switch it off. <strong>OCT&minus; and OCT+ together</strong> put it on and take it off.</p>
  <p class="lede"><b>Four screens wear it</b>: SONG, PATTERN, MIXER, and PROJECT through two rows of
  its own. Every picture below with a foot on it is the machine's framebuffer, not a drawing &mdash;
  the two hand-drawn ones are marked, and they are the argument that came before the thing.</p>"""]

    p += ['  <section>', '    <span class="tag">The gesture</span>',
          '    <h2>The one chord <em>the panel had left</em></h2>',
          """    <p>Every button on this machine acts on its press &mdash; <code>app/ui.h</code> says
    it plainly: <b>EDIT is the only one that can be held</b>. No modifier can be pressed before a
    second button without its own action going off first: HOME&nbsp;+&nbsp;anything has already
    stepped back, SAVE&nbsp;+&nbsp;anything has already saved.</p>
    <p><b>OCT&minus; and OCT+ are the exception, and by accident.</b> Every screen reads them in the
    same frame and in opposite directions, so the two together move the keyboard's octave by exactly
    nothing. The chord is read in <code>app/app.c</code> before any screen sees the frame, and the
    two buttons are rubbed out of it, so even that nothing never happens.</p>""", '  </section>']

    p += ['  <section>', '    <span class="tag">Why it is not a list</span>',
          '    <h2>It changes <em>under the cursor</em></h2>',
          """    <p>A fixed list of shortcuts says what exists somewhere. This says what is possible
    here. The two pictures are the same screen, the cursor moved one column: off the phrase number
    onto a pattern cell, and <b>SEQ and LFO go dark</b> &mdash; they copy and delete a phrase, and
    there is no phrase under the cursor any more &mdash; while SEL stops saying STEPS and says
    PATTERN.</p>""",
          '    <div class="pair">']
    p += fig(real("song-number", "SONG with the foot: the cursor on the phrase number, SEQ COPY and LFO DELETE live"),
             "On the phrase number. SEQ copies the one below, LFO deletes it.")
    p += fig(real("song-cell", "SONG with the foot: the cursor on a pattern cell, SEQ and LFO showing a dash"),
             "One column right. Both say &mdash;, which is an answer and not a hole.")
    p += ['    </div>',
          """    <p><b>A dash is information.</b> A cell with a name and nothing under it means
    something else: there is a gesture here and the word would not fit at any size. Two states, two
    readings &mdash; the first build had one blank cell doing both jobs, and it read as dead.</p>""",
          '  </section>']

    p += ['  <section>', '    <span class="tag">EDIT held</span>',
          '    <h2>The foot becomes <em>the keyboard itself</em></h2>',
          """    <p>While EDIT is down the twelve are not what the hand is about to press &mdash; the
    black keys are. So the foot swaps. The first build laid them in a flat row of six and he could
    not read it; <b>it is the keyboard's own shape now</b> (his design): a row of the foot is one
    octave, three keys together then two, so a key is found by its place in the group rather than by
    counting. Row one is F#3&nbsp;G#3&nbsp;A#3 then C#4&nbsp;D#4, row two the octave above.</p>
    <p>All ten are drawn. <b>PIT, GLO and POLY are printed on the panel</b> (NOTES.md, 29/10) and
    command nothing, so they say &mdash;. F#5 opens a third octave no OP lives in and is left out.
    The foot follows the CAPS LOCK rather than the thumb, so a tap of EDIT leaves the keys up until
    it is tapped again &mdash; a state only the EDIT LED used to tell.</p>""",
          '    <div class="pair">']
    p += fig(real("keys", "The foot showing the black keys in groups of three and two"),
             "The ten black keys of two octaves, in the shape they lie in.")
    p += ['    </div>', '  </section>']

    p += ['  <section>', '    <span class="tag">PATTERN</span>',
          '    <h2>Three views, <em>three feet</em></h2>',
          """    <p>PATTERN is the screen the coat earns its two rows on. It is three views of the
    same steps, and that is what moves: <b>the view you are not in is the button that goes there</b>,
    so FX goes dark on the effects and SEQ goes dark on the notes. ARP offers the chords only where
    the keys play notes. And SEL opens something only where there is something to open.</p>""",
          '    <div class="pair">']
    p += fig(real("pattern-notes", "PATTERN, the notes: FX EFFECTS, ENV INSTR, ARP CHORDS"),
             "NOTES. SEL, LFO and SEQ say &mdash;.")
    p += fig(real("pattern-fx", "PATTERN, the effects: SEL wakes as SYLLABLE"),
             "FX. SEL wakes as SYLLABLE, in the small size, which is the only one it fits.")
    p += ['    </div>', '    <div class="pair">']
    p += fig(real("pattern-ins", "PATTERN, the instruments: SEL and ENV both say OPEN"),
             "INS. SEL and ENV do the same thing and both say it.")
    p += ['      <div style="flex:1 1 300px;min-width:280px">',
          """        <p><b>LFO is dark on all three.</b> It does nothing on this screen at all, and
        the cell is drawn to say so: learning that a button is never for you here is worth a cell.
        </p>""",
          '      </div>', '    </div>', '  </section>']

    p += ['  <section>', '    <span class="tag">MIXER</span>',
          '    <h2>Eleven dashes <em>are the truth</em></h2>',
          """    <p>MIXER was the last screen with <code>ui_shift</code>, so the last where the EDIT
    layer existed without the foot naming it. Six of its twelve cells are dashes: <b>this screen is
    knobs and MONO</b>, and the foot saying so plainly is the most useful thing it can say about it.
    EDIT carries PROJECT there &mdash; on MIXER the tap goes to the project and never latches, so
    the guide leaves the LED dark while the cell can say it.</p>""",
          '    <div class="pair">']
    p += fig(real("mixer", "MIXER with the foot: six cells of twelve are dashes"),
             "A screen that answers almost none of the twelve, saying so.")
    p += ['    </div>', '  </section>']

    p += ['  <section>', '    <span class="tag">PROJECT</span>',
          '    <h2>The slow way in, <em>teaching the fast one</em></h2>',
          """    <p>Two gestures had no home on the foot because they are not of the moment: the
    listening speed and the metronome, both EDIT held plus something. They are rows of PROJECT now,
    straight under BPM, <b>and the foot says the gesture for the row the cursor is on</b> (his
    design). You find the thing by the slow path and the screen hands you the fast one &mdash; which
    is how anyone has ever graduated from menus to shortcuts.</p>
    <p>Neither is written to the file: <code>player_init</code> puts both back when a project opens,
    and neither dirties the song nor enters UNDO. A rule marking them off was tried and taken out:
    the foot already makes the distinction.</p>""",
          '    <div class="pair">']
    p += fig(real("project-speed", "PROJECT: SPEED under BPM, the foot saying EDIT + SELECT"),
             "SPEED, with EDIT + SELECT at the foot.")
    p += fig(real("project-metro", "PROJECT: METRONOME, the foot saying EDIT + REC"),
             "METRONOME. The list is ten rows now, so it scrolls.")
    p += ['    </div>', '  </section>']

    p += ['  <section>', '    <span class="tag">How the shape was chosen &mdash; drawn, before any of it</span>',
          '    <h2>A foot, <em>and not a frame</em></h2>',
          """    <p>These two are hand-drawn, over SONG's own measurements &mdash; a 16&nbsp;px head
    and rows of 28. They are what the decision was made on. SONG fills the panel to the pixel, so
    the help has to <b>take</b>: two rows of 28 at the foot leave six phrases instead of eight.</p>
    <p>A frame on four sides was the other idea. A column down each side costs a third of the width
    to hold two labels nobody can read sideways. <b>The foot costs less and buys more</b>, because
    the panel's buttons are two rows of six: draw two rows of six and the cell you read sits where
    the button sits under your thumb. Top right on the screen is top right under your hand.</p>""",
          '    <div class="pair">']
    p += fig(real("song-off", "SONG as it is with the coat off: eight rows"),
             "The real SONG, coat off. Eight phrases, no room spare.")
    p += ['      <figure>', panel(6, 3, 0, footer_buttons(ON_CELL)),
          '        <figcaption>Hand-drawn, 2 November. The words are not the built ones: '
          'SECT. and PROJ. were guesses at what would fit.</figcaption></figure>',
          '    </div>', '  </section>']

    p += ['  <section>', '    <span class="tag">What the panel changed</span>',
          '    <h2>Five things <em>no drawing could say</em></h2>',
          """    <ul class="notes">
      <li><b>The twelve read as one word.</b> SEL saying PATTERN and ENV saying SECTION, side by
      side, each filling its cell: on the panel they ran together. There is a rule between the cells
      now, which also makes the grid read as a grid.</li>
      <li><b>A word too wide drops a size, then another.</b> CONDENSED 12, then 11, then 9 &mdash;
      the name's own size &mdash; before it is given up. SYLLABLE fits at none of the ordinary
      sizes and is perfectly readable at the last one. It is never shortened: an abbreviation
      nobody reads is worse than nothing.</li>
      <li><b>A blank cell was saying two things at once.</b> The first build dropped the name along
      with the word, so SEL on the effects read as dead while it opened the syllable. The name is
      always drawn now, and a button with nothing to do says &mdash;.</li>
      <li><b>The CAPS LOCK became visible, unasked.</b> EDIT tapped latches the black keys on. The
      foot follows the latch, so it stays on the keys until EDIT is tapped again.</li>
      <li><b>A song with sections costs more than two rows.</b> A section marker takes a slot, so
      six rows of LEOMOTEUR show four phrases and two markers. The drawing could not have known.</li>
    </ul>""", '  </section>']

    p += ['  <section>', '    <span class="tag">Written down so nobody fixes them again</span>',
          '    <h2>Two faults <em>that were not faults</em></h2>',
          """    <ul class="notes">
      <li><b>GESTURES already documents EDIT held + REC and EDIT held + SELECT.</b> I had read the
      SONG, PATTERN and PHRASE pages and concluded the gestures were written nowhere but PATTERN.
      The page that exists for what is true everywhere is where they were.</li>
      <li><b>The listening speed already goes back to 100.</b> <code>player_init</code> does it, and
      it runs whenever a project is adopted. I had read <code>ui_speed</code>, which only applies
      the speed, and concluded nothing reset it. There is no trap to close.</li>
    </ul>""", '  </section>']

    p += ['  <section>', '    <span class="tag">Left open</span>',
          '    <h2>What <em>is not done</em></h2>',
          """    <ul class="notes">
      <li><b>PHRASE and INSTRUMENT wear nothing</b> and keep their eight rows, switched on or not.
      INSTRUMENT has no <code>ui_shift</code>; PHRASE is a screen whose future is its own
      question. <b>Answered on 3/11:</b> PHRASE is deleted. SEL on a phrase's number opens THE
      PHRASE, a menu of what the row can do, and nothing led to the old screen any more.</li>
      <li><b>The pages are still flat.</b> SONG's HELP page is forty-four lines with no grouping.
      The coat answers the moment; the reference still reads like a dump.</li>
      <li><b>The metronome is a knob, not a toggle.</b> Right for ON, left for OFF, consistent with
      every other row of PROJECT and perhaps wrong for a thing with two states. <b>Judged on 3/11
      and kept:</b> the encoder reports detents per frame, so one flick of the wrist would toggle
      two or three times and land where chance put it. Direction as state is absolute.</li>
      <li><b>Six characters a cell</b> remains the budget, and the vocabulary has to be built inside
      it. Each word is one more thing to keep true as the screens change.</li>
    </ul>""", '  </section>', '</div>', '']

    out = pathlib.Path("proto/coat.html")
    html = "\n".join(p)
    io.open(out, "w", encoding="utf-8").write(html)
    print(out)
    if "--inline" in sys.argv:
        where = pathlib.Path(sys.argv[sys.argv.index("--inline") + 1])
        where.parent.mkdir(parents=True, exist_ok=True)
        io.open(where, "w", encoding="utf-8").write(inline(html))
        print(where)


if __name__ == "__main__":
    main()
