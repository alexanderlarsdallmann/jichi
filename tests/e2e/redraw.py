"""Line-editor wrap redraw: at width 40, a line longer than one row must redraw
without duplicating the prompt. Renders the final screen with a tiny VT (DEC
deferred-wrap) emulator and asserts the prompt appears exactly once + text intact.
Run in BOTH render modes. Network-free: the TUI starts but never submits a turn.

M558 -- THIS TEST PASSED FOR THE WRONG REASON FOR ~200 MILESTONES, and the story
is the reason for the self-test below.

The emulator consumed `ESC[` and then only digits and `;`, so the private-mode
sequence `ESC[?2004h` (bracketed paste, which jichi emits at every prompt) fell
through and **printed `2004h` as five visible columns**. A real terminal shows
nothing. That should have broken this test immediately -- and did not, because
jichi's line editor redrew the WHOLE prompt+line on every keystroke, and each
redraw's `\r ESC[J` erased the emulator's own mistake before it could
accumulate. **The inefficiency under test was covering for a bug in the
instrument testing it.**

It surfaced the moment that inefficiency was removed (M558 made the incremental
echo unconditional): with no per-keystroke erase the phantom five columns
persisted, the emulator's column arithmetic drifted by five, and it reported a
duplicated prompt -- indistinguishable, from the failure message, from a real
redraw defect in the product. Two wrong diagnoses were written down before the
`?` in the parser was noticed.

So: the emulator now consumes private-mode sequences, and `_selftest()` proves
on every run that it can still detect a real duplicate. An instrument that
cannot fail has nothing to say, and one that fails for its own reasons is worse
than none."""
import os, sys, time, tempfile, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _e2e

W = 40


class VT:
    def __init__(self, w):
        self.w = w; self.grid = [[" "] * w]; self.row = 0; self.col = 0
        self.wrap = False

    def _ensure(self, r):
        while len(self.grid) <= r:
            self.grid.append([" "] * self.w)

    def _put(self, ch):
        if self.wrap:
            self.row += 1; self.col = 0; self.wrap = False
        self._ensure(self.row)
        self.grid[self.row][self.col] = ch
        if self.col == self.w - 1:
            self.wrap = True
        else:
            self.col += 1

    def feed(self, data):
        i, n = 0, len(data)
        while i < n:
            c = data[i]
            if c == 0x1b and i + 1 < n and data[i + 1] == ord('['):
                j = i + 2; params = ""
                # M558: a PRIVATE-MODE sequence (ESC[?...h / ESC[?...l) sets a
                # terminal mode and prints nothing. jichi emits ESC[?2004h and
                # ESC[?2004l around every prompt for bracketed paste. Without
                # this branch the '?' stopped the parameter scan and "2004h"
                # was printed as five visible columns -- see the module
                # docstring for how that hid for ~200 milestones.
                if j < n and data[j] == ord('?'):
                    while j < n and chr(data[j]) not in "hl":
                        j += 1
                    i = j + 1; continue
                while j < n and (chr(data[j]).isdigit() or data[j] == ord(';')):
                    params += chr(data[j]); j += 1
                if j < n:
                    fin = chr(data[j]); nums = [int(x) for x in params.split(';') if x]
                    a = nums[0] if nums else None
                    if fin == 'A': self.row = max(0, self.row - (a or 1)); self.wrap = False
                    elif fin == 'B': self.row += (a or 1); self._ensure(self.row); self.wrap = False
                    elif fin == 'C': self.col = min(self.w - 1, self.col + (a or 1)); self.wrap = False
                    elif fin == 'D': self.col = max(0, self.col - (a or 1)); self.wrap = False
                    elif fin == 'H':
                        self.row = (nums[0] - 1) if nums else 0
                        self.col = (nums[1] - 1) if len(nums) > 1 else 0
                        self.row = max(0, self.row); self.col = max(0, self.col)
                        self._ensure(self.row); self.wrap = False
                    elif fin == 'J':
                        self.wrap = False
                        for cc in range(self.col, self.w): self.grid[self.row][cc] = " "
                        del self.grid[self.row + 1:]
                    elif fin == 'K':
                        self.wrap = False
                        for cc in range(self.col, self.w): self.grid[self.row][cc] = " "
                    i = j + 1; continue
                i += 1; continue
            if c == 0x1b:
                i += 2; continue
            if c == ord('\r'): self.col = 0; self.wrap = False
            elif c == ord('\n'): self.row += 1; self._ensure(self.row); self.wrap = False
            elif c == ord('\b'): self.col = max(0, self.col - 1); self.wrap = False
            elif c >= 32: self._put(chr(c))
            i += 1

    def screen(self):
        return "\n".join("".join(r).rstrip() for r in self.grid)


