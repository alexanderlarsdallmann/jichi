# JICHI TEST_INTEGRITY.md — gap analysis (analysis 47)

## TL;DR

*Read-only survey of docs/TEST_INTEGRITY.md at HEAD 1b71247d (731 lines, the* *tree's "testing the tests" doctrine), cross-checked against the lints it names,* *2026-10-05.*

*The document is unusually self-critical and mostly right: I verified five of* *its named guards actually exist and do what they claim (rig_lint, check_graders,* *jc_env_verify_sanity, smoke-faults, smoke_lint check 16), and its driver count* *is a self-aware dated measurement ("317 ... 236 when this was written* *2026-08-22"). The one clear defect is a MIS-COUNT that is self-referential:* *the summary line says rig_lint "enforces **five** properties" and lists five,* *but the code has **six** numbered checks — and the document's own body (line* *617) cites "check 6". A test-integrity doc that undercounts its own* *integrity lint, and a milestone sweep (M463) that "corrected" the number to* *five from the doc rather than the code.*

## 1. What the document is
731 lines, "Who watches the watchmen?" A catalogue of **nine** failure modes
(§1–§9), each tied to a *measured incident* (M201, M205, M185, ...) and the
guard adopted in response. It is the register's own doctrine for trusting the
suite. The falsifiable claims are: (a) each incident is real, (b) each named
guard exists and does what it says, (c) the *counts* of properties/drivers are
right.

## 2. The one defect: the five-vs-six mis-count (self-referential)

**The claim (line 91):** "`tests/e2e/rig_lint.py` enforces **five** properties
across the residual Python drivers (no private request readers, no truncating
body loops, `SO_REUSEADDR` on every listener, every driver can fail, no
orphans)."

**The code (`tests/e2e/rig_lint.py`, 136 lines) has SIX numbered checks:**
  (1) a private reader            — line 70
  (2) the truncation bug, any spelling — line 80
  (3) SO_REUSEADDR on INET listener — line 90
  (4) a test that cannot fail       — line 97
  (5) orphans: not named in run.sh  — line 119
  (6) **harness parity: no literal binary names** — line 103

The summary names five and **omits check 6** ("no literal binary names" / the
built-binary-is-the-tested-binary harness-parity). And the document *contradicts
itself*: **line 617** reads "`rig_lint.py` check 6 rejects any driver naming the
binary as a literal (`_e2e.BIN` is the only sanctioned spelling)" — the same doc
cites a check 6 its own summary says does not exist.

## 3. The M463 history: the sweep that "corrected" it to five

`git log -S "five properties" -- docs/TEST_INTEGRITY.md` → **M463 (2026-10-05)**
is the commit that set the number to five. M463 is itself the register's
"125 rotted-claim" fleet sweep. So the mis-count is not a long-forgotten line:
**the most recent sweep touched it and wrote five** — because it read the
summary's *list* (five named) rather than the code (six checks). Check (6)
harness-parity is M271 (2026-09-23), which predates M463, so the sixth check was
already in the code when M463 ran. The sweep's per-page method (verify counts
against the doc's own table at line 392, which *does* list harness-parity) was
good; the summary line at 91 was the one place it trusted the prose.

**Why this is the class, not a typo.** TEST_INTEGRITY.md is the one doc whose
*job* is to be the source of truth for "how many things does our integrity lint
check." Undercounting its own instrument, by one, in the very sentence that
names the instrument — and doing it in the same doc that correctly cites check 6
two sections later — is the self-referential failure the document exists to
describe, happening to the document itself. A reader who trusts the summary
learns rig_lint has no harness-parity check; a reader who reads §M271 learns it
does. The two cannot both be right.

## 4. The guards I verified exist (the honest majority)
Each named in the doc, each confirmed present and doing the claimed job:
- **rig_lint.py** — 136 lines, the 6 checks above, exits fail on any. ✓
- **check_graders.py** (tests/bench/) — mode 4's oracle guard. ✓ present
- **jc_env_verify_sanity** — mode 5's "green that proves nothing" guard, in
  jc_agent.c + jc_envelope.c (the verify gate refuses a green that parsed no
  count). ✓
- **make smoke-faults** + smoke_lint.sh check 16 — every FAULT-gated driver
  named in the Makefile. ✓
- **mode 8** (read a gate's exit code, not `| tail -1`) — a doctrine, not a
  lint; no guard to verify, correctly framed as practice. ✓

## 5. Correction (ready to apply)
One sentence, line 91: "five" → "six" and add the omitted property to the list:
"(no private request readers, no truncating body loops, SO_REUSEADDR on every
listener, every driver can fail, no orphans, **and harness parity — no literal
binary names**)." ~30 characters. Optionally add the self-check rule to the
doc's own method section: *counts in a summary line are verified against the
code, not the doc's other prose.*

## 6. Limitations (honest)
- I did not run rig_lint.py (it needs JC_E2E_BIN and the e2e harness); the
  six-check count is from reading the numbered comments in the file itself,
  which is authoritative for *what* it checks.
- The nine incidents' *measurements* (M201, M205, M185, ...) I verified only
  as ROADMAP-consistent citations, not by replaying each incident.
- "smoke_lint enforces ten-plus properties" — I verified check 16 and check 5
  exist; I did not count all ten-plus to confirm "ten-plus" is not itself low.

## 7. Appendix: reproducible (all from the jichi checkout)
  grep -n "# (" tests/e2e/rig_lint.py                          # the six checks
  sed -n '91,96p' docs/TEST_INTEGRITY.md                        # "five" + list
  sed -n '617,618p' docs/TEST_INTEGRITY.md                       # "check 6"
  git log -S "five properties" --format="%h %ad" --date=short -- docs/TEST_INTEGRITY.md
                                                                 # M463 wrote five
  git log --format="%h %ad %s" --date=short -1 --grep="M271" | head -1
  grep -n "check 6" docs/TEST_INTEGRITY.md | cut -c1-90

## 8. Verdict
TEST_INTEGRITY.md is in good condition and is one of the tree's most honest
documents — five of its named guards verified real, its driver count
self-aware as a dated measurement. It "definitely lacks" exactly one thing:
its own summary line undercounts the instrument it describes (five instead of
six properties), and the same document cites the missing check six two
sections later. The fix is a sentence; the lesson is that the sweep which
"corrected" the number read the doc, not the code.
