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
carry `args`); the run journal deliberately does not record them. Compaction
comes from telemetry (--log-level metrics), whose `compact` events carry a
`phase`; `unrelieved` marks a mid-turn compaction that could not reach its
target, which is precisely the turn M326y is about.
"""

import argparse
import collections
import glob
import json
import os
import sys

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


def scan(files):
    reads, tools, compacts = [], collections.Counter(), collections.Counter()
    for f in files:
        try:
            fh = open(f)
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
                # telemetry: a compaction, and whether it could relieve anything
                if d.get("event") == "compact" or d.get("type") == "compact":
                    compacts[d.get("phase") or "?"] += 1
                    if d.get("unrelieved"):
                        compacts["unrelieved"] += 1
    return reads, tools, compacts


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+", help="stream/telemetry .jsonl or dirs")
    args = ap.parse_args()

    files = load(args.paths)
    if not files:
        print("no .jsonl found in: %s" % " ".join(args.paths))
        return 1
    reads, tools, compacts = scan(files)

    n = len(reads)
    uniq = len(set(reads))
    print("files scanned: %d" % len(files))
    print("tool calls: %d total, %d read_file"
          % (sum(tools.values()), tools.get("read_file", 0)))

    if n == 0:
        print("no read_file call carried a path -- nothing to measure.")
        print("(paths come from the --output jsonl STREAM; a run journal alone "
              "cannot answer this.)")
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
