# Landing `feature/hrz-model-info`: what each side must do before it merges

*Status: **coordination document**, written 2026-08-19 from master at `24e462c` (M488),
re-read against the branch at `9de442b` (7 commits). The branch is still in flight;
this exists so the collisions are settled on the branch, where they are cheap, rather
than during a merge, where they are not. It changes no code — it says what to change,
on which side, and why.*

*Two audiences. §1–§6 are for the agent working on the branch. §7 is what **master**
owes, including a defect the branch found in master's own tests.*

---

## 0. The situation

The branch left `9afd976` (M483). Master started **M484** from that same commit and
has shipped **M484–M488**, all pushed. The project's usual integration move is
therefore unavailable: of 948 commits there are three merge commits, **none since
2026-08-08**, and every other branch landed as a **fast-forward onto an unmoved
master**. That cannot happen now.

```
9afd976 (M483) ─┬─ M484 M485 M486 M487 M488 ── master (pushed, immovable)
                └─ cc10969 b8561f9 8e52fc7 1b4aa01 44352f2 f9bf9e0 9de442b
```

| | branch | master |
|---|---|---|
| commits | 7 | 5 milestones |
| files changed | 23 | 64 |
| ROADMAP / CHANGELOG entries | **none yet** | written |

**No milestone numbers are claimed on the branch** — the `M###` tokens in its commit
messages are references to existing milestones, which is correct usage. So there is
nothing to renumber: write the entries **after** rebasing, as **M489 onward**. Master
is deliberately not claiming M489.

**Only four files are touched by both sides**, and the largest of them stops being a
conflict once §1 is done:

| File | master | branch |
|---|---|---|
| `src/main.c` | M486 doctor predicate, M488 `--api-base` | `--api-base` (dup), `--context-length`, gateway reads, `print_help` |
| `tests/smoke/run.sh` | `+snapshot_lint`, `+docs_index_lint` | `+lite_context_cap` |
| `docs/DEFERRED.md` | M485 + M488 closures | `--api-base` row, new chmod row |
| `docs/PLATFORMS.md` | stale count 11,248 → 12,422 | the MSYS2 rows |

Everything else is disjoint. In particular **`tests/smoke/_smoke.sh` is branch-only** —
master did not touch the shared library — and there are **no `t_plan` collisions**:
master moved six drivers' plans (`snapshot_lint`, `portability_lint`,
`docs_counts_lint`, `posix_utils_lint`, `setup.sh`, `docs_index_lint`) and the branch
touched none of them.

---

## 1. Drop the duplicated `--api-base` — master shipped it at M488

`b8561f9` implements the same flag as M488 (`24e462c`), which is pushed. The two are
equivalent where they overlap, so do not try to reconcile them — but **the branch
covers three things master does not**, and those should survive:

| Part of `b8561f9` | Action |
|---|---|
| the `--api-base` arg-parser case, the args-struct field, `ans.api_base = …` | **drop** — M488 has all three, same shape |
| the not-a-TTY guidance rewording | **drop** — M488 rewrote the same block |
| **`man/jichi.1` (+5)** | **KEEP** — M488 never touched the man page |
| **the `print_help` text** | **KEEP** — M488 never touched it |
| **removing `--api-base` from `docs_flags.sh`'s `future` heredoc (−7)** | **KEEP** — that row says *"Move this line out when the flag ships"*, and M488 shipped it without removing it. This is the correct complement |
| `tests/smoke/setup_key_env.sh` (+46 of the +124) | **keep what is not already asserted.** M488 added three checks to `setup.sh`: the flag reaches the config, omitting it leaves the provider default, and the guidance names it. Two drivers covering one flag is fine; duplicated assertions are noise |
| `docs/DEFERRED.md` marking the row **DONE** | **drop** — M488 closed that same row with a longer note. Take master's |

Cheapest route: **rewrite `b8561f9` into a small commit** holding only the man page,
the help text and the `docs_flags.sh` cleanup. It then applies over M488 with no
conflict, and `src/main.c` stops being a hard conflict.

The other six commits do not collide functionally.

---

## 2. Rebase; do not merge

```sh
git fetch origin && git rebase origin/master
```

`CLAUDE.md` states *"history is linear on `master`"*, the last 260 commits hold that,
and a fourth merge commit would break `--first-parent` reading of everything since
M330. Commit messages survive a rebase, which matters here — the reasoning lives in
them.

