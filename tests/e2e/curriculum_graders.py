"""Curriculum sets A, B, and C: every grader is two-sided, proven THROUGH `jichi grade`
(never an ad-hoc re-implementation of the spec parser -- docs/ANECDOTES.md
#20). For each spec under docs/assignments/: the pristine fixtures must FAIL
(exit 1) and a reference solution must PASS (exit 0). Two half-solutions (a
fix without the demanded test, a fix without the debugging record) must still
fail, or those compound graders are hollow.

Needs a C compiler (cc) for tasks 06-08, like the project build itself.
"""
import os, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _e2e

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "docs", "assignments")

# M511: give the Zig tasks a PRIVATE build cache. Zig's default cache is shared
# (~/.cache/zig), and running a zig task's test.sh by hand -- with a different
# rpn.zig in the tree, which is exactly what comparing a grader before and after
# an edit does -- leaves an artifact this suite then hits instead of compiling
# the reference solution. Measured: identical sources, `58-zig-capstone` FAILS
# with the shared cache ("rpnEval does not pass the suite") and PASSES with a
# fresh one. Cost half an hour of believing a grader fix had broken a capstone.
# A gate whose verdict depends on what someone ran yesterday is not a gate.
_ZIG_CACHE = tempfile.mkdtemp(prefix="jc-zigcache-")
os.environ["ZIG_GLOBAL_CACHE_DIR"] = os.path.join(_ZIG_CACHE, "g")
os.environ["ZIG_LOCAL_CACHE_DIR"] = os.path.join(_ZIG_CACHE, "l")

ws = None


def fresh():
    global ws
    if ws:
        shutil.rmtree(ws, ignore_errors=True)
    ws = tempfile.mkdtemp(prefix="jc-curr-")
    shutil.copytree(SRC, os.path.join(ws, "docs", "assignments"))
    return ws


def path(rel):
    return os.path.join(ws, "docs", "assignments", rel)


# One `jichi grade` averages ~0.6s (194 of them run cold in ~119s on the
# reference box), but the per-call budget was 60s -- close enough to be reached
# by a transient stall under the load of a full `make ci`, which is exactly how
# it failed: one JVM-backed task timed out (exit 124) mid-suite and passed on
# the immediate retry. A budget this far above the real cost should only ever be
# hit by a true hang, so raise it out of transient range rather than leave the
# gate luck-dependent (M256). JC_E2E_TIMEOUT_MULT scales it for slow hardware,
# matching the convention the driver limits in run.sh already use.
GRADE_TIMEOUT = 180 * int(os.environ.get("JC_E2E_TIMEOUT_MULT", "1"))


def grade(spec, want, label):
    rc, out, err = _e2e.run(["grade", "docs/assignments/" + spec], cwd=ws,
                            timeout=GRADE_TIMEOUT)
    if rc != want:
        _e2e.fail("%s %s: wanted exit %d, got %d\n%s%s"
                  % (spec, label, want, rc, out, err))
    print("ok - %s %s" % (spec, label))


def edit(rel, old, new):
    p = path(rel)
    src = open(p).read()
    if old not in src:
        _e2e.fail("fixture drift: %r not in %s" % (old, rel))
    open(p, "w").write(src.replace(old, new, 1))


def write(rel, text):
    p = path(rel)
    d = os.path.dirname(p)
    if not os.path.isdir(d):
        os.makedirs(d)
    open(p, "w").write(text)


TEST_CLAMP = """\
#include <stdio.h>
#include "clamp.h"
static int failures = 0;
static void check(int n, const char *name, int got, int want)
{
    if (got == want) { printf("ok %d - %s\\n", n, name); }
    else { printf("not ok %d - %s\\n", n, name); failures++; }
}
int main(void)
{
    printf("1..3\\n");
    check(1, "above hi clamps to hi", clamp(9, 0, 5), 5);
    check(2, "below lo clamps to lo", clamp(-3, 0, 5), 0);
    check(3, "in range passes through", clamp(2, 0, 5), 2);
    return failures == 0 ? 0 : 1;
}
"""

NOTES = """\
## Symptom
total: 8 for "3,5,8"; expected 16.

## Dead ends
Suspected total() per the comment; probed it in isolation -- innocent.

## Root cause
split_fields never flushes the final field.

## Lesson
Comments are folklore; test each stage in isolation.
"""


def fix_07():
    # Only the x > hi branch is wrong; the x < lo one also says `return lo`.
    p = path("07-write-the-test-first/clamp.c")
    src = open(p).read()
    i = src.index("x > hi")
    j = src.index("return lo;", i)
    open(p, "w").write(src[:j] + "return hi;" + src[j + len("return lo;"):])


def fix_08():
    edit("08-the-wrong-suspect/fields.c", "    return n;",
         "    if (line[0] != '\\0' && n < max) {\n"
         "        out[n++] = cur;\n"
         "    }\n"
         "    return n;")


# --- set B (Stage 2, M176) -------------------------------------------------

CHECK_09 = """\
#!/bin/sh
cand="$1"
dir="$(dirname "$0")"
cat > "$dir/_probe.c" <<'PROBE'
#include <stdio.h>
#include "median3.h"
static int failures = 0;
static void check(int a, int b, int c, int want)
{
    if (median3(a, b, c) != want) { failures++; }
}
int main(void)
{
    check(1, 2, 3, 2); check(1, 3, 2, 2); check(2, 1, 3, 2);
    check(2, 3, 1, 2); check(3, 1, 2, 2); check(3, 2, 1, 2);
    check(2, 2, 5, 2); check(5, 2, 2, 2); check(7, 7, 7, 7);
    check(-1, 0, 1, 0); check(-5, -2, -9, -5);
    return failures == 0 ? 0 : 1;
}
PROBE
cc -std=c89 -I"$dir" -o "$dir/_probe" "$cand" "$dir/_probe.c" || exit 1
"$dir/_probe"
"""

# A lazy checker: happy path only. Accepts all four candidates, so the
# meta-grader must REJECT it -- that asymmetry is the whole assignment.
CHECK_09_LAZY = CHECK_09.replace(
    "    check(1, 2, 3, 2); check(1, 3, 2, 2); check(2, 1, 3, 2);\n"
    "    check(2, 3, 1, 2); check(3, 1, 2, 2); check(3, 2, 1, 2);\n"
    "    check(2, 2, 5, 2); check(5, 2, 2, 2); check(7, 7, 7, 7);\n"
    "    check(-1, 0, 1, 0); check(-5, -2, -9, -5);\n",
    "    check(1, 2, 3, 2);\n")

DESIGN_10 = """\
# linelog -- design

## Problem

A project team wants a shared, chronological log of one-line notes kept in
the repository: decisions, observations, and reminders that are too small
for a document but too important to lose in chat. Appending must be one
command; finding a note weeks later must be one command.

## Requirements

- `linelog add <text>` appends one timestamped line; never edits history.
- `linelog find <word>` prints matching lines, oldest first, with dates.
- Storage is one plain-text file in the repository, mergeable by git.
- Concurrent appends from two checkouts resolve as a trivial git merge.
- Non-goals: no encryption, no per-user identity beyond git's, no editing
  or deleting past notes (git history is the audit trail).

## Design

One file, `NOTES.log`, one record per line:

```
2026-07-28T14:02:11Z  switched the parser to a two-pass design
2026-07-29T09:15:40Z  flaky test traced to PATH ordering
```

`add` opens the file in append mode and writes `<ISO-8601 UTC>  <text>`;
`find` is a case-insensitive substring scan printing matches in file order.
Failure behaviour: a missing file is created by `add` and treated as empty
by `find`; an unwritable file is a plain error with the path in it.

## Alternatives considered

- One file per note (no merge conflicts at all) -- rejected: thousands of
  tiny files make `find` slow and the directory unreadable in listings.
- SQLite storage -- rejected: binary blobs do not merge in git and need a
  runtime dependency; the whole tool is otherwise POSIX shell + text.
- JSON lines -- rejected: quoting one-line prose in JSON buys nothing here
  and makes the file hostile to grep and to humans.

## Test plan

- `add` then `find` round-trips a note verbatim.
- Two `add`s preserve order; `find` prints oldest first.
- `find` on a fresh checkout (no log file) prints nothing, exit 0.
- A note containing the search word in different case is still found.
- An unwritable log directory makes `add` fail with a nonzero exit.
"""

REVIEW_11 = """\
## Smell 1: duplicated validation block

where: smelly.c:14 and smelly.c:29

The same three guards appear in both predicates.
why it matters: the next rule gets added in one copy and not the other;
the copies have already drifted (an idle check inside the lifetime
function).

## Smell 2: magic numbers

where: smelly.c:23 and smelly.c:38

why it matters: 1800 and 28800 are policy with no name and no unit; the
next policy change requires archaeology.

## Smell 3: dead code

where: smelly.c:41

why it matters: session_expired_v1 has no caller, contradicts the live
cutoffs, and misleads every future reader; git already remembers it.
"""

DUR_REFACTORED = """\
/* dur.c - see dur.h. Refactored: one constant, one guard. */
#include "dur.h"

#define SECONDS_PER_DAY 86400

static int valid_hms(int h, int m, int s)
{
    if (h < 0 || h > 23) {
        return 0;
    }
    if (m < 0 || m > 59) {
        return 0;
    }
    if (s < 0 || s > 59) {
        return 0;
    }
    return 1;
}

int hms_to_seconds(int h, int m, int s)
{
    if (!valid_hms(h, m, s)) {
        return -1;
    }
    return h * 3600 + m * 60 + s;
}

int seconds_remaining(int h, int m, int s)
{
    if (!valid_hms(h, m, s)) {
        return -1;
    }
    return SECONDS_PER_DAY - (h * 3600 + m * 60 + s);
}

int days_to_seconds(int d)
{
    if (d < 0) {
        return -1;
    }
    return d * SECONDS_PER_DAY;
}
"""

# A synthetic journal in the exact compact shape jc_env_journal_end writes.
# This tests the GRADER (a real bounded run emits the same lines); the spec
# itself tells the learner why forging one defeats the purpose.
JOURNAL_13 = (
    '{"ts":1,"run":"r1","event":"start"}\n'
    '{"ts":2,"run":"r1","event":"tool_call","name":"write_file","ok":true}\n'
    '{"ts":3,"run":"r1","event":"verify","exit":0,"retries_left":3,'
    '"passed":1}\n'
    '{"ts":4,"run":"r1","event":"end","outcome":"ok","rolled_back":false,'
    '"tokens_used":1234,"tool_calls":2}\n')

JOURNAL_13_LEAKY = JOURNAL_13.replace(
    '{"ts":4,',
    '{"ts":3,"run":"r1","event":"out_of_scope","paths":["README.md"]}\n'
    '{"ts":4,')


REPORT_19 = """\
## Environment
zig 0.16.0, Linux x86-64 32 cores, jichi commit 26ffbe3.

## The claim, tested
`make CC="zig cc"` built and linked first try; `make CC="zig cc" test`
ran 8828 checks, 0 failures. Zero source or Makefile changes.

## Measurements
| | cc | zig cc | zig cc -O2 |
|---|---|---|---|
| cold build -j8 | 0.7 s | 14.3 s | 14 s |
| warm rebuild | - | 1.3 s | - |
| binary | 1.4 MB | 9.7 MB (4.6 stripped) | 3.6 MB |
| suite | green | green | green |

## The cross target
`make CC="zig cc -target x86_64-linux-musl"` fails at link: undefined
jc_http_perform and friends -- the symbols belong to src/net, excluded
because the curl probe cannot link a musl libcurl that does not exist on
this host. To link, libcurl must be cross-built for musl first; zig
carries the libc, not the dependency tree. Bonus finding: the no-curl
fallback itself has drifted (pure jc_sse lives in the gated wildcard).

## What zig cc actually is
A clang driver with bundled headers, bundled libcs, and a build cache --
the third compiler is secretly the second. What zig adds is hermeticity
and -target; worth using for pinned toolchains and cross builds with
cross-built deps, not for speed on a hot host.
"""

REPORT_18 = """\
## Environment
Cygwin 3.5, gcc 11, jichi commit abc1234, Windows 11 VM.

## Method
Ran make, captured full logs to build-01.log; fixed one-liners, re-ran,
stopped at the wall. Logs beside this report.

## Findings
| # | file:line | what failed | class |
|---|---|---|---|
| 1 | src/tui/jc_term.c:12 | termios.h differences in VMIN handling | semantic |
| 2 | src/util/jc_meminfo.c:70 | /proc/self/status absent at runtime | runtime |
| 3 | src/chat/jc_control.c:40 | AF_UNIX path length limit differs | semantic |
| 4 | src/util/jc_proc.c:300 | /proc/<pid>/stat absent | runtime |
| 5 | Makefile:35 | libcurl probe needs cygwin package name | missing-header |
| 6 | src/tools/jc_parallel.c:88 | killpg missing at link time | missing-symbol |

## The wall
The parallel pool: it forks N children that inherit the parent's live
in-memory state copy-on-write and pipe progress back. Win32 CreateProcess
starts a fresh image -- there is no fork, so inherited-state workers would
need an explicit serialize-and-respawn redesign (job objects for the group
kill semantics, handle inheritance for the pipes). That is a subsystem
rewrite, not a patch; every earlier finding above was one line or one
package.

## What WSL gives you
All six findings vanish: WSL is a real Linux kernel interface, so /proc,
termios, AF_UNIX and fork behave exactly as on the bench. The survey's
lesson is that WSL is not a compatibility layer but the actual platform.
"""


