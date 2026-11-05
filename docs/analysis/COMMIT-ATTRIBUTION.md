# Commit attribution for agent-assisted work

jichi's log distinguishes *which agent did the work* — recent commits
from other agents end with e.g. `Co-Authored-By: Claude Opus 5.5
<noreply@anthropic.com>`. When **jichi** is the contributing agent, use
the same standard `Co-Authored-By` trailer **plus** two jichi-specific
metadata trailers, as a block after a blank line at the end of the
message:

    <subject, ≤72 chars, imperative, area prefix e.g. docs(analysis):>

    <body: what changed and why>

    Co-Authored-By: jichi 0.12.0 <maintainer@example.org>
    Jichi-Build: 1b71247d (master, 2026-10-04)
    Jichi-Model: jlu/qwen3.8-27b

## Who a commit is by

In this repository the author and the committer of every commit are the
maintainer; the trailer block, not the author line, says which agent did
the work. jichi has no address of its own, so its `Co-Authored-By` line
carries the maintainer's address too. This page shows
`<maintainer@example.org>` in its place; the real one is in
`docs/internal/COMMIT_IDENTITY.md`, which the published tree leaves out.
In your own project, it is yours.

Never write `noreply@jichi`, an address at `localhost`, the placeholder
from this page, or a respelling: agents copy the commits nearest to them,
and between 2026-10-05 and 2026-10-20 one wrong address was copied into
231 commits. A harness that runs agents sets the identity in their
environment (`GIT_AUTHOR_NAME`, `GIT_AUTHOR_EMAIL`, `GIT_COMMITTER_NAME`,
`GIT_COMMITTER_EMAIL`), not as a placeholder in an image.

## The three trailers, and how to fill them

| Trailer | Meaning | How to get the value |
|---|---|---|
| `Co-Authored-By` | Standard git/GitLab co-author. Names **jichi** (not the person who supervised) as a contributing agent, with the jichi version that produced the change. The address identifies the project maintainer: jichi's development questions are addressed to whoever supervises the agent that wrote the change. This page shows a placeholder — the trailer you commit keeps the maintainer's real address (see "Who a commit is by"); git metadata is not part of the publishable tree that `snapshot_lint` scans. | Version from `jichi --version` or `include/jc_version.h` (`JC_VERSION`). |
| `Jichi-Build` | Which jichi *build* did the work: the short commit hash of the jichi checkout that performed it, with its branch and date. This is the auditable link — the self-audit's 36 documents cite `1b71247d` throughout, and this trailer is what says "these were written by that build". | `git rev-parse --short HEAD` + branch + `git log -1 --format=%ad` in the jichi checkout that did the work. |
| `Jichi-Model` | The model that actually ran the session (provider/model as configured). Required so a later reader can attribute reasoning quality to the right model — the register's model-choice analyses (see `docs/analysis/`) are only meaningful with this recorded. | The session's model string, e.g. `jlu/qwen3.8-27b`. If the session switched models mid-run (routing/fallback), name the one that produced the committed change. |

## Rules

1. **Trailers last, after a blank line**, `Key: value` form — so
   `git interpret-trailers`, GitLab's MR view, and `git log --grep` all
   parse them. Body prose before the blank line never counts.
2. **All three whenever known.** Omit a line only if its value is
   genuinely unknowable (e.g. the build hash of a pre-git checkout);
   never omit `Co-Authored-By` — that is the one GitLab renders as a
   co-author and the one that keeps the agent distinction visible.
3. **One block per commit.** If jichi and a human both contributed the
   same commit, add the human's own `Co-Authored-By` line in the same
   block; both then show in the log.
4. **The trailers are facts, not claims.** They record what jichi, what
   build, and what model — they do not certify correctness. Review
   agent-produced commits like any other.

## Querying

    # all commits where jichi was the contributing agent
    git log --grep="Co-Authored-By: jichi" --oneline
    # what model produced a given range
    git log --format="%h %B" <range> | grep Jichi-Model
    # every build that has contributed, with dates
    git log --format="%h %ad %B" --date=short | grep Jichi-Build

## Worked example

The 36-document self-audit on branch
`analysis/2026-10-05-jichi-gap-audit` (commits `cc73a7cf`,
`5269ee34`, `ae24fe11`) uses this exact scheme:

    docs(analysis): 36 surface gap analyses (jichi self-audit, 2026-10-05)

    Read-only audit of jichi's own register, run by jichi as the
    contributing agent: …

    Co-Authored-By: jichi 0.12.0 <maintainer@example.org>
    Jichi-Build: 1b71247d (master, 2026-10-04)
    Jichi-Model: jlu/qwen3.8-27b

(Those three commits first carried `noreply@jichi`, and later commits
copied it. On 2026-10-20 the history was rewritten: since then they, and
every jichi-attributed commit after them, carry the maintainer's address,
under the commit IDs above. The placeholder stands in for the address
here, as in the template.)

The commit that added this note carries the same block, so the
convention is self-documenting: `git log -1 --format=%B 1e96e0d0`.
