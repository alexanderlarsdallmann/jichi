#!/usr/bin/env python3
"""refute_ab.py -- the refute A/B: produce the reports, run it, blind it, score it.

Pre-registration: docs/proposals/2026-09-refute-ab.md. This file does what that
page fixed and nothing it did not: the model, the twelve reports, the two arms,
opaque run ids, a grading form a PERSON fills in blind, and a verdict computed
only from that form. A measurement, not a gate -- it needs a live model and must
never run in `make ci`.

Provenance (docs/analysis/2026-09-17-reading-the-footer-in-anger.md): the first
version of this file was written by jichi itself, in plan mode and then --auto,
as the real task behind the reach-footer note. Its structure survives; its
jichi invocation (`--workflow`, an option that does not exist), its control arm
(the report passed with no "review critically" instruction), its per-report
grading form (one answer for two arms) and its score (a grep, where the
pre-registration says a person) did not. What was kept and what was replaced is
in that note.
"""

import argparse
import json
import os
import re
import secrets
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
BIN = os.path.join(ROOT, "jichi")
REPORTS_DIR = os.path.join(HERE, "reports")
PLANTED_TSV = os.path.join(HERE, "planted.tsv")
FILES_TXT = os.path.join(HERE, "files.txt")

MODEL = "jlu/qwen3-coder-next"           # free; priced_model_lint holds this
API_BASE = "https://api.hrz.uni-giessen.de/v1"
KEY_ENV = "JICHI_API_KEY"

# The control arm's words, fixed by the pre-registration.
CONTROL_PREFIX = "Review the following critically and list anything wrong:\n\n"

VERDICTS = {
    "frame": "the frame did work the words did not",
    "words": "the frame adds nothing over the words",
    "neither": "the second seat did not find planted errors on this model",
}


def die(msg):
    sys.exit("refute_ab: %s" % msg)


def write_config(path):
    """One config for every run. Everything resource- or prompt-shaped is pinned
    so the machine is not a hidden variable (craft_ab's rule)."""
    cfg = {
        "models": [{
            "name": "subject",
            "provider": "openai",
            "model": MODEL,
            "apiBase": API_BASE,
            "apiKeyEnv": KEY_ENV,
            "roles": ["chat"],
        }],
        "lowResource": False,
        "repoMap": False,
        "references": False,
        "autoContext": False,
        "markdown": False,
        "wisdom": False,
        "snapshots": False,
        "toolProfile": "full",
        "maxRetries": 1,
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)


def preflight():
    if not os.path.exists(BIN):
        die("build jichi first (make) -- missing %s" % BIN)
    if not os.environ.get(KEY_ENV):
        die("$%s is not set (source ~/.jichi.env)" % KEY_ENV)


def jichi_workflow(cfg, spec, cwd, deadline):
    """Run `jichi workflow <spec>` once. The workflow subcommand prints the
    pipeline's final context to stdout; that text IS the run's answer. No
    --budget-tokens: a cap that fires manufactures a different answer. The
    deadline is headroom, not a working cap."""
    spec_path = spec if isinstance(spec, str) else None
    home = tempfile.mkdtemp(prefix="refute_ab_home_")
    try:
        argv = [BIN, "--config", cfg, "--no-session", "--deadline", deadline,
                "workflow", spec_path]
        t0 = time.time()
        try:
            p = subprocess.run(argv, capture_output=True, text=True, timeout=3600,
                               cwd=cwd, stdin=subprocess.DEVNULL,
                               env=dict(os.environ, HOME=home, LANG="C",
                                        LC_ALL="C", NO_COLOR="1"))
            rc, out, err = p.returncode, p.stdout, p.stderr
        except subprocess.TimeoutExpired as e:
            rc, out, err = 124, (e.stdout or ""), (e.stderr or "")
            if isinstance(out, bytes):
                out = out.decode("utf-8", "replace")
            if isinstance(err, bytes):
                err = err.decode("utf-8", "replace")
        return rc, out, err, round(time.time() - t0, 1)
    finally:
        shutil.rmtree(home, ignore_errors=True)


