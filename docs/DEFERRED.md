# Deferred register

> **What this is.** Things deliberately **not done**, with the reason and where the
> reasoning lives. A companion to [`DECISIONS.md`](DECISIONS.md): that page records what
> was chosen, this one records what was consciously left.
>
> **Why it exists.** "Recorded as the next slice" is only honest if the record is
> findable. Deferrals were accumulating inside individual ROADMAP entries and proposals,
> where a reader would have to already know the milestone to find them.
>
> **Coverage starts at M298** (2026-08-05), the beginning of the current program. Earlier
> deferrals live in their own milestone entries and are not back-filled — inventing a tidy
> list of decisions I did not witness would defeat the purpose. Items are removed when
> done, with the closing milestone noted in `DECISIONS.md` or the ROADMAP.

A deferral belongs here when someone could reasonably ask "why isn't this done?" and the
answer is a judgement rather than an oversight.

## Check the checkable part of a reason BEFORE parking the item (M326b)

**Three entries in a row were parked on an assumption that a minute's reading would have
settled:**

| Entry | The reason given | What it actually was |
|---|---|---|
| Measuring a `core` attempt that needs a hint (M319) | *"core costs the hint ladder — the machinery `attempt` exists to exercise"* | across two models and 24 runs the ladder was **never once called**, six of those runs failing with the tool advertised |
| Flipping `attempt` to `core` (M320) | the same hint-ladder cost | same; the flip was then refused for an entirely different, measured reason |
| `repoMap: false` for `attempt` (M326) | *"tasks 20–22 are explicitly about reading this repository"* | all three work on **self-contained fixtures**; none names a file in `src/` |

The shape is identical every time: the reason made a **factual claim about a fixture, a tool or a
number**, it sounded obviously true, and nobody checked it. Meanwhile the *judgement* half of each
reason was fine. So:

> **A reason may contain judgement, evidence, or an unchecked factual claim. The first two belong
> here. The third does not — check it first, and write down what the check found.**

Concretely, before adding a row: if the reason asserts what a task contains, what a tool does, how
many call sites something touches, or what is reachable from this machine — **go and look.** These
are minutes of work, and every one of them that went unchecked outlived several milestones and
misdirected the next person to read it.

The audit that produced this rule (M326b) is recorded in the rows below: each remaining reason's
checkable claims were verified, and two of them changed.

**A deferral is not a rejection** (M317). Its first revision conflated the two: three rows
were things decided *against* on the merits, which will never be done and do not belong on a
list of pending work — a register that mixes "not yet" with "not ever" makes both unreadable,
and the "not ever" rows are the ones a future reader would waste time on. Those moved to
[`DECISIONS.md`](DECISIONS.md), where a rejected alternative belongs. **The test: if new
information could change the answer, it is deferred; if the answer follows from the design,
it was decided.**

---

## Open — instrumentation and cost

