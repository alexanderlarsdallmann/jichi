# Review 10 — The tests and the gates: how jichi decides a change may merge

*Part of the 2026-10-08 review of jichi at master `17bbb895` (M913); start at
[Review 0](2026-10-08-review-00-overview.md). This chapter rests on the complete
report for the build and the tests:

- the Makefile, the test harness and the four test tools, all in full;
- six unit-test files in full, and the rest by structure;
- about thirty smoke drivers, including every compaction, context, subagent,
  parallel, supervisor and envelope driver.

Its six excerpts were checked line for line against the source. Written by
Claude Opus 5.5 on threadwork, 2026-10-08.*

---

## Who this is for, and what you will be able to do

**For** a self-learner or junior developer who wants to see how a serious C
project tests itself, and how it guards against the most common way tests lie:
passing without having checked anything.

**After reading it you will be able to:**

- name jichi's test tiers and what each one can and cannot catch;
- read `make ci`'s stages and say why each exists;
- prove that a test you wrote can fail, using the project's own tools;
- recognise five ways a test can be vacuous, each with the incident that taught
  it.

**What you need first:** [Annai 9](../reading/annai-09-how-jichi-knows-it-works.md)
and [Fukabori 10](../reading/fukabori-10-the-test-architecture-as-a-system.md),
then [TESTING_RUNBOOK.md](../TESTING_RUNBOOK.md) when you write a test.

---

## 1. The build and the tiers

**The build.** One GNU Makefile of 1,644 lines (863 of them comments) builds
`jichi` and `jichi-convert` from about 300 C89 files.

**Before any rule runs, it probes the toolchain.** A probe compiles a tiny
program and asks one question, such as:

- can the platform's headers be parsed in strict C89?
- is `snprintf` declared?
- does `clock_gettime` need `-lrt`?
- does this compiler accept a given warning flag silently?

Each answer becomes a `-D` macro or a flag. The lesson running through those
comments is that **a probe must ask exactly the question the real build will
ask, under the same flags** (§3.2).

**The test tiers:**

| Tier | Run with | What it is | Today |
|---|---|---|---|
| unit | `make test` | 137 `tests/test_*.c` files, 194 test functions in one runner | 15,087 checks in 6.5 s |
| smoke | `make smoke` | 424 POSIX-sh drivers, each running the real binary offline against a mock model | the bulk of the gate's time |
| e2e | `make e2e` | a small Python remainder; skips without python3 | — |
| lints | inside smoke | drivers that read source or docs instead of running jichi | "prefer a lint to an audit" |

**The four test tools** (`tests/tools/`, C89, test-only):

- `mockmodel`: a scripted HTTP and SSE fake model server that saves every
  request it receives as `req.N`;
- `ptydrive`: runs a program on a pseudo-terminal, expect-style;
- `jsonq`: a JSON query tool;
- `sockq`: a one-shot client for jichi's control sockets.

**The harness's verbs never abort.**

- `JC_CHECK` records a result and continues.
- `JC_REQUIRE` records it and *returns* it, so a test can branch (§3.1).

A run ends with "N checks, M failures", and the exit code is 0 only when M is 0.

---

## 2. The gate

`make ci` is the only merge gate (`Makefile:1338-1438`). It runs these stages in
order, and the first failing line stops it:

```
 gcc -Werror test --> clang -Werror test --> g++ (C++) build --> cpp-check
      |
      v
 clang ASan/UBSan test --> leakcheck (7 subcommands under LSan)
      --> leak_turn (one real agent turn under ASan, keep-alive mock)
      |
      v
 gcc test under valgrind --> fuzz, 2,000 iterations (ASan) --> gcc -Werror all
      --> curl-free unit run under `ulimit -s 512` --> an over-long TMPDIR (rc 2)
      --> an unwritable TMPDIR (completes, writes nothing) --> fallback compile
      |
      v
 smoke-faults (FAULT=1: 5 drivers + units) --> gcc -Werror all
      --> examples --> smoke (424 drivers) --> smoke-mutant --> e2e --> "ci: OK"
```

