#!/usr/bin/env python3
"""Does the agent repeat a tool call that just failed? A measurement, never a gate.

The question behind it: "self-healing agent loops" usually means recovering
from failure without a human. jichi recovers in several senses already (retry
ladder, jsonrepair, verify fix-forward, mid-turn compaction, rollback) -- but
nothing notices, WITHIN a turn, that the same call keeps failing the same way.
Before building that, measure whether it happens.

    python3 tests/measure/repeat_failures.py                 # ~/.jichi.d/telemetry
    python3 tests/measure/repeat_failures.py PATH [PATH...]  # other logs
    python3 tests/measure/repeat_failures.py --continue-sessions ~/.jichi.d/sessions

WHAT COUNTS AS A REPEAT. The same (tool, target) failing >= 2 times inside one
turn, where `target` is the metrics-tier `args` summary -- the path, command or
query the call was about. Per TURN, not per session: a file edited four times
across an afternoon is ordinary work; the same edit failing four times in one
turn is a loop. This is the distinction the existing redo-loop detector
(jc_insights_redo_loops) does not make -- it reads sessions, counts only
edit_file/write_file, keys on the path, spans the whole session, and does not
look at whether the call FAILED at all. It measures churn.

THE PARAPHRASED FORM (added 2026-08-09, after the first run on jichi's own
telemetry). The exact key above reported ZERO repeats over a log that contained
a 32x loop: one turn re-ran `git log --oneline --all -N | head/tail -M` with N
escalating from 300 to 20,000,000, every attempt killed at the output cap --
same tool, same failure, different bytes in `args` every time. An exact-match
key is blind to a loop that varies a constant, which the calibration corpus
suggests is the COMMON form. So a second, weaker measure is reported beside it:
the same (tool, exit class) failing >= 3 times in one turn across >= 2 distinct
targets. Weaker because distinct probes legitimately share an exit code (three
different greps exiting 1 is ordinary work) -- read its examples, not just its
count, before believing it. The 32x loop also had a cause worth naming: every
kill note said "exceeded the memory budget" when the byte cap had fired (fixed
as M342), so the model was escalating against a false diagnosis. A loop
detector that names the wrong cause is a loop AMPLIFIER.

TURN BOUNDARIES come from the event's own `turn` field, which every telemetry
event carries alongside `sid` and `ws`. The first draft counted `turn_start`
events instead -- and looked for an event named `turn`, which does not exist --
so every log would have collapsed to one turn per session and inflated the
repeat rate. Two lessons kept here rather than tidied away: read what the data
actually contains before parsing it, and make a degraded read SAY so. A log
with neither is treated as one turn per session and reported as such, which
OVERSTATES repeats -- a null result there still means something, a positive one
does not.

CALIBRATION. On 294 Continue sessions of the same operator's real work
(2026-08-07): 1081 tool calls, 177 errored (16.4%), 50 turns with at least one
error, and 25 of those 50 repeating an identical failing call -- tail to 10x.
That is a different agent with different recovery machinery; it says the failure
mode is real in this workload, not that jichi shares its rate.
"""

import argparse
import collections
import glob
import json
import os
import sys


def load_events(paths):
    """Yield (file, event dict) from JSONL telemetry logs."""
    for p in paths:
        try:
            with open(p, encoding="utf-8", errors="replace") as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        yield p, json.loads(line)
                    except ValueError:
                        continue
        except OSError as e:
            print("skip %s: %s" % (p, e), file=sys.stderr)


