#!/usr/bin/env python3
"""Summarize a stress run (M185): latency percentiles, error/retry rates,
token throughput. Reads the artifacts stress.py wrote:

  <out>/requests.csv        driver ground truth (wall time per request)
  <out>/telemetry/*.jsonl   jichi's own model_call events
                            (latency_ms, ok, status, in_tok, out_tok, attempt)

Usage: python3 examples/stress/report.py <out-dir> [<out-dir> ...]
Pass several out-dirs (one per concurrency step) to print the knee table.
"""
import csv
import glob
import json
import os
import sys


def pct(sorted_vals, p):
    if not sorted_vals:
        return 0.0
    i = min(len(sorted_vals) - 1, int(len(sorted_vals) * p / 100.0))
    return sorted_vals[i]


def load(outdir):
    lat, in_tok, out_tok = [], 0.0, 0.0
    calls = ok = retries = timeouts = errors = 0
    for path in glob.glob(os.path.join(outdir, "telemetry", "*.jsonl")):
        with open(path) as f:
            for line in f:
                try:
                    o = json.loads(line)
                except ValueError:
                    continue
                ev = o.get("event")
                if ev == "model_call":
                    calls += 1
                    if o.get("attempt", 1) > 1:
                        retries += 1
                    if o.get("ok"):
                        ok += 1
                        lat.append(float(o.get("latency_ms", 0.0)))
                        in_tok += float(o.get("in_tok", 0.0))
                        out_tok += float(o.get("out_tok", 0.0))
                    elif o.get("result") == "timeout":
                        timeouts += 1
                    else:
                        errors += 1
                elif ev == "model_retry":
                    retries += 1
    wall = 0.0
    nreq = req_ok = 0
    csvp = os.path.join(outdir, "requests.csv")
    if os.path.exists(csvp):
        with open(csvp) as f:
            t0, t1 = None, None
            for row in csv.DictReader(f):
                nreq += 1
                if row["exit"] == "0":
                    req_ok += 1
                s = float(row["start"])
                e = s + float(row["duration_s"])
                t0 = s if t0 is None else min(t0, s)
                t1 = e if t1 is None else max(t1, e)
            if t0 is not None:
                wall = t1 - t0
    lat.sort()
    return {
        "dir": outdir, "requests": nreq, "req_ok": req_ok, "calls": calls,
        "ok": ok, "errors": errors, "timeouts": timeouts, "retries": retries,
        "p50": pct(lat, 50), "p90": pct(lat, 90), "p99": pct(lat, 99),
        "wall": wall,
        "req_s": (nreq / wall) if wall > 0 else 0.0,
        "out_tok_s": (out_tok / wall) if wall > 0 else 0.0,
    }


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    print("| run | req (ok) | calls ok/err/timeout | retries | "
          "p50 ms | p90 ms | p99 ms | req/s | out tok/s |")
    print("|---|---|---|---|---|---|---|---|---|")
    for d in sys.argv[1:]:
        r = load(d)
        print("| %s | %d (%d) | %d/%d/%d | %d | %.0f | %.0f | %.0f "
              "| %.2f | %.1f |" %
              (os.path.basename(r["dir"].rstrip("/")), r["requests"],
               r["req_ok"], r["ok"], r["errors"], r["timeouts"],
               r["retries"], r["p50"], r["p90"], r["p99"], r["req_s"],
               r["out_tok_s"]))
    if len(sys.argv) > 2:
        print("\nKnee reading: step up the concurrency (one out-dir per "
              "step); the knee is where p90 bends up while req/s stops "
              "climbing. Tune the server (parallel slots, batch, context) "
              "and re-run the same steps.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
