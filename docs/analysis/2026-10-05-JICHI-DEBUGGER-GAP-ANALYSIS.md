# jichi: the one feature it definitely lacks — a runtime/interactive debugging surface

> **Date:** 2026-10-05
> **Method:** read-only analysis of the jichi source tree and documentation at
> `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`, the M898-era tree).
> No files were modified in that tree. No build was run; no jichi process was
> executed. Every claim below is grounded in a citation, and the claims that are
> inferences rather than measurements are marked as such.
>
> **Honesty rule of this document:** the argument for this being a *gap* is
> substantially an argument from **absence** (a missing tool, a missing
> register row, a missing documentation page). Absences are only as strong as
> the survey behind them. The §8 Limitations section states exactly what the
> survey covered and what it could not prove.

---

## 1. TL;DR

jichi has **no way to observe a program while it runs**: no debugger tool, no
DAP/debug-adapter, no attach, no step, no breakpoints, no watchpoints, and no
structured backtrace/core-dump inspection. It can *build* and *test* the code it
writes (static feedback) but cannot *inspect the state of* that code at runtime
(dynamic feedback).

This is not a documented, deliberate omission like notebooks or OS-level
sandboxing (both of which have named deferral rows and reasons). **It does not
appear anywhere in the project's own Deferred register, decisions log, or
comparison pages** — in a project whose stated identity is that every
"why isn't this done?" answer must be findable, the fact that this question has
no recorded answer is itself the most telling piece of evidence.

## 2. What jichi does have (so the claim is precise)

A gap claim against jichi must survive contact with a genuinely large feature
set. Per the README and `docs/`, jichi has:

- a full agentic tool loop (`read_file`, `write_file`, `edit_file`,
  `apply_patch`, `search_code`, `list_files`, `run_terminal_command`,
  `run_tests`, `fetch_url`, `codebase_search`, git read/mutate tools,
  subagents, parallel agents, `todowrite`, `remember`, `ask_user`,
  `write_plan`, `load_skill`, background commands, media generation,
  web search, LSP refactors) — 46 built-in tool names, per the lint-
  verified registry table `JC_ALL_TOOL_NAMES` in `src/tools/jc_tool.c`
  (§5.1);
- **LSP integration** — compiler/type *diagnostics* and symbol-accurate
  navigation (`find_definition`, `find_references`, `list_symbols`), i.e.
  language intelligence, which is the *static* half of debugging;
- **structured test integration** — `run_tests` parses JUnit-XML / TAP /
  `file:line` failure output and feeds it back in the fix-forward loop;
- **one-shot memory-error triage skills** — `valgrind-triage` (a skill that
  turns a valgrind/sanitizer *dump* into a root cause,
  `src/scaffold/jc_scaffold.c:832`) and a C++ ASan/UBSan triage skill
  (`src/scaffold/jc_scaffold.c:4330`);
- an **autonomy envelope** (budgets, verify gate, edit scope, rollback),
  telemetry, run journals, snapshots/undo, prompt caching, routing, MCP,
  ACP, compaction, a curriculum, and a 373-entry field comparison.

The debugging *workflow* jichi teaches its own scaffolded `debugger` subagent
is: reproduce → hypothesize → probe → `git_diff`/output → fix
(`src/scaffold/jc_scaffold.c:145-171`). Every step in that workflow is
reasoning from **static** evidence — source text, diffs, test output, one-shot
sanitizer dumps.

**What is missing is the other half: observing live program state.** When the
answer to "why does it misbehave" lives in a variable's value at line 412, in a
branch taken in an order no test pins down, or in a state reachable only after
30 seconds of running — jichi's tool surface has nothing to look at that state.

## 3. The gap, stated

> **jichi lacks a runtime/interactive debugging surface: a way for the agent to
> launch or attach to a running program, control it (breakpoints, stepping),
> and inspect it (stack frames, locals, expressions), or at minimum to obtain
> and symbolize structured crash information (backtraces, core dumps) as a
> first-class capability.**

