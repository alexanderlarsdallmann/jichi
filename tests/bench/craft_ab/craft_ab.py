#!/usr/bin/env python3
"""craft_ab.py -- the frontier craft A/B: run it, blind it, score it.

The experiment M318 could not run. M318 measured the `craft` system-prompt
section (M299) on one 31B local model over three GRADED tasks and got 18/18 in
both conditions -- uninformative, because every graded task names its
deliverable, so it measures instruction-following rather than whether a habit
was instilled. `DEFERRED.md` recorded what a real test would need, and it was
three things, not one:

    1. a frontier model                       -- arrived 2026-08-07 (a key)
    2. a task whose deliverable is UNSTATED   -- tasks/ , this directory
    3. a grader who is not the author of the  -- YOU. Not this script, and not
       section under test                        the model that wrote the
                                                 section or these questions.

So this script deliberately does NOT score quality. It runs the pairs, strips
every clue to which is which, and hands you a form. `--score` only counts what
you wrote.

    python3 craft_ab.py run   --model jlu/qwen3-coder-next --pairs 3
    python3 craft_ab.py blind --label <label>
    ...fill in results/<label>/grading/FORM.md...
    python3 craft_ab.py score --label <label>

WHY BLINDING IS NOT OPTIONAL HERE. The section under test was written by
Claude; so were these tasks and this form. An unblinded grader who knows which
output came from the section's own condition cannot un-know it, and the author
of a thing is the last person whose unblinded judgement of it is worth having.

And in this workflow the person who RUNS the session is the person who GRADES
it, so the blind has to survive them looking around their own results
directory. The condition therefore appears in exactly one place -- `.sealed/`
-- and nowhere in the run directories (opaque ids), the public `meta.json`
(redacted), the run order (randomised, not alternated), the pack's file
timestamps (flattened), or the progress line (names the run, never the arm).
"""

import argparse
import hashlib
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
BIN = os.path.join(ROOT, "jichi")
TASKS = os.path.join(HERE, "tasks")

# The literal the craft section opens with (src/chat/jc_sysmsg.c, PROMPT_CRAFT_*).
# The preflight greps `jichi sysmsg` for it in BOTH conditions, because an A/B
# whose two arms are secretly identical reports a null result forever and looks
# exactly like a real one. (M86: a verify that passed while running nothing.)
CRAFT_MARK = "How to work (the craft):"


# --- the pre-registered form -------------------------------------------------
# Fixed here, before any run, so the questions cannot be chosen after seeing the
# outputs. `preferred` is the PRIMARY outcome and is deliberately phrased
# without any of the section's vocabulary -- it does not mention design,
# alternatives, or honesty -- so that it cannot be answered by pattern-matching
# the thing being tested. The rest are secondary and diagnostic: they ask
# whether the specific claimed behaviours appeared, and they are derived from
# the section's own text, which is stated rather than hidden.
QUESTIONS = [
    ("preferred",
     "A|B|tie",
     "PRIMARY. Which would you rather have received from a colleague?"),
    ("trust_more",
     "A|B|tie",
     "Which would you more readily act on without checking it yourself?"),
    ("wrote_down_why",
     "A|B|both|neither",
     "Which explains WHY it did what it did (not just what)?"),
    ("named_alternative",
     "A|B|both|neither",
     "Which names an approach it considered and rejected?"),
    ("honest_about_gaps",
     "A|B|both|neither",
     "Which says plainly what it did not do, or did not verify?"),
    ("noticed_the_real_problem",
     "A|B|both|neither",
     "Which found the problem that actually matters? (see the task's notes)"),
    ("overreach",
     "A|B|both|neither",
     "Which did MORE than the situation called for? (a cost, not a virtue)"),
]


def sh(argv, **kw):
    return subprocess.run(argv, capture_output=True, text=True, **kw)


def parse_prompt(path):
    """Split the YAML-ish frontmatter from the prompt body.

    Only the BODY is ever sent to the model. The frontmatter records what is
    deliberately unstated and where the real problems are -- notes for the
    human grader, and they would give the game away if they were in the prompt.
    """
    text = open(path, encoding="utf-8").read()
    body, meta = text, {}
    if text.startswith("---\n"):
        end = text.find("\n---\n", 4)
        if end != -1:
            head, body = text[4:end], text[end + 5:]
            key = None
            for line in head.split("\n"):
                m = re.match(r"^([a-z_]+):\s*(.*)$", line)
                if m:
                    key = m.group(1)
                    meta[key] = m.group(2).strip()
                elif key and line.strip():
                    meta[key] = (meta.get(key, "") + " " + line.strip()).strip()
    return meta, body.strip()


