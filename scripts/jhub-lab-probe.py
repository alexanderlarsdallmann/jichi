#!/usr/bin/env python3
"""Drive jichi inside a REAL JupyterLab terminal, over terminado's websocket.

WHAT THIS TESTS, AND WHAT IT CANNOT.  A JupyterLab terminal has two halves.
The SERVER half is jupyter-server + terminado: it allocates the pty, relays
bytes, and applies the size the client asks for.  The CLIENT half is xterm.js
in a browser.  This probe speaks the websocket directly, so it tests the server
half completely and the client half not at all.  The browser key bindings --
jichi binds Ctrl-R (reverse history search) and Ctrl-G (ghost text); a browser
binds Ctrl-R to reload and Firefox binds Ctrl-G to find-next -- cannot be
answered here, and the report says so instead of implying coverage it lacks.

FOUR DEFECTS THIS PROBE HAD, AND WHAT THEY COST, because each is a trap the
next person will fall into:

  1. `expect` searched the WHOLE accumulated transcript, so the second
     measurement of a repeated marker returned instantly on the first
     measurement's output.  Every check is now bounded to output produced
     AFTER a mark.
  2. It waited for the prompt chevron U+203A.  Under LC_ALL=C jichi correctly
     draws ASCII (`[chat:fast:0%] >`, not `[chat·fast·0%] ›`), so the probe
     timed out on a HEALTHY run and then blamed the locale check.  The marker
     is now `0%]`, which both forms share.
  3. The mock matched `"role":"tool"` to route follow-up calls -- but that
     string stays in the HISTORY for the rest of the session, so every later
     turn matched it too.  Content predicates must be unique to the turn.
  4. The $HOME check scraped whatever text was on screen and passed on junk.
     It now compares against the exact expected path.

TAP on stdout; the raw transcript is written to <outdir>/terminal.log.
"""
import json
import os
import re
import sys
import time

from websocket import create_connection

SGR = re.compile(r"\x1b\[[0-9;]*m")

# How long a check waits for a marker.  Tunable because the NEGATIVE CONTROL
# expects every marker to be absent: at the normal 30 s that is eight timeouts
# and a ten-minute run, which is long enough that nobody runs the control --
# and a control nobody runs is not a control.
WAIT = int(os.environ.get("JHUB_PROBE_TIMEOUT", "30"))

# Markers taken from an OBSERVED bare-pty run, not from the source.
PROMPT_ANY = "0%]"          # in both "[chat·fast·0%]" and "[chat:fast:0%]"
GLYPH_TOOL = "▸"       # the UTF-8 tool line
GLYPH_OK = "✓"         # the UTF-8 success mark
BRACKETED_ON = "\x1b[?2004h"