Two strength levels, stated separately because they matter:

- **(Strong) No interactive debugger of any kind is exposed as a jichi
  capability.** No tool, no protocol (DAP), no documented configuration, no
  skill that gives the model a *jichi-owned* channel into a running process.
  The model *can* type `gdb` commands through the generic
  `run_terminal_command` shell — but that is the same as any shell tool; it is
  not a debugging surface in jichi, it is a shell jichi happens to have.
- **(Weak, but the one with teeth) Crash — the most common hard failure —
  is jichi's least-explained failure.** There is no wired path from
  "the program segfaulted" to "here is the symbolized frame where it died,
  with the relevant source." That path is exactly what turns a crash from a
  wall into a task.

## 4. Why *this* is the "definitely lacking" feature

The DEFERRED register and COMPARED page let us check the candidates:

| Candidate absence | Status in jichi's own records |
|---|---|
| **Runtime debugging surface** | **No row. No decision. No mention in COMPARED.md. Nowhere recorded.** |
| OS-level sandbox (bwrap/seccomp) | Recorded; partly built (Landlock, M831); revisit triggers documented |
| Notebooks | Recorded, deliberately deferred, with a named trigger ("somebody says notebooks are the workflow") |
| Plugin ecosystem | Recorded as *deliberate* — "MCP is the extension surface" |
| Schema-constrained output | Acknowledged in COMPARED.md (the `octomind` row) |
| Deterministic crash replay | Acknowledged in COMPARED.md (the `smelt` row) |

Every other honest absence in jichi has a *record* — a deferral with a
reason, or a comparison row, or a decision with rejected alternatives. The
project's own M326b rule says a deferral belongs on the register when
"someone could reasonably ask 'why isn't this done?'" — and the reasonable
answer for a *coding agent* being asked why it cannot debug is a judgment that
has never been written down. That is the precise sense in which this feature
is *definitely* lacking: it is the one absence the absence-tracking
infrastructure did not catch, because it was never seen as an absence.

## 5. Evidence

### 5.1 The tool surface contains no debug tool

The authoritative list is `JC_ALL_TOOL_NAMES` in `src/tools/jc_tool.c` —
jichi's own comment there says it is "NOT hand-verified" in the unsafe
direction: `tests/smoke/tool_names_lint.sh` fails the build if the table
disagrees with the tool definitions. It carries **46 names**:

```
apply_code_action  apply_patch  ask_for_help  ask_user  board
codebase_search  edit_file  fetch_url  find_definition  find_references
format_file  generate_audio  generate_image  git_add  git_blame
git_branch  git_commit  git_diff  git_log  git_stash  git_status  hint
kill_background  list_code_actions  list_files  list_symbols  load_skill
play_audio  read_background_output  read_file  read_mcp_resource
record_audio  remember  rename_symbol  run_terminal_command  run_tests
search_code  search_docs  spawn_parallel  spawn_subagent  todoread
todowrite  transcribe_audio  web_search  write_plan  write_file
```

No `debug_*`, `attach`, `breakpoint`, `step`, `inspect`, `backtrace` — a
word-for-word check of all 46 names against that vocabulary returns zero
hits (grep exit 1, §9).

**Correction, recorded rather than tidied** (the M326b rule this document is
written about): the first draft of this section claimed "26 registered
built-in tools" from a grep over a fixed list of assumed names. That
cannot be right as a total — a grep over names *I chose* can only
undercount — and it undercounted by 20, missing LSP navigation/refactor
tools, media generation, web search, and background-command tools. The
first draft's claim *survived* only because the missing 20 are also not
debug tools; the number was wrong and the conclusion was right for the
weaker reason. This is precisely the defect class this project's own
DEFERRED register names — a checkable factual claim parked without being
checked — so the correction is stated in the body, not in the diff.

