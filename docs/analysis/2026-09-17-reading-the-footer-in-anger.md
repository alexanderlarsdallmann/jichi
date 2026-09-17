# Reading the reach footer and a PLAN.md in anger — one real task (M637)

*Companion: the task's result is [`2026-09-17-refute-ab.md`](2026-09-17-refute-ab.md).*

*The DEFERRED row said: one real task started in plan mode, `write_plan`
producing the file, the work run under `--auto`, the footer read as a user, and
one honest note on what it changed or did not. This is that note. The task was
real: build `tests/bench/refute_ab/refute_ab.py`, the harness for the
pre-registered refute A/B, which nothing else in the tree could run without.
Model: `jlu/qwen3-coder-next` on the HRZ gateway. Date: 2026-09-17.*

## 1. The plan-mode run

`./jichi --plan --no-session --model jlu/qwen3-coder-next -p "<the task>"`, headless,
from the repository root. The first attempt failed in one line — `provider
returned HTTP 401` — because the key file is sourced by an interactive shell and
my detached shell had not sourced it. The footer under that failed run read:

```
[jichi] checked: 0 tool calls, 0 errors
not checked: no envelope armed -- no verifier, no edit scope, no budget; nothing about this run's result was tested
```

That is correct and useless in equal measure: zero calls and zero errors was
true, and the reason the run produced nothing was on the line above it, from
the provider, not from anything the footer measures. **First observation: the
footer describes the record; it does not know the run failed.** A reader who
skims to the footer sees a clean run.

The second attempt ran 11 tool calls and wrote `.jichi/PLAN.md` with all five
sections: a claim, one rejected alternative (automated grep scoring, rejected
because the pre-registration requires a blind human reader), a falsifier that
names the three verification commands, not-goals that correctly exclude the
planting step, and five files under `## Touches`. The footer:

```
[jichi] checked: 11 tool calls, 4 errors
not checked: no envelope armed -- ...
```

The 4 errors, from the transcript: two `read_file` calls on files that did not
exist yet (`files.txt`, `planted.tsv` — the model probing before creating), and
two refusals by the plan-mode fence: `run_terminal_command ls -la ...` and
`run_in_background mkdir -p ...`. The model tried to create the harness
directory twice in a mode whose one permitted write is the plan. **Second
observation: the footer counts a fence doing its job and a model probing for
a file in the same number.** "4 errors" made me open the transcript, which is
what the count is for; but it cannot say that two of the four were the run
behaving correctly. A `refused` count beside `errors` would have said so.

## 2. The `--auto` run, and the footer

`./jichi --auto --no-session --model jlu/qwen3-coder-next --edit-scope "tests/bench/refute_ab/**"
--edit-scope ".gitignore" --max-tool-calls 80 --deadline 60m --strict-green --journal <path>
--verify "python3 -m py_compile ... && python3 ... --help >/dev/null && sh tests/smoke/priced_model_lint.sh >/dev/null"
-p "Carry out the plan in .jichi/PLAN.md. ..."`. Fences on (edit scope, tool-call
cap), no token budget, the deadline as headroom. It stopped on the tool-call fence
at 80 calls, after 7.9 million tokens of context churn, with the verifier passing
on the tree as it stood. The footer:

```
[jichi] checked: verify did not conclude · 80 tool calls, 23 errors · 0 test edits · plan: 3 of 5 predicted files touched · writes in scope
not checked: a shell command ran -- changes it made are not attributed to the run
[envelope] budget_exhausted (tokens 7,871,459, tool calls 80)
```

Read as a user, line by line, before opening anything else:

- **"verify did not conclude"** — the run ended on the fence before the verifier's
  turn, so no verdict; the envelope's warning above it said the verifier passes
  on the tree as it stands but that this is advisory. Right, and the distinction
  between "green" and "would be green" is one I would have collapsed without the
  footer saying so.
- **"80 tool calls, 23 errors"** — a 29% error rate. That number sent me to the
  transcript before I looked at a single file the run produced. 19 of the 23
  errors were `run_terminal_command`: the model wrote the Python file by
  `echo '...' >> refute_ab.py` and here-docs through the shell, dozens of lines
  at a time, and roughly a third of those appends failed on quoting. It did that
  because its first `write_file` failed — the target directory did not exist and
  the tool does not create it — and it never came back to `write_file` after
  `mkdir` succeeded through the shell. The footer could not say any of that; it
  said 23, and 23 was the right number to make me look.
- **"plan: 3 of 5 predicted files touched"** — false on its face: all five files
  the plan named exist. True in the record: two of them (`refute_ab.py`,
  `files.txt`) were written by shell commands, which the envelope does not
  attribute to the run. Which is exactly what the next line says.
- **"not checked: a shell command ran -- changes it made are not attributed to
  the run"** — this is the line that made the previous one make sense, and the
  two together are the most useful thing the footer did: a reader who saw only
  "3 of 5" would suspect the plan; the pair says the *instrument* went blind for
  the shell-written files. It also tells me the edit scope did not fence those
  writes — `--strict-green` and the scope watch the tool chokepoint, and a shell
  `echo >>` goes past both. That is a known limit (`AUTONOMY.md` says the shell is
  the hole in every fence); the footer put it under my nose at the moment it
  mattered.