def write_config(path, args, craft):
    """The two arms differ in ONE key. Everything resource- or prompt-shaped is
    pinned explicitly rather than left to a default, because several defaults
    (craft included) move with --lite and with detected RAM -- so an unpinned
    config makes the machine a hidden variable."""
    cfg = {
        "models": [{
            "name": "subject",
            "provider": "openai",
            "model": args.model,
            "apiBase": args.api_base,
            "apiKeyEnv": args.key_env,
            "roles": ["chat"],
        }],
        "craft": bool(craft),
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
    if args.context_limit:
        cfg["models"][0]["contextLength"] = args.context_limit
    if args.price_in:
        cfg["models"][0]["inputCostPer1M"] = args.price_in
    if args.price_out:
        cfg["models"][0]["outputCostPer1M"] = args.price_out
    with open(path, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)


def preflight(args, outdir):
    """Prove the two arms actually differ before spending a single token.

    Renders the system prompt under each config and requires the craft marker
    present in one and absent in the other. Without this the experiment can
    silently test nothing -- which is the failure mode this project has hit
    twice (M86's hollow verify, M201's truncating readers) and the reason
    `docs/TEST_INTEGRITY.md` exists.
    """
    home = tempfile.mkdtemp(prefix="jichi_ab_pre_")
    try:
        seen = {}
        for craft in (True, False):
            cfg = os.path.join(outdir, "config.craft-%s.json"
                               % ("on" if craft else "off"))
            write_config(cfg, args, craft)
            p = sh([BIN, "--config", cfg, "sysmsg"],
                   env=dict(os.environ, HOME=home, LANG="C", LC_ALL="C"))
            seen[craft] = (CRAFT_MARK in p.stdout, len(p.stdout))
        on_has, on_len = seen[True]
        off_has, off_len = seen[False]
        if not on_has:
            sys.exit("preflight: craft:true produced no %r -- the ON arm is "
                     "not testing the section" % CRAFT_MARK)
        if off_has:
            sys.exit("preflight: craft:false still contains %r -- the OFF arm "
                     "is not a control" % CRAFT_MARK)
        delta = on_len - off_len
        if delta <= 0:
            sys.exit("preflight: craft:true is not larger than craft:false "
                     "(%d vs %d bytes)" % (on_len, off_len))
        print("preflight ok: the section is present in ON, absent in OFF "
              "(+%d bytes of system prompt)" % delta)
        return delta
    finally:
        shutil.rmtree(home, ignore_errors=True)


def snapshot(src):
    """Map every file under `src` to its sha256, so a run's changes are a diff
    against the pristine fixture rather than a guess."""
    out = {}
    for base, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d not in (".git", "__pycache__")]
        for name in files:
            full = os.path.join(base, name)
            rel = os.path.relpath(full, src)
            try:
                out[rel] = hashlib.sha256(open(full, "rb").read()).hexdigest()
            except OSError:
                out[rel] = "?"
    return out


def changes(before, after):
    add = sorted(k for k in after if k not in before)
    rm = sorted(k for k in before if k not in after)
    mod = sorted(k for k in after if k in before and after[k] != before[k])
    return {"added": add, "removed": rm, "modified": mod}


def read_events(path):
    evs = []
    if os.path.exists(path):
        for line in open(path, encoding="utf-8", errors="replace"):
            line = line.strip()
            if line:
                try:
                    evs.append(json.loads(line))
                except ValueError:
                    pass
    return evs


# Keys that name the condition. They live in .sealed/runs.json and nowhere a
# grader browsing results/<label>/ can reach: renaming the directories alone
# would have been theatre, since meta.json sat at the top of the tree with
# "craft": true on every run.
SEALED_KEYS = ("craft", "label")


def redact(rec):
    return dict((k, v) for k, v in rec.items() if k not in SEALED_KEYS)


