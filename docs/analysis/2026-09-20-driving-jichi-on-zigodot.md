# Driving jichi on zigodot — seams found, and what each one measures

*2026-09-20. Eight headless runs against a real second project, with a free
`jlu/*` model on the institution's gateway. Every claim below is a measurement
from those runs, and every command is quoted in the form it was run.*

**The subject.** `zigodot` is a from-scratch re-implementation of the Godot
engine and GDScript in Zig: 81 `.zig` files, 202 `test` blocks, its own
`AGENTS.md`, its own `jichi` config. It is the third project jichi has been
driven against, and the first where the task was *use it*, not *gate it*.

> ## CORRECTION (2026-09-21) — this session surveyed the wrong checkout
>
> **Everything in this document that describes *zigodot as a project* is wrong.**
> There were two checkouts on this machine sharing a root commit:
> `/home/<this-account>/development/zigodot`, abandoned 2026-06-30 at 69 commits with no
> remote, and the live `/home/<this-account>/development/journey/zigodot`, 416 commits with
> an `origin`. The abandoned one sits one directory level shallower, so a glob
> reaches it first. Every run in this document went to it.
>
> Measured against the **live** repository on 2026-09-21:
>
> | Claim in this document | Actually |
> |---|---|
> | 81 `.zig` files | **109** |
> | 202 `test` blocks, the gate runs **29** | **558** blocks, the gate runs **510** (91%) |
> | `AGENTS.md` points under a *different account* | points at `/home/<this-account>/development/godotengine/godot` — **this** account, and the path exists |
> | retired model ids in its config | the config was already correct |
>
> **What survives.** The four seams are properties of *jichi*, not of zigodot:
> `doctor` validating an endpoint rather than a model id, the live probe
> discarding the server's own diagnosis, the envelope unable to tell a reading
> shell command from a writing one, and the hollow-gate detector silent without a
> parseable count. All four were real, all four are now closed (M689, M692). What
> does not survive is every number and every judgement about the project being
> driven — including the sentence above this box promising that "every claim below
> is a measurement", which was true and beside the point.
>
> **The lesson is not "check your paths".** It is that a measurement can be
> perfectly executed against the wrong subject and carry no internal sign of it:
> the tree built, the gate ran, the tests passed, the agent behaved sensibly. The
> cheap check that would have caught it is `git remote -v` and
> `git rev-list --count HEAD` before believing anything about a checkout — one
> command, and it distinguishes a live repository from an abandoned copy in a way
> that reading the source never will. The stale copy is now archived and deleted,
> with a marker file left where it stood. ANECDOTES #89's neighbour in kind.

**Why a second project is worth the trouble.** Everything in jichi's own tier is
tuned by people who know what it means. A stranger's repository supplies the one
thing a self-test cannot: configuration nobody checked, written months ago,
against a server that has since changed.

---

## 1. `doctor` says a model server is reachable without asking whether the model is there

**Measured.** A config naming `jlu/this-model-does-not-exist-9999`:

```
✓ model server reachable
    definitely-not-a-model: https://api.hrz.uni-giessen.de/v1
✓ the server publishes limits, but not for this model
    the endpoint answered but lists no limit for jlu/this-model-does-not-exist-9999
```

Both rows are green. The id is not in `/v1/models` — checked in the same minute,
376 ids, it is absent.

**This is not hypothetical, and that is the point.** zigodot's committed config
names **two** models that no longer exist on the gateway:

| config says | gateway has |
|---|---|
| `hosted_vllm/qwen3-coder-next` | `jlu/qwen3-coder-next` — no `hosted_vllm/` ids at all |
| `jlu/gemma-4-31b-it` | `jlu/gemma-4-26b-it` |

Every non-live `doctor` check passed against that config. The first and the
active one is the model with `roles: ["chat","edit","apply"]` — the one every
turn would use.

**Why it matters more than a typo.** `CLAUDE.md`'s spending rule is built on
checking an id against the free-namespace listing before a request. jichi
already **fetches that listing** — the very next row reads it for limits. The
data needed to say *"this id is not on the server"* is in hand and is used to
print `✓` instead.

**The second row is the near-miss.** *"the server publishes limits, but not for
this model"* is the only place the absence surfaces, and it is marked `✓` and
phrased as a property of the server. A reader scanning for `!` and `✗` sees
nothing.

## 2. The live probe throws away the server's own diagnosis

**Measured.** With the config as committed:

```
✗ --live: tool-calling probe failed
    jlu/qwen3-coder-next: the probe request did not complete (http error, HTTP error)
```

What the server actually said, to the same request:

```
HTTP 400
{"error":{"message":"litellm.BadRequestError: You passed in
 model=hosted_vllm/qwen3-coder-next. There are no healthy deployments for this
 model. Received Model Group=hosted_vllm/qwen3-coder-next ..."}}
```

The server named the problem, named the model, and gave a status code. jichi
reported `(http error, HTTP error)` — the same phrase twice, no status, no body.

**The cost is measured in what it takes to recover.** From jichi's message the
next move is to check the network, the key, the tunnel. From the server's
message the next move is to fix one string. The distinction between *"I could
not reach the server"* and *"the server rejected this model"* is the whole
diagnosis, and it was available and discarded.

## 3. The envelope cannot tell a reading shell command from a writing one

**Measured, both directions, same sentence.**

A run whose fourteen shell calls were `grep -rn`, `find`, `ls -la` and nothing
else:

```
[jichi] checked: verify green · 9 tool calls, 2 errors (0 refused by a fence)
not checked: a shell command ran -- changes it made are not attributed to the run
```