# --- set C (Stage 3, M177) -------------------------------------------------

GATE_14_FIXED = """\
#!/bin/sh
# CI gate for the rot13 library. The exit status IS the verdict.
cd "$(dirname "$0")" || exit 1
cc -std=c89 -o t rot13.c test_rot13.c 2> build.log || exit 1
./t
"""


def fix_14_code():
    edit("14-the-hollow-gate/rot13.c",
         "        return 'a' + (c - 'A' + 13) % 26;",
         "        return 'A' + (c - 'A' + 13) % 26;")


def fix_15_code():
    write("15-the-confident-misdiagnosis/wc_words.c", """\
/* wc_words.c - see wc_words.h. */
#include "wc_words.h"

int count_words(const char *s)
{
    int n = 0;
    int in_word = 0;
    int i;
    for (i = 0; s[i] != '\\0'; i++) {
        if (s[i] == ' ') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            n++;
        }
    }
    return n;
}
""")


def fake_patch_15():
    edit("15-the-confident-misdiagnosis/wc_words.c",
         "    return n;",
         "    if (i > 0 && s[i - 1] == ' ') {\n"
         "        n--;\n"
         "    }\n"
         "    return n;")


VERDICT_15 = """\
## Claim
The over-count is an off-by-one for trailing spaces; subtract one when the
line ends in a space.

## Evidence
count_words("") returns 1 and count_words("a  b") returns 3 on the shipped
code -- neither input ends in a space, so the claimed root cause cannot
explain them. The patch applied locally: test 2 goes green, tests 3-5 stay
red.

## Verdict
Rejected. The function counts separators plus one instead of tracking
whether it is inside a word; the patch hides one symptom of the wrong model.
"""

PEER_SPEC_16 = """\
---
title: Trim trailing whitespace from a file
audience: junior
points: 1
verify: "sh check.sh"
hints:
  - Look at the line ends with `cat -A notes.txt` -- what do you see there?
  - Ask the agent for the smallest edit that removes ONLY trailing blanks,
    then read the diff before approving it.
---
`notes.txt` has trailing spaces on some lines. Remove every trailing blank
without changing anything else. Practise this because invisible whitespace
churn once ruined a review of mine.

## Rubric

- The diff touches only line endings (good) vs. reflowed text (bad).
- You checked the result yourself before grading (tell me how).
"""

PEER_CHECK_16 = """\
#!/bin/sh
cd "$(dirname "$0")" || exit 1
! grep -q ' $' notes.txt
"""

PEER_SOLUTION_16 = """\
#!/bin/sh
cd "$(dirname "$0")" || exit 1
sed -i 's/ *$//' notes.txt
"""


def solve_16():
    write("16-teach-a-peer/task/spec.md", PEER_SPEC_16)
    write("16-teach-a-peer/task/notes.txt",
          "alpha \nbravo\ncharlie  \n")
    write("16-teach-a-peer/task/check.sh", PEER_CHECK_16)
    write("16-teach-a-peer/task/solution.sh", PEER_SOLUTION_16)


PROPOSAL_17 = """\
# Capstone proposal: relnote

## Goal

A `relnote` script for my notes repository: collect the `## Unreleased`
section of CHANGES.md into a dated release section and open the diff for
review. I retype this by hand for every tag; the capstone makes the ritual
a verified command.

## Scope and non-goals

- In: one POSIX shell script, a fixture repo for tests, a test runner.
- In: a bounded --auto run that drafts the awk section-splice under my
  verifier.
- Non-goal: no version-number inference; the tag is an argument.
- Non-goal: no git tagging or pushing -- the script edits the file only,
  because irreversible steps stay human.

## Envelope

--budget-tokens 300k (two focused runs' worth), --deadline 15m,
--edit-scope 'tools/**' and 'tests/**' (the script and its tests; CHANGES.md
itself stays out of scope -- runs prove themselves on fixtures), --journal
kept per run. Numbers sized from the Stage-2 delegation exercise.

## Verify strategy

tests/run.sh builds nothing (shell), runs the script against a fixture
CHANGES.md, and diffs the result against a golden file; a second case
asserts the failure path (missing Unreleased section exits nonzero with a
message). I have watched the gate red on both cases before wiring it into
the envelope, so the gate is two-sided; the golden diff makes a hollow pass
impossible unless the golden file itself is edited -- which the run's edit
scope forbids.

## Risks

The section splice will mangle an edge case (empty Unreleased, CRLF). Plan:
add the failing fixture first, roll back to green, shrink the step. If the
agent loops on awk quoting, take the wheel interactively -- delegation is a
choice per step, not an identity.
"""

RECORD_17 = """\
## Symptom
The golden-file test passed while the script printed a warning to stderr.

## Dead ends
Blamed the fixture; rewrote it twice.

## Root cause
The runner compared stdout only; 2>&1 was missing, so stderr never reached
the diff.

## Lesson
A gate checks exactly what you route into it -- nothing else.

## Symptom
The bounded run stopped at the deadline with the splice half-drafted.

## Dead ends
Raised the budget first, on reflex.

## Root cause
My prompt asked for the script AND its tests in one run; the scope was two
tasks wide.

## Lesson
One bounded run, one deliverable; the envelope was telling me my own plan
was too big.
"""


# --- set D (M221): memory & lifetimes ---------------------------------------
def solve_20():
    # The scratch-arena fix: request-scoped data on a request-scoped arena.
    edit("20-the-wrong-lifetime/notekeeper.c",
         "    struct arena *lifetime;   /* the keeper's own arena: lives to exit */",
         "    struct arena *lifetime;   /* the keeper's own arena: lives to exit */\n"
         "    struct arena *scratch;    /* request-scoped: reset per request */")
    edit("20-the-wrong-lifetime/notekeeper.c",
         "    char *copy = arena_strdup(k->lifetime, request);\n"
         "    long words = 0;\n"
         "    int in_word = 0;\n"
         "    char *p;\n"
         "\n"
         "    if (copy == NULL) {",
         "    char *copy;\n"
         "    long words = 0;\n"
         "    int in_word = 0;\n"
         "    char *p;\n"
         "\n"
         "    arena_reset(k->scratch);\n"
         "    copy = arena_strdup(k->scratch, request);\n"
         "    if (copy == NULL) {")
    edit("20-the-wrong-lifetime/notekeeper.c",
         "    k.lifetime = arena_new();\n    k.requests = 0;",
         "    k.lifetime = arena_new();\n    k.scratch = arena_new();\n"
         "    k.requests = 0;")
    edit("20-the-wrong-lifetime/notekeeper.c",
         "    if (k.lifetime == NULL) {",
         "    if (k.lifetime == NULL || k.scratch == NULL) {")
    edit("20-the-wrong-lifetime/notekeeper.c",
         "    arena_free(k.lifetime);",
         "    arena_free(k.scratch);\n    arena_free(k.lifetime);")


def solve_21():
    # Shrink-on-clear: release a capacity one outlier inflated.
    edit("21-the-invisible-growth/buf.c",
         "void buf_clear(struct buf *b)\n{\n    b->len = 0;",
         "#define BUF_KEEP_CAP 4096\n\n"
         "void buf_clear(struct buf *b)\n{\n"
         "    if (b->cap > BUF_KEEP_CAP) {\n"
         "        buf_free(b);\n"
         "        return;\n"
         "    }\n"
         "    b->len = 0;")


REF_CHECK_22 = """\
#!/bin/sh
cand="$1"
[ -n "$cand" ] || exit 2
cat > _main.c <<'EOF'
#include "digest.h"
#include "track.h"
#include <stdio.h>
int main(void)
{
    FILE *f = fopen("_input.txt", "wb");
    unsigned long expect = 0;
    unsigned long got;
    int i;
    if (f == NULL) return 2;
    for (i = 0; i < 5000; i++) {
        fprintf(f, "line %04d of the input\\n", i);
        expect = (expect * 31 + 22) & 0xffffffffUL;
    }
    for (i = 0; i < 1024; i++) fputc('x', f);
    fputc('\\n', f);
    expect = (expect * 31 + 1024) & 0xffffffffUL;
    fclose(f);
    got = digest_file_lines("_input.txt");
    remove("_input.txt");
    if (got != expect) { printf("wrong answer\\n"); return 1; }
    if (track_live() != 0) { printf("bytes still live\\n"); return 1; }
    if (track_peak() > 8192) { printf("peak scales with file\\n"); return 1; }
    return 0;
}
EOF
cc -std=c89 -pedantic -I. -o _check "$cand" track.c _main.c \\
    || { rm -f _main.c; exit 2; }
./_check
rc=$?
rm -f _check _main.c _input.txt
exit $rc
"""

# The lazy memory checker: tests only the arithmetic half of the contract --
# it accepts the whole-file borrower AND the leaker, so the runner's
# discrimination requirement must fail it.
REF_CHECK_22_LAZY = REF_CHECK_22.replace(
    '    if (track_live() != 0) { printf("bytes still live\\n"); return 1; }\n',
    '').replace(
    '    if (track_peak() > 8192) { printf("peak scales with file\\n");'
    ' return 1; }\n', '')


# --- extra 23 (M221): the C89 port ------------------------------------------
INVENTORY_23 = """\
/* inventory.c - a small stock report, ported to strict C89. */
#include <stdio.h>
#include <string.h>

struct item {
    const char *name;
    long count;
    long unit_cents;
};

static struct item stock[] = {
    { "bolt",   1200, 3 },
    { "washer", 4000, 1 },
    { "plate",  15,   950 },
};

static long stock_value_cents(void)
{
    long total = 0;
    int i;
    for (i = 0; i < 3; i++) {
        total += stock[i].count * stock[i].unit_cents;
    }
    return total;
}

static void report_line(char *out, size_t cap, const struct item *it)
{
    (void)cap; /* the caller's buffer is sized for the widest line */
    sprintf(out, "%-8s x%-5ld @%4ld = %8ld",
            it->name, it->count, it->unit_cents,
            it->count * it->unit_cents);
}

int main(void)
{
    long value;
    int i;
    puts("stock report");
    for (i = 0; i < 3; i++) {
        char line[64];
        report_line(line, sizeof(line), &stock[i]);
        puts(line);
    }
    value = stock_value_cents();
    printf("total value: %ld cents\\n", value);
    return 0;
}
"""

PORT_23 = """\
# The port, accounted for

| modern construct | C89 replacement | cost |
|---|---|---|
| `//` comments | `/* */` | none |
| designated initializers (`.name =`) | positional initializers | field order now load-bearing |
| `for (int i = ...)` declaration | `int i;` at block top | wider scope |
| mixed declarations and code | declarations at block top | none |
| `long long` | `long` | total bounded by LONG_MAX (2147483647 on 32-bit); a much larger stock would overflow where the modern version did not |
| `snprintf` | `sprintf` into a caller-sized buffer | the bound is the caller's promise now |
"""

# The trap: a port whose table never prices the long-long row -- "it works"
# with the cost column hollow is half the assignment.
PORT_23_FREE = PORT_23.replace(
    "total bounded by LONG_MAX (2147483647 on 32-bit); a much larger stock "
    "would overflow where the modern version did not", "none")


# --- extra 29 (M228): undefined behaviour a sanitizer catches ----------------
# The reference fix: accumulate into unsigned int, so the hash's wrap-around is
# DEFINED modular arithmetic (C89 6.1.2.5) instead of signed-overflow UB. The
# grader compiles with -fsanitize=undefined and checks both the trap is gone
# AND the one well-defined result is printed.
FOLD_29_FIXED = """\
#include <stdio.h>

static unsigned int fold(const int *v, int n)
{
    unsigned int acc = 0;
    int i;
    for (i = 0; i < n; i++) {
        acc = acc * 31u + (unsigned int)v[i]; /* defined wrap-around */
    }
    return acc;
}

int main(void)
{
    int ids[6];
    ids[0] = 1000003; ids[1] = 999983; ids[2] = 100000007;
    ids[3] = 2000000011; ids[4] = 1500000001; ids[5] = 777777773;
    printf("hash = %u\\n", fold(ids, 6));
    return 0;
}
"""

ACCOUNT_29 = """\
# Account: the undefined behaviour in fold()

## The bug
`fold()` accumulated the rolling hash into a **signed** `int`, and the hash
overflows it on the second fold. **Signed integer overflow is undefined
behaviour** in C89 (6.1.2.5) -- not the wrap-around a hash relies on. It
wrapped at -O0, which is why it "worked on my machine"; an optimizing compiler
may assume it never overflows, and UBSan traps it.

## The fix and its cost
Accumulate into an **`unsigned int`**. Unsigned arithmetic is defined to wrap
modulo 2^N -- exactly the modular behaviour a hash wants -- so this is the
correct tool, not a workaround. Cost: the result is now an explicit modular
32-bit value, and ids fold in as their unsigned bit pattern.
"""

# The trap: the account is fine but the code still has the UB (the student
# "fixed" only the printf cast, or nothing). The grader must reject it because
# the sanitizer still traps -- accounting for a bug you did not fix is hollow.
FOLD_29_STILL_BROKEN = """\
#include <stdio.h>

static int fold(const int *v, int n)
{
    int acc = 0;
    int i;
    for (i = 0; i < n; i++) {
        acc = acc * 31 + v[i]; /* still signed -- still UB */
    }
    return acc;
}

int main(void)
{
    int ids[6];
    ids[0] = 1000003; ids[1] = 999983; ids[2] = 100000007;
    ids[3] = 2000000011; ids[4] = 1500000001; ids[5] = 777777773;
    printf("hash = %u\\n", (unsigned int)fold(ids, 6));
    return 0;
}
"""


