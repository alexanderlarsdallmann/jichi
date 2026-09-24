#!/usr/bin/env python3
"""Replay recorded tool calls through D1's no-progress watch, as built (M733).

WHY THIS EXISTS.  success_repeats.py measured the corpus D1's threshold was
fitted on (M731), with the offline rule it could compute: mutating tools reset,
shell commands never do.  The watch that shipped in src/util/jc_toolloop.c differs
in three ways the loop can afford and the fit could not -- a shell command resets
every OTHER call's count, read_file and the todo list are left to their owners,
and the table is 32 entries with the oldest forgotten -- so a claim about where
the built note fires has to be measured with the built rule.  This is that
rule, replayed over telemetry: which turns the note would have been told in, at
which call, for which tool.

WHAT IT MIRRORS, BY HAND.  jc_noprogress_role and jc_noprogress_note at M733.
The read-only flags come from the binary itself (`jichi describe --output json`),
so a tool added later is classified as the build classifies it; the four name
lists below are copied from jc_noprogress_role and are the part that can drift.
Tonight's check on the copy: a drive run on the M733 build journals a
`no_progress` event for every note it told, and this script over the same
drive's telemetry must name the same calls.  A disagreement means one of the two
has moved.

WHAT IT READS.  Telemetry JSONL (files or directories, recursively), tool_call
events only -- compaction does not reset the watch (jc_toolloop.h says why).  At
the `full` tier a call is keyed on
its whole arguments and a hash of its result, as the loop keys it; an event
below that tier is keyed on the argument summary and the output size, and the
first line says how many such events there were -- a replay on them is weaker
than the loop, in the direction of false repeats.

Usage:
  python3 tests/measure/noprogress_replay.py DIR_OR_FILE ... [--jichi ./jichi]
"""
import argparse
import collections
import glob
import hashlib
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import corpus_filter  # noqa: E402 -- M720: which sessions are real

# Copied from jc_noprogress_role (src/util/jc_toolloop.c).
IGNORED = {"read_file", "read_background_output", "todowrite", "todoread"}
OUTSIDE = {"ask_user", "ask_for_help", "hint"}
RUNNERS = {"run_terminal_command", "run_tests"}
SHELL = "run_terminal_command"
NOTE_AT = 3          # JC_NOPROGRESS_NOTE_AT
TABLE = 32           # JC_NOPROGRESS_MAX_ENTRIES


def readonly_flags(binary):
    try:
        out = subprocess.run([binary, "describe", "--output", "json"],
                             stdin=subprocess.DEVNULL, capture_output=True,
                             timeout=60, check=False).stdout
        tools = json.loads(out).get("tools") or []
    except (OSError, ValueError, subprocess.SubprocessError) as e:
        sys.exit("cannot read tool flags from %s describe: %s" % (binary, e))
    return {t.get("name"): bool(t.get("readonly")) for t in tools}


def role(tool, ro):
    if not tool or tool in IGNORED:
        return "ignore"
    if tool in OUTSIDE:
        return "reset"
    if tool in RUNNERS:
        return "count"
    return "count" if ro.get(tool, False) else "reset"


def replay(events, ro):
    """events: (seq, kind, name, key, ok) in call order -> [(call_no, tool, n)]"""
    ent = []      # [key, count, told, last]
    notes = []
    clock = 0
    calls = 0
    for _seq, _kind, name, key, ok in events:
        calls += 1
        if ok is not True:
            continue           # failures are M432's
        r = role(name, ro)
        if r == "ignore":
            continue
        if r == "reset":
            for e in ent:
                e[1] = 0
            continue
        clock += 1
        k = (name, key)
        hit = next((e for e in ent if e[0] == k), None)
        if hit is None:
            if len(ent) >= TABLE:
                ent.remove(min(ent, key=lambda e: e[3]))
            hit = [k, 0, False, 0]
            ent.append(hit)
        hit[1] += 1
        hit[3] = clock
        if name == SHELL:
            for e in ent:
                if e is not hit:
                    e[1] = 0
        if not hit[2] and hit[1] >= NOTE_AT:
            hit[2] = True
            notes.append((calls, name, hit[1]))
    return notes, calls


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--jichi", default="./jichi",
                    help="the binary whose tool flags classify the calls")
    ap.add_argument("--include-synthetic", action="store_true")
    a = ap.parse_args()

    ro = readonly_flags(a.jichi)
    files = []
    for p in a.paths:
        if os.path.isdir(p):
            files += sorted(glob.glob(os.path.join(p, "**", "*.jsonl"),
                                      recursive=True))
        elif os.path.isfile(p):
            files.append(p)
    pop = corpus_filter.Population().feed_files(files)
    drop = set() if a.include_synthetic else pop.synthetic_sids()

    turns = collections.defaultdict(list)
    run_of = {}
    weak = 0
    for f in files:
        with open(f, errors="replace") as fh:
            for line in fh:
                try:
                    e = json.loads(line)
                except ValueError:
                    continue
                if not isinstance(e, dict) or e.get("sid") in drop:
                    continue
                ev = e.get("event")
                tkey = (e.get("sid"), e.get("turn"), e.get("depth"))
                seq = e.get("seq") if isinstance(e.get("seq"), (int, float)) else 0
                if ev != "tool_call":
                    continue
                if isinstance(e.get("args_full"), str) and isinstance(e.get("output"), str):
                    key = (e["args_full"], hashlib.sha1(
                        e["output"].encode("utf-8", "replace")).hexdigest())
                else:
                    weak += 1
                    key = (e.get("args"), e.get("output_bytes"))
                turns[tkey].append((seq, "call", e.get("name"), key, e.get("ok")))
                if e.get("run") and tkey not in run_of:
                    run_of[tkey] = e["run"]

    print(pop.session_report(a.include_synthetic))
    print("turns: %d   events below the full tier (weaker key): %d"
          % (len(turns), weak))
    told = 0
    total = 0
    for tkey, evs in sorted(turns.items(), key=lambda kv: run_of.get(kv[0], "")):
        evs.sort(key=lambda x: x[0])
        notes, calls = replay(evs, ro)
        if notes:
            told += 1
            total += len(notes)
            print("  run %s  calls %d  notes at %s" % (
                run_of.get(tkey, "?"), calls,
                ", ".join("%d (%s)" % (c, t) for c, t, _n in notes)))
    print("the note would be told in %d of %d turns, %d times in all"
          % (told, len(turns), total))


if __name__ == "__main__":
    main()
