#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""proto/help.html -- a way into HELP, proposed on 02/11. Not built.

The screens were drawn by a throwaway patch of app/help.c and app/app.c,
shot through build/web/moondaw.wasm, and reverted: nothing of the patch
is in app/ and the pictures are all that is left of it. The LEDs are not
in the framebuffer, so the panel beside each screen is drawn here.

    python3 tools/help_page.py              # writes proto/help.html
    python3 tools/help_page.py --inline F   # the same with the pictures carried
                                            # inside it, for sending somewhere
"""
import base64
import io
import pathlib
import re
import sys

BTN_ROWS = [["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"],
            ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"]]
OPS = {1: "OP1", 3: "OP2", 5: "OP3", 8: "OP4", 10: "OP5", 13: "OP6", 20: "MONO"}
BLACK = {1, 3, 5, 8, 10}
ON, OFF, EDGE = "#7FD962", "#15191F", "#2A313B"


def panel(lit_btns=(), lit_keys=(), lit_oct=False):
    """The FM-1's face: OCT- and OCT+ left of the screen, the twelve in two
    rows as on the machine, and the 27 keys under them."""
    o = ['<svg class="panel" viewBox="0 0 480 172" xmlns="http://www.w3.org/2000/svg">']
    for i, name in enumerate(("OCT−", "OCT+")):
        y = 10 + i * 34
        o.append(f'<rect x="8" y="{y}" width="54" height="26" rx="4" fill="{ON if lit_oct else OFF}" stroke="{EDGE}"/>'
                 f'<text x="35" y="{y + 18}" text-anchor="middle" class="bl{" on" if lit_oct else ""}">{name}</text>')
    for r, row in enumerate(BTN_ROWS):
        for c, name in enumerate(row):
            on, x, y = name in lit_btns, 84 + c * 66, 10 + r * 34
            o.append(f'<rect x="{x}" y="{y}" width="58" height="26" rx="4" fill="{ON if on else OFF}" stroke="{EDGE}"/>'
                     f'<text x="{x + 29}" y="{y + 18}" text-anchor="middle" class="bl{" on" if on else ""}">{name}</text>')
    white = [k for k in range(27) if k % 12 not in BLACK]
    wx = {k: 8 + i * 29.5 for i, k in enumerate(white)}
    for k in white:
        o.append(f'<rect x="{wx[k]:.1f}" y="94" width="28" height="70" rx="3" '
                 f'fill="{ON if k in lit_keys else "#D8D8D8"}" stroke="#0A0D12"/>')
    for k in range(27):
        if k % 12 not in BLACK:
            continue
        on, x = k in lit_keys, wx[max(w for w in white if w < k)] + 19
        o.append(f'<rect x="{x:.1f}" y="94" width="19" height="44" rx="2" '
                 f'fill="{ON if on else "#1A1A1A"}" stroke="#0A0D12"/>')
        if k in OPS:
            o.append(f'<text x="{x + 9.5:.1f}" y="156" text-anchor="middle" '
                     f'class="kl{" on" if on else ""}">{OPS[k]}</text>')
    return "\n".join(o) + "\n</svg>"


HEAD = """<meta charset="utf-8">
<title>HELP, under the hand</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;600&family=Barlow+Condensed:wght@500;600;700&display=swap">

<!--
  MOONDAW - HELP reached from the work. Proposed 02/11, NOT built.

  The screens are real pixels and not CSS: a throwaway patch of app/help.c
  and app/app.c, shot through build/web/moondaw.wasm (web_fb), and reverted
  the same day. Nothing of it is in app/. The panels beside the screens are
  drawn by tools/help_page.py, which writes this file.
-->

<style>
:root{
  --page:#070A0F; --rule:#1B2231; --prose:#8E99A8; --prose-hi:#D6DCE4;
  --dim:#5A6470; --accent:#7FD962; --title:#62C8D9; --mute:#9E5555;
  --mono:'IBM Plex Mono', ui-monospace, Menlo, monospace;
  --cond:'Barlow Condensed','Helvetica Neue',Arial,sans-serif;
  --sf:-apple-system, BlinkMacSystemFont,'SF Pro Text',system-ui,sans-serif;
  color-scheme:dark;
}
*,*::before,*::after{ box-sizing:border-box; }
body{ margin:0; background:var(--page); color:var(--prose);
  font-family:var(--sf); font-size:15px; line-height:1.55; -webkit-font-smoothing:antialiased; }
.wrap{ max-width:880px; margin:0 auto; padding-inline:20px; padding-block:52px 96px; }
.eyebrow{ font-family:var(--mono); font-size:11px; font-weight:600;
  letter-spacing:.18em; text-transform:uppercase; color:var(--dim); }
h1{ margin:16px 0 0; font-family:var(--cond); font-weight:700; text-transform:uppercase;
  font-size:clamp(36px,6vw,56px); line-height:.98; color:var(--prose-hi); text-wrap:balance; }
