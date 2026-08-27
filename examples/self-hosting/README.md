# Self-hosting dev pack — first slice (review-only)

A prototype of
[`docs/proposals/2026-08-self-hosting-dev-pack.md`](../../docs/proposals/2026-08-self-hosting-dev-pack.md):
a compiled jichi instance reviewing **jichi's own source**, driven by whatever
models are available (here, the JLU HRZ models). This is deliberately the
**smallest, safest slice** — two **read-only** review agents and one command.
It writes nothing, so it is safe against any model, and it lets you *measure*
whether a given model is a good enough reviewer before you ever trust one to
write.

## Honest scope

- **Read-only.** Both agents are `readonly: true` — they cannot modify the tree.
- **A second reviewer, not the gate.** The gate is `make ci`
  (gcc + clang `-Werror`, ASan/UBSan, valgrind, smoke, e2e). These agents catch
  what a fast human review would; they do not replace CI.
- **Design stays on the frontier model.** Local/HRZ models do the review and
  mechanical tail well; they do not design milestones or reason across 79k
  lines. That boundary is the point (see the proposal).

## What's here

```
examples/self-hosting/
  config.jichi-dev.json        # review-only config: HRZ models, embed for RAG
  config.jichi-dev-write.json  # WRITE slice: the same, + the autonomy envelope
  agents/c89-reviewer.md       # C89/pedantic + house-rule review (read-only)
  agents/arena-auditor.md      # the three-arena lifetime discipline (read-only)
  agents/test-author.md        # writes a shown-red-first test (writes tests/ only)
  agents/doc-updater.md        # ROADMAP/CHANGELOG in-style (writes docs/ + CHANGELOG)
  agents/committer.md          # drafts a house-style commit message (read-only)
  commands/review-diff.md      # /review-diff  -> both reviewers over the diff
  commands/add-test.md         # /add-test     -> the test-author
  commands/update-docs.md      # /update-docs  -> the doc-updater
  commands/draft-commit.md     # /draft-commit -> the committer
```

## Use it (from a jichi checkout)

> **If you are working through the curriculum**, start with
> [`docs/READING_OPEN_SOURCE.md`](../../docs/READING_OPEN_SOURCE.md) §"Then drive
> it on itself", which sequences this after the reading guides and leads with the
> honesty section. This page is the reference; that one is the on-ramp. No
> institutional key is needed — [`config.jichi-dev-local.json`](config.jichi-dev-local.json)
> runs the review slice against a local model server.

**The short way** — [`jichi-dev.sh`](jichi-dev.sh) does the four steps below and
enforces the three that are checkable (a key for the `gateway` mode, a non-master
branch for `write`, budgets on an unattended run):

```sh
# in the jichi checkout -- the mode is a word, so nothing here can be mistaken
# for a flag jichi itself takes
sh examples/self-hosting/jichi-dev.sh -p "/review-diff"           # local, read-only
sh examples/self-hosting/jichi-dev.sh gateway -p "/review-diff"
sh examples/self-hosting/jichi-dev.sh write -p "/add-test …"      # refuses on master
sh examples/self-hosting/jichi-dev.sh clean                       # put .jichi/ back
```

The long way, once, so you know what the launcher does:

```sh
# 1. Bring the assets into this checkout's .jichi/ (git-ignored, so `git status`
#    stays clean -- .gitignore ignores .jichi/ wholesale; the two docs reviewers
#    it ships are tracked DESPITE that rule, deliberately). Your copies join
#    them, so `jichi agents` will list seven, not five. Step 4 takes yours out.
mkdir -p .jichi/agents .jichi/commands
cp examples/self-hosting/agents/*.md   .jichi/agents/
cp examples/self-hosting/commands/*.md .jichi/commands/

# 2. Your key in the environment, under the name the config's apiKeyEnv uses
#    (never in the config). HRZ users can reuse their existing key:
export JICHI_API_KEY="<your-key>"     # or whatever apiKeyEnv names

# 3. Make a change, then review it -- read-only, so run it on a branch or dirty tree:
jichi --config examples/self-hosting/config.jichi-dev.json
#   in the TUI:
#     /review-diff
#   or headless:
jichi --config examples/self-hosting/config.jichi-dev.json -p "/review-diff"
```