# --- extra 30 (M229): implementation-defined behaviour (char signedness) -----
# The reference fix: read raw bytes through `unsigned char` (0..255 on every
# implementation), so the sum is both signedness-independent AND correct
# (1023). The grader compiles with -fsigned-char and -funsigned-char and
# requires the two builds to agree on the right answer.
BYTESUM_30_FIXED = """\
#include <stdio.h>

static long byte_sum(const unsigned char *data, int n)
{
    long total = 0;
    int i;
    for (i = 0; i < n; i++) {
        total += data[i]; /* 0..255 on every implementation */
    }
    return total;
}

int main(void)
{
    static const unsigned char raw[8] = { 1, 128, 255, 127, 192, 64, 254, 2 };
    printf("byte sum = %ld\\n", byte_sum(raw, 8));
    return 0;
}
"""

# The trap: "made it portable" by forcing the signed reading everywhere
# (a (signed char) cast) -- both builds now agree, but on the WRONG answer
# (-1, not 1023). Portable is not the bar; portable-AND-correct is. The grader
# must reject it on the correctness check, not the portability one.
BYTESUM_30_WRONG = """\
#include <stdio.h>

static long byte_sum(const char *data, int n)
{
    long total = 0;
    int i;
    for (i = 0; i < n; i++) {
        total += (signed char)data[i]; /* consistent -- and consistently wrong */
    }
    return total;
}

int main(void)
{
    static const unsigned char raw[8] = { 1, 128, 255, 127, 192, 64, 254, 2 };
    printf("byte sum = %ld\\n", byte_sum((const char *)raw, 8));
    return 0;
}
"""

ACCOUNT_30 = """\
# Account: the byte that changed sign

## The bug
`byte_sum` read raw bytes through a plain `char`. Whether `char` is signed is
**implementation-defined** (C89 3.1.2.5) -- signed on x86, unsigned on most
ARM -- so a byte >= 0x80 sign-extends to a negative value on one platform and
stays 0..255 on the other, and the same source prints -1 on x86 and 1023 on a
Pi. This is not undefined behaviour (nothing traps); it is non-portability,
found by compiling both ways (-fsigned-char / -funsigned-char) and diffing.

## The fix and its cost
Read raw bytes through `unsigned char`, whose 0..255 range is guaranteed on
every implementation, so the sum is signedness-independent AND correct (1023).
The rule: a plain `char` is for characters; the moment a byte is a number
(a checksum, a table index, an image sample) it must be `unsigned char`.
"""


# --- the graded Racket functional course, 31-34 (M238) -----------------------
# Reference solutions (task 31 is a one-line edit, done in its lambda).
RKT_LIST_MAX_FIXED = """\
#lang racket/base
(provide list-max)
(define (list-max lst)
  (foldl max (car lst) (cdr lst)))
"""

RKT_TEST_LIST_MAX = """\
#lang racket/base
(require "list-max.rkt")
(module+ test
  (require rackunit)
  (check-equal? (list-max '(5 2 9 1)) 9)
  (check-equal? (list-max '(-3 -1 -7)) -1)   ; the case that exposes the bug
  (check-equal? (list-max '(42)) 42))
"""

# Trap for 32: three checks that pass but test nothing of list-max, applied
# without fixing the bug -- the acceptance probe must still reject it.
RKT_TEST_LIST_MAX_HOLLOW = """\
#lang racket/base
(require "list-max.rkt")
(module+ test
  (require rackunit)
  (check-equal? 1 1)
  (check-equal? 2 2)
  (check-equal? 3 3))
"""

RKT_SQUARES_PURE = """\
#lang racket/base
(require racket/list)
(provide sum-of-even-squares)
(define (sum-of-even-squares lst)
  (foldl + 0 (map (lambda (x) (* x x)) (filter even? lst))))
(module+ test
  (require rackunit)
  (check-equal? (sum-of-even-squares '(1 2 3 4 5)) 20)
  (check-equal? (sum-of-even-squares '())          0)
  (check-equal? (sum-of-even-squares '(2 4 6))     56))
"""

# Trap for 33: the mutation disguised as a box -- the smell grep must catch it.
RKT_SQUARES_BOX = """\
#lang racket/base
(provide sum-of-even-squares)
(define (sum-of-even-squares lst)
  (define total (box 0))
  (for ([x (in-list lst)])
    (when (even? x) (set-box! total (+ (unbox total) (* x x)))))
  (unbox total))
(module+ test
  (require rackunit)
  (check-equal? (sum-of-even-squares '(1 2 3 4 5)) 20)
  (check-equal? (sum-of-even-squares '())          0)
  (check-equal? (sum-of-even-squares '(2 4 6))     56))
"""

RKT_RPN_IMPL = """\
#lang racket/base
(provide rpn-eval)
(define (rpn-eval tokens)
  (define (step stack tok)
    (if (number? tok)
        (cons tok stack)
        (let ([b (car stack)] [a (cadr stack)] [rest (cddr stack)])
          (cons (case tok [(+) (+ a b)] [(-) (- a b)] [(*) (* a b)]) rest))))
  (car (foldl (lambda (tok stack) (step stack tok)) '() tokens)))
"""

RKT_DESIGN = "A fold over a stack: numbers push; an operator pops two and pushes the result.\n"


# --- the graded Guile functional course, 35-38 (M244) ------------------------
# The Racket course re-homed in Guile's Scheme: same four skills, srfi-64 in
# place of rackunit. Task 35 is a one-line edit, done in its lambda.
GUILE_LIST_MAX_FIXED = """\
(define-module (list-max) #:export (list-max) #:use-module (srfi srfi-1))
(define (list-max lst)
  (fold max (car lst) (cdr lst)))
"""

GUILE_TEST_LIST_MAX = """\
(use-modules (srfi srfi-64) (list-max))
(define r (test-runner-create))
(test-with-runner r
  (test-begin "list-max")
  (test-equal 9  (list-max '(5 2 9 1)))
  (test-equal -1 (list-max '(-3 -1 -7)))   ; the case that exposes the bug
  (test-equal 42 (list-max '(42)))
  (test-end "list-max"))
(exit (zero? (test-runner-fail-count r)))
"""

# Trap for 36: three checks that pass but test nothing of list-max, applied
# without fixing the bug -- the acceptance probe must still reject it.
GUILE_TEST_LIST_MAX_HOLLOW = """\
(use-modules (srfi srfi-64) (list-max))
(define r (test-runner-create))
(test-with-runner r
  (test-begin "list-max")
  (test-equal 1 1)
  (test-equal 2 2)
  (test-equal 3 3)
  (test-end "list-max"))
(exit (zero? (test-runner-fail-count r)))
"""

GUILE_SQUARES_PURE = """\
(define-module (squares) #:export (sum-of-even-squares) #:use-module (srfi srfi-1))
(define (sum-of-even-squares lst)
  (fold + 0 (map (lambda (x) (* x x)) (filter even? lst))))
"""

# Trap for 37: the mutation disguised behind a correct result -- the smell grep
# (comments stripped first) must still catch the set!.
GUILE_SQUARES_MUT = """\
(define-module (squares) #:export (sum-of-even-squares) #:use-module (srfi srfi-1))
(define (sum-of-even-squares lst)
  (define total 0)
  (set! total (fold + 0 (map (lambda (x) (* x x)) (filter even? lst))))
  total)
"""

GUILE_RPN_IMPL = """\
(define-module (rpn) #:export (rpn-eval) #:use-module (srfi srfi-1))
(define (rpn-eval tokens)
  (car (fold (lambda (tok stack)
               (if (number? tok)
                   (cons tok stack)
                   (let ((b (car stack)) (a (cadr stack)) (rest (cddr stack)))
                     (cons (case tok ((+) (+ a b)) ((-) (- a b)) ((*) (* a b)))
                           rest))))
             '()
             tokens)))
"""

GUILE_DESIGN = RKT_DESIGN


# --- the graded Elixir functional course, 39-42 (M245) -----------------------
# The same four skills on the BEAM: ExUnit in place of rackunit/srfi-64, and --
# because Elixir has no mutable variable -- task 41's smell is hand-rolled
# recursion, not set!. Task 39 is a one-line edit, done in its lambda.
ELIXIR_LIST_MAX_FIXED = """\
defmodule ListMax do
  def list_max(list), do: Enum.max(list)
end
"""

ELIXIR_TEST_LIST_MAX = """\
Code.require_file("list_max.exs", __DIR__)
ExUnit.start()

defmodule ListMaxTest do
  use ExUnit.Case
  import ListMax
  test "positive", do: assert list_max([5, 2, 9, 1]) == 9
  test "singleton", do: assert list_max([3]) == 3
  test "all-negative", do: assert list_max([-3, -1, -7]) == -1
end
"""

# Trap for 40: three asserts that pass but test nothing of list_max, applied
# without fixing the bug -- the acceptance probe must still reject it.
ELIXIR_TEST_LIST_MAX_HOLLOW = """\
Code.require_file("list_max.exs", __DIR__)
ExUnit.start()

defmodule ListMaxTest do
  use ExUnit.Case
  test "a", do: assert 1 == 1
  test "b", do: assert 2 == 2
  test "c", do: assert 3 == 3
end
"""

ELIXIR_SQUARES_PURE = """\
defmodule Squares do
  def sum_of_even_squares(list) do
    list
    |> Enum.filter(&(rem(&1, 2) == 0))
    |> Enum.map(&(&1 * &1))
    |> Enum.sum()
  end
end
"""

# Trap for 41: a correct result reached with a spurious Enum call BUT still
# hand-rolling recursion (a defp helper) -- the smell grep must catch it.
ELIXIR_SQUARES_RECUR = """\
defmodule Squares do
  def sum_of_even_squares(list), do: Enum.sum([]) + loop(list, 0)
  defp loop([], acc), do: acc
  defp loop([h | t], acc), do: loop(t, acc + if(rem(h, 2) == 0, do: h * h, else: 0))
end
"""

ELIXIR_RPN_IMPL = """\
defmodule Rpn do
  def rpn_eval(tokens) do
    [result] =
      Enum.reduce(tokens, [], fn
        tok, stack when is_number(tok) -> [tok | stack]
        :+, [b, a | rest] -> [a + b | rest]
        :-, [b, a | rest] -> [a - b | rest]
        :*, [b, a | rest] -> [a * b | rest]
      end)

    result
  end
end
"""

ELIXIR_DESIGN = RKT_DESIGN


# --- the graded Haskell functional course, 43-46 (M246) ----------------------
# The same four skills with a static type system and a base-only test harness
# (no HUnit/cabal on the reference box, so `runghc` + System.Exit). Task 45's
# smell is hand-rolled recursion (Haskell has no set!); task 46 adds a Token
# sum type (make illegal states unrepresentable). Task 43 is a one-line edit.
HASKELL_LIST_MAX_FIXED = """\
module ListMax (listMax) where
listMax :: [Int] -> Int
listMax = maximum
"""

HASKELL_TEST_LIST_MAX = """\
module Main where
import ListMax (listMax)
import System.Exit (exitFailure, exitSuccess)
check :: (Eq a, Show a) => a -> a -> IO Bool
check got want
  | got == want = return True
  | otherwise   = putStrLn ("FAIL: " ++ show got) >> return False
main :: IO ()
main = do
  results <- sequence
    [ check (listMax [5, 2, 9, 1]) 9
    , check (listMax [3]) 3
    , check (listMax [-3, -1, -7]) (-1)
    ]
  if and results then exitSuccess else exitFailure
"""

# Trap for 44: three checks that pass but test nothing of listMax, applied
# without fixing the bug -- the acceptance probe must still reject it.
HASKELL_TEST_LIST_MAX_HOLLOW = """\
module Main where
import ListMax (listMax)
import System.Exit (exitFailure, exitSuccess)
check :: (Eq a, Show a) => a -> a -> IO Bool
check got want = if got == want then return True else return False
main :: IO ()
main = do
  results <- sequence
    [ check (1 :: Int) 1
    , check (2 :: Int) 2
    , check (3 :: Int) 3
    ]
  if and results then exitSuccess else exitFailure
"""

HASKELL_SQUARES_PURE = """\
module Squares (sumOfEvenSquares) where
sumOfEvenSquares :: [Int] -> Int
sumOfEvenSquares = sum . map (^ 2) . filter even
"""

# Trap for 45: a correct result reached with a spurious sum call BUT still
# hand-rolling recursion (a (y:ys) clause) -- the smell grep must catch it.
HASKELL_SQUARES_RECUR = """\
module Squares (sumOfEvenSquares) where
sumOfEvenSquares :: [Int] -> Int
sumOfEvenSquares xs = sum [] + go 0 xs
  where
    go acc [] = acc
    go acc (y:ys) = go (if even y then acc + y * y else acc) ys
"""

HASKELL_RPN_IMPL = """\
module Rpn (rpnEval, Token(..)) where
data Token = Num Int | Add | Sub | Mul
  deriving (Show, Eq)
rpnEval :: [Token] -> Int
rpnEval = head . foldl step []
  where
    step stack (Num n) = n : stack
    step (b:a:rest) Add = (a + b) : rest
    step (b:a:rest) Sub = (a - b) : rest
    step (b:a:rest) Mul = (a * b) : rest
    step _ _ = error "malformed expression"
"""

HASKELL_DESIGN = RKT_DESIGN


