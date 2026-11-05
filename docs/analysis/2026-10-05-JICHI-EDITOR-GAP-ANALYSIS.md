# jichi editor support: the one thing it definitely lacks — a real session on the path that makes it risky

> **Date:** 2026-10-05
> **Method:** read-only analysis of the jichi source tree at
> `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`). One exception,
> stated plainly: `make elisp-test` was **run** (read-only, offline, against
> the project's own stub) to establish what the suite does and does not
> exercise. No source files were modified. All other claims cite a source;
> inferences are flagged.
> **Companions:** `JICHI-PRESENTATIONS-GAP-ANALYSIS.md`,
> `JICHI-CURRICULUM-GAP-ANALYSIS.md`, `JICHI-UX-GAP-ANALYSIS.md`,
> `JICHI-DEBUGGER-GAP-ANALYSIS.md`.

---

## 1. TL;DR

jichi ships editor support for four editors over a single headless contract
(Emacs `jichi.el`, Vim/Neovim `jichi.vim`, nano `jichi-nano`, and the built-in
ACP server `jichi serve` for Zed / any ACP editor), plus an LSP integration
(for the *agent's* diagnostics, not the editor's) and an MCP client. The
flagship — Emacs — streams tokens, supports four dispositions, and has an
agentic command (`jichi-task`) that lets jichi edit files on disk and then
**offers to revert the buffers whose files it changed**.

The one thing this support definitely lacks is **a recorded real-model session
on the agentic path** — `jichi-task` editing files under open buffers, with
the file-change-detection and buffer-revert coordination actually working.
The entire editor/ACP test surface in the tree is **offline**: nine ERT tests
drive `jichi-stub.sh` (a shell script that prints a canned `(stub-answer)`),
the ACP smoke tests use a `"model":"mock"` config, and the one end-to-end test
that reaches a real model (`tests/e2e/acp_terminal.py`) is gated on
`JC_E2E_MODEL`, which **nothing in CI, the Makefile, or the docs sets**.
The riskiest code — the revert coordination — has **zero tests**, is
structurally unreachable by a stub that never writes a file, and has **no
feedback channel** for an editor user to report a break.

This is the fifth member of the same class, with a sharper twist: for the
debugger, UX study, curriculum, and presentations the gap was "never pointed
at the real thing." Here the gap is that **the test suite is *designed* to be
offline** — a correct, hermetic decision — and that same design is precisely
what keeps the path that makes an editor integration *dangerous* (it edits
your files) unproven. A read-only integration can only surprise you with bad
text; an agentic one can surprise you with bad *files*. The suite proves the
former plumbing and says nothing about the latter.

## 2. What editor support *does* have (for precision)

- **A single headless contract** (`docs/EDITORS.md`): "compose a prompt, pipe
  to `jichi -p -` on stdin, read the answer from stdout, keep stderr
  separate." Every integration is "a thin client" over it; "No editor needs
  any jichi C code, and none is privileged over the CLI."
- **Emacs** (`editors/emacs/jichi.el`, 510 lines): `make-process` streaming
  (`:filter` inserts chunks live into `*jichi*`), four dispositions (side
  buffer / at point / append / replace-region), `jichi-mode`, and the agentic
  `jichi-task` (`--auto`): asks `yes-or-no-p` naming the workspace root,
  streams the transcript, and after a successful run "**detects which open
  buffers' files it changed and offers to revert them**" (jichi.el:438–474;
  `EMACS.md` §The agentic command).
- **Vim/Neovim** (`editors/vim/jichi.vim`, 154 lines): `:JichiAsk` /
  `:JichiRegion` / `:JichiTask`, modal, keyboard-driven. **No tests** (grep of
  `tests/` for `jichi.vim`: empty; `VIM.md` makes no test claim).
- **nano** (`editors/nano/jichi-nano`, 51-line shell wrapper): `ask` / `edit`
  (backs the file up to `.bak` first) / `explain`; a bridge for servers.
- **ACP server** (`jichi serve`, real C in `src/`): the built-in path for
  Zed / any ACP editor, streaming + granular tool approval.
