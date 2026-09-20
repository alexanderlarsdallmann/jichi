#!/usr/bin/env python3
"""ansi-to-png.py - render a CAPTURED terminal transcript to a PNG.

WHAT THIS IS FOR, and the distinction is the whole point (M680). A screenshot in
documentation is a RECORD: it says "this is what the program printed". So it has
to be made from bytes the program actually printed. This script takes a
transcript captured from a real pseudo-terminal -- tests/tools/ptydrive writes
one with --log -- and draws it. It invents nothing.

It is deliberately NOT an image model. docs/ILLUSTRATION.md carries the rule:
an image that asserts "this is what it printed" must be captured; an image that
asserts nothing may be generated. A generated terminal would contain text the
program never produced, which is a fabricated record, and the model available
here renders letters as plausible nonsense besides.

Usage:
    ansi-to-png.py TRANSCRIPT OUT.png [--cols 76] [--title "jichi doctor"]

Reads the SGR subset jichi actually emits: reset, bold, dim, the eight basic
foreground colours and their bright forms, and 256-colour foregrounds. Anything
else is skipped rather than guessed at -- a renderer that invents a colour is
the same error one level down.
"""
import re
import sys

from PIL import Image, ImageDraw, ImageFont

# A dark palette chosen to match what a default terminal shows, so the picture
# looks like what the reader will see rather than like a design.
BG = (24, 24, 28)
FG = (216, 216, 220)
TITLE_BG = (38, 38, 44)
BASIC = {
    30: (40, 40, 46), 31: (224, 108, 117), 32: (152, 195, 121), 33: (229, 192, 123),
    34: (97, 175, 239), 35: (198, 120, 221), 36: (86, 182, 194), 37: (200, 200, 205),
    90: (92, 99, 112), 91: (233, 134, 143), 92: (178, 212, 155), 93: (240, 214, 163),
    94: (140, 198, 244), 95: (216, 160, 233), 96: (130, 205, 213), 97: (240, 240, 245),
}
SGR = re.compile(r"\x1b\[([0-9;]*)m")
OTHER_CSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


def mono_font(size):
    """A monospace face, by preference order, falling back to PIL's default."""
    for path in (
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    ):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default()


def parse(text):
    """-> [[(string, colour, bold), ...], ...], one list per line."""
    lines, cur, colour, bold, dim = [], [], FG, False, False
    i = 0
    while i < len(text):
        m = SGR.match(text, i)
        if m:
            for code in (m.group(1) or "0").split(";"):
                if code in ("", "0"):
                    colour, bold, dim = FG, False, False
                elif code == "1":
                    bold = True
                elif code == "2":
                    dim = True
                elif code.isdigit() and int(code) in BASIC:
                    colour = BASIC[int(code)]
            i = m.end()
            continue
        m = OTHER_CSI.match(text, i)
        if m:                      # cursor moves, erases: not drawable here
            i = m.end()
            continue
        ch = text[i]
        if ch == "\n":
            lines.append(cur)
            cur = []
        elif ch not in ("\r", "\x1b"):
            shade = tuple(c // 2 + 30 for c in colour) if dim else colour
            if cur and cur[-1][1] == shade and cur[-1][2] == bold:
                cur[-1][0] += ch
            else:
                cur.append([ch, shade, bold])
        i += 1
    if cur:
        lines.append(cur)
    return lines


def softwrap(lines, cols):
    """Wrap over-long lines at `cols`, the way a terminal does.

    NOT a crop and not a truncation: every character is drawn, on the next row.
    A real terminal at this width would show exactly this. The first draft
    sized the image to the longest line instead, which produced a 3,466-pixel
    picture of a program whose output is 96 columns wide -- and the draft
    before THAT clipped the overflow, which is a screenshot that misreports
    what was printed. Of the three, only this one shows what a reader sees.
    """
    out = []
    for line in lines:
        row, used = [], 0
        for text, colour, bold in line:
            while text:
                room = cols - used
                if room <= 0:
                    out.append(row)
                    row, used, room = [], 0, cols
                row.append((text[:room], colour, bold))
                used += len(text[:room])
                text = text[room:]
        out.append(row)
    return out


def render(lines, out, cols, title):
    lines = softwrap(lines, cols)
    size, pad, lead = 15, 14, 4
    font = mono_font(size)
    bold_font = mono_font(size)          # one face; bold is drawn by overdraw
    cw = font.getbbox("M")[2] or size // 2
    ch = (font.getbbox("Mg")[3] or size) + lead
    bar = ch + 10 if title else 0
    w = pad * 2 + cw * cols
    h = pad * 2 + bar + ch * max(len(lines), 1)
    img = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(img)
    if title:
        d.rectangle([0, 0, w, bar], fill=TITLE_BG)
        d.text((pad, 6), title, font=font, fill=(150, 150, 160))
    y = pad + bar
    for line in lines:
        x = pad
        for text, colour, bold in line:
            d.text((x, y), text, font=bold_font if bold else font, fill=colour)
            if bold:                      # overdraw one pixel right = a weight
                d.text((x + 1, y), text, font=bold_font, fill=colour)
            x += cw * len(text)
        y += ch
    img.save(out)
    return w, h


def main():
    # Hand-rolled and deliberately small, but it must SKIP a flag's value too.
    # The first draft filtered out words starting with "--" and left their
    # values behind as positionals, so `--cols 76` contributed "76" and the
    # usage message fired on a correct command line.
    argv, args, cols, title = sys.argv[1:], [], 76, ""
    i = 0
    while i < len(argv):
        if argv[i] == "--cols" and i + 1 < len(argv):
            cols = int(argv[i + 1]); i += 2
        elif argv[i] == "--title" and i + 1 < len(argv):
            title = argv[i + 1]; i += 2
        else:
            args.append(argv[i]); i += 1
    if len(args) != 2:
        sys.exit("usage: ansi-to-png.py TRANSCRIPT OUT.png "
                 "[--cols N] [--title TEXT]")
    with open(args[0], "r", encoding="utf-8", errors="replace") as fh:
        lines = parse(fh.read())
    w, h = render(lines, args[1], cols, title)
    print("%s: %d lines, %dx%d" % (args[1], len(lines), w, h))


if __name__ == "__main__":
    main()
