# The index that skipped quietly

**Date:** 2026-08-19 · **Milestone:** M483 · **Defect:** an unreadable directory
vanished from the codebase index, and `codebase_search` then answered *"No matching
code found"* for everything inside it. · **Found by:** following M482's `jc_list_dir`
split into its 27 callers.

---

## 0. Where it came from

M482 fixed a conflation in the session store — *the listing failed* and *you have no
sessions* had one message between them — and to do it, `jc_list_dir` learned to
distinguish `ENOENT` (absent) from `JC_ERR_IO` (present, unreadable). That left an
obvious question: **27 callers now have information none of them use.**

The first one read looked like this:

```c
if (jc_list_dir(dir, &names, a) != JC_OK) {
    jc_vec_free(&names);
    return JC_OK; /* unreadable dir: skip quietly */
}
```

The defect was written down as intent, in a comment, in the walk that builds the
codebase search index.

---

## 1. What it cost, measured

A workspace with two files in two directories, one of them `chmod 000`:

| | operator sees | exit |
|---|---|---|
| both readable | `Indexed 2 file(s), 2 chunk(s)` | 0 |
| one unreadable | `Indexed 1 file(s), 1 chunk(s)` | 0 |

Nothing else. Not a warning, not a count, not a different status. **A smaller number
is not evidence of anything** — it is exactly what a smaller workspace looks like.

And the half that matters more, because jichi is an agent: a `codebase_search` for a
symbol defined in that directory returned

    No matching code found in the index.

A model reads that as *"the code does not contain this"* and proceeds accordingly.
This is the same failure shape as the OpenBSD `search_code` defect at M461, which
`docs/ANECDOTES.md` records in the sharpest available terms: *the tool did not fail,
it **lied**, silently, on every call.* A wrong answer delivered confidently is worse
than an error, because an error gets handled.

**It needs no fault injection to reach.** A permissions mistake, a directory owned by
another user, an NFS hiccup, `EMFILE` — `chmod 000` is merely the cheapest
reproduction. That is why its test is an ordinary driver every platform runs, rather
than part of the `FAULT=1` tier whose absence from the gate (M482) started this.

---

## 2. The fix: count the holes, and tell both audiences

Three states, where the code had one:

| state | before | after |
|---|---|---|
| root cannot be listed | `Indexed 0 file(s)`, exit **0** | `error: index build failed`, exit 1 |
| a subdirectory cannot be read | silently absent | counted, and reported to both audiences |
| everything readable | normal | unchanged — **no warning at all** |

The root case is not a partial result: if the workspace itself cannot be enumerated
there is no index to speak of. A subtree is a *hole*, which is a real result that has
to carry a caveat. The walk now takes a depth and an accumulator, so it can tell
those apart instead of returning `JC_OK` for both.

The count is carried **on the index**, not only in the build stats, because the
audience that must be warned is not the audience that built it: the index is built
once per process and searched many times, and `jc_search_run` passed `NULL` for
stats. So `jc_index_unreadable_dirs()` exists, and both surfaces use it:

**The operator**, from `jichi index`:

    warning: 1 directory could not be read and is NOT indexed --
             searches over this index will silently miss what they contain

**The model**, inside the tool result — appended to hits *and* to the empty result,
since an incomplete ranking misleads exactly as much as an empty one:

    note: 1 directory under this workspace could not be read and is NOT in the
    index, so these results are incomplete -- absence here does not mean the
    code does not exist.

---

## 3. The test, and the typo that nearly made it worthless

`tests/smoke/index_coverage.sh`, five checks, all four new ones shown to fail against
the unfixed binary:

    not ok 2 - index reported nothing about an unreadable directory:
               out='Indexed 1 file(s), 1 chunk(s) [1 embedded, 0 reused]'
    not ok 4 - no coverage note in any request body
    not ok 5 - an unlistable root did not fail with a build error (rc=0):
               out='Indexed 0 file(s), 0 chunk(s) [0 embedded, 0 reused]'

Check 1 — a healthy workspace warns about *nothing* — passes in both builds, which is
the point of having it: a warning that always fires is noise, not a signal.

Two things went wrong writing it, both worth recording:

**The mock's rules matched the wrong requests.** `count 1` selects the first request,
and the first request is the *embeddings* call, not the chat call — so the tool rule
never fired and jichi reported "the model returned no tool call and no text while 18
tools were advertised". Rules here must select on content (`match "\"messages\""`,
and `"role":"tool"` to tell the two chat rounds apart), which is what the existing
drivers do.

**Check 5 passed for the wrong reason.** It drove the unlistable-root case with
`--cwd`, **which is not a jichi flag**. The binary answered `error: unknown option
'--cwd'`, exited non-zero, and the check went green while measuring its own typo. It
was caught because the flag looked unfamiliar and got checked against `--help` — not
by anything structural. The check now asserts a *reason* (`index build failed`) and
explicitly refuses `unknown option`, and it reaches the state with `chmod 100`
(traversable, not listable) from inside the directory, which is the honest way to
make a root unenumerable without also preventing the process from running there.

That is the third time in three milestones that a check written to catch a vacuity
defect was itself vacuous on the first attempt. The pattern is consistent enough to
state plainly: **assert on the reason, never on the status alone.**

---

## 4. What is still open

The other callers. `jc_list_dir` now distinguishes absent from unreadable, and these
still do not:

| caller | what an unreadable directory currently means |
|---|---|
| `jc_command.c` | "this project has no custom commands" |
| `jc_agentdef.c` | "no agent profiles" |
| `jc_output_style.c` | "no output styles" |
| `jc_skill.c` (via its own walk) | worth checking on the same grounds |
| `main.c` × 4, `jc_app.c`, `jc_convert_claude.c` | assorted asset and journal listings |

The asset cases are the interesting ones: a user deliberately configures a specialist
profile, a permissions problem hides it, jichi behaves as though it was never
configured, and `doctor` reports "0 project assets" without a hint as to why. Same
shape, lower blast radius than a search that lies — which is why the index went
first.

---

## 5. Lessons

1. **A defect can be written down as intent.** `/* unreadable dir: skip quietly */`
   is a decision nobody revisited, and the comment made it look considered.
2. **A smaller number is not a signal.** Any count that would also be produced by a
   smaller input cannot, by itself, tell you coverage was lost.
3. **Warn the audience that can act, not the one that happened to be there.** The
   build knew; the search is what a model reads. The count had to move onto the index
   to cross that gap.
4. **Say nothing when nothing is wrong.** The baseline check exists to keep the
   warning meaningful.
5. **Assert on the reason, never the status alone** — `--cwd` is not a flag, and a
   non-zero exit proved nothing.
