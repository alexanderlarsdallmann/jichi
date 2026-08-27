#!/usr/bin/env python3
"""M201: lint the test rig itself.

The harness is code, and it had a bug that mattered more than most product bugs:
**twelve** drivers each carried a private copy of a request reader whose body loop
broke on the first socket timeout, silently returning a TRUNCATED request
(ANECDOTES #18). That does not surface as a timeout. It surfaces as a *wrong
answer*, because the mock then evaluates `marker in req` against a partial body
and picks the wrong canned reply -- so the driver reports a product regression that
never happened. Both drivers observed flaking inside a full suite run
(`prose_nudge`, `constraints_scope`) carried it, and both of their failure
messages had exactly that signature.

`_e2e.recv_http_request` / `recv_http_head_body` keep reading until the declared
Content-Length is satisfied, tolerating per-recv timeouts up to a wall-clock
deadline. Every driver must use them rather than rolling its own.

Checks:
  1. No driver defines its own HTTP request reader.
  2. No driver breaks out of a Content-Length body loop (the truncation bug in
     any spelling).
  3. Every AF_INET listening socket sets SO_REUSEADDR, so a re-run cannot fail to
     bind while the previous port lingers in TIME_WAIT.
  4. Every driver can actually fail (calls fail()/sys.exit(1)) -- a test that can
     only print ok() is decoration.
  5. No ORPHANS: every driver file is named in run.sh (M213). The port waves
     delete a driver's run.sh entry together with the file; M212 dropped
     autocontext from the list but left autocontext.py behind, so it silently
     stopped running -- a coverage loss the one-driver-one-tier check cannot
     see (it only catches duplicates). Helper modules imported by drivers
     (mcp.py's MOCK) are exempted by name below.
  6. HARNESS PARITY: no driver names the binary literally -- _e2e.BIN only, so
     the test exercises the jichi that was just built rather than whatever the
     PATH resolves (failure mode 7 in docs/TEST_INTEGRITY.md).

Offline; no binary needed. See docs/analysis/2026-07-29-tool-arena.md.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _e2e import fail, ok  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
# _e2e.py is the ONE place a reader may live; rig_lint does not test itself.
EXEMPT = {"_e2e.py", "rig_lint.py"}

READER_DEF = re.compile(r"^def\s+(\w*(?:recv|read)_\w*req\w*|recv_request)\s*\(",
                        re.I)
BODY_LOOP = re.compile(r"while\s+len\(\s*(body|buf)\s*\)\s*<\s*clen")
INET_SOCKET = re.compile(r"socket\.socket\(\s*socket\.AF_INET")
CAN_FAIL = re.compile(r"_e2e\.fail\(|(?<![\w.])fail\(|sys\.exit\(1\)|"
                      r"raise\s+SystemExit")
# A whole quoted token that IS the binary name: "jichi", './jichi',
# "jichi-convert". Prose and flags never match.
LITERAL_BIN = re.compile(r"""["'](?:\./)?jichi(?:-convert)?["']""")

problems = []
checked = 0

for name in sorted(os.listdir(HERE)):
    if not name.endswith(".py") or name in EXEMPT:
        continue
    path = os.path.join(HERE, name)
    src = open(path).read()
    lines = src.split("\n")
    checked += 1

    # (1) a private reader
    for i, line in enumerate(lines, 1):
        if READER_DEF.match(line):
            # Delegating wrappers are fine: the body must call the shared helper.
            tail = "\n".join(lines[i:i + 25])
            if "_e2e.recv_http" not in tail:
                problems.append(
                    "%s:%d: defines its own HTTP request reader -- use "
                    "_e2e.recv_http_request / recv_http_head_body" % (name, i))

    # (2) the truncation bug in any spelling: a body loop that can break early
    for i, line in enumerate(lines, 1):
        if BODY_LOOP.search(line):
            window = "\n".join(lines[i:i + 8])
            if re.search(r"^\s+break\b", window, re.M):
                problems.append(
                    "%s:%d: Content-Length body loop breaks on timeout -- this "
                    "TRUNCATES the request and shows up as a wrong answer, not "
                    "a timeout (ANECDOTES #18)" % (name, i))

    # (3) SO_REUSEADDR on any INET listener
    if INET_SOCKET.search(src) and "bind(" in src and \
            "SO_REUSEADDR" not in src:
        problems.append(
            "%s: binds an AF_INET socket without SO_REUSEADDR -- a re-run can "
            "fail to bind while the port lingers in TIME_WAIT" % name)

    # (4) a test that cannot fail
    if not CAN_FAIL.search(src):
        problems.append(
            "%s: never calls fail()/sys.exit(1) -- it cannot report a failure, "
            "so it is decoration rather than a test" % name)

    # (6) harness parity: the binary under test is the one that was BUILT.
    # A driver that spells the binary as a literal launches whatever `jichi` the
    # PATH happens to resolve -- an installed copy, a stale one -- so a green run
    # says nothing about the tree. _e2e.BIN (from JC_E2E_BIN) is the only
    # sanctioned spelling. Failure mode 7 in docs/TEST_INTEGRITY.md, as an
    # invariant rather than a habit. Matches only a whole quoted token, so prose
    # ("Welcome to jichi") and flags ("--jichi") are untouched.
    for i, line in enumerate(lines, 1):
        if LITERAL_BIN.search(line):
            problems.append(
                "%s:%d: names the binary literally -- use _e2e.BIN so the test "
                "runs the BUILT jichi, not a PATH-resolved one" % (name, i))

if not checked:
    fail("rig lint scanned no drivers -- wrong directory?")

# (5) orphans: every driver must be named in run.sh, or it never runs.
with open(os.path.join(HERE, "run.sh"), "r", encoding="utf-8") as f:
    runsh = f.read()
for name in sorted(os.listdir(HERE)):
    if not name.endswith(".py") or name in EXEMPT:
        continue
    stem = name[:-3]
    if not re.search(r"\b%s\b" % re.escape(stem), runsh):
        problems.append(
            "%s: not named in run.sh -- an ORPHAN that never runs. Port it, "
            "delete it, or wire it in (M213)" % name)

if problems:
    fail("test-rig defects (%d):\n  %s" % (len(problems), "\n  ".join(problems)))

ok("test rig clean across %d driver(s): no private request readers, no "
   "truncating body loops, SO_REUSEADDR on every listener, every driver can "
   "fail, no literal binary names, no orphans" % checked)
