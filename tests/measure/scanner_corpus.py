#!/usr/bin/env python3
"""What jichi's constraint scanner infers from the prompts it has actually been
given (M743). A measurement, never a CI gate: the verdicts are read by hand.

The scanner (src/chat/jc_constraint.c) runs on every user message of an `--auto`
turn, so its population is the session store: every DISTINCT user-role message
in ~/.jichi.d/sessions/**/*.json -- jichi's own format and the Continue format
({"message": {...}} per turn) the store inherited -- plus any brief files named
with --extra. Each
is scanned by `jichi brief-check`, which calls the scanner and no model, in an
empty HOME so no configuration is read.

    scanner_corpus.py BIN [--against OLD_BIN] [--extra GLOB ...] [--store DIR]
                      [--keep DIR] [--first-only] [--top-only]

Prints one line per inferred rule -- message id, where it came from, the rule,
and the line of the message brief-check names -- then the totals per rule.
With --against, prints only what changed between OLD_BIN and BIN: the unit of a
scanner change is the set of messages whose verdict moved, and each of those is
read, not counted. --first-only restricts the store to each session's first user
message, and --top-only to the store's own directory (not the older layout
nested under it): together they are the population M730 measured. --keep
writes the corpus (one
file per message) to DIR for re-reading; it holds the operator's prompts, so it
is never written into the repository.

M730 measured the workstation's store, 289 first messages: 9 rules, 6 wrong.
M743 measured threadwork's, 656 distinct messages (every turn, three session
formats) and the two zigodot drives' tasks: 73 rules, 23 wrong, and 16 read-only
tasks that inferred nothing -- see docs/analysis/2026-09-25-the-scanner-read-again.md."""
import argparse
import collections
import glob
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile


def text_of(content):
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        return "\n".join(p.get("text", "") for p in content
                         if isinstance(p, dict) and isinstance(p.get("text"), str))
    return ""


def build_corpus(store, extras, first_only, top_only):
    """-> list of (source, text), one per distinct text, in a stable order."""
    seen, out = set(), []

    def add(text, src):
        if not text.strip():
            return
        h = hashlib.sha1(text.encode("utf-8", "replace")).hexdigest()
        if h in seen:
            return
        seen.add(h)
        out.append((src, text))

    nsess = 0
    pattern = os.path.join(store, "*.json") if top_only else \
        os.path.join(store, "**", "*.json")
    for p in sorted(glob.glob(pattern, recursive=not top_only)):
        try:
            with open(p, encoding="utf-8", errors="replace") as f:
                s = json.load(f)
        except ValueError:
            continue
        if not isinstance(s, dict):
            continue
        nsess += 1
        for m in s.get("history") or []:
            # A Continue-format session (the IDE extension's store, which jichi
            # can resume) wraps each turn as {"message": {"role", "content"}}.
            if isinstance(m, dict) and isinstance(m.get("message"), dict):
                m = m["message"]
            if isinstance(m, dict) and m.get("role") == "user":
                add(text_of(m.get("content")),
                    "session:" + os.path.basename(p)[:8])
                if first_only:
                    break
    for g in extras:
        for p in sorted(glob.glob(os.path.expanduser(g))):
            with open(p, encoding="utf-8", errors="replace") as f:
                add(f.read(), "file:" + p.replace(os.path.expanduser("~"), "~", 1))
    return nsess, out


RULE = re.compile(r"^  \d+\. (.*)$")
FROM = re.compile(r"^\s+(from line (\d+)|\(no single line)")


def scan(binary, path, home):
    """-> [(rule text, line number or 0)] as brief-check reports them."""
    env = dict(os.environ, HOME=home)
    r = subprocess.run([binary, "brief-check", path], stdin=subprocess.DEVNULL,
                       capture_output=True, text=True, env=env)
    rules, pending = [], None
    for line in r.stdout.split("\n"):
        m = RULE.match(line)
        if m:
            pending = m.group(1)
            continue
        m = FROM.match(line)
        if m and pending is not None:
            rules.append((pending, int(m.group(2)) if m.group(2) else 0))
            pending = None
    return rules


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("bin")
    ap.add_argument("--against", metavar="OLD_BIN")
    ap.add_argument("--extra", action="append", default=[], metavar="GLOB")
    ap.add_argument("--store", default=os.path.expanduser("~/.jichi.d/sessions"))
    ap.add_argument("--keep", metavar="DIR")
    ap.add_argument("--first-only", action="store_true")
    ap.add_argument("--top-only", action="store_true")
    a = ap.parse_args()

    nsess, corpus = build_corpus(a.store, a.extra, a.first_only, a.top_only)
    work = a.keep or tempfile.mkdtemp(prefix="jichi_scanner_corpus.")
    home = os.path.join(work, ".home")
    os.makedirs(home, exist_ok=True)
    paths = []
    for i, (src, text) in enumerate(corpus):
        p = os.path.join(work, "msg-%04d.txt" % i)
        with open(p, "w", encoding="utf-8") as f:
            f.write(text)
        paths.append(p)
    print("== %d session files, %d distinct messages%s" %
          (nsess, len(corpus), " (first user message only)" if a.first_only else ""))

    def line_of(i, n):
        if n <= 0:
            return "(spans lines)"
        lines = corpus[i][1].split("\n")
        return " ".join(lines[n - 1].split())[:160] if n <= len(lines) else "?"

    new = [scan(a.bin, p, home) for p in paths]
    if a.against:
        old = [scan(a.against, p, home) for p in paths]
        moved = 0
        for i in range(len(corpus)):
            o = collections.Counter(r for r, _ in old[i])
            n = collections.Counter(r for r, _ in new[i])
            if o == n:
                continue
            moved += 1
            for r in sorted((o - n).elements()):
                ln = next(l for rr, l in old[i] if rr == r)
                print("- %04d %-24s %s\n       %s" % (i, corpus[i][0][:24], r, line_of(i, ln)))
            for r in sorted((n - o).elements()):
                ln = next(l for rr, l in new[i] if rr == r)
                print("+ %04d %-24s %s\n       %s" % (i, corpus[i][0][:24], r, line_of(i, ln)))
        print("== %d messages moved; rules %d -> %d" %
              (moved, sum(len(x) for x in old), sum(len(x) for x in new)))
    else:
        tally = collections.Counter()
        for i, rules in enumerate(new):
            for r, ln in rules:
                tally[r] += 1
                print("%04d %-24s %s\n       %s" % (i, corpus[i][0][:24], r, line_of(i, ln)))
        print("== %d rules in %d messages" %
              (sum(tally.values()), sum(1 for x in new if x)))
        for r, c in tally.most_common():
            print("%5d  %s" % (c, r))
    if not a.keep:
        for p in paths:
            os.remove(p)
        os.rmdir(home)
        os.rmdir(work)
    return 0


if __name__ == "__main__":
    sys.exit(main())
