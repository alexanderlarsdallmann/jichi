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

THE TWO HALVES THE JOURNAL COULD NOT SAY, UNTIL M715.  A capped run is only half
of the question; the other half is "a ONE-SHOT that ANSWERED nothing", and
neither was recorded.  Since M715 the `start` event carries `one_shot` (headless
with --no-session: no session to resume) and the `end` event `answer_bytes` (the
size of THIS turn's answer).  A journal older than that says neither, and its
runs are counted as `unknown` -- never folded into "not a one-shot" or "answered",
which would dilute the rate with runs that could not have reported either.

`answer_bytes` IS A FACT, NOT A VERDICT, and this script keeps it that way: a
capped run's last words ("Let me try with explicit tabs:") are text but not an
answer, so the sizes are printed in buckets and the reading is left to whoever
decides item 7.  The text itself is not in the journal; for a --no-session run it
is only on the run's stdout (or in full-tier telemetry).

    python3 tests/measure/capped_oneshot.py [JOURNAL.jsonl | DIR ...] [--list]
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
    ap.add_argument("--list", action="store_true",
                    help="print one line per capped run (file, shape, "
                         "answer_bytes, tool_calls, tokens_used)")
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

    # M715: the shape of each capped run, from its own start event. The journal
    # is jichi's own sink, so the field is a strict JSON bool -- anything else
    # is a bug to see, and is counted as unknown rather than guessed at.
    def shape(r):
        v = (r["start"] or {}).get("one_shot")
        if v is True:
            return "one_shot"
        if v is False:
            return "session"
        return "unknown"

    def bucket(n):
        if n == 0:
            return "0 bytes (no text at all)"
        if n <= 80:
            return "1-80 bytes (a sentence -- read it before calling it an answer)"
        if n <= 400:
            return "81-400 bytes"
        return "> 400 bytes"

    shapes = {"one_shot": [], "session": [], "unknown": []}
    for r in capped:
        shapes[shape(r)].append(r)
    print("  capped one-shots (start.one_shot true, M715+): %d"
          % len(shapes["one_shot"]))
    print("  capped runs with a session to resume: %d" % len(shapes["session"]))
    if shapes["unknown"]:
        print("  capped runs whose journal predates M715 (shape unknown): %d"
              % len(shapes["unknown"]))

    if args.list:
        print("")
        for r in capped:
            e = r["end"]
            print("  %-44s %-8s answer_bytes=%-6s tool_calls=%-4s tokens=%s"
                  % (r["file"], shape(r),
                     e.get("answer_bytes", "?"), e.get("tool_calls", "?"),
                     e.get("tokens_used", "?")))

    measurable = [r for r in shapes["one_shot"]
                  if isinstance(r["end"].get("answer_bytes"), (int, float))]
    if len(measurable) < FLOOR:
        print("")
        print("NOT EVIDENCE: %d capped one-shots with a recorded answer size is"
              % len(measurable))
        print("  below the floor of %d. The question is whether a capped" % FLOOR)
        print("  one-shot usually answers SOMETHING.  Answering it from a handful")
        print("  of runs is how the --strict-green recommendation came to be")
        print("  withdrawn at M662.  Drive jichi (see docs/DRIVE_LOG.md) with")
        print("  --no-session on an M715+ build and re-run this.")
        return

    sizes = {}
    for r in measurable:
        b = bucket(int(r["end"]["answer_bytes"]))
        sizes[b] = sizes.get(b, 0) + 1
    print("")
    print("what the %d capped one-shots answered (answer_bytes):" % len(measurable))
    for b in ("0 bytes (no text at all)",
              "1-80 bytes (a sentence -- read it before calling it an answer)",
              "81-400 bytes", "> 400 bytes"):
        print("  %4d  %s" % (sizes.get(b, 0), b))
    print("  (sizes, not verdicts: the text is not in the journal. For a")
    print("   --no-session run it is on the run's own stdout, or in full-tier")
    print("   telemetry -- read those before deciding item 7.)")


if __name__ == "__main__":
    main()