def _selftest():
    """Prove the emulator can still fail, before trusting anything it says.

    M558: the emulator mis-modelled ESC[?2004h and this test passed anyway for
    ~200 milestones, because the product behaviour under test erased the
    emulator's mistake on every keystroke. These four cases would have caught it
    on the day it was written, and they run before every real check now.

    A failure here is an INSTRUMENT failure and says so, because the previous
    symptom -- "prompt appears twice" -- reads exactly like a product defect and
    cost two wrong diagnoses."""
    def screen_of(stream):
        v = VT(W); v.feed(stream); return v.screen()

    # It must still see a genuine duplicate: that is the whole assertion below.
    s = screen_of(b"[chat:x] > hello\r\n[chat:x] > hello")
    if s.count("[chat") != 2:
        _e2e.fail("INSTRUMENT: the emulator cannot see a duplicated prompt "
                  "(counted %d, want 2) -- the real check below would pass on "
                  "a broken product:\n%s" % (s.count("[chat"), s))
    # And not invent one.
    s = screen_of(b"[chat:x] > hello")
    if s.count("[chat") != 1:
        _e2e.fail("INSTRUMENT: one prompt counted as %d:\n%s"
                  % (s.count("[chat"), s))
    # Private-mode sequences print nothing. This is the bug that hid.
    s = screen_of(b"\x1b[?2004h[chat:x] > hi\x1b[?2004l")
    if "2004" in s:
        _e2e.fail("INSTRUMENT: ESC[?2004h leaked into the screen as text, so "
                  "column arithmetic drifts and a phantom prompt appears:\n%s" % s)
    # Erase-below must clear a stale row on ANOTHER row, which is what render()
    # relies on: it moves UP to the region's first row and erases downward. The
    # first version of this case erased and rewrote on the SAME row, so it could
    # not distinguish "the erase worked" from "the overwrite worked" -- deleting
    # the emulator's erase left it green. Go up a row first, so only a real
    # erase-below can produce one prompt.
    s = screen_of(b"[chat:x] > one\r\n[chat:x] > two"
                  b"\x1b[1A\r\x1b[J[chat:x] > fresh")
    if s.count("[chat") != 1:
        _e2e.fail("INSTRUMENT: ESC[J did not clear the row below (counted "
                  "%d, want 1) -- a genuinely duplicated prompt would be "
                  "invisible:\n%s" % (s.count("[chat"), s))


def one_mode(extra, label, marker):
    """`marker` is the prompt's shape in this mode, and it has to be passed in
    rather than hardcoded: M562 shortened the ACCESSIBLE prompt from
    `[chat:e2e:6%] >` to `chat >`, so a single `[chat` pattern counts zero in one
    of the two arms this test now runs. A test that greps for one mode's
    rendering is a test that only covers one mode."""
    ws = tempfile.mkdtemp(prefix="jichi_e2e_")
    pid, fd = _e2e.spawn(["--no-route"] + extra, cwd=ws, cols=W)
    buf = []
    _e2e.drain(fd, 3.0, buf)
    typed = "please echo this long sentence back verbatim ABCDEFGHIJ"  # > 40 cols
    os.write(fd, typed.encode())
    _e2e.drain(fd, 1.5, buf)
    snap = b"".join(buf)
    os.write(fd, b"\x03")            # Ctrl-C clears the line
    _e2e.drain(fd, 0.3, buf)
    os.write(fd, b"/exit\n")
    _e2e.drain(fd, 1.0, buf)

    vt = VT(W); vt.feed(snap); scr = vt.screen()
    joined = scr.replace("\n", "")
    if scr.count(marker) != 1:
        _e2e.fail("%s: prompt (%r) should appear exactly once on screen, got "
                  "%d:\n%s" % (label, marker, scr.count(marker), scr))
    if typed not in joined:
        _e2e.fail("%s: typed text not intact across the wrap:\n%s"
                  % (label, scr))


def main():
    _selftest()
    # BOTH render modes. Until M558 only the default was covered, and the
    # incremental echo -- the whole point of accessible mode's line editor --
    # was therefore never tested against a wrapping line at all. An instrument
    # that exists and is never pointed at a mode is indistinguishable from no
    # instrument (the M551 shape: accessible.sh had eight checks and every arm
    # passed --auto, so the approval prompt was never rendered).
    one_mode([], "default", "[chat")
    # M562: the accessible prompt has no brackets, no model and no percentage
    # below the 80% threshold -- `chat >`. That string cannot match the sighted
    # form `[chat:e2e:6%] >`, so the two markers really do discriminate.
    one_mode(["--accessible"], "accessible", "chat >")
    _e2e.ok("redraw (wrapped input, prompt once, both modes)")


main()