# --- the graded Clojure functional course, 47-50 (M247) ----------------------
# The family's last member: a Lisp on the JVM, clojure.test, and -- because
# Clojure DOES have managed mutation -- task 49's smell is the atom (its `set!`),
# closing the loop back to the Racket/Guile mutation tasks. Task 47 is a one-line
# edit, done in its lambda.
CLOJURE_LIST_MAX_FIXED = """\
(defn list-max [coll]
  (apply max coll))
"""

CLOJURE_TEST_LIST_MAX = """\
(load-file "list_max.clj")
(require '[clojure.test :refer [deftest is run-tests successful?]])
(deftest list-max-test
  (is (= 9  (list-max [5 2 9 1])))
  (is (= 3  (list-max [3])))
  (is (= -1 (list-max [-3 -1 -7]))))
(System/exit (if (successful? (run-tests)) 0 1))
"""

# Trap for 48: three checks that pass but test nothing of list-max, applied
# without fixing the bug -- the acceptance probe must still reject it.
CLOJURE_TEST_LIST_MAX_HOLLOW = """\
(load-file "list_max.clj")
(require '[clojure.test :refer [deftest is run-tests successful?]])
(deftest list-max-test
  (is (= 1 1))
  (is (= 2 2))
  (is (= 3 3)))
(System/exit (if (successful? (run-tests)) 0 1))
"""

CLOJURE_SQUARES_PURE = """\
(defn sum-of-even-squares [coll]
  (->> coll
       (filter even?)
       (map #(* % %))
       (reduce + 0)))
"""

# Trap for 49: a correct result reached with reduce/map/filter BUT still using an
# atom + reset! -- the mutation smell grep must catch it.
CLOJURE_SQUARES_ATOM = """\
(defn sum-of-even-squares [coll]
  (let [total (atom 0)]
    (reset! total (reduce + 0 (map #(* % %) (filter even? coll))))
    @total))
"""

CLOJURE_RPN_IMPL = """\
(defn rpn-eval [tokens]
  (first
    (reduce (fn [stack tok]
              (if (number? tok)
                (cons tok stack)
                (let [[b a & more] stack]
                  (cons (case tok :+ (+ a b) :- (- a b) :* (* a b)) more))))
            ()
            tokens)))
"""

CLOJURE_DESIGN = RKT_DESIGN


# --- the graded C systems course, 51-54 (M248) -------------------------------
# Manual memory & data structures: the machinery Set D reasons ABOUT, built by
# hand and graded under AddressSanitizer. Task 51 is a one-line edit (remove a
# premature free), done in its lambda.

# 52: a valid student test (grows past the initial capacity) + the grow fix.
IVEC_52_FIXED = """\
#include "ivec.h"
#include <stdlib.h>
void ivec_init(ivec *v)
{
    v->cap = 4;
    v->len = 0;
    v->data = (int *)malloc(v->cap * sizeof(int));
}
void ivec_push(ivec *v, int x)
{
    if (v->len == v->cap) {
        v->cap *= 2;
        v->data = (int *)realloc(v->data, v->cap * sizeof(int));
    }
    v->data[v->len] = x;
    v->len++;
}
int ivec_get(const ivec *v, size_t i) { return v->data[i]; }
void ivec_free(ivec *v) { free(v->data); }
"""

TEST_IVEC_52 = """\
#include "ivec.h"
#include <assert.h>
int main(void)
{
    ivec v; int i;
    ivec_init(&v);
    for (i = 0; i < 30; i++) ivec_push(&v, i * 2);
    assert(ivec_get(&v, 0) == 0);
    assert(ivec_get(&v, 10) == 20);
    assert(ivec_get(&v, 29) == 58);
    ivec_free(&v);
    return 0;
}
"""

# Trap for 52: three asserts that pass but never grow past the initial capacity,
# applied WITHOUT the grow fix -- the acceptance probe (pushes 50) must reject it.
TEST_IVEC_52_HOLLOW = """\
#include "ivec.h"
#include <assert.h>
int main(void)
{
    ivec v;
    ivec_init(&v);
    ivec_push(&v, 7);
    ivec_push(&v, 8);
    assert(ivec_get(&v, 0) == 7);
    assert(ivec_get(&v, 1) == 8);
    assert(v.len == 2);
    ivec_free(&v);
    return 0;
}
"""

# 53: the bounded fix.
FMT_53_FIXED = """\
#include "fmt.h"
#include <stdio.h>
void greet(char *out, size_t cap, const char *name)
{
    snprintf(out, cap, "Hello, %s!", name);
}
"""

# Trap for 53: routes through a big temp buffer so ASan is quiet for the test
# inputs, but keeps sprintf (a landmine for a longer name) -- the banned-name
# grep must still catch it.
FMT_53_TEMP_SPRINTF = """\
#include "fmt.h"
#include <stdio.h>
#include <string.h>
void greet(char *out, size_t cap, const char *name)
{
    char tmp[256];
    sprintf(tmp, "Hello, %s!", name);
    strncpy(out, tmp, cap - 1);
    out[cap - 1] = '\\0';
}
"""

# 54: the arena implementation.
ARENA_54_IMPL = """\
#include "arena.h"
#include <stdlib.h>
struct arena { char *base; size_t cap; size_t off; };
arena *arena_new(size_t cap)
{
    arena *a = (arena *)malloc(sizeof *a);
    if (a == NULL) return NULL;
    a->base = (char *)malloc(cap);
    if (a->base == NULL) { free(a); return NULL; }
    a->cap = cap; a->off = 0;
    return a;
}
void *arena_alloc(arena *a, size_t n)
{
    size_t align = sizeof(void *);
    size_t aligned = (a->off + (align - 1)) & ~(align - 1);
    if (aligned + n > a->cap) return NULL;
    a->off = aligned + n;
    return a->base + aligned;
}
void arena_reset(arena *a) { a->off = 0; }
void arena_free(arena *a) { if (a != NULL) { free(a->base); free(a); } }
size_t arena_used(arena *a) { return a->off; }
"""

ARENA_54_DESIGN = ("A bump/arena allocator: allocation advances one offset "
                   "(aligned, bounds-checked); reset reclaims everything at "
                   "once without freeing.\n")


# --- the graded Zig systems course, 55-58 (M249) -----------------------------
# Zig's own systems model: a built-in test runner, a leak-detecting test
# allocator (its ASan), `defer`, tagged unions, and error unions. Task 55 is a
# one-line edit; task 57 adds a one-line `defer`.
ZIG_LIST_MAX_FIXED = """\
pub fn listMax(xs: []const i64) i64 {
    var m = xs[0];
    for (xs[1..]) |x| {
        if (x > m) m = x;
    }
    return m;
}
"""

ZIG_TEST_LIST_MAX = """\
const std = @import("std");
const lm = @import("list_max.zig");
test "list_max" {
    try std.testing.expectEqual(@as(i64, 9), lm.listMax(&[_]i64{ 5, 2, 9, 1 }));
    try std.testing.expectEqual(@as(i64, 3), lm.listMax(&[_]i64{3}));
    try std.testing.expectEqual(@as(i64, -1), lm.listMax(&[_]i64{ -3, -1, -7 }));
}
"""

# Trap for 56: three checks that pass but never test an all-negative slice,
# applied without fixing the bug -- the acceptance probe must still reject it.
ZIG_TEST_LIST_MAX_HOLLOW = """\
const std = @import("std");
const lm = @import("list_max.zig");
test "list_max" {
    try std.testing.expectEqual(@as(i64, 9), lm.listMax(&[_]i64{ 5, 2, 9, 1 }));
    try std.testing.expectEqual(@as(i64, 6), lm.listMax(&[_]i64{ 1, 6, 2 }));
    try std.testing.expectEqual(@as(i64, 8), lm.listMax(&[_]i64{ 8, 0, 4 }));
}
"""

ZIG_RPN_IMPL = """\
pub const Token = union(enum) {
    num: i64,
    add,
    sub,
    mul,
};
pub const RpnError = error{StackUnderflow};
pub fn rpnEval(tokens: []const Token) RpnError!i64 {
    var stack: [64]i64 = undefined;
    var sp: usize = 0;
    for (tokens) |tok| {
        switch (tok) {
            .num => |n| {
                stack[sp] = n;
                sp += 1;
            },
            .add, .sub, .mul => {
                if (sp < 2) return error.StackUnderflow;
                const b = stack[sp - 1];
                const a = stack[sp - 2];
                sp -= 2;
                stack[sp] = switch (tok) {
                    .add => a + b,
                    .sub => a - b,
                    .mul => a * b,
                    .num => unreachable,
                };
                sp += 1;
            },
        }
    }
    if (sp < 1) return error.StackUnderflow;
    return stack[sp - 1];
}
"""

ZIG_DESIGN = RKT_DESIGN


# --- the graded C++ systems course, 59-62 (M250) -----------------------------
# C++'s own systems model: RAII/ownership, standard containers, exceptions --
# graded under AddressSanitizer + LeakSanitizer. Task 59 is a one-line edit.
CPP_LIST_MAX_FIXED = """\
#ifndef LIST_MAX_HPP
#define LIST_MAX_HPP
#include <vector>
inline long list_max(const std::vector<long>& xs)
{
    long m = xs[0];
    for (long x : xs) {
        if (x > m) m = x;
    }
    return m;
}
#endif
"""

CPP_TEST_LIST_MAX = """\
#include "list_max.hpp"
#include <cassert>
int main()
{
    assert(list_max({5, 2, 9, 1}) == 9);
    assert(list_max({3}) == 3);
    assert(list_max({-3, -1, -7}) == -1);
    return 0;
}
"""

# Trap for 60: three asserts that pass but never test an all-negative vector,
# applied without fixing the bug -- the acceptance probe must still reject it.
CPP_TEST_LIST_MAX_HOLLOW = """\
#include "list_max.hpp"
#include <cassert>
int main()
{
    assert(list_max({5, 2, 9, 1}) == 9);
    assert(list_max({1, 6, 2}) == 6);
    assert(list_max({8, 0, 4}) == 8);
    return 0;
}
"""

# 61: the RAII fix -- the standard library owns the memory, no raw new/delete.
CPP_BUFFER_FIXED = """\
#ifndef BUFFER_HPP
#define BUFFER_HPP
#include <cstddef>
#include <vector>
class Buffer {
public:
    explicit Buffer(std::size_t n) : data_(n) {}
    int& at(std::size_t i) { return data_[i]; }
    std::size_t size() const { return data_.size(); }

private:
    std::vector<int> data_;
};
#endif
"""

# Trap for 61: a destructor with delete[] fixes the LEAK (LeakSanitizer is quiet)
# but keeps raw new/delete -- the banned-name grep must still catch it.
CPP_BUFFER_DTOR = """\
#ifndef BUFFER_HPP
#define BUFFER_HPP
#include <cstddef>
class Buffer {
public:
    explicit Buffer(std::size_t n) : data_(new int[n]), size_(n) {}
    ~Buffer() { delete[] data_; }
    int& at(std::size_t i) { return data_[i]; }
    std::size_t size() const { return size_; }

private:
    int* data_;
    std::size_t size_;
};
#endif
"""

# 62: the capstone implementation.
CPP_RPN_IMPL = """\
#ifndef RPN_HPP
#define RPN_HPP
#include <vector>
#include <stdexcept>
struct Token {
    enum Kind { Num, Add, Sub, Mul } kind;
    long value;
};
inline long rpn_eval(const std::vector<Token>& tokens)
{
    std::vector<long> st;
    for (const Token& t : tokens) {
        if (t.kind == Token::Num) {
            st.push_back(t.value);
            continue;
        }
        if (st.size() < 2) throw std::runtime_error("stack underflow");
        long b = st.back();
        st.pop_back();
        long a = st.back();
        st.pop_back();
        long r = 0;
        switch (t.kind) {
        case Token::Add: r = a + b; break;
        case Token::Sub: r = a - b; break;
        case Token::Mul: r = a * b; break;
        default: break;
        }
        st.push_back(r);
    }
    if (st.empty()) throw std::runtime_error("empty expression");
    return st.back();
}
#endif
"""

CPP_DESIGN = RKT_DESIGN


# --- the graded Rust systems course, 63-66 (M251) ----------------------------
# Rust's own systems model: the borrow checker as COMPILE-TIME memory safety,
# Result/Option, sum types. Graded with `rustc --test` (no cargo needed). Task
# 63 is a one-line edit.
RUST_LIST_MAX_FIXED = """\
pub fn list_max(xs: &[i64]) -> i64 {
    let mut m = xs[0];
    for &x in &xs[1..] {
        if x > m {
            m = x;
        }
    }
    m
}
"""

RUST_TEST_LIST_MAX = """\
#[path = "list_max.rs"]
mod list_max;
use list_max::list_max;
#[test]
fn cases() {
    assert_eq!(list_max(&[5, 2, 9, 1]), 9);
    assert_eq!(list_max(&[3]), 3);
    assert_eq!(list_max(&[-3, -1, -7]), -1);
}
"""

# Trap for 64: three checks that pass but never test an all-negative slice,
# applied without fixing the bug -- the acceptance probe must still reject it.
RUST_TEST_LIST_MAX_HOLLOW = """\
#[path = "list_max.rs"]
mod list_max;
use list_max::list_max;
#[test]
fn cases() {
    assert_eq!(list_max(&[5, 2, 9, 1]), 9);
    assert_eq!(list_max(&[1, 6, 2]), 6);
    assert_eq!(list_max(&[8, 0, 4]), 8);
}
"""

