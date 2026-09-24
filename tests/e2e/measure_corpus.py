#!/usr/bin/env python3
"""M720: the measurement scripts count REAL runs, and say what they left out.

THE DEFECT.  On 2026-09-23 the workstation's ~/.jichi.d -- the corpus four register
decisions and plan D1 wait on -- turned out to be 40 % agent probes: sessions that
had run jichi against `mockmodel` with the real home directory (1,230 of 3,096
post-M432 tool calls; 28 of the 32 journals carrying a stop_reason).  Every script
in tests/measure/ counted them as real.  A mock's cap test is exactly the shape
DEFERRED item 7 counts, so the evidence for a stable-tier exit code was on course
to be manufactured by test fixtures.

THE FIXTURE is a corpus with the same four kinds the real one holds: a REAL
session (answered in seconds), one naming its model "mock" (the smoke tier's
write_config), one answering in under a millisecond under a realistic model name
(a mock by any other name), and one whose only model call was refused.  Each of
the three answered sessions carries the same shapes -- a repeated successful call,
a pressed compaction pass, re-reads, a capped one-shot journal -- so every
script's count moves if, and only if, the synthetic ones are counted.

THE CHECKS, per script: the default population is the real one; the output names
what was dropped and why; --include-synthetic restores the old count (so the
filter, and nothing else, is what moved it).  Journals are also classified by
their own start.model (M720+) when their run has no telemetry, and a journal with
neither is kept and counted as unverified rather than guessed at.

Offline; no binary needed -- the scripts read files.
"""
import json
import os
import re
import subprocess
import sys
import tempfile

import _e2e

HERE = os.path.dirname(os.path.abspath(__file__))
MEASURE = os.path.join(HERE, "..", "measure")
T0 = 1790000000

results = []


def check(cond, msg, detail=""):
    results.append((bool(cond), msg, detail))
    print(("ok: " if cond else "not ok: ") + msg + ("" if cond else " -- " + detail))


def run(script, *args):
    p = subprocess.run([sys.executable, os.path.join(MEASURE, script)] + list(args),
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       universal_newlines=True, timeout=60)
    return p.stdout


def num(pattern, text):
    m = re.search(pattern, text)
    return int(m.group(1)) if m else None


def session(sid, run_id, model_id, latency, answered, shape):
    """Events for one session; `shape` is (repeated command, read path)."""
    ev = []
    seq = [0]

    def add(**kw):
        seq[0] += 1
        base = {"v": 1, "ts": T0 + seq[0], "sid": sid, "run": run_id, "seq": seq[0],
                "depth": 0, "turn": 1}
        base.update(kw)
        ev.append(base)

    add(event="model_call", model="m1", model_id=model_id, attempt=0,
        status=200 if answered else 0, latency_ms=latency, ok=answered,
        error=not answered)
    if not answered:
        return ev
    cmd, path = shape
    for _ in range(6):
        add(event="tool_call", name="run_terminal_command", ok=True, error=False,
            output_bytes=5, args=cmd, args_full=json.dumps({"command": cmd}),
            output="a b c")
    for _ in range(3):
        add(event="tool_call", name="read_file", ok=True, error=False,
            output_bytes=40, args=path, offset=1, limit=100)
    add(event="compact", phase="midturn", before=90000, after=85000,
        target=60000, pressed=True, limit=65536)
    return ev


def journal(run_id, model):
    start = {"ts": T0, "run": run_id, "event": "start",
             "one_shot": True, "max_tool_calls": 6, "edit_scope": 1,
             "edit_scope_globs": ["src/**"]}
    if model is not None:
        start["model"] = model
    return [start,
            {"ts": T0 + 1, "run": run_id, "event": "out_of_scope",
             "paths": ["notes.txt"], "tracked": []},
            {"ts": T0 + 2, "run": run_id, "event": "end", "outcome": "ok",
             "stop_reason": "max_iters", "answer_bytes": 0, "tool_calls": 6}]


def write(path, events):
    with open(path, "w") as f:
        for e in events:
            f.write(json.dumps(e) + "\n")


tmp = tempfile.mkdtemp(prefix="jichi_measure_")
tel = os.path.join(tmp, "telemetry")
runs = os.path.join(tmp, "runs")
os.makedirs(tel)
os.makedirs(runs)
write(os.path.join(tel, "real.jsonl"),
      session("real-1", "run-real", "jlu/qwen3-coder-next", 2500.0, True,
              ("ls", "src/a.c")))
write(os.path.join(tel, "mock.jsonl"),
      # 300 ms on purpose: this session must be caught by its NAME alone, and
      # the fast one below by its latency alone -- a fixture either rule could
      # catch proves neither (found by perturbing each rule in turn).
      session("mock-1", "run-mock", "mock", 300.0, True, ("pwd", "src/b.c")))
write(os.path.join(tel, "fast.jsonl"),
      session("fast-1", "run-fast", "coder-next", 0.4, True, ("date", "src/c.c")))
write(os.path.join(tel, "dead.jsonl"),
      session("dead-1", "run-dead", "jlu/qwen3-coder-next", 3.0, False, None))
# A real corpus holds output text truncated mid-character at the event-log cap:
# a line that is not valid UTF-8.  reread_ratio.py died on the workstation's
# corpus on exactly this (found while recording M720's numbers), so every script
# must read past it.
with open(os.path.join(tel, "real.jsonl"), "ab") as f:
    f.write(b'{"event":"tool_call","sid":"real-1","run":"run-real","turn":1,'
            b'"depth":0,"seq":99,"name":"run_terminal_command","ok":true,'
            b'"output_bytes":20,"args":"echo","args_full":"{\\"command\\":'
            b'\\"echo\\"}","output":"cut mid-character \xe2\x28"}\n')
