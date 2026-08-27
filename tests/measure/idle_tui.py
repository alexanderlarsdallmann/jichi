#!/usr/bin/env python3
"""Idle interactive footprint of a TUI tool over time (M181). A measurement,
never a CI gate.

Spawns the given command in a PTY, sends NO input, and samples the whole
process GROUP's RSS and accumulated CPU time at 1 Hz for --secs seconds
(these tools fork helpers; a single-pid RSS undercounts). Prints
first/median/peak/last RSS and total CPU seconds burned while idle.

Usage:
  python3 tests/measure/idle_tui.py --secs 60 -- ./jichi
  python3 tests/measure/idle_tui.py --secs 60 -- claude
  python3 tests/measure/idle_tui.py --secs 60 -- opencode

No prompts are submitted; the tool idles at its input screen and is then
terminated. Safe to run offline and without API traffic.

CAVEAT (M197): this script does NOT isolate $HOME -- it is meant to compare
whole tools (jichi/claude/opencode) as a user actually runs them, and those need
their real config. A consequence for jichi: starting the TUI creates and saves a
session, so each run leaves one (empty) file in ~/.jichi.d/sessions. That is
harmless, but if you are measuring anything keyed to the session store, pass an
isolated HOME yourself or use tests/measure/session_scan.py, which does.
"""
import argparse
import os
import pty
import signal
import sys
import time


def group_stats(pgid):
    """Sum RSS (KB) and CPU (seconds) over /proc for the process group."""
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
        # after ')': state(0) ppid(1) pgrp(2) ... utime(11) stime(12) ...
        # ... rss(21)   [man proc: stat fields 3..]
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--secs", type=int, default=60)
    ap.add_argument("cmd", nargs="+")
    args = ap.parse_args()

    pid, fd = pty.fork()
    if pid == 0:
        os.environ.setdefault("TERM", "xterm-256color")
        try:
            os.execvp(args.cmd[0], args.cmd)
        except OSError:
            os._exit(127)

    samples = []
    cpu_last = 0.0
    try:
        time.sleep(1.0)  # let it reach the input screen
        for i in range(args.secs):
            # Drain the PTY so the child never blocks on a full buffer.
            try:
                import select
                while select.select([fd], [], [], 0)[0]:
                    if not os.read(fd, 65536):
                        break
            except OSError:
                pass
            rss, cpu = group_stats(pid)
            if rss <= 0:
                print("idle_tui: process group vanished at %ds" % i,
                      file=sys.stderr)
                break
            samples.append(rss)
            cpu_last = cpu
            time.sleep(1.0)
    finally:
        try:
            os.killpg(pid, signal.SIGTERM)
        except OSError:
            pass
        time.sleep(0.5)
        try:
            os.killpg(pid, signal.SIGKILL)
        except OSError:
            pass
        try:
            os.waitpid(pid, 0)
        except OSError:
            pass

    if not samples:
        print("idle_tui: no samples (command failed to start?)",
              file=sys.stderr)
        return 1
    s = sorted(samples)
    print("cmd: %s" % " ".join(args.cmd))
    print("samples: %d @1Hz | rss first %d KB, median %d KB, peak %d KB, "
          "last %d KB | idle CPU %.1f s" %
          (len(samples), samples[0], s[len(s) // 2], s[-1], samples[-1],
           cpu_last))
    return 0


if __name__ == "__main__":
    sys.exit(main())