# 65: the fix -- borrow the input directly (its lifetime outlives the call).
RUST_WORDS_FIXED = """\
pub fn first_word(s: &str) -> &str {
    s.split(' ').next().unwrap()
}
"""

# 66: the capstone implementation.
RUST_RPN_IMPL = """\
#[derive(Clone, Copy)]
pub enum Token {
    Num(i64),
    Add,
    Sub,
    Mul,
}
pub fn rpn_eval(tokens: &[Token]) -> Result<i64, String> {
    let mut st: Vec<i64> = Vec::new();
    for t in tokens {
        match t {
            Token::Num(n) => st.push(*n),
            op => {
                let b = st.pop().ok_or("underflow")?;
                let a = st.pop().ok_or("underflow")?;
                st.push(match op {
                    Token::Add => a + b,
                    Token::Sub => a - b,
                    Token::Mul => a * b,
                    Token::Num(_) => unreachable!(),
                });
            }
        }
    }
    st.pop().ok_or("empty".to_string())
}
"""

RUST_DESIGN = RKT_DESIGN


# --- the graded PROCESS curriculum, 67-73 (M253) -----------------------------
# The software-development process, graded by STRUCTURAL floors only (presence +
# shape + cross-file id consistency) -- never semantic quality. Pure-sh graders,
# no toolchain. Each reference passes the floor; each trap has the shape but not
# the substance the cross-check catches.
PROC_REQUIREMENTS = """\
# Requirements -- a URL shortener
- R1: The service shall accept a URL and return a unique short code.
- R2: The service shall redirect a short code to its original URL.
- R3: The service shall reject a malformed URL with a 400 response.
- R4: A short code shall be at most 8 characters.
- R5: The service shall record how many times each short code is used.
"""
# Trap for 67: five ids, but every one a vague wish -- no verifiable shall/must.
PROC_REQUIREMENTS_HOLLOW = """\
- R1: fast
- R2: nice
- R3: easy
- R4: modern
- R5: cool
"""

PROC_USECASES = """\
# Use cases
## UC1: Shorten a URL
- Actor: a visitor
- Trigger: submits a long URL
- Success: receives a short code
- Failure: the URL is malformed -> an error is shown
## UC2: Follow a short link
- Actor: a visitor
- Trigger: opens a short URL
- Success: redirected to the original
- Failure: the code does not exist -> a 404
## UC3: View stats
- Actor: the link owner
- Trigger: opens the stats page
- Success: sees the click count
- Failure: not the owner -> access denied
"""
# Trap for 68: three use-cases with actor + trigger, but only happy paths.
PROC_USECASES_NOFAIL = """\
## UC1: Shorten
- Actor: visitor
- Trigger: submits a URL
- Success: gets a code
## UC2: Follow
- Actor: visitor
- Trigger: opens a link
- Success: redirected
## UC3: Stats
- Actor: owner
- Trigger: opens stats
- Success: sees counts
"""

PROC_DESIGN = """\
# Design -- note-taking API
A small REST service over a single table notes(id, body).
- POST /notes creates a note and returns its id (satisfies R1).
- GET /notes/{id} fetches a note by id (R2).
- GET /notes lists all notes (R3).
- DELETE /notes/{id} deletes a note by id (R4).
"""
# Trap for 69: addresses R1-R3 but drops R4 (an untraced requirement).
PROC_DESIGN_PARTIAL = """\
# Design
- POST /notes -> id (R1)
- GET /notes/{id} (R2)
- GET /notes (R3)
delete is not designed yet
"""

PROC_README = """\
# noteapp
A tiny note-taking CLI.
## Install
    pip install noteapp
## Usage
Run it:
```
noteapp add "buy milk"
noteapp list
```
That prints your notes.
"""
# Trap for 70: install + usage headings, but no worked example (no code block).
PROC_README_NOEX = """\
# app
## Install
Download it somewhere.
## Usage
Run it somehow and it works.
"""

PROC_NOTE_1 = """\
# 2026-08-01
- Did: built the create-note endpoint.
- Decided: use SQLite for simplicity, no server to run.
- Next: add fetch-by-id.
"""
PROC_NOTE_2 = """\
# 2026-08-02
- Did: added fetch-by-id and the list endpoint.
- Decided: return 404 for a missing id rather than an empty body.
- Next: delete endpoint + input validation.
"""
PROC_NOTE_3 = """\
# 2026-08-03
- Did: delete endpoint and validation, wrote the README.
- Decided: cap note bodies at 4KB to keep things simple.
- Next: a small CLI client.
"""
# Trap for 71: three dated notes, but the spine is incomplete (no Next).
PROC_NOTE_NONEXT = """\
# 2026-08-0%d
- Did: some work.
- Decided: a thing.
"""

PROC_BOARD = """\
# Board (WIP limit on Doing: 2)
## Todo
- R3: list-notes endpoint
- R4: delete endpoint
## Doing
- R1: create-note endpoint
- R2: fetch-by-id endpoint
## Done
- set up the repo
"""
# Trap for 72: valid columns + WIP declared, but a Doing card with no trace.
PROC_BOARD_UNTRACED = """\
# Board (WIP: 2)
## Todo
- R3: something
## Doing
- R1: create endpoint
- refactor the storage layer
## Done
- setup
"""

PROC_PLAN = """\
# Plan -- note-taking API
## Milestones
- M1: backend CRUD endpoints -- M (est 3d)
- M2: input validation + errors -- S (est 1d)
- M3: a CLI client -- M (est 2d)
## Retro
- M1 estimate 3d, actual 5d -- underestimated the validation cases.
- M2 estimate 1d, actual 1d -- on target.
- Lesson: my estimates run ~1.5x; multiply future guesses.
"""
# Trap for 73: sized milestones, but no retro comparing estimate vs actual.
PROC_PLAN_NORETRO = """\
## Milestones
- M1: backend -- M (est 3d)
- M2: validation -- S (est 1d)
- M3: CLI -- M (est 2d)
"""


# --- extra 24 (M221): the reading track's floor ------------------------------
TEST_TRIM_24 = """\
/* test_trim.c - the proof: trim keeps the NEWEST notes, all of them. */
#include "journal.h"
#include <stdio.h>
#include <string.h>
int main(void)
{
    struct journal j;
    journal_init(&j);
    journal_append(&j, "one");
    journal_append(&j, "two");
    journal_append(&j, "three");
    journal_append(&j, "four");
    journal_append(&j, "five");
    journal_trim(&j, 2);
    printf("1..3\\n");
    if (journal_count(&j) != 2) { printf("not ok 1\\n"); return 1; }
    printf("ok 1 - count is 2\\n");
    if (strcmp(journal_get(&j, 0), "four") != 0) {
        printf("not ok 2 - oldest survivor is %s\\n", journal_get(&j, 0));
        return 1;
    }
    printf("ok 2 - oldest survivor is four\\n");
    if (strcmp(journal_get(&j, 1), "five") != 0) {
        printf("not ok 3 - newest is %s\\n", journal_get(&j, 1));
        return 1;
    }
    printf("ok 3 - the newest note survived\\n");
    return 0;
}
"""

ANALYSIS_24 = """\
## The map
main-less library: journal.h is the contract; journal.c implements append/
count/get/find/trim; test_journal.c covers everything EXCEPT journal_trim.

## The suspect
journal_trim: the only untested function, and its shift copies from
`i + drop - 1` -- one before the first survivor. The newest note is lost
and a discarded one survives; count is right, so nothing else notices.

## Proof
test_trim.c: five notes, trim to 2 -- must keep "four","five". Fails
as-found (keeps "three","four"), passes after changing the source index
to `i + drop`.
"""


def fix_24():
    edit("24-read-a-real-project/journal/journal.c",
         "memcpy(j->notes[i], j->notes[i + drop - 1], NOTE_MAX);",
         "memcpy(j->notes[i], j->notes[i + drop], NOTE_MAX);")



# --- extras 25/26 (M221): the C -> Zig migration arc -------------------------
VOWELS_ZIG_25 = """\
export fn wt_count_vowels(text: [*:0]const u8) c_long {
    var i: usize = 0;
    var n: c_long = 0;
    while (text[i] != 0) : (i += 1) {
        switch (text[i]) {
            'a', 'e', 'i', 'o', 'u', 'A', 'E', 'I', 'O', 'U' => n += 1,
            else => {},
        }
    }
    return n;
}
"""

STATS_ZIG_26 = """\
// stats.zig - wt_count_words / wt_longest_word behind the C header.
fn isSpace(ch: u8) bool {
    return ch == ' ' or ch == '\\t';
}

export fn wt_count_words(text: [*:0]const u8) c_long {
    var i: usize = 0;
    var n: c_long = 0;
    var in_word = false;
    while (text[i] != 0) : (i += 1) {
        if (isSpace(text[i])) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            n += 1;
        }
    }
    return n;
}

export fn wt_longest_word(text: [*:0]const u8) c_long {
    var i: usize = 0;
    var best: c_long = 0;
    var cur: c_long = 0;
    while (true) : (i += 1) {
        if (text[i] == 0 or isSpace(text[i])) {
            if (cur > best) best = cur;
            cur = 0;
            if (text[i] == 0) break;
        } else {
            cur += 1;
        }
    }
    return best;
}
"""


def solve_25():
    write("25-extend-in-zig/vowels.zig", VOWELS_ZIG_25)
    edit("25-extend-in-zig/wordtool.h",
         "long wt_longest_word(const char *text);",
         "long wt_longest_word(const char *text);\n"
         "long wt_count_vowels(const char *text);")
    edit("25-extend-in-zig/main.c",
         '    printf("words=%ld longest=%ld\\n",\n'
         '           wt_count_words(text), wt_longest_word(text));',
         '    printf("words=%ld longest=%ld vowels=%ld\\n",\n'
         '           wt_count_words(text), wt_longest_word(text),\n'
         '           wt_count_vowels(text));')
    edit("25-extend-in-zig/build.sh",
         "zig cc -std=c89 -Wall -Wextra -Werror -o wordtool main.c stats.c",
         "zig build-obj -O ReleaseSafe vowels.zig\n"
         "zig cc -std=c89 -Wall -Wextra -Werror -o wordtool main.c stats.c"
         " vowels.o")


def solve_26():
    write("26-refactor-to-zig/stats.zig", STATS_ZIG_26)
    os.remove(path("26-refactor-to-zig/stats.c"))
    edit("26-refactor-to-zig/build.sh",
         "zig cc -std=c89 -Wall -Wextra -Werror -o wordtool main.c stats.c",
         "zig build-obj -O ReleaseSafe stats.zig\n"
         "zig cc -std=c89 -Wall -Wextra -Werror -o wordtool main.c stats.o")



# --- extras 27/28 (M221): the C -> C++ migration arc -------------------------
VOWELS_CPP_27 = """\
// vowels.cpp - the vowel counter, in C++ behind the C header.
#include <algorithm>
#include <cstring>

extern "C" long wt_count_vowels(const char *text)
{
    const char *end = text + std::strlen(text);
    return std::count_if(text, end, [](char c) {
        switch (c) {
        case 'a': case 'e': case 'i': case 'o': case 'u':
        case 'A': case 'E': case 'I': case 'O': case 'U':
            return true;
        default:
            return false;
        }
    });
}
"""

STATS_CPP_28 = """\
// stats.cpp - wt_count_words / wt_longest_word behind the C header.
#include <string_view>

namespace {
bool is_space(char c)
{
    return c == ' ' || c == '\\t';
}
} // namespace

extern "C" long wt_count_words(const char *text)
{
    std::string_view s(text);
    long n = 0;
    bool in_word = false;
    for (char c : s) {
        if (is_space(c)) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            n++;
        }
    }
    return n;
}

extern "C" long wt_longest_word(const char *text)
{
    std::string_view s(text);
    long best = 0;
    long cur = 0;
    for (char c : s) {
        if (is_space(c)) {
            if (cur > best) best = cur;
            cur = 0;
        } else {
            cur++;
        }
    }
    if (cur > best) best = cur;
    return best;
}
"""


def solve_27():
    write("27-extend-in-cpp/vowels.cpp", VOWELS_CPP_27)
    edit("27-extend-in-cpp/wordtool.h",
         "long wt_longest_word(const char *text);",
         "long wt_longest_word(const char *text);\n"
         "long wt_count_vowels(const char *text);")
    edit("27-extend-in-cpp/main.c",
         '    printf("words=%ld longest=%ld\\n",\n'
         '           wt_count_words(text), wt_longest_word(text));',
         '    printf("words=%ld longest=%ld vowels=%ld\\n",\n'
         '           wt_count_words(text), wt_longest_word(text),\n'
         '           wt_count_vowels(text));')
    edit("27-extend-in-cpp/build.sh",
         "cc -std=c89 -pedantic -Wall -Wextra -Werror -c main.c stats.c\n"
         "cc -o wordtool main.o stats.o",
         "cc -std=c89 -pedantic -Wall -Wextra -Werror -c main.c stats.c\n"
         "c++ -std=c++17 -Wall -Wextra -Werror -c vowels.cpp\n"
         "c++ -o wordtool main.o stats.o vowels.o")


def solve_28():
    write("28-refactor-to-cpp/stats.cpp", STATS_CPP_28)
    os.remove(path("28-refactor-to-cpp/stats.c"))
    edit("28-refactor-to-cpp/build.sh",
         "cc -std=c89 -pedantic -Wall -Wextra -Werror -c main.c stats.c\n"
         "cc -o wordtool main.o stats.o",
         "cc -std=c89 -pedantic -Wall -Wextra -Werror -c main.c\n"
         "c++ -std=c++17 -Wall -Wextra -Werror -c stats.cpp\n"
         "c++ -o wordtool main.o stats.o")


