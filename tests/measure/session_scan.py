#!/usr/bin/env python3
"""Cost of one scan of the session store, per TUI command (M197). A measurement,
never a CI gate.

Drives the real TUI under a PTY against a synthesized store (mkstore.py) inside
an isolated HOME, and after every N commands reads three gauges out of the
binary's own /context output:

    Arenas: session <used> KB used (<reserved> KB reserved); turn scratch ...
    Process: <rss> KB resident (peak <hwm> KB)

plus the process GROUP's RSS from /proc (these tools fork helpers; a single-pid
RSS undercounts -- same reason idle_tui.py samples the group).

Why this exists: jc_session_list (src/session/jc_session.c:355) reads every
session file's FULL TEXT onto the caller's arena, and every TUI call site passes
the never-reset app->arena. The session-arena gauge climbing while turn-scratch
stays flat is what attributes the growth to that arena rather than to history,
libcurl, or the C library. ASan and valgrind --leak-check both report ZERO here
(main frees the arena at exit; the blocks are reachable until then), so only
peak/over-time instruments can see it.

Variants -- the number is scans of the whole store per iteration:

  sessions       1   /sessions                      (jc_tui.c:970)
  tab            1   /resume  + Tab, PER KEYPRESS   (jc_tui.c:1412)
  resume_prefix  2   /resume <full-id>              (:2688 -> resolve + re-read)
  resume_alias   3   /resume <alias>                (:2674 + :2679 duplicate + :2685)
  help           0   control: pure printf           (:1150-1230)
  context        0   control: the sampler's own cost
  idle           0   control: M181's flat baseline

Usage:
  python3 tests/measure/session_scan.py --variant sessions --calls 20 \
      --files 250 --bytes 71680 --csv /tmp/b.csv

Safety: caps peak RSS (--rss-ceiling-kb, default 400 MB) and kills the group if
exceeded. 12.5 GB is not reachable on a 4.9 GB host -- measure small, then
extrapolate arithmetically.
"""
import argparse
import collections
import errno
import fcntl
import json
import os
import pty
import re
import select
import shutil
import signal
import struct
import sys
import tempfile
import termios
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkstore  # noqa: E402

# --- gauges ----------------------------------------------------------------
# "Arenas: session" is unique in the tree and emitted unconditionally
# (src/chat/jc_context.c:132); "Process:" is gated on jc_meminfo_self, so the
# sync sentinel keys off the arenas line, not the process line.
ARENAS = re.compile(
    r"Arenas: session (\d+) KB used \((\d+) KB reserved\)"
    r"(?:; turn scratch (\d+) KB used \((\d+) KB reserved\))?")
PROC = re.compile(r"Process: (\d+) KB resident \(peak (\d+) KB\)")
SENTINEL = b"Arenas: session"
ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")

CTRL_U = b"\x15"  # kill the line, so a Tab-inserted prefix can't leak forward


def group_stats(pgid):
    """Sum RSS (KB) and CPU (seconds) over /proc for the process group.

    Lifted from tests/measure/idle_tui.py:26-55 (M181) so both measurements
    report the same quantity.
    """
    page_kb = os.sysconf("SC_PAGE_SIZE") // 1024
    hz = os.sysconf("SC_CLK_TCK")
    rss_pages = 0
    cpu_ticks = 0
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        try:
            with open("/proc/%s/stat" % name, "rb") as f:
                buf = f.read().decode("utf-8", "replace")
        except OSError:
            continue
        rp = buf.rfind(")")
        if rp < 0:
            continue
        fields = buf[rp + 1:].split()
        if len(fields) < 22:
            continue
        try:
            if int(fields[2]) != pgid:
                continue
            cpu_ticks += int(fields[11]) + int(fields[12])
            rss_pages += int(fields[21])
        except ValueError:
            continue
    return rss_pages * page_kb, cpu_ticks / float(hz)


class Timeout(Exception):
    pass