h1 em{ font-style:normal; color:var(--accent); }
.lede{ max-width:64ch; margin:18px 0 0; font-size:16px; }
.lede strong, p b{ color:var(--prose-hi); font-weight:600; }
code{ font-family:var(--mono); font-size:.9em; color:var(--title); }
h2{ margin:0; font-family:var(--cond); font-weight:700; text-transform:uppercase;
  font-size:30px; color:var(--prose-hi); line-height:1; text-wrap:balance; }
h2 em{ font-style:normal; color:var(--accent); }
section{ margin-top:76px; }
section > p{ max-width:64ch; margin:14px 0 0; }
.tag{ font-family:var(--mono); font-size:10px; font-weight:600; letter-spacing:.16em;
  text-transform:uppercase; color:var(--title); display:block; margin-bottom:8px; }
ul.notes{ margin:14px 0 0; padding:0; list-style:none; max-width:68ch;
  display:flex; flex-direction:column; gap:10px; }
ul.notes li{ padding-left:16px; position:relative; font-size:14.5px; }
ul.notes li::before{ content:""; position:absolute; left:0; top:.62em; width:7px; height:2px; background:var(--dim); }
.budget{ display:grid; margin-top:26px; border-top:1px solid var(--rule); border-bottom:1px solid var(--rule); }
@media (min-width:760px){ .budget{ grid-template-columns:repeat(2,minmax(0,1fr)); } }
.budget > div{ padding:14px 20px 14px 0; min-width:0; }
@media (min-width:760px){ .budget > div + div{ padding-left:20px; border-left:1px solid var(--rule); } }
.budget .k{ display:block; font-family:var(--mono); font-size:10px; font-weight:600;
  letter-spacing:.16em; text-transform:uppercase; margin-bottom:6px; color:var(--title); }
.budget .k.no{ color:var(--mute); } .budget .k.yes{ color:var(--accent); }
.budget p{ margin:0; font-size:14px; }

