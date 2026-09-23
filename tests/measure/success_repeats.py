#!/usr/bin/env python3
"""How often does a turn repeat a SUCCESSFUL tool call and get the same answer?

WHY THIS EXISTS.  M432's in-turn loop detector counts FAILED calls only -- the
branch that feeds it is `if (res.is_error)` in src/chat/jc_agent.c, and its
header (include/jc_toolloop.h) says so.  A model that re-issues a call that
SUCCEEDS, and gets the same answer back, is invisible to it.  M687 is the
incident: 200 tool calls, 0 errors, 4,887,733 tokens, the answer already in hand
by call sixteen, and nothing but --max-tool-calls to stop it.  Before anyone
builds a detector for that shape, its thresholds should be fitted to a corpus
the way M432's were (">= 3 exact and >= 4 class fire on every measured loop
while staying clear of the 2x tail") -- not guessed.  This script is the corpus
half.  M713 wrote it; docs/analysis/2026-09-23-what-to-build-next.md is the
first reading of it.

WHAT IT READS.  Telemetry JSONL -- by default every *.jsonl under
~/.jichi.d/telemetry, recursively -- and from it only `tool_call` events.  At the
default `metrics` tier those carry `name`, `ok`, `output_bytes`, `sid`, `turn`,
`depth`, `ts` and `args`, where `args` is an argument SUMMARY of at most 160
characters (jc_tool_arg_summary), not the arguments.  Nothing is written and no
argument text is printed: the output is counts, tool names and dates.

WHAT A "REPEAT" IS HERE, and why it is only a proxy.  Within one (session, turn,
depth), a successful call whose (tool, args summary, output_bytes) triple equals
an earlier successful call's.  Two things make that weaker than "the same call
got the same answer", and both bias it in known directions:

  1. `args` is a summary.  For read_file it is the PATH ONLY -- the M287 trap:
     a model paging through a large file looks exactly like a model re-reading
     it.  read_file is therefore EXCLUDED unless --include-read is given, and
     the page that uses these numbers says which it reports.
  2. Equal byte counts are not equal bytes.  A detector inside the loop can hash
     the result and settle it; an offline reader at this tier cannot.  So a
     count here can include two different answers of the same length.

  SINCE M715 THE FIRST WEAKNESS IS GONE AT THE `metrics` TIER TOO, for read_file:
  its event carries `offset` and `limit` -- the range the tool EXECUTED, reported
  by the tool itself -- and such an event is keyed on (path, offset, limit), so
  paging through a file no longer looks like re-reading it.  A read_file event
  without them (an older build) is still path-only, and the output counts both
  kinds, so --include-read on a mixed corpus shows how much of it is ambiguous.

  AT THE `full` TIER BOTH WEAKNESSES GO AWAY, and the script uses it when it can.
  A `full` event also carries `args_full` (the whole arguments) and `output` (the
  result text, truncated at the event-log cap), so an event that has both is keyed
  on (tool, args_full, output_bytes, sha1 of output) -- the same call and the same
  bytes back, which is what a detector in the loop would compare.  read_file is
  then no longer ambiguous (offset and limit are in args_full), but it stays
  excluded by default so that runs at the two tiers are counted the same way;
  --include-read counts it.  The first line of output says which key each event
  used, so a corpus mixing the tiers is visible as one.

MUTATING tools are excluded outright: an edit's success twin is M105's
jc_editwatch, which already owns repeated edits, and a repeated todo write is
bookkeeping rather than a question asked twice.  The universe is PRINTED --
which tools were counted and which were left out, with their call counts -- so a
tool added after this script is visible as uncounted instead of silently absent.

TWO COUNTS, because the first one over-reads.  "raw" counts every identical
repeat in the turn.  But edit -> run_tests -> edit -> run_tests with the same
passing output is re-verification, not a loop: something changed between the
two identical calls.  "unchanged" therefore resets a turn's counts after every
SUCCESSFUL mutating call, so it counts only "the same question, the same answer,
and nothing changed in between" -- the shape a no-progress detector would act
on.  One gap remains and is stated: a shell command can change the tree, and at
this tier nothing says whether it did, so run_terminal_command never resets the
count.  (Inside the loop jichi can tell -- M689's "a shell command that provably
changed nothing" -- which is one more reason the detector belongs there.)

THE FLOOR.  Below --min-turns turns in the window (default 50) the rates are
printed under a NOT EVIDENCE banner, in the spirit of reread_ratio.py's 50-call
floor: a script that happily computes a rate from ten turns is how a register
row gets closed by accident.  Window with --since/--until (YYYY-MM-DD, local
time) and read the build stamps it prints -- a rate mixed across builds describes
none of them, and events older than M290 carry no stamp at all.

Usage:
  python3 tests/measure/success_repeats.py                      # ~/.jichi.d/telemetry
  python3 tests/measure/success_repeats.py --since 2026-08-14   # post-M432 only
  python3 tests/measure/success_repeats.py DIR_OR_FILE ... --include-read
"""
import argparse
import collections
import datetime
import glob
import hashlib
import json
import os
import sys