class Pump(object):
    """Bidirectional PTY pump.

    A naive os.write() of a whole batch deadlocks: the child blocks writing
    output while we block writing input. So we select() on both directions and
    cap writes at 1 KB per pass.

    The output tail is a BOUNDED deque -- /sessions x 200 emits megabytes, and a
    harness that accumulated all of it would confound the measurement it is
    taking.
    """

    # The TUI prompt tail, e.g. "[chat:e2e:0%] > ". Every completed command ends
    # with a fresh prompt, so counting these is a positive completion signal --
    # unlike output silence, which is indistinguishable from a long scan in
    # progress (a 480 MB store keeps jc_session_list busy for minutes with no
    # output at all).
    PROMPT = b"] > "

    def __init__(self, fd, tail_chunks=512):
        self.fd = fd
        self.q = b""
        self.tail = collections.deque(maxlen=tail_chunks)
        self.eof = False
        self.prompts = 0
        self._carry = b""

    def _count_prompts(self, chunk):
        buf = self._carry + chunk
        self.prompts += buf.count(self.PROMPT)
        # Keep enough tail to catch a marker split across two reads.
        self._carry = buf[-(len(self.PROMPT) - 1):] if len(self.PROMPT) > 1 else b""

    def feed(self, data):
        self.q += data

    def text(self):
        return ANSI.sub("", b"".join(self.tail).decode("utf-8", "replace"))

    def run(self, until=None, timeout=30.0, settle=0.0):
        """Pump until `until` appears in the tail, else until output settles.

        `settle` is how long the child must produce NOTHING (with our write
        queue empty) before we call the step done. That quiet window is
        load-bearing, not politeness: jc_term treats a newline as a pasted-row
        commit rather than a submit whenever more input is already buffered
        (input_pending, src/tui/jc_term.c:780 -- the M156 burst-paste
        fallback). Writing the next command too early therefore silently glues
        two commands into one logical line.
        """
        end = time.time() + timeout
        idle_since = None
        while time.time() < end:
            if until is not None and until in b"".join(self.tail):
                return self.text()
            wl = [self.fd] if self.q else []
            try:
                r, w, _ = select.select([self.fd], wl, [], 0.05)
            except select.error:
                break
            if w:
                try:
                    n = os.write(self.fd, self.q[:1024])
                    self.q = self.q[n:]
                except OSError as e:
                    if e.errno not in (errno.EAGAIN, errno.EINTR):
                        raise
            if r:
                try:
                    d = os.read(self.fd, 65536)
                except OSError:
                    self.eof = True
                    break
                if not d:
                    self.eof = True
                    break
                self.tail.append(d)
                self._count_prompts(d)
                idle_since = None
            elif not self.q:
                if idle_since is None:
                    idle_since = time.time()
                elif settle and time.time() - idle_since >= settle:
                    return self.text()
        if until is not None:
            raise Timeout("did not see %r within %.0fs; tail:\n%s"
                          % (until, timeout, self.text()[-800:]))
        return self.text()

    def send(self, data, markers, timeout=30.0, settle=0.08):
        """Write ONE keystroke sequence and wait for the command to COMPLETE.

        Two separate waits, for two separate reasons:

        1. `settle` -- a short quiet window right after the bytes are written,
           which is when the editor has echoed and consumed them (including the
           trailing newline). This is what keeps jc_term from mistaking the next
           command for a pasted row (input_pending, src/tui/jc_term.c:780 --
           the M156 burst-paste fallback). Get this wrong and two commands are
           silently glued into one logical line.
        2. one of `markers` in the output -- the positive signal that the
           command finished. Required because a slow scan produces NO output at
           all for minutes (a 480 MB store keeps cJSON busy), so output silence
           cannot mean "done", and a prompt redraw cannot be told apart from the
           per-keystroke echo redraws.

        Never batch two commands into one send.
        """
        if not data:
            return self.text()
        self.tail.clear()
        self.feed(data)
        self.run(timeout=timeout, settle=settle)
        if not markers:
            return self.text()
        end = time.time() + timeout
        while time.time() < end and not self.eof:
            blob = b"".join(self.tail)
            if any(m in blob for m in markers):
                return self.text()
            self.run(timeout=0.5, settle=0.0)
        blob = b"".join(self.tail)
        if any(m in blob for m in markers):
            return self.text()
        raise Timeout("command %r did not complete within %.0fs (markers %r); "
                      "tail:\n%s"
                      % (data[:40], timeout, markers, self.text()[-800:]))


