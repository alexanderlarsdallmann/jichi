# The second walk, and one sibling deliberately left alone

**Date:** 2026-08-19 · **Milestone:** M493 · **Defect:** `list_files` with a
`pattern` answered *"(no files match …)"* for a tree whose matches were all inside a
directory it could not read. · **Found by:** finishing the sweep M483 named.

---

## 0. Where it came from

M483 fixed the codebase index and named the remaining `jc_list_dir` callers that
still read *any* failure as *nothing here*. Re-reading that list against the merged
tree, two callers were on it that M483's own note had missed — both in
`src/tools/jc_tool_ls.c`, the `list_files` tool. That matters more than the ones I
had listed, for three reasons: `list_files` is in the **`core`** tool profile, its
result is read by a **model** rather than an operator, and since M324 the `glob`
alias resolves to exactly this code path — so it is what a model uses when it asks
*"find the files matching X"*.

---

## 1. The tool told the truth on one path and not the other

The two sites are not equivalent, and the asymmetry is the finding.

**The flat listing** (no pattern, `:182`) has always been right:

```c
if (jc_list_dir(path, &names, ...) != JC_OK) {
    tu_err(out, "error: could not list directory");
```

**The recursive walk** (`:83`) was not:

```c
if (jc_list_dir(dir, &names, ...) != JC_OK) {
    jc_vec_free(&names);
    return;              /* unreadable subdirectory: skip, don't fail */
}
```

The *skip* is correct — one unreadable subtree must not fail a whole recursive
listing, which is M483's root-versus-subtree distinction. The silence is not.

## 2. What made it worse than an incomplete list

The walk already had two notices, and they sit in an `else if` chain with the empty
case **first**:

```c
if (w.nresults == 0)   → "(no files match <pattern>)"
else if (w.truncated)  → "[... truncated at 1000 matches; narrow the pattern ...]"
else if (w.exhausted)  → "[... stopped after scanning 200000 entries ...]"
```

So the worst case was not a quietly short list. It was this, measured against the
unfixed binary with every match inside a `chmod 000` directory:

    (no files match **/beta*)

That is the *"these files do not exist"* answer given to a question whose true
answer was *"I could not look everywhere"* — the same shape as M483's `No matching
code found in the index.` and M461's `search_code` returning `(no matches)` on
OpenBSD, which `ANECDOTES.md` records as *the tool did not fail, it lied, silently,
on every call.*

And the file already knew the principle. The comment on the `exhausted` notice, from
M324:

> *Distinct from truncation on purpose: "I stopped looking" is not "there were too
> many", and a model told the wrong one will narrow a pattern that was already fine.*

"I could not read part of the tree" is a third distinct reason, and it was the one
missing.

---

## 3. The fix, and why the note composes instead of joining the chain

`struct ls_walk` gains two fields — `unreadable` (a count) and `root_failed` (the
distinction) — and the render gains:

    [... 1 directory under this path could not be READ and was not searched, so
    the results above are incomplete -- absence here does not mean the file does
    not exist ...]

**Appended, not chained.** The three existing notices are mutually exclusive because
they are competing explanations of one result: exactly one of *nothing matched*,
*too many matched*, *I stopped early* is the reason you got what you got. A hole in
the tree is not a competing explanation — it can be true alongside any of them — and
the pair that matters most is precisely the one an `else if` would have swallowed:
**zero matches AND a hole.** That pair is check 3 of the driver.

The root case now matches the flat branch: an unlistable directory the caller
*named* is `error: could not list directory`, not an empty match list.

**Proven two-sided.** Against the unfixed binary, with the fixture in place:

    ok 1 - a readable tree matches both files and carries no coverage note
    not ok 2 - no note beside a partial result: open/alpha.c
    not ok 3 - a no-match answer with an unreadable subtree carried no note --
               a model cannot tell this from "the file does not exist":
               (no files match **/beta*)
    not ok 4 - an unlistable root did not error (it used to answer an empty
               match list): (no files match **/*.c)

Check 1 passes in **both** builds, which is deliberate: a note that fires on a
healthy tree is noise, and the baseline is what keeps it meaningful.

The driver gates on `smoke_can_fence_owner` (M491) rather than `id -u`, because
"can this host make a path unreadable to its owner" is the real precondition —
root ignores modes, and on Windows the owner keeps access regardless.

---

## 4. The sibling I did not fix, and why

`src/index/jc_repomap.c:624` has the identical shape — `rm_walk` returns silently on
an unreadable directory, so the repository map injected into the system prompt omits
that subtree. I recommended fixing it, read it, and decided against. Recording the
reason, because a recommendation withdrawn on inspection is worth more than one
carried out on momentum:

1. **The map already carries the caveat.** Its own header, every session:
   *"It is heuristic and may be incomplete; use read_file / find_definition /
   search_code for detail."* A reader is already told not to treat it as
   exhaustive — which is exactly what the `list_files` result had no way of saying.
2. **It already truncates for an unrelated reason.** The map is bounded to 12 KB by
   `repoMapLimit` and prints `... (truncated; N more files)`. Omission is a normal,
   announced state there; in a `list_files` answer it was not.
3. **It costs cached-prefix bytes.** The map lives in the M31 cached system prefix
   and is further capped by `jc_sysmsg_fit_caps`. Adding a line to a section that
   already says "may be incomplete" spends budget without changing what a reader
   should conclude.

The map is built **once at startup** into `app->repo_map`, so a note there would not
have been a per-turn statistic — the M440 hazard does not apply, and that is not the
reason. The reason is that the caveat is already true and already present.

**What that leaves open**, unchanged from M483's list: the asset listings
(`jc_command.c`, `jc_agentdef.c`, `jc_output_style.c`, `jc_skill.c`) where an
unreadable directory reads as *"you configured none of these"*, and assorted
listings in `main.c`, `jc_app.c` and `jc_convert_claude.c`.

---

## 5. A note on how this was caught before commit

`snapshot_lint` check 12 — added at M492, hours earlier — failed the tier while this
work was in progress:

    not ok 12 - untracked non-ignored file(s) -- this run measured a tree WITHOUT
                them, so a green result says nothing about what you are about to commit

The untracked file was this milestone's own new driver. That is the check working as
designed on the first outside occasion it had: M492 built it after pushing a red
master because every check it ran had been blind to a file the index could not see.
The correct response is to stage the work and re-run, which is what happened.

---

## 6. Lessons

1. **Finish the sweep, and re-derive the list rather than trusting the old one.**
   M483 named the remaining callers and missed the two that mattered most; the grep
   was cheap to re-run and the ranking changed completely.
2. **An `else if` chain of explanations hides the combination.** Ask of any such
   chain: which two of these can be true at once, and which pair is the dangerous
   one?
3. **A tool that is right on one code path and wrong on another is worse than one
   that is wrong everywhere** — the correct branch is the evidence that the author
   knew what the answer should be.
4. **A recommendation is not a commitment.** The repo map looked identical from the
   grep and was not, because the caveat it needed was already in its own header.
