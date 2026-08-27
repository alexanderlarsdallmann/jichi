#!/usr/bin/env python3
"""Turn a bench run's telemetry into the measurement-plan table.

Reads results/<label>/ (written by run_bench.py) and reports the rows of the
§9 measurement plan from docs/DEFERRED_LOCAL_GPU.md:

  * task pass-rate and points (the outcome the tools are a means to)
  * per-tool ok-rate, and the median across the core tools
  * edit-failure taxonomy: how many `edit_file`/`apply_patch` errors were a
    stale/unmatched `old_string` -- the failure the M38 fuzzy matcher targets
  * nudge fired/recovered (M147) and args_repair ok/total (M148)
  * real prompt tokens: first-call prefix and per-run peak, from the server's
    own counts rather than the byte/4 estimate

Why this reads raw JSONL rather than shelling out to `jichi telemetry`:
the built-in summarizer covers turn/model_call/tool_call/route/compact but has
no reader for the `nudge` and `args_repair` events, so two rows of the plan are
invisible to it today (see the recommendations in docs/BENCH_LOCAL_GPU.md).

Usage: python3 report.py [label ...]        (default: every label present)
Python stdlib only; entirely offline.
"""
import json
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RESULTS = os.path.join(HERE, "results")

CORE_TOOLS = ("read_file", "write_file", "edit_file", "apply_patch",
              "list_files", "search_code", "run_terminal_command")

# Substrings that mark a tool error as "the old_string did not match" rather
# than any other failure. Kept as data so the taxonomy is auditable.
STALE_MARKERS = ("not found in", "no match", "did not match", "ambiguous",
                 "occurrences", "old_string")


def events(path):
    out = []
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                out.append(json.loads(line))
            except ValueError:
                continue
    return out


def analyse(label):
    d = os.path.join(RESULTS, label)
    sp = os.path.join(d, "summary.json")
    if not os.path.exists(sp):
        return None
    summary = json.load(open(sp))

    tools = {}          # name -> [ok, total]
    stale = 0
    edit_fail = 0
    nudge = {"fired": 0, "recovered": 0}
    repair = {"ok": 0, "total": 0}
    prefixes = []
    peaks = []
    compacts = 0

    for r in summary["results"]:
        evs = events(r.get("telemetry", ""))
        ins = []
        for e in evs:
            t = e.get("type") or e.get("event") or e.get("ev")
            if t == "tool_call":
                nm = e.get("tool") or e.get("name") or "?"
                rec = tools.setdefault(nm, [0, 0])
                rec[1] += 1
                okv = e.get("ok")
                is_ok = bool(okv) if okv is not None else not e.get("error")
                if is_ok:
                    rec[0] += 1
                elif nm in ("edit_file", "apply_patch"):
                    edit_fail += 1
                    blob = json.dumps(e).lower()
                    if any(m in blob for m in STALE_MARKERS):
                        stale += 1
            elif t == "nudge":
                ph = e.get("phase")
                if ph in nudge:
                    nudge[ph] += 1
            elif t == "args_repair":
                repair["total"] += 1
                if e.get("ok"):
                    repair["ok"] += 1
            elif t == "model_call":
                v = e.get("in") or e.get("usage_in") or e.get("in_tok")
                if v:
                    ins.append(float(v))
            elif t == "compact":
                compacts += 1
        if ins:
            prefixes.append(ins[0])
            peaks.append(max(ins))

    core_rates = [100.0 * v[0] / v[1] for k, v in tools.items()
                  if k in CORE_TOOLS and v[1] > 0]
    return {
        "summary": summary, "tools": tools, "stale": stale,
        "edit_fail": edit_fail, "nudge": nudge, "repair": repair,
        "core_median": statistics.median(core_rates) if core_rates else None,
        "prefix": statistics.median(prefixes) if prefixes else None,
        "peak": max(peaks) if peaks else None, "compacts": compacts,
    }


def render(label, a):
    s = a["summary"]
    print("=" * 66)
    print("bench: %s   model=%s   profile=%s"
          % (label, s["model"], s["profile"]))
    print("=" * 66)
    print("Tasks:   %d/%d passed   (%d/%d points)"
          % (s["passed"], s["tasks"], s["points"], s["points_total"]))
    for r in s["results"]:
        print("  %-22s %-4s rc=%-4s %6.1fs"
              % (r["task"], "PASS" if r.get("passed") else "fail",
                 r.get("rc"), r.get("wall_s", 0)))

    print("\nTools:")
    if not a["tools"]:
        print("  (no tool_call events -- telemetry off, or no tool was called)")
    for nm in sorted(a["tools"]):
        ok, tot = a["tools"][nm]
        star = " *" if nm in CORE_TOOLS else ""
        print("  %-24s ok=%d/%d (%d%%)%s"
              % (nm, ok, tot, (100 * ok // tot) if tot else 0, star))
    if a["core_median"] is not None:
        print("  core-tool ok-rate median: %.0f%%" % a["core_median"])

    print("\nMeasurement plan (docs/DEFERRED_LOCAL_GPU.md §4):")
    md = a["core_median"]
    worst = min(((100 * v[0] // v[1]) for k, v in a["tools"].items()
                 if k in CORE_TOOLS and v[1] > 0), default=None)
    row = lambda k, v, t: print("  %-34s %-22s %s" % (k, v, t))
    row("tool ok-rate (core, median)",
        "%.0f%%" % md if md is not None else "n/a", "target >=85%, none <60%")
    row("  worst core tool",
        "%d%%" % worst if worst is not None else "n/a", "")
    row("stale-old_string share of edit fails",
        "%d/%d" % (a["stale"], a["edit_fail"]) if a["edit_fail"]
        else "0 edit failures", "target: halved vs baseline")
    row("nudge fired / recovered",
        "%d / %d" % (a["nudge"]["fired"], a["nudge"]["recovered"]),
        "target: recovery >=60% of fires")
    row("args_repair ok / total",
        "%d / %d" % (a["repair"]["ok"], a["repair"]["total"]),
        "target: majority of parse failures")
    row("prefix tokens (real, first call)",
        "%.0f" % a["prefix"] if a["prefix"] else "n/a",
        "target: fits 8k with >=5k room")
    row("peak input tokens (real)",
        "%.0f" % a["peak"] if a["peak"] else "n/a", "")
    row("mid-turn compactions", str(a["compacts"]), "")
    print()


def main():
    labels = sys.argv[1:]
    if not labels:
        if not os.path.isdir(RESULTS):
            sys.exit("no results/ yet -- run run_bench.py first")
        labels = sorted(d for d in os.listdir(RESULTS)
                        if os.path.exists(os.path.join(RESULTS, d,
                                                       "summary.json")))
    for label in labels:
        a = analyse(label)
        if a is None:
            print("skip %s (no summary.json)" % label)
            continue
        render(label, a)
    return 0


if __name__ == "__main__":
    sys.exit(main())