Cross-check in `src/` and `include/` for debugger machinery — `ptrace`, `gdb`,
`lldb`, `debug adapter`, `DAP`, `stack frame`, `watchpoint`, `SIGSEGV`
handling that reports, `PTRACE` — finds **nothing that implements a debugging
capability**:

- All `breakpoint` occurrences in the tree are *prompt-cache* breakpoints
  (`cache_control:{ephemeral}` — `docs/ARCHITECTURE.md:155`,
  `docs/PROMPT_CACHING.md`, `docs/GATEWAY_ADMIN.md:108`).
- All `gdb`/debugger mentions are in *scaffold prose* (text jichi writes out
  for other projects) or security commentary, never in executable code.

### 5.2 The "debugger" subagent is a prompt persona, not a capability

`src/scaffold/jc_scaffold.c:145-171` ships `agents/debugger.md`:

```
description: Reproduces a bug, finds the root cause, and proposes the fix.
tools:
  - read_file
  - search_code
  - run_terminal_command
  - run_tests
  - git_diff
  - git_log
...
1. Reproduce the failure (run the failing test/command); state what you see.
2. Form one hypothesis and test it with the smallest possible probe.
3. Identify the root cause precisely (file:line + why), not the symptom.
```

Its *entire toolset is static*. Step 2's "smallest possible probe" is, in
practice, adding a print statement and re-running — the pre-interactive
debugging technique — because there is no better one on the tool surface.

### 5.3 The smoking gun: jichi must teach the model to type into a foreign debugger, by prose