write(os.path.join(runs, "real.jsonl"), journal("run-real", None))
write(os.path.join(runs, "mock.jsonl"), journal("run-mock", None))
write(os.path.join(runs, "named.jsonl"), journal("run-named", "mock"))
write(os.path.join(runs, "orphan.jsonl"), journal("run-orphan", "jlu/x"))

# ---- success_repeats.py (plan D1) ---------------------------------------------
out = run("success_repeats.py", tel)
inc = run("success_repeats.py", tel, "--include-synthetic")
check(num(r"turns with tool calls: (\d+)", out) == 1,
      "success_repeats counts only the real session's turn",
      "got %r" % num(r"turns with tool calls: (\d+)", out))
check(re.search(r"population: 1 real sessions, 2 synthetic \(.*a model named mock x1.*"
                r"every answered call under 50 ms x1.*\), 1 with no answered", out),
      "success_repeats names what it dropped, and why",
      out.splitlines()[:3].__repr__())
check(num(r"turns with tool calls: (\d+)", inc) == 3 and "INCLUDED" in inc,
      "success_repeats --include-synthetic restores the old population",
      "got %r" % num(r"turns with tool calls: (\d+)", inc))
# M731: --per-turn is the population D1's threshold is FITTED on, so it must be
# the same population the counts above describe -- real turns only, one row each,
# joinable to the journal on `run` -- and carry the maxima a threshold is read from.
pt = os.path.join(tmp, "per-turn.tsv")
run("success_repeats.py", tel, "--per-turn", pt)
rows = ([l.rstrip("\n").split("\t") for l in open(pt)]
        if os.path.exists(pt) else [])
check(len(rows) == 2 and rows[0][:3] == ["run", "sid", "turn"]
      and rows[1][0] == "run-real",
      "success_repeats --per-turn writes one row per REAL turn, keyed on run",
      "got %r" % rows[:3])
check(len(rows) == 2 and rows[1][6:9] == ["6", "6", "run_terminal_command"],
      "the per-turn row carries the raw and unchanged maxima and the tool behind them",
      "got %r" % rows[1:2])

# ---- compaction_pressure.py (the latch's re-arm rule) ---------------------------
out = run("compaction_pressure.py", tel)
inc = run("compaction_pressure.py", tel, "--include-synthetic")
check(num(r"THE CORE\s*:\s*(\d+)", out) == 1 and "population:" in out,
      "compaction_pressure's core holds only the real pass",
      "got %r" % num(r"THE CORE\s*:\s*(\d+)", out))
check(num(r"THE CORE\s*:\s*(\d+)", inc) == 3,
      "compaction_pressure --include-synthetic restores it",
      "got %r" % num(r"THE CORE\s*:\s*(\d+)", inc))

# ---- reread_ratio.py, telemetry route (the re-read advisory) --------------------
out = run("reread_ratio.py", tel)
inc = run("reread_ratio.py", tel, "--include-synthetic")
check(num(r"telemetry route, by PATH: (\d+) read_file", out) == 3
      and "population:" in out,
      "reread_ratio's telemetry route reads only the real session",
      "got %r" % num(r"telemetry route, by PATH: (\d+) read_file", out))
check(num(r"telemetry route, by PATH: (\d+) read_file", inc) == 9,
      "reread_ratio --include-synthetic restores it",
      "got %r" % num(r"telemetry route, by PATH: (\d+) read_file", inc))

# ---- capped_oneshot.py (DEFERRED item 7) ----------------------------------------
out = run("capped_oneshot.py", runs, "--telemetry", tel)
inc = run("capped_oneshot.py", runs, "--telemetry", tel, "--include-synthetic")
check(num(r"capped one-shots \(start\.one_shot true, M715\+\): (\d+)", out) == 2,
      "capped_oneshot keeps the real run and the unverified one, drops the two mocks",
      "got %r" % num(r"capped one-shots \(start\.one_shot true, M715\+\): (\d+)", out))
check(re.search(r"population: 1 journals of real runs, 2 synthetic, 0 unanswered, "
                r"1 unverified", out),
      "capped_oneshot says which journals were real, synthetic and unverified",
      [l for l in out.splitlines() if "population" in l].__repr__())
check(num(r"capped one-shots \(start\.one_shot true, M715\+\): (\d+)", inc) == 4,
      "capped_oneshot --include-synthetic restores all four",
      "got %r" % num(r"capped one-shots \(start\.one_shot true, M715\+\): (\d+)", inc))

# ---- strict_green_fp.py (the tracked-path rule) ---------------------------------
out = run("strict_green_fp.py", runs, "--telemetry", tel)
inc = run("strict_green_fp.py", runs, "--telemetry", tel, "--include-synthetic")
check(num(r"flagged out-of-scope \(what strict-green downgrades\): (\d+)", out) == 2
      and "population:" in out,
      "strict_green_fp counts the real and unverified runs only",
      "got %r" % num(r"flagged out-of-scope \(what strict-green downgrades\): (\d+)", out))
check(num(r"flagged out-of-scope \(what strict-green downgrades\): (\d+)", inc) == 4,
      "strict_green_fp --include-synthetic restores all four",
      "got %r" % num(r"flagged out-of-scope \(what strict-green downgrades\): (\d+)", inc))

failed = [m for good, m, _ in results if not good]
if failed:
    _e2e.fail("%d of %d checks failed: %s" % (len(failed), len(results),
                                               "; ".join(failed)))
_e2e.ok("measurement scripts count real runs only, say what they dropped, "
        "and restore it on request (%d checks)" % len(results))
