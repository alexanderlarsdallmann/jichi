#!/usr/bin/env python3
"""How much of a run's reading is RE-reading, and did compaction fire? A measurement.

Two DEFERRED rows are blocked on numbers this computes, and both name the same
missing thing: a workload under real context pressure.

  M326z ("a doctor check for a high re-read ratio") measured 72% on ONE workload
  -- 2,056 read_file calls over 584 distinct paths, one path read 216 times --
  and refuses to ship advice on a single sample. Its revisit condition is a
  second workload measured POST-M348, because M348 attacked the loop
  mechanically: the elision marker became a claim ticket naming a preservation
  store, so the re-read the loop consists of has a cheap targeted substitute.
  "If the ratio collapses, this row closes without doctor ever advising."

  M326y ("a mid-turn mechanism for turns eliding cannot save") needs the
  `unrelieved` share on a post-M326y log. The 2026-08-10 sweep found ZERO
  compact events in 364 telemetry events: ordinary use never presses, so the
  row needs a deliberately pressured corpus rather than a bigger sample of calm.

    python3 tests/measure/reread_ratio.py STREAM.jsonl [TELEMETRY.jsonl ...]
    python3 tests/measure/reread_ratio.py DIR

WHAT COUNTS. The re-read ratio is (calls - distinct paths) / calls over
`read_file` only. Reading two different files is not re-reading; reading one
file twice is. A run with one read has no ratio worth quoting and this says so
rather than printing 0% -- SMALL_N below is the floor, and the M326z reference
is 2,056 calls, so a handful of doc edits cannot confirm or refute it. Printing
"67%" from four repeats would be the overclaim these rows exist to avoid.

WHERE THE DATA IS. Paths come from the --output jsonl STREAM (tool_call events
carry `args`) or, since M715, from TELEMETRY (--log-level metrics), whose
tool_call events carry the path as `args` for read_file; the run journal
deliberately does not record them. Compaction comes from telemetry too, whose
`compact` events carry a `phase`; `unrelieved` marks a mid-turn compaction that
could not reach its target, which is precisely the turn M326y is about.

TWO ROUTES, NEVER SUMMED. A run driven with both --output jsonl and --log writes
each read twice, once per sink, so each route is counted and printed on its own
-- adding them would double every call.

PATHS VERSUS RANGES. The ratio above is over PATHS: a model paging through one
big file (offset 1, 201, 401 ...) re-reads nothing, and still counts. Telemetry
events from an M715+ build also carry `offset` and `limit` -- the range the tool
EXECUTED, reported by the tool -- so for those this also prints the stricter
RANGE ratio: the same path AND the same lines read again. It is computed only
from those events. The stream's raw `args` are not re-parsed for a range here:
jichi repairs and unwraps arguments before the tool sees them, so a second
parser would measure what was sent rather than what ran.
"""

import argparse
import collections
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import corpus_filter  # noqa: E402 -- M720: which sessions are real

# Below this, a percentage is arithmetic rather than evidence. The reference
# measurement this is compared against is 2,056 calls.
SMALL_N = 50