def one_run(task, pair, craft, rid, args, outdir):
    tid = os.path.basename(task)
    label = "%s__p%d__%s" % (tid, pair, "on" if craft else "off")
    rundir = os.path.join(outdir, "runs", rid)
    os.makedirs(rundir, exist_ok=True)
    meta, prompt = parse_prompt(os.path.join(task, "prompt.md"))
    fixture = os.path.join(task, "files")
    ws = os.path.join(rundir, "workspace")
    home = tempfile.mkdtemp(prefix="jichi_ab_home_")
    shutil.copytree(fixture, ws)
    before = snapshot(ws)
    cfg = os.path.join(outdir, "config.craft-%s.json" % ("on" if craft else "off"))
    events_path = os.path.join(rundir, "events.jsonl")

    # M546: a FENCE bounds this run; the CAPS are off by default.
    #
    # CLAUDE.md draws the line and this harness used to ignore it:
    #   fences (--edit-scope, pathFence, --max-tool-calls) bound BLAST RADIUS and
    #   stay on; caps (--deadline, --budget-tokens, stall/request timeouts) bound
    #   DURATION and stay OFF for measurement runs, because "a cap that fires does
    #   not merely hide the answer, it manufactures a plausible different one".
    #
    # This file is a measurement -- its own README says "a measurement, not a gate"
    # -- and it armed a token cap on every run anyway. The comment above already
    # knew why that is wrong ("a run the budget cut off is not a sample") and the
    # code then threw those samples away instead of not creating them. On
    # `service-01` that was 3 of 8 runs, on a default whose help text calibrated it
    # against Opus 4.5 while the model under test was not Opus.
    #
    # --max-tool-calls is the fence and it is enough: N tool calls bound the token
    # spend too, since every model call is driven by one. So the budget defaults to
    # 0 (jichi treats <= 0 as no budget: every enforcement site guards on
    # `budget_tokens > 0.0`) and is passed only when the operator asks for one.
    #
    # --deadline stays, deliberately, and is documented as HEADROOM rather than a
    # working cap: runs take 50-80s and the deadline is 15m. It exists for the case
    # `service-01` hit -- a run_terminal_command child that hung for 13 minutes on
    # a fixture the model had built and run. A bound that can only fire on a
    # genuine fault is the `connect`-timeout case the doctrine says to keep.
    argv = [BIN, "--config", cfg, "--auto", "--no-stdin", "--no-session",
            "--no-route", "--output", "jsonl",
            "--deadline", args.deadline,
            "--max-tool-calls", str(args.max_tool_calls)]
    # Built by appending, never by slicing into a fixed index: the first cut used
    # argv[8:8] and would have emitted `--output --budget-tokens N jsonl`, because
    # index 8 is the VALUE of --output, not the slot after it. The prompt goes last.
    if args.budget_tokens > 0:
        argv += ["--budget-tokens", str(args.budget_tokens)]
    argv += ["-p", prompt]
    t0 = time.time()
    try:
        p = subprocess.run(argv, capture_output=True, text=True,
                           timeout=args.timeout, cwd=ws,
                           env=dict(os.environ, HOME=home, LANG="C",
                                    LC_ALL="C", NO_COLOR="1"))
        rc, out, err = p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired as e:
        rc, out, err = 124, (e.stdout or ""), (e.stderr or "")
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        if isinstance(err, bytes):
            err = err.decode("utf-8", "replace")
    wall = round(time.time() - t0, 1)
    open(events_path, "w", encoding="utf-8").write(out)

    evs = read_events(events_path)
    done = next((e for e in reversed(evs) if e.get("type") == "done"), {})
    answer = done.get("text", "")
    tools = [e for e in evs if e.get("type") == "tool_call"]
    # "Did it look before it leapt?" -- the first mutating tool call, and
    # whether any read/list/search preceded it. A crude proxy, labelled as one.
    MUT = ("write_file", "edit_file", "apply_patch", "run_terminal_command")
    first_mut = next((i for i, t in enumerate(tools)
                      if t.get("name") in MUT[:3]), None)
    rec = {
        "task": tid, "pair": pair, "craft": bool(craft), "rid": rid,
        "label": label,
        "model": args.model, "rc": rc, "wall_s": wall,
        "stop_reason": done.get("stop_reason"),
        "tokens_in": done.get("tokens", {}).get("input"),
        "tokens_out": done.get("tokens", {}).get("output"),
        "cost": done.get("cost"),
        "tool_calls": done.get("tool_calls", len(tools)),
        "tool_sequence": [t.get("name") for t in tools],
        "read_before_first_edit": (None if first_mut is None
                                   else first_mut > 0),
        "changes": changes(before, snapshot(ws)),
        "stderr_tail": err[-800:],
    }
    rec["files_created"] = len(rec["changes"]["added"])
    rec["files_touched"] = (len(rec["changes"]["added"])
                            + len(rec["changes"]["modified"]))
    # A run the budget cut off is not a sample: its answer stops mid-thought,
    # and comparing it with a complete one measures the budget, not the
    # section. Recorded here and enforced in `blind`.
    rec["complete"] = (rec["stop_reason"] == "done")
    # The run directory is browsable, so what lands in it is redacted.
    with open(os.path.join(rundir, "meta.json"), "w", encoding="utf-8") as f:
        json.dump(redact(rec), f, indent=2)
    open(os.path.join(rundir, "answer.md"), "w", encoding="utf-8").write(answer)
    shutil.rmtree(home, ignore_errors=True)
    # The progress line names the RUN, never the arm. In this workflow the
    # person who runs the session is the person who grades it, so a console
    # that prints "…__on  out=5753" beside a pack whose answer lengths are
    # visible hands the mapping straight back. `label` stays in the sealed
    # record for after grading.
    print("  %-16s p%-2d %-10s %-9s %5ss  in=%-7s out=%-6s tools=%-3s "
          "files=%d%s"
          % (tid, pair, rid, rec["stop_reason"], wall, rec["tokens_in"],
             rec["tokens_out"], rec["tool_calls"], rec["files_touched"],
             "" if rec["complete"] else "   <-- TRUNCATED, not gradable"))
    return rec