def read_files_txt():
    if not os.path.exists(FILES_TXT):
        die("missing %s" % FILES_TXT)
    with open(FILES_TXT, encoding="utf-8") as f:
        items = [l.strip() for l in f if l.strip() and not l.startswith("#")]
    if len(items) != 12:
        die("files.txt must name exactly 12 files (found %d)" % len(items))
    for item in items:
        if not os.path.exists(os.path.join(ROOT, item)):
            die("files.txt names a file that does not exist: %s" % item)
    return items


def cmd_reports(args):
    """The first seat: one read-only map stage per file, the model reviewing it.
    Saved verbatim; the planting is done BY HAND afterwards, so an existing
    report is never overwritten without --force."""
    preflight()
    items = read_files_txt()
    os.makedirs(REPORTS_DIR, exist_ok=True)
    tmpdir = tempfile.mkdtemp(prefix="refute_ab_")
    try:
        cfg = os.path.join(tmpdir, "config.json")
        write_config(cfg)
        for item in items:
            report_path = os.path.join(REPORTS_DIR, os.path.basename(item) + ".md")
            if os.path.exists(report_path) and not args.force:
                die("%s exists; the reports are edited by hand after this step, "
                    "so re-generating them would destroy the planting. --force "
                    "overwrites." % report_path)
            spec = {"name": "refute-ab-first-seat", "stages": [{
                "type": "map", "readonly": True,
                "prompt": "Review $ITEM for defects. For each finding cite the "
                          "function and the line, and quote the line.",
                "items": [item]}]}
            spec_path = os.path.join(tmpdir, "spec.json")
            with open(spec_path, "w", encoding="utf-8") as f:
                json.dump(spec, f)
            rc, out, err, wall = jichi_workflow(cfg, spec_path, ROOT, args.deadline)
            if rc != 0 or not out.strip():
                print("WARNING: %s exited %d after %ss with %d bytes; last stderr: %s"
                      % (item, rc, wall, len(out), err.strip().splitlines()[-1:]))
            with open(report_path, "w", encoding="utf-8") as f:
                f.write(out)
            print("wrote %s (%ss, %d bytes)" % (os.path.relpath(report_path, HERE), wall, len(out)))
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)
    print("Now plant ONE false claim in each report by hand, list them in "
          "planted.tsv, and only then: python3 refute_ab.py run --label <label>")


