#!/usr/bin/env python3
"""Does a capped one-shot usually answer anything?  (DEFERRED.md item 7)

WHY THIS EXISTS.  M687 made the tool-call cap visible in the reach footer and
the `[envelope]` verdict, and deliberately left the exit code at 0.  M322 chose
that, with a reason that is still good for a SESSION: the cap is a circuit
breaker, the history is intact, and another prompt resumes from it.  The
counter-argument is that a `--no-session` one-shot has no next prompt, so for
that shape the work is gone and the caller was told it succeeded.

Flipping a currently-zero exit code is a stable-tier contract change, and M662
withdrew a `--strict-green` default flip for being proposed ahead of its
measurement.  This script is what would stop the same thing happening again.

WHAT IT CANNOT DO YET, said first because it is the honest part.  Until M690 the
run journal recorded only the envelope's `outcome` -- ok / running /
budget_exhausted / verify_failed -- which CANNOT express `max_iters`.  Measured
over 114 journals and 101 completed runs on 2026-09-21: **zero** capped runs
were identifiable, not because none happened but because the record could not
say so.  DEFERRED item 7 claimed the journal already carried `stop_reason`; that
was read, not measured, and it was wrong.

So this prints NOT EVIDENCE until enough post-M690 journals exist, in the same
spirit as tests/measure/reread_ratio.py's 50-call floor: a script that will
happily compute a rate from four runs is how a row gets closed by accident.
"""

import argparse
import glob
import json
import os

FLOOR = 20          # capped one-shots below which no rate is reported


def scan(paths):
    rows = []
    for p in paths:
        start = end = None
        try:
            f = open(p, encoding="utf-8", errors="replace")
        except OSError:
            continue
        with f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    e = json.loads(line)
                except ValueError:
                    continue
                if e.get("event") == "start":
                    start = e
                elif e.get("event") == "end":
                    end = e
        if end is None:
            continue
        rows.append({"file": os.path.basename(p), "start": start, "end": end})
    return rows


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*",
                    help="journal .jsonl files or directories "
                         "(default: ~/.jichi.d/runs)")
    args = ap.parse_args()

    files = []
    for p in (args.paths or [os.path.expanduser("~/.jichi.d/runs")]):
        if os.path.isdir(p):
            files.extend(sorted(glob.glob(os.path.join(p, "*.jsonl"))))
        else:
            files.append(p)

    rows = scan(files)
    # A journal written before M690 has no stop_reason at all.  Counted
    # separately rather than lumped in as "not capped", which would silently
    # dilute the rate with runs that could not have reported a cap.
    recorded = [r for r in rows if r["end"].get("stop_reason")]
    silent = len(rows) - len(recorded)
    capped = [r for r in recorded if r["end"].get("stop_reason") == "max_iters"]

    print("journals scanned: %d   completed runs: %d" % (len(files), len(rows)))
    print("runs whose journal records a stop_reason (post-M690): %d" % len(recorded))
    if silent:
        print("runs with NO stop_reason recorded (pre-M690): %d "
              "-- these cannot answer this question either way" % silent)
    print("of the recorded ones, ended max_iters: %d" % len(capped))

    if len(capped) < FLOOR:
        print("")
        print("NOT EVIDENCE: %d capped runs is below the floor of %d."
              % (len(capped), FLOOR))
        print("  The question is whether a capped one-shot usually answers")
        print("  SOMETHING.  Answering it from a handful of runs is how the")
        print("  --strict-green recommendation came to be withdrawn at M662.")
        print("  Drive jichi (see docs/DRIVE_LOG.md) and re-run this.")
        return

    answered = sum(1 for r in capped if (r["end"].get("no_changes") is False))
    print("")
    print("capped runs that changed something: %d/%d" % (answered, len(capped)))
    print("  (`no_changes` is the nearest proxy the journal carries; it is not")
    print("   the same as 'produced a final message', and that gap should be")
    print("   closed before this number decides anything.)")


if __name__ == "__main__":
    main()
