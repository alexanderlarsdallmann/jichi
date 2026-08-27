#!/usr/bin/env python3
"""Language-version probe (docs/CHOOSING_A_MODEL.md section 4, automated).

Runs the M373 version-probe ritual against a live OpenAI-compatible endpoint
and prints a MODEL_KNOWLEDGE-style record block for the operator to date,
review, and paste into the project documentation. It automates the two
mechanical steps -- the self-report question and the behaviour probe whose
arbiter is the locally installed compiler -- and deliberately NOT the third:
writing the record into a project's docs is the operator's dated claim, so the
block goes to stdout, never to a file (the learn-apply propose-only rule).

Probes are sent CONTEXT-FREE: a bare user message, no system prompt, no tools --
so the probe measures the model, not a project's context, and costs the minimum.
(The incident that prompted this rule was in fact a jichi CLI bug -- `-p`
swallowing a flag as the prompt, fixed in M375 -- not context pressure; the
context-free rule stays because it isolates the variable. ANECDOTES #50.)

The probe can FAIL a model, never certify one: a toy program compiling is weak
evidence, and the suggested verdict says so. The compile gate itself is proven
two-sided offline by `--mode self-test` (no model needed): a canned old-dialect
source must be refused and a canned current-idiom source must compile, for
every language whose toolchain is present. A gate never seen failing has never
been seen working.

Usage:
  python3 tests/bench/version_probe.py --url http://127.0.0.1:1234/v1/chat/completions \
      --model MODEL_ID [--key-env JLU_API_KEY] [--lang zig] [--lang c89] [--trials 3]
  python3 tests/bench/version_probe.py --mode self-test

Compiler discovery: `zig` / `cc` on $PATH, overridable via $VP_ZIG / $VP_CC.
Python stdlib only.
"""
import argparse
import datetime
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request

CALL_TIMEOUT = 300
COMPILE_TIMEOUT = 120
MAX_TOKENS = 768

SELF_REPORT = (
    "Answer from your training data only, without guessing: what is the newest "
    "released version of {lang} you have reliable knowledge of? Name two "
    "standard-library APIs that changed in releases after that version, if you "
    "know of any.")

# Per-language corpus. Behaviour prompts are chosen to touch the APIs that
# actually moved (for Zig: the managed->unmanaged ArrayList break and the
# 0.15 Io rewrite are the measured discriminators). c89 is the CONTROL: it has
# not changed since 1990, so a model failing it indicts the probe, not the
# model -- run it when a result looks suspicious.
LANGS = {
    "zig": {
        "display": "Zig",
        "bin_env": "VP_ZIG",
        "bin_default": "zig",
        "version_argv": ["version"],
        "behaviour": [
            ("arraylist", "probe_arraylist.zig",
             "Write a minimal Zig program that appends three integers to a "
             "std.ArrayList and prints them. Output only code, no explanation, "
             "no markdown fences."),
            ("stdout", "probe_stdout.zig",
             "Write a minimal Zig program that prints the numbers 1 to 5, one "
             "per line, to standard output. Output only code, no explanation, "
             "no markdown fences."),
        ],
    },
    "c89": {
        "display": "C89",
        "bin_env": "VP_CC",
        "bin_default": "cc",
        "version_argv": ["--version"],
        "behaviour": [
            ("loop", "probe_loop.c",
             "Write a minimal C89 (ANSI C) program that prints the numbers 1 "
             "to 5, one per line. Output only code, no explanation, no "
             "markdown fences."),
        ],
    },
}


def compile_argv(lang, binpath, src, outdir):
    if lang == "zig":
        return [binpath, "build-exe", src]
    return [binpath, "-std=c89", "-pedantic", "-o",
            os.path.join(outdir, "a.out"), src]


def toolchain_bin(lang):
    spec = LANGS[lang]
    binpath = os.environ.get(spec["bin_env"], spec["bin_default"])
    return binpath if shutil.which(binpath) else None


def toolchain_version(lang, binpath):
    try:
        out = subprocess.run([binpath] + LANGS[lang]["version_argv"],
                             capture_output=True, text=True, timeout=30)
        return (out.stdout or out.stderr).strip().splitlines()[0]
    except Exception as e:  # noqa: BLE001 - reported, not hidden
        return "unknown (%s)" % e


