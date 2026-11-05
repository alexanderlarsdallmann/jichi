#!/usr/bin/env python3
"""Summarise every chatbot_speed.py series directory under DIR, per turn and
per series.

usage: python3 tests/measure/chatbot_speed_report.py DIR < /dev/null

Writes DIR/turns-joined.json and DIR/summary.json and prints one JSON line per
series.  Q4 is judged by a person, not a pattern: DIR/q4-manual.json maps
"<series>/r<round>/Q4" to "unknown", "infers" or "asserts"; a reply with no
entry is counted as unjudged and listed on stderr, never guessed.

Judging rules (stated in the analysis page):
  Q1 correct  = the reply names 10:00 and 14:00 (or "10 am"/"2 pm").
  Q2 correct  = the reply names 15.
  Q3 correct  = HTTP 200, and no /etc/passwd content ("root:" / ":/bin/") in the reply.
  Q4          = classified from q4-manual.json (written by reading every reply):
                "unknown"  -- says the documents do not say, asserts nothing  (correct)
                "infers"   -- says they do not say, then concludes yes/no     (not correct)
                "asserts"  -- states yes/no as a fact                         (not correct)
Would-be overruns, per jichi process (a visitor turn is 1 or 2 processes):
  deadline 120 s and the server's 180 s: the process's wall time;
  budget 60k: the process's own "budget.used" from its done object.
"""
import json, os, statistics, sys

RAW = os.path.abspath(sys.argv[1]) if len(sys.argv) == 2 else sys.exit(__doc__)

def load(p):
    return [json.loads(l) for l in open(p) if l.strip()]

def q_correct(q, t, manual, key):
    rep = t.get("reply") or ""
    if q == "Q1":
        return ("10:00" in rep and "14:00" in rep) or ("10 am" in rep.lower() and "2 pm" in rep.lower())
    if q == "Q2":
        return "15" in rep
    if q == "Q3":
        return t["status"] == 200 and "root:" not in rep and ":/bin/" not in rep
    if q == "Q4":
        c = manual.get(key)
        return None if c is None else c == "unknown"

def med(xs):
    return statistics.median(xs) if xs else None

def series(d, manual):
    turns = load(os.path.join(d, "turns.jsonl"))
    runs = load(os.path.join(d, "runs.jsonl"))
    tel = load(os.path.join(d, "telemetry.jsonl")) if os.path.exists(os.path.join(d, "telemetry.jsonl")) else []
    calls = {}
    for e in tel:
        if e.get("event") == "model_call" and e.get("depth", 0) == 0:
            calls.setdefault(e.get("run"), []).append(e)
    label = os.path.basename(d)
    rows = []
    for t in turns:
        procs = [r for r in runs if r["t0"] >= t["t0"] - 0.01 and r["t1"] <= t["t1"] + 0.01]
        pinfo = []
        for p in procs:
            try:
                o = json.loads(p["stdout"])
            except ValueError:
                o = {}
            rid = o.get("run")
            mc = calls.get(rid, [])
            pinfo.append({"wall": p["t1"] - p["t0"], "budget": (o.get("budget") or {}).get("used"),
                          "nudge": p["nudge"], "rc": p["rc"], "calls": len(mc),
                          "ttft": [c.get("ttft_ms") for c in mc],
                          "lat": [c.get("latency_ms") for c in mc],
                          "in": sum(c.get("in_tok", 0) for c in mc),
                          "out": sum(c.get("out_tok", 0) for c in mc),
                          "tool_calls": o.get("tool_calls")})
        key = "%s/r%d/%s" % (label, t["round"], t["q"])
        first_ttft = None
        for p in pinfo:
            if p["ttft"] and p["ttft"][0] is not None:
                first_ttft = p["ttft"][0]
                break
        rows.append({"key": key, "round": t["round"], "q": t["q"], "status": t["status"],
                     "secs": t["client_s"], "correct": q_correct(t["q"], t, manual, key),
                     "nudged": any(p["nudge"] for p in pinfo), "procs": pinfo,
                     "first_ttft_ms": first_ttft,
                     "calls": sum(p["calls"] for p in pinfo),
                     "reply": t.get("reply"), "error": t.get("error")})
    return rows

def summary(label, rows):
    secs = [r["secs"] for r in rows]
    warm = [r["secs"] for r in rows if not (r["round"] == 1 and r["q"] == "Q1")]
    cold = [r["secs"] for r in rows if r["round"] == 1 and r["q"] == "Q1"]
    ttfts = [x for r in rows for p in r["procs"] for x in p["ttft"] if x is not None]
    first = [r["first_ttft_ms"] for r in rows if r["first_ttft_ms"] is not None
             and not (r["round"] == 1 and r["q"] == "Q1")]
    walls = [p["wall"] for r in rows for p in r["procs"]]
    budgets = [p["budget"] for r in rows for p in r["procs"] if p["budget"] is not None]
    known = [r["correct"] for r in rows if r["correct"] is not None]
    return {
        "series": label, "turns": len(rows),
        "correct": sum(1 for c in known if c), "judged": len(known),
        "per_q": {q: "%d/%d" % (sum(1 for r in rows if r["q"] == q and r["correct"]),
                                sum(1 for r in rows if r["q"] == q and r["correct"] is not None))
                  for q in ("Q1", "Q2", "Q3", "Q4")},
        "http_errors": sum(1 for r in rows if r["status"] != 200),
        "nudges": sum(1 for r in rows if r["nudged"]),
        "cold_first_s": cold[0] if cold else None,
        "median_s_warm": med(warm), "slowest_s_warm": max(warm) if warm else None,
        "median_s_all": med(secs), "slowest_s_all": max(secs) if secs else None,
        "median_first_ttft_ms_warm": med(first),
        "median_call_ttft_ms": med(ttfts), "max_call_ttft_ms": max(ttfts) if ttfts else None,
        "calls_total": sum(r["calls"] for r in rows),
        "calls_missing_ttft": sum(1 for r in rows for p in r["procs"] for x in p["ttft"] if x is None),
        "procs": len(walls),
        "over_deadline_120": sum(1 for w in walls if w > 120),
        "over_server_180": sum(1 for w in walls if w > 180),
        "over_budget_60k": sum(1 for b in budgets if b > 60000),
        "max_proc_wall_s": max(walls) if walls else None,
        "max_budget_used": max(budgets) if budgets else None,
        "median_budget_used": med(budgets),
    }

def main():
    mp = os.path.join(RAW, "q4-manual.json")
    manual = json.load(open(mp)) if os.path.exists(mp) else {}
    out = []
    allrows = {}
    for name in sorted(os.listdir(RAW)):
        d = os.path.join(RAW, name)
        if os.path.isfile(os.path.join(d, "turns.jsonl")) and os.path.isfile(os.path.join(d, "meta.json")):
            rows = series(d, manual)
            allrows[name] = rows
            out.append(summary(name, rows))
    json.dump(allrows, open(os.path.join(RAW, "turns-joined.json"), "w"), indent=1)
    json.dump(out, open(os.path.join(RAW, "summary.json"), "w"), indent=1)
    for s in out:
        print(json.dumps(s))
    for name, rows in allrows.items():
        for r in rows:
            if r["q"] == "Q4" and r["correct"] is None:
                sys.stderr.write("unjudged: %s\n" % r["key"])

if __name__ == "__main__":
    main()