# Tools whose success changes state.  A repeat of one of these is not a question
# asked twice, so it is outside this measurement's universe (see the header).
MUTATING = {
    "edit_file", "write_file", "apply_patch", "multi_edit", "format_file",
    "todo_write", "todowrite", "todoadd", "todoedit", "remember", "write_plan",
    "git_commit", "git_add", "spawn_parallel", "spawn_subagent",
    "ask_user", "ask_for_help", "hint",
}
PAGED = {"read_file"}          # args summary is the path only (M287)
THRESHOLDS = (3, 5, 10)


def day_of(ts):
    """`ts` is epoch milliseconds on current events; tolerate seconds and ISO."""
    try:
        if isinstance(ts, (int, float)):
            secs = ts / 1000.0 if ts > 1e11 else float(ts)
            return datetime.datetime.fromtimestamp(secs).strftime("%Y-%m-%d")
        return str(ts)[:10]
    except (ValueError, OverflowError, OSError):
        return "?"


def iter_files(paths):
    for p in paths:
        if os.path.isdir(p):
            for f in sorted(glob.glob(os.path.join(p, "**", "*.jsonl"),
                                      recursive=True)):
                yield f
        elif os.path.isfile(p):
            yield p


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("paths", nargs="*",
                    default=[os.path.expanduser("~/.jichi.d/telemetry")])
    ap.add_argument("--since", help="first day included, YYYY-MM-DD")
    ap.add_argument("--until", help="first day EXCLUDED, YYYY-MM-DD")
    ap.add_argument("--include-read", action="store_true",
                    help="count read_file too (its args summary is the path only)")
    ap.add_argument("--min-turns", type=int, default=50)
    a = ap.parse_args()

    turns = collections.defaultdict(list)
    builds = collections.Counter()
    keyed = collections.Counter()
    nfiles = 0
    for f in iter_files(a.paths):
        nfiles += 1
        with open(f, errors="replace") as fh:
            for line in fh:
                try:
                    e = json.loads(line)
                except ValueError:
                    continue
                if not isinstance(e, dict) or e.get("event") != "tool_call":
                    continue
                d = day_of(e.get("ts"))
                if a.since and d < a.since:
                    continue
                if a.until and d >= a.until:
                    continue
                builds[e.get("jichi", "(unstamped: pre-M290)")] += 1
                seq = e.get("seq")
                full_args = e.get("args_full")
                out_text = e.get("output")
                if isinstance(full_args, str) and isinstance(out_text, str):
                    # full tier: the whole arguments and a hash of the result
                    keyed["full (args_full + sha1 of output)"] += 1
                    args_key = full_args
                    bytes_key = (e.get("output_bytes"), hashlib.sha1(
                        out_text.encode("utf-8", "replace")).hexdigest())
                elif ("offset" in e and "limit" in e
                      and e.get("name") in PAGED):
                    # M715: the executed range beside the path summary
                    keyed["summary + executed range (M715)"] += 1
                    args_key = (e.get("args"), e.get("offset"), e.get("limit"))
                    bytes_key = e.get("output_bytes")
                else:
                    keyed["summary (args + output_bytes)"] += 1
                    args_key = e.get("args")
                    bytes_key = e.get("output_bytes")
                    if e.get("name") in PAGED:
                        keyed["  of which read_file, path only (ambiguous)"] += 1
                turns[(e.get("sid"), e.get("turn"), e.get("depth"))].append(
                    (seq if isinstance(seq, (int, float)) else 0,
                     e.get("name"), args_key, bytes_key,
                     e.get("ok"), d))

    excluded = set(MUTATING) | (set() if a.include_read else PAGED)
    counted_tools = collections.Counter()
    uncounted_tools = collections.Counter()
    ncalls = 0
    hits = {t: 0 for t in THRESHOLDS}
    hits_unch = {t: 0 for t in THRESHOLDS}
    fail_hits = 0
    worst = []
    repeat_tools = collections.Counter()
    days = []
    for calls in turns.values():
        calls.sort(key=lambda c: c[0])
        ncalls += len(calls)
        days.append(calls[0][5])
        ok_keys = collections.Counter()      # raw: the whole turn
        unch_keys = collections.Counter()    # reset after each state change
        unch_top = 0
        fail_keys = collections.Counter()
        for (_seq, name, args, nbytes, ok, _d) in calls:
            if name in excluded:
                uncounted_tools[name] += 1
                if name in MUTATING and ok is True:
                    unch_top = max(unch_top, max(unch_keys.values())
                                   if unch_keys else 0)
                    unch_keys.clear()
                continue
            counted_tools[name] += 1
            if not args:
                continue
            if ok is True:
                ok_keys[(name, args, nbytes)] += 1
                unch_keys[(name, args, nbytes)] += 1
            elif ok is False:
                fail_keys[(name, args)] += 1
        unch_top = max(unch_top, max(unch_keys.values()) if unch_keys else 0)
        top = max(ok_keys.values()) if ok_keys else 0
        for t in THRESHOLDS:
            if top >= t:
                hits[t] += 1
            if unch_top >= t:
                hits_unch[t] += 1
        if fail_keys and max(fail_keys.values()) >= 3:
            fail_hits += 1
        if top >= THRESHOLDS[0]:
            tool = ok_keys.most_common(1)[0][0][0]
            repeat_tools[tool] += 1
            worst.append((top, unch_top, len(calls), tool, calls[0][5]))

    nturns = len(turns)
    span = "%s .. %s" % (min(days), max(days)) if days else "(empty)"
    print("files scanned: %d   turns with tool calls: %d   tool calls: %d"
          % (nfiles, nturns, ncalls))
    print("window: %s   (--since %s, --until %s)"
          % (span, a.since or "-", a.until or "-"))
    print("builds on these events: %s" % ", ".join(
        "%s x%d" % kv for kv in builds.most_common()))
    print("repeat key used: %s" % (", ".join(
        "%s x%d" % kv for kv in keyed.most_common()) or "(no events)"))
    print("counted tools: %s" % (", ".join(
        "%s %d" % kv for kv in counted_tools.most_common()) or "(none)"))
    print("NOT counted (mutating%s): %s" % (
        "" if a.include_read else ", or paged read_file",
        ", ".join("%s %d" % kv for kv in uncounted_tools.most_common())
        or "(none)"))
    if nturns < a.min_turns:
        print("")
        print("NOT EVIDENCE: %d turns is below the floor of %d. The counts below"
              % (nturns, a.min_turns))
        print("describe this window and nothing wider.")
    print("")
    print("turns where one successful (tool, args, bytes) repeats >= N times:")
    print("      N    raw (whole turn)     unchanged (nothing mutated between)")
    for t in THRESHOLDS:
        pr = 100.0 * hits[t] / nturns if nturns else 0.0
        pu = 100.0 * hits_unch[t] / nturns if nturns else 0.0
        print("   >=%2d   %4d  (%5.1f%%)       %4d  (%5.1f%%)"
              % (t, hits[t], pr, hits_unch[t], pu))
    print("for comparison, turns where one FAILED (tool, args) repeats >= 3x "
          "(M432's domain): %d" % fail_hits)
    if repeat_tools:
        print("tools behind the raw >= %dx turns: %s" % (THRESHOLDS[0], ", ".join(
            "%s %d" % kv for kv in repeat_tools.most_common())))
    if worst:
        print("worst turns (raw repeats, unchanged repeats, calls in turn, "
              "tool, day):")
        for w in sorted(worst, reverse=True)[:10]:
            print("   %4d  %4d  %4d  %-22s %s" % w)
    return 0


if __name__ == "__main__":
    sys.exit(main())