def strip_fences(text):
    """Extract the first fenced block if the reply is fenced despite the
    prompt; drop a leading language tag line; else return the text as-is."""
    t = text.strip()
    if "```" not in t:
        return t
    parts = t.split("```")
    if len(parts) < 3:
        return t.replace("```", "").strip()
    body = parts[1]
    lines = body.splitlines()
    if lines and lines[0].strip().isalpha() and len(lines[0].strip()) <= 12:
        lines = lines[1:]
    return "\n".join(lines).strip()


def chat(url, model, key, prompt):
    body = {"model": model, "max_tokens": MAX_TOKENS, "temperature": 0,
            "messages": [{"role": "user", "content": prompt}]}
    headers = {"Content-Type": "application/json"}
    if key:
        headers["Authorization"] = "Bearer " + key
    req = urllib.request.Request(url, data=json.dumps(body).encode(),
                                 headers=headers, method="POST")
    with urllib.request.urlopen(req, timeout=CALL_TIMEOUT) as r:
        o = json.loads(r.read().decode("utf-8", "replace"))
    return o["choices"][0]["message"]["content"]


def try_compile(lang, binpath, source, fname):
    """-> (ok, first_error_line). Compiles in a throwaway dir so artifacts
    never land in the tree."""
    d = tempfile.mkdtemp(prefix="vprobe-")
    try:
        src = os.path.join(d, fname)
        with open(src, "w") as f:
            f.write(source if source.endswith("\n") else source + "\n")
        p = subprocess.run(compile_argv(lang, binpath, src, d),
                           capture_output=True, text=True, cwd=d,
                           timeout=COMPILE_TIMEOUT)
        if p.returncode == 0:
            return True, ""
        err = (p.stderr or p.stdout).strip().splitlines()
        return False, err[0] if err else "exit %d" % p.returncode
    finally:
        shutil.rmtree(d, ignore_errors=True)


def probe_language(args, lang, key):
    spec = LANGS[lang]
    binpath = toolchain_bin(lang)
    if not binpath:
        print("SKIP %s: no `%s` on PATH (set $%s)" %
              (lang, spec["bin_default"], spec["bin_env"]), file=sys.stderr)
        return None
    tver = toolchain_version(lang, binpath)
    report = chat(args.url, args.model, key, SELF_REPORT.format(lang=spec["display"]))
    rows = []
    for slug, fname, prompt in spec["behaviour"]:
        for trial in range(args.trials):
            code = strip_fences(chat(args.url, args.model, key, prompt))
            ok, err = try_compile(lang, binpath, code, fname)
            rows.append((slug, trial + 1, ok, err))
            print("  %-10s trial %d: %s%s" %
                  (slug, trial + 1, "compiles" if ok else "REFUSED",
                   "" if ok else " -- " + err[:100]), file=sys.stderr)
    return {"lang": spec["display"], "toolchain": tver,
            "self_report": report.strip(), "rows": rows}


def render_record(model, results):
    today = datetime.date.today().isoformat()
    out = []
    for r in results:
        npass = sum(1 for _, _, ok, _ in r["rows"] if ok)
        total = len(r["rows"])
        out.append("### Model knowledge record -- %s, probed %s" % (model, today))
        out.append("")
        out.append("- **language / arbiter:** %s -- `%s` (the locally installed"
                   " toolchain judged the probes)" % (r["lang"], r["toolchain"]))
        out.append("- **self-reported newest:** (verbatim; evidence, not ground"
                   " truth)")
        out.append("")
        for line in r["self_report"].splitlines():
            out.append("  > " + line)
        out.append("")
        out.append("- **behaviour probes:** %d/%d compiled" % (npass, total))
        for slug, trial, ok, err in r["rows"]:
            out.append("  - `%s` trial %d: %s%s" %
                       (slug, trial, "compiles" if ok else "compile-refused",
                        "" if ok else " (`%s`)" % err[:120]))
        if npass < total:
            out.append("- **suggested verdict: DOES NOT MEET** the installed"
                       " toolchain version -- dialect evidence above. Mitigate"
                       " (delta table, readable toolchain source, verify gate)"
                       " or choose another model.")
        else:
            out.append("- **suggested verdict:** not refuted by this probe"
                       " (%d/%d toy programs compiled) -- WEAK positive"
                       " evidence; a toy compiling certifies nothing. Confirm"
                       " on real tasks before recording MEETS." % (npass, total))
        out.append("")
    return "\n".join(out)