The Guile skill in `src/scaffold/jc_scaffold.c:4699-4719` ("Diagnose a Guile
error … by driving the live REPL and Guile's debugger, **since Scheme has no
standard LSP**") instructs:

```
4. Enter the debugger on the error: at the REPL prompt after a throw use
   `,bt` for the backtrace, `,frame N` to select, and `,locals` to
   [inspect].
```

Jichi's own teaching material has the model hand-type a *foreign* debugger's
REPL commands through the generic shell, in a language-specific, model-
dependent recipe. The jichi-side equivalent — a tool the model can call —
does not exist. This is the project's own philosophy in tension with itself:
the ghost-text milestone (M280) justified its design by observing *"an
instruction a model may ignore is weaker than an example it can pattern-match"*
(README, M280 row). Prose REPL recipes are precisely the ignored-instruction
case; the pattern-matchable tool is the answer jichi lacks.

### 5.4 Memory errors are handled one-shot, never live

- `src/scaffold/jc_scaffold.c:832-841` — `valgrind-triage` skill: "Turn
  valgrind/sanitizer **output** into a located root cause."
- `src/scaffold/jc_scaffold.c:4330-4339` — C++ ASan/UBSan triage skill:
  "Turn a crash or a sanitizer/Valgrind **dump** into a root cause."

Both consume *post-mortem, batch* reports. There is no live-equivalent: no
"run under a debugger and inspect the allocation when it happens." (Sanitizers
are the right tool for memory errors specifically; the gap is structural —
dynamic inspection as a *general* capability — and memory errors are only the
documented fragment of it.)

### 5.5 The crash path yields the least signal

`src/main.c:17759` records that sessions die "out through `SIGSEGV` after its
refusal was printed" — jichi's own codebase treats a segfault as a *terminal
event with no captured explanation*. There is no signal-handler path that
captures, symbolizes, and presents a backtrace; no core-dump ingestion; no
"here is where the program died and here is the source" flow. (This is a
claim about jichi *as the agent*: nothing on its tool surface can turn a
crash of *another program* into structured frames either, §5.1.)

**Inference, flagged as such:** I did not execute a segfaulting program under
jichi to observe what the model actually receives. The claim rests on the
absence in the tool registry (§5.1) and on there being no crash-capture code
path in the source. It could be overstated only if a crash-capture path exists
that I failed to find.

### 5.6 ptrace is known to the project — as a threat, never as a feature

The M770 decision (`docs/DECISIONS.md:848-851`, ROADMAP M770,
`docs/HARDENING.md` §2) makes jichi non-dumpable by default
(`prctl(PR_SET_DUMPABLE, 0)`) to keep a same-user child from reading the API
key out of `/proc/<pid>/{environ,mem}`, and its own rationale says the opt-in
override "gives the core dump **and the debugger** back" — and the `include/`
header comments (`include/jc_proc.h:234,280,292`) discuss `ptrace_scope` and
"a debugger (ptrace)" in the context of **preventing** key reads. `src/main.c:12034-12040`
does the same. ptrace/debugging appears in jichi's records exclusively as
*something to defend against*, never as *something to offer*.

### 5.7 The absence is unrecorded

Searches of `docs/DEFERRED.md`, `docs/ROADMAP.md`, `docs/DECISIONS.md` and
`docs/COMPARED.md` for any row offering/deferring/rejecting a runtime
debugger, step tool, breakpoint tool, backtrace tool, or debug adapter:
**no hits** (aside from prompt-cache breakpoints, §5.1). The M657/M658 audit
walks of the register — which *did* mechanically check 23 rows' factual
claims — had nothing to walk on this topic, because nothing was written.

## 6. Implications

Ordered roughly by severity, each tied to a stated project value:

1. **The fix-forward loop does not close for dynamic bugs.** The loop is
   read → edit → run → (structured) failure → fix. It closes when the failure
   is *observable in a batch artifact* (test output, sanitizer dump, compiler
   error). It does not close when the answer is in live state: wrong values
   mid-execution, hangs, races, order-dependent branches, and anything a
   passing test suite doesn't pin down. For those, the model falls back to
   static reasoning and printf-bisection — which is exactly the
   *"measure rather than assume — a number you have not checked is a guess
   wearing a number's clothes"* doctrine, broken.

2. **The safety machinery is blind to the one capability most likely to be
   unbounded.** A `gdb` attach, a live `call foo(...)`, or a multi-GB core
   dump are, to jichi's nine-step tool-decision chain (`docs/TOOL_DECISIONS.md`),
   indistinguishable from "a shell command ran." The privileged gate, kinetic
   gate, edit scope, hooks, and the M831 Landlock write fence all reason about
   *commands and file writes*; none can reason about *debugger semantics*. The
   fences that are jichi's strongest differentiator do not apply, in
   debugger-aware form, to the debugger.

3. **Crashes are the least explained of the agent's failures**, and they are
   the failures a coding agent most needs explained. Every other failure class
   has a structured surface: tests (`run_tests` parses them), build errors
   (LSP diagnostics), tool errors (detailed messages). A segfault of the
   *target program* is a wall.

4. **A brand asymmetry.** 自 (self-knowledge): jichi journals, times, and
   costs *itself* to token precision (the M897 first-token-time milestone
   included) while offering no equivalent of runtime self-knowledge to the
   programs it writes. It can state exactly what it spent; it cannot state
   why your program died.

5. **Workarounds already in the tree contradict the project's own
   instruction-design rationale** (§5.3): prose recipes steering the model
   into foreign REPLs are the weaker design the ghost-text work argued against.

6. **The meta-implication: the gap is invisible to the project's
   self-knowledge infrastructure.** A deferral register that records every
   other honest absence but not this one is not just incomplete — it means
   "lack" was being measured as "things we decided not to do," not "things
   a coding agent could be asked to do." That is a blind spot in the
   *method*, and it is worth more than any single feature: it will have
   produced other unrecorded absences nobody has noticed yet.

## 7. Recommended shape (if it is ever taken up)

Stated as a design sketch in the codebase's own idiom — not a commitment.
Jichi's own rules (M326b: check checkable parts before parking; M662: do not
recommend ahead of measurement) mean the right first artifact is *a row in
DEFERRED.md with these measured costs*, and the right second artifact is
whichever step below survives a measurement.

