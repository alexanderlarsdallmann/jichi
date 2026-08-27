# Seam survey, 2026-08-11 — what M375's incident points at next

A follow-up to [`2026-08-09-three-seams.md`](2026-08-09-three-seams.md), run the
day M375 landed. Method: rather than replaying the registers (DEFERRED holds 50
rows and knows its own schedule), this survey asks what **today's incidents**
expose that the registers do not yet know — the M375 lens: silent drops, and
vocabularies without owner lints. Every claim below was verified against code or
by running the command, per the M326b rule (check the checkable part first).

## S1 — the smoke tier can dial a real model (live defect, measured today)

**Incident.** M375's born-red run of `flags.sh` made real HTTPS calls to the HRZ
proxy — the driver output shows `[route] -> zigodot-fast-coder` and live 429
retries. A smoke driver, in the Python-free build-validation tier, spent real
key budget.

**Mechanism.** `smoke_home` isolates `$HOME` (so `~/.jichi` is neutralized) but
**not the cwd**. Config resolution consults `./local/config.json` ahead of
`~/.jichi` (`jc_config.c:82`), and on a dev box that git-ignored file holds real
endpoints. Any driver that runs `"$BIN"` from the repo root without `--config`
inherits it. Verified: **7 drivers** currently run `$BIN` with no config
reference at all (`degenerate_store`, `doctor_cache`, `export`, `learner_flow`,
`learn`, `project_records_lint`, `sessions_footprint`) — all subcommand drivers
today, so no model call yet, but the hazard is one edit away, and my own new
checks fell into it on their first run.

**Mend (proposed).** `smoke_home` additionally writes a benign `{}` config and
exports `JC_CONFIG` pointing at it — verified feasible: `$JC_CONFIG` outranks
`local/config.json` (`jc_config.c:75`), and a driver's explicit `--config` still
outranks the env var. Teeth: a canary run with a planted `local/config.json`
must NOT reach it. Optionally `smoke_lint.sh` gains the absence check. This is
TEST_INTEGRITY failure mode 8's cousin: the harness's isolation claim was
narrower than believed, and the gap was found by an incident, not the lint.

## S2 — `-p` is silently dropped when a subcommand matches (live defect, M375's family)

**Measured.** `jichi --config-json "{}" -p "what is this" describe` runs
`describe` and discards the prompt without a word; exit 0. Subcommand dispatch
matches on `pos[0]` regardless of `print_prompt` (M375's guard sits *after* the
dispatch chain, so it never sees this case).

**Mend (proposed).** Refuse the combination: a `-p` prompt and a dispatched
subcommand are two different run modes, and choosing one silently is exactly the
sin M375 fixed one layer down. Same guard family, same exit 2, one smoke check
born red. (The `ls` special case — `print_prompt == "ls"` — is the positional
form and stays.)

## S3 — the `@`-reference vocabulary has no owner lint (registry-series gap)

M372 closed the registry series over "flags, subcommands, tools, slash
commands, config keys, events, fields, tags — and keys". The **`@`-references**
(`jc_refs.c`: 13 `JC_REF_*` kinds, from `@diff` to `@ref:<name>`) are a
user-typed vocabulary that is not on that list. Hand-audit today first
reported the coverage clean — **wrongly**: the lint built from this finding
(M377, `refs_lint.sh`) caught `@img:` genuinely undocumented on its very
first run. The hand-audit had produced two alarms, dismissed one correctly
(`@ref:` vs "alias") and false-cleared the other by misreading its own grep
output — the exact rot mode "prefer a lint to an audit" names, demonstrated
inside the survey that proposed the lint. The lint extracts the token list
from `jc_refs_scan`'s `strncmp` prefixes with an extraction floor and
requires each as `@<token>` in `REFERENCES.md`.
`reading_refs_lint.sh` is unrelated (docs/reading prose-drift). Control-channel
verbs were spot-checked as a second candidate: documented in CONTROL.md,
low-churn, already exercised end-to-end by `control.sh` — weaker case, noted
only.

## S4 — observed, not actionable in jichi: caching invisible on the wire

Measured today after the HRZ pod restart: vLLM prefix caching is **live** on
`jlu/gemma-4-31b-it` (identical 18.9k-token prefix: 2.97s cold → 0.16s repeat,
18.6×) while the wire carries **no** `prompt_tokens_details` — so jichi's
`cached=` line and cost accounting read zero through a working cache. Nothing
jichi can fix (the data is not sent); worth one honest sentence in
`PROMPT_CACHING.md` so an operator does not read `cached=0` as "caching broken"
on such a backend. The practical inversion for configs tuned against this
backend (zigodot): prefix **stability** now pays where prefix **size** was
being optimized.

## Standing items, for completeness (registers already own these)

- **In-turn loop detector** — measurement window opens **2026-08-14** (DEFERRED
  top row; the bar is a week of ordinary use, deliberately not jumped today).
- **Craft A/B** — waiting on the operator's grading (never Claude's).
- **TEST_INTEGRITY open recommendations** — `make teeth` helper, per-module
  check counts, the M201 re-ask for the unit suite.
- **GATE_INTEGRITY** — `--strict-green` default flip is the operator's call; the
  gate-rehearsal helper waits for a run with a reference completion.

## Recommended order

S1 first (a harness-integrity defect with a same-day incident, small mend,
provable teeth), S2 with it (same family, ~20 lines total), S3 as the
registry-series closer, S4's doc sentence alongside. None blocks the 08-14
measurement.