class Term:
    """One JupyterLab terminal, driven over terminado's websocket."""

    def __init__(self, port, token, name, log):
        self.url = "ws://127.0.0.1:%s/terminals/websocket/%s?token=%s" % (
            port, name, token)
        self.ws = create_connection(self.url, timeout=15)
        self.buf = ""
        self.log = log
        self.drain(1.5)

    def send(self, text):
        self.ws.send(json.dumps(["stdin", text]))

    def set_size(self, rows, cols):
        self.ws.send(json.dumps(["set_size", rows, cols, cols * 8, rows * 16]))

    def drain(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            self.ws.settimeout(max(0.05, end - time.time()))
            try:
                msg = self.ws.recv()
            except Exception:
                continue
            try:
                frame = json.loads(msg)
            except Exception:
                continue
            if frame and frame[0] == "stdout" and len(frame) > 1:
                self.buf += frame[1]

    def mark(self):
        return len(self.buf)

    def since(self, at):
        return self.buf[at:]

    def expect_since(self, at, needle, timeout=30, strip=False):
        """Wait for a substring in output produced AFTER `at`.

        Bounded to new output on purpose: searching the whole transcript makes
        a repeated marker satisfy its own earlier occurrence.

        `strip=True` matches against SGR-stripped text.  The prompt is the
        reason: it renders as "0%<ESC>[0m<ESC>[36m]", so the literal "0%]" is
        never on the wire even though that is exactly what a reader sees.
        """
        end = time.time() + timeout
        while time.time() < end:
            hay = self.buf[at:]
            if strip:
                hay = SGR.sub("", hay)
            if needle in hay:
                return True
            self.drain(0.4)
        hay = self.buf[at:]
        return needle in (SGR.sub("", hay) if strip else hay)

    def close(self):
        if self.log:
            with open(self.log, "w", encoding="utf-8", errors="replace") as fh:
                fh.write(self.buf)
        try:
            self.ws.close()
        except Exception:
            pass


class Tap:
    def __init__(self, total):
        self.n = 0
        self.failed = 0
        print("1..%d" % total)

    def ok(self, msg):
        self.n += 1
        print("ok %d - %s" % (self.n, msg))
        sys.stdout.flush()

    def fail(self, msg, detail=""):
        self.n += 1
        self.failed += 1
        print("not ok %d - %s" % (self.n, msg))
        for line in (detail or "").splitlines()[:8]:
            print("#   %s" % line[:200])
        sys.stdout.flush()


def main():
    port, token, tname = sys.argv[1], sys.argv[2], sys.argv[3]
    jichi, config, ws_dir, home, outdir = sys.argv[4:9]
    baseline = open(sys.argv[9], encoding="utf-8", errors="replace").read()
    os.makedirs(outdir, exist_ok=True)

    base_env = "HOME=%s JC_CONFIG=%s/nc.json TERM=xterm-256color" % (home, home)
    tap = Tap(11)
    t = Term(port, token, tname, os.path.join(outdir, "terminal.log"))
    t.send("cd %s\n" % ws_dir)
    t.drain(1.0)

    def start_jichi(extra_env):
        """Launch jichi; return (started, mark_before) with the mark bounding it."""
        at = t.mark()
        t.send("%s %s %s --config %s --no-lite\n"
               % (base_env, extra_env, jichi, config))
        return t.expect_since(at, PROMPT_ANY, WAIT, strip=True), at

    def quit_jichi():
        t.send("/exit\r")
        t.drain(2.0)

    # --- 1 -- a pty on stdin AND stdout -------------------------------------
    at = t.mark()
    t.send("[ -t 0 ] && [ -t 1 ] && echo JC_TTY''_BOTH_OK\n")
    if t.expect_since(at, "JC_TTY_BOTH_OK", 10):
        tap.ok("a pty on stdin and stdout (jichi's isatty() gate)")
    else:
        tap.fail("no pty on both ends", t.since(at)[-400:])

    # --- 2 -- $HOME is exactly what the server was given ------------------
    # Measured HERE, at the front, because the shell is idle: at the end of the
    # run a previous check may still be unwinding, and the measurement then
    # reads an echo instead of an answer (it did, under the negative control).
    # Compared against the EXPECTED path -- an earlier version scraped whatever
    # was on screen and passed on a fragment of the prompt.
    at = t.mark()
    t.send("printf 'HOME''IS[%s]\\n' \"$HOME\"\n")
    t.expect_since(at, "HOMEIS[", 12)
    time.sleep(0.4)
    clean = SGR.sub("", t.since(at)).replace("\r", "")
    m = re.search(r"HOMEIS\[([^\]]*)\]", clean)
    got_home = m.group(1) if m else None
    if got_home == home:
        tap.ok("the terminal's $HOME is exactly the server's (%s) -- jichi's "
               "state is per-user" % home)
    else:
        tap.fail("$HOME is %r, expected %r -- jichi would put its key file and "
                 "state somewhere unexpected (M472)" % (got_home, home),
                 clean[-300:])

    # --- 3 -- TIOCGWINSZ: two DIFFERENT sizes, each with its OWN marker ------
    sizes = []
    for tag, rows, cols in (("A", 40, 100), ("B", 24, 132)):
        t.set_size(rows, cols)
        time.sleep(0.5)
        at = t.mark()
        t.send("printf 'SIZE%s=%%sx%%s\\n' \"$(tput cols)\" \"$(tput lines)\"\n" % tag)
        t.expect_since(at, "SIZE%s=" % tag, 12)
        time.sleep(0.4)
        clean = SGR.sub("", t.since(at)).replace("\r", "")
        m = re.search(r"SIZE%s=(\d+)x(\d+)" % tag, clean)
        sizes.append((m.group(1), m.group(2)) if m else None)
    if sizes == [("100", "40"), ("132", "24")]:
        tap.ok("terminado propagates TIOCGWINSZ (100x40 then 132x24, both observed)")
    else:
        tap.fail("winsize did not propagate: %r" % (sizes,))

    t.set_size(24, 100)
    time.sleep(0.4)

    # --- 4 -- jichi starts a TUI here, as it does on a bare pty -------------
    started, at_tui = start_jichi("LC_ALL=C.UTF-8 LANG=C.UTF-8")
    if started:
        tap.ok("jichi starts its interactive TUI in a JupyterLab terminal")
    else:
        tap.fail("no TUI prompt", t.since(at_tui)[-600:])

    # --- 5 -- bracketed paste is enabled (M156's premise) ------------------
    if BRACKETED_ON in t.since(at_tui):
        tap.ok("jichi enables bracketed paste (ESC[?2004h reaches the client)")
    else:
        tap.fail("jichi never enabled bracketed paste here",
                 "present in the bare-pty control: %s" % (BRACKETED_ON in baseline))

    # --- 6 -- a full turn, with a tool call --------------------------------
    at = t.mark()
    t.send("read the note\r")
    got = t.expect_since(at, "Here is the answer.", WAIT)
    time.sleep(0.6)
    seen = t.since(at)
    if got and GLYPH_TOOL in seen and GLYPH_OK in seen:
        tap.ok("a full turn runs: tool line, success glyph, streamed answer")
    else:
        tap.fail("turn incomplete (answer=%s tool=%s ok=%s)"
                 % (got, GLYPH_TOOL in seen, GLYPH_OK in seen), seen[-600:])

    # --- 7/8 -- bracketed paste: three lines stay ONE logical line ----------
    at = t.mark()
    t.send("\x1b[200~PASTE_ONE alpha\nbeta\ngamma\x1b[201~")
    time.sleep(2.0)
    if "(mock)" not in t.since(at):
        tap.ok("a pasted 3-line block does NOT submit on its embedded newlines")
    else:
        tap.fail("the paste submitted a turn by itself (M156 regression here)",
                 t.since(at)[-600:])
    at = t.mark()
    t.send("\r")
    if t.expect_since(at, "PASTED_OK", WAIT):
        tap.ok("the pasted block submits as ONE logical line on Enter")
    else:
        tap.fail("the pasted block did not reach the model intact",
                 t.since(at)[-600:])
    quit_jichi()

    # --- 9 -- ASCII fallback when the locale is not UTF-8 ------------------
    started, at = start_jichi("LC_ALL=C LANG=C")
    t.send("read the note\r")
    t.expect_since(at, "Here is the answer.", WAIT)
    time.sleep(0.6)
    seen = t.since(at)
    if started and GLYPH_TOOL not in seen and GLYPH_OK not in seen and "> read_file" in SGR.sub("", seen):
        tap.ok("LC_ALL=C: ASCII fallbacks, no UTF-8 glyphs (the image decides, not Jupyter)")
    else:
        tap.fail("locale fallback wrong (started=%s tool_glyph=%s ok_glyph=%s)"
                 % (started, GLYPH_TOOL in seen, GLYPH_OK in seen), seen[-600:])
    quit_jichi()

    # --- 10 -- NO_COLOR is honoured over the websocket ---------------------
    started, at = start_jichi("LC_ALL=C.UTF-8 LANG=C.UTF-8 NO_COLOR=1")
    t.send("read the note\r")
    t.expect_since(at, "Here is the answer.", WAIT)
    time.sleep(0.6)
    seen = t.since(at)
    # bound to jichi's own output: the shell's prompt legitimately emits SGR
    jichi_region = seen.split("interactive agent", 1)[-1].split("session total", 1)[0]
    if started and not SGR.search(jichi_region):
        tap.ok("NO_COLOR=1: jichi emits no SGR escapes")
    else:
        tap.fail("NO_COLOR ignored: SGR present in jichi's own output",
                 repr(SGR.findall(jichi_region)[:6]))
    quit_jichi()

    # --- 11 -- type-ahead survives a slow model --------------------------
    started, at = start_jichi("LC_ALL=C.UTF-8 LANG=C.UTF-8")
    t.send("typeahead please\r")
    time.sleep(1.0)                    # the mock is sleeping (`delay 3000`)
    t.send("QUEUED_WHILE_BUSY")
    finished = t.expect_since(at, "SLOWDONE", WAIT)
    time.sleep(1.0)
    # BOTH conditions, and the second is the one with teeth. A terminal in
    # canonical mode echoes typed characters itself, so "the marker appeared"
    # is satisfied by a program that does nothing at all -- the negative
    # control passed this check against a stub until the turn had to complete
    # as well.
    if finished and "QUEUED_WHILE_BUSY" in t.since(at):
        tap.ok("text typed during a turn survives AND the turn completes "
               "(type-ahead)")
    else:
        tap.fail("type-ahead check failed (turn_finished=%s marker=%s)"
                 % (finished, "QUEUED_WHILE_BUSY" in t.since(at)),
                 t.since(at)[-600:])
    quit_jichi()

    t.close()
    return 1 if tap.failed else 0


if __name__ == "__main__":
    sys.exit(main())