def read_planted():
    """planted.tsv: report-file<TAB>subject<TAB>what was planted. The subject is
    what a hit must mention (a function name, a line number); it is for the
    grader's eyes, never for the model's."""
    if not os.path.exists(PLANTED_TSV):
        die("missing %s -- plant the claims first" % PLANTED_TSV)
    rows = []
    with open(PLANTED_TSV, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line.strip() or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 3:
                die("planted.tsv row has fewer than 3 tab-separated columns: %r" % line)
            rows.append({"file": parts[0].strip(), "subject": parts[1].strip(),
                         "description": parts[2].strip()})
    if len(rows) < 12:
        die("planted.tsv has %d rows; the pre-registration says 12" % len(rows))
    for r in rows:
        if not os.path.exists(os.path.join(REPORTS_DIR, r["file"])):
            die("planted.tsv names a report that does not exist: reports/%s" % r["file"])
    return rows


def cmd_run(args):
    """Two arms per planted report, under opaque ids; the condition is written
    only to .sealed/runs.json."""
    preflight()
    planted = read_planted()
    label = args.label or time.strftime("%Y%m%d-%H%M%S")
    outdir = os.path.join(HERE, "results", label)
    if os.path.exists(os.path.join(outdir, ".sealed")):
        die("%s already has a sealed run; pick another --label" % outdir)
    os.makedirs(os.path.join(outdir, "runs"), exist_ok=True)
    os.makedirs(os.path.join(outdir, ".sealed"), exist_ok=True)
    cfg = os.path.join(outdir, "config.json")
    write_config(cfg)
    recs = []
    for entry in planted:
        with open(os.path.join(REPORTS_DIR, entry["file"]), encoding="utf-8") as f:
            report = f.read()
        arms = [("refute", {"type": "refute", "prompt": report}),
                ("control", {"type": "synthesize", "prompt": CONTROL_PREFIX + report})]
        secrets.SystemRandom().shuffle(arms)     # run order is not the arm
        for arm, stage in arms:
            rid = "r-" + secrets.token_hex(4)
            rundir = os.path.join(outdir, "runs", rid)
            os.makedirs(rundir)
            spec_path = os.path.join(rundir, "spec.json")
            with open(spec_path, "w", encoding="utf-8") as f:
                json.dump({"name": "refute-ab", "stages": [stage]}, f)
            rc, out, err, wall = jichi_workflow(cfg, spec_path, ROOT, args.deadline)
            with open(os.path.join(rundir, "answer.md"), "w", encoding="utf-8") as f:
                f.write(out)
            with open(os.path.join(rundir, "stderr.txt"), "w", encoding="utf-8") as f:
                f.write(err)
            # spec.json names the stage type, which IS the condition: it leaves
            # the run directory now that the run is over.
            os.remove(spec_path)
            recs.append({"rid": rid, "arm": arm, "report": entry["file"],
                         "subject": entry["subject"], "rc": rc, "wall_s": wall,
                         "bytes": len(out)})
            print("  %s  %s  rc=%d  %ss  %d bytes" % (entry["file"], rid, rc, wall, len(out)))
    with open(os.path.join(outdir, ".sealed", "runs.json"), "w", encoding="utf-8") as f:
        json.dump({"label": label, "model": MODEL,
                   "when": time.strftime("%Y-%m-%dT%H:%M:%S"), "runs": recs}, f, indent=2)
    with open(os.path.join(outdir, "meta.json"), "w", encoding="utf-8") as f:
        json.dump({"label": label, "model": MODEL, "runs": len(recs)}, f, indent=2)
    print("wrote %s (%d runs). next: python3 refute_ab.py blind --label %s"
          % (outdir, len(recs), label))


def cmd_blind(args):
    """The grading pack: per report, the two answers as A and B in a random
    order, mtimes flattened, the mapping sealed. One form row PER ARM."""
    outdir = os.path.join(HERE, "results", args.label)
    sealed_path = os.path.join(outdir, ".sealed", "runs.json")
    if not os.path.exists(sealed_path):
        die("no sealed run under %s" % outdir)
    gdir = os.path.join(outdir, "grading")
    form = os.path.join(gdir, "FORM.md")
    if os.path.exists(form) and not args.force:
        with open(form, encoding="utf-8") as f:
            if "?" not in f.read():
                die("FORM.md is already filled in; re-blinding would re-draw A/B "
                    "and make those answers meaningless. --force to start over.")
    if os.path.exists(gdir):
        shutil.rmtree(gdir)
    os.makedirs(gdir)
    sealed = json.load(open(sealed_path, encoding="utf-8"))
    by_report = {}
    for r in sealed["runs"]:
        by_report.setdefault(r["report"], {})[r["arm"]] = r
    mapping = {}
    lines = ["# Refute A/B -- grading form (%s, %s)" % (args.label, MODEL), "",
             "Per report, two answers, A and B, in an order drawn at random per report.",
             "For EACH of A and B answer three things. `planted.tsv` says what was",
             "planted in that report; a HIT is an answer that names the planted claim's",
             "subject as false or as unsupported (under Rebutting or Undercutting in a",
             "framed answer; anywhere in a free one). `attacks_true` counts claims the",
             "answer attacks that are in fact TRUE of the source -- check the source.",
             "Do not open .sealed/ until you run score.", ""]
    for report in sorted(by_report):
        arms = by_report[report]
        if "refute" not in arms or "control" not in arms:
            print("skipping %s: only one arm recorded" % report)
            continue
        order = ["refute", "control"]
        secrets.SystemRandom().shuffle(order)
        pdir = os.path.join(gdir, report)
        os.makedirs(pdir)
        for letter, arm in zip("AB", order):
            shutil.copyfile(os.path.join(outdir, "runs", arms[arm]["rid"], "answer.md"),
                            os.path.join(pdir, "%s.md" % letter))
            lines += ["## %s / %s" % (report, letter), "",
                      "hit (y/n): ?", "attacks_true (count): ?", "notes: ?", ""]
        mapping[report] = {"A": order[0], "B": order[1]}
    flat = (1767225600, 1767225600)
    for base, dirs, files in os.walk(gdir):
        for name in files + dirs:
            os.utime(os.path.join(base, name), flat)
    with open(form, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    os.utime(form, flat)
    with open(os.path.join(outdir, ".sealed", "mapping.json"), "w", encoding="utf-8") as f:
        json.dump(mapping, f, indent=2)
    print("wrote %s -- fill in FORM.md, then: python3 refute_ab.py score --label %s"
          % (gdir, args.label))
    print("Do NOT open %s until the form is filled." % os.path.join(outdir, ".sealed"))


def parse_form(path):
    answers = {}
    cur = None
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            m = re.match(r"^## (.+?) / ([AB])$", line)
            if m:
                cur = (m.group(1), m.group(2))
                answers[cur] = {}
                continue
            if cur is None:
                continue
            m = re.match(r"^(hit|attacks_true|notes)\b[^:]*:\s*(.*)$", line)
            if m:
                answers[cur][m.group(1)] = m.group(2).strip()
    return answers


def cmd_score(args):
    """Join the PERSON's form with the sealed mapping. Nothing here reads an
    answer file: the grep the first draft did in place of the grader is exactly
    what the pre-registration ruled out."""
    outdir = os.path.join(HERE, "results", args.label)
    form = os.path.join(outdir, "grading", "FORM.md")
    for p in (form, os.path.join(outdir, ".sealed", "mapping.json"),
              os.path.join(outdir, ".sealed", "runs.json")):
        if not os.path.exists(p):
            die("missing %s" % p)
    mapping = json.load(open(os.path.join(outdir, ".sealed", "mapping.json"), encoding="utf-8"))
    sealed = json.load(open(os.path.join(outdir, ".sealed", "runs.json"), encoding="utf-8"))
    answers = parse_form(form)
    unfilled = [k for k, a in answers.items() if a.get("hit", "?") in ("?", "")]
    if unfilled:
        die("%d form entries are still '?': %s" % (len(unfilled), ", ".join("%s/%s" % k for k in unfilled[:4])))
    tally = {"refute": {"n": 0, "hits": 0, "false_attacks": 0},
             "control": {"n": 0, "hits": 0, "false_attacks": 0}}
    for (report, letter), a in answers.items():
        arm = mapping.get(report, {}).get(letter)
        if arm is None:
            die("form names %s/%s, which the mapping does not" % (report, letter))
        t = tally[arm]
        t["n"] += 1
        t["hits"] += 1 if a["hit"].lower().startswith("y") else 0
        try:
            t["false_attacks"] += int(re.match(r"\d+", a.get("attacks_true", "0") or "0").group(0))
        except (AttributeError, ValueError):
            die("attacks_true for %s/%s is not a number: %r" % (report, letter, a.get("attacks_true")))
    walls = {"refute": [], "control": []}
    for r in sealed["runs"]:
        walls[r["arm"]].append(r["wall_s"])
    print("Refute A/B -- %s -- model %s" % (args.label, sealed["model"]))
    for arm in ("refute", "control"):
        t = tally[arm]
        w = walls[arm]
        print("  %-8s hits %d of %d   false attacks %d   mean wall %.0fs (n=%d)"
              % (arm, t["hits"], t["n"], t["false_attacks"],
                 sum(w) / len(w) if w else 0.0, len(w)))
    nr, nc = tally["refute"]["hits"], tally["control"]["hits"]
    if nr >= 6 and nr > nc:
        v = VERDICTS["frame"]
    elif nr >= 6:
        v = VERDICTS["words"]
    else:
        v = VERDICTS["neither"]
    print("pre-registered verdict: %s  (refute %d/%d, control %d/%d; threshold 6 of 12 and more than control)"
          % (v, nr, tally["refute"]["n"], nc, tally["control"]["n"]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("reports", help="produce the 12 first-seat reports (spends wall-clock)")
    r.add_argument("--force", action="store_true", help="overwrite existing reports -- destroys the planting")
    r.add_argument("--deadline", default="30m", help="per-run headroom, not a working cap")
    r.set_defaults(fn=cmd_reports)
    ru = sub.add_parser("run", help="run both arms per planted report (spends wall-clock)")
    ru.add_argument("--label", default=None)
    ru.add_argument("--deadline", default="30m")
    ru.set_defaults(fn=cmd_run)
    b = sub.add_parser("blind", help="build the blinded grading pack")
    b.add_argument("--label", required=True)
    b.add_argument("--force", action="store_true")
    b.set_defaults(fn=cmd_blind)
    s = sub.add_parser("score", help="join the filled form with the sealed mapping")
    s.add_argument("--label", required=True)
    s.set_defaults(fn=cmd_score)
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