- **"writes in scope"** — true of the writes it could see. Given the line above,
  I did not trust it further than that, and checked `git status`: nothing outside
  the two scopes was touched. The shell-written files were inside the scope too;
  the fence would not have known if they were not.

## 3. What the footer changed, and did not

**It changed the order in which I looked.** Without the footer I would have
opened the harness first, found it plausible (it compiles, its `--help` is
right, its shape follows the file it was told to copy) and only then discovered
that its jichi invocation was wrong. With the footer, "23 errors" and "3 of 5"
sent me to the transcript first, where the shell-append pattern was visible in
one screen, and the harness was read afterwards *knowing* it had been assembled
by fifty shell appends of which a third failed. The footer did not find the
harness's defects; it set my prior about how many to expect.

**It did not, and cannot, say whether the work is right.** The verifier I chose
— compile, `--help`, the priced-model lint — passed on a harness whose two
model-driving subcommands could not have run (`--workflow` is not an option
jichi has; the control arm had no instruction; the grading form had one row
for two arms; the score was a grep where the pre-registration says a person).
`checked: verify green` would have been true and the harness still unusable.
The footer states what was checked; the honesty of *that* rests entirely on
what I asked it to check, and I asked for too little. The reach footer's own
limit paragraph in the ROADMAP says this; it is a different thing to see it.

**Two things I would change, having read it in anger.**

1. A `refused` count beside `errors`, or a `fence:` clause. Four of the plan
   run's errors and several of the auto run's were the fences working; the
   number that is supposed to worry the reader should not include them.
2. The `plan:` clause should say *why* it is short when the shell ran. It does,
   across two lines; on the first reading I read the first line before the
   second and doubted the plan. "plan: 3 of 5 predicted files touched (2 more
   written by a shell command, not attributed)" would say it in one.

> **Made, M638 (2026-09-17), the same day.** Both: the footer now reads `23 errors (4
> refused by a fence)` — a counter incremented at every fence, the loop's denials and
> the tools' own policy refusals, never a grep over the denial strings — and the plan
> clause reads `plan: 3 of 5 predicted files touched (2 unaccounted for -- a shell
> command ran; its writes are not attributed)` when the shell ran and the plan is
> short. And the defect under the cascade in §2 was in the product, not the model:
> `jc_path_resolve` tolerated one missing path component, so a `write_file` into a
> directory that did not exist yet failed to resolve, the fence read the failure as
> "outside", and the model was told a falsehood it then routed around through the
> shell. The resolver now walks up to the deepest existing ancestor
> (`tests/smoke/pathfence_nested.sh`). "One run is a proposal" stood for the footer
> wording, which changed shape and not meaning; the fence refusing what the tool
> would have done was a bug, and a bug needs one witness.

**What stands.** The footer's central promise — that a reader is told what the
record did and did not check, with the tool-error count the model would not
have volunteered — held on the first real task it was read on. The plan
artifact held too: the plan the model wrote was a good one (its not-goals
correctly excluded the planting step; its falsifier named the three checks),
and the drift line, once the shell clause was read with it, was exact.

## 4. What was wrong with the harness the model wrote, and what I changed

The model built a harness of the right shape — four subcommands, sealed
mapping, a `.gitignore` line, a README — and got the parts a verifier cannot
see wrong:

| Defect | What it would have done |
|---|---|
| `jichi ... --workflow spec.json --output md` | there is no `--workflow` option and no `md` format; `workflow` is a subcommand. Both model-driving subcommands would have failed on their first run |
| the control arm's prompt was the bare report | the pre-registration fixes the words: "Review the following critically and list anything wrong:" — without them the control is not the control |
| the answer was read from a jsonl `done` event | `jichi workflow` prints the pipeline's result to stdout; the answer would always have been empty |
| one form entry per report | the question is which *arm* named the planted claim; one answer for two arms cannot say |
| `score` grepped the planted subject in the answer text | the pre-registration says a person reads the outputs blind; a grep counts a run that names the function while agreeing with the false claim as a hit |
| `planted.tsv` filled with twelve invented rows | the plan's own not-goals said planting is manual and comes after the reports exist; the rows described reports that did not exist ("plan_finalize() which was removed in M102" — there is no such function and no such milestone) |
| `files.txt` with a thirteenth line, `EOF 2>&1` | a here-doc terminator that leaked into the file through the shell append; the harness's own count check caught it on the first `reports` run |
| a README copied from `craft_ab`'s, describing a preflight and a truncation check this script does not have | prose about features that do not exist |

I kept the structure, the sealed-mapping design, the four subcommand names and
the `.gitignore` entry; rewrote the invocation, both arms, the form, the score
and the README; deleted the invented planted rows (kept beside the note as a
record); and removed the stray line. The corrected file's docstring says so, so
the provenance travels with it. The verifier I should have written: run each
subcommand against a scripted model (`tests/tools/mockmodel`) and check the
files it produces — which is what every graded task in the curriculum does, and
what I did not do for my own tool.

## 5. What was asked, answered

The DEFERRED row asked for one honest session note on what the footer changed, or did
not. It changed the order in which I read, and it made me distrust a passing verifier I
had under-specified; it did not, and could not, tell me the harness was wrong. The plan
artifact's drift line was exact once its shell clause was read with it, and would have
misled me for a moment on its own. Two small changes to the footer are proposed in §3;
neither is made here, because a proposal from one run is a proposal, and the point of the
row was to read, not to fix.