def variant_bytes(variant, store):
    """(keystrokes, completion markers) for ONE iteration of `variant`.

    Markers are unique strings the command prints when it is DONE -- see
    Pump.send() for why a completion signal is required rather than silence.
    """
    if variant == "sessions":
        return (CTRL_U + b"/sessions\n",
                [b"most recent last", b"none for this project",
                 b"no saved sessions"])
    if variant == "tab":
        # Empty token => every id matches => n>1 => common-prefix branch in
        # jc_term (src/tui/jc_term.c:872-889) replaces the token silently with
        # the shared prefix, printing no listing. That inserted prefix, echoed
        # back, is the completion marker.
        return (CTRL_U + b"/resume \t", [mkstore.ID_FMT[:24].encode()])
    if variant == "resume_prefix":
        return (CTRL_U + b"/resume " + store["newest_id"].encode() + b"\n",
                [b"(resumed:", b"session matching"])
    if variant == "resume_alias":
        return (CTRL_U + b"/resume " + store["alias"].encode() + b"\n",
                [b"(resumed:", b"session matching", b"no session with alias"])
    if variant == "help":
        return (CTRL_U + b"/help\n", [b"/timeouts"])
    if variant == "context":
        return (CTRL_U + b"/context\n", [SENTINEL])
    if variant == "idle":
        return (b"", [])
    raise SystemExit("unknown variant %r" % variant)


def write_config(home, src_config):
    """Copy the offline fixture config and harden it against side work."""
    with open(src_config) as f:
        cfg = json.load(f)
    cfg["snapshots"] = False
    cfg["repoMap"] = False
    cfg["references"] = False
    cfg["maxRetries"] = 0
    cfg["telemetry"] = False
    path = os.path.join(home, "config.json")
    with open(path, "w") as f:
        json.dump(cfg, f)
    return path


def store_stat(sessions_dir):
    n = 0
    total = 0
    for name in os.listdir(sessions_dir):
        if not name.endswith(".json"):
            continue
        n += 1
        total += os.path.getsize(os.path.join(sessions_dir, name))
    return n, total


def parse_gauges(text):
    """Last Arenas/Process match in `text` -> dict, or None."""
    a = None
    for a in ARENAS.finditer(text):
        pass
    if a is None:
        return None
    p = None
    for p in PROC.finditer(text):
        pass
    g = {
        "used_kb": int(a.group(1)),
        "reserved_kb": int(a.group(2)),
        "scratch_kb": int(a.group(3)) if a.group(3) else 0,
        "scratch_res_kb": int(a.group(4)) if a.group(4) else 0,
        "rss_self_kb": int(p.group(1)) if p else 0,
        "hwm_self_kb": int(p.group(2)) if p else 0,
    }
    return g