1. **Close the crash hole first — cheap, C89-safe, no live ptrace.**
   A `backtrace`/`crash_report` tool: given a binary (and a core, or a
   re-run), produce a *symbolized* frame list (`addr2line`/`eu-stack` — both
   ordinary userland utilities, so no kernel dependency, preserving the
   small-OS constraint) and present it with source context, exactly the way
   `run_tests` presents parsed test output. Gate any core-dump creation on an
   explicit opt-in, mirroring M770's `JICHI_KEEP_DUMPABLE` philosophy:
   protection is the default, the convenience asks.
2. **Then, if the measurement says so: a DAP-style debug-adapter
   configuration surface.** Generalize the existing `lspServers:
   [{name, command, extensions}]` shape (the subprocess machinery in
   `src/util/jc_proc.*` is already there) to `debuggers: [{name, command,
   language, transport}]`, exposing a small mostly-read tool set:
   `launch`/`attach`, `set_breakpoint`, `step over|into|out`,
   `inspect frame|local|expr`, `continue`. Wire it through the *existing*
   permission chain like the LSP and git tools (inspection read-only;
   `attach` and `continue` mutating) so the nine-step chain — rather than
   the shell — is what reasons about it.
3. **Document the boundary either way.** A short `docs/DEBUGGING.md` (or at
   minimum the DEFERRED row) stating what jichi can and cannot observe at
   runtime, and why — because per the project's own M657 finding, a gap a
   reader discovers by grep instead of by reading is a defect in the record.

Rejected, with reasons, in the spirit of the register:

- *Embedding a full debugger in the binary* — violates the "libcurl and
  nothing else" dependency doctrine and the small-footprint rows; the
  adapter/external-tool shape is the only fit.
- *Waiting on MCP alone* — see §8, counterargument 2.
- *printf-instrumentation guidance as the answer* — that is what the
  `debugger.md` persona already reduces to, and §5.3 shows the project
  already does worse than acknowledge it when it works around it.

## 8. Limitations and honest counterarguments

This section is required by the document's own rules; it is the part a
reviewer should read first.

1. **Absence-based argument.** The core claim is that a capability does not
   exist. The survey was: the 46 lint-verified tool names (`JC_ALL_TOOL_NAMES`,
   read from the table, not grepped from assumptions — see the §5.1
   correction, which is how a first-pass undercount was caught); `src/` +
   `include/` grep for the debugger machinery vocabulary (ptrace, DAP, gdb,
   lldb, breakpoint, watchpoint, stack frame, SIGSEGV, backtrace, core dump);
   reads of DEFERRED, DECISIONS, COMPARED, TOOL_DECISIONS, and the scaffold
   contents. A capability implemented under vocabulary none of those greps
   would match (e.g., a DAP client written with different identifiers) would
   have evaded the survey. **Probability assessed low, not zero** — the tool
   registry is the single choke point every jichi capability flows through,
   and it was enumerated, not grepped.
2. **The MCP counterargument is real and not dismissed here.** A user can
   point any DAP-MCP server at jichi's existing MCP client, and the model
   *could* then debug. This does not retire the gap, for three measured
   reasons: (a) nothing in the shipped tree bundles, references, or teaches
   this (no MCP debugging server in `examples/`, no mention in `docs/MCP.md`
   or the skills — the survey would have found it, and the `guile` skill's
   *prose REPL recipe* is what it found instead, which is evidence the path
   is not in common use); (b) MCP tools get generic `<name>__<tool>`
   permission handling — the debugger-specific semantics of §6.2 still
   belong to the shell-blind gates; (c) it is a per-user, per-language
   improvisation, which is exactly the category (instruction-a-model-may-
   ignore) the project argued is the weaker design. The honest version of
   the gap statement is: *no debugging surface is provided, integrated, or
   taught as jichi's* — which is what §3 says.
