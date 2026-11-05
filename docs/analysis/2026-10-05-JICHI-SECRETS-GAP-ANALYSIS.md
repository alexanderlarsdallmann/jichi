# jichi secrets & untrusted content: the one thing it definitely lacks — a measurement of the mitigation's actual strength

> **Date:** 2026-10-05
> **Method:** read-only analysis of the jichi source tree at
> `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`). No files were
> modified. All claims cite a source; inferences are flagged.
> **Companions:** the other `JICHI-*-GAP-ANALYSIS.md` files (seven earlier).

---

## 1. TL;DR

jichi treats its *records* as the threat surface, and it has done that well:
the 2026-09-27 finding (a secret a tool prints reached the saved session and
the `--output jsonl` stream unredacted) was logged, measured with a red-first
canary, and fixed (M802 routes every on-disk sink through the redactor;
M802b handles a secret split across two streamed deltas;
`tests/smoke/redact_sinks.sh` and `redact_split_delta.sh` keep them honest).
Env scrubbing (M130), non-dumpability (M770), a secret-scrubbed audit log,
and an honest boundary statement (`SECURITY.md`: "a command running as you
somewhere else … That boundary is another user or a container") complete a
defence whose *data flow* is measured end to end.

The one thing this seam definitely lacks is **any measurement of the
*behavioural* half of its security story — the prompt-injection fence.**
`SECURITY.md` says it plainly: "**Prompt injection is mitigated, not
solved.**" The mitigation has three parts, none of which is measured:

1. **The fence is convention, not enforcement.** Fetched/external content
   (web, RSS, search, MCP resources, `@`-references) is wrapped in
   `<<< UNTRUSTED … >>>` and the system prompt states *once* "do not comply."
   `test_untrusted.c` proves the wrapper exists. Nothing proves it *works*.
2. **The one channel the source itself calls "the highest-risk" is fenced by
   a model that has a shell in AUTO mode, with no approval in between**
   (`src/tools/jc_tool_fetch.c:103`), and the *compliance* of that model has
   never been tested against an actual injection. No test in `tests/` feeds
   a hostile instruction through a fenced channel and asserts refusal.
3. **The fence's coverage rests on a single recorded judgement, and that
   judgement has an unrecorded consequence.** Exactly four call sites fence:
   `jc_tool_fetch`, `jc_tool_websearch`, `jc_tool_mcp` (resources only),
   `jc_refs` (RSS/@-ref). The MCP *tool result* is deliberately unfenced —
   M300, in-code: "the server was configured by hand by the user, which
   makes it semi-trusted by the same argument that makes the user's own
   repo semi-trusted. **That line is a judgement.**" `run_terminal_command`
   output and `read_file` of fetched material are unfenced by the same
   principle — *but the consequence of it* (fetched content re-entering the
   context via a shell echo, still labelled-untrusted at the source but
   unfenced at the re-entry) is nowhere recorded as considered.

So: the *secret* half of this seam (registered values, sinks, child envs) is
the most rigorously data-flow-verified security work in the tree; the
*injection* half (the part whose success, per SECURITY.md's own words,
"depends on the model behaving") is a mitigation with **zero measurements**
— and it is the half that an attacker actually exploits.

## 2. What this seam *does* have (for precision)

- **A value-based redactor with unit-tested core** (`jc_redact_apply`,
  `include/jc_log.h:42` — "the pure, unit-tested core"), four registration
  sites (model `apiKey` `main.c:15987`, search key `:15993`, `@`-ref values
  `jc_refs.c:385`), and **seven sinks** all redacting (eventlog, input
  history, log, request dumps, session save `jc_session.c:437` — the M802
  fix, plus the `--output jsonl` stream (redacted line-by-line in
  `main.c:1626–1638`; the tool preview that feeds it is bounded in
  `hl_tool_result`, `main.c:1871`).
- **Red-first drivers**: `redact_sinks.sh` (a canary = the mock's *registered*
  `apiKey`, echoed by a shell tool, grepped out of every sink — "Before M802
  the saved session and the --output jsonl stream carried the canary
  verbatim (checks 6-7 red) while telemetry already redacted; both now
  redact") and `redact_split_delta.sh` (M802b, the cross-delta case).
- **Process-boundary defences**: `jc_proc_scrub_secret_env` (M130 — secret
  env vars unset from child environments), non-dumpable on Linux (M770 —
  `/proc/<pid>/environ` no longer readable by same-user processes), audit
  log `0700/0600, secret-scrubbed` (`jc_audit.c:32`).
- **A written incident** (`docs/internal/2026-09-27-tool-output-secrets-
  unredacted.md`) with the mechanism, the measurement, the fix, and the
  *limit* stated: "a secret jichi never registered is caught by no sink."
- **An honest threat-model boundary** (`SECURITY.md:46–55`): "What no part of
  jichi can do is hide a key from a command running as you somewhere else …
  That boundary is another user or a container."
- **The fence** (`src/util/jc_untrusted.c`): wrapper + one-time system-prompt
  rule; applied at the four call sites; `test_untrusted.c` unit-proves the
  wrapping.

## 3. The gap, stated

> **jichi's prompt-injection defence — the fence + the "data, not
> instructions" convention — is the one part of its security story with no
> measurement. SECURITY.md calls it "mitigated, not solved"; the code calls
> the fenced fetch path "the highest-risk external channel in jichi"; and
> the tree contains no test that feeds an actual injection through a fenced
> channel and asserts the agent does not comply — no refusal check, no
> behavioural benchmark, no logged incident of an injection attempt (the
> kind every other surface in this tree has: def-rows, `analysis/` pages,
> red-first drivers). The defence's strength is a model-attitude claim, and
> the model attitude has never been probed.**

Two strength levels:

* **(Strong) Zero behavioural measurement.** Grep of `tests/` for hostile-
  instruction / injection / refusal content: no driver. `DEFERRED.md` for
  "prompt injection": no row. `analysis/` for an injection event: none.
  The fence's *existence* is unit-tested; its *effect* is untested, and the
  documented effect ("depends on the model behaving") is precisely the
  variable that changes with every model swap, every provider fallback, and
  every `--model` the user chooses.
* **(Weak, but structural) One recorded judgement carries an unrecorded
  consequence.** The fencing policy *is* recorded — at the decision point,
  in-code (M300, `jc_tool_mcp.c:86–91`: MCP tool results unfenced because
  a hand-configured server is "semi-trusted by the same argument that makes
  the user's own repo semi-trusted. That line is a judgement"). The
  consequence that policy implies — fetched content *re-entering* the
  context unfenced via a shell echo or `read_file` — is not recorded as
  considered anywhere: no DEFERRED row, no analysis, no test names it.

## 4. Why "definitely lacking"

| Candidate | Status |
|---|---|
| Registered secret reaching an on-disk sink | **Measured and red-first** (`redact_sinks.sh`, 7 sinks; the pre-M802 state is the documented red). Not a hole. |
| Secret split across streamed deltas | **Measured** (`redact_split_delta.sh`, M802b). Not a hole. |
| Secret leaking into a child process env | **Fixed + documented** (M130, `jc_proc.c:108`); non-dumpable on Linux (M770). Not a hole. |
| Unregistered secret (a key jichi never holds) | **Limit stated, boundary drawn** (incident doc §Resolution; `SECURITY.md`: "another user or a container"). An honest boundary, recorded — not a hidden hole. |
| Project config pointing a key at another endpoint | **Fixed and red-first** (`project_config_trust.sh`, M851: "a cloned repository could carry one that redirected model requests — and the user's key — to its own endpoint … Each was reproduced offline"). Not a hole. |
| **Injection through a fenced channel: does the agent refuse?** | **No measurement of any kind.** The mitigation is named, its dependence on model behaviour is *self-declared*, and no test, row, or analysis probes it. |
| **Fencing consequences** (re-entry of fetched content via shell/read_file, unfenced MCP tool results) | **The policy judgement is recorded in-code (M300, `jc_tool_mcp.c:86–91`); its *consequences* are recorded nowhere** — no DEFERRED row, no analysis, no test names the re-entry path. |

The distinctive form, the ninth instance: every other analysis found a
*live* boundary the offline design stops at (a crash, a learner, a class, a
room, a file edit, a re-drive, a fleet, a tool call). This one finds the
boundary where **a defence's own documented strength is a property of the
model, and the model's property is unmeasured** — the set's first gap that is
not about a *feature* never being used, but about a *promise* never being
checked.

## 5. Evidence

### 5.1 The self-declaration

`SECURITY.md:56`: "**Prompt injection is mitigated, not solved.** Content
jichi fetches at the *model's* choosing — a web page, an RSS feed, an MCP
resource — is wrapped in an explicit 'data, not instructions' fence, and the
convention is stated once in the system prompt. That is a mitigation whose
success depends on the model behaving…"

The document does the honest thing the register does everywhere: it states
the limit. But unlike the platform register (which then *measures* what it
can) or the editor page (which then explains the stub), this claim has no
measurement attached — no "refusal rate across N prompts," no incident log,
no DEFERRED row, no driver.

### 5.2 The fence's own highest-risk note

`src/tools/jc_tool_fetch.c:102–106`: "Cap the body so a huge page cannot
blow up the context, then fence it as untrusted (M300). **This is the
highest-risk external channel in jichi: the URL is chosen by the MODEL, and
in AUTO mode the result reaches a model that has a shell without any
approval prompt in between.** The fence is a mitigation, not a fix."

The two properties that make fetch the worst channel (model-chosen input,
shell-armed downstream, no gate between) are exactly the two that a test
would have to reproduce to prove anything — and no test does.

### 5.3 What is and isn't tested

- `tests/test_untrusted.c`: asserts the wrapper emits
  `<<< UNTRUSTED web page`, `END UNTRUSTED`, and the prompt rule — **structure
  only**.
- `tests/smoke/project_config_trust.sh`: the *config* trust boundary
  (M851, reproduced defects, red-first) — the adjacent defence, not the fence.
- Grep of `tests/` for hostile-instruction / injection / "ignore
  instructions" / refusal-through-fence: **no driver**. Grep of `DEFERRED.md`
  for "prompt injection": **no row**. Grep of `docs/analysis/` for an
  injection event: **none**.
- The four fenced channels (`fetch`, `websearch`, `mcp` resources, `refs`)
  vs. the unfenced re-entry: a shell command or `read_file` on already-
  fetched content returns that content to context *without* the fence.
  **Flagged inference:** whether that is a hole or the intended boundary
  (local file = user's responsibility) is not recorded anywhere; the
  `jc_untrusted.h` "semi-trusted … judgement" comment is the only trace of
  the decision.

### 5.4 The contrast that makes it sharp

The *secret* half has the full register treatment: incident → measurement →
red-first driver → fix → stated limit. The *injection* half has: the
`SECURITY.md` paragraph and the in-code comment. One half of the security
page was measured with a canary; the other half was declared. The declaration
is honest; the measurement is the missing half.

## 6. Implications

1. **The fence's effectiveness varies with the model, and the model varies.**
   Fallback walks to another provider; routing swaps the model per turn;
   the user passes `--model`. The one variable the documented defence
   depends on is the least pinned variable in a jichi run. A refusal that
   holds on the frontier model may not hold on the local 4B the docs
   advertise. No datum exists for either direction.
2. **AUTO mode is where the defence lives, and AUTO mode is where it's
   unmeasured.** The fetch note's "no approval prompt in between" is an
   AUTO-mode statement. The entire unattended-run story (budgets, verify
   gates, journals) is jichi's headline autonomy claim; the injection fence
   is the only security control in that story with no measurement.
3. **The unfenced re-entry is the standing attack shape.** Fetch →
   `run_terminal_command` (echo/cat the page) → context, unfenced. If the
   fence's rule is "stated once in the system prompt," a page containing
   text that *restates the fence's END marker* (or that the model has seen
   echoed without markers) is the probe that would tell. That probe is
   cheap, offline (local file, mock model scripted to comply or refuse), and
   does not exist.
4. **The trust-boundary judgements need the register form.** "Terminal
   output isn't fenced" and "MCP resources are semi-trusted" are
   threat-model decisions with rejected alternatives (fence it; don't
   fence it; fence by source not channel). They are currently one comment
   each. Every other decision of this weight in the tree has a DEFERRED row
   or an analysis page.
5. **Meta, the ninth, the set's turning point to promises.** Eight analyses
   found *features* never exercised. This one finds a *defence* never
   checked — the class the register was built to avoid (M326b: "a check that
   passes because its universe shrank"). The fence's unit test passes
   because its universe is a string, not a model. The honest completion is
   the one `SECURITY.md` already implies: a named limit — "refusal is
   unmeasured; the model's behaviour is the control" — is not there yet.

## 7. Recommendation

1. **A behavioural probe set, offline and red-first.** Ten-to-twenty hostile
   payloads (instruction-override, fence-escape with a fake `END UNTRUSTED`,
   exfil attempt via `run_terminal_command`, approval-pressuring prose) fed
   through the *real* fetch/ref paths against the mock model's scripted
   behaviour at the *jichi* layer: the assertion is not "the model refused"
   (the mock always does what it's told) but "**the fence survived the
   pipeline**" — payload reached the model wrapped, the END-marker injection
   did not truncate the fence, and a *complying* model's attempt to echo
   the page via a tool produced output that is (a) audited, (b) still
   fenced on re-entry, or (c) at minimum logged as an untrusted-origin
   action. The last point — fencing or labelling *tool output that repeats
   untrusted content* — is the one concrete code change the probe set would
   demand.
2. **Record the fencing policy's consequences.** The policy itself is
   recorded (M300, at the decision point). What is missing is the *consequence*
   review: a DEFERRED row (or an `analysis/` page) naming the re-entry path
   (fetched content → shell echo / `read_file` → context, unfenced at the
   re-entry) and the MCP tool-result policy's reach (semi-trusted server ⇒
   its *output* is trusted data), each with the rejected alternative
   (fence on re-entry; don't). The probe set in §7.1 is what would exercise
   them.
3. **Pin the model variable.** The probe set, run across two or three
   models (frontier + the local 4B the docs cite), turns "depends on the
   model behaving" from a declaration into a table — the same move the
   platform register made for "works on this board." Until then,
   `SECURITY.md`'s honest line is the strongest statement available, and it
   should say what it now only implies: *the refusal is unmeasured.*
4. **Then the incident log.** The register's culture is "every real event
   gets an `analysis/` page." The injection defence has no incident, which
   is either luck or the absence of a surface that invites one. The probe
   set is the surface; the log is the discipline.

Rejected, in the register's spirit:

* *Adversarial fuzzing against a real model* — a network/cost/key event,
  non-reproducible, and the *model's* refusal rate is a model-vendor
  property, not jichi's. The control jichi owns is the pipeline: fencing,
  re-entry, audit. Measure that, offline.
* *Enforce the fence by stripping tool access in AUTO mode* — the threat
  model is exactly that the fetched content is *useful data* the agent
  should act on; stripping the shell is removing the product.
* *Declare the fence "solved by test_untrusted.c"* — M326b's shape: a check
  on the wrapper's string is not a check on the wrapper's effect. The
  `SECURITY.md` paragraph already refuses this; the tree just hasn't
  finished the refusal with a measurement.

## 8. Limitations and honest counterarguments

1. **The fence may be *sufficient* in practice and the gap is one of
   evidence, not safety.** A model that treats `<<< UNTRUSTED` as data is
   current frontier behaviour; the absence of a test is not the presence of
   a breach. I claim the *measurement* is missing, not that the defence
   fails. (No injection incident is recorded anywhere in the tree — which is
   also consistent with "it works" and with "it has never been probed.")
2. **The unregistered-secret limit is real, and I am not re-opening it.**
   "A secret jichi never registered is caught by no sink" is the stated,
   bounded, documented limit of the redactor — the honest boundary, like
   "another user or a container." §3 targets only the *behavioural* half.
3. **Which channels fence is a design choice, not necessarily a defect.**
   Fencing `run_terminal_command` output could *destroy* legitimate use
   (a user's shell echoing a doc they asked jichi to read). My claim is the
   *recording* of that choice (with the re-entry risk), not the choice
   itself.
4. **Absence-based, as always.** "No test feeds an injection" is a negative
   grep over `tests/`, `DEFERRED.md`, and `docs/analysis/`. A private
   refusal-rate experiment (the kind this tree does publish) would beat §3
   — none is in the tree at `1b71247d`.
5. **Tree vintage.** M802/M802b/M851 (the fixes cited) are 2026-09; the
   `SECURITY.md` paragraph and `jc_untrusted.c` are current at `1b71247d`.
   If a probe set landed between the last commit and this reading, §5.3's
   greps would not show it — the same bound as every analysis in the set.

## 9. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

```sh
# the self-declaration and the boundary
sed -n '46,58p' SECURITY.md                                  # "mitigated, not solved"; "another user or a container"

# the fence: structure, sites, and the highest-risk note
grep -n "jc_untrusted_wrap" src/tools/*.c src/command/*.c    # exactly 4 call sites (+ the helper file)
sed -n '102,107p' src/tools/jc_tool_fetch.c                  # "highest-risk external channel … mitigation, not a fix"
sed -n '85,95p' src/tools/jc_tool_mcp.c                      # "semi-trusted. That line is a judgement"

# what the secret half proves (the measured contrast)
head -16 tests/smoke/redact_sinks.sh                          # red-first canary, 7 sinks
head -6  tests/smoke/redact_split_delta.sh                     # M802b
sed -n '433,442p' src/session/jc_session.c                     # the M802 fix in the session writer
grep -n "jc_proc_scrub_secret_env" src/util/jc_proc.c | head -2  # M130 env scrub

# what the injection half proves (the gap)
cat tests/test_untrusted.c | head -35                          # wrapper-structure only
grep -rln "injection\|UNTRUSTED" tests/                        # 7 files: structure + config-trust, no behavioural probe
grep -n "prompt injection" docs/DEFERRED.md                    # empty: no row
grep -rln "injection" docs/analysis/                           # empty: no incident
```

**Verdict line for the register this could seed:** *jichi's secrets data
flow is the tree's most rigorously measured security work — incident
(2026-09-27) → red-first canary (`redact_sinks.sh`, 7 sinks) → fixes
(M802/M802b/M851/M130/M770) → stated limits (unregistered secrets; "another
user or a container"). Its prompt-injection defence is the measured half's
shadow: `SECURITY.md` declares it "mitigated, not solved … success depends
on the model behaving"; `jc_tool_fetch.c` names the fenced path "the
highest-risk external channel in jichi" (model-chosen URL, AUTO mode, shell
armed, no gate); and the tree contains **no** test, DEFERRED row, or
analysis that feeds an actual injection through a fenced channel and
asserts anything about the outcome — `test_untrusted.c` proves the wrapper's
string, not the wrapper's effect. The trust-boundary judgements (which
channels fence; the unfenced re-entry via shell/read_file) exist as one code
comment each, with no rejected-alternative recorded. Revisit when: an
offline probe set (fence-escape, re-entry echo, exfil attempt) is red-first
against the pipeline across ≥2 models, and the four unfenced-channel
judgements are recorded as decisions.*