# --- self-test: prove the compile gate two-sided, offline --------------------
# The bad fixtures are the measured 0.11 dialect (gemma-4-26b-it's actual
# output shape, 2026-08-11) and a C syntax error; the good fixtures are
# current idiom, themselves compile-verified before being trusted here.

ZIG_BAD = """\
const std = @import("std");
pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();
    var list = std.ArrayList(i32).init(allocator);
    defer list.deinit();
    try list.append(10);
    const stdout = std.io.getStdOut().writer();
    try stdout.print("{any}\\n", .{list.items});
}
"""

ZIG_GOOD = """\
const std = @import("std");
pub fn main() !void {
    var gpa = std.heap.DebugAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();
    var list: std.ArrayList(i32) = .empty;
    defer list.deinit(allocator);
    try list.append(allocator, 10);
    try list.append(allocator, 20);
    try list.append(allocator, 30);
    std.debug.print("{any}\\n", .{list.items});
}
"""

C_BAD = "int main(void) { int x = ; return 0; }\n"

C_GOOD = """\
#include <stdio.h>
int main(void)
{
    int i;
    for (i = 1; i <= 5; i++) {
        printf("%d\\n", i);
    }
    return 0;
}
"""

FENCE_CASES = [
    ("plain code stays as-is", "int x;", "int x;"),
    ("fenced block extracted, tag dropped",
     "Here you go:\n```zig\nconst x = 1;\n```\nEnjoy!", "const x = 1;"),
    ("bare fences stripped", "```\nint y;\n```", "int y;"),
]


def self_test():
    failed = 0
    n = 0
    for name, raw, want in FENCE_CASES:
        n += 1
        got = strip_fences(raw)
        if got == want:
            print("ok %d - fence: %s" % (n, name))
        else:
            failed += 1
            print("FAIL %d - fence: %s (got %r)" % (n, name, got))
    for lang, bad, good in (("zig", ZIG_BAD, ZIG_GOOD), ("c89", C_BAD, C_GOOD)):
        binpath = toolchain_bin(lang)
        _, fname, _ = LANGS[lang]["behaviour"][0]
        if not binpath:
            print("skip - %s gate untested: no toolchain (set $%s)"
                  % (lang, LANGS[lang]["bin_env"]))
            continue
        for label, src, want_ok in (("refuses the old dialect", bad, False),
                                    ("accepts current idiom", good, True)):
            n += 1
            ok, err = try_compile(lang, binpath, src, fname)
            if ok == want_ok:
                print("ok %d - %s gate %s" % (n, lang, label))
            else:
                failed += 1
                print("FAIL %d - %s gate %s (ok=%s err=%s)"
                      % (n, lang, label, ok, err[:100]))
    print("# self-test: %d checks, %d failed" % (n, failed))
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--mode", choices=["probe", "self-test"], default="probe")
    ap.add_argument("--url", help="chat/completions endpoint URL")
    ap.add_argument("--model", help="model id to probe")
    ap.add_argument("--key-env", default="",
                    help="env var holding the API key (omit for keyless)")
    ap.add_argument("--lang", action="append", choices=sorted(LANGS),
                    help="language(s) to probe (default: zig)")
    ap.add_argument("--trials", type=int, default=3,
                    help="behaviour-probe repetitions per prompt (default 3)")
    args = ap.parse_args()

    if args.mode == "self-test":
        sys.exit(self_test())

    if not args.url or not args.model:
        ap.error("--url and --model are required in probe mode")
    key = os.environ.get(args.key_env, "") if args.key_env else ""
    if args.key_env and not key:
        print("error: $%s is empty" % args.key_env, file=sys.stderr)
        sys.exit(1)

    results = []
    for lang in (args.lang or ["zig"]):
        print("probing %s (%s)..." % (args.model, lang), file=sys.stderr)
        r = probe_language(args, lang, key)
        if r:
            results.append(r)
    if not results:
        print("error: every requested toolchain was missing", file=sys.stderr)
        sys.exit(1)
    print(render_record(args.model, results))


if __name__ == "__main__":
    main()
