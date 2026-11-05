# jichi gap analysis — game development and its crafts (story, systems, graphics, music, deployment, business)

Read-only. All paths under `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`),
evidence cited, inference flagged, reproducible appendix.

## TL;DR

jichi treats game development as **one language among thirty** — a `godot` pack
(GDScript) alongside c-cli, zig-cli, python-cli, rust-cli, music, etc. — and its
entire philosophy for the craft is a *mechanical gate*: "did it work?" must be
answerable by an exit code, not by judgement. That philosophy is real and strong
where it applies: GAME_DEVELOPMENT.md (the self-learner course) shows a
`--headless --script` harness that *runs your code in the real engine* (a
213 ms `--check-only` gate), and the **music pack** ships a working `verify`
gate (`lilypond` engraving, exit 0) plus a `render_midi` tool. But the **godot
pack — the only game-engine pack — ships no `verify` gate and no
`config.example.json`**, and its own AGENTS.md tells the model to *"Test
in-editor; headless test setups vary by project"* (jc_scaffold.c:1112) —
**deferring the verification to the human** — even though a 2026-10-01 decision
(DECISIONS.md:2038) explicitly put "the `--check-only` gate and GDScript 4
guidance **in the godot pack by default**." That decision was committed to
*eight docs files and zero C files* (9375707a); `git log --all -S
CharacterBody2D -- src/` returns nothing, so at HEAD `1b71247d` (2026-10-04,
three days later) the pack still carries neither the gate nor the guidance it
was decided to have. The one thing definitely lacking: **jichi's game
support is the single place in the pack system where the "mechanical gate"
that is the entire point of the domain is absent, and where a *decided,
measured* fix to add it has not been applied.** For the other crafts (story,
level design, graphics, deployment, business), jichi is *honestly* out of
scope — no pack addresses them — but the godot pack is the one that *should*
have the runnable-gate foundation and does not.

## 1. What exists — and why the *model* is a genuine strength

- **A real domain philosophy, stated.** GAME_DEVELOPMENT.md:1-23 frames
  game dev as different from the reading courses precisely because "the engine
  runs **headless**, a script's exit code is yours to set, and a scene can be
  loaded and inspected without a window. So the question *'did it work?'*
  stops being a matter of judgement." The `--check-only` gate is measured at
  **213 ms** (GAME_DEVELOPMENT.md:39). This is the three-layer model applied
  to games: mechanical floor + agent + human.
- **A working course that runs the code.** Every command in GAME_DEVELOPMENT.md
  was run against Godot 4.7.2 (2026-09-21), and it teaches the *harness* —
  `$GODOT --headless --path … --script res://tests/harness.gd` (19-23) — i.e.
  the recipe for the gate the pack lacks.
- **The music pack *does* ship its gate.** `MUSIC_FILES` (jc_scaffold.c:4199)
  ships `config.example.json` (4201) with
  `"verify": "sh -c 'for f in *.ly; do lilypond --loglevel=ERROR $f || exit
  1; done'"` (4182) and a `render_midi` user tool (fluidsynth). Its header
  (3973-3976) states the three-layer model verbatim: "the mechanical floor is
  *'it engraves without errors'* (lilypond exit 0) — musical quality is the
  engraver agent + the human." So jichi *has* proven the pattern for a
  creative craft.
- **A measured LSP story for Godot.** The 2026-10-01 hand test (DECISIONS.md:2038)
  measured the headless editor serving GDScript diagnostics, the stdio bridge
  through `socat` (0.6 s) / `nc` (1.1 s), `unavailable in 11 ms` with no
  editor, and `--check-only` exit 1 in 213 ms. The numbers are in the tree.
- **30 packs, one for Godot.** The pack table (jc_scaffold.c:6108-6184) has a
  `godot` pack (6130-6132): "Godot game (GDScript): scene/signal discipline and
  a Godot reviewer."

## 2. The one thing definitely lacking

**The godot pack does not carry the mechanical gate that is the entire stated
point of jichi's game-development support — and a decided, measured fix to add
it is unapplied.** Three concrete, load-bearing facts:

1. **No `verify`, no `config.example.json`.** `GODOT_FILES` (jc_scaffold.c:2895-2916)
   ships **zero** `config.example.json` (verified: the only `config.example.json`
   entries are at 2822, 2849, 2873, 3023, 3050, 3077, 4201, … — none in the
   godot range). Every other runnable pack (C, Zig, Python, Rust, Go, web-ts,
   *music*) ships one with a test/verify command. The godot pack ships a
   `godot-reviewer.md` (read-only) but nothing that *executes* the code.