- **LSP** (`docs/LSP.md`) and **MCP client** (`docs/MCP.md`): the *agent's*
  diagnostics/navigation and external-tool reach — not, strictly, editor
  integrations, but the "editor-shaped" surfaces they share the tree with.
- **A test suite, all offline**: `tests/elisp/jichi-tests.el` (9 ERT tests)
  "drive `jichi-stub.sh` (a stand-in binary) so **no real jichi and no
  network are involved**" (`EMACS.md:143`); ACP smoke tests
  (`acp_load`, `acp_cancel`, `acp_result_utf8`, `acp_child_stdin`, …) run
  against a config whose only model is `"name":"m","model":"mock"`; the
  e2e `acp_terminal.py` is the one real-model path and "Runs only when
  `JC_E2E_MODEL` is set … skips otherwise so CI stays offline."

Every *artifact* of editor support exists, and the offline suite is a
deliberate, correct choice. What does not exist is a real session on the
agentic path, and — the sharper point — the offline design means the CI will
not produce one either.

## 3. The gap, stated

> **jichi's editor support has never been run on its agentic path against a
> real model in any recorded session, and its test suite is built to be
> offline — so the path that makes an editor integration *risky* (jichi-task
> editing files under open buffers and reverting them) has zero tests, is
> structurally unreachable by the stub, and has no feedback channel.**

Two strength levels, as in the companion analyses:

* **(Strong) Zero recorded real-model editor sessions, and the CI is
  designed to stay that way.** The nine ERT tests are stub-driven by
  assertion in their own header; the ACP smoke tests are mock-driven; the
  one real-model e2e is gated on a variable nothing sets. I ran the elisp
  suite to confirm: 9/9 pass against a shell script that prints
  `(stub-answer)`.
