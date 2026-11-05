# The jichi self-audit (53 documents, 2026-10-04 / 2026-10-05)

This directory's `YYYY-MM-DD-JICHI-*.md` files are one set: a read-only audit
of jichi's own register, **run by jichi** (v0.12.0, build `1b71247d`, model
`jlu/qwen3.8-27b`) analyzing jichi. The `JICHI-` prefix and the date prefix
(deliberately preserved, not conformed to the directory's lowercase style)
mark the provenance: this is jichi reading jichi, distinct from analyses
performed under other agents.

## Method (every document follows the same discipline)

- **Read-only.** No source change in the audited tree; evidence cited to
  `file:line` at HEAD `1b71247d` (2026-10-04).
- **One finding per surface.** Each document names the single feature its
  surface *definitely lacks*, argues it, and rejects the alternatives.
- **Inferences flagged; a reproducible appendix** re-derives every
  load-bearing claim.
- Several documents are written as *findings for the register to
  reconcile*, not as claims the current code is wrong in its own terms —
  especially where they record live drift against `1b71247d`.

## What the 53 found — grouped

**A. Live drift at `1b71247d` (the code and the docs disagree *today*)** —
these are the ones most likely to be closed by other in-flight branches;
check before acting.

| Document | The live finding |
|---|---|
| `…-JICHI-MANPAGE-GAP-ANALYSIS.md` | The man page's ENVIRONMENT section has no parity lint; the code reads 12 project env vars, the page names 5 — the seven missing include the env sides of `--lang` / `--dump-requests` / `--provider`. |
| `…-JICHI-EXAMPLES-GAP-ANALYSIS.md` | The `init --list` bench literal (`main.c:5175-5180`) names **12** of the **13** domain benches — `slm-lab` is missing from the discovery path; no check reconciles the three inventory copies (directory / README / binary). |
| `…-JICHI-READINGCOURSE-GAP-ANALYSIS.md` | `docs/reading/` is 99% current, with two prose fixes due: one broken anchor (`fukabori-07:91` points at a call site, not the definition) and one inaccurate sentence (`fukabori-09:14` "before and after each mutating turn" — the code takes no after-snapshot; the green-checkpoint class is unnamed in chapter 9). |

**B. One fix that is a code change, not a doc change** — the examples and
man-page analyses converge on the same root cause: adding `slm-lab` to the
`init --list` string is a one-line fix, and the parity lint makes the class
impossible to re-enter. If another branch already lands it, these two
documents stay true as dated findings.

**C. Security-adjacent findings (the audit + the focused analyses)**

| Document | The finding |
|---|---|
| `…-JICHI-SECURITY-AUDIT.md` | The full-tree audit: no exploitable criticals; the findings (G1–G8) are hardening gaps, several already tracked as def items. |
| `…-JICHI-NETGUARD-GAP-ANALYSIS.md` | The SSRF guard has no test for DNS-rebinding or redirect-to-internal; the shell tool is outside the guard by design. |
| `…-JICHI-SCOPESHELL-GAP-ANALYSIS.md` | The shell escapes `--edit-scope` by default; `--strict-scope` is opt-in. |
| `…-JICHI-TRUST-GAP-ANALYSIS.md` | The trust gate is the most rigorous *permission* check in the tree, but the rule's own premise — that a hostile repo's instructions are treated as data — has no probe test. |
| `…-JICHI-SECRETS-GAP-ANALYSIS.md` | The untrusted-content fence's actual strength has never been measured. |
| `…-JICHI-MEMORY-GAP-ANALYSIS.md` | `memory.md` is appended to the system prompt raw, with no untrusted-content fencing. |
| `…-JICHI-TELEMETRY-GAP-ANALYSIS.md` | Redaction is a 3-entry allow-list; the full tier logs `args_full` unredacted; no canary-leak test. |

**D. Safety-mechanism gaps (the machinery exists; one leg is missing)**

deferred register (one line-anchored fact rotted — cap moved jc_agent.c:4102→5276, lint stays green) · plans (4 of 5 not-built headers stale vs ROADMAP-done milestones; no plan-status lint) · checkpoint (git-ignored files never restored by `/undo`) · snapshot (no
green answer from the destroy that destroyed) · hooks (HOOKS.md line 60 promises env vars no code has ever exported — a negative-existence claim that survived M170's rename and M711's 125-claim fleet sweep; fix = delete the line + one child-world test) · compact-optimization (recommendations for the elision asymmetry: the M218 args pass has no M348-style preservation; R1-R6 ranked) · compaction (no claim ticket
for summarized-away history) · resume-envelope (a resumed run loses its
bounds) · claudecode (standalone, not a doc-surface: profiled the sibling tool's own session logs that built jichi -- $5.1k/18 days over two sessions; per-turn budget reminders, an uncapped 301 KB of subagent hand-backs, a per-session scratchpad as working memory, 254 token-in-URL pushes on disk; C1-C5 recommendations) · autocomplete (stale quote: the doc quotes the pre-M280 Ctrl-G prompt in present tense and attributes it to jc_tui.c; M280 rewrote it AND moved it to jc_suggest.c; M711 edited this file six weeks later and missed it; the page contradicts itself across one paragraph) · observability (inverse: every checked claim true; the rot-prone event tables are two-way lint-guarded with anti-breakage floors -- the M366 pattern is the fix family for #47-#50) · decisions (14 duplicated <a id> anchors -> 28 in-register links ambiguous, 14 misdirect to the wrong same-milestone decision; no anchor-uniqueness lint; the register fails its own stated purpose: findability) · hardening (inverse-leaning: all sampled M130-M771 security claims verified true; the stale part is the section-7 header vs its own built 7.1/7.2/7.5 notes; the doc names this failure class in 5 other files) · gate-integrity (design accurate; all three line anchors stale by 1,002-2,746 lines; the line-anchor-forbidding lint scans docs/reading/ only — the doc sits outside it) · test-integrity (summary says rig_lint has five properties; the code has six, and the doc's own line 617 cites check 6; M463 wrote five from the prose) · notify (inverse: accurate doc; the doc's '/bin/sh' abbreviates JICHI_SHELL; the real gap is no 'wants input'/rollback bell) · typeahead (M753's Esc-abort and off-mode notice absent from the keys table; the 'arrows ignored' row false for Esc) · workflowmerge (the second merge destroys the conflict-loser's
work) · prompt-cache (no affirmative byte-stability test) · session (no
"which build last saved this" signal at `--resume`) · editor (no real
session on the risky path) · mcp (no tested remote *tool call*) ·
lsp (no repeatable real-server test) · semsearch (no retrieval-quality
measurement) · repomap (one-shot build is a documented feature, but the
staleness is never noted to the model) · daemon (doc-promised continuity
the code doesn't have) · orchestration (no recorded multi-machine run) ·
devcycles (the model is never told its git branch; landing ops not
effect-previewed) · cost (bounded in tokens/seconds/calls, never in
dollars, though the machinery exists to do it) · platform (no continuous
re-verification of the live gate) · portability (the driven half of the
retest regime has no debt number) · setup (the generated `run.sh` violates
all four launcher rules) · gamedev (no verify gate on the godot pack;
def-294 still open) · i18n (deck coverage: nine English decks, seven translated x4 languages, the README's denominator frozen at "seven") · board (M436 prose says board is readonly:1 and touches no filesystem; the struct is readonly:0 from the board's creation, exclusion is the main_agent_only flag, not a body check) · i18n-ui (the inverse finding: the M137 catalog's per-language coverage — 12/30 es/ja/zh, en/de complete — is build-pinned in tests/test_msg.c but disclosed in neither document) · modelchoice (only 2 of ~20 languages have runnable
probes; the NL battery has no fixture/runner).

**E. The observation class — the register's deepest pattern**

Several surfaces' "definitely lacking" is *an observation that never
happened*: the UX analysis found no study of a non-builder; curriculum, a
complete product that was never delivered to anyone; presentations, zero
recorded talks; journal, no durability/self-verification of the evidence
log; `PROJECT_TIMELINE` (37th, `2026-10-05`), the effort-estimate
document, checked its three estimate-only rows against external histories
(git v1.0.0, git's team era, tmux) — the estimate is defensible; the
decay curve and the AI-assisted row have no external benchmark at all.

These are not code bugs — they are the surfaces where jichi is most
theorized and least witnessed. The debugger analysis is the extreme form:
jichi cannot observe a running program at all (no DAP, no attach, no step).

## Reading order, if any

1. `SECURITY-AUDIT` for the tree-level view.
2. Group A (live drift) if you are about to touch man page, `init --list`,
   or `docs/reading/` — cross-check against in-flight branches first.
3. Group B/D for the concrete one-line and one-lint fixes.
4. Group E as the design review: what the project most wants is witnesses.

## Provenance

- Branch: `analysis/2026-10-05-jichi-gap-audit`, off `master` @ `1b71247d`.
- Commit trailers: `Co-Authored-By: jichi 0.12.0`, `Jichi-Build: 1b71247d
  (master, 2026-10-04)`, `Jichi-Model: jlu/qwen3.8-27b`.
- Working copies of the 53 documents live outside the tree (the audit was
  run read-only against the checkout; the copies here are byte-identical,
  verified by blob hash).