2. **The pack's AGENTS.md defers verification to the human.**
   jc_scaffold.c:1112: "Test in-editor; headless test setups vary by project."
   That is the exact inversion of the domain's stated philosophy ("*did it work*
   stops being a matter of judgement") and of the music pack's "lilypond exit 0"
   floor. A model given the godot pack has no `verify` to satisfy, so a
   `--auto` run has no gate, and the "test" is "go look in the editor."
3. **The fix was decided and measured, then never applied to the tree.**
   DECISIONS.md:2038 (2026-10-01): "**the `--check-only` gate and GDScript 4
   guidance** (signals, `CharacterBody2D`, `super()`) **in the godot pack by
   default**." The commit that recorded it, 9375707a, touched **8 files, all
   docs** (DECISIONS/DEFERRED/GAME_DEVELOPMENT/LSP + internal) and **no C**.
   `git log --all -S CharacterBody2D -- src/` is empty: the guidance and gate
   never entered `src/scaffold/jc_scaffold.c`. At HEAD `1b71247d` (2026-10-04),
   three days after the decision, the pack still lacks both. DEFERRED def-294
   (1432, 1721) still lists "the godot pack gives no parse gate … and no
   language-server example" under an **Open** section (1380) — i.e. the
   register itself still counts it as open even though a DECISIONS entry says
   it was "decided … in the pack by default."

These are not three separate gaps; they are one: **the pack is missing the
runnable gate, the doc that decided to add it is unimplemented, and the pack's
own rules actively tell the model to do without it.**

## 3. Why this is "definitely lacking" (alternatives and their status)

- *"The godot pack is *assets* — it just scaffolds conventions; the *gate*
  lives in the course (GAME_DEVELOPMENT.md)."* The course teaches the harness
  to a *self-learner*, but a pack is what `jichi init` drops into a *project*
  so a `--auto`/`test` run has a `verify`. The music pack proves the pack *can*
  carry the gate (4182). A project initialized from the godot pack has no
  `verify` in its config, so the autonomous/loop machinery — the whole reason
  a "gate" matters — is disarmed for the one engine pack. (The def-294 text
  "in the pack by default" is unambiguous about *where*.)
- *"GDScript has no standard test runner, so a `verify` is not possible."*
  The 213 ms `--check-only` *is* the standard runner for a single script, and
  the measured harness (`--headless --script`) is the runner for logic. The
  decision explicitly chose `--check-only` *because* it runs with no editor.
  The music pack's `lilypond exit 0` is the same shape. So the gate is not
  merely possible; it was *measured* possible on this machine.
- *"Maybe it's intentionally deferred to 'later' like the TCP transport."*
  def-295 (the TCP client transport) *is* honestly "later." def-294 (the gate
  + guidance) is *not*: DECISIONS.md:2038 records it as **decided and
  in-the-pack-by-default**, not deferred. The TCP part of the same decision is
  marked "later"; the gate part is marked "by default." Conflating them would
  excuse the gate; the text separates them.
- *"The other crafts (story, graphics, deployment, business) are the real gap."*
  For those, jichi is *out of scope by design*, and honestly so: no pack
  addresses narrative/level-design/graphics/Steam/monetization (verified:
  `grep -i` for those terms across `src/scaffold` finds nothing). That is a
  *scope* statement, not a *defect* — a coding agent is not a game-production
  studio, and claiming otherwise would be the dishonest move. The godot pack's
  missing gate is different: it is inside jichi's stated scope (the "did it
  work" mechanical floor) and its absence is *decided-to-be-fixed*. That is
  why the gate, not the crafts, is the "definitely lacking" item.
- *"It's only three days since the decision; the C change is in flight."*
  At a fixed HEAD, "in flight" is not a state — the tree either has it or
  does not, and `git log --all -S` across *all* refs shows it nowhere in
  `src/`. The analysis is about the committed state, which is the only state a
  user of `1b71247d` gets. (This is the honest, falsifiable form of the claim.)

## 4. Evidence

- **The godot pack ships no config/gate.**
  `sed -n '2895,2916p' src/scaffold/jc_scaffold.c` → `GODOT_FILES` has
  AGENTS.md, reviewer/godot-reviewer/test-writer/debugger/planner/docs-*,
  skills, commands, a11y — **no `config.example.json`**. `grep -c
  config.example.json` over that range = **0**. (Verified.)