def scan_telemetry(paths):
    """Group tool_call events into turns and count repeated failures.

    Returns (stats, examples). A session is keyed by `sid` when present so a
    shared log across projects does not merge two runs into one turn.
    """
    turns = collections.defaultdict(collections.Counter)   # turn key -> fails
    classes = collections.defaultdict(dict)  # turn key -> (tool, exit) -> [targets]
    order = []
    calls = errored = 0
    no_turn_events = True
    cur = {}
    for path, e in load_events(paths):
        ev = e.get("event")
        sid = e.get("sid") or e.get("session") or path
        if ev == "turn_start":
            cur[sid] = cur.get(sid, 0) + 1
            continue
        if ev != "tool_call":
            continue
        calls += 1
        if e.get("ok"):
            continue
        errored += 1
        # EVERY event carries `turn` (and `sid`, and `ws`). Use it: counting
        # turn_start myself was a second source of truth for a fact the log
        # already states, and the first draft got even that wrong by looking
        # for an event named `turn`.
        t = e.get("turn")
        if t is None:
            t = cur.get(sid, 0)
        else:
            no_turn_events = False
        key = (sid, t)
        if key not in turns:
            order.append(key)
        target = e.get("args") or "(no argument summary)"
        turns[key][(e.get("name", "?"), target)] += 1
        cls = (e.get("name", "?"), e.get("exit"))
        classes[key].setdefault(cls, []).append(target)
    return _summarise(turns, order, calls, errored, no_turn_events, classes)


def scan_continue_sessions(dirs):
    """The same measurement over Continue-format sessions (history/toolCallStates).

    Present because that is what exists in quantity on this machine, and because
    the operator's other project may also predate jichi telemetry. Labelled
    separately in the output: it is a DIFFERENT agent's failure profile.
    """
    SAL = ("file_path", "filepath", "path", "file", "dirpath",
           "command", "query", "pattern", "url", "name")

    def target(args):
        try:
            a = json.loads(args) if isinstance(args, str) else (args or {})
        except ValueError:
            return "(unparseable arguments)"
        if not isinstance(a, dict):
            return "(unparseable arguments)"
        for k in SAL:
            v = a.get(k)
            if isinstance(v, str) and v:
                return v[:80]
        return "(no salient argument)"

    turns = collections.defaultdict(collections.Counter)
    classes = collections.defaultdict(dict)
    order = []
    calls = errored = 0
    files = []
    for d in dirs:
        files.extend(glob.glob(os.path.join(d, "*.json")))
    for p in files:
        try:
            d = json.load(open(p, encoding="utf-8", errors="replace"))
        except Exception:
            continue
        if not isinstance(d, dict):
            continue
        turn = 0
        for m in d.get("history", []):
            if (m.get("message") or {}).get("role") == "user":
                turn += 1
            for t in (m.get("toolCallStates") or []):
                calls += 1
                if t.get("status") != "errored":
                    continue
                errored += 1
                fn = ((t.get("toolCall") or {}).get("function") or {})
                key = (os.path.basename(p), turn)
                if key not in turns:
                    order.append(key)
                tgt = target(fn.get("arguments"))
                turns[key][(fn.get("name", "?"), tgt)] += 1
                # Continue sessions carry no exit code; the class is the tool
                # alone, which is weaker still -- the report says so.
                classes[key].setdefault((fn.get("name", "?"), None),
                                        []).append(tgt)
    return _summarise(turns, order, calls, errored, False, classes)


def _summarise(turns, order, calls, errored, no_turn_events, classes=None):
    hist = collections.Counter()
    tools = collections.Counter()
    examples = []
    para_examples = []
    with_repeat = 0
    with_para = 0
    for key in order:
        rep = [(k, v) for k, v in turns[key].items() if v >= 2]
        if rep:
            with_repeat += 1
            for k, v in rep:
                hist[v] += 1
                tools[k[0]] += 1
                examples.append((v, k[0], k[1], key))
        # The paraphrased form: same (tool, exit class) failing >= 3 times on
        # >= 2 distinct targets. The distinct-target floor keeps it disjoint
        # from the exact measure above (one target is already exact); the >= 3
        # floor keeps two unrelated same-exit probes from counting.
        hit = False
        for cls, tgts in (classes or {}).get(key, {}).items():
            if len(tgts) >= 3 and len(set(tgts)) >= 2:
                hit = True
                para_examples.append((len(tgts), cls[0], cls[1],
                                      tgts[0], tgts[-1]))
        if hit:
            with_para += 1
    return ({"calls": calls, "errored": errored,
             "turns_with_error": len(order),
             "turns_with_repeat": with_repeat,
             "turns_with_para": with_para,
             "hist": hist, "tools": tools,
             "no_turn_events": no_turn_events}, (examples, para_examples))