3. **Scope counterargument: "a CLI agent should not ship an IDE feature."**
   Fair, and partially conceded. The strongest version of this analysis
   therefore rests on the *weak* half of §3 (crash information is a first-
   class failure a coding agent must explain, and jichi's own scaffolded
   debugger persona has no tool for it) plus the *meta* finding (§6.6:
   unrecorded absence in a record-obsessed project), not on "jichi must have
   gdb." If the operator agrees the surface belongs in the IDE via ACP
   instead, the crash-hole step (§7.1) and the DEFERRED row (§7.3) stand on
   their own.
4. **Not executed.** No jichi build was run, no segfaulting program was
   driven through jichi, and no DAP server was attempted against it. The
   crash-path claim (§5.5) is source-absence-based and flagged as inference
   there.
5. **Tree vintage.** Evidence is from the tree at HEAD `1b71247d`
   (2026-10-04/05 era, milestone M898+). If a debugging row landed in
   DEFERRED.md after this commit, §5.7 would be stale; it was verified at
   the time of analysis.
6. **"Definitely lacking" is graded, not absolute.** Other honest absences
   exist (schema-constrained output, deterministic crash *replay*, ACP
   client mode — all acknowledged in COMPARED.md, all defensible as scope
   calls). This feature's claim to be *the* answer is not that it is worse
   than those (they are smaller and recorded) but that it is the largest
   *unacknowledged* one, sitting at the intersection of the agent loop, the
   safety model, and the teaching layer simultaneously — and that in a
   project whose method is to record absences, an unrecorded absence is the
   strongest defect class its own documentation names.

## 9. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

```sh
# the 46 built-in tool names, from the lint-verified table (authoritative)
awk '/static const char \*const JC_ALL_TOOL_NAMES\[\] = \{/,/};/' src/tools/jc_tool.c \
  | grep -oE '"[a-z_]*"' | tr -d '"' | sort | tr '\n' ' ' ; echo

# none of the 46 matches any debug-related word (exit 1 = zero hits)
awk '/static const char \*const JC_ALL_TOOL_NAMES\[\] = \{/,/};/' src/tools/jc_tool.c \
  | grep -oE '"[a-z_]*"' | tr -d '"' \
  | grep -iE 'debug|attach|break|step|backtrace|inspect|core' ; echo "exit=$?"

# the lint that keeps the table honest (jichi's own M262 lesson)
cat tests/smoke/tool_names_lint.sh | head -3

# debugger machinery in code (expect: scaffold prose + security commentary only)
grep -rin "ptrace\|gdb\|lldb\|debugger\|attach\|core dump\|PR_SET_DUMPABLE" src/ include/

# the Guile prose-REPL recipe (the smoking gun)
sed -n '4699,4719p' src/scaffold/jc_scaffold.c

# the persona, the valgrind skill, the ASan skill
sed -n '145,171p'   src/scaffold/jc_scaffold.c
sed -n '832,841p'    src/scaffold/jc_scaffold.c
sed -n '4330,4339p'  src/scaffold/jc_scaffold.c

# the non-dumpable decision and its "and the debugger back" phrasing
sed -n '848,851p' docs/DECISIONS.md
grep -n "ptrace" include/jc_proc.h src/main.c

# the unrecorded-absence check
grep -rin "debug tool\|step tool\|breakpoint tool\|backtrace tool\|debug adapter\|DAP\|interactive debug" docs/DEFERRED.md docs/ROADMAP.md docs/DECISIONS.md docs/COMPARED.md
```

**Verdict line for the register this could seed:** *jichi offers no runtime
observation of programs it writes — no debug tool, no DAP surface, no
crash-to-frames path — and has no record of having noticed. Zero of the 46
lint-verified tool names (`JC_ALL_TOOL_NAMES`, src/tools/jc_tool.c) match any
debug-related word. Closest capabilities: LSP diagnostics and refactors
(static), `run_tests` (batch), valgrind/ASan triage skills (one-shot
post-mortem), and a `debugger.md` persona whose tools are all read-only.
Checkable claims above verified 2026-10-05 at `1b71247d`; the §5.1 tool
count corrected the same day after review (was 26, from an undercounting
grep; is 46, from the lint-verified table).*