| Deferred | Why | Where |
|---|---|---|
| **The same craft A/B on a frontier model**, with a task whose deliverable is unstated and whose output a human grades. | M318 measured one 31B model and shipped the conclusion its evidence licenses (off under `--lite` only). The frontier case is where the section's claimed value lives. **Checked (M326b): no frontier model is reachable from this machine** — all five configured endpoints are HRZ-hosted (`jlu/gemma-4-31b-it`, `jlu/qwen3-coder-next`, plus embed/rerank), so this was genuinely resource-blocked and not merely unattempted.<br>**Operator's statement (2026-08-06): they will supply an API key with frontier-model access for this test.** So the blocker moves from *"no such model here"* to *waiting on the key* — and the entry is kept rather than closed, because the key is only **one of three** things the experiment needs. The other two do not arrive with it: a task whose deliverable is genuinely **unstated** (every graded curriculum task names its deliverable, which is exactly why M318's pass-rate result was uninformative), and **a grader who is not the author of the section under test** — blind pairs for the operator to grade is the clean form. **Harness and tasks built 2026-08-07** (M326g): `tests/bench/craft_ab/`, three unstated-deliverable tasks, blind pairwise grading, pre-registered in [proposals/2026-08-craft-ab-frontier.md](proposals/2026-08-craft-ab-frontier.md). What remains is the operator running and **grading** it.<br>**Run attempted 2026-08-10 (`session-01`): 18/18 errored on the key budget.** Two runs answered (~96k input) and consumed the key's remainder; sixteen then failed in under a second each with the gateway's own `429 budget_exceeded`, which named the key, the spend and the cap (machine-verifiable, and far more actionable than a bare 429). The 2026-08-07 pilot ran under a different key (`JC_DEV_KEY`, not on this machine). So the blocker is now **the key budget, not the harness**: reachability had been checked, the per-key budget had not — the M326b shape, again, in our own register. Unblock is either a budget raise/reset on that key (the operator report should carry the quota finding) or the pilot's dev key; then the session is one command (a fresh three-pair run under a new label, then the blinding step, then the operator grades). The over-budget key 429s **every** model including `jlu/*`, so ordinary jichi work on this key is blocked with it.<br>**Run completed 2026-08-10 (`session-02`, on the dev key the operator supplied): 18/18 runs `done`, zero truncations** — ~3.73M input / ~54k output on `anthropic/claude-opus-4-5`, ~20 minutes, preflight proving the arms differ (+1316 bytes in ON). The blinded pack was built, and **that pack is gone (checked 2026-08-22, M545).** `results/` is in `.gitignore`, nothing committed it, and the directory exists on no machine here — so ~3.73M input tokens of frontier data produced **no result**, because the one step a machine cannot do was also the slowest and the artifact did not outlive the wait. A writer produced something its reader could never read, and this row went on naming the path as "exactly one thing remaining" for twelve days. The spend is unrecoverable; the frontier question is **open again and now costs money to reopen**. M545 makes `blind` print that the pack is the only copy, with the one command that preserves it (archiving `grading/` alone keeps the blind, since the arm mapping lives in `.sealed/`).<br>**Superseded in practice (M545):** the operator's standing rule is local and free models only, so the frontier arm is not re-run. A **fresh pre-registration on `jlu/qwen3-coder-next`** asks the question that is actually actionable — the craft section ships **on** by default for every non-`--lite` model, that model is one, and nothing has tested it there. M318 measured a 31B (`jlu/gemma-4-31b-it`, no benefit) and this proposal registered a frontier class; the new run is neither, and says so. | [analysis](analysis/2026-08-06-craft-ab.md), [§7](analysis/2026-08-09-hrz-gateway-findings.md) |

## Open — the graded-attempt cost chain

The measured chain for one 1-point task is **128k → 66k → 29k → 9.4k tokens** (M309, M310,
M312). One lever is still a recommendation rather than a default.

> **Removed from this list (M325b):** *"make `--tool-profile core` the default for `attempt`"*
> carried the reason *"it costs the hint ladder"* — which **M319 and M320 measured away** (two
> models, 24 runs, zero `hint` calls, six of them failures with the tool advertised). M320 then
> refused the flip on a *better* reason and moved it to [`DECISIONS.md`](DECISIONS.md), but this
> row survived with the refuted argument still attached. A register carrying a reason known to be
> false is worse than one missing the row: it invites a future reader to re-open a settled
> question with a dead argument.

| Deferred | Why | Where |
|---|---|---|
| **Making `repoMap: false` the default for `attempt`.** *(reason replaced, M326 — the old one was false)* | The stated blocker was *"tasks 20–22 are explicitly about reading this repository"*. **They are not** — all three are self-contained fixtures, checkable by reading them. The measured reason: 18 runs found no pass-rate difference (4/9 vs 3/9) and a 15–62% token saving, but **+67% and +50% more model calls** without the map, which on the hardest task cancels the saving exactly (408k → 409k). And the leaner arm went 0/3 there — the second time after M320 that a leaner prompt goes 0/3 on the hardest task. To change the default, disprove the call inflation on tasks the model comfortably passes. | [analysis](analysis/2026-08-06-repomap-navigating-tasks.md) |

## Open — from the M321 large-workload measurement

A 34,216-event log from a private third-party workload
([analysis](analysis/2026-08-06-large-workload-telemetry.md)). Its four findings shipped as
M321 (transport diagnosis), M323 (compaction short-fall, observability only), M324 (the `glob`
gap) and M325 (`spawn_parallel`). **One item is left, and it is a design question rather than a
measurement** — which is why it outlived the others.

| Deferred | Why | Where |
|---|---|---|
| **Decide what jichi should DO when mid-turn compaction cannot reach its target.** M323 made the short-fall visible (event fields, a once-per-turn warning, a summary line); the behaviour is unchanged — the request still goes out over the configured `contextLimit`. | Three options, all lossy in different ways, and **the costs are not equal — checked (M326b), where the entry had implied they were**:<br>**(a) drop old messages** — `jc_history_drop_front` exists, and `jc_compact_find_cut` already knows how to snap a cut to a user-message boundary so a `tool_call`/`tool_result` pair is never split. *Cheapest: the machinery is there.* Loses work the agent may still need.<br>**(b) summarize mid-turn** — `summarize_call` is internal to `jc_compact.c`, so no new plumbing, but it means a model call inside a turn that is already over budget.<br>**(c) refuse the call** — **new code**: `jc_text_is_context_overflow` only recognises a *server's* rejection after the fact; there is no pre-send refusal path. Turns a degraded run into a failed one.<br>Picking one is a judgement about what is acceptable to lose, and wants a workload to measure against. **Checked (2026-08-10 sweep): the local telemetry cannot supply that workload -- 364 events since 08-07, zero compact events; the M321 third-party log remains the only pressured corpus.** **A local pressured corpus now exists (M459): 7 mid-turn compactions, 7/7 `unrelieved`.** It did not settle (a)/(b)/(c), and it changed the question. The run's short-fall was caused by an **under-declared window**, not by a genuinely full one: `contextLength` said 32000 while the server's real `max_model_len` was **256000** and it had been accepting ~160k-token requests throughout. So jichi compacted seven times toward a target it never needed, and M323's warning advised shrinking tool output when the fix was one config number. **Before deciding what to DO in this state, jichi now checks whether the state is real** — `last_prompt_tokens` is the server's own count for a request it ACCEPTED, so a served request larger than the declared limit proves the limit understates the model, and the operator is told that instead (`tests/smoke/context_underdeclared.sh`). That removes the commonest false instance of this row's condition from the population it has to decide about; a workload that presses a CORRECTLY declared window is still owed. | [COMPACTION.md](COMPACTION.md) |

## Open — gate integrity

| Deferred | Why | Where |
|---|---|---|
| **Flipping `--strict-green` on by default.** | M332 shipped it opt-in because the flip changes a currently-zero exit code (a stable interface) and the false-positive rate was unmeasured. **Measured (M343, retroactively from 138 existing journals): 0 downgrades in 21 scoped green runs** — the incidental lock-file FP never occurred; the two flagged runs ended non-ok, which strict-green ignores. Still deferred because the evidence is one project and one operator's gates, and the change is to a stable-tier contract — **the operator's call now, with a number instead of a fear**. Re-run `tests/measure/strict_green_fp.py` as corpora grow; a second project's corpus at 0 FPs is the natural strengthening. **Re-run (2026-08-10 sweep): 94 local journals, 16 completed runs, 0 with an edit scope -- nothing strict-green could downgrade either way; the M343 0/21 stands as the only number.** **Re-run (M459, a genuinely second and third project): jichi driven headless against **chrtext** and **zigodot**, each fenced to one named file — **0/2 downgrades**. Small, and said plainly: two runs, each a single-file documentation edit, so this strengthens M343's 0/21 without transforming it. Combined 0/23. **The more useful half of that re-run was a defect in the measurement itself:** the journal recorded only the *count* of edit-scope globs, so `--edit-scope AGENTS.md` and `--edit-scope '**'` were indistinguishable — and seven concurrent fleet runs, all `'**'`, would have contributed seven free zeroes to a rate that cannot be falsified. The journal now records `edit_scope_globs` and the script excludes vacuously-scoped runs from the denominator (older journals are counted as before rather than guessed at). A rate computed over fences that fence nothing is the shape of evidence this row was right to distrust. | [GATE_INTEGRITY.md](GATE_INTEGRITY.md) §8b |
| **A gate rehearsal that proves a goal gate satisfiable** — run the verifier against a stub or hand-completed fixture and confirm green, then red without it (the curriculum's two-sided grader bar, ported to working gates; TEST_INTEGRITY recommendation #1 is its unit-suite sibling). | M343's declaration checks the red side for free (a declared goal must be red at start) but cannot prove the green side: that needs a *reference completion*, which only the operator can supply. The manual discipline is a standing rule (ANECDOTES #38: prove the gate green by hand first). **Revisit when** a run has a natural artifact to rehearse against — e.g. `attempt`'s reference solutions, or an operator-supplied stub patch. | [TEST_INTEGRITY.md](TEST_INTEGRITY.md) |

## Open — invariants known to be incomplete

| Deferred | Why | Where |
|---|---|---|
| **A writer object for the system prompt**, where every append names its section, so a section *cannot* be added anonymously. | Today `sum(parts) == total` catches a section appended after the last mark, and the zero-slot assertions catch most of the rest; an insertion between two *active* slots is credited to a neighbour. **Counted (M326b): the "~40 call sites" is accurate — 38 `jc_sb_append` calls inside `jc_sysmsg_build_parts` (57 in the file, counting the helpers). But the count overstates the *labelling* work: only 16 are section boundaries, which is exactly what the existing `mark()` calls already are.** So the real cost is 38 mechanical call-site changes to route through the writer, not 38 decisions — weaker than the entry claimed, and still not obviously worth converting a misattribution in a diagnostic report into a compile error. | [jc_sysmsg.h](../include/jc_sysmsg.h) |

## Open — tool-output cost

| Deferred | Why | Where |
|---|---|---|
| **Tighten the per-tool output caps automatically when a 0% cache hit-rate is measured.** | jichi knows the hit-rate after a few calls (M326w) and the `--lite` caps already exist, so the pieces are there. **Not done because it changes tool behaviour mid-session based on a statistic** — a `read_file` returning 200 KB yesterday and 32 KB today, for reasons invisible in the config and hard to debug from the outside. `doctor` advises instead. **Revisit when** there is a way to make the adaptation *visible* at the point it happens (the truncation notice naming the reason, not just the byte count). | [TOOL_OUTPUT_COST.md](TOOL_OUTPUT_COST.md) §7 |
| **A `doctor` check for a high re-read ratio.** *(reviewed M503 and deliberately NOT built: the row's own argument still holds -- doctor's advice names a lever the reader can pull, and the lever here is a prompt or a skill doctor cannot check was applied. It needs a second measurement, not a feature.)* | Measured at **72%** in one workload — 2,056 `read_file` calls over 584 distinct paths, one path read 216 times — which is a loop: compaction elides the read, the model re-reads, compaction elides it again. Computable from telemetry exactly as the M316 unused-tools check is. **Not done because it is advice about the AGENT's behaviour, not the operator's configuration**, and doctor's other advice all names a lever the reader can pull. The lever here is a prompt or a skill, which doctor cannot check was applied. **Revisit when** a second workload confirms the ratio is high generally rather than specific to one agent on one codebase — **and measure that workload post-M348**, which attacked the loop mechanically: the elision marker is now a claim ticket naming a preservation-store path, so the re-read the loop consists of has a cheap targeted substitute. If the ratio collapses, this row closes without doctor ever advising. **Measured (M459, post-M348, first pressured corpus): 0% — 14 `read_file` calls over 14 distinct paths, no path read twice**, on an unbudgeted read-heavy run over 16k lines of a real codebase that compacted seven times while doing it. Directionally this is what M348's claim ticket was for: the run read every module once, under the very pressure that is supposed to cause the loop, and never went back. **It is NOT evidence and this row stays open**: 14 calls against a 2,056-call reference cannot confirm or refute a 72% figure, and quoting "0%" as a refutation would be the overclaim this row was written to avoid. The measurement is now a committed script, `tests/measure/reread_ratio.py`, which carries a 50-call floor and prints NOT EVIDENCE below it — so the next person to run it cannot accidentally close this row with a handful of doc edits. | ROADMAP M326z |

## Open — compaction

| Deferred | Why | Where |
|---|---|---|
| **A mid-turn mechanism for turns eliding cannot save.** **Measured at M588 — the pressured corpus finally exists, and it argues against building the mechanism first.** | The row's revisit condition was *"a workload's `unrelieved` share measured on a post-M326y log"*, and the 2026-08-10 sweep found **zero** `compact` events to measure. An overnight autonomous run on a 9B model at a **65,536** window produced one: **29 mid-turn passes, 100% pressured, 27 of 29 (93%) UNRELIEVED, 28 SHORT**, and reclaim that was **100% lossy** (`dup=0`) because the zero-loss dedup found nothing to dedup. So turns eliding cannot save are real, and on that configuration they are almost all of them.<br>**But the control run says the mechanism is not the first lever.** The same jichi, the same envelope shape and near-identical per-call input (~43k vs ~48k) against a **196,608** window: **2 compactions, both zero-loss, zero pressured.** The arithmetic is the whole story — system 11,536 + tool definitions 4,732 = **16,268 fixed tokens before any history**, which is 25% of a 65k window and 8% of a 196k one. Elision can only touch history, keep-recent protects the newest of it, so the pass ends above the line and re-triggers. **Mid-turn summarization would be treating a symptom of mis-provisioning.** The cheaper levers, in order: size the window to the work, then cut tool output (`readMaxBytes`), then prune tool definitions. **Revisit the mechanism when** a workload presses at a window that is already generously sized — this corpus is not that. | [proposals/2026-08-observability-seams.md](proposals/2026-08-observability-seams.md) |

## Open — lessons that become checks (M602)

| Deferred | Why | Where |
|---|---|---|
| **`hook:` bullets under `## Checks` — a lesson committed as a `PreToolUse` hook.** | A hook is a shell command in `config.json` behind `hooksEnabled: true`, top-level only, that can block a tool with exit 2 (`HOOKS.md`). It is the stronger bridge from a lesson to a refusal — arbitrary predicates, not the constraint scanner's eight phrasings — and exactly for that reason letting the learning loop write one is a larger trust decision than committing a constraint: the loop would be authoring code that runs on every tool call. M602 counts such bullets as *unsupported* and says so in the apply summary. **Revisit when** a real draft proposes a check the constraint vocabulary cannot express and a human would have written the hook by hand anyway — then the design is a `## Checks` kind that writes a script under `.jichi/hooks/` and prints the `config.json` lines for the human to paste, never enabling it itself. | ROADMAP M602, [analysis/2026-08-27-the-language-of-lessons.md](analysis/2026-08-27-the-language-of-lessons.md) D4 |

## Open — teaching and documentation

| Deferred | Why | Where |
|---|---|---|
| **An `init` option that scaffolds the records tree as `.org` instead of markdown.** | The format itself is a one-line change in the scaffold tables; the *cost* is that shipping `.org` assets pushes users toward one editor, which is exactly what M326s decided against. **Revisit when** two users ask for it — an observable trigger, per this page's own rule, and cheap to honour once someone has. (Deliberately described without inventing a flag spelling: `docs_flags.sh` scans this page — unlike `DECISIONS.md`, which is excluded — so naming a switch here would document one that does not exist. The first draft did, twice: once in the entry and once in the note explaining why not to.) | ROADMAP M326s |
| **A graded assignment for the records practice.** | Designed as `74-your-own-registers` and dropped at M326s: the checker could only grade the *shape* of a register — it cannot know whether a decision was real, whether the dates are true, or whether `Where:` points anywhere. **Revisit when** there is a way to grade the habit rather than the headings; a fixture check is not one, and adding it would cost two count bumps, a grader entry and an INDEX row for a check nobody should trust. | [DECISIONS.md](DECISIONS.md) |
| **A seeded fuzz-lite harness for the pure cores** (`jc_patch`, `jc_utf8`, `jc_jsonrepair`, `jc_glob_match`): a tiny C89 LCG with fixed committed seeds, so "random" inputs are byte-reproducible and every failure becomes a permanent regression case. | The 2026-08-10 procedural-generation determination: the pure cores' current failure findings come from real workloads — the honest source — and a fuzz harness deserves its own milestone with its own teeth (a generator that has never found a planted bug has never been seen working). **Revisit when** a pure-core defect ships that seeded input generation would plausibly have caught. | [analysis/2026-08-10-guidance-and-crown.md](analysis/2026-08-10-guidance-and-crown.md) |

## Open — JupyterHub and notebooks (M478)

| Deferred | Why | Where |
|---|---|---|
| **`read_file` renders an `.ipynb` to cells**, the way M42's PDF path renders a PDF (detect the extension, transform to text, never mark it read-before-edit because it is not editable that way). | jichi has **zero** notebook support (`grep -rc ipynb src/ include/` is empty), and M478 measured what that costs: a notebook with one figure is **~65,631 tokens** — over the 256 KB read cap, so truncated as well — against **~255** for the same code as a jupytext-paired `.py`. **257×.** The fix is modest and in character. **Not started because the use case is undecided:** the request that prompted M478 was a loose "can this be used with JupyterHub?", and the answer to "do your learners live in notebooks?" is not yet known. **Revisit when** someone states that notebooks *are* the workflow — the 257× figure is the argument, and [JUPYTERHUB.md](JUPYTERHUB.md) §14 is the decision aid. Full cell-**editing** is recommended against separately: a new corruption class in exchange for something `jupytext` already delivers. | [JUPYTERHUB.md](JUPYTERHUB.md) §4 |
| **The browser half of the terminal contract** — whether xterm.js or the browser wins for the keys jichi binds (Ctrl-R against page-reload, Ctrl-G against Firefox's find-next), plus browser paste and browser resize. | M478's rig speaks terminado's **websocket**, so it tests jupyter-server completely and xterm.js not at all — a websocket cannot press a key in a browser. This is **not** a deferral for cost reasons: it is ten minutes with a browser, and the nine-item checklist already ships with `scripts/jhub-verify.sh`. It is deferred because it needs a **human at a display**, which no rig here can be. **Revisit when** anyone runs the course, or sooner — it is the cheapest open item on this page. | [JUPYTERHUB.md](JUPYTERHUB.md) §13 |
| **A German *Einfache Sprache* counterpart** for the JupyterHub learner section. | [`PLAIN_LANGUAGE.md`](PLAIN_LANGUAGE.md) has a German **original** ([i18n/de/EINFACHE_SPRACHE.md](i18n/de/EINFACHE_SPRACHE.md)) — *Einfache Sprache* is a defined register with its own rules, and the English page is its sibling, not its source. The new section was written in English first, which inverts that relationship for this one section. The phased-i18n policy makes English canonical, so this is a known cost rather than a defect. **Revisit when** a German-speaking cohort actually uses a hub — and note it needs a **writer of the register**, not a translation pass. | [PLAIN_LANGUAGE.md](PLAIN_LANGUAGE.md) |
| **Shape D: the web bridge behind `jupyter-server-proxy`.** | `examples/web-bridge/bridge.py` already uses "the Jupyter model" (a boot token) and binds loopback; a hub could put its own authentication in front of it at `/user/<name>/proxy/<port>/`, supplying the one thing the bridge deliberately lacks — real accounts. Attractive, and **entirely unrun**. Two specific unknowns: whether Server-Sent Events survive the proxy without buffering, and whether the bridge's own token still earns its place once the hub authenticates. **Revisit when** someone wants a browser UI rather than a terminal; until then the terminal is the recommendation and costs nothing. | [JUPYTERHUB.md](JUPYTERHUB.md) §3 |

## Open — peer transport buffers (M609)

The 2026-08-27 hardening survey (`docs/analysis/2026-08-27-what-the-structure-claims.md`
§5) found three peer transports that accumulate an inbound message without a byte
bound, where the SSE layer (`JC_SSE_FIELD_MAX`) and, since M609, the LSP framer's
header block (`JC_LSP_MAX_HEADER`) are bounded. M609 fixed the LSP framer because it
is a pure function with a born-red unit test; the other two are real but their
deterministic red test needs a memory-pressure harness the smoke tier does not have,
so they are recorded rather than half-proven.

| Deferred | Why | Where |
|---|---|---|
| **The MCP stdio `rbuf` line buffer is bounded only by a 120 s deadline, not by bytes.** A server that streams a line with no newline grows it until the OOM killer fires (`exit 137`). | The fix is a one-line cap in the `read_line` loop (`return JC_ERR_IO` past a ceiling, the path a closed server already takes); the block is a born-red test, which needs to feed >cap bytes and assert bounded RSS. | `src/mcp/jc_mcp_stdio.c:240` |
| **The ACP `inbuf` line buffer has no byte cap and no deadline.** Anything on jichi's stdin under `--acp` that never sends a newline grows it without limit. | Same one-line fix (set `s->eof` past a ceiling, which the loop treats as a closed client) and same test gap. | `src/acp/jc_acp.c:159` |
| **MCP and LSP shutdown block in `waitpid(pid, …, 0)` after `SIGTERM`.** A server that traps or ignores SIGTERM hangs jichi's exit forever — no journal finalisation, no lease release. | `jc_worker_reap_grace` (the SIGTERM→300ms→SIGKILL reap the parallel pool and the daemon use) is the drop-in; the born-red test is a TERM-trapping mock server and a bounded-exit assertion, deterministic but fiddly in POSIX sh. | `src/mcp/jc_mcp_stdio.c:300`, `src/lsp/jc_lsp.c:1195` |

## Open — fence hardening and isolation

| Deferred | Why | Where |
|---|---|---|
| **A shell/interpreter sandbox** — real OS-level containment of what `run_terminal_command` (and any program it launches) can read and write. | This is the project's oldest safety deferral, and until now it was **invisible from the register built to make deferrals findable** — it lived only in ANECDOTES #12 (*"a fuller shell sandbox is deferred"*) and `proposals/2026-07-privileged-commands.md` §Deferred, both predating this page's M298 coverage window. Recorded here so it is findable. **Checked (M326b):** no `seccomp`/`bubblewrap`/`firejail`/`unshare`-as-sandbox exists anywhere in `src/` or `docs/`; the file-tool fence (incl. `search_code`, M383) does not cover the shell, and M83 detection is writes-only / in-git-tree-only / post-hoc (GATE_INTEGRITY.md §9.1). **The honest position is that this is not a C feature:** a userspace heuristic cannot contain a determined program, so the real answer is deployment — run as a non-root user, in a container/VM (DEPLOYMENT.md §5, AUTONOMOUS_LOOPS.md's systemd unit). **Revisit as** a *documented `bwrap`/`unshare` launcher recipe* wrapping the whole process (not per-tool), and — separately and only behind an explicit opt-in — the heuristic path-screen of GATE_INTEGRITY.md §9.3-C, which is defense-in-depth with a stated miss list, never a wall. | [GATE_INTEGRITY.md](GATE_INTEGRITY.md) §9 |
| **The path fence's check-then-open window** (M472, audit L3). `jc_path_in_root` resolves with `realpath()` and returns a verdict; the `open()` happens afterwards, so a path component swapped in between is a classic TOCTOU. | **Analysed and left, with the reasoning stated, because the threat model makes it near-worthless to fix.** The attacker who could win the race is a process running concurrently as the same user -- in practice the model's own background shell. But a model with shell access does not need to race the fence: it can write the file directly, which is [the shell-sandbox deferral](#) above and this project's oldest safety gap. The fence is the ONLY door in exactly one configuration, `--edit-scope --strict-scope`, which forbids `run_terminal_command` -- and there, by construction, there is no concurrent attacker to swap anything. So the window is reachable only where it does not matter, and closed where it would. **The cost of fixing it anyway is not small:** the file tools go through stdio (`fopen`), which has no `O_NOFOLLOW`, so it means converting the file I/O layer to `open()` + `fdopen()` plus an `fstat` re-check across five libcs and three non-Linux kernels -- a portability risk taken for a threat the same configuration already answers. **Revisit when** a sandbox lands (which changes the concurrency assumption), or when a file tool gains a caller that runs while an untrusted process shares the workspace -- a multi-tenant daemon would be the shape. | [analysis/2026-08-17-source-hardening-audit.md](analysis/2026-08-17-source-hardening-audit.md) L3 |
| **The popen path's descriptor total** (M472). Every child jichi forks and execs itself now inherits stdio and nothing else -- `jc_proc_child_close_fds()` closes the range, `jc_fd_cloexec()` marks the sinks and sockets at creation, and both are pinned by `posix_utils_lint.sh` checks 6/8 and `tests/smoke/child_fds.sh`. A command run WITHOUT a `timeout` goes through `jc_proc_popen`, where the fork happens inside libc, so no jichi code runs between fork and exec and the close-range backstop structurally cannot reach it. | **One pipe pair still arrives there, and it is libcurl's, not jichi's** -- created without `O_CLOEXEC` in curl's internals, where jichi has no hook (`CURLOPT_SOCKOPTFUNCTION` covers sockets, not pipes). Measured by strace: jichi's own pipes are all marked, libcurl's is not. The three descriptors with a demonstrated exploit -- the run journal, the telemetry sink, the provider socket -- are closed on BOTH paths, and this residual is not a sink, a socket or a secret; a child can hold it open or write bytes nobody reads. Recorded rather than papered over, because `child_fds.sh` deliberately asserts the total on the fork/exec path only, and a reader of that driver should know why. **Revisit as** routing model-issued commands through jichi's own fork/exec path unconditionally (the `timeout` argument already selects it), which would retire `jc_proc_popen` for tool execution and make the total hold everywhere -- a behaviour change with its own milestone, not a line in this one. | [analysis/2026-08-17-source-hardening-audit.md](analysis/2026-08-17-source-hardening-audit.md) §H2 |
| **Mid-run one-off fence exceptions** — an interactive "grant read of this path for the session?" prompt when a file tool hits the fence (D1), designed but unbuilt. | Designed in the item-7 proposal, not built this batch. **Reads only, interactive front-ends only** (a human answers); writes stay a pre-run decision (rollback cannot undo an out-of-tree write) and are the proposal's D2 deferral; the silence policy is settled (no timeout — structural, D4). The full implementation design — decisions with rejected alternatives, the safety invariant, the negative-test-first sequencing — is [plans/2026-08-tui-fence-grant.md](plans/2026-08-tui-fence-grant.md). **Revisit when** a workload shows the occasional-unforeseen-external-read case is common enough to beat re-launching with `--reference-root`. | [plans/2026-08-tui-fence-grant.md](plans/2026-08-tui-fence-grant.md) |

## Narrowed at M582 — localized presentation decks

| Deferred | Why | Where |
|---|---|---|
| **Bringing the four localized decks up to the English decks' CONTENT** (de · es · ja · zh). Four decks are one to two slides short: `00-super-features` (−1), `03-roadmap` (−2), `04-university` (−2), `05-school` (−1), in every language. | **The figures half is done (M582)** and is now gated. What is left is prose: the English decks gained sections, and translating them is writing in three languages this repository cannot review. Each of the sixteen files declares the gap with `<!-- slides-behind: N -->`, the count is checked, and the declaration re-fires when the English deck moves again — so the gap is bounded and visible rather than silent. **Revisit when** a reviewer for that language exists (see the row below). | [i18n/README.md](i18n/README.md) |
| **`PROJECT_TIMELINE.md`: figures behind the M579 recount** — **ja is down to 4** (M587); de · es · zh remain at **22**. | M587 brought across everything in the Japanese page that is a *pure numeral*: the 17-slice subsystem pie chart, the tool count inside its label, and five summary rows. The four that remain are each welded to prose that would become false — the test row's breakdown changed shape in English, and the proportion and commits-per-day tables are each followed by a paragraph that *interprets* them (English now argues "documentation now outweighs source"; the daily table was recounted, Jul 24 is 384 not 378, and extended by a month). Redrawing bars is arithmetic; the prose around them is not. **Revisit when** a Japanese writer can take the page as a unit. de/es/zh were left untouched deliberately — the same split applies and none has a reviewer. | [i18n/README.md](i18n/README.md) |
| **`docs/i18n/ja/PROJECT_TIMELINE.md` owes a corrected third-party row** (deleted at M587, not rewritten). | The row said the bundled cJSON is *"not authored by this project"*. That is **false**, and it contradicts [LICENSING.md](LICENSING.md), the README and the English page, all of which state `src/json/cJSON.{c,h}` is original code (M171) — **and LICENSING.md's argument that the licence choice is unconstrained rests on exactly that**. The translation was *faithful when made*: English carried the same claim until **M498 (2026-08-20)**, four days after this page's tracked commit, and the correction never propagated. That is the failure `tracks:` exists to expose and `i18n_tracks_lint` now gates. Deleting a false claim needs no Japanese; writing a true one does, so the row is **gone and owed**. **Revisit when** a Japanese reviewer can add the corrected sentence. | [i18n/README.md](i18n/README.md) |
| **Native review of the es · ja · zh decks and prose.** | M582 corrected **numbers only**, on the maintainer's explicit instruction, because a wrong number is wrong in every language while phrasing is not auditable without a reader. Two edits sit at that boundary and are named rather than hidden: the four decks' figures now read `10,000+` / `11,000+` where English says "over" (a `+` sign was chosen precisely so no "more than" word had to be invented in three languages), and `docs/i18n/zh/PHILOSOPHY.md`'s CJK numeral `四千五百多` became `一万多`. Both are numerals; neither has been read by a native speaker. Japanese has a route — `llm-jp` as a first-pass reviewer (M580), then the maintainer's friends. Spanish and Chinese have none. | [i18n/README.md](i18n/README.md) |

**What the old row got wrong, kept because it is the same failure this section is about.** It listed the stale figures as ``107``, ``~770 KB``, ``~85,000`` and ``~700 KB``, "verified by grep". Re-measured at M582: **only `~700 KB` was still present**; the other three had been gone for milestones. A deferral row that cites grep evidence rots exactly like the documentation it describes, and nothing checked it either. The row also recorded *"deliberately NOT linted: the parity check would have to parse four languages' number prose"* — that premise was false and cheap to test, and the lint that does not parse prose is [`tests/smoke/i18n_tracks_lint.sh`](../tests/smoke/i18n_tracks_lint.sh); see the M582 row in [DECISIONS.md](DECISIONS.md).

## Closed at M499 — documentation owed to the self-learner

The four rows here were the milestone-sized items from
[analysis/2026-08-12-docs-review.md](analysis/2026-08-12-docs-review.md) (four
reviewers, 30 pages, one rubric). All four are done — and **one of them had
already been done and the row had rotted**, which is the M326b rule earning its
place again: check the checkable part of a reason before acting on it.

| Was deferred | Closed by |
|---|---|
| A "where your state lives" page, and the source-checkout prerequisite | [STATE.md](STATE.md) — all 15 `~/.jichi.d/` subtrees plus the file INSTALL's table omitted (`~/.jichi.env`, the one holding a secret), what is irreplaceable, and the exact `make install` manifest with the consequence stated: the course lives in the tree, not in the binary. |
| The tool-call decision chain, as one page | [TOOL_DECISIONS.md](TOOL_DECISIONS.md) — the nine steps in the order the code runs them, read out of `jc_agent.c` rather than summarised, including the three that surprise readers: an ALLOW verdict does not mean it runs, your "yes" is not the last word (the scope fence and the hook come after it), and headless refuses an ASK rather than proceeding. |
| Five tutorial-shaped sections inside reference pages | Written, and pinned by `tests/smoke/self_learner_lint.sh` check 3 — a tutorial section inside a reference page is the least defended documentation there is, and deleting one was a two-line diff nobody would question. |
| The curriculum's lone-learner gaps | **Four of the five had already shipped** at M396/M406 and the row had not been updated: the skip rule is stated in `assignments/INDEX.md` ("the margin is exactly one 3-point task… leave it, go forward, come back later"), the four `cc`-needing specs carry prerequisite boxes, and CURRICULUM §Who it is for already routes both the process track and `PLAIN_LANGUAGE.md` and names the four INSTRUCTOR sections a lone learner should read. What was genuinely missing was the **cross-reference from the map**: a learner reading "gate: 14/17" saw a threshold, not a permission. One paragraph, now in CURRICULUM.md. |

Also closed with them: register item 15, *definitions of the load-bearing words
before use* — [VOCABULARY.md](VOCABULARY.md), 48 terms, with the 24 the review and
the smoke tier force on a reader pinned by check 4. And the **name trap**: a reader
looking for "what does posture mean" opened `docs/GLOSSARY.md` and found a config
page (the `docs/DOCS.md` shape), so that page now carries a sign in front of it,
pinned by check 5.

**Still open from that review, and deliberately:** the items whose value is a
judgement rather than a structure — item 18's promised user-story tutorial, item 19
(`jichi assignments` as an orientation, which is the row below), and the prose-level
findings the register carries. A lint can hold a heading in place; it cannot make a
page good.

## Open — assignment and grading support

Found by the M398 workflow review of the assignment/tutoring documentation; the
workflows themselves shipped, these are the product gaps they had to document
around ([TEACHING_ASSIGNMENTS.md](TEACHING_ASSIGNMENTS.md) § "What this feature
does not yet support well").

| Deferred | Why | Where |
|---|---|---|
| **`jichi assignments` as an orientation rather than a flat list** — stage/module grouping, per-stage point totals, and a filter. | It prints all 77 specs name-sorted with phase/points/status and no grouping, so a day-one learner sees tracks they cannot run, and the stage gates stay arithmetic done by hand against INDEX.md. **Checked (M326b):** the data is all there — `--output json` already carries `phase`, `difficulty`, `points`, `solution` and status per row, so this is presentation, not plumbing. **Not done because the grouping belongs to the CURRICULUM's structure, not the directory's**: stage membership lives in INDEX.md's prose tables, and teaching the binary to parse those would couple the tool to a document's formatting. **Revisit when** either the specs carry a `stage:` frontmatter key (cheap, and `jc_assign` already ignores unknown keys) or the totals are wanted badly enough to justify it. | [assignments/INDEX.md](assignments/INDEX.md) |
| **A cohort view for teachers** — one command that reads many learners' `progress.jsonl`. | `jichi assignments` reads exactly one workspace, so thirty students are thirty benches with no aggregate; the documented answer is a shell loop collecting `--output json`. **Not done because it is a gradebook**, i.e. someone else's software: it needs identity, storage and a policy about grades, none of which belong in a coding agent (the M165 web-frontend reasoning applies — jichi provides the machine surface, a sidecar owns the aggregation). **Revisit as** a documented recipe or an `examples/` script rather than a subcommand. | [SCRIPTING.md](SCRIPTING.md) |
| **A per-spec "if you are stuck alone" line** — present in 7 of 77 specs (the process track only). | The stuck path is complete but lives on the module pages, so it reaches a learner who navigates module-first and misses one who arrives from `jichi assignments` or the index. The escalation ladder is now documented once in TEACHING_ASSIGNMENTS (M398), which is the cheap half; putting one line in each spec is 70 small edits and wants a template pass rather than hand-editing. **Revisit with** the next assignment-authoring milestone, so the footer template changes once. | [analysis](analysis/2026-08-12-docs-review.md) |

## Open — unit-suite integrity

| Deferred | Why | Where |
|---|---|---|
| **A `{name, fn}` table for the unit runner**, replacing `test_main.c`'s 146 hand-written `printf(name); test_name();` pairs. | It is the single enabler for three things at once: a per-function `jc_test_checks` delta (so "every test contributes ≥1 check" becomes checkable with no baseline), per-test selection (the prerequisite for the M201 re-ask below), and a cleaner ground truth than grepping call sites. **Not done because it touches the instrument** — failure mode 1 is "the instrument is broken" and M201's incident was exactly that, so it needs its own milestone with its own teeth: the total check count identical across the change (11,305 → 11,305) and a planted failure still reported with the right name and count. **Checked before parking (M326b):** `jc_test_checks` is a plain global incremented by the JC_CHECK macros, so the delta is trivial once the loop exists; and `test_main.c` takes no `argc`/`argv` at all, so selection genuinely cannot be bolted on without it. **Revisit when** either the ≥1-check assertion or the isolation sweep is wanted — neither is reachable without it. | [TEST_INTEGRITY.md](TEST_INTEGRITY.md) rec 3/4 |
| **The M201 re-ask for the unit suite** — run each of the 146 test functions alone and compare with the in-suite result, both directions. | Blocked on the table above (no per-test selection exists). The expectation is honest: probably **zero** findings — `test_msg` already restores the process-wide language with a comment saying why, so the discipline exists — and a clean zero is a result worth recording (the M343 0/21 precedent), not a reason to skip the measurement. What it could find is order dependence via the `jc_msg` language global, registered redaction secrets, log level, locale, cwd, or a static cache. **Revisit with** the table, as its first customer. | [TEST_INTEGRITY.md](TEST_INTEGRITY.md) rec 4 |

## Open — externally blocked

| Deferred | Why | Where |
|---|---|---|
A reason can also rest on **someone's statement** rather than on evidence or judgement, and the
M326b rule applies there too — not by checking the statement (I cannot read anyone's email) but by
**saying whose it is**, so a future reader knows what kind of thing they are looking at. Two of the
three rows below are of that kind, and their elapsed time is stated because "we asked once" quietly
becomes "we asked months ago and never followed up".

| Deferred | Why | Where |
|---|---|---|
| **The licence, and the public repository.** | Waiting on a JLU rights answer. **Operator's statement:** the email was sent **2026-07-27** — *21 days* as of the 2026-08-17 sweep (14 at the
2026-08-10 one; the count is re-derived each sweep precisely because a stale "we asked
recently" is how this row would rot); not verifiable from inside the repo, so it is recorded as their account, and the elapsed count is the thing to watch. **Checked (M326b):** no `LICENSE` or `COPYING` file exists, so "the first public commit needs it" holds. (README's *"Apache-2.0; status verified 2026-07-28"* is about **Continue**, the upstream specification — not jichi's own licence, which README separately calls *leaning Apache-2.0, deferred*.) | ROADMAP ★ TODO |
| **The logo (SVG + PNG).** | **Operator's statement:** they are drafting it and will bring it before release. Nothing here to check or do. | ROADMAP M307 |
| **Robotics Tier B (motion) and Tier C (the reflex layer).** | Deliberately human-gated: a person on the physical E-stop. **A judgement, not a schedule** — and the one row on this page that should never acquire an unblocking condition. | [ROBOTICS_BRINGLIST.md](ROBOTICS_BRINGLIST.md) |

## Closed at M503 — four signals that were silent, absent, or invisible

Reviewed as a batch with the operator, and the review itself is the lesson: of
five rows picked as "ready to build", **two turned out to argue against being
built** once read in full. Checking the reason before acting on it (the M326b
rule) is now three-for-three this month.

| Was deferred | Closed by |
|---|---|
| **Say something when a line typed before the first prompt is discarded** (M464) | `jc_term_readline` now probes for buffered input before the `TCSAFLUSH` that discards it, and says so: *"input typed before this prompt was discarded … please retype it."* The bytes are deliberately **not** recovered — the flush is what stops stray type-ahead from answering a `y/n` prompt nobody has read — so the honest fix is a notice with a way forward, not a smuggled line. The probe reuses M156's existing `input_pending` rather than adding a second readability check that could drift. `preprompt_discard.sh`, 3 checks (one asserting the stray text still never reached the model), proven red. |
| **Verify that a private file actually became private, instead of trusting `chmod`** | `doctor` now creates a probe under `~/.jichi.d`, tightens it, and **reads the mode back**: `✓ created 0664, tightened to 0600, read back`. On a filesystem that accepts `chmod` and ignores it — MSYS2's default `noacl` mount, some network mounts — it reports *"private files are NOT private on this filesystem"* and names what is exposed (the API key file, the daemon socket, the audit log). **The policy question the row left open is answered by precedent:** WARN interactively, **FAIL under `--unattended`**, joining M158b's explicit escalation set for the same reason `privilegedAudit: false` is in it. It also states what it did *not* exercise: under a narrow umask the probe is already private before the tightening runs, so the guarantee is verified while the mechanism is not. Proven with a new `JC_FAULT_CHMOD` site — the same justification as `JC_FAULT_PROCFS`, since there is no `noacl` mount on the bench and the branch guards a safety property. `faults.sh` checks 9-10. |
| **An `--auto` run silently inherits `testCommand`, and there is no opt-out** | **Half of it had rotted:** `--verify ""` already disarms the gate, inherited or not — measured, and it was simply undocumented. The half that was real is now built: the journal's `start` event carries **`verify_source`** (`flag` \| `config` \| empty), because `verify: make test` used to mean both "the operator chose this" and "the config supplied it", and those differ sharply when the gate then fails. A deliberate `--verify ""` records as `flag`, so a disarm is a decision rather than an absence. Documented in [AUTONOMY.md](AUTONOMY.md) §"Where the verifier comes from"; `verify_source.sh`, 4 checks, proven red. |
| **A `doctor` line for the BSDs, illumos, and any other POSIX host** | **Declined, on the row's own evidence.** It already said the M400 note fires on any non-Linux `uname`, that no BSD-specific code exists, and that "inventing conditionals for a platform nobody has is how the Darwin branch got into the state M400 found it in". Then M460/M461 measured it: FreeBSD and OpenBSD found **ten real defects between them and none of them wanted a BSD conditional** — every fix was a capability probe, a portable flag, or POSIX-correct signal discipline. A row whose reason has been confirmed by measurement is not deferred work; it is a decision. |
| **Identify the unit check that failed once during M407's gate** | **Closed as unidentifiable.** One occurrence, never reproduced, no artefact kept, and nothing left to examine. The honest action is to say so rather than carry a row nobody can act on — and to point at the mechanism that would catch the next one: *the M201 re-ask for the unit suite* (still open below), which labels a failure "in-suite only" or "also alone" instead of leaving it a mystery. |

## Open — the source-reading guides

From the M399 review of `docs/reading/` (25 files measured against their own
documented chapter skeleton; Annai measured complete and uniform, Fukabori
measured thin in one specific way).

| Deferred | Why | Where |
|---|---|---|
| **Bring the remaining Fukabori chapters to Annai's code density.** | Measured: Annai's nine numbered chapters carry **2–3 code/pseudocode blocks each**; Fukabori's twelve carry **one** (and chapter 3 carried none until M399 fixed it). The *expert* guide shows less code than the *beginner* guide, which is inverted — and FUKABORI.md's own conventions promise "the invariants first, then **the code that carries them**, then the failure that taught them". **Not done wholesale because the fix is per-chapter judgement, not a sweep:** each needs the *one* excerpt that carries its argument (chapter 3 wanted the wrong-arena line and its fix; chapter 7's would be the fork/select shape; chapter 8's the read-callback), and a block added to hit a quota would be decoration, which the house diagram rule already forbids. **Revisit** one chapter at a time, cheapest first, whenever that subsystem is being touched anyway. | [FUKABORI.md](reading/FUKABORI.md) |
| **A lint that each reading chapter carries a diagram or is a stated exception.** | Three Fukabori chapters have no mermaid (1 "why C89", 11 "AI-supported coding examined", 12 — the last now has one). **Checked before parking:** 1 and 11 are *arguments*, not mechanisms, and a diagram restating prose is decoration — the UML tutorial's own rule is "one diagram, one question", so the honest state is an exception list, not a missing diagram. A lint would therefore encode a two-item allowlist and check almost nothing, which is the "checks zero things and reports success" shape this project's lints are written to avoid. **Revisit if** the exception list ever grows past a handful — that would mean the convention had actually drifted. | [reading_refs_lint.sh](../tests/smoke/reading_refs_lint.sh) |

## Open — platforms never compiled

From M400, which asked the honest form of "we have not built this on macOS or WSL":
[`PLATFORMS.md`](PLATFORMS.md) now owns every platform verdict, and these two are
measurements this project does not have.

| Deferred | Why | Where |
|---|---|---|
| **Compile jichi on macOS and run `make check-target`.** | **Never compiled — not once.** Not deferred out of doubt: there is no Mac on this project. **Checked before parking (the M326b rule):** the old claim "no Darwin-specific code" in both `BUILD.md` and `INSTALL.md` was **false** — `jc_mem_total_mb` has an `#if defined(__APPLE__)` `sysctl(HW_MEMSIZE)` branch, and it used `unsigned long long` + `ULL` constants, which is three diagnostics under this project's own mandatory `-std=c89 -pedantic -Wall -Wextra` and a failed build under `WERROR=1`. So the honest state was worse than "untested": jichi's only macOS code **could not have compiled**, and no build here would ever have said so. Fixed and pinned by a lint in the same milestone. **Revisit** the moment anyone has access to a Mac for an afternoon; one `uname -srm` + two check counts turns a "never compiled" row into a verified one. | [PLATFORMS.md](PLATFORMS.md) |


## Closed — done, and formerly left sitting under an *Open* heading

Four rows that were **done and still filed as open**. Three were reported at M475 and
fixed at M488; the fourth is M475's own WSL2 row, marked **DONE (M475)** and left for
thirteen milestones under a heading reading *platforms never compiled* — about a
platform `PLATFORMS.md` now records as **Verified, full gate**.

**Moved here rather than left annotated in place,
because that is what went wrong:** the closure was appended to the *end* of each row,
behind a reason still written in the present tense, so `--api-base` went on reading
*"there is **no** base-URL flag"* for four milestones after `main.c` began parsing one.
A reader scanning the register met the stale assertion and never reached the closure —
and all three sat under an **Open** heading while doing it. Found by the
`feature/hrz-model-info` session while landing (M492), which noted it is the same shape
as the `docs_flags.sh` `future` entry that same branch removed: a row saying *"move this
line out when the flag ships"* that outlived the shipping by a milestone.

This page says at its head that items are **removed** when done; ten `## Closed` sections
say the practice is gentler. Either is fine. A closed row under an *Open* heading is not.

| Deferred | Why | Where |
|---|---|---|
| **Stop `jc_eventlog_open` tightening a parent directory it does not own.** | **Found 2026-08-18 (M475); reported, not fixed.** `make_parent_dir()` calls `jc_make_private()` on the log path's parent **even when that directory already existed**; the guard catches `/` and nothing else. Run as root — every container, most CI — `--log /tmp/jichi.jsonl` turns `/tmp` into 0700 root-only for the whole machine (measured: 1777 before `run_tests`, 700 after; call site captured with an `LD_PRELOAD` shim on `chmod` and PIE offsets resolved against the symbol table). Non-root is inert **by accident**: the `chmod` fails with EPERM and the return is discarded. **Deferred because the fix is a design call, not a patch** — skip pre-existing directories, refuse when the owner differs, or tighten only what `jc_mkdir_p` actually created; each changes M132's privacy guarantee differently and M132 was deliberate. Note that `jc_platform_posix.c` already reasons about this exact hazard for the state root (*"jc_make_private applies a mode to whatever the path RESOLVES to"*) and guards it with an `lstat` owner/mode check — **the sibling two files away has none.** When a hazard earns a documented fix, grep for its family. | `src/util/jc_eventlog.c`  **CLOSED (M488).** Fixed as a FAMILY rather than a patch: `jc_mkdir_p_private()` creates the chain and applies 0700 **only to what it created**, and all four `jc_mkdir_p`+`jc_make_private` sites route through it — two of which (`--log`, `--control`) take the path from the operator, so the hazard was never confined to telemetry. `posix_utils_lint` check 7c bans the old pairing, proven two-sided. **The design question is answered and the rejected options recorded:** refusing on an owner mismatch turns a legitimate shared directory into an error, and chmod-ing only when the mode is wider than 0700 still re-permissions `/tmp`, just conditionally. M132 is unchanged for everything jichi owns — a directory it creates is still 0700, the file still 0600. Unit-tested both ways (`test_parent_dir_not_retightened`, `test_created_dir_is_private`).|
| **Stop a failed precondition in `test_session_roundtrip` from crashing the suite.** | **Found 2026-08-18 (M475); reported, not fixed.** Reachable once the row above has poisoned `/tmp`: a non-root `make test` reports `FAIL tests/test_session.c:759` (save failed), then four more FAILs that read a `struct jc_session` the failed `jc_session_load_by_id` never populated, then `free(): invalid pointer` / SIGSEGV — deterministic across three runs. `JC_CHECK` is non-fatal **by design**, so a failed *precondition* is followed by code dereferencing the object it was meant to produce. Worse, the abort discards block-buffered stdout, so the FAIL lines that name the cause vanish and the reader gets a bare `Aborted (core dumped)` — `stdbuf -o0` was needed to recover them. **Deferred because the fix is a design call:** leave the block when a precondition fails, or make precondition checks fatal suite-wide (which changes every test's semantics). | `tests/test_session.c`  **CLOSED (M488).** Reproduced deterministically **and without root** — pre-create that test's own sessions directory read-only, and it exits **139**. The fatal step turned out not to be the reads named above but `jc_session_free(&back)` on **uninitialised stack memory**: `jc_session_load_by_id` correctly leaves its out-param untouched on failure, and the product's own callers all bail before using or freeing it (checked, so no product change). Fixed by zeroing `back` and guarding the two preconditions with the existing `JC_REQUIRE`; the suite-wide semantics change was not needed. Now: exit **1**, both FAIL lines naming the cause, and the other 12,413 checks still run. `JC_REQUIRE`'s own comment records an audit that found 19 sites across 8 files (M452) — this was the twentieth, and that audit missed it.|
| **Add `--api-base` to `jichi setup`'s non-TTY flag form.** | **Found 2026-08-18 (M475).** `setup` correctly detects a non-TTY and prints the flag form — `--preset --provider --model --key-env --from-global --import` — but there is **no base-URL flag**, while the interactive wizard *does* prompt for `"apiBase URL"` (`src/main.c:4943`) and `model_obj()` already writes `apiBase` when given one (`jc_setup.c:333`). The machinery exists; only the CLI surface is missing. It bites precisely where it matters: **`small-local`** ("Small local model (7-14B)") and **`constrained`** exist for locally-hosted models, which by definition need a custom endpoint — so following the tool's own non-TTY guidance yields a config pointing at the provider's cloud. Workaround is the one `setup` already prints (`$EDITOR local/config.json`), and the generated scaffold is otherwise sound: verified against a local OpenAI-compatible server, `doctor` 19 ok / 0 problems and live inference green once the line is added. | `src/main.c`, `src/setup/jc_setup.c`  **CLOSED (M488).** The flag exists, is wired to `ans.api_base`, and is documented in `SETUP_WIZARD.md` with the local-server example — and, the half that actually mattered, **the not-a-TTY message now names it**, because that printed flag list was itself what produced a config pointing at the cloud. Three smoke checks, two-sided: the flag reaches the config, omitting it still leaves the provider default, and the guidance names it.|
| **Compile jichi under WSL2 and run `make check-target`.** | **DONE (M475, 2026-08-18).** The row was right about which parts mattered and wrong about how much. The compiler was a non-event — `make` clean on the first try, **12,418 unit checks / 0 failures** — and of the three subsystems it named, **only `/mnt/c` behaved differently**, exactly as warned (the same commit reads 0 modified on ext4 and **1,639 modified** through v9fs, from line-ending translation). The terminal surprised in the *other* direction: smoke ran **209 drivers / 1,104 checks at `JC_SMOKE_TIMEOUT_MULT=1`**, so WSL2's pty layer times like real hardware, not a constrained VM. `PREPARE_AND_BUILD.md`'s walkthrough has now been **executed end to end** — by a non-root user, against pristine HEAD, and it passes. What the row did not anticipate: running the *full gate* found **four pre-existing defects in `ci`-only configurations** (clang, sanitized, valgrind, no-emacs), none WSL-specific, all reproducing on bare-metal Ubuntu 24.04, all traceable to M472 having been measured against gcc/unsanitized/with-emacs — the third instance of the M447/M189 shape. Verdict and numbers: [PLATFORMS.md](PLATFORMS.md); session write-up: anecdote 62 in [ANECDOTES.md](ANECDOTES.md). | [PORTING_WINDOWS.md](PORTING_WINDOWS.md) |

## Open — found by the M420 join, within an hour of it existing (2026-08-13)

| Deferred | Why | Where |
|---|---|---|

## Closed at M501 — two ways an explicit intent lost to an inference

| Was deferred | Closed by |
|---|---|
| **An inferred constraint can override an explicit `--edit-scope`.** | Already fixed at **M459** for `write_file`/`edit_file` — and the row had rotted, which the M326b rule caught: checking it before acting found the fix in place with six checks green. What was still broken is that M459 tested **tool names**, so `apply_patch` — whose paths live in `edits[]`, and which is what a model reaches for when making several edits — was never exempt, and neither were `generate_audio`/`generate_image`/`record_audio` (a top-level `path` outside the name list). M501 replaces the name list with a property of the **arguments** (`jc_argpath_collect`): exempt when the call declares at least one path and **every** path it declares is in scope. All-or-nothing, because `apply_patch` is atomic and one out-of-scope path must refuse the whole call — and an overflowed collection (more paths than the buffer holds) counts as *not* exempt, so a truncated view can never widen a permission. `constraint_vs_scope.sh` checks 7–9, proven red against M459's behaviour. |
| **The envelope attributes every mid-run change to the run — a concurrent actor's edits get reverted under `revertOutOfScope`.** | M501 took the row's second option ("scope the revert to paths the run's own tools touched") and made it provable rather than heuristic. jichi can change a file in exactly two ways: the write chokepoint (fenced to the scope) and a shell command (not fenced). So the envelope now records both — the paths written through `jc_app_write_file`, and whether any shell tool ran — and reverts an out-of-scope change when it was written by this run **or** when a shell command ran (attributable). When neither is true the change **cannot** be the run's: it is left alone, named on stderr, and journalled as `not_ours`. `revert_provenance.sh`, 6 checks, with a floor under the "still reverted" half after the first version passed while the fixture's shell call never happened. **The residual is documented rather than closed:** a run that uses the shell still cannot tell its own out-of-scope writes from a colleague's, so the operating rule stays *one envelope per working tree at a time* — see [AUTONOMY.md](AUTONOMY.md) §"What `revertOutOfScope` will and will not undo". |

## Open — found by the 2026-08-20 dogfooding runs (M504)

| Deferred | Why | Where |
|---|---|---|
| **Decide whether `.jichi/` state is implicitly inside the edit scope.** | Measured: a run scoped `--edit-scope 'src/agent/**'` journalled `out_of_scope: [".jichi/memory.md"]` -- the agent writing its own memory file. Detection-only there, so it was reported and kept, which is the right outcome; the question is whether it should have been reported at all. **For an exemption:** it is jichi's own bookkeeping rather than the user's code, an operator trained to ignore the notice will ignore a real one, and under `revertOutOfScope: true` the M501 rule *would* revert it (a run that used the shell is attributable). **Against:** an implicit exemption is a hole in a fence, `.jichi/memory.md` is a file the MODEL controls, and an operator who wants it writable can say so with a second `--edit-scope`. **Not decided here** because it is a fence-semantics decision, not a defect -- and the current behaviour is the conservative one. **Revisit when** a run with `revertOutOfScope: true` loses memory it should have kept, or an operator reports the notice as noise. | [analysis/2026-08-20-three-runs-two-projects.md](analysis/2026-08-20-three-runs-two-projects.md) |
| ~~**A `--max-tool-calls` recommendation shaped by the WORK, not one number.**~~ **CLOSED at M595 — and the shape hypothesis was wrong.** The row asked to revisit *"when a second model or a non-migration task is measured the same way"*. Both arrived on 2026-08-26: six driven runs on chrtext across `qwen/qwen3.5-9b` (LM Studio) and `jlu/qwen3.8-27b` (HRZ), on a mechanical sweep and a documentation task. The proposed advice — *migration/exploration → calls, generation → tokens* — **does not hold**: the documentation run ended on the **call** cap (50/50, 1.73M of 2M tokens) and the mechanical sweep ended on **tokens** (800k at 27 of 80 calls). What fits every run measured, old and new, is arithmetic rather than shape: with token budget `B`, call cap `C` and `t` tokens per call, tokens bind when `B / t < C`. `t` is a property of the BACKEND — 21–35k per call on a cacheless local server here, against 92% cache on one HRZ session — so a token cap on a cacheless model converts almost exactly into a call count and the other cap never fires. **Built:** that arithmetic, with the measurement table, in [AUTONOMY.md](AUTONOMY.md) §1. **Already built and found during this review:** the run-end note naming which budget bound, which M97 has carried all along in the `budget` journal event's `kind`, the warn line, and the headless result — so half of what this row proposed existed before the row was written. | [analysis/2026-08-20-three-runs-two-projects.md](analysis/2026-08-20-three-runs-two-projects.md) · [chrtext AGENT_ASSISTED_DEVELOPMENT.md](../../chrtext/docs/AGENT_ASSISTED_DEVELOPMENT.md) |

## Open — from the M505 self-review

| Deferred | Why | Where |
|---|---|---|
| **Whether `config validate` should surface posture warnings, or stay a pure parse check.** | Found at M505 while checking a documentation claim: `jichi --config bad.json config validate` prints `OK` for `{"models":[{"name":"a"}]}` -- a config naming no model id, whose active model then becomes a *priced* built-in default. `doctor` now warns about that (and FAILs under `--unattended`), so the fact is reachable; `config validate` still says OK. **Both readings are defensible:** validate is documented as a *parse* check and a reader may want exactly that, and duplicating doctor's judgement invites the two to drift (the M431 renderer argument). **Against:** an operator who runs `config validate` and reads `OK` reasonably believes the config is fit, which is what M486's "the front door told a lie its own gate could not see" was about. **Revisit when** someone reports the OK as misleading, or when a second posture warning wants the same home -- one is not a pattern. | [analysis/2026-08-20-reviewing-my-own-wave.md](analysis/2026-08-20-reviewing-my-own-wave.md) |
| **A four-persona documentation pass over the M499/M502 wave.** | M505 ran the *mechanical* half of `DOC_REVIEW.md` over the four new pages and five new sections -- every copy-pasteable command executed, every factual claim checked against the source -- and found one overstated claim plus the product defect above. What it could not do is the judgement half: the author and the reviewer were the same agent, which invalidates exactly the independence the four-reviewer method buys. **Revisit when** an independent pass can be run (a person, or agents given the read-only reviewer brief and no authoring history), before the public release. The mechanical floor is in place, so that pass starts from a corpus whose commands all run. | [DOC_REVIEW.md](DOC_REVIEW.md) |

## Closed at M506 — the budget stop that reported no verdict

| Was deferred | Closed by |
|---|---|
| **A budget-stopped run never runs its verifier, so a run that satisfied its own gate is reported as a failure with no gate verdict.** | The envelope ran the verifier at a budget exit only when the result could change the **rollback decision** (rollback armed *and* a green checkpoint banked) — correct for a decision, wrong for a report. A run whose gate was red at the start never banks a green, so the verifier was skipped entirely. Measured three times, twice in one afternoon of dogfooding, and one of those runs had done the valuable half of its task with its gate passing a minute later. Now the same verifier runs **for the record** in that case: journalled as `phase: "budget_exit_advisory"` with `advisory: true`, the exit code and the declared kind, and said on stderr when green (*"the run stopped on a budget, but its verifier PASSES on the tree as it stands"*). **Advisory by construction** — the code is journalled, logged, and dropped; the outcome stays `budget_exhausted`, because turning a green advisory verdict into a pass would make a stopped run indistinguishable from a completed one. `budget_stop_verdict.sh`, 4 checks, with check 3 pinning exactly that guarantee and check 1 a floor proving the fixture really stopped on a budget. Proven red. Cost stated in AUTONOMY.md: one extra verifier run per stopped run, bounded by `--verify-timeout`, avoidable with `--verify ""`. |

## Open — the data seams (M419)

Seven measured seams in what jichi records, designed but not built:
[`proposals/2026-08-observability-seams.md`](proposals/2026-08-observability-seams.md)
carries the numbers, the seven decisions and their rejected alternatives. The rows
here are only what a reader needs to *find* them; the order is the proposal's
cheapest-first order.

| Deferred | Why | Where |
|---|---|---|
| **`history.jsonl`: one append-only summary line per turn or run.** | Work without an envelope (interactive, plain `-p` — i.e. most work) leaves nothing a trend can be computed from, and the raw corpus is the thing retention must be free to delete. The shape is already proven three times here (`progress.jsonl`, the `improve` pass-rate history, `runs/`); this gives the whole tool the family member it lacks, so a trend is `tail`. **Deliberately conditional:** it earns its keep only if the trend is consulted — if nobody runs it in a month, delete it rather than polish it. **One measured need arrived at M425 (probe P7), and it is one field.** M86's hollow-gate check warns on `no_tests` (green with zero) or `fewer_tests` (fewer than an earlier green *within the run*). Measured on chrtext: its own `zig build test` gate reported **8 tests passed, exit 0** while the repository holds **1,525 `test " ` blocks and 99 `addTest` steps** -- roughly 0.5% of the suite, with four of those steps aborting on SIGABRT. Eight is neither zero nor a within-run regression, so nothing fired: the check sees the cliff and the slide, and cannot see a gate that was *always* tiny, because it has no idea what normal is for the project. Telling "8 tests" from "8 tests where there used to be 340" needs a **per-project historical test count** -- cross-run memory, which no current sink holds. So the row moves from *no demonstrated need* to **one measured need with a named consumer (`jc_env_verify_sanity`) and a single required field**; that is not the whole roll-up, and it is deliberately not recorded as a reversal. **Verdict, M421: not built, and this row now says why.** The M420 plan promised to *use* the join before deciding, and the use happened: three bounded runs across two projects, read as `runs --output json` indexed by `run` against telemetry grouped by the same key. The cross-sink question the roll-up was meant to answer — behaviour beside outcome, including the `in/call` quotient that diagnosed two budget deaths — came out of thirty lines of scripting over the existing sinks, with **no new file, schema, lint or version**. Two frictions were real and both were fixed in the reader instead: the sinks' differing vocabularies (now a table in [OBSERVABILITY.md](OBSERVABILITY.md)) and a phantom row when both logs shared a directory (M421). **What is still untested is the longitudinal half** — three runs from one afternoon are not a trend, and the "most work leaves nothing" argument stands on its own for un-enveloped runs. **Revisit** when a trend question is actually asked twice and the answer needs journals that retention has deleted; that, not the correlation question, is what would earn the file. | [S4 + D2](proposals/2026-08-observability-seams.md) |
| **Readers aggregate by default; retention as new `prune` scopes.** | `telemetry` reads **one** log (1 of 35 here) while a throwaway measurement script globs the directory — the shipped reader is weaker than the scratch tool. And `prune` covers sessions only: `doctor` sizes the one store that *has* a policy (370 sessions, 21 MB) and none of the ones that do not. The audit log is decided **never** auto-pruned. **Narrowed at M599:** the default log is now one appended file per workspace and every reader opens *that* file first, so for the common case "one log" IS the project's whole history and the aggregation half is moot; what remains is retention, whose urgency M599 raised by turning `metrics` on by default. **Revisit when** a workspace's telemetry file passes ~100 MB, or when `doctor` grows a size check for the directory (not built: a metrics event is a few hundred bytes, and the operator asked for the memory, not for its pruning). | [S3/S6 + D3/D5](proposals/2026-08-observability-seams.md) |
| **A bounded failure *class* on `tool_call`, and the remaining unread events.** | At `metrics` an offline reader sees *that* a call failed and never *why* — which is what forced the loop detector's classifier into the loop. The exception is deliberate and narrow: a classifier output (`not_found`/`denied`/`bad_args`/`killed`/`nonzero_exit`/`other`), never the message, because `metrics` must stay content-free. | [S2/S7 + D6/D7](proposals/2026-08-observability-seams.md) |

**Closed at M584 — D6, the unread events.** Eight telemetry event types were emitted on
every run and displayed by no command. All nine unread types (the eight plus `privileged`)
now have a reader line in `jichi telemetry`, and `telemetry_events_lint.sh` check 10 fails
the build when a new event type arrives without one — the guarantee moves from "somebody
remembered" to "the gate says so". **What the measurement said, against the proposal's own
argument:** on the only real corpus available (42,652 events, one workload) just **two** of
the eight had ever fired — `hook` 15 times, `privileged` twice. The other six had never
occurred, because the features behind them are off by default (auto-context), need a
violation (`history_check`) or need hardware (`kinetic`). D6 argued from **emit sites**, not
from **occurrence**; so six of the new readers are unevidenced, and the honest claim is
structural, not empirical. **Still open from D6:** `history_check` is emitted to the journal
as well and `runs` does not read it there; and D7's bounded failure *class* on `tool_call`
is untouched.

**Closed at M583 — D4, the stamping path.** The row that stood here said *"the eight
direct `jc_eventlog_begin` call sites"* and named `kinetic` and `privileged` among them.
Re-measured before the work: **both already went through `telem()`**, and the real set was
**nine call sites in five files** emitting six event types — `prefix_churn`, `hook`,
`retrieve`, `test_edit`, `args_truncated` and four `args_repair` variants. Same class of rot
as M582's decks: a register row citing a count that nothing re-checks. The fix is structural
rather than per-site — one shared `jc_app_telem_begin()` in `src/chat/jc_app.c`, and
`telemetry_events_lint.sh` check 9 fails the build when an app-sourced event reaches
`jc_eventlog_begin` directly. It also made a **documented** claim true: `TELEMETRY.md` had
listed `depth`/`turn`/`run` as *"common fields on every event"* since M420.

## Closed — the changelog's drift is bounded (M431)

| Closed | What it does, and what it deliberately does not | Where |
|---|---|---|
| **`tests/smoke/changelog_coverage_lint.sh`** pins CHANGELOG.md's newest named milestone to within **10** of the ROADMAP's newest entry. | The row this replaces declined the lint for a real reason — *"a changelog entry is a judgement about what is user-visible, and a check demanding one per milestone would be satisfied by a line of noise"* — and named its own revisit condition: **"revisit if the file drifts again despite the note."** That condition was met by measurement, not preference: M402 added the coverage note after 75 milestones of silent drift, and **M431 then shipped with no entry anyway**. So the objection is honoured rather than overruled — the lint bounds *systemic* silence and demands **no** entry for any individual milestone, which means it would **not** have caught M431's own drift of one. That limitation is stated in the driver's header, because a lint that oversells its reach is worse than a loose one. Content is not checked and cannot be. Deliberately **not** git-dependent: a tighter rule could ask whether the newest milestone's commit touched `src/`, but the published snapshot ships a fresh history, so a history-dependent gate would behave differently in the tree people actually acquire. Teeth proven three ways (a simulated 34-milestone drift, a changelog leading the ROADMAP, and a broken extraction shape) — the second of which caught a defect in the lint itself: it reported a *negative* drift as "within the ceiling", a reassuring green over a broken state. | [CHANGELOG.md](../CHANGELOG.md) |
## Open — RAM tiers and libcs never measured

From M403, which asked the M400 question of [`LOW_MEMORY.md`](LOW_MEMORY.md): which
tiers are measured, and on what. Three grades of evidence are now tagged per tier
(real machine / cgroup ceiling / advice); these are the gaps that remain.

**Three of the four are now closed.** M430 closed the ≤64 MB row and the
minimal-libcurl row on threadwork, which is the access both were waiting for (see
Closed below); **M449 compiled uClibc** — a Bootlin toolchain, 11,593 checks, though
the unit suite only and not a `check-target`, and it says nothing about a ≤64 MB
uClibc box. What remains is the row below, and it is **narrower than it reads**: a
phone has run jichi since M461, so what is genuinely open is *Termux on a phone* —
an on-device toolchain, not the platform.

| Deferred | Why | Where |
|---|---|---|
| **Termux on a phone — an on-device toolchain.** | **Narrowed at M461, not closed.** The platform half is done: a **Motorola moto g(30)** (Android 12 / SDK 31, arm64-v8a) ran **11,571 unit checks / 0 failures** and all four offline surfaces, so "never run on a phone" no longer holds — see [PLATFORMS.md](PLATFORMS.md). What a phone still adds is **Termux's own libc/filesystem quirks and battery behaviour on a handset**: no on-device toolchain is installed on that phone, so the build-it-on-the-device claim stays **tablet-only** (M459, Lenovo TB336FU). A second, narrower gap sits beside it, and **M507 moved it**: the first smoke driver has now run on a phone (`smoke_lint`, 17/17, exit 0), but the **rest of the tier has not**, and the reason is cost rather than portability — 9m35s for that one driver against 7.8s on the bench, a fork penalty of roughly **27x** once the handset is held awake. **Revisit** opportunistically; the twenty-minute estimate was wrong for the smoke half. | [LOW_MEMORY.md](LOW_MEMORY.md#platform-notes) |

## Closed — found by driving jichi on zigodot (2026-08-12)

Four defects measured while using jichi to author and work an assignment in another
repository; **all four are closed** (M409 the hint-ladder truncation, M410 the
attempt verdict, M411 the `--model` override, M412 the unprovable gate — see Closed
below). The section was kept one register cycle as the worked example of a
dogfooding run converting into fixes; **retitled at M463**, because the cycle has
passed and an `## Open` heading with no rows is how this page ends up advertising
work that does not exist. The full write-up, with the fixtures and the token numbers,
is `zigodot/docs/analysis/2026-08-12-driving-jichi-on-zigodot.md`.

## Open — found by the second zigodot campaign (2026-08-13)

| Deferred | Why | Where |
|---|---|---|
| **A hang before the first tool boundary is outside every envelope budget.** *(re-scoped M503: M438 closed the reporting half -- the journal's `start` now lands before the first network touch and the control socket answers earlier, so a supervisor is no longer staring at a 0-byte file. What remains is the BOUNDING half: the deadline should cap the PROCESS, not just the loop.)* | Measured: an `--auto` run with `--deadline 12m`, `--journal`, and configured `timeouts` (connect 20 / stall 120 / request 900) hung for **22 minutes producing a 0-byte journal** — not even the `start` event — and 121 bytes of stderr (the M411 pin note). Every liveness mechanism jichi has assumes the loop is running: the deadline is enforced at tool boundaries, `--heartbeat` is jsonl-only and fires from the model-call progress callback, and the journal opens with the run. A pre-loop hang (this one was somewhere between arg processing and the first request — the pin note printed, nothing after) is invisible and unbounded. A supervisor's only tell is silence, indistinguishable from a long model call. **What to build:** journal `start` (or a stderr line) *before* the first network touch, and a wall-clock backstop that covers the pre-loop phase — the deadline the user asked for should bound the *process*, not just the loop. **Not diagnosed to root cause** (one occurrence, killed gracefully after announcement; a second concurrent jichi held the same workspace/HOME, so shadow-repo or session-store contention is the suspect). | [AUTONOMY.md](AUTONOMY.md) |

## Closed at M470 — the two improvements the architecture sweep asked for (M469)

| Deferred | Why | Where |
|---|---|---|
| **`tier-v-arch.sh` should snapshot the tree ONCE per sweep, not once per target.** | `jc_rig_ship_tar` runs inside the per-target loop, so a commit landing mid-sweep gives later rows a different tree than earlier ones and the table stops being comparable — silently, since every row still prints a check count. It is why M469's commit had to wait for a 21-target sweep to finish before anything could be committed, which is a real workflow cost on a rig meant to run unattended. **Fix:** ship once to `$DIR/src`, then copy per target. | `scripts/tier-v-arch.sh` |
| **`tier-v-arch.sh` should preflight `pipe()`/`select()`/fork+exec per target and say "the environment cannot run the subprocess tests".** | The six MIPS rows reported *"unit suite RAN and reported failures: 11,627 checks, 73 failures"*, which reads as an accusation against jichi. The cause is one level below it: `pipe()` fails under `qemu-mips` with zig's musl (MIPS is the one Linux architecture whose `pipe` syscall returns both descriptors in registers), and all 73 failures are downstream — the seven failing files are exactly those that spawn a subprocess or read `/proc`. A row that blames the program for its emulator's gap is worse than no row, and `tier-v-openbsd.sh` already has the pattern to copy (its procfs check). **Also worth recording in the same pass:** armeb cannot print a `double` (the *literal* `0.2` renders as `-2.35344e-185` while its stored bytes are correct big-endian IEEE-754), so that target is unusable for measurement through no fault of jichi's; and `x86_64-linux-muslx32` builds clean but cannot execute because this kernel lacks `CONFIG_X86_X32`. | `scripts/tier-v-arch.sh`, [PLATFORMS.md](PLATFORMS.md) |

> **Both closed at M470, and the second found more than it was filed for.** The rig
> snapshots once to `$DIR/src` and copies per target, so a mid-sweep commit can no longer
> make the table non-comparable. `scripts/envprobe.c` runs per target before the suite and
> the MIPS row now reads
> *"11,638 checks, 73 failures — env probe says [pipe=FAIL select=FAIL forkexec=ok
> procfs=present]: the EMULATOR cannot run the subprocess tests"*, classified `no-run`
> instead of accusing jichi. The probe independently confirmed the manual diagnosis
> (`pipe` and `select` fail, `fork`+`exec` does not) and it only ever **downgrades** an
> accusation — a passing suite reads the same whatever the probe said. It also reports
> `procfs=present|absent`, because *present but different* is the illumos hazard
> [`PLATFORMS.md`](PLATFORMS.md) now names.

## Closed by measurement — parallel_abort does NOT reproduce on Guix (M458; re-scoped M466, corrected M467, CLOSED M468)

> **The premise of this row changed on 2026-08-17.** It was filed as isolated to Guix
> System — the one platform whose row cannot rebuild itself — so the next step was
> believed to be building `scripts/tier-v-guix.sh`. The OpenBSD row, once
> `JC_SMOKE_KEEP_GOING=1` let it run to the end, reports **`parallel_abort`: "parent did
> not exit within 15s of SIGINT — abort/reaping deadlocked"** — word for word the Guix
> failure — and it fails standalone there too. **So it reproduces on a platform with an
> unattended rig** (`scripts/tier-v-openbsd.sh`, an 11 MB ISO and one command), and
> Guix is no longer on the critical path. Iterate with
> `sh tests/smoke/run.sh parallel_abort` in that guest. The three suspects named below
> — all landing after the Guix measurement and none re-measured — are now testable
> without Guix at all.
>
> This also means the row was never really about Guix: it is about a **non-glibc,
> non-Linux** reaping path, and the two platforms share that rather than sharing
> Guix's non-FHS layout. The M450 process-group explanation is now doubly incomplete.
>
> **CORRECTION (M467), and it retracts the paragraph above.** OpenBSD's identical
> failure turned out to be a defect in **jichi's own test harness**, not in the agent:
> `parallel_abort.sh` backgrounded a subshell (`( cd "$ws" && "$BIN" ... ) &`) and
> captured `$!`, and on OpenBSD ksh `$!` names **the subshell**, not the command —
> measured, against dash and bash, which both perform an implicit exec and name the
> command. So the driver SIGINT-ed a shell, jichi never received the signal, and the
> check reported *"abort/reaping deadlocked"* about an agent that was never asked to
> abort. With `exec` added it passes.
>
> **Therefore the "both platforms share a cause" claim written at M466 is withdrawn.**
> Guix's `/bin/sh` is **bash**, which does perform that implicit exec, so this
> explanation does **not** transfer. Guix's instance is **still open and still
> unexplained** — but it is now cheap to settle rather than blocked: the `exec` fix is
> shipped, so one re-measure (`sh tests/smoke/run.sh parallel_abort` in a Guix guest)
> distinguishes "the same harness bug after all" from "a real defect in the
> `spawn_parallel` reaping path". Until that runs, neither claim is earned — and the
> earlier framing was exactly the over-reach this register exists to prevent.
>
> **CLOSED at M468, by measuring it.** Guix System 1.5.0 (`guix describe` d58da8a,
> kernel 6.17.12-gnu, `/bin/sh` = bash 5.2.37, **no `cc` and no `c99`** — M458's lesson
> measured rather than recalled), built at HEAD `f025185` with `CC=gcc` inside
> `guix shell`: `-Werror` C89 build **clean**, **11,594 unit checks / 0 failures**, and
>
> ```
> --- smoke: parallel_abort
> ok 1 - SIGINT-ed parent exited (reaped both stalled children)
> ok 2 - exit was prompt (1s) -- the abort path, not the 120s watchdog
> ```
>
> plus `parallel_hang` 2/2, `signals` 4/4 and `stop_reason_capped` 5/5 (the last fails on
> OpenBSD, confirming *that* one as ksh-specific). The likely fix is one of the three
> commits this row already named — `448616d` is the best structural fit, since a child
> spinning on `EPIPE` instead of dying is exactly a parent that never leaves `waitpid`.
>
> **Caveat, so the closure is not stronger than the evidence:** this is not a
> byte-identical re-run of M458's row (that was a Guix System from the recorded config;
> this is `guix shell` in the published desktop image, 6 vCPUs / 8 GB). The defensible
> claim is *does not reproduce at HEAD*, not *was never real*. And **I predicted in
> writing that it would still fail** — see
> [`analysis/2026-08-17-driving-the-published-guix-image.md`](analysis/2026-08-17-driving-the-published-guix-image.md).
>
> **The Guix image itself, same day:** the operator downloaded
> `guix-system-vm-image-1.5.0` and its source tarball. GRUB in that image turns out to
> read *and* write serial — so M450's "cannot be driven" is wrong at the bootloader
> level, and the boot entry (kernel, initrd, root UUID, args) is now extracted and
> recorded. What resists is GRUB's serial **input** reliability across four attempts;
> the decisive error was `unspecified search type`, i.e. GRUB parsing a bare `search`
> whose search-type argument was dropped in transit. Three ranked next moves — QEMU monitor
> `sendkey`, a direct `-kernel`/`-initrd` boot (the `tier-v-tiny.sh` pattern, needing
> the two files copied out via the existing Debian rig rather than root), or building
> the headless image from the now-parameterised `.scm` — are in
> [`analysis/2026-08-17-driving-the-published-guix-image.md`](analysis/2026-08-17-driving-the-published-guix-image.md).
> **The one step that needs a human** is five minutes at that image's graphical console
> to enable sshd; everything after it is automatable.

| Deferred | Why | Where |
|---|---|---|
| **Diagnose why `tests/smoke/parallel_abort.sh` deadlocks on Guix System.** | After SIGINT the parent never exits; the driver reports *"abort/reaping deadlocked"*. **Two explanations were ruled out before filing:** it is **not a timeout artefact** (identical failure at `JC_SMOKE_TIMEOUT_MULT` 1 and 6, so a slower machine is not the cause), and it is **not the absent process groups** that explained the `guix shell -C` failure at M450 — `/proc/self/stat` shows a normal non-zero `pgrp` on the real system. The unit suite is green there (11,599 / 0), so it is isolated to the `spawn_parallel` abort/reaping path in `jc_parallel.c` / `jc_bg.c`, which uses `setpgid` + `kill(-pid, ...)`. **This also corrects M450**, whose process-group explanation was sufficient for the container and is now known to be incomplete. **Not diagnosed in the session that found it** because the row it belonged to was otherwise complete and a signal/reaping deadlock deserves its own sitting rather than the tail of a long one. **Revisit** with the Guix image still buildable from the recorded config — it reproduces in one command. **RE-SCOPED at M466: re-measure before debugging, and three named suspects.** The deadlock was measured at `8f1d4b6` (2026-08-15), and **34 commits later three fixes landed in its exact code path**, none of which has been re-measured on Guix: `448616d` — every child inherited jichi's *ignored* SIGPIPE, so a pipeline producer spins on `EPIPE` instead of dying when its consumer exits (the textbook shape of a parent stuck in `waitpid` on a child that will not die); `0d5b030` — a timed-out capture killed only its direct child and **orphaned the rest of the pipeline**; and `ac166d5` — the smoke harness reaped only the *last* mock a driver started, whose symptom on FreeBSD was a **2.19-second driver reported as a timeout 118 seconds before any deadline, with all five of its checks passing**. That last one is the closest match to the observed report, though its FreeBSD mechanism (a `timeout(1)` that waits for the whole process group) should not apply under Guix's GNU coreutils — which is exactly why it needs measuring rather than reasoning about. **Two blockers removed at M466:** the machine definition no longer hardcodes one host's ssh key path (it reads `JICHI_BENCH_PUBKEY` and refuses when unset), and the re-measure now costs **one boot** rather than one per defect — `JC_SMOKE_KEEP_GOING=1` reports the whole failure set, and `sh tests/smoke/run.sh parallel_abort` runs just this driver. **Still blocked on Guix itself:** `guix` is not installed on the threadwork bench and building a Guix System image needs a working daemon and `/gnu/store`, so this row belongs to whichever machine has Guix. `scripts/tier-v-guix.sh` is deliberately **not** written blind — an untested rig for a platform nobody can run here would be a fourth never-executed artifact, and the honest first step is a re-measure that needs Guix regardless. | ROADMAP M458/M466, [LOW_MEMORY.md](LOW_MEMORY.md) |

## Closed by measurement — the /tmp assumption and the crash class (M457)

Both rows below were filed at M453 with the reasons they were not done that day. They were
done at M457, in the order the plan's own *"what would make this plan wrong"* section
specified: the crash class first, because the audit found it wide (19 sites on a narrow
pattern, 43 on a broader one) and it is valuable on every platform rather than only on
`/tmp`-less ones.

| Closed | What it measured | Where |
|---|---|---|
| **Route the unit suite's fixture paths through `TMPDIR`.** | 158 literal paths converted, plus `jc_test_tmpdir()` for the six directory uses. The idiom was **copied from `_smoke.sh`'s `smoke_tmp`**, not invented. Verification is the **check count, not the colour**: 11,599 / 0 with `TMPDIR` unset, `/tmp`, short, and ~100 chars. That mattered — `test_git.c` built its repo through `system("mkdir -p /tmp/...")` while the C side used the helper, so with any other `TMPDIR` the fixture landed outside the repo and **ten checks silently did not run** while everything stayed green. Three other classes needed reading rather than regex: string concatenation, a non-literal format argument, and expectations whose matching input is literal JSON fixture data. | ROADMAP M457 |
| **Audit for a recorded null check followed by a dereference.** | Two harness verbs added, because the harness had no way to express "check, then branch": `JC_REQUIRE` (records and yields) and `JC_VEC_STR` (fetch-or-NULL). Same gap as M450's `t_skip_one`. **Shown to work on the machine that exposed it:** the Android 4.4 tablet went from *aborting at the 4th of 123 test files* to running to completion — 11,534 checks, 47 failures, zero crashes. The 47 are themselves a finding: they cluster on every test that forks or shells out, because **Android has no `/bin/sh`**, which musl's `system()` invokes. | ROADMAP M457, [TEST_INTEGRITY.md](TEST_INTEGRITY.md) |

## Closed by measurement — A5, line breaking for German compounds (M579)

**The item as scoped:** *"UAX #14 line breaking, so a German compound does not split
mid-word."* Raised at M554 when the chrome-width budget was first measured, and made to look
urgent by M568 shipping German chrome for real.

**Both halves of its premise turned out to be false, and both were cheap to check.**

**jichi does not wrap output.** `term_cols` (TIOCGWINSZ, then `$COLUMNS`, then 80) appears in
exactly one file — `src/tui/jc_term.c` — and only for cursor arithmetic while the user is
*typing*. No renderer takes a width; `jc_mdrender` has no width parameter at all. Output is
emitted as-is and **the terminal** wraps it, which `jc_tui.c`'s own comment says plainly: *"the
renderer emits them, the terminal wraps on them."* So there is no break-point decision in
jichi's code for UAX #14 to improve.

**And nothing is wide enough to wrap.** Measured by `tests/test_width.c`, widest non-exempt
chrome line per language:

| en | de | es | ja | zh |
|---|---|---|---|---|
| 75 | **76** | 59 | 64 | 52 |

against a budget of 78 — 80 columns minus a two-space indent. **At a standard terminal no chrome
line wraps in any language**, German included, and the budget exists precisely to keep that true:
a translation that would wrap fails the build the day it lands.

**What is genuinely left is a different question, and it needs a measurement I cannot make.**
Should jichi *pre-wrap* its chrome at word boundaries, so that a terminal narrower than ~78
columns breaks between words rather than mid-compound? That is a new capability rather than a
fix, and its value depends entirely on whether a hard mid-word wrap actually harms a listener —
which depends on the reader, the emulator, and how continuation lines are marked. **Nobody here
has heard it.** Building UAX #14 against an unmeasured harm, in a code path that does not exist
yet, would be the shape this project has already paid for twice this month.

**Reopen it when:** somebody reports that a wrapped chrome line reads badly at a narrow width.
Then the work is *pre-wrapping chrome*, and UAX #14 is one possible implementation of it.

## Closed by measurement — uClibc, on the toolchain it was waiting for (M449)

The row said it was blocked on a toolchain and named the unblocking condition: *"Revisit
if a buildroot/OpenWrt toolchain becomes available."* Bootlin publishes buildroot-built
toolchains, so the condition was already met and nobody had checked — the M326b failure
in its mildest form, since the reason was true when written and quietly expired.

| Closed | What it measured | Where |
|---|---|---|
| **Compile jichi against uClibc.** | Bootlin `x86-64--uclibc--stable-2025.08-1` (gcc 14.3.0, uClibc-ng), `HAVE_CURL=` + `-static`: **zero diagnostics** under the project's mandatory `-std=c89 -pedantic -Wall -Wextra -Werror`, **11,593 unit checks / 0 failures** run natively, all offline surfaces OK, `--version` peak RSS **384 KB** with 0 shared libraries. The row predicted the risk was "genuinely lower than the Darwin case" *because* the one libc-dependent feature is probe-gated rather than `#ifdef`-gated — and that prediction was **half right in an instructive way**. There was indeed no rotted `#ifdef` branch. But the probe itself was wrong: it omitted the build's warning flags, so it answered "yes" for a `malloc_trim` that uClibc-ng declares only under `__USE_GNU` while still exporting the symbol, and **every translation unit then failed under `-Werror`**. Probe-gating was the right design and was not, by itself, sufficient. | [PLATFORMS.md](PLATFORMS.md#libc-and-ram-tiers), ROADMAP M449 |

## Closed by measurement — two RAM rows, on the machine they were waiting for (M430)

Both rows above said, in the text, that they were blocked on access to one machine
rather than on a design question. That access arrived; here is what the sitting
produced. Kept rather than deleted because each row's *reason* contained a prediction,
and one of them was wrong in an instructive direction.

| Closed | What it measured | Where |
|---|---|---|
| **Run jichi on a machine with ≤64 MB of physical RAM.** | On a kernel + busybox initramfs guest: offline surfaces at **64 MB**, a **verified model turn at 80 MB**, jichi's own peak **1,160 / 1,396 KB**. The row predicted the tier "is not in doubt on jichi's side" and that was right — but it framed the open question as *the three things a real board pays*, and the actual binding constraint turned out to be **the kernel**: swapping Debian's generic kernel for Alpine's `virt` moved the floor 96 → 64 MB with jichi byte-identical. Also measured, and not predicted by anyone: a stock Debian 12 cloud image **cannot boot at 128 MB at all**, so the `~128 MB` tier was never gradeable with a distro. Still a VM, not a board. | [LOW_MEMORY.md](LOW_MEMORY.md), [analysis](analysis/2026-08-13-ram-tiers-whole-machine.md) |
| **Build a minimal single-TLS-backend libcurl and link it into a static musl jichi.** | **804 KB** `--version` RSS, 0 shared libraries, and it **makes a real model call** — against the system libcurl's 10,012 KB. M403's honest bound ("between 0.5 and 8.6 MB") collapses to the bottom of its own range. The row's estimate of the work was accurate ("an afternoon of libcurl `configure` flags"); what it did not anticipate is that the four blockers were all *toolchain* facts rather than flags — `zig` as a multi-call binary breaking `CMAKE_AR`, mbedTLS's `-Werror` against a newer clang, `zig cc` linking UBSan by default, and a mock deleted mid-test. Uses **mbedTLS**, not the OpenSSL the recipe names, so it is a sibling claim rather than the same number. | [LOW_MEMORY.md](LOW_MEMORY.md#build-time-footprint-reduction) |

## Closed by measurement — two recommendations withdrawn (M406/M407)

Kept because a withdrawn recommendation that leaves no trace gets re-proposed. Both
were raised by me after M405, both on **ad-hoc greps whose extraction was wrong** —
the same blindness the project's lints are written to avoid, met four times in one
day from my own probes.

| Recommendation | Why it was withdrawn |
|---|---|
| **"9 of 12 curriculum modules have no link forward" — add next-links.** | **All twelve already carry** `[◀ Prev] · [▲ Curriculum map] · [Next ▶]`. My probe grepped for `Next:` and `→` and missed the actual `[Next ▶]` form. Acting on it would have "fixed" a unanimous convention into duplication. Residue: the invariant is now pinned by `docs_locators_lint.sh` check 6, whose comment records this. |
| **"Nothing ever runs a documented command" — build an executable doc-command tier.** | **`subcommands_lint.sh` checks 7–11 (M326e) already run every advertised subcommand bare**, in a throwaway workspace, with exclusions named and check 11 guarding stale exclusions. I had read the *static* drivers (`doc_commands_lint`, `docs_flags`, `subcommands_lint` checks 1–6) and generalised from them. A separate driver was written and **deleted** rather than shipped: for six forms it would have been a duplicate wearing a new name. Residue: those six *flag-carrying* forms were genuinely uncovered — the strip between "the verb exists" and "the bare verb runs" — and are now executed by `doc_commands_lint.sh` check 5, which asserts they are not **rejected** (exit 2), the precise M375 signature. Its reach is stated in the driver: six forms, four of which legitimately exit 1 on an empty fixture HOME. |

**The transferable lesson**, recorded because it cost four probes in one session: *a
number produced by an ad-hoc grep is not a measurement.* Every one of these
extractions matched something real and missed the thing it was looking for — the
comment quoting its own subject, the wrong output marker, the wrong link form, the
wrong error signature. The rule that survives: before recommending work on the
strength of a count, either derive the count from ground truth the code owns, or
verify one instance by hand.

## Closed during this program

Kept briefly, because "we said we'd do this" is worth being able to check.

| Was deferred | Closed by |
|---|---|
| `run` on telemetry + `ws` on the journal's `start` (the S1 join) | **M420** — two conditional fields, envelope struct untouched. Proven by performing the join rather than asserting presence: `telemetry_join.sh` compares the journal's `run` against telemetry's and requires **every** event to carry it (partial stamping would join some behaviour and silently drop the rest). Teeth: the perturbed binary reports `journal run='…' vs telemetry run=''`, the exact pre-M420 state |
| `verify_stuck` + `test_assertion_edit` in `runs` NOTES | **M420** — `stuck=N` and `goalposts=N`, plus the `ws` column the journal now supplies; printed only when non-zero, because an always-present `goalposts=0` trains a reader to skip the column the one time it matters. The plan called this "two counters" and it was three: a `ws` the reader could not read would have been written for nobody |
| Nothing checks that an authored assignment's gate can fail | **M412** — `grade --expect-fail`, the smaller option the row recommended: exit 0 iff the verify fails on the untouched tree, HOLLOW (exit 1) when it is already green, `--record` refused. A command composes with every authoring path (scaffold, hand-written, a model that ignored its brief); prompt text binds only the model that reads it |
| An explicit `--model` reported active, then overridden by config routing | **M411** — `--model` pins the run (routing disabled, one-line note, `-q`-respecting); `--route-fast`/`--route-strong` alongside it keeps routing. CLI-only on purpose: the TUI user sees the `[route]` banner and has `/route off`, which is exactly the visibility the headless `-q` user lacked |
| A colon in a `hints:` value silently deleting the whole ladder | **M409** — deferred for about six hours: the row recommended "the lint first, do not touch `jc_yaml` under time pressure", and the driver was indeed built first — born red, it measured **64 of 80** shipped ladders short, far past the sampled five. The parser fix then turned out to be eleven contained lines (`quoted_whole_line` before `find_colon`, quoted keys still mapping), every ladder recovered with no prose changed, and the M289 skip now counts what it drops so `hint` can say so |
| `attempt` reporting PASS over ten goalpost warnings, then deleting the evidence | **M410** — all three separable pieces from the row: the counter (`test_edits` on the envelope), the verdict (**TAINTED**, exit 1, via a pure unit-tested word function), and `--keep-worktree`. The "0 hints used" oddity resolved itself: the ladder was unreadable (M409), so the learner *had* no hints to use |
| **Tell the model when its own edit moved the goalpost** | **M435** — one sentence appended to `edit_file`'s and `apply_patch`'s own results, escalating with a count. The row's rejection held: not a refusal, because correcting a genuinely wrong test is fair work and a refusal is routable into `sed`. What the row did not anticipate was the precondition — the two tools each carried a near-identical five-part M88 block, so a sixth destination meant factoring them into `tu_report_test_edit` first. Two of my own bugs were caught by the new tests: `"the 2th test assertion"` from a hand-built ordinal (the same bug `jc_toolloop_render` fixed three milestones earlier), and a fixture that failed all six checks for a missing `read_file` rather than for the property under test |
| **Stop advertising tools a depth gate will refuse** | **M436** — the row's rejection held (the gate is right, the advertisement was the bug) and the fix went one step further than the row asked: the fact is now ONE field on `struct jc_tool`, read by the builder for the omission and by `jc_tool_execute` for the backstop, and the three hand-written `agent_depth > 0` checks in the tool bodies are deleted. Stating it three times and reading it in none of the places that build the advertisement is how the two halves could disagree at all. Cost: 40 definitions name the new field, because `-Wextra` does not let C89's implicit zeroing of a trailing member pass silently — and naming it everywhere is the better outcome. A name table plus a lint was rejected: with the gates gone the table would be its own ground truth |
| **One structured report shape for both delegation tools** | **M437** — the shape shipped with both named defects fixed: the grandchild poison (one save/restore of the per-run slots around every `jc_tool_execute`, restoring rather than zeroing so this run's own earlier failure survives) and the capped parallel child (its stop reason now travels the pipe as a NAME, so an enum renumbering cannot silently reinterpret it). Two things the row did not anticipate. A **policy block never reaches the loop's `is_error` branch**, so the denial case -- the single most valuable one -- bypassed the only recording site; `block_message` is the chokepoint for both block sites and now records there, classified as DENIED directly rather than inferred from wording. And `files_changed[]` shipped for a `spawn_parallel` write child ONLY: a worktree gives a per-delegate baseline, a nested in-process run has no equivalent, and manufacturing one would mean a shadow checkpoint per delegation appearing in the user's `/undo` stack. The row's rejection of forwarding the transcript held |
| **Arm liveness before the first tool boundary** | **M438** — all three parts: an `open` journal record at file-creation time (with the pid, so a supervisor can check the process rather than infer life from file growth), a full control boundary before the first request, and a poll-only one BETWEEN the tool calls of a round. The poll is deliberately not the full boundary: that folds steering into history, and a user message between two tool results of one round is malformed under M364's contract. Writing the test taught more than the fix did — the first cut asserted that `status` ANSWERED and passed with the fix reverted, because the client waits 300s; the property is latency, and measured over a 12s round it reads 1s with the fix and 7s without. One limit stands and is stated: the socket is still not served DURING a model call, since polling from libcurl's progress callback would let a `pause` block the HTTP read |
| **Cut the jsonl `tool_result.preview` on a UTF-8 boundary** | **M439** — fixed with the in-tree helper the row named, behind a pure `jc_agentjson_preview` in the module that owns the jsonl schema. The row understated the frequency: 512 is not a multiple of 3, so splitting a character was the NORMAL outcome for non-ASCII output long enough to truncate, not an edge case. It also missed a second site -- `copy_trunc` in `jc_cli.c`, backing `jc_tool_arg_summary`, which reaches stderr and the telemetry `args` field. Two of my own assertions were inverted before the driver was right: "the last byte is not a continuation byte" is FALSE for every valid string ending in a multi-byte character, and a length-modulo test ignored `read_file`'s 7-byte ASCII gutter |
| **Tell the model the price list when the numbers are measured** | **M440** — a `# Cost model` section with the effective caps and §6 items 5-8, gated tri-state on the cache verdict exactly as the row asked. Two things the row did not anticipate. The gate had to read the CONFIGURED cache setting, not the observed hit-rate: a running statistic in the system prompt changes the cached prefix every turn and destroys the caching it describes, so the section's signature takes five integers and no `jc_app` -- the hazard is structurally impossible, not merely discouraged, and the remaining gap (a backend that accepts a caching request and returns nothing) is what the explicit flag is for. And the four built-in caps had to move from four `#define`s in four tools into `include/jc_toolcaps.h` first, since a second reader of a number that must agree is the drift M296 forbids; `tool_caps_lint` failed loudly on the move and now reads one file instead of four |
| **Teach `mockmodel` to emit a `usage` block on the chat path** | **M441** — the tool path now sends usage in a separate final chunk with an empty `choices`, the shape a real provider uses. The row's fear of a blast radius was right in kind and small in size: ONE driver moved. `learn_on_stop_cost` ran on `--budget-tokens 1`, which only sufficed because a tool call cost nothing; it now ends budget_exhausted at 25 tokens, and the fix is a bigger budget plus a comment saying why it is not 1. The payoff was taken at once — `budget_panel.sh` gained the RATE check the row was written to unblock, proven two-sided against the old rig, which reads `0/400000 tokens (0%)` with no rate at all |
| **A tool-call id in the jsonl stream** | **M442** — the provider's own id on both `tool_call` and `tool_result`. "Small and additive" understated one thing: it is a shared callback signature, so all four front-ends had to change, and three of them deliberately IGNORE the id — the TUI (a human reads lines in order), ACP (it mints its own `toolCallId` an editor already pairs by, and swapping it would change bytes on a live wire), and the fork pool (a board and an aggregate, never a paired timeline). Each says so at the parameter. The driver pairs two calls to the SAME tool, since two different tools can be paired by name and would have passed with no id at all |
| **A `degraded` flag on the terminal object** | **M443** — an object rather than a boolean, counting `unanswered` / `approval_unavailable` / `privilege_refused`, and present only when one is non-zero so a supervisor tests for the key (the M420 non-zero-gate argument). The row listed `confirm_tool` NULL ⇒ allow among the degradations; that case is an `--auto` run, and it is deliberately NOT counted — auto-approval is the operator's instruction, not a decision taken in their absence, and counting it would make every `--auto` run degraded and the flag worthless. `stop_reason` is untouched: the flag reports, it does not judge |
| **`jichi sysmsg` omits every envelope-gated prompt section** | **M444** — and `context` with it, which the row did not name but which mattered more: its whole job is to size the prompt a run sends. The row said the fix was "a dispatch-order change with a blast radius worth measuring"; measuring it showed moving the dispatch was the WRONG fix, because MCP servers are connected in between and a read-only prompt dump would have begun spawning subprocesses. The arming was split from the journal instead: `envelope_arm` takes the path as a parameter, an introspection command passes NULL, and the driver requires the runs directory to stay empty. Measured effect on `context`: the environment slot goes from ~25 to ~136 tokens |
| **`tests/smoke/mincurl_recipe_lint.sh` is promised by a script and does not exist** | **M445** — it exists, and the script's claim is a fact again. Built with the caution the row asked for: only `--disable-*`/`--without-*` tokens, from BOUNDED regions (the doc's fenced block, the script's single `set --` list), floors of 15 on each side, and curl's own prefix, cross-host and TLS-backend options excluded because they legitimately differ between a page teaching one build and a script parameterised over three rungs. Proven three ways — a flag dropped from the script, one dropped from the page, and a reshaped invocation that trips the floor at 0 instead of comparing two empty sets |
| `docs/MCP.md`, the one entirely missing feature page | **M395** — written from the source rather than from memory, which caught three of my own wrong claims mid-draft: `headers` is an array of whole header lines (not an object), `type` is inferred from whether `url` is set, and `autoApprove`/`deny` accept `"*"`/`true` for every tool a server offers. The object-shaped `mcpServers` that Claude Code and Continue use now **warns** instead of configuring nothing in silence |
| STATE-THE-REACH in the system prompt + doctor | **M387** — a conditional prompt line when an edit scope is armed (deterrent framing; the unit test forbids it reading as an invitation) plus a doctor line, born red at both tiers. It was parked for one batch and closed two milestones later; the row outlived its deferral by a day and was found by reading this page back, which is the failure this page exists to prevent |
| Skip the lossy mid-turn pass once it is exhausted | **M361** — the exhaustion latch: a dry pressed pass records the exact length at which the sliding window next releases a candidate (oldest protected candidate + keep + 1; every latch expires within keep+1 appends, so a conservative detector can only delay one scan, never skip one forever — the exact re-arm the row demanded); on the driver fixture 12 pressed passes collapsed to 4 full scans |
| The CLI `undo` leaving saved sessions believing the pre-undo state | **M350** covered it from the resume side (the mtime drift note); the residual sliver (a clock moving backwards between undo and resume) is not worth a mechanism — kept one register cycle per the row's own note, closed by the 2026-08-10 sweep |
| The DECLARE-THE-GATE repetition (GATE_INTEGRITY §8's n=1 caveat) | **M344** — five per arm on the #45 setup: the tampering did not recur in either arm; the sentence halved the pre-endpoint poking; table in GATE_INTEGRITY §8c |
| The wizard's 17-option first screen, two questions under one list | **M326i** |
| The `context` subcommand reporting `rules ~0` | **M311** |
| The system prompt's unnamed remainder | **M312** |
| Per-tool definition sizes | **M313** |
| Telemetry joined to the tool registry | **M314** |
| The history as one number | **M315** |
| Flipping `attempt`'s default to `core` | **M320** — closed as *decided against*: the null held on a second model, but the same runs produced a better objection than the one they dissolved |
| `context tools` under-reporting the live toolset | **M325b** — one shared registrar for main/context/doctor; 16 → 26 on a git repo, with MCP named as the one exclusion |
| `spawn_parallel` at 4/10 | **M325** — the log *could* say why after all: 3 watchdog timeouts on a 300 s default against 300–462 s children, 3 forks that could not happen |
| The `glob` capability gap | **M324** — `list_files` gained a `pattern`, so the schema objection was fixed rather than argued with, and `glob` graduated from hint to alias |
| Measuring a `core` attempt that needs a hint | **M319** — 12 runs, 0 hint calls: the model never asks, so the "capability cost" was theoretical |
| Whether the craft section earns its tokens | **M318** — measured; a null on a 31B model, so it is now off under `--lite` and unchanged elsewhere |
| A TUI `/context tools` / `/context history` | **M317** — and better there than in the CLI: the TUI holds the *live* history, so no save lag |
| A `doctor` check for never-called tools | **M316** — the objection was answered by changing the evidence axis to *distinct sessions* and advising the lever (`toolProfile`) rather than individual tools |
| German editions of the plain-register assignments | **M309b** |

## Open — what the model asked for and M431 did not build (2026-08-13)

M431 implemented Tier 0 (five promises made true). The tiers below are designed, with
every rejected alternative, in
[`proposals/2026-08-model-facing-orchestration.md`](proposals/2026-08-model-facing-orchestration.md).
They are parked as a **scope decision**, not an oversight: the release is weeks away and
Tier 0 was the part that was making jichi's own documents untrue.

The thesis they serve is one sentence: **jichi shows the model its failures and shows the
human its false successes.** Every Tier 1/2 row below is one instance of that.

**Since closed, and removed from the table per this register's own rule** (an item is
removed when done, with the closing milestone in the ROADMAP): the M331 finding at the
periodic verify site and the changelog-coverage bound (**M431b**), the run id on the
machine surface (**M431c**), the hollow completion green (**M431d**), the workspace
lease (**M431e**), the ambient budget panel (**M431f**, shipped OFF so M347's decision
is measured rather than overruled) and the `--connect --output json` downgrade
(**M431g**). Two rows below were **missing entirely** until 2026-08-14 --
the refused-tool advertisement and the UTF-8 preview cut -- so they were findable nowhere
while being named in the proposal, which is the one failure a findability register cannot
afford.

| Deferred | Why | Where |
|---|---|---|
| **Charge a subagent's tool calls to the run** — *closed by M431 for the check and the counter*; what remains is the **journal**, which stays depth-0. | The enforcement half shipped (`env_budget_applies`). The journal deliberately did not: a forked parallel child appending to the same file would interleave with its siblings, and the parent already reconciles each child's piped count. So `runs`' `tool_calls` for a fan-out is the reconciled total while the journal's `tool_call` events remain top-level only — an asymmetry worth stating before someone joins on it. | ROADMAP M431 |
| **Daemon fleet-worker changes (`fresh`, exit codes, the `--output json` downgrade).** | **Promoted from a deferral to a planned EXPERIMENT with a discard gate**, at the maintainer's direction: [`plans/2026-08-daemon-fleet-worker.md`](plans/2026-08-daemon-fleet-worker.md) carries the honest implications, the design, and — written before any code — the criteria for discarding it. Built on `feat/daemon-fleet-worker`, field-tested on zigodot and chrtext over several sittings, then reviewed. The reason it is not simply built: history persisting across requests is **documented, correct behaviour for a warm interactive helper**, so the real question is whether the daemon should be a fleet worker at all rather than whether it has a bug. One of the three WAS an unambiguous bug (`--connect --output json` silently becoming text, on a Stable surface) and **landed on master at M431g**, ahead of the experiment and exempt from its discard gate; the remaining two (`fresh`, exit codes) are the experiment's. | [plans/2026-08-daemon-fleet-worker.md](plans/2026-08-daemon-fleet-worker.md) |
| **Taper `spawn_parallel` children's iterations, or state why they are not tapered.** | Looked like a defect and is at least an inconsistency: `spawn_subagent` applies `jc_subagent_iters_at_depth` (halving per level) while `run_child` passes `max_subagent_iters` raw, so a depth-1 subagent gets base/2 and a depth-1 parallel child gets base. **Checked before parking:** `SUBAGENTS.md` scopes the taper to *a deep synchronous chain*, and parallel children are a fan at one depth — what bounds a fan is the per-child slice, which M431 makes real. Tapering would silently halve every child's iterations with no measured failure behind it. So the honest remainder is a *documentation* decision (say which rule governs a fan) rather than a code change, and it should be made by whoever can say whether one rule for "a delegate at depth 1" is worth the behaviour change. | ROADMAP M431 |

## Closed — found while merging M430 and M431 (2026-08-13)

Two milestones were authored the same afternoon and both claimed the number M430; the
later one (this document's M431) was renumbered, because the other was already pushed
and renumbering unpublished work costs nothing. Reading both diffs against each other
is what surfaced its row — and it was the same defect class M431 is about, which is
why it was recorded rather than quietly built. **The row it produced is closed; the
heading is kept for the lesson, retitled at M463 because an `## Open` section with no
rows makes this page advertise work that does not exist.**

