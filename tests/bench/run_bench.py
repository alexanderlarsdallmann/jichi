#!/usr/bin/env python3
"""The fixed dogfood prompt suite -- the bench corpus runner (docs/BENCH_LOCAL_GPU.md).

Runs every spec under corpus/ against a live model, one bounded headless `--auto`
turn per task, in a throwaway workspace with a throwaway HOME, then grades the
result with the spec's own `verify` command.

Design notes:
  * Fixed corpus, not the live repo. The numbers are only comparable across
    sessions and machines if the input never moves, so each task ships its own
    small fixture tree (corpus/<id>/files/) which is copied fresh per run.
  * Throwaway HOME per task. Telemetry, the M77 calibration file, snapshots and
    run journals all live under $HOME/.jichi.d; isolating HOME keeps the
    bench from reading or polluting the operator's real state, and keeps task N
    from inheriting task N-1's calibration ratio.
  * Bounded. A small model can loop; every task runs under the autonomy
    envelope's token/wall-clock/tool-call caps so a wedged task costs one
    timeout, not the session.
  * Per-task telemetry file. Attribution is then trivial -- no cross-task
    filtering, no shared-log parsing.

Writes results/<label>/ containing one <task>.json per task (the graded outcome
plus its raw telemetry path) and summary.json. Read it with report.py.

Python stdlib only. Requires a reachable model; skips nothing -- if the endpoint
is down it fails loudly.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BIN = os.environ.get("JC_BENCH_BIN", os.path.join(ROOT, "jichi"))
CORPUS = os.path.join(HERE, "corpus")


def parse_spec(path):
    """Pull `verify`, `points`, `title` and the body out of a spec.md.

    Deliberately a small local reader rather than shelling out to the binary:
    the bench must be able to report on a task even when the binary under test
    is the thing that is broken.
    """
    text = open(path, encoding="utf-8").read()
    body = text
    fm = ""
    if text.startswith("---"):
        end = text.find("\n---", 3)
        if end != -1:
            fm = text[3:end]
            body = text[end + 4:]

    def key(name, default=None):
        m = re.search(r'^%s:\s*(.*?)\s*$' % name, fm, re.M)
        if not m:
            return default
        v = m.group(1)
        if len(v) >= 2 and v[0] == '"' and v[-1] == '"':
            v = v[1:-1].replace('\\"', '"')
        return v

    return {
        "title": key("title", os.path.basename(os.path.dirname(path))),
        "verify": key("verify"),
        "points": int(key("points", "1")),
        "task": body.strip(),
    }


def read_events(path):
    """Telemetry JSONL -> list of dicts, tolerating a truncated final line."""
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
                continue          # a run killed mid-write; ignore the tail
    return out


def run_task(task_dir, args, outdir):
    spec = parse_spec(os.path.join(task_dir, "spec.md"))
    tid = os.path.basename(task_dir)
    ws = tempfile.mkdtemp(prefix="jichi_bench_ws_")
    home = tempfile.mkdtemp(prefix="jichi_bench_home_")
    telem = os.path.join(outdir, tid + ".telemetry.jsonl")
    journal = os.path.join(outdir, tid + ".journal.jsonl")
    rec = {"task": tid, "title": spec["title"], "points": spec["points"],
           "profile": args.profile, "model": args.model}
    try:
        src = os.path.join(task_dir, "files")
        for name in os.listdir(src):
            s, d = os.path.join(src, name), os.path.join(ws, name)
            if os.path.isdir(s):
                shutil.copytree(s, d)
            else:
                shutil.copy2(s, d)

        argv = [BIN, "--config", args.config, "--model", args.model,
                "--no-route", "--auto", "--no-stdin",
                "--tool-profile", args.profile,
                "--log", telem, "--log-level", args.log_level,
                "--journal", journal,
                "--budget-tokens", str(args.budget_tokens),
                "--deadline", args.deadline,
                "--max-tool-calls", str(args.max_tool_calls),
                "-p", spec["task"]]
        t0 = time.time()
        try:
            p = subprocess.run(argv, capture_output=True, text=True,
                               timeout=args.timeout, cwd=ws,
                               env=dict(os.environ, HOME=home,
                                        LANG="C", LC_ALL="C"))
            rc, out, err = p.returncode, p.stdout, p.stderr
        except subprocess.TimeoutExpired as e:
            rc = 124
            out = (e.stdout or b"").decode("utf-8", "replace") \
                if isinstance(e.stdout, bytes) else (e.stdout or "")
            err = (e.stderr or b"").decode("utf-8", "replace") \
                if isinstance(e.stderr, bytes) else (e.stderr or "")
        rec["wall_s"] = round(time.time() - t0, 1)
        rec["rc"] = rc
        rec["answer_tail"] = out[-600:]
        rec["stderr_tail"] = err[-1200:]

        # Grade with the spec's own verify, in the workspace the agent edited.
        if spec["verify"]:
            g = subprocess.run(["sh", "-c", spec["verify"]], cwd=ws,
                               capture_output=True, text=True, timeout=120)
            rec["verify_rc"] = g.returncode
            rec["passed"] = (g.returncode == 0)
            rec["verify_stderr"] = g.stderr.strip()[-300:]
        else:
            rec["passed"] = None

        rec["telemetry"] = telem
        rec["events"] = len(read_events(telem))
    finally:
        shutil.rmtree(ws, ignore_errors=True)
        shutil.rmtree(home, ignore_errors=True)
    return rec


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default=os.environ.get(
        "JC_BENCH_MODEL", "google/gemma-4-e4b"))
    ap.add_argument("--config", default=os.path.join(HERE, "config.bench.json"))
    ap.add_argument("--profile", default="full", choices=["auto", "core", "full"],
                    help="tool profile to advertise (the item-1 A/B knob)")
    ap.add_argument("--label", default=None,
                    help="results subdirectory name (default: <profile>)")
    ap.add_argument("--only", default=None,
                    help="substring filter on the task id")
    ap.add_argument("--log-level", default="metrics",
                    choices=["metrics", "full"],
                    help="'full' also records tool I/O content -- required for "
                         "the stale-old_string taxonomy, since metrics-tier "
                         "tool_call events carry ok but not the error text")
    ap.add_argument("--budget-tokens", type=int, default=200000)
    ap.add_argument("--deadline", default="10m")
    ap.add_argument("--max-tool-calls", type=int, default=25)
    ap.add_argument("--timeout", type=int, default=900,
                    help="hard per-task wall clock (s), backstop to --deadline")
    args = ap.parse_args()
    args.config = os.path.abspath(args.config)

    if not os.path.exists(BIN):
        sys.exit("bench: %s not built (run make)" % BIN)

    label = args.label or args.profile
    outdir = os.path.join(HERE, "results", label)
    if os.path.isdir(outdir):
        shutil.rmtree(outdir)
    os.makedirs(outdir)

    tasks = sorted(d for d in os.listdir(CORPUS)
                   if os.path.isdir(os.path.join(CORPUS, d)))
    if args.only:
        tasks = [t for t in tasks if args.only in t]
    if not tasks:
        sys.exit("bench: no tasks matched")

    print("bench: %d tasks, model=%s profile=%s -> results/%s"
          % (len(tasks), args.model, args.profile, label))
    results = []
    for t in tasks:
        sys.stdout.write("  %-22s " % t)
        sys.stdout.flush()
        rec = run_task(os.path.join(CORPUS, t), args, outdir)
        results.append(rec)
        mark = "PASS" if rec.get("passed") else "fail"
        print("%s  rc=%-3s %5.1fs  events=%d"
              % (mark, rec.get("rc"), rec.get("wall_s", 0), rec.get("events", 0)))
        with open(os.path.join(outdir, rec["task"] + ".json"), "w") as f:
            json.dump(rec, f, indent=2)

    npass = sum(1 for r in results if r.get("passed"))
    summary = {"label": label, "model": args.model, "profile": args.profile,
               "tasks": len(results), "passed": npass,
               "points": sum(r["points"] for r in results if r.get("passed")),
               "points_total": sum(r["points"] for r in results),
               "results": results}
    with open(os.path.join(outdir, "summary.json"), "w") as f:
        json.dump(summary, f, indent=2)
    print("bench: %d/%d tasks passed (%d/%d points) -> results/%s/summary.json"
          % (npass, len(results), summary["points"], summary["points_total"],
             label))


if __name__ == "__main__":
    main()