def load(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            files.extend(sorted(glob.glob(os.path.join(p, "*.jsonl"))))
        else:
            files.append(p)
    return files


def scan(files, drop=frozenset()):
    reads, tools, compacts = [], collections.Counter(), collections.Counter()
    # M715: the telemetry route -- paths (every build) and executed ranges (M715+)
    treads, tranges = [], []
    for f in files:
        try:
            # M720: errors="replace", as every other script here -- a real
            # corpus holds output cut mid-character at the event-log cap, and
            # this route died on the workstation's on the first such line.
            fh = open(f, errors="replace")
        except OSError:
            continue
        with fh:
            for line in fh:
                try:
                    d = json.loads(line)
                except ValueError:
                    continue
                # stream: a tool call, with its arguments
                if d.get("type") == "tool_call":
                    name = d.get("name") or "?"
                    tools[name] += 1
                    if name == "read_file":
                        try:
                            a = json.loads(d.get("args") or "{}")
                        except ValueError:
                            a = {}
                        if a.get("path"):
                            reads.append(a["path"])
                # telemetry: a read, with the range it executed when recorded
                if (d.get("event") == "tool_call" and d.get("name") == "read_file"
                        and d.get("sid") not in drop):
                    if d.get("args"):
                        treads.append(d["args"])
                        if "offset" in d and "limit" in d:
                            tranges.append((d["args"], d["offset"], d["limit"]))
                # telemetry: a compaction, and whether it could relieve anything
                if d.get("event") == "compact" or d.get("type") == "compact":
                    compacts[d.get("phase") or "?"] += 1
                    if d.get("unrelieved"):
                        compacts["unrelieved"] += 1
    return reads, tools, compacts, treads, tranges


def ratio_lines(label, keys, what):
    """Print one route's ratio; `keys` are paths, or (path, offset, limit)."""
    n = len(keys)
    uniq = len(set(keys))
    print("%s: %d read_file calls over %d distinct %s" % (label, n, uniq, what))
    if n == 0:
        return
    print("  re-read ratio: %.0f%%   (%d of %d calls re-read a %s already read)"
          % (100.0 * (n - uniq) / n, n - uniq, n, what.rstrip("s")))
    if n < SMALL_N:
        print("  NOT EVIDENCE: %d calls is below the %d-call floor." % (n, SMALL_N))


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+", help="stream/telemetry .jsonl or dirs")
    ap.add_argument("--include-synthetic", action="store_true",
                    help="count mock/probe sessions in the telemetry route too "
                         "(corpus_filter.py)")
    args = ap.parse_args()

    files = load(args.paths)
    if not files:
        print("no .jsonl found in: %s" % " ".join(args.paths))
        return 1
    # M720: the TELEMETRY route is classified by session; a --output jsonl stream
    # is a file the caller chose and carries no model calls, so it is not.
    pop = corpus_filter.Population().feed_files(files)
    drop = frozenset() if args.include_synthetic else frozenset(pop.synthetic_sids())
    print(pop.session_report(args.include_synthetic) + " (telemetry route only)")
    reads, tools, compacts, treads, tranges = scan(files, drop)

    n = len(reads)
    uniq = len(set(reads))
    print("files scanned: %d" % len(files))
    print("stream route (--output jsonl): %d tool calls, %d read_file"
          % (sum(tools.values()), tools.get("read_file", 0)))

    if n == 0 and treads:
        print("  (no read_file in a stream -- the telemetry route is below)")
    elif n == 0:
        print("no read_file call carried a path -- nothing to measure.")
        print("(paths come from the --output jsonl STREAM or from telemetry; a "
              "run journal alone cannot answer this.)")
    else:
        ratio = 100.0 * (n - uniq) / n
        top = collections.Counter(reads).most_common(3)
        print("read_file with a path: %d over %d distinct paths" % (n, uniq))
        print("re-read ratio: %.0f%%   (%d of %d calls re-read a path already read)"
              % (ratio, n - uniq, n))
        print("most-read paths: %s"
              % ", ".join("%s x%d" % (os.path.basename(p), c) for p, c in top))
        if n < SMALL_N:
            print()
            print("NOT EVIDENCE: %d calls is below the %d-call floor. The M326z"
                  % (n, SMALL_N))
            print("reference is 2,056 calls / 584 distinct / 72% / one path 216x.")
            print("A percentage from this few calls neither confirms nor refutes it.")

    # M715: the telemetry route, printed apart from the stream (never summed).
    if treads:
        print()
        ratio_lines("telemetry route, by PATH", treads, "paths")
        if tranges:
            ratio_lines("telemetry route, by RANGE (M715+ events only)",
                        tranges, "ranges")
            if len(tranges) < len(treads):
                print("  (%d of %d telemetry reads predate M715 and carry no range)"
                      % (len(treads) - len(tranges), len(treads)))
        else:
            print("  no telemetry read carries a range -- every event predates M715,")
            print("  so paging and re-reading cannot be told apart on this route.")

    print()
    if not compacts:
        print("compaction: no `compact` events -- this workload never pressed the")
        print("context, so it says nothing about M326y. That was exactly the")
        print("2026-08-10 sweep's finding: 0 compact events in 364 telemetry")
        print("events. A pressured corpus has to be built on purpose.")
    else:
        total = sum(v for k, v in compacts.items() if k != "unrelieved")
        unrel = compacts.get("unrelieved", 0)
        print("compaction: %d event(s)" % total)
        for k, v in sorted(compacts.items()):
            if k != "unrelieved":
                print("  phase %-10s %d" % (k, v))
        if total:
            print("unrelieved: %d of %d (%.0f%%) -- mid-turn compactions that could"
                  % (unrel, total, 100.0 * unrel / total))
            print("            not reach their target (the M326y turns)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