A run whose single shell call was `zig fmt src/gdscript/tokenizer.zig`:

```
not checked: a shell command ran -- changes it made are not attributed to the run
```

The second is correct and important — `zig fmt` rewrites the file, and the
envelope genuinely cannot attribute that. The first is a false alarm about
`ls`.

**Why this is the project's own doctrine pointed the other way.** `CLAUDE.md`
says: *"Classify an action by its EFFECT before choosing how to probe it"* — the
rule jichi learned after treating `export`, `rewind` and `undo` as one set. The
envelope applies the opposite rule to shell commands: one class, worst case
assumed. A warning that fires on every `ls` is a warning readers learn to skip,
and it is attached to the one sentence that should never be skipped.

**jichi is not short of the means.** It keeps a snapshot per snapshotted turn.
"A shell command ran and the tree is byte-identical afterwards" is a
*measurement* it could make, and it would turn a standing warning into a finding.

## 4. The hollow-gate detector needs a number, and a silent verifier gives it none

jichi has carried a hollow-green check since M86: *"verify passed but ran 0
tests -- is the gate wired?"*, plus a shrink detector and a
tests-not-wired detector. It reads a count out of the verifier's output.

zigodot's configured verifier is `zig build test`. **On success it prints
nothing at all.** No count, no summary, no lines.

**Measured, two-sided, from the run journals:**

| verify command | journal record |
|---|---|
| `zig build test` | no `tests` key — three consecutive runs |
| `zig build test --summary all` | `{"tests": 29}` |

So the detector is not wrong here, it is *blind*: with no parseable count it
cannot distinguish "ran zero tests" from "ran five hundred", and correctly says
nothing. Every run still printed `verify green`.

**And there was something to find.** Perturbing zigodot's own test suite:

- a **syntax error** in `src/core/string/string.zig` → `zig build test` exits **1**
  (so the file is genuinely compiled);
- `try std.testing.expect(false)` injected into the first `test` block of that
  same file → `zig build test` exits **0**, and `--summary all` reports
  `29/29 tests passed`.

The project has **202 `test` blocks**; the gate runs **29**. Five subsystems are
not imported from `src/root.zig` at all and so are never reached:

| subsystem | `test` blocks | reached by the gate |
|---|---|---|
| core | 90 | partly |
| editor | 32 | **no** |
| gdscript | 30 | partly |
| physics | 14 | **no** |
| profiler | 13 | **no** |
| shader | 10 | **no** |
| platform | 8 | **no** |

The physics subsystem was added in the repository's second-most-recent commit.
Its fourteen tests have never run in a gate.

**Whose defect is this?** zigodot's, not jichi's — jichi ran the command it was
given and reported its exit code, which is the only honest thing it can do. The
**seam** is that jichi's one instrument for noticing exactly this class was
disarmed by a verifier that prints nothing, and nothing said so. A single line —
*"verify passed but printed no test count, so the hollow-gate check could not
run"* — would have pointed straight at it.

**Actionable, for zigodot:** set `verify` and `testCommand` to
`zig build test --summary all`. Measured above: jichi then records the count and
the M86 machinery arms itself.

## 5. What held: the edit fence, two-sided

Both halves, same task, same model, same file:

| `--edit-scope` | result | tree |
|---|---|---|
| `docs/**` | `2 tool calls, 1 error (1 refused by a fence)` | unchanged, `git status` clean |
| `src/**` | `4 tool calls, 0 errors` | the comment landed on the right line |

The model's own account of the refusal was accurate: it named the tool, the
path and the scope. A fence that is merely enforced leaves the agent thrashing;
this one is enforced *and* legible.

## 6. What held: a stale rules file produced an honest answer, not an invented one

`AGENTS.md` sends the agent to the upstream Godot source at an absolute path
under `/home/<some-other-account>/development/miscellaneous/godotengine/godot`.

That path does not exist on this machine: the account differs, and so does
everything after it — the tree is actually at
`/home/<this-account>/development/godotengine/godot`. The rules file has another
machine's home directory baked into it.

*(The literal paths are not quoted here. `snapshot_lint` check 9 refuses a real
account name anywhere in the publishable tree, and it flagged the first draft of
this very paragraph — the check repaired three sections above, working on the
document describing the repair.)*

Asked to read the reference, jichi spent nine tool calls, took two errors, and
answered:

> The Godot C++ reference directory `…` does not exist or is inaccessible. I
> cannot read the reference because the specified path is not accessible — `ls`
> reports "No such file or directory" for that path.

This is the behaviour worth having: no invented function name, no quiet
substitution of a plausible file. It is recorded here because a negative result
that a system handles well is evidence too, and because the failure mode it
avoided — a confident answer about a file nobody can open — is the one that
costs most.

**Actionable, for zigodot:** `AGENTS.md` names an absolute path under another
account. Rules files are what steers the agent; this one steers it at nothing.

---

## What this cost, and what it did not test

Eight headless runs, `jlu/qwen3-coder-next` (free namespace, checked against
`/v1/models` before the first request), 2–7 s each, 27k–113k tokens per run.

**Not tested:** the TUI; any priced model; multi-turn sessions (`--no-session`
throughout); subagents; MCP; the LSP tools, which zigodot's config declares but
which need a Zig language server this run did not start. **One model, one
gateway** — every claim about tool-calling behaviour is about
`jlu/qwen3-coder-next` and nothing else.

**Not a controlled comparison.** These are eight runs, not a benchmark. Where a
number appears it is what one run printed.