def cmd_run(args):
    label = args.label or time.strftime("%Y%m%d-%H%M%S")
    outdir = os.path.join(HERE, "results", label)
    os.makedirs(outdir, exist_ok=True)
    if not os.path.exists(BIN):
        sys.exit("build jichi first (make) -- missing %s" % BIN)
    if not os.environ.get(args.key_env):
        sys.exit("$%s is not set; export the key naming it in --key-env "
                 "(the value is never read by this script's output)"
                 % args.key_env)

    delta = preflight(args, outdir)

    tasks = sorted(os.path.join(TASKS, d) for d in os.listdir(TASKS)
                   if os.path.isdir(os.path.join(TASKS, d))
                   and (not args.only or args.only in d))
    if not tasks:
        sys.exit("no tasks matched --only %r" % args.only)

    # Order was alternated by pair parity, which avoided systematic bias but was
    # DEDUCIBLE: directory timestamps plus a documented rule unblind every odd
    # pair. Randomising instead keeps the bias protection and carries no
    # information. The seed is sealed, so a session stays reproducible.
    order_seed = random.SystemRandom().getrandbits(32)
    rng = random.Random(order_seed)
    recs = []
    total = len(tasks) * args.pairs * 2
    print("%d runs: %d task(s) x %d pair(s) x 2 conditions, model %s"
          % (total, len(tasks), args.pairs, args.model))
    for task in tasks:
        print(os.path.basename(task) + ":")
        for pair in range(1, args.pairs + 1):
            order = [True, False]
            rng.shuffle(order)
            for craft in order:
                rid = "r-%08x" % rng.getrandbits(32)
                recs.append(one_run(task, pair, craft, rid, args, outdir))

    meta = {"label": label, "model": args.model, "pairs": args.pairs,
            "tasks": [os.path.basename(t) for t in tasks],
            "craft_bytes_in_system_prompt": delta,
            "budget_tokens": args.budget_tokens,
            "deadline": args.deadline,
            "max_tool_calls": args.max_tool_calls,
            "when": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "jichi_version": sh([BIN, "--version"]).stdout.strip(),
            "git_head": sh(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"]).stdout.strip(),
            "runs": [redact(r) for r in recs]}
    with open(os.path.join(outdir, "meta.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)
    sealed = os.path.join(outdir, ".sealed")
    os.makedirs(sealed, exist_ok=True)
    with open(os.path.join(sealed, "runs.json"), "w", encoding="utf-8") as f:
        json.dump({"order_seed": order_seed, "runs": recs}, f, indent=2)
    print("\nwrote %s" % outdir)
    print("next: python3 craft_ab.py blind --label %s" % label)


# Every blinded file gets this mtime. shutil.copy2 preserves timestamps, and
# run order alternates by pair parity -- so a grader who listed the pack with
# `ls -l` could read the condition off the clock for half the pairs. A blind
# that survives only until someone looks at the metadata is not a blind.
FLAT_MTIME = (1767225600, 1767225600)   # 2026-01-01T00:00:00Z, arbitrary


def _looks_binary(path):
    try:
        with open(path, "rb") as f:
            return b"\0" in f.read(8000)
    except OSError:
        return False


def _flatten_times(root):
    for base, dirs, files in os.walk(root):
        for name in files:
            os.utime(os.path.join(base, name), FLAT_MTIME)
        for name in dirs:
            os.utime(os.path.join(base, name), FLAT_MTIME)


def submission(rundir, dest):
    """One arm's output, with every clue to its condition removed: the answer,
    the diff summary, and the files it created or changed."""
    os.makedirs(dest, exist_ok=True)
    rec = json.load(open(os.path.join(rundir, "meta.json"), encoding="utf-8"))
    shutil.copy2(os.path.join(rundir, "answer.md"),
                 os.path.join(dest, "answer.md"))
    ch = rec["changes"]
    with open(os.path.join(dest, "changes.md"), "w", encoding="utf-8") as f:
        f.write("# What it changed\n\n")
        for kind in ("added", "modified", "removed"):
            f.write("**%s** (%d): %s\n\n"
                    % (kind, len(ch[kind]), ", ".join(ch[kind]) or "-"))
    ws = os.path.join(rundir, "workspace")
    keep = os.path.join(dest, "workspace")
    os.makedirs(keep, exist_ok=True)
    for rel in sorted(set(ch["added"]) | set(ch["modified"])):
        src = os.path.join(ws, rel)
        dst = os.path.join(keep, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if not os.path.exists(src):
            continue
        # Compiled artifacts land here when a run built and ran its work --
        # which is worth knowing, and changes.md records it. The BYTES are
        # noise in a pack meant to be read, so large/binary files are listed
        # rather than copied.
        if os.path.getsize(src) > 262144 or _looks_binary(src):
            with open(dst + ".omitted.txt", "w", encoding="utf-8") as f:
                f.write("%s omitted from this pack: %d bytes, %s.\n"
                        % (rel, os.path.getsize(src),
                           "binary" if _looks_binary(src) else "over 256 KB"))
            continue
        shutil.copy2(src, dst)
    _flatten_times(dest)


def sealed_runs(outdir):
    """The unblinded per-run records. Deliberately the ONLY place the condition
    is written down, so `results/<label>/` can be browsed without spoiling a
    grading in progress."""
    path = os.path.join(outdir, ".sealed", "runs.json")
    if not os.path.exists(path):
        sys.exit("missing %s -- this session predates the sealed layout; "
                 "re-run it, or migrate it by hand" % path)
    return json.load(open(path, encoding="utf-8"))["runs"]


def cmd_blind(args):
    outdir = os.path.join(HERE, "results", args.label)
    meta = json.load(open(os.path.join(outdir, "meta.json"), encoding="utf-8"))
    meta["runs"] = sealed_runs(outdir)
    gdir = os.path.join(outdir, "grading")
    sealed = os.path.join(outdir, ".sealed")
    form = os.path.join(gdir, "FORM.md")
    # Re-blinding destroys the pack AND draws a new A/B assignment, so doing it
    # over a part-filled form loses the grading and silently invalidates what
    # was already answered. Refuse rather than warn: the work being protected
    # is a human's reading of nine pairs, not a file that can be regenerated.
    if os.path.exists(form) and not args.force:
        answered = [ln for ln in open(form, encoding="utf-8")
                    if re.match(r"^[a-z_]+:\s*\S", ln)
                    and not ln.rstrip().endswith(":")
                    and not ln.rstrip().endswith("?")]
        if answered:
            sys.exit("%s already has %d answer(s) filled in. Re-blinding would "
                     "discard them and re-draw the A/B assignment, so the "
                     "answers would no longer mean anything.\n"
                     "  score it:      python3 craft_ab.py score --label %s\n"
                     "  or start over: python3 craft_ab.py blind --label %s "
                     "--force" % (form, len(answered), args.label, args.label))
    if os.path.exists(gdir):
        shutil.rmtree(gdir)
    os.makedirs(sealed, exist_ok=True)
    rng = random.Random(args.seed)
    mapping = {}
    form = ["# Craft A/B -- grading form",
            "",
            "Model: **%s**  ·  runs: %d  ·  %s"
            % (meta["model"], len(meta["runs"]), meta["when"]),
            "",
            "For each pair, replace the `?` with your answer. A and B are the",
            "two arms in a random order that differs per pair -- there is no",
            "pattern to find, and the mapping is sealed until you run `score`.",
            "",
            "Read `A/answer.md` and `B/answer.md` first; `changes.md` and",
            "`workspace/` are there when the answer alone does not settle it.",
            "The task's own notes (`tasks/<id>/prompt.md` frontmatter) say what",
            "was deliberately left unstated and where the real problems are --",
            "read them before grading, they are not visible to either arm.",
            ""]
    for q, opts, text in QUESTIONS:
        form.append("- `%s` (%s) -- %s" % (q, opts, text))
    form.append("")

    by_pair = {}
    for r in meta["runs"]:
        by_pair.setdefault((r["task"], r["pair"]), {})[r["craft"]] = r
    skipped = []
    for (task, pair), arms in sorted(by_pair.items()):
        if True not in arms or False not in arms:
            continue
        # Both arms must have finished. A truncated answer loses to a complete
        # one on every question here, whichever arm it came from, so grading
        # such a pair would measure the budget and report it as the section.
        # M546: each bad arm with ITS OWN stop reason. This used to name the bad
        # arm(s) and then print `arms[True]`'s stop_reason -- the craft-ON arm's --
        # whichever arm had actually failed. On service-02 that produced
        # "02-more-callers/pair3 (craft-off: done)", which is self-contradictory:
        # `complete` IS `stop_reason == "done"`, so a `done` arm can never be the
        # bad one. The reader is told which arm to look at and then handed the
        # other arm's reason for looking. A diagnostic that misattributes is worse
        # than a terse one, because it sends you to the wrong run.
        #
        # `or "no terminal event"` because a stop_reason of None is not a reason:
        # it means the run emitted no `done` event at all -- killed by the deadline
        # or the subprocess timeout before it could -- and printing "None" invites
        # the reader to look for a stop reason by that name.
        bad = ["%s: %s" % ("craft-on" if c else "craft-off",
                           r.get("stop_reason") or "no terminal event")
               for c, r in sorted(arms.items(), reverse=True)
               if not r.get("complete", True)]
        if bad:
            skipped.append("%s/pair%d (%s)" % (task, pair, "; ".join(bad)))
            continue
        flip = rng.random() < 0.5
        a_craft = flip
        pdir = os.path.join(gdir, task, "pair%d" % pair)
        submission(os.path.join(outdir, "runs", arms[a_craft]["rid"]),
                   os.path.join(pdir, "A"))
        submission(os.path.join(outdir, "runs", arms[not a_craft]["rid"]),
                   os.path.join(pdir, "B"))
        mapping["%s/pair%d" % (task, pair)] = {
            "A": "on" if a_craft else "off",
            "B": "off" if a_craft else "on"}
        form.append("## %s / pair%d" % (task, pair))
        form.append("")
        for q, opts, _ in QUESTIONS:
            form.append("%s: ?" % q)
        form.append("notes:")
        form.append("")
    with open(os.path.join(gdir, "FORM.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(form) + "\n")
    with open(os.path.join(sealed, "mapping.json"), "w", encoding="utf-8") as f:
        json.dump({"seed": args.seed, "mapping": mapping}, f, indent=2)
    with open(os.path.join(sealed, "README.md"), "w", encoding="utf-8") as f:
        f.write("# Sealed\n\n`mapping.json` says which of A and B carried the "
                "craft section.\n\nOpening it before the form is filled in "
                "does not invalidate the run in any way a script can detect "
                "-- which is exactly why it is worth not doing. `score` reads "
                "it for you.\n")
    if skipped:
        print("SKIPPED %d pair(s) -- one or both arms did not finish; raise "
              "--budget-tokens / --max-tool-calls and re-run those tasks:"
              % len(skipped))
        for x in skipped:
            print("   ", x)
    print("wrote %s" % gdir)
    # M545: say that this pack is the ONLY copy and is git-ignored.
    #
    # Session-02 (2026-08-10) ran 18/18 against a priced frontier model, ~3.73M
    # input tokens, and built its pack here. DEFERRED.md recorded that the one
    # remaining step was the operator grading results/session-02/grading/FORM.md.
    # By 2026-08-22 that directory did not exist on any machine: `results/` is in
    # .gitignore, nothing committed it, and the grading -- the slowest step and the
    # only one a machine cannot do -- never happened before it was gone. The spend
    # is unrecoverable and the experiment produced no result.
    #
    # A harness that writes an artifact its reader may not reach owes them this
    # sentence. `grading/` is separable from `.sealed/`, so archiving the pack does
    # NOT break the blind: the arm mapping stays sealed in a sibling directory.
    print("")
    print("KEEP THIS: results/ is git-ignored, so this session is the only copy")
    print("           and will not survive a clean checkout or a tidied /home. The")
    print("           grading is the slow step; archive it now, not later:")
    print("")
    print("    tar czf craft-%s.tgz -C %s ." % (args.label,
                                                os.path.join("results",
                                                             args.label)))
    print("")
    print("           The WHOLE label directory, including .sealed/ -- `score`")
    print("           reads .sealed/mapping.json to attribute A and B to arms, so")
    print("           a tarball of grading/ alone can be filled in and never")
    print("           scored. The blind is kept by NOT OPENING .sealed/, which is")
    print("           what the sealed layout is for; deleting it does not protect")
    print("           the blind, it destroys the result. (The first draft of this")
    print("           very message advised archiving grading/ alone.)")
    print("")
    print("fill in %s, then: python3 craft_ab.py score --label %s"
          % (os.path.join(gdir, "FORM.md"), args.label))


def cmd_score(args):
    outdir = os.path.join(HERE, "results", args.label)
    meta = json.load(open(os.path.join(outdir, "meta.json"), encoding="utf-8"))
    meta["runs"] = sealed_runs(outdir)
    sealed = json.load(open(os.path.join(outdir, ".sealed", "mapping.json"),
                            encoding="utf-8"))["mapping"]
    form = open(os.path.join(outdir, "grading", "FORM.md"),
                encoding="utf-8").read()

    answers, cur = {}, None
    for line in form.split("\n"):
        m = re.match(r"^## (\S+) / (pair\d+)\s*$", line)
        if m:
            cur = "%s/%s" % (m.group(1), m.group(2))
            answers[cur] = {}
            continue
        m = re.match(r"^([a-z_]+):\s*(.+?)\s*$", line)
        if m and cur and m.group(1) in dict((q, 1) for q, _, _ in QUESTIONS):
            answers[cur][m.group(1)] = m.group(2).strip()

    unfilled = [p for p, a in answers.items()
                for q, _, _ in QUESTIONS if a.get(q, "?") == "?"]
    tally = dict((q, {"on": 0, "off": 0, "both": 0, "neither": 0, "tie": 0})
                 for q, _, _ in QUESTIONS)
    graded = 0
    for pair, a in sorted(answers.items()):
        if any(a.get(q, "?") == "?" for q, _, _ in QUESTIONS):
            continue
        graded += 1
        for q, _, _ in QUESTIONS:
            v = a[q].strip().upper()
            if v in ("A", "B"):
                tally[q][sealed[pair][v]] += 1
            elif v.lower() in tally[q]:
                tally[q][v.lower()] += 1

    print("Craft A/B -- %s  (model %s)" % (args.label, meta["model"]))
    print("%d of %d pairs graded%s\n"
          % (graded, len(answers),
             "" if not unfilled else "  (%d answers still '?')" % len(unfilled)))
    print("%-26s %6s %6s %6s %6s %6s" %
          ("question", "CRAFT", "off", "both", "neither", "tie"))
    for q, _, _ in QUESTIONS:
        t = tally[q]
        print("%-26s %6d %6d %6d %6d %6d"
              % (q, t["on"], t["off"], t["both"], t["neither"], t["tie"]))

    print("\nMechanical, no grader involved:")
    for cond in (True, False):
        rs = [r for r in meta["runs"] if r["craft"] is cond]
        if not rs:
            continue
        n = len(rs)
        def avg(k):
            vals = [r[k] for r in rs if isinstance(r.get(k), (int, float))]
            return sum(vals) / len(vals) if vals else 0
        print("  craft=%-3s n=%-3d in=%-8.0f out=%-7.0f tools=%-5.1f "
              "files_created=%-4.1f wall=%.0fs"
              % ("ON" if cond else "off", n, avg("tokens_in"),
                 avg("tokens_out"), avg("tool_calls"),
                 avg("files_created"), avg("wall_s")))
    if graded < len(answers):
        print("\nPartial: fill the remaining '?' and re-run score.")
    print("\nn is small by construction. A direction is the most this can show;"
          "\na magnitude is not. Report it with its n, per the M318 rule.")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("run", help="run the pairs (spends tokens)")
    # M544: the default is a FREE model, and that is a rule rather than a
    # preference. --api-base points at the institutional gateway, which exposes
    # 353 model ids including priced ones -- so a priced default here spends a
    # SHARED key's money on any run that forgets to pass --model. It did: ~$10
    # went on anthropic/claude-opus-4-5 for routine dogfooding that the free
    # jlu/* models then did better (ANECDOTES #63), and this line is why. The
    # earlier default was a record of one pre-registered experiment, graded by
    # hand, and a record of a past decision is not a standing permission --
    # CLAUDE.md says so, and saying so was not enough. Passing a priced model
    # explicitly still works, and still needs the operator's consent BEFORE the
    # run. tests/smoke/priced_model_lint.sh keeps this honest.
    r.add_argument("--model", default="jlu/qwen3-coder-next")
    r.add_argument("--api-base", default="https://api.hrz.uni-giessen.de/v1")
    r.add_argument("--key-env", default="JC_DEV_KEY")
    r.add_argument("--pairs", type=int, default=3,
                   help="pairs per task (default 3; each pair is 2 runs)")
    r.add_argument("--only", default=None, help="substring of a task id")
    r.add_argument("--label", default=None)
    r.add_argument("--context-limit", type=int, default=0)
    r.add_argument("--price-in", type=float, default=0.0,
                   help="$/1M input tokens, so cost means something")
    r.add_argument("--price-out", type=float, default=0.0)
    r.add_argument("--budget-tokens", type=int, default=0,
                   help="0 (default) = OFF. A token budget is a CAP, and a cap "
                        "that fires makes the run measure the cap; "
                        "--max-tool-calls is the fence and bounds the spend "
                        "anyway. The old default of 500000 was calibrated at "
                        "~14k/call on Opus 4.5 and truncated 3 of 8 runs on a "
                        "smaller model. Pass a value only to bound a run "
                        "deliberately.")
    r.add_argument("--deadline", default="15m")
    # M546: 45, from measurement. service-01 (defaults) completed in 17-23 calls
    # and every truncated run wanted 26+, one reaching exactly the old fence of 30.
    # service-02 then completed runs at 25, 27, 28, 30 and 38 calls -- so 30 would
    # have discarded the 38 as well. 45 leaves room for the model to finish and
    # still bounds the run; a run that hits it gave up rather than being cut off.
    r.add_argument("--max-tool-calls", type=int, default=45)
    r.add_argument("--timeout", type=int, default=900)
    r.set_defaults(fn=cmd_run)

    b = sub.add_parser("blind", help="build the blinded grading pack")
    b.add_argument("--label", required=True)
    b.add_argument("--seed", type=int, default=20260807)
    b.add_argument("--force", action="store_true",
                   help="re-blind over a part-filled form, "
                        "discarding those answers")
    b.set_defaults(fn=cmd_blind)

    s = sub.add_parser("score", help="join your filled form with the mapping")
    s.add_argument("--label", required=True)
    s.set_defaults(fn=cmd_score)

    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
