# JICHI HARDENING.md — gap analysis (analysis 49)

## TL;DR

*Read-only survey of docs/HARDENING.md at HEAD 1b71247d (877 lines), the
M130–M134 hardening pass + M300 + M693, cross-checked against
src/net/jc_http.c, src/chat/jc_memory.c, src/chat/jc_agent.c,
src/tools/jc_tool_parallel.c, src/util/jc_proc.c, and the ROADMAP,
2026-10-05.*

*HARDENING.md is a design-decisions document of exceptional quality: every
shipped claim I checked is true (M130 scrub + M472 close-fds + the M770
/proc self-leak, M131 SSRF loopback/link-local/re-resolved-IP blocking,
M132 0600 sinks, M133 the scope fence at every depth, M300 the untrusted
convention in the cached prefix), and the two recommendations that later
shipped (7.1 → M144, 7.2 → M159) carry in-section "built" notes in the
house style. The gap is the **section header**: "## 7. Recommendations
(designed, not yet built)" — true for 7.3/7.4/7.5, false for 7.1 and 7.2.
The section self-describes a state it no longer has; a reader who trusts
the header and skips the subsections sees two shipped features as
"not yet built."*

## 1. The shipped claims (all verified true)

The document's core value is its hardening record. Checked each shipped item
against code — **every one holds**:

- **M130 scrub + M472 close-fds + M770 self-leak.** Keys removed from every
  child's env (`jc_proc.c:112-115` unsetenv); the M472 close-fds backstop
  (`jc_spawn.c:133/137`) closes inherited descriptors; the fifth channel the
  doc names (the parent's own `/proc` entries) is fixed by `prctl`
  non-dumpability (`jc_proc.c:178-180`, M770; M771 adds the FreeBSD path). All present.
- **M131 SSRF.** `jc_http.c` blocks loopback (127/8, ::1), link-local
  (169.254, fe80), the resolved-IP recheck after DNS (line ~847), and
  `fence_only` for the web-search path. The doc's "resolves-then-rechecks"
  framing is accurate.
- **M132 0600 sinks.** `jc_make_private` (0600) on the control/on-disk
  sinks (`jc_control.c:126`), plus the M770 non-dumpable parent.
- **M133 scope fence at every depth.** `env_scope_fence` (jc_agent.c:756)
  returns `app->env != NULL && edit_scope.len > 0` — **no agent_depth gate**,
  exactly the fix the doc describes (the old bug was `agent_depth==0`).
- **M300 untrusted-content convention.** Established once in the cached
  prefix (`jc_sysmsg.c:646-650`, `jc_untrusted_prompt_rule()`), not
  re-stated per fetch — the doc's "one sentence in the prefix" design,
  as built.

So the hardening *record* is accurate and complete. This is not the surface's
weak point.

## 2. The recommendations that shipped (house style honored)

Three of the five §7 recommendations later built, and the doc updated them in
place in the project's established "Status" style:

- **7.1 Per-child verify gate → M144.** "Status: built as M144 ... exactly in
  the shape above (parallelVerify / --parallel-verify; quarantine + bounded
  verifier)." Verified in `jc_tool_parallel.c:2000,2557-2628` (n_quarantined,
  per-child `jc_env_run_verify`, red child quarantined not merged).
- **7.2 Mid-run control channel → M159/M162.** "Update (M159): built. This
  sketch became --control + the control subcommand." Shipped.
- **7.5 UX quick wins → M156 / M763 / built / built.** "Status re-checked at
  M300, because 'not yet built' had drifted. Three of the four had shipped and
  the heading still claimed otherwise — the same stale-list failure this
  project has now hit in the ROADMAP (M230/M267), the README table, and
  PHILOSOPHY.md's own check count. **A list of open items is a live claim.**"
  All four items struck-through and marked built.

The two still-open items (7.3 retrieval-ranked memory, 7.4 grading records)
remain accurately "not yet built" — I confirmed the memory store is still the
flat tail-bounded whole-file form (jc_memory.c, M825 names what it drops) and
no per-learner grading-record store exists.

## 3. The one thing the section gets wrong: its own header

The section heading reads **"## 7. Recommendations (designed, not yet
built)."** That is now true for only **2 of 5** subsections. It is false for
7.1, 7.2, and 7.5 — all three shipped and all three carry *in-subsection*
"built" notes that directly contradict the header a reader meets first.

The failure mode is precisely the one this project has now measured five times
(man-page ENVIRONMENT, reading anchors, plan Status headers, deferred
line-anchors, and TEST_INTEGRITY's rig_lint count): **a list whose heading is
a live claim about membership, updated nowhere but the leaves.** The doc's
*own* 7.5 note names this exact failure class in three other files — yet the
correction it applies (strike the leaf, note "built") never propagates to the
section header that still says "not yet built" for the whole group.

## 4. What it lacks: nothing mechanical needs to be built here

Unlike analyses 38–39 (plans, deferred — which have no lint at all), HARDENING
is a *design + recommendation* doc, not a live inventory; its shipped record
is a milestone history (ROADMAP's job), and its §7 recs are explicitly
"pick up as its own milestone." The header drift is a one-line prose fix, not
a missing guard. I did not find a defect class that warrants a new lint: the
recommendations are forward-looking by design, and the "built" notes are
already in place. So the honest recommendation is minimal.

## 5. The correction (one line)

Change the header from a categorical state to an honest one, e.g.:

  ## 7. Recommendations (designed in this pass; 7.1, 7.2, 7.5 since built —
  see each subsection's Status note; 7.3, 7.4 open)

or, matching the project's own 7.5 lesson, just drop the "(designed, not yet
built)" qualifier and let each subsection's Status note carry the truth — the
document already does that; only the header lags.

## 6. Verdict

HARDENING.md is **accurate and excellent** on everything it claims about what
it *built* — the five shipped hardening items all verify true against source,
including the subtle M770 /proc self-leak and the M133 every-depth fence. Its
single real gap is self-referential and minor: the §7 header "designed, not
yet built" is stale for 3 of its 5 subsections (all now built), a drift the
document itself documents having caught in four other files (M230/M267, README
table, PHILOSOPHY check count) and in its own 7.5. The fix is one line. No
code change, no new lint warranted — this is the register's M711-style
"the pointer resolved; only the header's claim rotted," and it rotted because
the correction habit lives in the leaves, not the branch.