**Why so many stages?** Each catches a class the others cannot:

- **two compilers at `-Werror`:** each warns about different things;
- **a C++ compiler:** C code that is not valid C++ (headers are included by the
  C++ examples);
- **sanitizers:** memory errors and undefined behaviour;
- **valgrind:** what the sanitizers' build changes;
- **a tiny stack:** recursion and large locals;
- **odd TMPDIRs:** paths the tests assume;
- **a fault-injection build:** the error paths;
- **the smoke tier:** the real binary;
- **`smoke-mutant`:** tests that pass without checking anything (§4).

**Its cost.** It took 17.6 minutes at 329 drivers (ROADMAP). Today's tier has
424 drivers, and the current runtime was not measured by this review.

---

## 3. Code to study

Six excerpts, each copied exactly from `17bbb895`.

### 3.1 A test verb that is an expression — `tests/jc_test.h:29-51`

```c
/* JC_REQUIRE - record a check exactly like JC_CHECK, and YIELD ITS RESULT so the
 * caller can branch:
 *
 *     if (JC_REQUIRE(p != NULL)) {
 *         JC_CHECK(p->field == x);      / * only reached when p is real * /
 *     }
 *
 * This exists because JC_CHECK deliberately RECORDS AND CONTINUES, so it is not
 * a guard -- and a null check written as one is a SIGSEGV waiting for the day
 * the environment changes. M265 fixed that shape once (a test that segfaulted
 * on git < 2.5); M452 hit it again on an Android tablet with no /tmp, where a
 * fixture was never written and the suite ABORTED AT THE 4TH OF 123 TEST FILES,
 * reporting nothing about the other ~119. An audit then found 19 sites across 8
 * files, which is why this is a harness verb rather than 19 hand-written ifs:
 * the idiom is now one line, greppable, and the same everywhere.
 *
 * Use it ONLY in a condition. It is an expression, not a statement, so a bare
 * `JC_REQUIRE(x);` discards its value and reads like a guard while guarding
 * nothing -- the precise mistake it exists to prevent. */
#define JC_REQUIRE(cond) \
    (jc_test_checks++, (cond) ? 1 : \
        (jc_test_fails++, \
         printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond), 0))
```

**What it teaches.** C89's **comma operator** lets a macro both *do* things (count
the check, print a failure) and *yield a value*. So `if (JC_REQUIRE(p != NULL))
{ … }` checks and branches in one greppable idiom.

**Why a guard matters here.** A failed `JC_CHECK(p != NULL)` records the failure
and continues to dereference the null pointer, and one segmentation fault ends
the whole suite. That once aborted the suite at the 4th of 123 files, reporting
nothing about the other 119.

**Read the last paragraph twice.** A bare `JC_REQUIRE(x);` as a statement
compiles, looks like a guard, and guards nothing. The tree contains 17 of them
(finding F1). A convention a macro cannot enforce needs a lint.

### 3.2 A probe that asks the build's own question — `Makefile:192-211`