```sh
# 4. When you are done, take the copies back out -- by name, so the two agents
#    this repository ships are left alone. Not a git-safety step (they were never
#    visible to git); a leave-the-bench-as-you-found-it one:
for f in examples/self-hosting/agents/*.md;   do rm -f ".jichi/agents/$(basename "$f")";   done
for f in examples/self-hosting/commands/*.md; do rm -f ".jichi/commands/$(basename "$f")"; done
git status --short .jichi     # expect: nothing
```

**If a local model will not work, probe the server before blaming the model:**

```sh
# in the jichi checkout
sh scripts/probe-models.sh http://127.0.0.1:1234/v1
```

`loads` / `native` / `prose` per advertised id. Measured 2026-08-21 on this
bench: five of eight ids would not load (a full GPU, not broken models — the
server log said `unable to allocate ROCm0 buffer`) and the two that did emitted
tool calls as prose the server never translated, which jichi cannot execute.
Details and the fixes: [`docs/LOCAL_MODELS.md`](../../docs/LOCAL_MODELS.md)
§"LM Studio".

`doctor` first if unsure the models resolve:

```sh
jichi --config examples/self-hosting/config.jichi-dev.json doctor --live
```

## Measured: model latency is the binding constraint (2026-08-02)

Validated from the reference box: the assets load (`jichi ... agents` /
`commands` list them), and `doctor --live` confirms the HRZ models resolve and
**native tool calling works** (probe prefix 106 tokens). But the honest finding
— the whole reason this "measure the model" slice exists — is that HRZ's
`gemma-4-31b` latency is **intermittent and often too slow for an interactive
review loop**: short probes returned fast, yet full streamed review turns
sometimes did not complete in 200–300s (a shared university endpoint under
load). That is the proposal's thesis made concrete: **the model tier and its
availability, not the harness, are what gate self-hosting.**

Practical responses, in order:

1. **Fail cleanly, don't hang.** The config sets `timeouts`
   (`stall: 90`, `request: 600`) so a wedged call errors instead of blocking —
   raise them for batch/background review, lower them to fail fast interactively.
2. **Run review as a background job, not at the keyboard.** Pair with the
   `supervise-long-command` skill / `--run-timeout`, or run headless under a
   supervisor ([`docs/AUTONOMOUS_LOOPS.md`](../../docs/AUTONOMOUS_LOOPS.md)) and
   read the result later.
3. **Prefer a faster or local reviewer** when you have one (a local GPU / LM
   Studio model, or a smaller HRZ model) — add it as the `review` model and keep
   HRZ as `fallback`. Measure it here before trusting it (below).

## What good looks like

A useful run names real `file:line` findings, separates **MUST-FIX** from
*nice-to-have*, and ends with a verdict that still points at `make ci`. If the
model invents nitpicks, misses a planted `//` comment or a `long long`, or
declares a change "safe" without naming CI, that model is not (yet) a good
enough reviewer for this codebase — which is exactly the thing this slice lets
you find out cheaply.

## Write-enabled slice — fenced, verified, on a branch

`config.jichi-dev-write.json` lets jichi **write**, but hard-fenced and under the
autonomy envelope. The safety is layered, and the key layer is the fence:

- **`editScope: ["tests/test_*.c", "docs/ROADMAP.md", "docs/analysis/*.md",
  "CHANGELOG.md"]`** — a *positive* allow-list, tightened at M517 to a stated
  rule: **the fence may admit only files the verify runs, or files that cannot
  affect a gate.** The edit tools cannot touch `src/` or `include/`, so the loop
  can modify neither core code nor its own guardrails — and they can no longer
  touch a **gate** either. The old list said `tests/**` and `docs/**`, which
  admitted all 231 smoke drivers *and* the 74 graded assignment `test.sh`
  graders, none of which `make test` runs: a weakened lint or a weakened grader
  passed its own verify, and `revertOutOfScope` could not help because those
  paths were in scope. To author a gate, pass **both** flags deliberately:
  `--edit-scope 'tests/smoke/*.sh' --verify 'make smoke'`.