/* The panel: 240 px square. Shown doubled. */
figure{ margin:0; display:flex; flex-direction:column; gap:10px; }
figure img{ display:block; width:300px; max-width:100%; height:auto; image-rendering:pixelated;
  border-radius:4px; box-shadow:0 0 0 2px #000, 0 24px 50px -18px rgba(0,0,0,.8); }
figcaption{ font-family:var(--mono); font-size:11px; color:var(--dim); line-height:1.5; max-width:300px; }
.duo{ margin-top:26px; display:flex; gap:24px; flex-wrap:wrap; align-items:flex-start; }
.duo .said{ flex:1 1 300px; min-width:280px; }
.panel{ display:block; width:100%; max-width:460px; height:auto; margin-top:4px; }
.panel .bl{ font-family:var(--cond); font-weight:600; font-size:13px; fill:#8E99A8; letter-spacing:.04em; }
.panel .bl.on{ fill:#0B1409; }
.panel .kl{ font-family:var(--mono); font-size:9px; fill:#5A6470; }
.panel .kl.on{ fill:#7FD962; }
</style>
"""


def inline(html):
    """Every picture carried in the page itself. A page read from a folder
    finds them beside it; one sent on its own has nothing beside it."""
    def swap(m):
        b = pathlib.Path("proto", m.group(1)).read_bytes()
        return 'src="data:image/png;base64,%s"' % base64.b64encode(b).decode()
    return re.sub(r'src="(help/[^"]+\.png)"', swap, html)


def main():
    out = pathlib.Path("proto/help.html")
    p = [HEAD, '<div class="wrap">',
         '  <div class="eyebrow">MOONDAW &middot; FM-1 &middot; proposed 2 November 2026 &middot; not built</div>',
         '  <h1>Help <em>where the hand already is</em></h1>',
         '''  <p class="lede">HELP has a page for every screen and no way to it: one walks back to
  HOME's root and opens it from the menu, which nobody does in the middle of a bar. This proposes a
  gesture of its own &mdash; <strong>OCT&minus; and OCT+ pressed together</strong>, from any screen
  &mdash; opening <strong>the page of the screen in front</strong>, HOME going back to the work
  rather than out to the menu. And the line under the cursor <strong>lighting its controls on the
  panel</strong>: help read with the eyes down on the keys.</p>
  <p class="lede"><b>None of it is in the firmware.</b> The screens below are real pixels &mdash; a
  patch written to take these pictures, and thrown away. What is kept is the picture and the
  argument.</p>''']

    p += ['  <section>', '    <span class="tag">The gesture</span>',
          '    <h2>The one chord <em>the panel had left</em></h2>',
          '''    <p>Every button on this machine acts on its press &mdash; <code>app/ui.h</code> says
    it plainly: <b>EDIT is the only one that can be held</b>. So no modifier can be pressed before a
    second button without its own action going off first: HOME&nbsp;+&nbsp;anything has already
    stepped back, SAVE&nbsp;+&nbsp;anything has already saved.</p>
    <p><b>OCT&minus; and OCT+ are the exception, and by accident.</b> Every screen reads them in the
    same frame and in opposite directions, so the two together move the keyboard's octave by exactly
    nothing. The chord would be read in <code>app/app.c</code> before any screen sees the frame, the
    two buttons rubbed out of it, so even that nothing never happens.</p>''',
          '    <div class="duo">',
          '      <figure><img src="help/5-chord.png" alt="HELP HOME: SELECT KNOB 1 move, SEL go, SYSTEM, SAMPLES">',
          "        <figcaption>The chord pressed on HOME's root. No list, no menu: the page.</figcaption></figure>",
          '      <div class="said">', panel(lit_oct=True),
          '        <p style="font-size:14px">Pressed together, released together. Nothing else on the '
          'panel answers to two buttons, so nothing else can be hit by mistake.</p>',
          '      </div>', '    </div>', '  </section>']

    p += ['  <section>', '    <span class="tag">The page</span>',
          '    <h2>A cursor, <em>and the panel says the rest</em></h2>',
          '''    <p>A page is a block of text one scrolls. It would become a <b>cursor</b>, a line at a time,
    with a green bar at the left of the line it is on &mdash; and the line's controls would
    <b>light on the machine</b>. Reading stops being a translation: the three keys named under
    your fingers are the three keys lit under your fingers.</p>
    <p>The words are the ones the pages are already written in. Each line is read for whole words
    the machine knows &mdash; the fourteen buttons, and OP1 to OP6 and MONO, the black keys EDIT
    gives a command to &mdash; so a page lights itself and nothing is written twice.
    <b>SELECT is not SEL</b>: whole words only.</p>''',
          '    <div class="duo">',
          '      <figure><img src="help/3-ops.png" alt="PATTERN, the cursor on OP1 OP2 OP3, copy paste cut">',
          '        <figcaption>PATTERN, the cursor on the clipboard line.</figcaption></figure>',
          '      <div class="said">', panel(lit_keys={1, 3, 5}),
          '        <p style="font-size:14px">The three black keys light where the hand plays them. '
          'A knob has no LED, so what KNOB&nbsp;3 does is only ever read.</p>',
          '      </div>', '    </div>',
          '    <div class="duo">',
          '      <figure><img src="help/4-edit.png" alt="PATTERN, the cursor on EDIT held, black keys: commands">',
          '        <figcaption>Four lines higher: one button, and it is the shift itself.</figcaption></figure>',
          '      <div class="said">', panel(lit_btns={"EDIT"}),
          '        <p style="font-size:14px">PLAY and REC are never taken: their LEDs are their '
          'state, and a help that lied about what is playing would be worse than none.</p>',
          '      </div>', '    </div>', '  </section>']

    p += ['  <section>', '    <span class="tag">To take out</span>',
          '    <h2>The cursor that <em>moves by itself</em></h2>',
          '''    <p>HELP opens with its cursor already on the topic of the last screen worked on
    &mdash; a help of where you were, for a screen nobody reaches. Read from the menu, with no
    memory of why, <b>it looks like a fault</b>: the list opens on PATTERN and the line reads
    &ldquo;white: where you came from&rdquo;, explaining a cleverness that cost a walk back to the
    root. It should go. <b>The list opens on its first topic</b> and says nothing, because the page
    you actually want is one chord away from the screen you are on.</p>''',
          '    <div class="duo">',
          '      <figure><img src="help/1-list.png" alt="The HELP list: HOME, SONG, PHRASE, PATTERN, SYLLABLE, INSTRUMENT, MIXER">',
          '        <figcaption>The list, on HOME. No footer, no ink for a screen you crossed.</figcaption></figure>',
          '      <div class="said">',
          '''        <div class="budget"><div><span class="k yes">What it gains</span><p>Nothing to
        explain. A list that opens where lists open.</p></div><div><span class="k no">What it
        costs</span><p>Through the menu, the topic is found by turning. Four turns at most, and
        the menu is no longer the way in.</p></div></div>''',
          '      </div>', '    </div>', '  </section>']

    p += ['  <section>', '    <span class="tag">Left open</span>',
          '    <h2>The pages <em>are still flat</em></h2>',
          '''    <ul class="notes">
      <li><b>SONG's page is forty-four lines.</b> A cursor makes them walkable, not scannable.
      What they want is a grouping &mdash; the cursor's own gestures, then the commands, then the
      way out &mdash; under a grey intertitle. That is a content job, ten pages of it, and no part
      of it was tried here.</li>
      <li><b>A carousel was considered and dropped.</b> One page at a time hides everything you did
      not ask for, and the thing you look up in a hurry is never the slide you are on.</li>
      <li><b>EFFECTS does not light anything.</b> Its lines are effect names, not controls.</li>
      <li><b>A line that names PLAY cannot light it.</b> PLAY's LED belongs to the transport.</li>
    </ul>''', '  </section>', '</div>', '']
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