`tests/smoke/run.sh` is the one mechanical trap: both sides edited the **same
~450-character driver line**. Hand-merge all three names, then `ls tests/smoke/*.sh`
and diff against it. `smoke_lint` check 11 catches a dropped driver, but only after
you have spent a run finding out.

---

## 3. The MSYS2 row meets M486, which did not exist when it was written

M486 rewrote how platform verdicts are policed, and the new row lands inside that
machinery.

1. **`tests/smoke/portability_lint.sh` check 7** decides which platforms to police by
   reading `PLATFORMS.md`'s *Verified / Partly verified* tables through a token list:
   `WSL Cygwin FreeBSD NetBSD OpenBSD Termux`. **Add `MSYS2`.** Without it, any page
   may go on calling MSYS2 never-compiled and nothing notices — the exact defect M486
   existed to fix, one platform later.
2. **`jc_platform_verified_row()`** (`src/platform/jc_platform_posix.c`, M488) lists
   the kernels `doctor` stays silent on: `Linux FreeBSD NetBSD OpenBSD` — **full-gate
   rows only**. Cygwin is Partly verified and deliberately absent, so **MSYS2 should
   be absent too**; `doctor` will warn there, which is correct. Check **7c** pins that
   array against the page in both directions, so changing one means changing both.

---

## 4. The gateway note: the lesson ships, the census does not

`docs/analysis/2026-08-19-gateway-published-context-windows.md` states **"353 models
on this gateway"**. **Remove the census.**

At **M485**, on the operator's instruction, the 2026-08-09 gateway findings report was
withheld: it was written *to* that gateway's administrators, it describes another
organisation's configuration, and those matters are under active correspondence with
them. The model census was scrubbed from `docs/ROADMAP.md` in the same pass; this note
reintroduces it in a new file.

What **stays**, and should — this is the M485 rule, not a request to prune:

- the gateway host `api.hrz.uni-giessen.de` (a wire value; the decision is *real host
  in the docs that measured it, placeholder in `examples/`*);
- `jlu/…` model ids;
- that `/v1/model/info` is a LiteLLM extension, and what it returns;
- every measurement about **jichi's** behaviour, including the 1.6 MB response size
  and the parse cost.

What goes: **counts of what the gateway hosts**, which vendor families it fronts, and
which of them a given key reaches.

Read `docs/analysis/2026-08-09-hrz-gateway-findings.md` before editing — it keeps the
*lessons* in full at the same path and states at the top why the report does not ship.
That is the shape to copy, and its lesson §1 is the same finding `cc10969` implements.

**No lint catches this.** `snapshot_lint` check 9 allowlists the gateway host, so the
file passes. Its checks cover identity and hosts, not disclosure judgement.

---

## 5. `smoke_can_fence_owner` is right, and it has a sibling master needs

`f9bf9e0` is the best thing on the branch. Replacing `[ "$(id -u)" = 0 ]` with *can
this host fence a directory against its owner* is the correct generalisation: "am I
root" was one instance of the real question, and MSYS2 is another. It is the M477
pid-1 shape again — a fact about the hosts the author ran on, written down as a fact
about POSIX.

Two notes for the branch:

- Keep it in `_smoke.sh`. Master did not touch that file, so it merges clean.
- The helper answers the **fixture** question: *can a 000 directory be made unreadable
  to its owner*. There is a second, different question — *does `chmod` persist at
  all* — and per `44352f2` MSYS2 answers them differently: mounted `acl`, modes are
  honoured but the owner still is not fenced. Do not merge the two probes. §7 is where
  the second one is owed.

---

## 6. Before pushing

```sh
sh tests/smoke/snapshot_lint.sh     # 11 checks over the publishable tree
```

Checks 5–8 are **allowlists**, and they exist for a branch developed on another
machine: a pasted path, transcript, log excerpt or `git show` header carrying that
machine's username, an email address, an ssh login naming a private address, or an `adb` serial.
Permitted `/home/` accounts are `u you me user users stud1 stud2 tierv bench USER`;
permitted emails are example/no-reply domains only. Fix the text — do not widen the
allowlist, or the matcher self-test fires instead. (Commit *authorship* is not
scanned and does not matter: the public snapshot starts from a fresh history.)

Two more a new file can trip:

- **`docs_index_lint`** (M487) — any new **top-level** `docs/*.md` must also appear in
  `docs/README.md`. The branch's new pages are under `docs/analysis/`, which is exempt
  — as is `docs/plans/`, which is why this page can name `--context-length` without
  tripping `docs_flags`.
