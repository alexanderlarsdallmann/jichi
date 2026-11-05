# jichi gap analysis — the repository map (data flow)

Read-only. All paths under `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`),
evidence cited, inference flagged, reproducible appendix.

## TL;DR

The repo map is the compact, authoritative index of the workspace's source
files and their top-level symbols, injected into the system prompt "so the
agent knows the layout up front, instead of grep-guessing" (`docs/REPOMAP.md:5-6`).
It is **built once at startup** (`src/main.c:3906`) and **never rebuilt during a
session**, and it carries **no staleness note to the model**. In a long session
— precisely the one compaction exists to keep alive — the map silently drifts
from the files it is telling the model to trust: a file created on turn 40
absent from it, a symbol renamed on turn 20 still shown under its old name.
The register *documents* the one-shot build to the human (REPOMAP.md:33-34,67
"Generated once at startup / Regenerated each startup") but has never (a) told
the *model* the map is a startup snapshot, or (b) tested that a mid-session
layout change is detectable.

## 1. What exists — and why it is a strength

- **Real, deterministic, bounded.** `jc_repomap_build` "walks the workspace and
  renders the bounded map section" (src/index/jc_repomap.c:8); top-level
  symbols are "scanning lines — no parsing, no LSP" (`:4`); over-size files
  and `node_modules`/`target`/`build`/`__pycache__`/`dist` are excluded, and a
  hit on the budget yields "… (map truncated; N more files)" (REPOMAP.md:22,33).
  Bounded + deterministic is exactly what a *cache prefix* must be.
- **The one-shot build is a deliberate, documented *feature*, not an accident.**
  REPOMAP.md:67: "**Regenerated each startup** — no on-disk cache yet"; and
  `main.c:3906` builds it once at startup into `app->repo_map`, which
  `jc_sysmsg.c:807` then appends verbatim every turn. Staticness here is the
  point: a per-turn-changing map would churn the prompt-cache prefix
  (PROMPT_CACHING.md:204 lists "a big prefix (rules, repo map, craft)" as
  billed once).
- **It is honestly scoped.** "This costs tokens every turn (bounded by the
  cap); disable with `repoMap`" (REPOMAP.md:43,43). The human operator knows
  it is a snapshot built once.

The net: the map is a sound, bounded, cache-friendly, *documented-as-snapshot*
index. The gap is that the snapshot property is documented to the *human*,
never to the *model*, and never pinned by a test.

## 2. The one thing definitely lacking

**A staleness signal the model can see, and a test that the one-shot map is
known to be one-shot.** Two concrete absences:

- **No staleness note in the injected text.** The map is appended by
  `append_capped(&sb, app->repo_map, map_cap, "repository map")`
  (`jc_sysmsg.c:807`) — a bare section. Verified: `grep -ni "stale|as of|
  snapshot|may not|rebuilt|refresh|out of date" src/index/jc_repomap.c` is
  empty. The model is told the map is "the structural overview" and nothing
  about it being *turn-0* truth. In a long session it cannot distinguish "not
  in the map" from "does not exist."
- **No test drives a mid-session layout change.** The map is built once and
  injected; I found no smoke test that starts a session, edits the tree after
  the map is built, and checks whether anything (a note, a re-inject, a
  documented expectation) reflects the drift. The register's own doctrine
  elsewhere (snapshot_lint.sh header: "prefer a lint to an audit") has no
  counterpart for the map.

## 3. Why "definitely lacking" (alternatives and their status)

- *"The human reads REPOMAP.md, so the snapshot property is documented."*
  The *model* does not read REPOMAP.md; it reads the injected section, which
  says nothing about time. The agent that "knows the layout up front"
  (REPOMAP.md:5) is the one *most* likely to trust a stale map, because it was
  told that is the layout. Documentation to the human is not documentation to
  the consumer.
- *"The map is deterministic and bounded, so it's fine to rebuild each turn —
  the gap is just that it isn't."* Rebuilding each turn would (a) cost the
  tokens the doc says it spends, and (b) — worse — **churn the prompt-cache
  prefix** on every file change, defeating the cache (PROMPT_CACHING.md:204).
  The one-shot build is the *right* call for caching; the gap is not that it
  is one-shot, it is that the one-shot is invisible to the model that consumes
  it. The fix is a note (or a cheap staleness flag), not a rebuild.