SOLUTIONS = [
    ("00-hello.md",
     lambda: write("00-hello/hello.txt", "hello from my bench\n")),
    ("01-find-the-setting.md",
     lambda: write("01-find-the-setting/answer.txt", "9\n")),
    ("02-where-is-it-defined.md",
     lambda: write("02-where-is-it-defined/found.txt",
                   "docs/assignments/02-where-is-it-defined/src/beta.c\n")),
    ("03-the-smallest-change.md",
     lambda: edit("03-the-smallest-change/greet.c",
                  "Welcome to jichi", "Welcome to the bench")),
    ("04-two-places-one-truth.md",
     lambda: (edit("04-two-places-one-truth/ring.h",
                   "RING_CAP 64", "RING_CAP 128"),
              edit("04-two-places-one-truth/README.md",
                   "64 entries", "128 entries"))),
    ("05-the-ambiguous-edit.md",
     lambda: edit("05-the-ambiguous-edit/settings.ini",
                  "[uploads]\nsize = 256", "[uploads]\nsize = 512")),
    ("06-make-the-test-pass.md",
     lambda: edit("06-make-the-test-pass/stats.c",
                  "int best = 0;", "int best = v[0];")),
    ("07-write-the-test-first.md",
     lambda: (fix_07(),
              write("07-write-the-test-first/test_clamp.c", TEST_CLAMP))),
    ("08-the-wrong-suspect.md",
     lambda: (fix_08(), write("08-the-wrong-suspect/NOTES.md", NOTES))),
    ("09-grade-the-grader.md",
     lambda: write("09-grade-the-grader/check.sh", CHECK_09)),
    ("10-design-before-code.md",
     lambda: write("10-design-before-code/DESIGN.md", DESIGN_10)),
    ("11-name-whats-wrong.md",
     lambda: write("11-name-whats-wrong/REVIEW.md", REVIEW_11)),
    ("12-refactor-without-change.md",
     lambda: write("12-refactor-without-change/dur.c", DUR_REFACTORED)),
    ("13-delegate-with-a-leash.md",
     lambda: (write("13-delegate-with-a-leash/work/report.txt",
                    "delegation with verification\n"),
              write("13-delegate-with-a-leash/journal.jsonl", JOURNAL_13))),
    ("14-the-hollow-gate.md",
     lambda: (write("14-the-hollow-gate/gate.sh", GATE_14_FIXED),
              fix_14_code())),
    ("15-the-confident-misdiagnosis.md",
     lambda: (fix_15_code(),
              write("15-the-confident-misdiagnosis/VERDICT.md", VERDICT_15))),
    ("16-teach-a-peer.md", solve_16),
    ("17-capstone.md",
     lambda: (write("17-capstone/portfolio/PROPOSAL.md", PROPOSAL_17),
              write("17-capstone/portfolio/journal.jsonl", JOURNAL_13),
              write("17-capstone/portfolio/RECORD.md", RECORD_17))),
    # Extras (M187): the porting survey's artifact floor.
    ("18-where-posix-ends.md",
     lambda: write("18-where-posix-ends/REPORT.md", REPORT_18)),
    ("19-the-third-compiler.md",
     lambda: write("19-the-third-compiler/REPORT.md", REPORT_19)),
    ("20-the-wrong-lifetime.md", solve_20),
    ("21-the-invisible-growth.md", solve_21),
    ("22-slope-lies-keep-the-peak.md",
     lambda: write("22-slope-lies-keep-the-peak/check.sh", REF_CHECK_22)),
    ("23-the-time-traveling-c.md",
     lambda: (write("23-the-time-traveling-c/inventory.c", INVENTORY_23),
              write("23-the-time-traveling-c/PORT.md", PORT_23))),
    ("24-read-a-real-project.md",
     lambda: (fix_24(),
              write("24-read-a-real-project/journal/test_trim.c",
                    TEST_TRIM_24),
              write("24-read-a-real-project/ANALYSIS.md", ANALYSIS_24))),
]

# The zig-gated extras (25/26) prove two-sided only where zig exists --
# mirroring their own test.sh prerequisite. The skip is loud, never silent.
if shutil.which("zig"):
    SOLUTIONS += [
        ("25-extend-in-zig.md", solve_25),
        ("26-refactor-to-zig.md", solve_26),
    ]
else:
    print("ok - skipped extras 25/26 (no zig on PATH)")

if shutil.which("c++"):
    SOLUTIONS += [
        ("27-extend-in-cpp.md", solve_27),
        ("28-refactor-to-cpp.md", solve_28),
    ]
else:
    print("ok - skipped extras 27/28 (no c++ on PATH)")


def has_ubsan():
    """Task 29's grader compiles with -fsanitize=undefined; its two-sided
    proof needs a UBSan-capable compiler (cc or clang), not just any cc. Probe
    by actually compiling -- a capability, not a binary on PATH -- and skip
    loudly if absent, mirroring test.sh's own fallback."""
    d = tempfile.mkdtemp(prefix="jc-ubsan-")
    try:
        src = os.path.join(d, "u.c")
        open(src, "w").write("int main(void){return 0;}\n")
        for cc in ("cc", "clang"):
            if not shutil.which(cc):
                continue
            rc = os.system("%s -fsanitize=undefined -o %s/u %s "
                           ">/dev/null 2>&1" % (cc, d, src))
            if rc == 0:
                return True
        return False
    finally:
        shutil.rmtree(d, ignore_errors=True)


HAVE_UBSAN = has_ubsan()
if HAVE_UBSAN:
    SOLUTIONS += [
        ("29-works-on-my-machine.md",
         lambda: (write("29-works-on-my-machine/fold.c", FOLD_29_FIXED),
                  write("29-works-on-my-machine/ACCOUNT.md", ACCOUNT_29))),
    ]
else:
    print("ok - skipped extra 29 (no UBSan-capable compiler)")


def has_char_flags():
    """Task 30's grader forces each char signedness with -fsigned-char /
    -funsigned-char; its two-sided proof needs a compiler that honours both.
    gcc and clang do; probe rather than assume, and skip loudly otherwise."""
    d = tempfile.mkdtemp(prefix="jc-char-")
    try:
        src = os.path.join(d, "c.c")
        open(src, "w").write("int main(void){return 0;}\n")
        for cc in ("cc", "clang"):
            if not shutil.which(cc):
                continue
            ok = all(os.system("%s %s -o %s/c %s >/dev/null 2>&1"
                               % (cc, flag, d, src)) == 0
                     for flag in ("-fsigned-char", "-funsigned-char"))
            if ok:
                return True
        return False
    finally:
        shutil.rmtree(d, ignore_errors=True)


HAVE_CHAR_FLAGS = has_char_flags()
if HAVE_CHAR_FLAGS:
    SOLUTIONS += [
        ("30-the-signed-byte.md",
         lambda: (write("30-the-signed-byte/bytesum.c", BYTESUM_30_FIXED),
                  write("30-the-signed-byte/ACCOUNT.md", ACCOUNT_30))),
    ]
else:
    print("ok - skipped extra 30 (no -f{,un}signed-char support)")


# The graded Racket functional course (M238): the reading track's assignments,
# each two-sided through `jichi grade`, gated on `raco` with a loud skip.
# A toolchain is "available" only if it RUNS, not if its name resolves (M459).
#
# shutil.which finds a version-manager shim -- asdf, mise, rbenv, pyenv -- which
# exists whether or not a version is selected. This tier runs under run.sh's
# suite-wide private $HOME (M198), and asdf resolves its version from
# $HOME/.tool-versions, so every shim reports "unknown command ... Perhaps you
# have to reshim?" here while working perfectly in the operator's shell.
#
# That cost a red CI and three wrong diagnoses: the Elixir course was RUN
# because the shim existed, its gate then failed, and the report read
# "solution accepted: wanted exit 0, got 1" -- a wrong answer, when the truth
# was no usable toolchain. Proven by JC_E2E_KEEP_HOME=1, under which the whole
# tier passes.
#
# Probe args differ per tool: zig has `version` not `--version`, raco and
# clojure answer neither and take `-h`. Each one below was checked on this
# machine rather than assumed.
def usable(tool, *probe):
    if shutil.which(tool) is None:
        return False
    # NARROW, deliberately. A bare `except Exception` here turns "this probe is
    # broken" into "the toolchain is absent" -- two states that must never look
    # alike. The first draft of this function did exactly that: `subprocess` was
    # not imported, every call raised NameError, the except swallowed it, and
    # all five language courses silently skipped while printing "no raco on
    # PATH" about a raco that was on PATH. Only OSError (no such executable) and
    # SubprocessError (timeout) mean "unavailable"; anything else is a bug here
    # and must be allowed to crash loudly.
    try:
        return subprocess.run([tool, *probe], stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL,
                              timeout=60).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False

HAVE_RACKET = usable("raco", "-h")
if HAVE_RACKET:
    SOLUTIONS += [
        ("31-racket-make-it-pass.md",
         lambda: edit("31-racket-make-it-pass/clamp.rkt",
                      "[(> x hi) x]", "[(> x hi) hi]")),
        ("32-racket-test-first.md",
         lambda: (write("32-racket-test-first/list-max.rkt", RKT_LIST_MAX_FIXED),
                  write("32-racket-test-first/test-list-max.rkt",
                        RKT_TEST_LIST_MAX))),
        ("33-racket-loops-to-folds.md",
         lambda: write("33-racket-loops-to-folds/squares.rkt", RKT_SQUARES_PURE)),
        ("34-racket-capstone.md",
         lambda: (write("34-racket-capstone/rpn.rkt", RKT_RPN_IMPL),
                  write("34-racket-capstone/DESIGN.md", RKT_DESIGN))),
    ]
else:
    print("ok - skipped the Racket course 31-34 (raco not usable here -- absent, or a version-manager shim with no version under this $HOME)")


# The graded Guile functional course (M244): the Racket course re-homed in
# Guile, two-sided through `jichi grade`, gated on `guile` with a loud skip.
HAVE_GUILE = usable("guile", "--version")
if HAVE_GUILE:
    SOLUTIONS += [
        ("35-guile-make-it-pass.md",
         lambda: edit("35-guile-make-it-pass/clamp.scm",
                      "((> x hi) x)", "((> x hi) hi)")),
        ("36-guile-test-first.md",
         lambda: (write("36-guile-test-first/list-max.scm", GUILE_LIST_MAX_FIXED),
                  write("36-guile-test-first/test-list-max.scm",
                        GUILE_TEST_LIST_MAX))),
        ("37-guile-loops-to-folds.md",
         lambda: write("37-guile-loops-to-folds/squares.scm", GUILE_SQUARES_PURE)),
        ("38-guile-capstone.md",
         lambda: (write("38-guile-capstone/rpn.scm", GUILE_RPN_IMPL),
                  write("38-guile-capstone/DESIGN.md", GUILE_DESIGN))),
    ]
else:
    print("ok - skipped the Guile course 35-38 (guile not usable here -- absent, or a version-manager shim with no version under this $HOME)")


# The graded Elixir functional course (M245): the same four skills on the BEAM,
# two-sided through `jichi grade`, gated on `elixir` with a loud skip.
HAVE_ELIXIR = usable("elixir", "--version")
if HAVE_ELIXIR:
    SOLUTIONS += [
        ("39-elixir-make-it-pass.md",
         lambda: edit("39-elixir-make-it-pass/clamp.exs",
                      "when x > hi, do: x", "when x > hi, do: hi")),
        ("40-elixir-test-first.md",
         lambda: (write("40-elixir-test-first/list_max.exs", ELIXIR_LIST_MAX_FIXED),
                  write("40-elixir-test-first/test_list_max.exs",
                        ELIXIR_TEST_LIST_MAX))),
        ("41-elixir-loops-to-folds.md",
         lambda: write("41-elixir-loops-to-folds/squares.exs", ELIXIR_SQUARES_PURE)),
        ("42-elixir-capstone.md",
         lambda: (write("42-elixir-capstone/rpn.exs", ELIXIR_RPN_IMPL),
                  write("42-elixir-capstone/DESIGN.md", ELIXIR_DESIGN))),
    ]
else:
    print("ok - skipped the Elixir course 39-42 (elixir not usable here -- absent, or a version-manager shim with no version under this $HOME)")


# The graded Haskell functional course (M246): the same four skills with a
# static type system, two-sided through `jichi grade`, gated on `runghc`.
HAVE_HASKELL = usable("runghc", "--version")
if HAVE_HASKELL:
    SOLUTIONS += [
        ("43-haskell-make-it-pass.md",
         lambda: edit("43-haskell-make-it-pass/Clamp.hs",
                      "| x > hi    = x", "| x > hi    = hi")),
        ("44-haskell-test-first.md",
         lambda: (write("44-haskell-test-first/ListMax.hs", HASKELL_LIST_MAX_FIXED),
                  write("44-haskell-test-first/TestListMax.hs",
                        HASKELL_TEST_LIST_MAX))),
        ("45-haskell-loops-to-folds.md",
         lambda: write("45-haskell-loops-to-folds/Squares.hs", HASKELL_SQUARES_PURE)),
        ("46-haskell-capstone.md",
         lambda: (write("46-haskell-capstone/Rpn.hs", HASKELL_RPN_IMPL),
                  write("46-haskell-capstone/DESIGN.md", HASKELL_DESIGN))),
    ]