- **`config_keys_lint`** — a new top-level config key must be documented where a
  reader reads. `cc10969` and `8e52fc7` are the likely sources.

Then the numbers, in this order, because they are resolved **positionally**:

1. **`docs/ROADMAP.md`** — append entries at EOF as **M489 onward, highest last**.
   `docs_counts_lint`, `changelog_coverage_lint` and `milestone_currency_lint` all
   read "newest" as `tail -1`, not as a numeric maximum, so the wrong order turns all
   three red at once and each points at this same file.
2. **The `Where we stand` blockquote** in `★ TODO` — write yours, demote M488's into
   the `Previously (M488):` chain. Keep the *Measured, not incremented* paragraph
   inside the `★ TODO` section: two nested `sed` ranges scrape it, and a reflow breaks
   the extraction rather than the assertion.
3. **`README.md`** — `latest milestone MNNN` must equal the ROADMAP's last heading
   exactly, and the three milestone **counts** must be within ±10 of it.
4. **`CHANGELOG.md`** — under `### Added` / `### Fixed` in `## [Unreleased]`. The
   file's highest `M###` must be **≤** the ROADMAP's last heading and within 10 of it.
   The CHANGELOG may never lead.

The gate. The tier is **fail-fast**, which after a rebase means one lint per run:

```sh
sh scripts/preflight.sh
make WERROR=1 test
JC_SMOKE_KEEP_GOING=1 make smoke      # see everything red at once
make smoke                             # then fail-fast, clean
make ci                                # alone, last (685 s on the master bench)
```

`JC_SMOKE_TIMEOUT_MULT` is a **ratio, not a constant** — quote the formula and the
denominator with any figure from your bench (`docs/SESSION_RUNBOOK.md` §5).

---

## 7. What master owes — including a defect this branch found

**M488 gave the unit suite its first file-mode assertions, and they are not portable.**
`tests/test_eventlog.c` is the only test in the whole suite that asserts a mode, and
all four assertions arrived at M488:

| line | assertion | on a host where `chmod` does not persist |
|---|---|---|
| 302 | pre-existing dir's mode is unchanged | **passes** (vacuously — nothing changed because nothing can) |
| 305 | the log file is `0600` | **fails** |
| 325 | a directory jichi created is `0700` | **fails** |

The branch's own measurement is what exposes this: `44352f2` records that MSYS2 ships
with modes off, and honours them only when mounted `acl`. So `make test` on MSYS2
would report two failures that say nothing about jichi.

**This is master's to fix, not the branch's** — do not fix it on the branch, or we
both will. Master will add the unit-side sibling of `smoke_can_fence_owner`: a probe
asking *does `chmod` persist here*, which is a **different question** from *does a 000
directory fence its owner*, and skip the two exposed assertions where it answers no.
There is no unit-side guard concept today, so it needs one.

Also on master, after the merge:

- **The pre-release documentation review** (`docs/DOC_REVIEW.md` §5 — all three of its
  re-run triggers currently fire) over the merged tree, plus the machine-checkable
  half of `docs/ACCESSIBILITY.md`. Both need the final tree, which is why they wait on
  the branch rather than the other way round. The branch's two analysis notes and the
  pages it touches are in scope as *"the newest material — most likely to be wrong,
  least likely to be read"*. Nothing is expected of the branch there; it is said so
  the review is not a surprise.
- **Nothing else changes on master while the branch is in flight.** No code, no docs,
  no renumbering. M489 is unclaimed and is the branch's.

---

## 8. The short version, for the branch

1. Rewrite `b8561f9` down to the man page, `print_help`, and the `docs_flags.sh`
   cleanup. Drop the flag itself — M488 shipped it.
2. `git rebase origin/master`. Four files conflict; three become trivial after (1).
3. Add `MSYS2` to `portability_lint` check 7's token list. Leave
   `jc_platform_verified_row()` alone — Partly verified rows do not belong in it.
4. Remove the model census from the new gateway note. Keep everything about jichi.
5. Leave `docs/DEFERRED.md`'s `--api-base` row to master; keep the new chmod row, and
   word it against post-M488 code (`jc_mkdir_p_private` is now the one directory-side
   caller of `jc_make_private`).
6. Do **not** fix `tests/test_eventlog.c` — §7, master's.
7. Number the entries **M489 onward, highest last**; bump both banners.
8. `snapshot_lint`, then the gate with `JC_SMOKE_KEEP_GOING=1` first.