- *"Compaction re-injects fresh context, so the drift is bounded."* Compaction
  summarizes *messages*; it does not rebuild the repo map (the map is a system-
  prompt section, not a message — `jc_sysmsg.c:805-807`). A compacted long
  session has the *same turn-0 map* plus a summary of the changes made since,
  so the map and the summary can actively *disagree* (summary says "created
  foo.c", map says nothing) — the model must guess which is current.
- *"Symbol extraction is line-scanning (no LSP), so the map is already fuzzy."*
  True, and a separate (documented) limitation. The *staleness* axis — the map
  reflecting an older tree than the one on disk — is orthogonal: even a
  perfect LSP map built at startup is stale on turn 40.

## 4. Evidence

- **`src/main.c:3906`** — `app->repo_map = app->config.repo_map ?
  jc_repomap_build(app) : NULL;` — built **once at startup**. (Verified.)
- **`src/chat/jc_sysmsg.c:805-807`** — `if (app->repo_map != NULL && app->repo_map[0])
  { ... append_capped(&sb, app->repo_map, map_cap, "repository map"); }` — the
  *same* cached string appended every turn. (Verified.)
- **`grep -n "repo_map" src/chat/jc_agent.c`** — empty (verified): the agent
  loop never rebuilds it; there is no per-turn or post-edit refresh.
- **`src/index/jc_repomap.c:4,8`** — "top-level definitions … scanning lines,
  no parsing, no LSP"; "jc_repomap_build walks the workspace and renders the
  bounded map section." (Verified.)
- **`grep -ni "stale\|as of\|snapshot\|may not\|rebuilt\|refresh\|out of date"
  src/index/jc_repomap.c`** — empty (verified): the builder emits no staleness
  note.
- **`docs/REPOMAP.md:5-6,22,33-34,43,67`** — "knows the layout up front";
  excludes over-size/`node_modules` etc., "(map truncated; N more files)";
  "**Generated once at startup**"; "costs tokens every turn … disable with
  `repoMap`"; "**Regenerated each startup** — no on-disk cache yet." (Verified
  — the *human-facing* doc states the snapshot property; the model-facing
  section does not.)
- **`docs/PROMPT_CACHING.md:204`** — "a big prefix (rules, repo map, craft) …
  billed ~1.25x once" (verified: the one-shot build is the cache's friend, so
  the fix must not be a per-turn rebuild).
- **Tests** — no driver builds the map, mutates the tree, and checks
  staleness: `grep -rln "repomap\|repo map\|repoMap" tests/smoke/ | grep -iE
  "stale|refresh|drift"` → none; the map's only smoke coverage is the
  truncation and cost paths. (Verified.)

## 5. Implications