```makefile
# M833 (the DJGPP step 0, plans/2026-09-29-the-fork-less-rows.md section 5): a flag
# is accepted only when the compiler takes it SILENTLY -- exit 0 and no output on
# either stream. DJGPP's gcc answers -fPIE with "-fPIC ignored (not supported for
# DJGPP)" on STDOUT and exit 0, even under -Werror; this probe used to discard only
# stderr, so the sentence became part of HARDENFLAGS and every compile died on a
# shell syntax error at "(". A flag the compiler says it ignores is not a flag worth
# passing, so silence is the test. Every feature probe below discards the
# compiler's stdout for the same reason (only the probe's own `echo yes` may reach
# the captured value). tests/smoke/probe_quiet_lint.sh holds both, red-first.
cc_warn_ok = $(shell o=$$(printf 'int main(void){return 0;}\n' \
  | $(CC) -Werror $(1) -xc - -o $(PROBE_OUT) 2>&1) && [ -z "$$o" ] && echo $(1); rm -f $(PROBE_OUT) $(PROBE_OUT).exe)

# -Wvla is probed as well since the userland ladder (2026-09-24): gcc gained it in
# 4.3, and Debian 4's gcc 4.1.2 rejected all 171 compiles on it -- the M722 defect
# again, one flag older. Every compiler that knows it keeps the tripwire.
WARN_OPTIONAL := $(call cc_warn_ok,-Wlogical-op) \
                 $(call cc_warn_ok,-Wduplicated-cond) \
                 $(call cc_warn_ok,-Wjump-misses-init) \
                 $(call cc_warn_ok,-Walloca) \
                 $(call cc_warn_ok,-Wvla)
```

**What it teaches.** A flag is accepted only when the compiler takes it
**silently**: exit 0, and no output on either stream, under `-Werror`. Each half
was learned from a real failure:

- **`-Werror`:** clang merely *warns* about a flag it does not know, so only
  `-Werror` reveals that the flag is unsupported;
- **no output on stdout:** DJGPP's gcc printed "ignored" on *stdout* with exit
  0, and that sentence became part of every compile command.

`$(call …)` is GNU make's user-defined function, used here so one probe serves
many flags.

### 3.3 Completion decided by a parser, never by a pause — `tests/tools/mm_core.c:973-993`

```c
        if (h->head_end == 0)
            return h->state;    /* still inside the head */
        if (mm_http_is_chunked(h->buf, h->head_end)) {
            h->state = MM_HTTP_ERROR;
            return h->state;
        }
        h->content_length = mm_http_header_num(h->buf, h->head_end,
                                               "content-length");
    }

    /* COMPLETE only when the declared body has fully arrived. A missing
     * Content-Length means no body (GET): complete at end-of-head. This is
     * the invariant that makes the ANECDOTES #18 truncation bug impossible:
     * a pause in the byte stream can only ever yield NEED_MORE. */
    if (h->content_length < 0) {
        h->state = MM_HTTP_COMPLETE;
    } else if (h->len >= h->head_end + (size_t)h->content_length) {
        h->state = MM_HTTP_COMPLETE;
    }
    return h->state;
}
```

**What it teaches.** The mock's receive loop can only *feed* bytes in; only this
function can declare a request complete, after the headers and after
`Content-Length` bytes of body.

