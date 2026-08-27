#!/usr/bin/env python3
"""Would --strict-green have downgraded any legitimate green run? A measurement.

M332 shipped REFUSE-THE-GREEN opt-in and default-off, because turning it on
changes a currently-zero exit code (a stable interface, docs/EMBEDDING.md), and
said the default flip is "a separate, evidenced decision once the false-positive
rate on real runs is known". This script computes that rate RETROACTIVELY from
run journals that already exist: a run strict-green would downgrade is exactly
one that ended `ok` while carrying an `out_of_scope` event -- both already
journaled since M83, so no new runs are needed.

    python3 tests/measure/strict_green_fp.py                # ~/.jichi.d/runs
    python3 tests/measure/strict_green_fp.py DIR [DIR...]   # journal dirs/files

A scope that permits everything is EXCLUDED from the denominator (M459). The
journal used to record only the glob count, so `--edit-scope AGENTS.md` and
`--edit-scope '**'` were indistinguishable and the second contributes a free
zero -- a rate that cannot be falsified. Journals now carry edit_scope_globs;
older ones are counted as before rather than guessed at.

Read the flagged runs' paths before calling one a false positive: the plausible
FP M332 named is an incidental shell-written file (a lock file, a generated
artifact); a genuine one is the gate edited through the shell (ANECDOTES #45).
The denominator that matters is runs with a NONZERO edit scope -- without one,
the out-of-scope diff has nothing to compare against and the run cannot be
flagged at all, so counting it would flatter the rate.

FIRST MEASUREMENT (2026-08-09, the zigodot driving corpus + ~/.jichi.d/runs):
138 journal files, 57 completed runs, 41 with an edit scope; 21 of those ended
`ok` and NONE carried an out-of-scope flag -- 0/21 downgrades, zero false
positives, zero true positives among greens. The two flagged runs both ended
budget_exhausted (strict-green ignores non-ok outcomes). Bounded evidence: one
project, one operator's gates. First scan of this data had two bugs worth
keeping: it looked for a `begin` event (the journal writes `start`), and it
string-matched the `edit_scope` KEY, which every start event carries as a count
-- both directions of the M326b rule (read what the data contains).
"""

import argparse
import glob
import json
import os


def scan(paths):
    rows = []
    for p in paths:
        start = None
        outcome = None
        oos = []
        sg = None
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
                ev = e.get("event")
                if ev == "start":
                    start = e
                elif ev == "out_of_scope":
                    oos.append(e)
                elif ev == "strict_green":
                    sg = e
                elif ev == "end":
                    outcome = e.get("outcome")
        if start is None or outcome is None or outcome == "running":
            continue
        rows.append({"file": os.path.basename(p),
                     "scope": int(start.get("edit_scope") or 0),
                     "globs": start.get("edit_scope_globs"),
                     "outcome": outcome, "oos": oos, "sg": sg})
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
    # A NONZERO scope is not the same as a REAL one. `--edit-scope '**'` records
    # edit_scope: 1 and permits the whole workspace, so nothing can ever be
    # flagged out-of-scope and the run contributes a free 0 to the rate. Before
    # M459 the journal carried only the COUNT, so this was invisible: a corpus of
    # '**' runs reported a perfect false-positive rate while proving nothing.
    # Journals written since M459 carry edit_scope_globs and can be told apart;
    # older ones cannot, and are counted as before rather than guessed at.
    def vacuous(r):
        g = r.get("globs")
        if not g:
            return False          # pre-M459 journal: unknowable, keep as-is
        return any(x in ("**", "*", "**/*") for x in g)

    scoped_all = [r for r in rows if r["scope"] > 0]
    vac = [r for r in scoped_all if vacuous(r)]
    scoped = [r for r in scoped_all if not vacuous(r)]
    ok = [r for r in scoped if r["outcome"] == "ok"]
    downgrades = [r for r in ok if r["oos"]]
    live = [r for r in rows if r["sg"] is not None]

    print("journals scanned: %d   completed runs: %d   with an edit scope: %d"
          % (len(files), len(rows), len(scoped)))
    if vac:
        print("excluded as VACUOUSLY scoped (a glob permitting everything): %d"
              % len(vac))
    print("scoped runs that ended ok: %d" % len(ok))
    print("...of those, flagged out-of-scope (what strict-green downgrades): %d"
          % len(downgrades))
    if len(ok):
        print("observed downgrade rate: %d/%d" % (len(downgrades), len(ok)))
    for r in downgrades:
        for e in r["oos"]:
            print("  READ THIS ONE: %-40s paths=%s"
                  % (r["file"], e.get("paths")))
    flagged_nonok = [r for r in scoped if r["outcome"] != "ok" and r["oos"]]
    print("flagged runs that ended non-ok anyway (untouched by strict-green): %d"
          % len(flagged_nonok))
    if live:
        print("runs where strict_green actually fired (post-M332): %d" % len(live))
        for r in live:
            print("  %-40s was=%s -> outcome=%s"
                  % (r["file"], (r["sg"] or {}).get("was"), r["outcome"]))
    if not scoped:
        print("nothing to measure: no completed scoped runs found.")


if __name__ == "__main__":
    main()
