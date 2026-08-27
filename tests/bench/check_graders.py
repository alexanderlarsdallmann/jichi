#!/usr/bin/env python3
"""Prove every bench grader is two-sided, THROUGH THE RUNNER'S OWN PARSER.

Each corpus `verify` must reject the untouched fixture and accept a reference
solution. A grader that cannot fail is the hollow gate of ANECDOTES #17; a
grader that cannot pass is worse, because it reports a working model as broken.

Why this exists as a script rather than a paragraph of advice: the first version
of task 09's grader was validated by an ad-hoc snippet that unescaped the YAML
differently from `run_bench.parse_spec`. The snippet said the grader was fine;
the runner then scored a correct edit as a failure, five times, and the blame
landed on the model. So this checker imports `run_bench.parse_spec` and uses the
exact string the runner will execute -- validating a grader through a different
code path than the one that runs it is not validation.

Reference solutions live here, next to the check, deliberately: writing one
forces you to state what "solved" means for a new task in code rather than in
prose.

Usage: python3 tests/bench/check_graders.py   (offline, no model needed)
Exit 0 when every grader is two-sided.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run_bench  # noqa: E402

CORPUS = os.path.join(HERE, "corpus")


def _sub(ws, path, a, b):
    p = os.path.join(ws, path)
    t = open(p, encoding="utf-8").read()
    if a not in t:
        raise AssertionError("reference solution stale: %r not in %s" % (a, path))
    open(p, "w", encoding="utf-8").write(t.replace(a, b))


def _write(ws, path, text):
    with open(os.path.join(ws, path), "w", encoding="utf-8") as f:
        f.write(text)


def solve(task, ws):
    """Apply a correct solution for `task` inside workspace `ws`."""
    if task.startswith("01"):
        _write(ws, "answer.txt", "7\n")
    elif task.startswith("02"):
        _write(ws, "found.txt", "src/b.c\n")
    elif task.startswith("03"):
        _sub(ws, "greet.c", "Hello, World", "Hello, Giessen")
    elif task.startswith("04"):
        _sub(ws, "queue.h", "MAX_ITEMS 16", "MAX_ITEMS 32")
        _sub(ws, "README.md", "(16)", "(32)")
    elif task.startswith("05"):
        _write(ws, "util.h", "#ifndef JC_UTIL_H\n#define JC_UTIL_H\n"
                             "int jc_add(int a, int b);\n#endif\n")
    elif task.startswith("06"):
        _write(ws, "count.txt", "3\n")
    elif task.startswith("07"):
        _sub(ws, "math.c", "return a - b;", "return a + b;")
    elif task.startswith("08"):
        for f in ("buf.h", "buf.c", "app.c"):
            _sub(ws, f, "buf_reset", "buf_clear")
    elif task.startswith("09"):
        # Only the [server] occurrence changes -- the point of the task.
        p = os.path.join(ws, "settings.ini")
        t = open(p, encoding="utf-8").read()
        i = t.index("[server]")
        open(p, "w", encoding="utf-8").write(
            t[:i] + t[i:].replace("timeout = 30", "timeout = 60", 1))
    elif task.startswith("10"):
        for f in ("src/net.h", "src/net.c", "src/main.c"):
            _sub(ws, f, "DEFAULT_PORT", "LISTEN_PORT")
    elif task.startswith("11"):
        _write(ws, "answer.txt", "12\n")
    else:
        return False
    return True


def run_verify(verify, ws):
    p = subprocess.run(["sh", "-c", verify], cwd=ws, capture_output=True,
                       text=True, timeout=120)
    return p.returncode, p.stderr.strip()


def check(task):
    tdir = os.path.join(CORPUS, task)
    spec = run_bench.parse_spec(os.path.join(tdir, "spec.md"))
    verify = spec["verify"]
    problems = []
    if not verify:
        return ["no verify command"]

    for solved in (False, True):
        ws = tempfile.mkdtemp(prefix="jichi_grader_")
        try:
            shutil.copytree(os.path.join(tdir, "files"), ws, dirs_exist_ok=True)
            if solved and not solve(task, ws):
                problems.append("no reference solution defined "
                                "(add one to solve() in check_graders.py)")
                continue
            rc, err = run_verify(verify, ws)
            if solved and rc != 0:
                problems.append("rejects a CORRECT solution (rc=%d)%s"
                                % (rc, (" stderr=" + err[:160]) if err else ""))
            if not solved and rc == 0:
                problems.append("accepts the UNTOUCHED fixture (rc=0)")
        except AssertionError as e:
            problems.append(str(e))
        finally:
            shutil.rmtree(ws, ignore_errors=True)
    return problems


def main():
    tasks = sorted(d for d in os.listdir(CORPUS)
                   if os.path.isdir(os.path.join(CORPUS, d)))
    bad = 0
    for t in tasks:
        problems = check(t)
        if problems:
            bad += 1
            print("FAIL %-24s %s" % (t, "; ".join(problems)))
        else:
            print("ok   %-24s two-sided" % t)
    if bad:
        print("\n%d of %d graders are not two-sided" % (bad, len(tasks)))
        return 1
    print("\nall %d graders reject the fixture and accept a solution" % len(tasks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
