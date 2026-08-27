#!/usr/bin/env python3
"""Stress-test an LLM server with a fleet of jichi instances (M185).

Python stdlib only, same as examples/web-bridge. Spawns N jichi processes
(the concurrency step) in throwaway workspaces, each issuing M requests
against ONE target server, with staggered ramp-up and SIGINT-clean
teardown. Two artifacts land in --out:

  requests.csv        the driver's ground truth: one row per request
                      (instance, seq, start, duration_s, exit)
  telemetry/*.jsonl   each instance's standard jichi telemetry (model_call
                      events with latency_ms/ok/in_tok/out_tok/status)

`report.py <out-dir>` turns them into percentiles and rates. Scale by
REQUESTS, not by processes: 32-512 instances saturate any single server;
100k concurrent processes is a cluster exercise, not a laptop one (see
docs/STRESS_TESTING.md for the resource math).

Usage:
  python3 examples/stress/stress.py \
      --jichi ./jichi --server http://127.0.0.1:1234/v1 --model my-model \
      --instances 8 --requests 20 --out /tmp/stress-8

  # cheapest request shape (one non-streaming completion, no tools):
  ... --mode complete --prompt "say ok"
  # realistic agent turns (streaming, tools advertised):
  ... --mode turn --prompt "reply with the word ok"
"""
import argparse
import csv
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time


def one_instance(args, idx, outdir, rows, lock, stop):
    ws = tempfile.mkdtemp(prefix="jc-stress-%d-" % idx)
    home = tempfile.mkdtemp(prefix="jc-stress-home-%d-" % idx)
    telem = os.path.join(outdir, "telemetry", "i%03d.jsonl" % idx)
    cfg = {
        "models": [{
            "name": "target", "provider": "openai", "model": args.model,
            "apiBase": args.server, "roles": ["chat", "autocomplete"],
        }],
        "repoMap": False, "references": False, "snapshots": False,
        "maxRetries": args.retries,
    }
    if args.api_key_env:
        cfg["models"][0]["apiKeyEnv"] = args.api_key_env
    else:
        cfg["models"][0]["apiKey"] = "stress"
    cfgp = os.path.join(home, "config.json")
    with open(cfgp, "w") as f:
        json.dump(cfg, f)
    env = dict(os.environ, LANG="C", LC_ALL="C", HOME=home)

    for seq in range(args.requests):
        if stop.is_set():
            break
        if args.mode == "complete":
            argv = [args.jichi, "--config", cfgp, "--log", telem,
                    "--log-level", "metrics", "complete", args.prompt]
        else:
            argv = [args.jichi, "--config", cfgp, "--log", telem,
                    "--log-level", "metrics", "--no-session", "--no-stdin",
                    "-q", "-p", args.prompt]
        t0 = time.time()
        try:
            p = subprocess.run(argv, capture_output=True, env=env, cwd=ws,
                               timeout=args.timeout)
            rc = p.returncode
        except subprocess.TimeoutExpired:
            rc = 124
        with lock:
            rows.append((idx, seq, t0, time.time() - t0, rc))
    shutil.rmtree(ws, ignore_errors=True)
    shutil.rmtree(home, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jichi", default=os.environ.get("JC_STRESS_BIN",
                                                      "./jichi"))
    ap.add_argument("--server", required=True,
                    help="target apiBase, e.g. http://127.0.0.1:1234/v1")
    ap.add_argument("--model", required=True)
    ap.add_argument("--instances", type=int, default=8)
    ap.add_argument("--requests", type=int, default=10,
                    help="requests per instance")
    ap.add_argument("--mode", choices=["turn", "complete"], default="turn")
    ap.add_argument("--prompt", default="Reply with the single word: ok")
    ap.add_argument("--ramp", type=float, default=0.25,
                    help="seconds between instance starts")
    ap.add_argument("--timeout", type=float, default=120.0,
                    help="per-request kill timeout")
    ap.add_argument("--retries", type=int, default=0,
                    help="jichi maxRetries (0 = clean error counting)")
    ap.add_argument("--api-key-env", default="")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    if not os.access(args.jichi, os.X_OK):
        print("stress: --jichi %r is not executable" % args.jichi,
              file=sys.stderr)
        return 2
    os.makedirs(os.path.join(args.out, "telemetry"), exist_ok=True)

    rows, lock, stop = [], threading.Lock(), threading.Event()
    signal.signal(signal.SIGINT, lambda *a: stop.set())

    t_start = time.time()
    threads = []
    for i in range(args.instances):
        th = threading.Thread(target=one_instance,
                              args=(args, i, args.out, rows, lock, stop),
                              daemon=True)
        th.start()
        threads.append(th)
        time.sleep(args.ramp)
    for th in threads:
        th.join()
    wall = time.time() - t_start

    rows.sort()
    with open(os.path.join(args.out, "requests.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["instance", "seq", "start", "duration_s", "exit"])
        for r in rows:
            w.writerow(["%d" % r[0], "%d" % r[1], "%.3f" % r[2],
                        "%.3f" % r[3], "%d" % r[4]])

    done = len(rows)
    ok = sum(1 for r in rows if r[4] == 0)
    print("stress: %d instances x %d requests -> %d completed, %d ok, "
          "%.1fs wall (%.2f req/s)" %
          (args.instances, args.requests, done, ok, wall,
           done / wall if wall > 0 else 0.0))
    print("stress: artifacts in %s; summarize with: "
          "python3 examples/stress/report.py %s" % (args.out, args.out))
    return 0 if (done and ok == done) else 1


if __name__ == "__main__":
    sys.exit(main())
