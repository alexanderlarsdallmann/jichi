#!/usr/bin/env python3
"""curriculum_stage1_report.py -- summarise curriculum_stage1.sh's series: one row per attempt, one per series.

Per attempt (from result.json, attempt's own --output json, M898; and the attempt's telemetry):
  verdict, wall_s, model_calls, tool_calls, tokens, bound_reached;
  max_prompt (largest in_tok of a model call), max_reply (largest out_tok);
  model_s (sum of model-call latency), tool_s (sum of tool-call duration);
  tool mix: read / edit / shell / test / other (by tool name);
  failed tool calls (tool_call events with ok false).
A missing or empty result.json is reported as NORESULT, never as a verdict.

usage: curriculum_stage1_report.py OUT [SERIES ...]
  OUT is curriculum_stage1.sh's output directory; it must hold sample.txt (lines "spec | points | ...").
  SERIES default to every directory under OUT holding attempt results. Writes OUT/report.json and prints
  a table. An attempt directory whose name contains ".STOPPED" or ".INTERRUPTED" is reported as such and
  never counted as a verdict (docs/analysis/2026-10-04-curriculum-stage1.md §8).
"""
import json, os, statistics, sys

if len(sys.argv) < 2:
    sys.exit("usage: curriculum_stage1_report.py OUT [SERIES ...]")
M = os.path.abspath(sys.argv[1])
POINTS = {}
for line in open(os.path.join(M, "sample.txt")):
    p = [x.strip() for x in line.split("|")]
    if len(p) >= 3:
        POINTS[p[0]] = int(p[1])

READ = {"read_file", "list_files", "search_code", "codebase_search", "read_many_files", "glob"}
EDIT = {"edit_file", "write_file", "apply_patch", "multi_edit"}
SHELL = {"run_terminal_command"}
TEST = {"run_tests"}

def attempt(d):
    row = {"task": os.path.basename(d), "points": POINTS.get(os.path.basename(d).split(".")[0])}
    try:
        r = json.load(open(os.path.join(d, "result.json")))
    except Exception:
        r = None
    base = os.path.basename(d)
    if ".STOPPED" in base or ".INTERRUPTED" in base:
        row["verdict"] = "STOPPED" if ".STOPPED" in base else "INTERRUPTED"
    elif not r:
        row["verdict"] = "NORESULT"
    else:
        for k in ("verdict", "wall_s", "model_calls", "tool_calls", "tokens", "bound_reached"):
            row[k] = r.get(k)
    mp = mr = 0; ms = ts = 0.0; mix = {"read": 0, "edit": 0, "shell": 0, "test": 0, "other": 0}
    failed = 0; calls = 0
    tp = os.path.join(d, "telemetry.jsonl")
    if os.path.exists(tp):
        for l in open(tp):
            try:
                e = json.loads(l)
            except Exception:
                continue
            if e.get("event") == "model_call":
                calls += 1
                mp = max(mp, e.get("in_tok") or 0); mr = max(mr, e.get("out_tok") or 0)
                ms += (e.get("latency_ms") or 0) / 1000.0
            elif e.get("event") == "tool_call":
                n = e.get("name", "")
                k = "read" if n in READ else "edit" if n in EDIT else "shell" if n in SHELL \
                    else "test" if n in TEST else "other"
                mix[k] += 1
                ts += (e.get("duration_ms") or 0) / 1000.0
                if e.get("ok") is False:
                    failed += 1
    row.update({"max_prompt": mp, "max_reply": mr, "model_s": round(ms, 1), "tool_s": round(ts, 1),
                "mix": mix, "tool_failed": failed, "telemetry_calls": calls})
    return row

def series(name):
    base = os.path.join(M, name)
    rows = [attempt(os.path.join(base, t)) for t in sorted(os.listdir(base))
            if os.path.isdir(os.path.join(base, t))]
    done = [r for r in rows if r["verdict"] not in ("NORESULT", "STOPPED", "INTERRUPTED")]
    passed = [r for r in done if r["verdict"] == "PASS"]
    pts = sum(r["points"] or 0 for r in passed)
    tok = sum(r.get("tokens") or 0 for r in done)
    mins = sum((r.get("wall_s") or 0) for r in done) / 60.0
    per = [((r.get("tokens") or 0) / r["points"], (r.get("wall_s") or 0) / 60.0 / r["points"])
           for r in passed if r["points"]]
    return {"series": name, "attempts": len(rows), "with_result": len(done), "pass": len(passed),
            "points_passed": pts, "tokens": tok, "minutes": round(mins, 1),
            "tokens_per_point_median": round(statistics.median([a for a, _ in per])) if per else None,
            "tokens_per_point_range": [round(min(a for a, _ in per)), round(max(a for a, _ in per))] if per else None,
            "min_per_point_median": round(statistics.median([b for _, b in per]), 2) if per else None,
            "min_per_point_range": [round(min(b for _, b in per), 2), round(max(b for _, b in per), 2)] if per else None,
            "max_tool_calls": max([r.get("tool_calls") or 0 for r in done], default=0),
            "max_prompt": max([r["max_prompt"] for r in rows], default=0),
            "max_reply": max([r["max_reply"] for r in rows], default=0),
            "rows": rows}

def main():
    names = sys.argv[2:] or sorted(n for n in os.listdir(M) if os.path.isdir(os.path.join(M, n))
                                   and any(os.path.exists(os.path.join(M, n, t, "result.json"))
                                           for t in os.listdir(os.path.join(M, n))))
    out = [series(n) for n in names]
    json.dump(out, open(os.path.join(M, "report.json"), "w"), indent=1)
    for s in out:
        print("== %s: %d/%d PASS, %d points, %.1f min, %s tokens, tok/pt %s %s, min/pt %s %s, max tool calls %d, max prompt %d, max reply %d"
              % (s["series"], s["pass"], s["with_result"], s["points_passed"], s["minutes"], s["tokens"],
                 s["tokens_per_point_median"], s["tokens_per_point_range"], s["min_per_point_median"],
                 s["min_per_point_range"], s["max_tool_calls"], s["max_prompt"], s["max_reply"]))
        for r in s["rows"]:
            print("  %-46s %-8s pts=%s wall=%s calls=%s tools=%s tok=%s bound=%s prompt=%d reply=%d model_s=%s tool_s=%s mix=%s failed=%d"
                  % (r["task"], r["verdict"], r["points"], r.get("wall_s"), r.get("model_calls"), r.get("tool_calls"),
                     r.get("tokens"), r.get("bound_reached"), r["max_prompt"], r["max_reply"], r["model_s"],
                     r["tool_s"], r["mix"], r["tool_failed"]))

main()
