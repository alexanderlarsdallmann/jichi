# Four findings from this branch, three unfixed, and one merge that must not be automatic

*2026-09-22. Written because these exist nowhere else. Two are defects in files
another machine was editing all evening, deliberately **not** fixed here; one is a
contradiction between this branch and `master` that a careless merge would
resolve wrongly in either direction; the fourth was fixed here, and is recorded
because the reason it hid for 41 milestones is a property of the check. Nothing in
`src/` or `tests/` changes on this page.*

## Why a page instead of a fix

Six of the eight master commits before this was written touched the smoke tier
(`portability_lint.sh`, `run.sh`, `doctor.sh`, `daemon_auth.sh`,
`bool_dialect.sh`, `pathfence_dangling.sh` twice). Racing a shared gate file for
a two-line change is poor value and invites a conflict in the one place a
conflict is expensive. **Handing the finding over costs nothing and loses
nothing.**

## 1. `posix_utils_lint` check 18 matches a backslash and a bare `t`

The rule is:

```sh
grep -E -q '(^|[^A-Za-z_#"])(python3|nc|curl)([ \t]|$)' "$f"
```

The intent is *"the tool name followed by whitespace or end of line"* — an
invocation, as opposed to a mention inside an ERE alternation. But **`[ \t]` is a
POSIX bracket expression**, and inside one `\t` is not a tab: it is the two
characters **backslash** and **`t`**. So the set is {space, backslash, `t`}.

Measured, three inputs through the rule exactly as written:

| input | matches | should |
|---|---|---|
| `x curl\` | **yes** | no |
| `x curlt` | **yes** | no |
| `x curl.` | no | no |

**This is not theoretical — it fired.** Writing `portability_lint` check 19, the
natural pattern for the header is `'curl/curl\.h'`, and the backslash after
`curl` made the tier report the lint as *running* curl. The workaround in the
tree spells the dot `[.]` and says so in a comment; the rule itself is untouched.

A tab in an ERE bracket expression has to be a literal tab or `[:blank:]`. The
same file's other rules are worth a look for the same construct.

## 2. A cross-file line citation went stale

`tests/smoke/posix_utils_lint.sh:855` reads:

> *"RS="" is PARAGRAPH MODE, defined by POSIX, and is not flagged —
> `portability_lint.sh:518` relies on it and passes on illumos."*

`portability_lint.sh:518` is now inside M695's check 7c
(`jc_sys_verified holds '$_e'…`) — the +239 lines M695/M696 added above it moved
the paragraph-mode `awk` elsewhere. The claim is still *true*; its pointer is not.
Cheap to re-anchor while that file is open.

## 3. The merge that must not be automatic

**`master` and this branch state opposite things about the same sweep, in the
same register row, and both of them also carry something the other needs.**

M697 rewrote `DEFERRED.md`'s *"Record driven-ness ON the platform rows"* row and,
in the rewrite, says:

> *"What remains undriven: … and the emulated architectures. For the `qemu-user`
> sweep it is not merely undone but impossible as built. Those `qemu-user` rows
> link `HAVE_CURL=`, so there is no HTTP at all; driving them needs a different
> rig, not a longer run."*

This branch measured **19 of 19 runnable triples driven**, through
`tier-v-arch.sh --drive`, each reporting a nonce minted that second. So that
clause is false as of 2026-09-21.

**And this branch is not simply right.** It carries the *pre-M697* text of that
same row, which reads:

> *"The rest — OpenBSD, NetBSD, the Pis, Termux/proot, Guix, musl-static, the 14
> emulated architectures — have **never made a model call**."*

M697 corrected exactly that, noting the OpenBSD/NetBSD half *"was already wrong
when written: both were driven 2026-09-19."*

**So the resolution is neither `--ours` nor `--theirs`.** It must take M697's
corrections **and** replace the qemu clause with the measurement. Taking this
branch wholesale reintroduces a claim master already fixed; taking master
wholesale reinstates one this branch disproved.

### The same shape in `PLATFORMS.md`

M697 flipped **Windows + Cygwin** and **Windows + MSYS2** from `no` to `yes` in
the Driven register. This branch flips **s390x**, the **M469 sweep** and the
**M542 re-sweep**. Same table, different rows, no textual overlap — but
`portability_lint` **check 17** asserts the Driven register's row count equals the
platform tables' row count, so a merge that drops or duplicates a row fails there
rather than silently. **Run `portability_lint` after resolving**; it is the
instrument that catches this exact mistake.

## 4. `figures-behind: 0` did not mean the translation was current

Found while re-counting `PROJECT_TIMELINE.md`, and **fixed in the tree** rather
than handed over — it is recorded here because the *reason* it went unnoticed is a
property of the check, not of the page.

`i18n_tracks_lint` check 5 asks: does this translation carry a figure of three or
more digits that **does not appear anywhere in the English page**? If not, the
marker may read `figures-behind: 0`. The Japanese `PROJECT_TIMELINE.md` read `0`
while being **41 milestones behind** — its summary table still said `M1 – M645`,
`1,141` commits, `13,329` unit checks, `1,736` smoke checks, `466` English pages.

Every one of those survived in English, because the M686 revision's own recount
table **preserves superseded figures on purpose**:

| | M579 | M620 | M646 | … |
|---|--:|--:|--:|--|
| tests | ~83,500 | ~87,900 | **~93,700** | |
| unit checks | 12,960 | 13,177 | **13,329** | |
| smoke checks | — | 1,608 | **1,736** | |

So the page that documents its own history **keeps its translations' staleness
invisible**: the more carefully the English page records what a number used to be,
the longer a translation can carry that number and still pass. The marker only
moved when this recount removed 16 figures from English *entirely* — the pie
slices and two totals, which no history table quotes.

**The check is not wrong**; its question is "is this claim checkable against the
English page", and the answer was honestly yes. But `figures-behind: 0` reads as
"this translation is current", and on this page it did not mean that. A check that
compared the translation against the English page's **current** figures — the
summary table, not the whole document — would have said so 41 milestones earlier.
Left as an observation: narrowing the universe is a change to a shared gate file,
and the same reasoning as §"Why a page instead of a fix" applies.

## Also pending, and not a defect

- **Milestone numbers.** No branch cites an unshipped `Mnnn`, so
  `milestone_currency_lint` stays green and they can wait. ROADMAP and CHANGELOG
  entries are deliberately unwritten for six pieces of work.
- **Two doc branches will conflict with each other**, independently of master:
  `docs/grounded-discourse` and `docs/game-development` each bumped the task count
  88 → 89, and `stage_index_lint` uses an **exact** count, not a floor.
- **No anecdote numbers** were taken, because the other machine is writing
  anecdotes too and a number picked here collides on rebase.