else:
    print("ok - skipped the Haskell course 43-46 (runghc not usable here -- absent, or a version-manager shim with no version under this $HOME)")


# The graded Clojure functional course (M247): the family's last member, a Lisp
# on the JVM, two-sided through `jichi grade`, gated on `clojure`.
HAVE_CLOJURE = usable("clojure", "-h")
if HAVE_CLOJURE:
    SOLUTIONS += [
        ("47-clojure-make-it-pass.md",
         lambda: edit("47-clojure-make-it-pass/clamp.clj",
                      "(> x hi) x", "(> x hi) hi")),
        ("48-clojure-test-first.md",
         lambda: (write("48-clojure-test-first/list_max.clj", CLOJURE_LIST_MAX_FIXED),
                  write("48-clojure-test-first/test_list_max.clj",
                        CLOJURE_TEST_LIST_MAX))),
        ("49-clojure-loops-to-folds.md",
         lambda: write("49-clojure-loops-to-folds/squares.clj", CLOJURE_SQUARES_PURE)),
        ("50-clojure-capstone.md",
         lambda: (write("50-clojure-capstone/rpn.clj", CLOJURE_RPN_IMPL),
                  write("50-clojure-capstone/DESIGN.md", CLOJURE_DESIGN))),
    ]
else:
    print("ok - skipped the Clojure course 47-50 (clojure not usable here -- absent, or a version-manager shim with no version under this $HOME)")


def has_asan():
    """The C systems course (51-54) grades under AddressSanitizer; its two-sided
    proof needs an ASan-capable compiler, not just any cc. Probe by actually
    compiling -- a capability, not a binary on PATH -- and skip loudly if absent,
    mirroring each test.sh's own cc->clang fallback."""
    d = tempfile.mkdtemp(prefix="jc-asan-")
    try:
        src = os.path.join(d, "a.c")
        open(src, "w").write("int main(void){return 0;}\n")
        for cc in ("cc", "clang"):
            if not shutil.which(cc):
                continue
            rc = os.system("%s -fsanitize=address -o %s/a %s >/dev/null 2>&1"
                           % (cc, d, src))
            if rc == 0:
                return True
        return False
    finally:
        shutil.rmtree(d, ignore_errors=True)


# The graded C systems course (M248): manual memory & data structures, built by
# hand and graded under AddressSanitizer, two-sided through `jichi grade`.
HAVE_ASAN = has_asan()
if HAVE_ASAN:
    SOLUTIONS += [
        ("51-the-dangling-pointer.md",
         lambda: edit("51-the-dangling-pointer/shout.c",
                      "    free(out);              /* <-- one line here is the bug */",
                      "    /* out is the caller's to free (see main) */")),
        ("52-the-array-that-outgrew-itself.md",
         lambda: (write("52-the-array-that-outgrew-itself/ivec.c", IVEC_52_FIXED),
                  write("52-the-array-that-outgrew-itself/test_ivec.c",
                        TEST_IVEC_52))),
        ("53-never-call-sprintf.md",
         lambda: write("53-never-call-sprintf/fmt.c", FMT_53_FIXED)),
        ("54-the-arena.md",
         lambda: (write("54-the-arena/arena.c", ARENA_54_IMPL),
                  write("54-the-arena/DESIGN.md", ARENA_54_DESIGN))),
    ]
else:
    print("ok - skipped the C systems course 51-54 (no ASan-capable compiler)")


# The graded Zig systems course (M249): Zig's own systems model, two-sided
# through `jichi grade`, gated on `zig` with a loud skip.
HAVE_ZIG = shutil.which("zig") is not None
if HAVE_ZIG:
    SOLUTIONS += [
        ("55-zig-make-it-pass.md",
         lambda: edit("55-zig-make-it-pass/clamp.zig",
                      "if (x > hi) return x;", "if (x > hi) return hi;")),
        ("56-zig-test-first.md",
         lambda: (write("56-zig-test-first/list_max.zig", ZIG_LIST_MAX_FIXED),
                  write("56-zig-test-first/test_list_max.zig", ZIG_TEST_LIST_MAX))),
        ("57-zig-the-missing-defer.md",
         lambda: edit("57-zig-the-missing-defer/shout.zig",
                      "    // BUG: scratch is never freed -- add a `defer` right here.",
                      "    defer allocator.free(scratch);")),
        ("58-zig-capstone.md",
         lambda: (write("58-zig-capstone/rpn.zig", ZIG_RPN_IMPL),
                  write("58-zig-capstone/DESIGN.md", ZIG_DESIGN))),
    ]
else:
    print("ok - skipped the Zig systems course 55-58 (no zig on PATH)")


def has_cxx_asan():
    """The C++ systems course (59-62) grades under AddressSanitizer; probe a real
    ASan-capable C++ compiler, not just any g++/clang++, and skip loudly."""
    d = tempfile.mkdtemp(prefix="jc-cxxasan-")
    try:
        src = os.path.join(d, "a.cpp")
        open(src, "w").write("int main(){return 0;}\n")
        for cc in ("g++", "c++", "clang++"):
            if not shutil.which(cc):
                continue
            rc = os.system("%s -std=c++17 -fsanitize=address -o %s/a %s "
                           ">/dev/null 2>&1" % (cc, d, src))
            if rc == 0:
                return True
        return False
    finally:
        shutil.rmtree(d, ignore_errors=True)


# The graded C++ systems course (M250): RAII/ownership, containers, exceptions,
# under ASan -- two-sided through `jichi grade`, gated on an ASan-capable g++.
HAVE_CXX_ASAN = has_cxx_asan()
if HAVE_CXX_ASAN:
    SOLUTIONS += [
        ("59-cpp-make-it-pass.md",
         lambda: edit("59-cpp-make-it-pass/clamp.hpp",
                      "if (x > hi) return x;", "if (x > hi) return hi;")),
        ("60-cpp-test-first.md",
         lambda: (write("60-cpp-test-first/list_max.hpp", CPP_LIST_MAX_FIXED),
                  write("60-cpp-test-first/test_list_max.cpp", CPP_TEST_LIST_MAX))),
        ("61-cpp-own-your-memory.md",
         lambda: write("61-cpp-own-your-memory/buffer.hpp", CPP_BUFFER_FIXED)),
        ("62-cpp-capstone.md",
         lambda: (write("62-cpp-capstone/rpn.hpp", CPP_RPN_IMPL),
                  write("62-cpp-capstone/DESIGN.md", CPP_DESIGN))),
    ]
else:
    print("ok - skipped the C++ systems course 59-62 (no ASan-capable C++ compiler)")


# The graded Rust systems course (M251): the borrow checker as compile-time
# memory safety, Result/Option, sum types -- two-sided through `jichi grade`,
# gated on `rustc` (no cargo needed).
HAVE_RUST = shutil.which("rustc") is not None
if HAVE_RUST:
    SOLUTIONS += [
        ("63-rust-make-it-pass.md",
         lambda: edit("63-rust-make-it-pass/clamp.rs",
                      "        return x; // <-- one of these lines is wrong",
                      "        return hi;")),
        ("64-rust-test-first.md",
         lambda: (write("64-rust-test-first/list_max.rs", RUST_LIST_MAX_FIXED),
                  write("64-rust-test-first/test_list_max.rs", RUST_TEST_LIST_MAX))),
        ("65-rust-the-borrow-checker.md",
         lambda: write("65-rust-the-borrow-checker/words.rs", RUST_WORDS_FIXED)),
        ("66-rust-capstone.md",
         lambda: (write("66-rust-capstone/rpn.rs", RUST_RPN_IMPL),
                  write("66-rust-capstone/DESIGN.md", RUST_DESIGN))),
    ]
else:
    print("ok - skipped the Rust systems course 63-66 (no rustc on PATH)")


# The graded PROCESS curriculum (M253): structural floors on the software-
# development process artifacts. No toolchain -- pure-sh graders -- so always
# registered (they run in the cc-gated main loop with the rest).
SOLUTIONS += [
    ("67-process-requirements.md",
     lambda: write("67-process-requirements/REQUIREMENTS.md", PROC_REQUIREMENTS)),
    ("68-process-use-cases.md",
     lambda: write("68-process-use-cases/USE_CASES.md", PROC_USECASES)),
    ("69-process-design.md",
     lambda: write("69-process-design/DESIGN.md", PROC_DESIGN)),
    ("70-process-documentation.md",
     lambda: write("70-process-documentation/README.md", PROC_README)),
    ("71-process-session-notes.md",
     lambda: (write("71-process-session-notes/notes/2026-08-01.md", PROC_NOTE_1),
              write("71-process-session-notes/notes/2026-08-02.md", PROC_NOTE_2),
              write("71-process-session-notes/notes/2026-08-03.md", PROC_NOTE_3))),
    ("72-process-kanban.md",
     lambda: write("72-process-kanban/BOARD.md", PROC_BOARD)),
    ("73-process-scheduling.md",
     lambda: write("73-process-scheduling/PLAN.md", PROC_PLAN)),
]