- **`verify: make test`** — a red verify rolls back to the last green checkpoint
  (fix-forward `verifyRetries` times first). Override per task: `--verify "make
  smoke"` for a smoke driver, `--verify "make ci"` for the full gate.
- **`revertOutOfScope: true`** — a stray `src/` change made via the *shell*
  (which `editScope` doesn't fence) is detected and reverted.
- **`selfReview`, `snapshots`, `pathFence`, `privilegedCommands: deny`,
  `learnOnStop`** — the usual wall, plus lessons drafted after a clean run.

**Always run it on a feature branch, never master**, with budgets:

```sh
git switch -c dev-loop            # never let the loop run on master
cp examples/self-hosting/agents/*.md   .jichi/agents/
cp examples/self-hosting/commands/*.md .jichi/commands/
export JICHI_API_KEY=...

# author a shown-red-first test for an under-covered pure function, unattended:
jichi --config examples/self-hosting/config.jichi-dev-write.json --auto \
      --budget-tokens 200k --deadline 20m --max-tool-calls 60 \
      -p "/add-test a unit test for jc_reread_hash: determinism, one-byte and length sensitivity"

# or record an already-made change in the docs:
jichi --config examples/self-hosting/config.jichi-dev-write.json --auto \
      --budget-tokens 100k -p "/update-docs"

# review, then commit yourself (the committer only drafts):
jichi --config examples/self-hosting/config.jichi-dev.json -p "/draft-commit"
```

Because it is unattended (`--auto`), the model-latency caveat above bites
harder: run it as a **background/batch** job (`docs/AUTONOMOUS_LOOPS.md`) and
read the journal (`jichi runs`) after, rather than watching it at the keyboard —
especially on a slow shared endpoint. Nothing merges that `make ci` hasn't
passed.

## Promotion path — and why it stays here for now

**Decision (2026-08-02): these assets stay in `examples/` until they have been
*exercised*; they are not promoted into a compiled pack yet.**

A compiled **`jichi-dev`** scaffold pack — baked into the binary as chunked C
literals, so `jichi init jichi-dev` works from just the executable, no repo — is
the eventual home. But promotion costs a `jc_scaffold.c` / `jc_setup.c` /
scaffold-test change and freezes each asset into 509-char C string literals that
are tedious to edit, whereas here every agent is a one-line file change. So
while the shape is still moving, a loose reference set (a `cp` into `.jichi/`)
wins.

Promote only once all three hold — the "earned its shape" bar:

1. the read-only reviewers have produced **useful findings on several real
   diffs** (real `file:line` issues, not invented nitpicks, not missed
   violations);
2. a **write agent has completed a real task end to end** under the envelope
   (a test authored shown-red-first, or a ROADMAP/CHANGELOG update) — which
   needs a **responsive model**: the HRZ-latency caveat above is the current
   blocker, and a faster or local reviewer clears it;
3. the agent set, tool fences, and config defaults have **stopped churning**.

Until then, iterating on these files beats re-splitting C tables. See
[`docs/proposals/2026-08-self-hosting-dev-pack.md`](../../docs/proposals/2026-08-self-hosting-dev-pack.md)
§Decisions.

### Measured: criterion 1, first real run (2026-08-03)

`/review-diff` run headless against a real ~200-line working diff (the M269
fuzz/fault wave) on `jlu/gemma-4-31b-it`. **Criterion 1 is partly met, and the
run exposed two defects in jichi and this pack — not in the model.**

*The review itself was genuinely useful.* Correct, specific observations: the
`-1L` suffix matching the `long` prototype, declarations at block top in
`replay_corpus`, `jc_snprintf` over `sprintf`, `fclose` on every path including
the allocation-failure branch, `closedir` at exit. One valid nice-to-have — a
1 MB `malloc` **inside** the replay loop that should be hoisted — **was
applied**. No invented nitpicks; `MUST-FIX: none` with the required verdict
line, and it honored "never imply safe". 111k input / 6.9k output tokens, 5 tool
calls, no timeout.

*Defect 1 (this pack, fixed).* Every `spawn_subagent`/`spawn_parallel` call came
back **`needs approval`**: they are mutating tools, chat mode makes them ASK, and
headless has no approval front-end. So the two-reviewer command silently
degraded to a single manual pass — the model said so in its answer, which is the
only reason it was noticed. `config.jichi-dev.json` now pre-approves exactly
those two tools (every agent here is `readonly: true`, so this grants no write
capability) instead of requiring a blanket `--auto`.

*Defect 2 (jichi, fixed as M269).* Running step 3 before step 1 — assets not yet
copied into `.jichi/` — did not error. `run_headless` passed the literal text
`/review-diff` to the model, which improvised a confident, fabricated "review"
of the diff (it even reported `--accessible` as a newly *added* flag) and exited
**0**. An unresolved `/name` is now warned about on stderr; it is still passed
through, since a prompt may legitimately begin with a path. Driver:
`tests/smoke/slash_unknown.sh`.

*Re-run after the permissions fix, same diff.* Both `spawn_subagent` calls now
succeed and the command works as designed: two independent labeled passes, then a
consolidated MUST-FIX / nice-to-have table and the verdict line. The
`arena-auditor` pass correctly verified the 1 MB buffer it had itself flagged was
now hoisted and freed, that `ptext` is freed after `jc_arena_strdup` into the
scratch arena, and that the new `stream_no` global is reset in `jc_fault_reset` —
i.e. it checked the *specific* lifetime claims, not generic advice. 80k input /
733 output tokens, 3 tool calls, no timeout.

### Criterion 1 met: a diff that CONTAINS defects (2026-08-03)

The clean-diff runs above only showed the reviewers do not *invent* findings. So a
throwaway `src/util/jc_planted.c` was written into a git worktree with **seven**
deliberate defects drawn from this project's own documented bug classes, and
`/review-diff` was run against it.

**Five of seven caught, zero invented, and the verdict flipped** to *"Must-fixes
first; not ready for `make ci`"* — the same command that said "ready" on the clean
diffs, which is the discrimination that matters:

| planted | caught | cited line |
|---|---|---|
| `//` comment | yes | 11 — exact |
| `sprintf` instead of `jc_snprintf` | yes | 19 — exact |
| `long long` | yes | 36 — off by one |
| per-call data on the session arena (`app->arena`) | yes | 26 — right construct, ~3 off; **named the correct remedy** (`scratch`/`tool_scratch`) |
| `jc_sb` never freed | yes | 42 — exact |
| declarations after statements | **missed** | — |
| unchecked `jc_strdup` return | **missed** | — |

Four of six anchors were byte-exact and the other two pointed at the right
construct. The arena finding is the notable one: it is the M197–M199 class, the
bug family that cost this project three milestones, and the reviewer both spotted
it and prescribed the right fix. The two misses are the cheap ones —
declaration-after-statement is caught by `-Werror=declaration-after-statement` on
every build, so `make ci` is its real gate.

*Treat the reviewers as a second pair of eyes with a known blind spot, never as a
gate.* Two independent passes, ~51k input / 3.5k output tokens, 3 tool calls.

### Defect 3 (jichi, fixed as M269): a wedged run from one malformed tool call

The planted-defect run above **failed reproducibly twice before it ever produced a
review**, and the cause was jichi, not the model. The model truncated a large
`spawn_parallel` arguments blob; `jc_jsonrepair` could not repair it; the tool
correctly returned an error — and then the *next* request echoed those unterminated
arguments back, at which point litellm/vLLM rejected the whole request with
**HTTP 400 "Unterminated string"**. A 400 is not transient, so the retry ladder
could not help, and the malformed text stays in the history, so every later turn
would die identically: a wedged run, not a glitch.

The Anthropic provider had solved this at M145; the OpenAI provider had not, because
there `arguments` is a *string the server re-parses* — the request document itself
is valid, so nothing local notices. `jc_prov_args_wire` now guarantees that string
always parses as an object, preserving the original text under
`_unparsed_arguments` (M145's reasoning: a silent `{}` next to a "your arguments
failed to parse" tool_result contradicts the evidence the model needs). With the fix
in place the same run completed and produced the table above.

### Criterion 2: attempted, NOT met — and it found a design conflict (2026-08-03)

`/add-test` run in a worktree under the full envelope (`--auto --budget-tokens
200k --deadline 20m --max-tool-calls 60`, `editScope` = `tests/**`, `docs/**`,
`CHANGELOG.md`, `verify = make test`), asked for coverage of
`jc_path_resolve`'s not-yet-existing-target branch.

**Outcome: `budget_exhausted`, work kept.** Closer than that sounds, though. The
agent wrote a test covering exactly the three requested cases — nonexistent leaf,
symlinked parent resolved, nonexistent parent ⇒ `JC_ERR_NOTFOUND` — and it
**compiles and passes** (+4 checks). One defect: a declaration after a statement,
which `-Werror=declaration-after-statement` rejects, so `make ci` is its gate.
Note the symmetry with criterion 1 — that is the same class the `c89-reviewer`
missed. What it never did was the shown-red proof, so by this pack's own standard
the test is unfinished.

Two findings worth more than the verdict:

**1. The 200k token budget lasted 78 seconds and 12 tool calls.** This backend has
no prompt caching, so every call re-sends the whole history and input grows
quadratically: 213k tokens consumed for 1.5k of output. On a no-cache endpoint the
real currency is *tokens per tool call*, ~17k here, so budget a test-authoring task
at 500k–1M, or use a caching backend. `--budget-tokens 200k` is not a small task's
budget; it is barely a dozen steps.

**2. `/add-test`'s own procedure was impossible under `editScope`.** The command
said "break the guarded code, confirm the expected failure, restore" — which needs
a `src/` edit, which the positive allow-list forbids. The agent duly edited
`src/util/jc_path.c`, and M142's out-of-scope guard **detected and reverted it**
(`out_of_scope: {"paths":["src/util/jc_path.c"],"reverted":1}`) while keeping the
in-scope test edit. The guardrail behaved perfectly; the *command* was
self-contradictory, and has been rewritten to have the agent report the exact
one-line break for a human to apply, and to state plainly that it has not observed
the red.

So criterion 2 stays open, but the blocker is now specific and fixable rather than
"the model is too slow": a realistic token budget, and a command whose procedure
its own fences permit.

### Criterion 2 met on the re-run (2026-08-03)

Same task, with the two fixes above: the rewritten `/add-test` and
`--budget-tokens 800k`. **`outcome: ok`** — completed end to end, verifier
(`make test`) green, no out-of-scope path, nothing rolled back, and it used
**131k of the 800k** tokens over 8 tool calls. The earlier 200k exhaustion was
budget starvation, not a capability ceiling.

The test it wrote is in this repo (`tests/test_path.c`, +4 checks): it compiles
clean under `-std=c89 -pedantic -Wall -Wextra -Werror` — the
declaration-after-statement of the first attempt is gone — and passes.

It followed the rewritten shown-red protocol exactly: named the file and line,
gave the before/after of a one-line break (`if (1 || realpath(...) == NULL)`),
listed which checks it should red, stated plainly that it had **not** performed
the edit, and predicted the failure count.

**The human half, run afterwards: predicted 2, actual 3.** The two locations it
named were exactly right (`test_path.c:52` and `:147`); the miss was a third
assertion at `:150` that depends on `:147`'s output, so it fails as a cascade
rather than independently. Under-counting a dependent assertion is a small,
honest error — but it is the reason this step stays with a human. *A predicted
red is a hypothesis, not a result.*

**All three criteria now stand: 1 met, 2 met, 3 (churn) is a judgement call about
whether the asset set has settled.** Promotion to a compiled `jichi-dev` pack is
unblocked on evidence; what remains open is the missing assets the proposal
specifies (`AGENTS.md` digest, five skills, two agents, `/red-first`,
`/milestone`) and build-order steps 4–5.