- **The "know the layout up front" claim ages into a lie.** REPOMAP.md's
  promise (":5-6") is true at turn 0. In a long session — the exact scenario
  jichi's compaction is built to sustain — the map is *older than the summary
  of work it accompanies*, and a model that "grep-guesses" less because it has
  the map will *miss files the map never showed* and *target symbols the map
  shows under a renamed name*. The map converts "not there" into "not in my
  layout" — a stronger, and wrong, conclusion, the same M483/M461 shape the
  register already names for `search_code` and `codebase_search` ("the tool did
  not fail, it lied quietly").
- **It compounds with the other analyses.** The repo map is a *cache prefix*
  (PROMPT_CACHING.md:204) and a *compaction survivor* (it is not a message, so
  COMPACTION's summary-and-discard leaves it at turn-0 truth). Both seams —
  cache and compaction — *depend on the map being static*, and both are the
  seams where its drift is worst. A one-shot static map is the right call for
  the cache and the wrong call for truth, and nothing says which to believe.
- **The model has no way to ask "is this current?"** There is no staleness
  flag, no build timestamp in the section, no "this reflects the tree as of
  session start" line. It must infer recency from the absence of evidence.

## 6. Recommendation

1. **One line in the injected map section.** After `append_capped(... "repository
   map")`, append a bounded note: "Built at session start; files created or
   renamed after that are not reflected here — re-list or search to confirm a
   path." This is the M387 "state-the-reach" move jichi already makes for the
   edit scope (jc_sysmsg.c:702-706: "the shell reaches past it and is only
   detected afterward") — the *same* pattern, applied to the map's time
   boundary. It costs a fixed few tokens (cache-stable, since it is constant
   per session), not a rebuild.
2. **A red-first staleness driver.** Start a session (map built), create
   `newfile.c` and rename a top-level symbol, and assert the model-facing
   section now *discloses* the snapshot (the note from step 1) — and, as the
   honest weaker check, that the raw map still shows the *old* state (pinning
   the drift so a future "rebuild each turn" change is caught as a deliberate
   behavior change, not a silent one).
3. **REPOMAP.md:33-34 already says "Generated once at startup" — extend it**
   to name the consequence: "and, until the note in step 1 ships, the model is
   not told that."

Rejected, in the register's spirit:

* *Rebuild the map every turn (or after every file edit).* Defeats the prompt
  cache (PROMPT_CACHING.md:204) for the one property the map exists to have
  (a stable, cheap, prefix-safe index). Staticness is correct; invisibility is
  the defect.
* *Treat the line-scan symbol extraction as the gap.* That is a separate,
  documented limitation (repomap.c:4 "no parsing, no LSP"); the staleness axis
  — a correct-but-old tree — is the one no doc or test addresses.

## 7. Limitations and honest counterarguments

1. **The map is only *layout* guidance, not the model's whole view.** The model
   also has `list_files`, `search_code`, and actual reads, which *are*
   current. The gap is that the *authoritative-looking* section (the one that
   "knows the layout up front") is stale, which biases the model away from the
   current tools. A strong model that always re-lists is largely unaffected;
   the risk concentrates in exactly the turns the map is meant to save.
2. **For short sessions the map is essentially fresh.** The drift is a
   *long-session* property — the sessions compaction sustains. In a 5-turn
   session the two agree. I scope the gap to the long-session case, which is
   the one the register cares about (it built compaction to make them
   indefinite).
3. **The one-shot build is documented to the human (REPOMAP.md:67).** This is
   not a hidden defect in the register's eyes — it is a *known* property that
   lacks the two things the register insists on elsewhere: a signal to the
   actual consumer (the model) and a test pinning the behavior. The
   "definitely lacking" thing is precisely those, not the awareness.
4. **I did not run a session long enough to observe the drift** (read-only).
   The "never rebuilt after startup" claim is verified by the single
   `repo_map =` assignment at main.c:3906 and the empty `jc_agent.c` result;
   the drift itself is the arithmetic of (built once) × (files change).

## 8. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

    # built once at startup, into a cached string
    grep -n "repo_map =" src/main.c                      # 3906: the only build
    grep -n "repo_map" src/chat/jc_agent.c               # (expect empty: no rebuild)

    # injected verbatim every turn, as a bare section
    sed -n '805,807p' src/chat/jc_sysmsg.c               # append_capped "repository map"

    # the builder emits no staleness note
    grep -ni "stale\|as of\|snapshot\|rebuilt\|refresh\|out of date" src/index/jc_repomap.c   # (expect empty)

    # the human-facing doc states the snapshot property
    sed -n '5,6p;33,34p;67p' docs/REPOMAP.md             # "Generated once at startup"; "Regenerated each startup"

    # the cache depends on its staticness (so the fix is a note, not a rebuild)
    sed -n '204p' docs/PROMPT_CACHING.md                 # "a big prefix (rules, repo map, craft)"

    # no test drives a mid-session layout change
    grep -rln "repoMap\|repomap\|repo map" tests/smoke/ | grep -iE "stale|refresh|drift"      # (expect none)

**Verdict line for the register this could seed:** jichi's repository map
(src/index/jc_repomap.c, docs/REPOMAP.md) is a bounded, deterministic,
line-scanned index of source files and top-level symbols, built once at
startup (main.c:3906) and injected verbatim every turn (jc_sysmsg.c:807) so
the agent "knows the layout up front, instead of grep-guessing" (REPOMAP.md:5).
The one-shot build is correct — it is what keeps the map a stable prompt-cache
prefix (PROMPT_CACHING.md:204) — and the register documents it to the human
("Generated once at startup"; "Regenerated each startup — no on-disk cache
yet", REPOMAP.md:33-34,67). What it never does is tell the *consumer*: the
injected section carries no staleness note (verified: no stale/as-of/snapshot
text in jc_repomap.c), no build timestamp, and no "this is the turn-0 tree"
disclosure, and no test starts a session, changes the tree, and checks that
the model's "layout" reflects it. In the long session compaction exists to
sustain, the map is *older than the summary it sits beside* and a model that
trusts "the layout" will miss post-startup files and target renamed symbols
under old names — the M483/M461 "did not fail, lied quietly" shape applied to
the one section that is framed as authoritative. Definitely lacking: (1) a
bounded, cache-stable note in the section ("Built at session start; re-list or
search to confirm a path") — the M387 state-the-reach pattern already used for
the edit scope; (2) a red-first driver pinning that the map is one-shot, so a
future "rebuild each turn" change is caught as a deliberate decision, not a
silent cache regression. Revisit when: the model is told the map is a snapshot
and a test proves the drift is disclosed, at which point "knows the layout up
front" is true for the session it was built for, and honestly scoped for the
rest.