def slope(xs, ys):
    """OLS slope, intercept, R^2."""
    n = len(xs)
    if n < 2:
        return 0.0, 0.0, 0.0
    mx = sum(xs) / float(n)
    my = sum(ys) / float(n)
    sxx = sum((x - mx) ** 2 for x in xs)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    if sxx == 0:
        return 0.0, my, 0.0
    b = sxy / sxx
    a = my - b * mx
    syy = sum((y - my) ** 2 for y in ys)
    if syy == 0:
        r2 = 1.0
    else:
        ssr = sum((y - (a + b * x)) ** 2 for x, y in zip(xs, ys))
        r2 = 1.0 - ssr / syy
    return b, a, r2


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--variant", default="sessions",
                    choices=["sessions", "tab", "resume_prefix",
                             "resume_alias", "help", "context", "idle"])
    ap.add_argument("--calls", type=int, default=20)
    ap.add_argument("--files", type=int, default=50)
    ap.add_argument("--bytes", type=int, default=4096, dest="bytes_each")
    ap.add_argument("--match", type=int, default=1)
    ap.add_argument("--sample-every", type=int, default=1)
    ap.add_argument("--bin", default=None, help="jichi binary (default ./jichi)")
    ap.add_argument("--config", default=None,
                    help="base config (default tests/e2e/fixtures/config.json)")
    ap.add_argument("--wrap", default=None,
                    help="prefix the child argv, e.g. a valgrind invocation")
    ap.add_argument("--timeout", type=float, default=30.0,
                    help="per-sentinel wait; raise for valgrind")
    ap.add_argument("--rss-ceiling-kb", type=int, default=409600,
                    help="kill the group past this; 0 disables")
    ap.add_argument("--csv", default=None)
    ap.add_argument("--keep", action="store_true", help="keep the temp HOME/ws")
    args = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    binary = args.bin or os.path.join(root, "jichi")
    base_cfg = args.config or os.path.join(root, "tests", "e2e", "fixtures",
                                           "config.json")
    if not os.path.exists(binary):
        raise SystemExit("session_scan: no binary at %s (run make)" % binary)

    home = tempfile.mkdtemp(prefix="jc_scan_home_")
    ws = tempfile.mkdtemp(prefix="jc_scan_ws_")
    rc = 1
    try:
        store = mkstore.build_store(home, ws, args.files, args.bytes_each,
                                    args.match, mkstore.ALIAS_DEFAULT, 1,
                                    quiet=True)
        cfg = write_config(home, base_cfg)
        files0, bytes0 = store_stat(store["sessions_dir"])

        print("session_scan: variant=%s store=%d files / %d bytes calls=%d"
              % (args.variant, files0, bytes0, args.calls))
        thp = "?"
        try:
            with open("/sys/kernel/mm/transparent_hugepage/enabled") as f:
                thp = f.read().strip()
        except IOError:
            pass
        print("session_scan: THP=%s  home=%s  ws=%s" % (thp, home, ws))

        argv = [binary, "--config", cfg]
        if args.wrap:
            argv = args.wrap.split() + argv

        env = dict(os.environ, LANG="C", LC_ALL="C", HOME=home, NO_COLOR="1",
                   TERM="xterm-256color")
        env.pop("JICHI_LANG", None)

        pid, fd = pty.fork()
        if pid == 0:
            try:
                os.chdir(ws)
                os.execve(argv[0], argv, env)
            except OSError:
                os._exit(127)
        # Fixed 24x120 window so line wrapping never perturbs byte counts.
        try:
            fcntl.ioctl(fd, termios.TIOCSWINSZ,
                        struct.pack("HHHH", 24, 120, 0, 0))
        except IOError:
            pass

        pump = Pump(fd)
        rows = []
        ceiling_hit = None
        one, done = variant_bytes(args.variant, store)

        def sample(i):
            text = pump.send(CTRL_U + b"/context\n", [SENTINEL],
                             timeout=args.timeout)
            g = parse_gauges(text)
            if g is None:
                raise Timeout("no gauges parsed; tail:\n" + text[-800:])
            rss, cpu = group_stats(pid)
            g.update(call=i, rss_group_kb=rss, cpu_s=cpu,
                     wall_s=time.time() - t0)
            rows.append(g)
            return g

        t0 = time.time()
        pump.run(timeout=15.0, settle=0.5)  # reach the input prompt

        if one:
            assert one.lstrip(CTRL_U).startswith(b"/"), \
                "never send a non-slash line: it would trigger a model call " \
                "and a one-time libcurl/TLS RSS step"

        # Warm-up: one iteration so the page cache is hot; reported as call 0
        # and excluded from the fit (a cold first read is a timing artifact).
        pump.send(one, done, timeout=args.timeout)
        sample(0)

        for i in range(1, args.calls + 1):
            pump.send(one, done, timeout=args.timeout)
            if args.variant == "idle":
                time.sleep(0.2)
            if i % args.sample_every == 0 or i == args.calls:
                g = sample(i)
                if args.rss_ceiling_kb and g["rss_group_kb"] > args.rss_ceiling_kb:
                    ceiling_hit = i
                    print("session_scan: RSS ceiling %d KB hit at call %d "
                          "(%d KB) -- stopping" % (args.rss_ceiling_kb, i,
                                                   g["rss_group_kb"]))
                    break

        files1, bytes1 = store_stat(store["sessions_dir"])

        try:
            pump.send(CTRL_U + b"/exit\n", [], timeout=3.0)
            pump.run(timeout=3.0, settle=0.3)
        except Timeout:
            pass

        # --- report ---
        if not rows:
            print("session_scan: no samples", file=sys.stderr)
            return 1
        hdr = ("call", "used_kb", "reserved_kb", "scratch_kb", "rss_self_kb",
               "rss_group_kb", "wall_s")
        print("")
        print("%6s %14s %13s %11s %12s %13s %8s" % hdr)
        for r in rows:
            print("%6d %14d %13d %11d %12d %13d %8.2f"
                  % (r["call"], r["used_kb"], r["reserved_kb"],
                     r["scratch_kb"], r["rss_self_kb"], r["rss_group_kb"],
                     r["wall_s"]))

        print("")
        if len(rows) == 1:
            # --calls 0: the warm-up IS the single scan, so row 0 is the
            # cost of ONE /sessions -- the single-call worst case.
            r = rows[0]
            print("session_scan: ONE scan: session-arena used %d KB "
                  "(reserved %d KB); rss_group %d KB; %.1f s"
                  % (r["used_kb"], r["reserved_kb"], r["rss_group_kb"],
                     r["wall_s"]))
            pred1 = (bytes0 + 248 * files0) / 1024.0
            print("session_scan: predicted %.1f KB; measured %d -> %+.1f%%"
                  % (pred1, r["used_kb"],
                     100.0 * (r["used_kb"] - pred1) / pred1 if pred1 else 0.0))
            print("session_scan: store drift %+d files, %+d bytes"
                  % (files1 - files0, bytes1 - bytes0))
            rc = 0
            return rc

        fit = [r for r in rows if r["call"] >= 1]
        xs = [r["call"] for r in fit]
        ys = [r["used_kb"] for r in fit]
        b, _, r2 = slope(xs, ys)
        print("session_scan: %d calls; session-arena used %d -> %d KB"
              % (rows[-1]["call"], rows[0]["used_kb"], rows[-1]["used_kb"]))
        print("session_scan: slope %.1f KB/call  R^2 = %.6f" % (b, r2))

        # Tail-half slope + delta ratio: the linear-vs-superlinear discriminator
        # (soak.py:278-288's contract).
        if len(fit) >= 4:
            half = len(fit) // 2
            bt, _, _ = slope(xs[half:], ys[half:])
            print("session_scan: tail-half slope %.1f KB/call" % bt)
            d = [ys[i] - ys[i - 1] for i in range(1, len(ys))]
            k = max(1, len(d) // 4)
            first, last = sum(d[:k]) / float(k), sum(d[-k:]) / float(k)
            if first == 0 and last == 0:
                print("session_scan: no growth at all (flat control)")
            else:
                ratio = (last / first) if first else float("inf")
                verdict = ("LINEAR" if 0.98 <= ratio <= 1.02 else
                           "SUPERLINEAR -- investigate" if ratio > 1.10 else
                           "SUBLINEAR -- something reclaims" if ratio < 0.95
                           else "near-linear")
                print("session_scan: delta ratio last/first = %.4f -> %s"
                      % (ratio, verdict))

        d_rss = rows[-1]["rss_group_kb"] - rows[0]["rss_group_kb"]
        d_used = rows[-1]["used_kb"] - rows[0]["used_kb"]
        d_res = rows[-1]["reserved_kb"] - rows[0]["reserved_kb"]
        if d_rss > 0:
            # RSS tracks RESERVED, not used: an oversized request gets a
            # dedicated block and the previous head's free tail is orphaned
            # permanently (src/util/jc_mem.c:77-90). Attribute against reserved
            # and report the waste factor separately.
            print("session_scan: rss_group %d -> %d KB (%.1f KB/call); arena "
                  "reserved accounts for %.1f%% of delta RSS (used alone: "
                  "%.1f%%)"
                  % (rows[0]["rss_group_kb"], rows[-1]["rss_group_kb"],
                     float(d_rss) / rows[-1]["call"],
                     100.0 * d_res / d_rss, 100.0 * d_used / d_rss))
        if d_used > 0:
            print("session_scan: block-tail waste factor reserved/used = %.2fx"
                  % (float(d_res) / d_used))
        print("session_scan: scratch %d -> %d KB"
              % (rows[0]["scratch_kb"], rows[-1]["scratch_kb"]))
        print("session_scan: store drift %+d files, %+d bytes"
              % (files1 - files0, bytes1 - bytes0))

        # Two regimes, so this stays a regression detector rather than a
        # description of one build:
        #   UNFIXED  ~ scans * (S + 138*F)  -- every file's text retained
        #   FIXED    ~ 0                    -- the listing outlives nothing
        # (Pre-M197 the per-file term measured ~138 B; S dominates either way.)
        scans = {"sessions": 1, "tab": 1, "resume_prefix": 2,
                 "resume_alias": 3}.get(args.variant, 0)
        if scans:
            unfixed_kb = scans * (bytes0 + 138.0 * files0) / 1024.0
            print("session_scan: pre-M197 model would be %.1f KB/call "
                  "(%d scan(s) of S + 138*F); measured %.1f"
                  % (unfixed_kb, scans, b))
            if b > 0.5 * unfixed_kb:
                print("session_scan: VERDICT **UNFIXED** -- the store's bytes "
                      "are being retained per call (%.0f%% of the pre-M197 "
                      "model)" % (100.0 * b / unfixed_kb))
            elif b > 4.0 * scans:
                print("session_scan: VERDICT partial -- %.1f KB/call is well "
                      "below the pre-M197 model but not flat; something per-"
                      "session is still landing on a caller arena" % b)
            else:
                print("session_scan: VERDICT **FIXED** -- flat (%.1f KB/call "
                      "over a %.1f MB store)" % (b, bytes0 / 1048576.0))
        if ceiling_hit:
            print("session_scan: NOTE stopped early at the RSS ceiling")

        if args.csv:
            with open(args.csv, "w") as f:
                f.write(",".join(hdr) + "\n")
                for r in rows:
                    f.write("%d,%d,%d,%d,%d,%d,%.3f\n"
                            % (r["call"], r["used_kb"], r["reserved_kb"],
                               r["scratch_kb"], r["rss_self_kb"],
                               r["rss_group_kb"], r["wall_s"]))
            print("session_scan: wrote %s" % args.csv)
        rc = 0
    except Timeout as e:
        print("session_scan: TIMEOUT %s" % e, file=sys.stderr)
    finally:
        try:
            os.killpg(pid, signal.SIGTERM)
            time.sleep(0.3)
            os.killpg(pid, signal.SIGKILL)
        except (OSError, NameError, UnboundLocalError):
            pass
        try:
            os.waitpid(pid, 0)
        except (OSError, NameError, UnboundLocalError):
            pass
        if args.keep:
            print("session_scan: kept %s and %s" % (home, ws))
        else:
            shutil.rmtree(home, ignore_errors=True)
            shutil.rmtree(ws, ignore_errors=True)
    return rc


if __name__ == "__main__":
    sys.exit(main())