def main():
    if not shutil.which("cc"):
        print("ok - skipped (no cc on PATH)")
        return
    for spec, solve in SOLUTIONS:
        fresh()
        grade(spec, 1, "pristine rejected")
        solve()
        grade(spec, 0, "solution accepted")

    # Half-solutions must still fail (compound graders are not hollow).
    fresh()
    fix_07()
    grade("07-write-the-test-first.md", 1, "fix without a test rejected")
    fresh()
    fix_08()
    write("08-the-wrong-suspect/NOTES.md", NOTES)
    os.remove(path("08-the-wrong-suspect/NOTES.md"))
    grade("08-the-wrong-suspect.md", 1, "fix without the record rejected")

    # Set B's traps: the lazy checker that accepts everything (the hollow
    # gate in person), a refactor that only renames the constant, and a
    # journal that records an out-of-scope write.
    fresh()
    write("09-grade-the-grader/check.sh", CHECK_09_LAZY)
    grade("09-grade-the-grader.md", 1, "lazy happy-path checker rejected")
    fresh()
    edit("12-refactor-without-change/dur.c",
         "/* dur.c - see dur.h. The tests are green; the code still smells. */"
         '\n#include "dur.h"\n',
         "/* dur.c - see dur.h. */\n"
         '#include "dur.h"\n\n#define SECONDS_PER_DAY 86400\n')
    edit("12-refactor-without-change/dur.c",
         "    return 86400 - (h * 3600 + m * 60 + s);",
         "    return SECONDS_PER_DAY - (h * 3600 + m * 60 + s);")
    edit("12-refactor-without-change/dur.c",
         "    return d * 86400;", "    return d * SECONDS_PER_DAY;")
    grade("12-refactor-without-change.md", 1,
          "constant-only refactor (duplication kept) rejected")
    fresh()
    write("13-delegate-with-a-leash/work/report.txt",
          "delegation with verification\n")
    write("13-delegate-with-a-leash/journal.jsonl", JOURNAL_13_LEAKY)
    grade("13-delegate-with-a-leash.md", 1,
          "journal with an out-of-scope write rejected")

    # Set C's traps: the code fixed but the gate left hollow (detection is
    # the grade, not the fix), the diagnosis's symptom-patch applied
    # verbatim, a peer-task check that cannot fail, and a proposal with the
    # template's scaffolding comments still in it.
    fresh()
    fix_14_code()
    grade("14-the-hollow-gate.md", 1, "code fixed but gate still hollow rejected")
    fresh()
    fake_patch_15()
    write("15-the-confident-misdiagnosis/VERDICT.md", VERDICT_15)
    grade("15-the-confident-misdiagnosis.md", 1,
          "the diagnosis's symptom-patch rejected")
    fresh()
    solve_16()
    write("16-teach-a-peer/task/check.sh", "#!/bin/sh\nexit 0\n")
    grade("16-teach-a-peer.md", 1, "peer task with a hollow check rejected")
    fresh()
    text = open(os.path.join(ws, "docs", "assignments",
                             "17-capstone", "proposal-template.md")).read()
    write("17-capstone/portfolio/PROPOSAL.md", text + "x" * 1200)
    write("17-capstone/portfolio/journal.jsonl", JOURNAL_13)
    write("17-capstone/portfolio/RECORD.md", RECORD_17)
    grade("17-capstone.md", 1, "unfilled template proposal rejected")

    # Set D's trap (M221): a memory checker that tests only the arithmetic
    # half of the contract accepts the whole-file borrower and the leaker --
    # exactly the slope-only measurement the task exists to cure.
    fresh()
    write("22-slope-lies-keep-the-peak/check.sh", REF_CHECK_22_LAZY)
    grade("22-slope-lies-keep-the-peak.md", 1,
          "answer-only memory checker rejected")

    # Extra 23's trap: the port compiles and the table lists every construct,
    # but the long-long row claims "none" -- a cost column that never prices
    # anything is decoration.
    fresh()
    write("23-the-time-traveling-c/inventory.c", INVENTORY_23)
    write("23-the-time-traveling-c/PORT.md", PORT_23_FREE)
    grade("23-the-time-traveling-c.md", 1,
          "port with a cost-free long-long row rejected")

    # Extra 29's trap: the account is written but the code still overflows a
    # signed int -- accounting for a bug you did not fix is hollow. Only where
    # a UBSan-capable compiler exists (same gate as the reference proof).
    if HAVE_UBSAN:
        fresh()
        write("29-works-on-my-machine/fold.c", FOLD_29_STILL_BROKEN)
        write("29-works-on-my-machine/ACCOUNT.md", ACCOUNT_29)
        grade("29-works-on-my-machine.md", 1,
              "account written but the UB never fixed rejected")

    # Extra 30's trap: "made it portable" by forcing the signed reading
    # everywhere -- both builds now agree, but on the WRONG answer (-1, not
    # 1023). Portable is not the bar; portable-AND-correct is.
    if HAVE_CHAR_FLAGS:
        fresh()
        write("30-the-signed-byte/bytesum.c", BYTESUM_30_WRONG)
        write("30-the-signed-byte/ACCOUNT.md", ACCOUNT_30)
        grade("30-the-signed-byte.md", 1,
              "portable-but-wrong (forced signed) rejected")

    # Extra 24's trap: the fix without the proof-test -- reading without
    # conviction. (The reverse trap, a test green on both builds, is
    # structurally impossible to sneak past: the runner compiles the test
    # against the as-found snapshot itself.)
    fresh()
    fix_24()
    write("24-read-a-real-project/ANALYSIS.md", ANALYSIS_24)
    grade("24-read-a-real-project.md", 1, "fix without the proof-test rejected")

    # The Racket course's traps (M238): a hollow test suite that passes but
    # leaves the bug unfixed (32), mutation disguised as a box (33), and a
    # working capstone whose design note names nothing (34).
    if HAVE_RACKET:
        fresh()
        write("32-racket-test-first/test-list-max.rkt", RKT_TEST_LIST_MAX_HOLLOW)
        grade("32-racket-test-first.md", 1,
              "hollow tests that leave the bug unfixed rejected")
        fresh()
        write("33-racket-loops-to-folds/squares.rkt", RKT_SQUARES_BOX)
        grade("33-racket-loops-to-folds.md", 1,
              "mutation disguised as a box rejected")
        fresh()
        write("34-racket-capstone/rpn.rkt", RKT_RPN_IMPL)
        write("34-racket-capstone/DESIGN.md", "done.\n")
        grade("34-racket-capstone.md", 1,
              "capstone with a design note that names nothing rejected")

    # The Guile course's traps (M244): the same three failure modes, in Guile
    # -- a hollow suite that passes but leaves the bug unfixed (36), mutation
    # disguised behind a correct result (37), and a working capstone whose
    # design note names nothing (38).
    if HAVE_GUILE:
        fresh()
        write("36-guile-test-first/test-list-max.scm", GUILE_TEST_LIST_MAX_HOLLOW)
        grade("36-guile-test-first.md", 1,
              "hollow tests that leave the bug unfixed rejected")
        fresh()
        write("37-guile-loops-to-folds/squares.scm", GUILE_SQUARES_MUT)
        grade("37-guile-loops-to-folds.md", 1,
              "mutation disguised behind a correct result rejected")
        fresh()
        write("38-guile-capstone/rpn.scm", GUILE_RPN_IMPL)
        write("38-guile-capstone/DESIGN.md", "done.\n")
        grade("38-guile-capstone.md", 1,
              "capstone with a design note that names nothing rejected")

    # The Elixir course's traps (M245): the same three failure modes on the BEAM
    # -- a hollow suite that passes but leaves the bug unfixed (40), manual
    # recursion disguised behind a correct result + a spurious Enum call (41),
    # and a working capstone whose design note names nothing (42).
    if HAVE_ELIXIR:
        fresh()
        write("40-elixir-test-first/test_list_max.exs",
              ELIXIR_TEST_LIST_MAX_HOLLOW)
        grade("40-elixir-test-first.md", 1,
              "hollow tests that leave the bug unfixed rejected")
        fresh()
        write("41-elixir-loops-to-folds/squares.exs", ELIXIR_SQUARES_RECUR)
        grade("41-elixir-loops-to-folds.md", 1,
              "manual recursion disguised behind a spurious Enum rejected")
        fresh()
        write("42-elixir-capstone/rpn.exs", ELIXIR_RPN_IMPL)
        write("42-elixir-capstone/DESIGN.md", "done.\n")
        grade("42-elixir-capstone.md", 1,
              "capstone with a design note that names nothing rejected")

    # The Haskell course's traps (M246): the same three failure modes with a
    # static type system -- a hollow suite that passes but leaves the bug
    # unfixed (44), manual recursion disguised behind a spurious sum call (45),
    # and a working capstone whose design note names nothing (46).
    if HAVE_HASKELL:
        fresh()
        write("44-haskell-test-first/TestListMax.hs",
              HASKELL_TEST_LIST_MAX_HOLLOW)
        grade("44-haskell-test-first.md", 1,
              "hollow tests that leave the bug unfixed rejected")
        fresh()
        write("45-haskell-loops-to-folds/Squares.hs", HASKELL_SQUARES_RECUR)
        grade("45-haskell-loops-to-folds.md", 1,
              "manual recursion disguised behind a spurious sum rejected")
        fresh()
        write("46-haskell-capstone/Rpn.hs", HASKELL_RPN_IMPL)
        write("46-haskell-capstone/DESIGN.md", "done.\n")
        grade("46-haskell-capstone.md", 1,
              "capstone with a design note that names nothing rejected")

    # The Clojure course's traps (M247): the same three failure modes on the JVM
    # -- a hollow suite that passes but leaves the bug unfixed (48), the atom
    # (Clojure's set!) disguised behind a correct reduce/map/filter result (49),
    # and a working capstone whose design note names nothing (50).
    if HAVE_CLOJURE:
        fresh()
        write("48-clojure-test-first/test_list_max.clj",
              CLOJURE_TEST_LIST_MAX_HOLLOW)
        grade("48-clojure-test-first.md", 1,
              "hollow tests that leave the bug unfixed rejected")
        fresh()
        write("49-clojure-loops-to-folds/squares.clj", CLOJURE_SQUARES_ATOM)
        grade("49-clojure-loops-to-folds.md", 1,
              "atom mutation disguised behind a correct reduce rejected")
        fresh()
        write("50-clojure-capstone/rpn.clj", CLOJURE_RPN_IMPL)
        write("50-clojure-capstone/DESIGN.md", "done.\n")
        grade("50-clojure-capstone.md", 1,
              "capstone with a design note that names nothing rejected")

    # The C systems course's traps (M248): a hollow suite that passes but never
    # grows past the initial capacity while the bug is unfixed (52), a "fix" that
    # routes sprintf through a big temp buffer but keeps the unbounded call (53),
    # and a working arena whose design note names nothing (54).
    if HAVE_ASAN:
        fresh()
        write("52-the-array-that-outgrew-itself/test_ivec.c",
              TEST_IVEC_52_HOLLOW)
        grade("52-the-array-that-outgrew-itself.md", 1,
              "test that never grows past capacity leaves the overflow -- rejected")
        fresh()
        write("53-never-call-sprintf/fmt.c", FMT_53_TEMP_SPRINTF)
        grade("53-never-call-sprintf.md", 1,
              "sprintf hidden behind a temp buffer rejected")
        fresh()
        write("54-the-arena/arena.c", ARENA_54_IMPL)
        write("54-the-arena/DESIGN.md", "done.\n")
        grade("54-the-arena.md", 1,
              "capstone with a design note that names nothing rejected")

    # The Zig course's traps (M249): a hollow suite that never tests an
    # all-negative slice while the bug is unfixed (56), and a working capstone
    # whose design note names nothing (58). (Task 57 has no hand-written guard --
    # the leak-detecting test allocator cannot be fooled.)
    if HAVE_ZIG:
        fresh()
        write("56-zig-test-first/test_list_max.zig", ZIG_TEST_LIST_MAX_HOLLOW)
        grade("56-zig-test-first.md", 1,
              "hollow tests that never hit the all-negative case rejected")
        fresh()
        write("58-zig-capstone/rpn.zig", ZIG_RPN_IMPL)
        write("58-zig-capstone/DESIGN.md", "done.\n")
        grade("58-zig-capstone.md", 1,
              "capstone with a design note that names nothing rejected")

    # The C++ course's traps (M250): a hollow suite that never tests an
    # all-negative vector while the bug is unfixed (60), a destructor-with-delete
    # that fixes the LEAK but keeps raw new/delete -- LeakSanitizer is quiet, only
    # the grep catches it (61), and an empty design note (62).
    if HAVE_CXX_ASAN:
        fresh()
        write("60-cpp-test-first/test_list_max.cpp", CPP_TEST_LIST_MAX_HOLLOW)
        grade("60-cpp-test-first.md", 1,
              "hollow tests that never hit the all-negative case rejected")
        fresh()
        write("61-cpp-own-your-memory/buffer.hpp", CPP_BUFFER_DTOR)
        grade("61-cpp-own-your-memory.md", 1,
              "a delete[] destructor (no leak, but still raw new/delete) rejected")
        fresh()
        write("62-cpp-capstone/rpn.hpp", CPP_RPN_IMPL)
        write("62-cpp-capstone/DESIGN.md", "done.\n")
        grade("62-cpp-capstone.md", 1,
              "capstone with a design note that names nothing rejected")

    # The Rust course's traps (M251): a hollow suite that never tests an
    # all-negative slice while the bug is unfixed (64), and an empty design note
    # (66). (Task 65 has no hand-written guard -- the borrow checker cannot be
    # fooled into compiling a dangling reference.)
    if HAVE_RUST:
        fresh()
        write("64-rust-test-first/test_list_max.rs", RUST_TEST_LIST_MAX_HOLLOW)
        grade("64-rust-test-first.md", 1,
              "hollow tests that never hit the all-negative case rejected")
        fresh()
        write("66-rust-capstone/rpn.rs", RUST_RPN_IMPL)
        write("66-rust-capstone/DESIGN.md", "done.\n")
        grade("66-rust-capstone.md", 1,
              "capstone with a design note that names nothing rejected")

    # The process curriculum's traps (M253): each artifact has the SHAPE but not
    # the substance the structural floor requires.
    fresh()
    write("67-process-requirements/REQUIREMENTS.md", PROC_REQUIREMENTS_HOLLOW)
    grade("67-process-requirements.md", 1,
          "five requirement ids but no verifiable phrasing rejected")
    fresh()
    write("68-process-use-cases/USE_CASES.md", PROC_USECASES_NOFAIL)
    grade("68-process-use-cases.md", 1,
          "use-cases with only happy paths (no failure path) rejected")
    fresh()
    write("69-process-design/DESIGN.md", PROC_DESIGN_PARTIAL)
    grade("69-process-design.md", 1,
          "design that leaves a requirement untraced rejected")
    fresh()
    write("70-process-documentation/README.md", PROC_README_NOEX)
    grade("70-process-documentation.md", 1,
          "README with no worked example rejected")
    fresh()
    write("71-process-session-notes/notes/2026-08-01.md", PROC_NOTE_NONEXT % 1)
    write("71-process-session-notes/notes/2026-08-02.md", PROC_NOTE_NONEXT % 2)
    write("71-process-session-notes/notes/2026-08-03.md", PROC_NOTE_NONEXT % 3)
    grade("71-process-session-notes.md", 1,
          "dated notes missing the 'next' spine rejected")
    fresh()
    write("72-process-kanban/BOARD.md", PROC_BOARD_UNTRACED)
    grade("72-process-kanban.md", 1,
          "a Doing card that traces to no requirement rejected")
    fresh()
    write("73-process-scheduling/PLAN.md", PROC_PLAN_NORETRO)
    grade("73-process-scheduling.md", 1,
          "sized milestones but no estimate-vs-actual retro rejected")

    # --- the plain-register tier (M309) ---------------------------------------
    # For readers the dense prose excludes (docs/PLAIN_LANGUAGE.md). Plain language
    # is NOT easier marking: p2 also asserts the file it told you to READ was left
    # alone, and p3 asserts an exact line count, because "the file quietly gained a
    # blank line" is the surprise it exists to teach.
    fresh()
    grade("p1-ask-for-a-file.md", 1, "no file yet rejected")
    write("p1-ask-for-a-file/note.txt", "I asked and it wrote this line\n")
    grade("p1-ask-for-a-file.md", 0, "the exact line accepted")
    write("p1-ask-for-a-file/note.txt", "I asked and it wrote this line extra\n")
    grade("p1-ask-for-a-file.md", 1, "a longer line rejected")

    fresh()
    grade("p2-find-the-answer.md", 1, "placeholder answer rejected")
    write("p2-find-the-answer/answer.txt", "timeout = 30\n")
    grade("p2-find-the-answer.md", 0, "the copied timeout line accepted")
    edit("p2-find-the-answer/settings.txt", "timeout = 30", "timeout = 99")
    grade("p2-find-the-answer.md", 1, "editing the read-only source rejected")

    fresh()
    grade("p3-change-one-line.md", 1, "unchanged speed rejected")
    edit("p3-change-one-line/notes.txt", "50 steps", "80 steps")
    grade("p3-change-one-line.md", 0, "the one-line change accepted")
    write("p3-change-one-line/notes.txt",
          "Line one must not change.\nThe speed is 80 steps.\n"
          "Line three must not change.\n\n")
    grade("p3-change-one-line.md", 1, "a stray trailing blank line rejected")

    shutil.rmtree(ws, ignore_errors=True)
    print("ok: curriculum sets A+B+C+D + Racket + Guile + Elixir + Haskell + "
          "Clojure + C-systems + Zig + C++ + Rust + PROCESS + PLAIN -- every grader "
          "two-sided through `jichi grade` (M174b/M176/M177/M221/M228/M229/"
          "M238/M244/M245/M246/M247/M248/M249/M250/M251/M253/M309)")


main()