- **The pack's AGENTS.md defers testing.**
  `src/scaffold/jc_scaffold.c:1112`:
  `"- Test in-editor; headless test setups vary by project.\n"`. (Verified.)
- **Every runnable peer pack ships a config with a command.** The
  `config.example.json` rows are at 2822 (C), 2849 (Zig), 2873 (Python),
  3023 (Rust), 3050 (Go), 3077 (web-ts), **4201 (music)**, and the C++/Perl/R/
  Guile/Racket/Clojure/Haskell/Elixir/Erlang/EmacsLisp packs. The music one is
  the on-craft contrast: `"verify": "sh -c 'for f in *.ly; do lilypond
  --loglevel=ERROR $f || exit 1; done'"` (4182) + a `render_midi` tool
  (fluidsynth). (Verified.)
- **The decision put the gate in the pack "by default."**
  `docs/DECISIONS.md:2038` (dec-2026-10-01-godot): "**the `--check-only` gate
  and GDScript 4 guidance** (signals, `CharacterBody2D`, `super()**) in the
  godot pack by default**" — with the 213 ms `--check-only` measurement and
  the "rejected: no language server at all / the bridge as a default / TCP now"
  reasoning. (Verified.)
- **The decision's commit touched no C.**
  `git show --stat 9375707a` → 8 files, all `docs/**` (DECISIONS.md,
  DEFERRED.md, GAME_DEVELOPMENT.md, LSP.md, three internal notes). **No
  `src/` file.** (Verified.)
- **The gate/guidance are absent from the source tree at HEAD.**
  `git log --all -S CharacterBody2D -- src/` → **empty**;
  `grep -rn "check-only\|CharacterBody2D\|GDScript 4" src/ --include=*.c` →
  **empty**. (Verified.)
- **The register still counts it open.** `docs/DEFERRED.md:1432` lists "The
  godot pack gives no parse gate and no GDScript 4 guidance, and no
  language-server example" (def-294, detail at 1721) under the **Open**
  section headed at 1380 ("## Open — found by the 2026-09-29 branch review").
  The def-294 "Why" (1723) itself says the pack "ships no `config.example.json`
  … although Godot 3 syntax is a model's usual mistake" and that it was
  "Decided on 2026-10-01 … the pack gains the `--check-only` gate … and
  GDScript 4 guidance" — i.e. the register holds *both* the open-finding and
  the decision to close it, with the C change in neither. (Verified.)
- **The course *has* the recipe the pack lacks.** `docs/GAME_DEVELOPMENT.md:19-23`
  (the `--headless --script` harness) and `:39` ("**213 ms**, measured. That is
  a real gate — the thing the reading tasks cannot do"). (Verified.)
- **The crafts have no pack.** `grep -rniE "narrative|level design|story
  board|gdd|texture|sprite|shader|steam|itch.io|store page|monetiz"
  src/scaffold/jc_scaffold.c` → no game-craft hits (the single "narrative" at
  529 is in a log-analysis skill about incident timelines, not a game).
  (Verified.)

## 5. Implications

- **The godot pack disarms jichi's core autonomy for games.** Without a
  `verify` in the scaffolded config, a `jichi setup --preset <godot>` project
  has no gate for `--auto`, `test`, the verify-every checkpoint, or the
  hollow-gate detector. The "did it work" question — the *entire stated
  reason* GAME_DEVELOPMENT.md exists — is answered by "go look in the editor"
  (the pack's own AGENTS.md), i.e. by the human, not by an exit code. The
  music pack shows the same agent doing exactly the right thing for a creative
  craft one line over.
- **The decision/register split is a *truthfulness* risk, not just a missing
  feature.** DEFERRED.md still says "no parse gate" (open); DECISIONS.md says
  "in the pack by default" (decided); the C says "no parse gate" (true at
  HEAD). A future reader who trusts DECISIONS.md will believe the pack has the
  gate and skip adding it; a reader who trusts the tree will see it missing.
  The two are reconcilable only by the uncommitted C change. This is the
  register's own standing hazard (a record that disagrees with the artifact)
  made concrete: the *prose* is ahead of the *code*.
- **Godot 3 vs 4 is the named failure mode, and the pack does not prevent it.**
  def-294 (1723) and D4 (scaffolding review, 304-305): "GDScript 4 guidance …
  although Godot 3 syntax is a model's usual mistake." A model scaffolding a
  Godot 4 project in GDScript 3 style (`yield` instead of `await`, no
  `CharacterBody2D`) passes the *absent* gate and produces a project that will
  not load — and nothing in the pack catches it, because there is nothing in
  the pack to run. The reviewer agent is read-only and cannot execute.
- **The crafts are out of scope — and that is fine, so long as it is said.**
  Story, level design, graphics, audio-asset pipeline, Steam/itch deployment,
  and business (monetization, pricing) have no pack and no course. jichi is a
  coding agent; it should not pretend to be a production studio. The honest
  position — and the one this analysis takes — is that the *only* craft jichi
  can genuinely serve (game *systems*: the code that runs in the engine) is
  the one currently missing its gate. The scope gap is a boundary to *state*;
  the godot gate gap is a defect to *fix*.
- **It composes with the loop seams.** The same "no verify ⇒ disarmed
  autonomy" applies to `spawn_parallel`'s per-child verify, `--verify-every`,
  and the doctor unattended gate — all of which need a `verify` to bite. A
  godot project gets none from its own scaffold.

## 6. Recommendation

1. **Apply the already-decided fix: put the gate and guidance in the godot
   pack.** Add to `GODOT_FILES` a `config.example.json` (as `MUSIC_FILES`
   does at 4201) with
   `"verify": "sh -c \"$GODOT_BIN --headless --path . --check-only --script
   <script>\""` (or the harness form), and add the GDScript 4 lines (signals,
   `CharacterBody2D`, `super()`) to `FILE_AGENTS_GODOT`. The recipe is
   *measured* (213 ms) and *written* (GAME_DEVELOPMENT.md:19-39); this is
   transcribing a decision into the pack, not inventing one.
2. **Reconcile DEFERRED and DECISIONS.** Either close def-294's "Why" to point
   at the commit that lands the C change, or — at a HEAD where the C is not
   yet in — keep def-294 *open* and soften DECISIONS.md:2038 from "in the pack
   by default" to "decided; pending the pack's C change" so the two records
   do not contradict. The tree is the ground truth the register defers to.
3. **A red-first driver.** Scaffold the godot pack; assert its
   `config.example.json` contains a `verify` (or `testCommand`) that runs
   `$GODOT --headless` — before the fix: red (there is no config); after:
   green. Mirror the music pack's assertion. This is the test the pack should
   have had when the decision was committed.
4. **One sentence in GAME_DEVELOPMENT.md / the pack** naming the *boundary*:
   story, level design, graphics, deployment, and business are out of scope
   for the agent (human + other tools); the *one* craft jichi serves — game
   systems code — is gated by the `--check-only`/harness verify. Stating the
   scope honestly is the register's own standard (the "What deliberately does
   NOT exist" section).

Rejected, in the register's spirit:

* *Claim the missing crafts are the gap.* The crafts are out of scope by
  design; a coding agent that pretended to do level design or Steam pricing
  would be the dishonest product. The defect is inside the one craft in scope.
* *Ship the LSP bridge as the default.* The decision itself rejected that
  (needs a running editor, ~750 MB, `socat`/`nc`); `--check-only` works
  everywhere the pack does. The gate, not the server, is the default.
* *Leave it "in flight."* At a fixed HEAD the C is either present or not; the
  honest record is to say it is not, and to close the loop with the commit
  that lands it.

## 7. Limitations and honest counterarguments

1. **This is a *pack-asset* gap, not an *engine* gap.** jichi does not need to
   ship Godot; it needs the pack to carry the command that *runs* Godot
   headless. The recipe is documented and measured; the omission is that the
   scaffolded project does not invoke it.
2. **`--check-only` is a *single-script* gate.** It does not load a scene or
   run a full build; the harness (`--script`) is the fuller gate, and the
   course uses both. I do not claim `--check-only` covers "the whole game"; I
   claim it is the *measured, decided, in-scope* floor the pack lacks.
3. **I have not run `jichi init godot` and inspected the config** (read-only,
   and it needs the C change anyway). The "no config in the pack" claim is
   verified by reading `GODOT_FILES` (no `config.example.json` row) and the
   `grep -c` = 0 over the table range — the source of truth for what `init`
   would emit.
4. **The decision is only three days old at HEAD.** I do not imply the C
   change is forgotten forever; I imply it is *not in this tree*, and the
   record (DEFERRED open + DECISIONS "in the pack by default") is currently
   self-contradictory. "Revisit when" the commit lands is the honest
   termination.
5. **The crafts-out-of-scope claim is a *scope* observation, not a defect.**
   I am explicit that it is the *stated boundary* that matters, and that the
   only in-scope craft (game systems) is the one with the missing gate.
   Framing it as "jichi can't do story" would be the strawman I am avoiding.

## 8. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

    # the godot pack's files (no config.example.json / no verify)
    sed -n '2895,2916p' src/scaffold/jc_scaffold.c
    sed -n '2895,2916p' src/scaffold/jc_scaffold.c | grep -c "config.example.json"   # 0

    # the pack defers testing to the human
    sed -n '1112p' src/scaffold/jc_scaffold.c        # "Test in-editor; headless …"

    # the music pack DOES ship its gate (the on-craft contrast)
    sed -n '4182p' src/scaffold/jc_scaffold.c        # "verify": lilypond … exit 1
    sed -n '4199,4201p' src/scaffold/jc_scaffold.c   # MUSIC_FILES + config.example.json

    # every runnable peer pack ships a config
    grep -n "config.example.json" src/scaffold/jc_scaffold.c | head

    # the decision put the gate "in the pack by default"
    sed -n '2038p' docs/DECISIONS.md                 # dec-2026-10-01-godot

    # the decision's commit touched docs only, no C
    git show --stat 9375707a | head

    # the gate/guidance are absent from the source tree at HEAD
    git log --all -S CharacterBody2D -- src/          # (expect none)
    grep -rn "check-only\|CharacterBody2D\|GDScript 4" src/ --include=*.c   # (none)

    # the register still counts it open, and names Godot 3 syntax as the risk
    sed -n '1432p;1721,1724p' docs/DEFERRED.md       # def-294
    sed -n '301,306p' docs/analysis/2026-09-29-scaffolding-packs-review.md   # D4

    # the course has the recipe the pack lacks (213 ms gate)
    sed -n '19,23p;39p' docs/GAME_DEVELOPMENT.md

    # the crafts have no pack (the one hit, "narrative" at 529, is the
    # log-analysis pack's incident-timeline skill, LOGS_FILES:2984 -- not a game)
    grep -rniE "narrative|level design|gdd|texture|sprite|shader|steam|itch.io|monetiz" src/scaffold/jc_scaffold.c

**Verdict line for the register this could seed:** jichi's game-development
support is real in its *model* — a mechanical gate, "did it work = exit code"
(GAME_DEVELOPMENT.md:1-39, a 213 ms `--check-only` gate) — and the **music
pack proves the pattern** by shipping a working `verify` (`lilypond`,
jc_scaffold.c:4182). But the **godot pack — the only game-engine pack — ships
no `config.example.json` and no `verify`** (GODOT_FILES, 2895-2916), and its
own AGENTS.md tells the model to *"Test in-editor; headless test setups vary
by project"* (1112), i.e. defers verification to the human — the exact
inversion of the stated philosophy. Worse, the fix is **already decided and
measured**: DECISIONS.md:2038 (dec-2026-10-01-godot) puts "the `--check-only`
gate and GDScript 4 guidance in the godot pack by default," and the commit
that recorded it (9375707a) touched **eight docs files and zero C files**;
`git log --all -S CharacterBody2D -- src/` is empty, so at HEAD `1b71247d`
(2026-10-04) the pack still has neither the gate nor the guidance it was
decided to carry, while DEFERRED.md:1432 (def-294, Open) still records
"no parse gate … no GDScript 4 guidance" — so the decision record and the
tree currently *contradict each other*. The named failure mode is uncaught:
Godot 3 syntax in a Godot 4 project (the model's usual mistake, def-294)
passes the *absent* gate and produces a project that will not load.
Definitely lacking: (1) the godot pack's `config.example.json` with the
measured `--check-only`/harness `verify` + the GDScript 4 lines — transcribing
the decision, not inventing it; (2) reconciling DEFERRED (open) with DECISIONS
("in the pack by default") so the record matches the artifact; (3) a red-first
driver asserting the scaffolded godot pack contains a `verify` that runs
`$GODOT --headless` (the music pack's assertion, mirrored). For the other
crafts — story, level design, graphics, deployment, business — jichi is
*honestly out of scope* (no pack addresses them, and it should not); the
finding is that the *one in-scope craft* (game systems code) is the one
missing its gate. Revisit when: the commit lands the C change, def-294 closes,
and the driver is green — at which point the godot pack matches the music
pack, and the record agrees with the tree.
