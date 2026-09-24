# The corpus pilot: six runs on zigodot, and the tool that said the code was not there

*2026-09-23, M714. The pilot of the drive that plan D1 needs — a corpus of real
agent turns on builds that already carry M432, made on the development bench
because the machine that holds one was not available (plan D1, step 0). Six runs,
two free models, three tasks each. It was meant to prove the harness. It also found
a defect in `search_code` that is fixed in the same milestone, reproduced D1's loop
twice on the current build, corrected a number M713 had just published, and left
two findings open. Written for a self-learner who wants to see what a pilot is for,
and for the developer who will run the full drive. Every figure below comes from the
run's own records; §9 has the commands.*

---

## Contents

0. [The answer first](#0-the-answer-first)
1. [The setup, and why each piece is there](#1-the-setup-and-why-each-piece-is-there)
2. [The six runs](#2-the-six-runs)
3. [search_code answered "(no matches)" for code that was there](#3-search_code-answered-no-matches-for-code-that-was-there)
4. [D1's loop, twice, on the current build](#4-d1s-loop-twice-on-the-current-build)
5. [A green gate over a weakened test file](#5-a-green-gate-over-a-weakened-test-file)
6. [Under --auto, a command's stderr is copied onto jichi's own](#6-under---auto-a-commands-stderr-is-copied-onto-jichis-own)
7. [The bench: uutils timeout, and the number M713 got wrong](#7-the-bench-uutils-timeout-and-the-number-m713-got-wrong)
8. [What I got wrong during the pilot](#8-what-i-got-wrong-during-the-pilot)
9. [Reproduce it yourself](#9-reproduce-it-yourself)
10. [What this page does not claim](#10-what-this-page-does-not-claim)

---

## 0. The answer first

- **The harness works.** Six runs, every artifact where it belongs: the jsonl stream,
  the journal, telemetry at the `full` tier, the diff each run left, one line of
  `runs.tsv`. Read tasks took 16 s to 6 min; a run that reached the 200-call cap,
  14 min. The full drive can be sized from that.
- **`search_code` was misreading the regex dialect models write** — and that is the
  finding that reordered the plan. It ran grep in POSIX *basic* mode, where `|` and
  `+` are literal and `\(` opens a group. Re-running the pilot's 45 searches against
  the same tree: **11 false "(no matches)" and 1 spurious error — 27 % of all
  searches told the model something false.** Fixed in M714 (`grep -rnE`), with
  checks that were red first. The operator chose to fix it before the full drive, so
  D1's thresholds are fitted to the behaviour after the fix, not to its artifact.
- **D1's loop reproduced twice, on the current build.** Two of six runs reached the
  200-call cap with **no answer**, recorded by the journal as `outcome: ok`, exit 0,
  at 14.35 M and 6.93 M tokens. One of them repeated a single successful shell
  command **51 times**. No detector saw either.
- **A green gate over a weakened test file.** A local model's edit task passed its
  verifier while deleting an existing assertion and eight lines of the reasoning
  behind the file's tests; jichi could not count the tests, so it could not notice.
- **Two open findings, recorded rather than fixed:** under `--auto` a command's
  stderr is also copied onto jichi's own stderr; and on this bench uutils `timeout
  -s INT -k` SIGKILLs at once where GNU waits.
- **M713 published a rate the stronger key lowers:** 13 of 264 turns, not 20.

---

## 1. The setup, and why each piece is there

| piece | what it is | why |
|---|---|---|
| **workspace** | a `git clone` of zigodot per model, in a directory outside every repository; every task resets it to `70801ac` | the operator's checkout had an uncommitted `.jichi/lessons.draft.md`, and a drive must never be able to touch it. Checked first: `origin journey/zigodot`, 418 commits — the 2026-09-20 drive had measured an abandoned copy |
| **driver** | `scripts/pin-driver.sh` — a jichi binary copied outside the tree, `build: 03d45422` | the tree is rebuilt during the drive; a binary the build can delete is a binary a run can lose |
| **configs** | one per model, each naming **one** free chat model (`jlu/qwen3-coder-next` on the HRZ gateway; `prism-ml/bonsai-27b` in LM Studio), the local embedder, and `strong` as an alias of the same model | an explicit `--config` is a single source (`jc_config.c` does not merge `~/.jichi`), so nothing else can leak in. zigodot's own config routes between two tiers and names a priced model on the gateway — unusable for a measurement. zigodot's six agents ask for `strong`; aliasing it keeps every call on one model |
| **preflight** | `jichi doctor --live` on each config | both answered `tool calling observed "native"`; `0 problems` after the alias. The warnings left were read: no pricing (deliberately unset — HRZ rates are unknown and inventing them would make `/cost` lie), keyless local servers, no rerank model |
| **bounds** | fences **on** (edit scope `src/**` + `tests/**` with revert, `--lease fail`, the path fence, 200 tool iterations as in the operator's configs); caps **off** (no budget, no deadline, stall and request timeouts `0`) | a cap that fires manufactures an answer; a fence only bounds the blast radius |
| **housekeeping timeout** | 3,600 s, SIGINT then SIGKILL, via GNU's `gnutimeout` | §7: this bench's uutils `timeout` gets `-s INT -k` wrong |
| **telemetry** | `--log FILE --log-level full` | the `full` tier carries the whole arguments and each result's text, so a repeat can be keyed on the same bytes back rather than on equal byte counts |
| **LM Studio** | `bonsai-27b` loaded with an explicit 65,536-token context; the embedder loaded too | a model loaded on demand can get a small default context, and jichi's fixed system prompt plus tool definitions alone is about 16 k tokens |

The tasks: two read-only explorations (*which token kinds does the tokenizer define,
and does the parser handle each keyword*; *which source files under `src/` contain no
test block*) and one edit (*add a test for an untested public tokenizer function*,
gated by `zig build test && zig build gate-lint` — zigodot's own completion gate).
Measured before the drive: the gate runs **518 tests** and `gate-lint` reports
**529 test blocks run**, in about 8 s.

---

## 2. The six runs

| model | task | exit | seconds | tool calls | distinct | most repeated | stop | journal | tokens (journal) | answer |
|---|---|---|---|---|---|---|---|---|---|---|
| qwen | explore-tokens | 0 | 52 | 33 | 33 | 1 | done | ok | 2,070,469 | yes |
| qwen | untested-files | 0 | 16 | 8 | 8 | 1 | done | ok | 194,530 | **yes — correct** (64 of 104 files; checked by hand) |
| qwen | add-test | 0 | 823 | **200** | 157 | 16 | **max_iters** | **ok** | **14,352,482** | **no** — no change made |
| bonsai | explore-tokens | 0 | 106 | 12 | 10 | 2 | done | ok | 420,838 | yes |
| bonsai | untested-files | 0 | 362 | **200** | **44** | **51** | **max_iters** | **ok** | **6,931,022** | **no** |
| bonsai | add-test | 0 | 1,092 | 66 | 53 | 7 | done | ok | 3,266,946 | yes — see §5 |

qwen is `jlu/qwen3-coder-next` (HRZ gateway), bonsai is `prism-ml/bonsai-27b`
(LM Studio on the bench's RTX 4070 Ti SUPER). "Distinct" counts distinct
(tool, arguments) pairs in the run's stream.

---

## 3. search_code answered "(no matches)" for code that was there

**How it surfaced.** bonsai's `untested-files` run left two lines on jichi's stderr,
`grep: Unmatched ( or \(`. Chasing them (§8 records the two wrong turns) led to
`search_code`'s own results in the telemetry: the model's **first** search,
`Kind|Token.*Kind|TokenKinds|token_kind`, had come back `(no matches)` — and
`pub const Kind = enum` is at `src/gdscript/analyzer.zig:11`.

**The mechanism.** `search_code` ran `GREP_OPTIONS= grep -rnI -e PATTERN`: POSIX
**basic** regular expressions, where `|` and `+` are ordinary characters and `\(`
opens a group. Its schema told the model only *"Text or regex to search for"*.

**Measured, three ways.**

| | |
|---|---|
| the model's first pattern, run both ways in the same tree | basic mode (what the tool ran): **0 lines**; extended mode (`grep -E`): **7 lines** |
| all 45 pilot searches, re-run against a clean clone at `70801ac` | 29 plain patterns unaffected; of 16 extended-looking ones, **11 false negatives**, **1 spurious error**, 4 genuinely empty either way — **12 of 45 wrong** |
| every `search_code` call in the bench's telemetry (888; 588 with full arguments) | **268** extended-looking patterns, and **256 (95.5 %)** of them returned `(no matches)` |
| which dialect do models write? | **230** patterns use a bare `\|`, **5** use GNU-basic `\\|` — and the example among those five is an extended pattern escaping a literal `\|=` |

That last row is what decides the fix: models write the extended dialect, and
switching to it breaks essentially nothing they write.

**The fix (M714).** `grep -rnE` (and `-rnIE` where `-I` is supported). `-E` is POSIX,
and every grep in the platform matrix takes it — illumos's own usage line is
`grep [-E|-F] ...`. The schema now says *"Text or extended regular expression (grep
-E): a|b alternates, + ? {n} repeat, ( ) groups; backslash a literal …"*. And an
invalid pattern is now reported as one: on the failure path the tool asks grep about
the pattern alone, against `/dev/null`, and returns grep's own first line — *"error:
the pattern is not a valid extended regular expression (grep -E): Unmatched ( or \\( —
put a backslash before a literal ( ) | + ? { or ["* — where the old message was a
generic *"grep exited with an error"* that sent the model round again with the same
pattern.

**The checks, red first** (`tests/test_tool.c`, through the real tool and this
platform's grep): the prefix carries `-E`; `Kind|TokenKinds` finds `pub const Kind`;
`alph[a]_on+e` finds `alpha_one`; `call\(x\)` finds `call(x);`; `unbalanced(` is an
error that names the pattern. **Before the fix: 7 failures of 13,607. After: 0.**
Teeth per check: removing `-E` turns all 7 red; disabling only the pattern probe turns
exactly **1** red — the message check — so each half is guarded by its own assertion.

**What moved with it.** The tool description travels in every request body, so the
three reading-guide traces that carry the tool list drifted: seven request files, each
**exactly 141 bytes** longer. They were re-taken with `capture.sh`, and the diff was
proved to be the description and nothing else — replacing the two new strings with the
old ones reproduces the committed bytes exactly, and the `shape` files differ only in
size fields, each by 141.

---

## 4. D1's loop, twice, on the current build

**bonsai, `untested-files`: the textbook shape.** 200 tool calls, only 44 distinct;
one shell command — `grep -rn "^fn\s+test_[a-zA-Z]" src/ …` — ran **51 times**; at
call 76, when I first looked, the last five calls had all been that command. The pattern reflects a wrong idea of
how Zig spells a test (`test "name" {`, not `fn test_…`); run in the shell's basic
grep, `+` is literal too, so it matched nothing — successfully, every time. The run
ended at the cap with the text *"… Let me try with explicit tabs:"*: no answer,
**6,911,978 input tokens**, `stop_reason: max_iters`, journal `outcome: ok`, exit 0.
M432 counted nothing, because nothing failed.

**qwen, `add-test`: the long, varied shape.** 200 calls, 157 distinct, the most
repeated 16 times — not a loop of identical calls but a model that never converged:
*"OK I've spent way too much time on this. Let me try one completely different
approach…"*. No change made, **14,352,482 tokens**, `outcome: ok`, exit 0. Part of
that is the task's fault, and mine (§8).

**Why these two matter together.** They are the two shapes D1 has to tell apart —
*the same call, the same answer, again* versus *a lot of different work that goes
nowhere* — and only the first is a repetition a detector can key on. They are also
two more instances of `DEFERRED.md` item 7: capped one-shots that answered nothing
and exited 0. Two runs are an anecdote, not the 20 that item's floor asks for; they
are recorded as that.

---

## 5. A green gate over a weakened test file

bonsai's `add-test` "succeeded": `verify` exit 0, `stop_reason: done`, a real answer.
Its diff to `src/gdscript/tokenizer_test.zig`, measured against `70801ac`:

| | before | after |
|---|---|---|
| `test` blocks | 9 | **11** — two new tests, as asked |
| `testing.expect…` calls | 89 | 92 |
| `message != null` assertions | 3 | **2** — one existing assertion **removed** |
| comment lines | 61 | **53** — including the notes that adjudicate the tests against the Godot reference (*"Adjudicated against gdscript_tokenizer.cpp:1282-1300 …"*) |

The model also added a header line of its own: *"This file was modified by
jichi-corpus agent; edits are tracked in git history."* The verifier went green, and
jichi recorded `sanity: no_count` — it could not count the tests, so the hollow-gate
check that compares counts was disarmed. zigodot's `gate-lint` does print a count
(*"529 test blocks run"*), in a format jichi does not read. **Recorded, not acted on:**
one run, one model, and a fix would be either a zigodot gate change or a parser for a
project-specific line.

---

## 6. Under --auto, a command's stderr is copied onto jichi's own

**Measured with `mockmodel`** (the model's side scripted, so nothing but jichi
varies): a command `echo X; ls /nonexistent-path | cat`. Plain `-p`: the `ls` error
reaches the model in the tool result, jichi's stderr stays empty. With `--auto`: the
error reaches the model **and** appears on jichi's stderr. The command runs **once**
— a file it appends to gets one line — so no side effect is doubled.

**Why it is not harmless although the model is told:** in headless use jichi's stderr
carries the reach footer and the `[envelope]` verdict that wrappers read; in the TUI it
is the screen. **Not fixed here** — the operator chose to fix `search_code` first — and
the mechanism is not established. The reproduction is a five-check smoke driver that
fails its check 5 today, by design, so it is kept out of the tier; its full text is
[Appendix A](#appendix-a-the-reproduction-for-6-kept-where-it-can-be-found).

> **Corrected at M721 — two sentences above were wrong, and the mechanism was the
> one this page ruled out.** Plain `-p` never ran the command: headless without
> `--auto` refuses `run_terminal_command` (*"Tool requires approval, unavailable in
> headless mode"*), so its empty stderr compared nothing. Under `--auto` the error
> did **not** reach the model: the reproduction's checks 2–3 grepped the next
> request for the probe *path*, which is there anyway — as the command itself,
> echoed back in the assistant's tool call. The cause is the popen path's trailing
> `2>&1`, which binds to the last simple command only, so in `A; B` and `A | B`
> A's stderr went to jichi's stderr and to nobody else; `make test | tail -20`
> lost every compiler error. Fixed in M721 (`exec 2>&1` first, for the whole
> shell), and `tests/smoke/shell_stderr_captured.sh` now uses markers that exist
> only in output. The text above is kept, because a correction that erases what
> it corrects teaches nothing.

---

## 7. The bench: uutils timeout, and the number M713 got wrong

**uutils coreutils 0.10.0 `timeout -s INT -k N` SIGKILLs at once.** Measured side by
side on this bench against GNU's `timeout` (installed as `/usr/bin/gnutimeout`):

| command | uutils 0.10.0 | GNU 9.7 |
|---|---|---|
| `timeout 1 sleep 3`, `-s INT`, `-k 5`, `-s TERM -k 5` | 124 | 124 |
| **`timeout -s INT -k 5 1 sleep 3`** | **137 after 1.0 s** | 124 |
| the same, where the child traps INT and exits 42 | **137 after 1.0 s** | 124 |

jichi's own tier is not affected — its runner uses `timeout -k 5` with the default
signal, and nothing in `tests/`, `scripts/` or `examples/` passes `-s INT` — but the
drive script would have been, which is why it prefers `gnutimeout`. It is the second
concrete reason, after M713's, to carry the userland as a platform axis (plan D10).

**M713's repetition rate was overstated.** `tests/measure/success_repeats.py` keyed a
repeat on the argument *summary* and the output's *byte count*. On the bench's old
telemetry, **7,313 of the 11,276 events carry the `full` tier**, and the upgraded
script keys those on the whole arguments and a hash of the result:

| a successful call repeats | M713 (summary key) raw / unchanged | M714 (full key where present) raw / unchanged |
|---|---|---|
| ≥ 3 times | 72 / 51 | **52 / 33** |
| ≥ 5 times | 34 / **20** | 22 / **13** |
| ≥ 10 times | 10 / 8 | 7 / 6 |
| a *failed* call ≥ 3 times | 19 | 16 |

So the honest figure is **13 of 264 turns (4.9 %)**, not 20 (7.6 %): equal summaries
and equal byte counts were hiding different calls — `search_code` drops out of the
list entirely once its full arguments are compared. The finding stands (the worst turn
still repeats `run_tests` 33 times with nothing changed in between); its size was
overstated, and M713's pages now say so beside the old number.

---

## 8. What I got wrong during the pilot

1. **Two hypotheses about the stderr lines, both refuted by measurement before any fix.**
   First, that the default shell path's trailing `2>&1` binds only to the last command
   of a compound line, so earlier stderr leaks — a driver written to prove it passed
   all five checks on today's code. Second, that `search_code` let grep's stderr
   through — its source appends `2>/dev/null`. The third reading, measured, is §6.
   Chasing the two lines is also how §3 was found.
2. **A pilot task that contradicted itself.** `add-test` asked for a test "beside the
   existing tokenizer tests" while "keeping the change to the tokenizer's own source
   file" — and `tokenizer.zig` has **no** test blocks; they live in
   `tokenizer_test.zig`. qwen's 200-call run is partly that. Corrected for the full
   drive as `add-test-2`.
3. **I overwrote a task file while the pilot was running.** Writing the full drive's
   tasks replaced `add-test.md` before the second model had reached it. Caught, and the
   original restored — then proved byte-identical to the prompt the first model had
   received, from the telemetry — so both arms ran the same task.
4. **One probe command I wrote was broken** (a relative-path hack that made `timeout`
   fail to find the binary) and measured nothing; it was re-run properly before any
   conclusion was drawn from it.

---

## 9. Reproduce it yourself

```sh
# anywhere -- the dialect finding, in any checkout of any repository
printf 'pub const Kind = enum {\n' > /tmp/k.zig
grep -n -e 'Kind|TokenKinds' /tmp/k.zig; echo "basic: $?"      # 1: no match
grep -nE -e 'Kind|TokenKinds' /tmp/k.zig; echo "extended: $?"  # 0: the line
```

```sh
# in the jichi checkout -- the fix's own checks, and the repetition rate
make run_tests && ./run_tests | tail -1
python3 tests/measure/success_repeats.py
scripts/corpus-drive.sh --self-test
```

```sh
# anywhere -- which timeout you have, and whether it gets -s INT -k right
readlink -f "$(command -v timeout)"
timeout -s INT -k 5 1 sleep 3; echo "rc=$? (GNU: 124)"
```

The drive itself is `scripts/corpus-drive.sh --help`; it refuses, before any request,
a config that names a model outside the free `jlu/` namespace on a non-loopback
server, and `--self-test` proves that refusal two-sided.

---

## 10. What this page does not claim

- **Not** that the full drive will look like the pilot: six runs size a harness, they
  measure nothing about rates.
- **Not** that `search_code`'s false negatives caused the loops in §4 — bonsai's worst
  loop was a shell `grep` of its own choosing, not `search_code`. The claim is narrower:
  the tool gave wrong answers to 27 % of the pilot's searches, and a detector fitted on
  a corpus with that defect would be fitted to an artifact.
- **Not** that bonsai edits badly in general (§5 is one run), nor that qwen cannot add
  a test (its task was flawed).
- **Not** why `--auto` copies stderr (§6): measured, not explained.

---

## Appendix A: the reproduction for §6, kept where it can be found

*Superseded at M721: checks 2–4 of this version are vacuous (see the correction
in §6); the driver that shipped as `tests/smoke/shell_stderr_captured.sh` is a
rewrite.*

This is the five-check smoke driver that reproduces §6. It is **not** in
`tests/smoke/`, because its check 5 fails on today's code by design and the tier
must stay green; it lives here so that whoever fixes the defect does not have to
rebuild it, and so it does not exist only on one machine (the failure
[M713 §4.4](2026-09-23-what-to-build-next.md#44-the-corpora-decisions-rest-on-live-on-one-machine-and-get-pruned)
describes). Save it as `tests/smoke/shell_stderr_captured.sh` with the fix, and
prove check 5 red first. Checks 1–4 pass today; check 5 fails with the leaked
`ls` line.

```sh
#!/bin/sh
# smoke: a model-issued shell command's stderr goes to the MODEL -- from every
# part of a compound command -- and NOT onto jichi's own stderr (M714).
#
# FOUND BY DRIVING (the 2026-09-23 zigodot corpus pilot): a run's stderr file held
# two `grep: Unmatched ( or \(` lines from the model's shell commands. Chasing it
# refuted two hypotheses before the third was measured, and the record keeps all
# three, because the wrong two were the plausible ones:
#
#   1. "the popen path's trailing `2>&1` binds only to the last command, so earlier
#      stderr leaks" -- REFUTED by this driver's checks 2-3 without --auto: the
#      model saw every part's stderr and jichi's stderr stayed empty.
#   2. "search_code runs grep without capturing stderr" -- REFUTED by reading
#      src/tools/jc_tool_search.c: it appends `2>/dev/null`.
#   3. MEASURED: under --auto the command runs ONCE (a file it appends to gets one
#      line) and its stderr reaches the model AND is copied onto jichi's stderr.
#      Plain -p: 0 stderr lines; --auto: 1. The mechanism is not established here.
#
# Why it matters although the model is told: jichi's stderr carries the reach
# footer and the [envelope] verdict that wrappers read, and in the TUI it is the
# screen. The captured requests are the ground truth for "the model saw it": each
# tool result travels in the NEXT request body (the M429 lesson).
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

P1=/nonexistent-jichi-stderr-probe-one
P2=/nonexistent-jichi-stderr-probe-two
P3=/nonexistent-jichi-stderr-probe-three

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"echo STDOUT_MARK; ls $P1 | cat"}
rule
  count 2
  tool run_terminal_command {"command":"ls $P2; echo LAST_MARK"}
rule
  count 3
  tool run_terminal_command {"command":"ls $P3; echo WATCHED_MARK","timeout":30}
rule
  text STDERR_DONE
EOF

mm_start "$tmp/replies.mm" "$tmp/cap" 9
write_config "$tmp/config.json" "$MM_PORT"

(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto -p "run the commands" < /dev/null) > "$tmp/out" 2> "$tmp/err"; rc=$?
mm_stop

# --- 1: the denominator -- all three commands ran and were answered -----------
# Without this every later check could pass on a run that never executed a
# command (an empty request proves nothing about what the model was shown).
if [ "$rc" -eq 0 ] && [ -s "$tmp/cap/req.4" ] && grep -q "STDERR_DONE" "$tmp/out"; then
    t_ok "the run executed three commands and finished (rc=0, 4 requests)"
else
    t_fail "run rc=$rc, requests: $(ls "$tmp"/cap/req.* 2>/dev/null | wc -l | tr -d ' ') -- the fixture did not run"
fi

# --- 2: stderr of a NON-LAST pipeline member reaches the model -----------------
if grep -q "STDOUT_MARK" "$tmp/cap/req.2" 2>/dev/null &&
   grep -q "nonexistent-jichi-stderr-probe-one" "$tmp/cap/req.2" 2>/dev/null; then
    t_ok "\`A; B | C\`: B's stderr is in the tool result the model reads"
else
    t_fail "B's stderr is missing from the tool result (stdout present: $(grep -c STDOUT_MARK "$tmp/cap/req.2" 2>/dev/null))"
fi

# --- 3: stderr of the FIRST command of a list reaches the model ----------------
if grep -q "LAST_MARK" "$tmp/cap/req.3" 2>/dev/null &&
   grep -q "nonexistent-jichi-stderr-probe-two" "$tmp/cap/req.3" 2>/dev/null; then
    t_ok "\`A; B\`: A's stderr is in the tool result the model reads"
else
    t_fail "A's stderr is missing from the tool result"
fi

# --- 4: the control -- the watched path (a per-call timeout) always captured ---
if grep -q "WATCHED_MARK" "$tmp/cap/req.4" 2>/dev/null &&
   grep -q "nonexistent-jichi-stderr-probe-three" "$tmp/cap/req.4" 2>/dev/null; then
    t_ok "the watched path (timeout set) captures stderr, as it always did"
else
    t_fail "the watched path lost stderr -- the fix broke the path that worked"
fi

# --- 5: and none of it leaks onto jichi's own stderr ----------------------------
# In the TUI that stream is the screen, so a leak there is a garbled display as
# well as a diagnostic the model never saw.
if ! grep -q "nonexistent-jichi-stderr-probe" "$tmp/err" 2>/dev/null; then
    t_ok "no command stderr on jichi's own stderr"
else
    t_fail "command stderr leaked onto jichi's stderr: $(grep -m1 nonexistent-jichi "$tmp/err" | head_bytes 160)"
fi

t_done
```