**Why that matters.** An earlier reader stopped at the first pause in the byte
stream. It truncated requests, which produced phantom regressions (ANECDOTES
#18). That bug cannot be written on top of this design: it was made
*unrepresentable*, not merely avoided.

The function is pure (no sockets), so it is unit-tested, and the I/O shell
around it stays thin.

### 3.4 Proving a check has teeth — `tests/teeth.sh:73-97`

```sh
# --- baseline: the command must be GREEN on the unperturbed file --------------
# Run it before touching anything. Without this, an unrunnable command "proves"
# the teeth of every check it never executed.
if ! "$@" > /dev/null 2>&1; then
    echo "teeth: VACUOUS -- the command is already failing BEFORE the" >&2
    echo "  perturbation, so its red proves nothing about $file." >&2
    echo "  Fix the command first (note argv is exec'd, not shell-parsed:" >&2
    echo "  pass 'sh tests/smoke/x.sh' as two arguments, not one)." >&2
    exit 2
fi

if ! sed "$expr" "$bak" > "$file"; then
    echo "teeth: sed expression failed" >&2
    exit 2
fi
if cmp -s "$bak" "$file"; then
    echo "teeth: VACUOUS -- '$expr' changed nothing in $file (a no-op proves nothing)" >&2
    exit 2
fi

if "$@"; then
    echo "teeth: TOOTHLESS -- the command stayed green under the perturbation of $file" >&2
    exit 1
fi
echo "teeth: OK -- the perturbation of $file was caught (the command failed as demanded)"
```

**What it teaches.** "Revert the fix and see the test fail" sounds simple, and
needs three refusals to mean anything:

1. **The command must be green before the change.** Otherwise its red proves
   nothing.
2. **The perturbation must actually change the file.** A `sed` that matched
   nothing "proves" every check.
3. **Only then does a red count.**

The restore runs from traps on EXIT, HUP, INT, PIPE and TERM, so an interrupted
ritual cannot leave the sabotage in the tree. Its remaining gap (F12): "red"
means any non-zero exit, including a crash for an unrelated reason.

### 3.5 Prove the instrument fires before reading its verdict — `tests/smoke/leak_turn.sh:70-81`

```sh
t_plan 7

# ---- 1: the instrument fires ------------------------------------------------
ASAN_OPTIONS="detect_leaks=1:exitcode=0" "$tmp/leaker" > /dev/null 2> "$tmp/leaker.err"
if grep -q "LeakSanitizer: detected memory leaks" "$tmp/leaker.err"; then
    t_ok "the sanitizer runtime reports a planted 64-byte leak"
else
    t_fail "a planted, deliberately-unreachable 64-byte allocation was NOT reported:
$(head_bytes 300 < "$tmp/leaker.err")
Leak detection is not armed on this host, so check 4 below could not distinguish
a clean run from a disabled checker. Fix the environment, not the floor."
fi
```

**What it teaches.** "No leak reported" is evidence only if the leak detector is
known to work *on this host*, and on some platforms it is off by default.

The driver therefore checks in order:

1. **The detector works.** It compiles a program that leaks on purpose and
   demands that the leak be reported.
2. **The work happened.** Two requests reached the mock, and the answer reached
   stdout.
3. **Only then the verdict.** It reads whether jichi itself leaked.

This is the "floors first" pattern several of the project's vacuous-green
incidents taught: a check that cannot fail tells you nothing.

### 3.6 A performance property, asserted as a count — `tests/test_compact.c:1352-1374`

```c
    /* 120 rounds of the loop: a tool call and a sub-threshold result each. */
    for (i = 0; i < 120; i++) {
        a = jc_history_add(&h, JC_ROLE_ASSISTANT, "r");
        jc_msg_add_tool_call(a, "t", "run_tests", "{}");
        jc_history_add_tool_result(&h, "t", small, 0);
        jc_compact_midturn(&app, &h, NULL, &rep, 0, &latch);
        JC_CHECK(rep.pressed == 1);      /* the pressure is real throughout */
        if (!rep.latched) {
            scans++;
        }
    }

    /* MEASURED, both ways, on this very loop: before M712 the horizon expired
```

**What it teaches.** "This pass is faster now" is hard to test with a clock: a
timing test is flaky on a loaded machine. But "the expensive scan ran once
instead of thirty times" is a *count*, and a count is exact and repeatable.

**Replay the measured shape.** The test reproduces the workload that was measured
in the field: 120 rounds, each with one small tool result. It counts the scans,
and asserts both that the pressure stayed real throughout and that the work was
skipped.

*Lesson:* turn a performance claim into a counted property of the algorithm.

---

## 4. Five ways a test can lie, and jichi's answer to each

| The lie | Example | jichi's answer |
|---|---|---|
| the check never ran | a driver that exits early still reports "ok" | the runner compares the plan count with the ok count |
| the check cannot fail | an assertion whose condition is always true | `teeth.sh`; `make smoke-mutant` runs each changed driver against a binary that prints nothing |
| the instrument is off | a leak checker disabled on this host | the planted-failure check first (§3.5) |
| the fixture is not the real peer | a mock that accepts any request | TESTING_RUNBOOK §3c; periodic runs against a real free model |
| the universe is not the one you meant | a lint that scans the wrong files | "audit the universe": enumerate the set a second way and compare |

The incidents behind each row are in [TEST_INTEGRITY.md](../TEST_INTEGRITY.md)
and [GATE_INTEGRITY.md](../GATE_INTEGRITY.md). They are among the most useful
pages in the tree for a junior developer, because they show the failure *before*
the rule.

---

## 5. What the review found

Thirteen findings, all low or low-medium. **The gate itself is sound.** What the
reviewer found are rules written in prose that no check holds.

- **F1 — `JC_REQUIRE` is used as a bare statement 17 times** (§3.1), and
  check-then-dereference remains in four test files. All 17 came in M909–M913,
  in commits by sessions of jichi itself on `jlu/qwen3.8-27b`. A regression
  there would crash the whole unit suite instead of reporting one failure.
  *Fix:* a lint.
- **F2 — fourteen smoke drivers run `jichi doctor` without isolating HOME.** Run
  by hand, as TESTING_RUNBOOK.md shows, they send the developer's configured keys
  to the configured servers (security class; a canary key captured six requests).
  Through the runner they are isolated, so the gate itself is not affected.
- **F3** — each unit run leaves 29 entries in TMPDIR, and two of them grow with
  every run.
- **F4 — a skipped driver counts as a pass.** 63 drivers can skip, and "ci: OK"
  still lists them as passing.
- **F5** — `make install`'s dirty-tree check reads a stamp that other targets
  regenerate without relinking `jichi`. Reproduced: the binary said
  `aaaa111-dirty` while the header said `bbbb222`.
- **F6** — three drivers never call `t_done`, so their exit status says nothing.
- **F7** — `mockmodel` leaks every tool after the first in a rule with several
  (found with ASan).
- **F8** — two unit tests pass a TMPDIR-derived string as the *format* of
  `jc_snprintf`. A `%` in TMPDIR would be read as a conversion.
- **F10** — every `mockmodel` start sleeps about a second, about 390 s per smoke
  run (estimated).
- **F11** — `make ci` builds `all` twice under identical flags.
- **F12** — `teeth.sh` accepts any non-zero exit as "the check went red".
- **A fourteenth, found while writing this chapter** (§8): no unit test pins the
  mid-turn elision threshold `ELIDE_MIN_BYTES` at its boundary. Changing it from
  800 to 801 leaves all 15,087 checks green. Measured with `teeth.sh` in an
  exported copy.

---

## 6. The design, seen whole

**Four observations from the reviewer, each a lesson:**

- **The prose is the institutional memory, and it is heavy.** 52% of the
  Makefile is comments, and they are accurate: each probe says which platform
  taught it. But about 20 probes repeat one shape by hand, and two have drifted
  (one misses a flag the others pass). *Lesson:* when a comment explains why
  twenty copies must be identical, make them one function.
- **The unit runner is a hand-written list** of about 191 calls. It takes no
  arguments, so one test cannot be run alone, timed, or checked for order
  dependence. *The refactor to do first:* a `{name, function}` table, about 200
  mechanical lines, whose proof is that the total check count stays exactly
  15,087.
- **Unit tests share one process, one HOME, one working directory.** It works
  today, but it is the main hazard for a new test that touches a HOME-based
  store (sessions, tool output, trust, telemetry).
- **The agent loop is tested only against the mock.** The mock reports whatever
  usage its script says, and never refuses a request for its size unless told
  to. So "the request really overflowed a real window" is never exercised in the
  gate, by design, because the gate is offline. *Lesson:* an offline gate needs
  an online twin, run periodically against a free local model.

---

## 7. What this means for the context relay

**What is covered today:** compaction, the mid-turn elision, the latch, the
`[context]` note, the subagent caps, budgets and their notices.

**The gaps the relay would land in** (the reviewer's list):

1. A *top-level* run refused with a context-window 400: no driver asserts its
   stop reason, exit code, journal or session, and no recovery path exists.
2. Compaction triggered by server-reported usage that grows over a long loop.
   Every pressure driver forces pressure with a tiny limit and byte estimates
   instead.
3. Context pressure inside a subagent or parallel child.
4. `/compact`: it has no test in any tier.
5. A summarizer failure other than "too large".
6. The "could NOT … write durable results now" note reaching the model, end to
   end.
7. Notes carried from one session into a fresh one.

**What `mockmodel` can already do for a relay test:**

- report growing prompt sizes, with one scripted `usage` value per request;
- refuse with an overflow 400;
- serve several jichi processes in turn, saving every request.

**What it would need:** usage that grows from the request itself (`usage grow
BASE STEP`), so pressure follows the history without a script that knows the
request count in advance. The relay proposal's test plan
([§10](../proposals/2026-10-context-relay.md)) is built on this.

---

## 8. Something to do

**With a bench (an exported copy you may change, never a working tree you
care about):** run the teeth ritual three times on one constant. In a copy made
with `git archive HEAD | tar -x -C <dir>`:

```sh
tests/teeth.sh src/chat/jc_compact.c 's/#define ELIDE_MIN_BYTES     800/#define ELIDE_MIN_BYTES     801/' make test
tests/teeth.sh src/chat/jc_compact.c 's/#define ELIDE_MIN_BYTES     800/#define ELIDE_MIN_BYTES     8000/' make test
tests/teeth.sh src/chat/jc_compact.c 's/#define ELIDE_MIN_BYTES     900/#define ELIDE_MIN_BYTES     901/' make test
```

What they printed on threadwork on 2026-10-08, at `17bbb895`:

| Perturbation | `teeth.sh` said | Exit |
|---|---|---|
| 800 to 801 | `TOOTHLESS -- the command stayed green` (15,087 checks, 0 failures) | 1 |
| 800 to 8000 | `OK -- the perturbation … was caught`, with six `FAIL tests/test_compact.c:…` lines | 0 |
| 900 to 901 | `VACUOUS -- … changed nothing` (the expression matched no line) | 2 |

**The first line is a finding, found by this exercise.** No unit test pins the
elision threshold at its boundary: a result of exactly 801 bytes is elided under
one constant and kept under the other, and the suite cannot tell. A boundary test
would feed results of 800 and 801 bytes and assert which one is elided. Whether a
smoke driver pins it was not checked here.

**Without a bench (the read-only twin):**

1. In §3.1, explain in one sentence why `JC_REQUIRE(p != NULL);` as a statement
   compiles, and why it guards nothing.
2. In §4's table, pick one row and find the incident behind it in
   TEST_INTEGRITY.md. What would the test have reported without the answer?
3. In §2's diagram, which stage would catch a function that is valid C89 but
   uses `class` as a variable name? Why is that a defect for jichi at all?

## 9. Read further

- [TESTING_RUNBOOK.md](../TESTING_RUNBOOK.md): the ten steps for adding a test.
  *Primary for jichi.*
- [TEST_INTEGRITY.md](../TEST_INTEGRITY.md), [GATE_INTEGRITY.md](../GATE_INTEGRITY.md),
  [TEST_TIERS.md](../TEST_TIERS.md): the doctrine and its incidents. *Primary for
  jichi.*
- [Fukabori 10](../reading/fukabori-10-the-test-architecture-as-a-system.md): the
  tiers as a system. *Primary for jichi.*
- The TAP specification, *Test Anything Protocol* version 13: the format the
  smoke drivers print. *Primary.*
- Richard A. DeMillo, Richard J. Lipton and Frederick G. Sayward, "Hints on Test
  Data Selection: Help for the Practicing Programmer", *IEEE Computer*, 1978:
  the origin of mutation testing, the idea behind `teeth.sh` and
  `smoke-mutant`. *Primary.*
