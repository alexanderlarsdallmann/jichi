#!/usr/bin/env python3
"""How costly is argument elision: how often is a WRITE re-sent, and how often
does the elided-placeholder copy-back trap fire? A measurement (def-317 / R3).

WHY THIS EXISTS.  M218's mid-turn compaction elides old tool CALL ARGUMENTS:
a write_file/apply_patch body -- the document itself -- is replaced by a ~120
byte placeholder that names the path (if it could extract one) and drops the
content. When the model later needs to repeat the call it must regenerate the
content from memory, or it copies the placeholder back and jichi rejects it
(jc_tool.c: args_repair, kind "elided_placeholder"). The 2026-10-05 audit
(#43) hit exactly this: a 3 KB heredoc re-sent 8+ times, once per elided
round. R3 says: before R1/R2 change the mechanism, count the loop, the way
M201 applies to the audit's own symptom. This is the instrument.

WHAT IT COUNTS (three numbers, printed apart, never summed):

  1. WRITE RE-SENDS.  Within one session, a write-tool call (write_file,
     edit_file, apply_patch) whose (tool, path, sha1 of args_full) equals an
     earlier call's is a re-send: the model paid for the same content again.
     The ratio (calls - distinct) / calls over the write universe is the
     "write re-send ratio" -- the WRITE twin of reread_ratio.py's read
     re-read ratio. It is a COST measure, not a fault: idempotent rewrites
     count too, and only the elision join below says which ones elision drove.

  2. ELIDED-PLACEHOLDER REJECTIONS.  args_repair events with kind
     "elided_placeholder", by tool -- the M289 copy-back trap firing. These
     are the unambiguous "the model hit the placeholder and bounced" events;
     R2's telemetry makes them queryable, so they are counted here now, not
     after R2 lands.

  3. THE COARSE JOIN.  A session that shows BOTH a write re-send (1) and an
     elided-placeholder rejection (2) is the population where the two loop
     together. It is reported as a count of sessions, NOT attributed to
     individual re-sends -- at the `metrics` tier a re-send's args_full is
     absent, and even at the `full` tier an identical re-send after a rejection
     is consistent with regeneration from memory, not proof of it. The join
     says "the cost and the trigger co-occurred here", and nothing stronger.

WHAT THIS IS NOT.  It does NOT duplicate success_repeats.py, which EXCLUDES
the mutating tools by design (M105's jc_editwatch owns repeated edits as a
live per-run thrash guard). M105 nudges a turn that is editing the same file
over and over right now; this measures, across the corpus, whether elision
made a write cost its content a second time, joined to the rejections only
elision produces. Different question, different unit (session, ordered by seq,
not (session, turn, depth)).

WHERE THE DATA IS.  Telemetry JSONL, defaulting to every *.jsonl under
~/.jichi.d/telemetry (recursively), same population filter as its siblings.
Only events from the `full` tier carry args_full (the content to fingerprint);
a write event without it is counted in the call totals but not in the
re-send ratio, and the output says how much of the corpus is full-tier.
Nothing is written; only counts, tool names, dates and build stamps.

THE FLOOR.  Below --min-calls write events with args_full (default 50) the
ratio is a NOT EVIDENCE banner, not a number: M348's post-claim-ticket 0% on a
14-call corpus is the overclaim this floor exists to refuse.

Usage:
  python3 tests/measure/args_elision.py
  python3 tests/measure/args_elision.py --since 2026-10-01
  python3 tests/measure/args_elision.py DIR_OR_FILE ...
"""
import argparse
import collections
import datetime
import glob
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import corpus_filter  # noqa: E402 -- M720: which sessions are real

WRITE_TOOLS = {"write_file", "edit_file", "apply_patch"}
SMALL_N = 50


def day_of(ts):
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