* **(Weak, but the one with teeth) The riskiest code is the untestable-
  offline code.** `jichi-task`'s file-change detection + revert offer is
  exactly what a stub (which never writes a file) cannot exercise, and it has
  no other test. So the project's strongest safety property for the offline
  world (hermetic CI) is the exact reason the property no one can yet assert
  about the agentic path ("your buffer comes back in sync after jichi edits
  a file under it") remains a design claim.

## 4. Why "definitely lacking"

| Candidate | Status |
|---|---|
| Vim/Neovim plugin untested | **True, and unstated** (no tests, no test claim in `VIM.md`). A real gap — but a *coverage* gap, and the Vim plugin is the second-tier, modal, non-streaming integration. |
| nano wrapper | A 51-line shell bridge; "on a server" use. Trivial surface; not the load-bearing integration. |
| No ELPA/MELPA distribution | **True** (grep for `elpa/melpa/package-install` in the editor docs: empty). A *reach* gap, and a deliberate "plain markdown, no install step" posture consistent with the project. |
| No feedback/issues channel for editor users | **True** (no feedback/issues/contact section in `EDITORS.md`/`EMACS.md`/`VIM.md`; the README has no editor-user route). A real gap, and it is the reason the *next* item can never be found. |
| **A real-model session on the agentic path (jichi-task file-edit + buffer-revert)** | **Not parked, not deferred, not claimed — simply unreachable by the offline suite and unrecorded.** The read-only path is proven (stdin reaches the binary, stdout inserts, errors surface). The agentic path — the whole value of an editor integration over the CLI — is not. |

The distinctive form, the fifth instance: the editor integrations are
**protocol** clients, and the offline suite proves the **protocol**. But an
agentic editor integration is not only a protocol client — it is a
**coordinator** between an external process that edits files and the editor's
in-memory buffers. The coordination (detect which buffers' files moved, offer
to revert, undo-ability) is where a read-only test — a stub that answers and
never touches disk — has no business. The project correctly refuses to put
a network call in CI; it has not yet built the *other* hermetic thing that
would prove the coordination: a test in which the "stub" actually edits a
file under an open buffer, and the assertion is that `jichi-task` offers to
revert it.

## 5. Evidence

### 5.1 The suite is offline by its own statement

`tests/elisp/jichi-tests.el:1–4`:

> *Offline tests for the Emacs integration. They exercise the pure prompt /
> argument / de-fence helpers, and drive each disposition against
> `jichi-stub.sh` (a stand-in binary) so no real jichi and no network are
> involved.*

The stub (`tests/elisp/jichi-stub.sh`, in full) has four modes — `answer`
(default: drain stdin, `printf '```elisp\n(stub-answer)\n```\n'`, exit 0),
`echo`, `error` (stderr + exit 1), `interrupt` (exit 130). **It writes no
file.** Therefore the tests can prove: the prompt reached the binary
(`stdin-reaches-binary`), an answer is fenced and de-fenced into the right
place, an error surfaces to `*jichi errors*`, a SIGINT is handled. They cannot
prove anything about disk, because nothing in the suite touches disk.

I ran it: `make elisp-test` → `Ran 9 tests, 9 results as expected, 0
unexpected`. Green, offline, against the stub. (This is the one command this
analysis executed; it is the project's own read-only gate.)

### 5.2 The agentic path has zero tests and is unreachable by the stub

`jichi.el:438–474` is the `jichi-task` after-run logic: it streams the
transcript, appends the tool log, and "after a successful run, detects which
open buffers' files it changed and **offers** to revert them
(`y-or-n-p "jichi changed %d file(s); revert?"` → `revert-buffer
'ignore-auto 'noconfirm')`. Grep of `tests/elisp/jichi-tests.el` for
`reload|revert|modified|file-changed|auto-revert`: **empty.** The
dispositions that *are* tested (`display`, `insert`, `replace`, `error-path`)
are all read-only — they take the stub's canned text and place it. None
simulates jichi having written a file out from under a buffer.

So the highest-stakes behavior of the flagship integration — the reason you
use `jichi-task` instead of the CLI is that it keeps your buffers honest
after an external edit — is the one behavior with no test, no real-session
record, and a test *infrastructure* that cannot express it as written.

### 5.3 The real-model e2e is gated, and the gate is never set

`tests/e2e/acp_terminal.py:1–3`: "*…model. Runs only when `JC_E2E_MODEL` is
set (against `JC_E2E_REAL_CONFIG`, a config that can actually reach a model);
skips otherwise so CI stays offline.*" Grep of `.github/`, `Makefile`, and
`docs/` for `JC_E2E_MODEL`/`JC_E2E_REAL_CONFIG`: the only hits are the e2e
itself and a Makefile comment ("Set `JC_E2E_MODEL=<id>` to also run …").
**No CI job, no config, no doc sets it.** So the one test that could
reach a model on an editor-shaped surface (ACP) is, in the committed record,
never run.

### 5.4 The second-tier integrations are untested; there is no feedback route

- **Vim** (`jichi.vim`, 154 lines): no tests anywhere (grep `tests/` for
  `jichi.vim`: empty) and `VIM.md` makes no test claim — the honest posture,
  but the Vim plugin is still "an integration the suite has never touched."
- **No feedback channel**: `EDITORS.md`, `EMACS.md`, `VIM.md` carry no
  issues/feedback/contact section, and the README has no editor-user route.
  If `jichi-task` mis-reverts a buffer for a real Emacs user, the mechanism
  by which that break would reach this repository is the same one that
  reached the DEFERRED register for code defects — but the integration docs
  don't even name it.

## 6. Implications

1. **The value proposition is the untested part.** A read-only editor
   integration (`jichi-ask`) is a convenience over `jichi -p` in the shell.
   The *point* of an editor integration — and of `jichi-task` specifically —
   is coordinated file editing: jichi changes files, your buffers follow.
   That is the claim no test in the tree supports. The read-only plumbing is
   proven; the reason to use the editor at all is not.
2. **The hermetic design is correct and is the cause.** Keeping CI offline
   is right (hermetic, fast, no cost, no key). The consequence is that the
   one behavior that needs a *filesystem side-effect* — external edit, then
   buffer-revert — has no offline home, because the current stub only
   produces text. The fix is not "add a network test"; it is a **stub that
   writes a file** (a `JICHI_STUB_MODE=edit` that touches a path), and an
   assertion that `jichi-task`'s revert offer fires. Hermetic, offline, and
   reaching the coordination for the first time.
3. **The ACP server shares the gap.** `jichi serve` is the "rich,
   interactive" path for Zed/ACP editors, and its tests are the mock-model
   ones. The real-model e2e that would exercise a live ACP session is gated
   off. So the gap is not Emacs-specific: *every* editor-shaped surface is
   proven against a stub or a mock, and the one live-model e2e is never run.
4. **No feedback channel means the first real break is also the first
   report.** For the code, defects reach DEFERRED/ROADMAP because the
   maintainer's own runs surface them. For an editor integration, the users
   are *other people's* Emacs/Vim configs, and the docs give them no route
   back. The integration is, in practice, tested by the maintainer alone —
   and the maintainer's testing of it is the offline stub.
5. **Meta, the fifth of the same class.** Debugger: no observation of a real
   crash. UX: no observation of a non-builder. Curriculum: no delivery.
   Presentations: no audience. Editor support: **no observation of the
   integration doing the one thing that makes it worth observing** — and,
   uniquely here, the observation is *structurally excluded* by a correct
   offline design rather than merely *not yet arranged*. The first four were
   "the instrument exists, the precondition is one arrangement." This one is
   "the instrument exists, and the offline test that is its precondition for
   CI can only be made to reach the agentic path by giving the stub a
   filesystem."

## 7. Recommendation

1. **Give the stub a hand that touches disk.** Add `JICHI_STUB_MODE=edit`
   to `jichi-stub.sh`: drain the prompt, then `touch` (or write one line to)
   a path the test opened in a buffer. Add one ERT test that runs
   `jichi-task` (or the underlying after-run helper) and asserts the revert
   offer fires for that file — i.e., that Emacs noticed the external edit.
   Offline, hermetic, CI-safe, and it tests, for the first time, the
   coordination the integration exists to provide. This is the single
   highest-leverage change in the analysis.
2. **Turn on the real-model e2e somewhere.** `acp_terminal.py` already
   says "deterministic given any tool-calling model" and "no assertion
   depends on the model's wording — only on the protocol traffic." A
   scheduled (non-PR) CI job that sets `JC_E2E_MODEL` on one cheap model —
   the local-model fork the curriculum's Module 0 names as a first-class
   path — would close the "never run against a model" gap for ACP without
   making PR CI pay for it.
3. **Name the feedback route in the editor docs.** One line each in
   `EDITORS.md`/`EMACS.md`/`VIM.md`: where an editor user reports a break
   (the same register the code uses). Without it, the first real
   mis-revert is invisible to the tree by design.
4. **Test or say it isn't tested, for Vim.** Either a minimal `jichi.vim`
   check (the modal `:JichiRegion` transform is pure-ish and testable) or a
   line in `VIM.md` matching Emacs's honest "drive against the stub" note,
   so the coverage asymmetry is declared rather than implicit.

Rejected, in the register's spirit:

* *Add a network/model ERT test to the main suite* — breaks the
  hermetic-PR-CI property the offline design exists to keep; the right
  place for a live model is the already-gated e2e, scheduled, not on PR.
* *Assert the revert path by reading the buffer after the stub* — the
  stub writes nothing, so there is nothing to assert; the recommendation is
  to make the stub write, not to test the read.
* *Ship an ELPA/MELPA package to "force" users* — distribution does not
  test; a wider, unobserved integration is the presentations gap again
  (a claims artifact shown to a room that isn't there).

## 8. Limitations and honest counterarguments

1. **I ran the suite once.** `make elisp-test` was executed (offline,
   against the project's stub, read-only to the tree) to establish 9/9
   green. That is the one command that touched anything; it changed no
   source. The ACP mock tests and the gated e2e were **not** run here —
   their status is read from the test sources, not observed.
2. **"No real session" is absence-based.** The claim is that no *recorded*
   real-model editor session exists. A maintainer could have run
   `jichi-task` against a live model in their own Emacs, off-record —
   exactly as the curriculum/presentations analyses bound their gap. The
   register culture is the discount, not a proof.
3. **The stub's limitation is a design choice, not a defect.** Calling the
   offline suite a "gap" risks misreading it as a failure of rigor; it is
   the *opposite* — the rigor is maximal on the hermetic surface, and the
   gap is the honest statement that the surface does not include the
   filesystem. §6.2 says this plainly.
4. **Vim/nano are secondary.** The load-bearing integration is Emacs
   (streaming + agentic) and ACP (the built-in server). The Vim/nano
   findings are coverage/reach observations, not the crux; the crux is the
   agentic coordination shared by Emacs `jichi-task` and the ACP server.
5. **The "structural exclusion" claim needs the edit-mode fix to be
   fully fair.** It is true that the *current* stub cannot express the
   coordination — but it is also true that one small addition would, so the
   exclusion is *as-built*, not *in-principle*. The gap is "untested and
   unrecorded today," and the cheapest close is specified in §7.1.

## 9. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

```sh
# the four integrations + the contract
sed -n '1,40p' docs/EDITORS.md                                   # "a thin client over the headless contract"
wc -l editors/emacs/jichi.el editors/vim/jichi.vim editors/nano/jichi-nano

# the flagship agentic path (the untested coordination)
sed -n '435,475p' editors/emacs/jichi.el                          # "detects which open buffers' files it changed; offers to revert"
grep -n "reload\|revert\|modified\|file-changed\|auto-revert" tests/elisp/jichi-tests.el   # empty: zero tests for it

# the offline suite, as it names itself
sed -n '1,10p' tests/elisp/jichi-tests.el                         # "no real jichi and no network are involved"
cat tests/elisp/jichi-stub.sh                                      # 4 modes; writes no file
make elisp-test                                                    # (the one command this analysis ran) -> 9/9, offline, against the stub
sed -n '140,146p' docs/EMACS.md                                    # "drive jichi-stub.sh ... fully offline"

# the ACP surface: mock models
grep -m1 '"model":"mock"' tests/smoke/acp_cancel.sh tests/smoke/acp_result_utf8.sh
grep -n "offline, no model" tests/smoke/acp_load.sh

# the one real-model e2e, and its never-set gate
sed -n '1,6p' tests/e2e/acp_terminal.py                           # "Runs only when JC_E2E_MODEL is set; skips otherwise"
grep -rn "JC_E2E_MODEL\|JC_E2E_REAL_CONFIG" .github/ Makefile docs/ | grep -v "e2e/"   # only a Makefile comment; nothing sets it

# coverage asymmetry + no feedback route
grep -rn "jichi.vim" tests/                                        # empty: no Vim tests
grep -rn -i "feedback\|report a\|issues\|contact" docs/EDITORS.md docs/EMACS.md docs/VIM.md   # empty
grep -rn -i "elpa\|melpa\|package-install" docs/EMACS.md docs/VIM.md docs/EDITORS.md           # empty: no distribution
```

**Verdict line for the register this could seed:** *jichi's editor support
has no recorded real-model session on its agentic path: the 9 ERT tests
drive `jichi-stub.sh` (which writes no file) "so no real jichi and no
network are involved" (`jichi-tests.el:4`); the ACP smoke tests use a
`"model":"mock"` config; the one live-model e2e (`acp_terminal.py`) is
gated on `JC_E2E_MODEL`, which nothing in `.github/`, the Makefile, or the
docs sets. The riskiest code — `jichi-task`'s file-change detection +
buffer-revert offer (`jichi.el:438–474`) — has zero tests, is
structurally unreachable by the text-only stub, and has no feedback
channel in `EDITORS.md`/`EMACS.md`/`VIM.md`. The read-only plumbing is
proven; the coordination that is the whole reason to use an editor
integration is not. Revisit when: (a) a `JICHI_STUB_MODE=edit` + one ERT
test asserting the revert offer fires (offline, CI-safe), and (b) a
scheduled CI job setting `JC_E2E_MODEL` on the e2e — either one turns the
"never observed" into "observed against a filesystem" / "observed against a
model."*