def _rank(t):
    """Sort key for the worst-offender tables.

    A plain `sorted(rows, reverse=True)` compares the tuples element by element,
    so two rows tying on (count, tool) fall through to the EXIT CODE -- which is
    an int for shell tools and None for every other tool. That raises
    TypeError: '<' not supported between instances of 'int' and 'NoneType'.

    Latent until a corpus had such a tie: the zigodot logs never did, the chrtext
    logs (17,553 tool calls, both shell and edit_file loops) do on the first run.
    So the fix is a key that stringifies every element past the count rather than
    a guard at the call site -- there are two call sites and both had the bug.
    """
    return (t[0],) + tuple("" if x is None else str(x) for x in t[1:])


def report(label, stats, examples):
    exact, para = examples
    c, e = stats["calls"], stats["errored"]
    print("== %s ==" % label)
    if c == 0:
        print("  no tool calls found -- nothing to measure yet.\n")
        return
    print("  tool calls: %d   errored: %d (%.1f%%)" % (c, e, 100.0 * e / c))
    t, r = stats["turns_with_error"], stats["turns_with_repeat"]
    print("  turns with >=1 tool error: %d" % t)
    if t:
        print("  turns repeating an identical failing call: %d (%.0f%%)"
              % (r, 100.0 * r / t))
        print("  turns with a PARAPHRASED repeat (same tool+exit, >=3 fails,"
              " varied target): %d (%.0f%%)"
              % (stats["turns_with_para"],
                 100.0 * stats["turns_with_para"] / t))
    if stats["no_turn_events"]:
        print("  NOTE: no `turn_start` events in this log, so each session was"
              " treated as ONE turn.\n        That inflates repeats -- read a"
              " positive result here with suspicion.")
    if stats["hist"]:
        print("  repeat distribution: " +
              ", ".join("%dx:%d" % (k, v) for k, v in sorted(stats["hist"].items())))
        print("  tools: " + ", ".join("%s:%d" % kv
                                      for kv in stats["tools"].most_common(6)))
        print("  worst (identical):")
        for v, tool, tgt, key in sorted(exact, key=_rank, reverse=True)[:5]:
            print("    %3dx %-12s %s" % (v, tool, str(tgt)[:60]))
    if para:
        print("  worst (paraphrased -- read these, a shared exit code is not"
              " proof of a loop):")
        for v, tool, ex, first, last in sorted(para, key=_rank, reverse=True)[:5]:
            print("    %3dx %-12s exit=%-4s %s" % (v, tool, ex, str(first)[:52]))
            print("         %-12s      ... %s" % ("", str(last)[:52]))
    print()


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*",
                    help="telemetry .jsonl files or directories "
                         "(default: ~/.jichi.d/telemetry)")
    ap.add_argument("--continue-sessions", metavar="DIR", action="append",
                    default=[],
                    help="also scan Continue-format sessions in DIR")
    args = ap.parse_args()

    paths = []
    for p in (args.paths or [os.path.expanduser("~/.jichi.d/telemetry")]):
        if os.path.isdir(p):
            paths.extend(sorted(glob.glob(os.path.join(p, "*.jsonl"))))
        else:
            paths.append(p)
    if paths:
        report("jichi telemetry (%d file(s))" % len(paths), *scan_telemetry(paths))
    else:
        print("== jichi telemetry ==\n  no .jsonl logs found. Enable it with:"
              "\n    jichi --config ~/.jichi --config-editable config telemetry"
              " metrics\n")
    if args.continue_sessions:
        report("Continue sessions (a DIFFERENT agent -- calibration only)",
               *scan_continue_sessions(args.continue_sessions))


if __name__ == "__main__":
    main()