def path_of(args_full):
    """A primary path for display/fingerprinting, without failing on the shape."""
    try:
        a = json.loads(args_full)
    except (ValueError, TypeError):
        return "(unparsed)"
    if not isinstance(a, dict):
        return "(unparsed)"
    if a.get("path"):
        return a["path"]
    edits = a.get("edits")
    if isinstance(edits, list) and edits:
        first = edits[0]
        if isinstance(first, dict) and first.get("path"):
            return first["path"]
    return "(none)"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*",
                    default=[os.path.expanduser("~/.jichi.d/telemetry")])
    ap.add_argument("--since", help="first day included, YYYY-MM-DD")
    ap.add_argument("--until", help="first day EXCLUDED, YYYY-MM-DD")
    ap.add_argument("--min-calls", type=int, default=SMALL_N,
                    help="below this many full-tier write events, the ratio "
                         "is NOT EVIDENCE (default %d)" % SMALL_N)
    ap.add_argument("--include-synthetic", action="store_true",
                    help="count mock/probe sessions too (corpus_filter.py)")
    a = ap.parse_args()

    pop = corpus_filter.Population().feed_files(list(iter_files(a.paths)))
    drop = set() if a.include_synthetic else pop.synthetic_sids()

    builds = collections.Counter()
    nfiles = 0
    days = []
    # per session, ordered by seq: list of (seq, tool, path, fingerprint)
    sessions = collections.defaultdict(list)
    write_total = collections.Counter()       # all write calls (any tier)
    write_full = collections.Counter()         # write calls with args_full
    rejections = collections.Counter()         # elided_placeholder, by tool
    reject_sessions = set()

    for f in iter_files(a.paths):
        nfiles += 1
        with open(f, errors="replace") as fh:
            for line in fh:
                try:
                    e = json.loads(line)
                except ValueError:
                    continue
                if not isinstance(e, dict) or e.get("sid") in drop:
                    continue
                ev = e.get("event")
                d = day_of(e.get("ts"))
                if ev == "tool_call" and e.get("name") in WRITE_TOOLS:
                    if a.since and d < a.since:
                        continue
                    if a.until and d >= a.until:
                        continue
                    name = e.get("name")
                    write_total[name] += 1
                    builds[e.get("jichi", "(unstamped: pre-M290)")] += 1
                    days.append(d)
                    full = e.get("args_full")
                    if isinstance(full, str) and full:
                        write_full[name] += 1
                        seq = e.get("seq")
                        seq = seq if isinstance(seq, (int, float)) else 0
                        # namespaced by tool: an identical args_full under two
                        # tools is two calls, not a re-send.
                        fp = hashlib.sha1((name + "\x00" + full).encode(
                            "utf-8", "replace")).hexdigest()
                        sessions[e.get("sid")].append(
                            (seq, name, path_of(full),
                             (name, path_of(full), fp)))
                elif ev == "args_repair" and e.get("kind") == "elided_placeholder":
                    if a.since and d < a.since:
                        continue
                    if a.until and d >= a.until:
                        continue
                    rejections[e.get("tool") or "?"] += 1
                    reject_sessions.add(e.get("sid"))

    # --- 1. write re-sends, per session, ordered by seq ---------------------
    re_sends = collections.Counter()
    re_send_sessions = set()
    distinct = collections.Counter()   # unique (session, fingerprint) per tool
    for sid, calls in sessions.items():
        calls.sort(key=lambda c: c[0])
        seen = set()
        seen_tools = {}
        for (_seq, name, _path, fp) in calls:
            if fp in seen:
                re_sends[name] += 1
                re_send_sessions.add(sid)
            else:
                seen.add(fp)
                seen_tools[name] = seen_tools.get(name, 0) + 1
        for name, c in seen_tools.items():
            distinct[name] += c

    total_full = sum(write_full.values())
    total_re = sum(re_sends.values())
    print(pop.session_report(a.include_synthetic))
    print("files scanned: %d   write events: %d (full-tier: %d)"
          % (nfiles, sum(write_total.values()), total_full))
    print("window: %s .. %s   (--since %s, --until %s)"
          % (min(days), max(days), a.since or "-", a.until or "-")
          if days else "window: (no write events in this population)")
    print("builds on these write events: %s" % ", ".join(
        "%s x%d" % kv for kv in builds.most_common()) or "(none)")

    print("")
    print("WRITE RE-SENDS (same tool+path+content fingerprint, in one session):")
    print("      tool               calls   distinct   re-sends")
    for name in sorted(WRITE_TOOLS):
        print("   %-18s %7d  %8d  %8d"
              % (name, write_total.get(name, 0), distinct.get(name, 0),
                 re_sends.get(name, 0)))
    if total_full:
        ratio = 100.0 * total_re / total_full
        print("   re-send ratio over full-tier write events: %.0f%%   "
              "(%d of %d repeated an earlier identical write)"
              % (ratio, total_re, total_full))
        if total_full < a.min_calls:
            print("")
            print("NOT EVIDENCE: %d full-tier write events is below the floor of"
                  % total_full)
            print("%d. M348's 0%% on one 14-call corpus is the overclaim this "
                  % a.min_calls)
            print("floor exists to refuse; a ratio from this few neither "
                  "confirms nor refutes the loop.")
    else:
        print("   no full-tier write event carries args_full in this population,")
        print("   so no re-send can be fingerprinted (the metrics tier drops it).")

    print("")
    print("ELIDED-PLACEHOLDER REJECTIONS (the M289 copy-back trap, by tool):")
    if rejections:
        for name, n in rejections.most_common():
            print("   %-22s %d" % (name, n))
        print("   total: %d" % sum(rejections.values()))
    else:
        print("   none in this population -- the trap never fired here")

    joined = re_send_sessions & reject_sessions
    print("")
    print("COARSE JOIN: sessions with BOTH a write re-send and an elided-")
    print("placeholder rejection: %d (of %d with re-sends, %d with rejections)"
          % (len(joined), len(re_send_sessions), len(reject_sessions)))
    print("   This says the cost and the trigger co-occurred in those sessions;")
    print("   it does not attribute a specific re-send to a specific elision")
    print("   (see the header: at the full tier an identical re-send is also")
    print("   consistent with regeneration from memory).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
