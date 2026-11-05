# The scaffolding packs, reviewed against the current code — findings and recommendations

*An analysis, 2026-09-29, for the maintainers and for anyone deciding how far to trust a pack. It
reviews the 33 packs `jichi init` writes, their documentation, and the setup presets and domain
benches built on them. The yardstick is master `c2f407b5` (M842). The method follows
[`DOC_REVIEW.md`](../DOC_REVIEW.md) §3, and the shape of the report follows
[the S3 review](2026-09-26-docs-s3-entry-path-review.md). The recommendations are in §4, and
nothing on this page has been changed yet.*

---

## 0. The answer first

1. **Packs promise fences that jichi does not apply.** A profile marked read-only lists a shell
   it cannot have. A command's profile loses its tool list and its model. Text such as "will not
   run the gate for you" is only advice to the model. Almost none of it is visible when it matters:
   `COMMANDS.md` does say a command's profile loses its tools and model, but nothing says so at run
   time (§2 A).
2. **Three findings are security-relevant.** They are named here by class only (§3). Their
   mechanisms and reproductions are in an internal note that the public tree does not carry, until
   each is fixed; `SECURITY.md` sets the fix-then-write-up order. (A fourth, S3, was withdrawn on
   review: jichi always exports `JICHI_BIN`, so the path it feared is never reached. It survives
   as low-priority hardening, not a vulnerability.)
3. **Several shipped configs and commands cannot work.**
   - Three language servers never start, and a fourth has the wrong name.
   - The music pack's audio playback is never registered.
   - The Guile pack's test gate is green even when the tests fail.
   - `/triage-log` never receives its file.
   - The course coach names a tool that does not exist.
   - Six skills' tool lists parse as empty (§2 C).
4. **The documentation around the packs has drifted.**
   - The README tells learners to run `jichi init learner`, which is refused.
   - Ten packs and two pages say jichi loads one config file, when it merges two.
   - Writing a pack's AGENTS.md silently stops jichi reading an existing CLAUDE.md (32 of the 33
     packs), and the default pack's stub then wrongly claims jichi reads "(and CLAUDE.md)".
   - The domain benches start the reader in the wrong folder (§2 E).
5. **Nothing checks what a pack means, only its shape.** Today's lints check frontmatter keys,
   names and command shadowing. One semantic check over the scaffolded packs would have caught
   about a dozen of these findings (§4, the structural recommendation).

In numbers: four reviewers reported **72 numbered findings**, plus four cosmetic ones. §2 and §3
group them, with this session's own, into **43 distinct findings**: **13 high**, **23 medium**
and **7 low** (S3 moved from medium to low when it was withdrawn as a vulnerability). Some group
several raw findings; C11 and D5 hold six and seven. All 13 high findings were re-checked by this
session: eleven in jichi's code or by an offline run, C5 against ctest 3.28.3, and C2 against the
upstream documentation.

---

## 1. What this was, and how it was done

**The yardstick.**
- `origin/master` at `c2f407b5` was unpacked with `git archive` into a scratch directory and
  built there. The build had no errors and no warnings.
- All 33 packs were scaffolded into separate empty directories with that binary, using a scratch
  `HOME`.
- The tool universe was enumerated twice: from `jichi describe --output json`, and from the
  source's tool definitions. The two disagreed (§2 G1). The union, 46 names, is what the pack
  assets were checked against.

**The reviewers.** There were four read-only reviewers, one per dimension:

| Reviewer | Dimension | Raw findings |
|---|---|---|
| R1 | the assets against the loaders and the runtime: what each key and each promise actually does | 14 and 4 cosmetic |
| R2 | the technical accuracy and currency of the 19 language and domain packs | 24 |
| R3 | the documentation against the packs, presets and benches | 19 |
| R4 | safety, honesty and fitness for self-learners, junior developers and junior administrators | 15 |

They ran the binary only offline, with a scratch `HOME` and a model config that pointed at a
closed port, so no model was ever called. R2 also checked upstream projects on the web; each
such check is cited with its URL and the date, 2026-09-29. Nothing was built from the repository,
nothing in it was changed, and `make ci` was not run.

**What this session verified itself**, rather than taking a reviewer's word:
- the mechanical tool-name check across 419 asset files and 1,300 references;
- the language-server spawn path, and the four affected configs;
- the formatter quoting defect, reproduced harmlessly with `printf`;
- the delegate posture and the tool list of a subtask command;
- the rules loader, and whether `CLAUDE.md` is read;
- the order of command expansion (`@` before `$ARGUMENTS`);
- the parsing of a scalar `allowed-tools`, and pack composition, both run offline;
- `init learner`, run and refused;
- a domain bench in its own folder, run offline;
- the parsing of `sound.play`;
- a failing Guile SRFI-64 test, run on Guile 3.0.9;
- the config overlay in `jc_config.c`.

Findings marked **reported** below come from a reviewer's run or reading and were not repeated.

---

## 2. Findings

Each finding gives its severity, its evidence level and the reviewer IDs it merges.
- **VERIFIED:** this session read the deciding code or ran it.
- **VERIFIED (reviewer):** a reviewer ran or read it, and this session did not repeat it.
- **LIKELY** or **UNCERTAIN:** as stated.

Line numbers refer to `src/scaffold/jc_scaffold.c` unless another file is named.

### A. The packs promise fences that jichi does not apply

**A1. Read-only profiles that list a tool read-only mode removes.** High; VERIFIED. R1-02, R4-04,
and this session's mechanical check.
- The profiles: log-analysis's `log-analyst` (428–440), rewrite's `port-auditor` (3796–3808),
  and sdlc's `release-manager` (3385–3397).
- A `readonly: true` profile is run with its mutating tools withheld from the menu and refused at
  the gate (`jc_tool.c:857` and `1021–1033`). `run_terminal_command` and `run_tests` count as
  mutating tools.
- log-analyst's description says "read + shell, no mutation". Its body says "the shell is
  available for read-only queries". All four of its skills are shell recipes, and none can run.
- release-manager's `/release-check` must report "tests green … command output", and cannot run
  a test.
- `jichi agents` prints the contradiction itself: "readonly · tools: … run_terminal_command".
- A parent that re-spawns such a profile can lift the read-only tightening, since neither spawn
  tool treats a profile's `readonly` as a floor (§3 S2).

**A2. A command's profile and `attempt --agent` ignore the profile's `tools:` and `model:`.** High;
VERIFIED. R1-03, R4-08. **Fixed at M848** by the rule decided on 2026-09-30: a subtask and
`attempt` are fenced by the profile's `tools:`, and `attempt` takes its `model:`.
- `jc_app_command_agent_apply` says it outright: "The profile's model/tools fence are a
  subagent-only concern" (`src/chat/jc_app.c:124–128`).
- So the assignments pack's learner tiers differ only in persona text, under `attempt`:
  - learner-senior's "You have no hint or help tools" is untrue whenever `assignments: true`
    registers them;
  - with it off, the other tiers are told to call tools that do not exist.
- The pack's AGENTS.md advice, "Add a `model:` selector to a profile to make a tier use a
  weaker/stronger model", does nothing under `attempt`. So does `ASSIGNMENTS.md`'s "differ by tool
  allow-list + persona".
- **Decided on 2026-09-30, not built yet:** in a run of its own (a subtask, a subagent, `attempt`)
  a profile's `tools:` becomes a fence that can only narrow, and `attempt` also takes its `model:`;
  the main turn keeps persona, style and read-only. `DECISIONS.md` has the reasoning and the
  options not taken; `DEFERRED.md` def-287 the work, and the gap it leaves (with `assignments` off
  the tiers still name tools that do not exist).

**A3. The `# model: strong` and `# model: fast` hints do nothing.** Medium; VERIFIED
(reviewer). R1-08, R4-13.
- Every profile's `model:` line is commented out.
- `strong` and `fast` are not roles (`jc_config.c:393–429`). If uncommented, a spawn fails with
  "no model matches" unless some model's name contains that word.
- The mentor's hint cannot take effect even when uncommented: the mentor runs only through
  `/learn` and learn-on-stop, and both ignore the profile's model (A2).

**A4. Skill text stated as fact.** Medium; VERIFIED (reviewer). R4-07.
- platform-test: "It will not run the gate for you", and "the learner runs it, not the model".
- course: "It will not write your code".
- Both promises are made in the packs' guide pages (`PLATFORM_TEST.md`, `COURSE.md`) and in
  `init --list`, and the behaviour behind them rests only on instructions in a skill; a top-level
  skill does not fence anything
  (`docs/SKILLS.md`, "advisory at the top level"). The main agent keeps the shell and the file
  tools. In chat mode it asks before using them, which is the real protection, but the pack does
  not say so.

**A5. The sdlc drafting commands cannot write their documents.** Medium; VERIFIED (reviewer).
R1-05, R4-06.
- `/requirements`, `/usecases`, `/design-doc` and `/api` (3614–3670) run a read-only profile at
  the top level, so `write_file` is not offered.
- Each says "draft `docs/REQUIREMENTS.md`" (or its own file). The next command begins "Read
  docs/REQUIREMENTS.md", which nothing wrote.
- `UML_TUTORIAL.md` and `USE_CASE_TUTORIAL.md` warn about this; the pack does not.

**A6. The rewrite pack's "the old tree is read-only (a configured reference root)".** Medium;
VERIFIED (reviewer). R4-10.
- The pack configures no reference root.
- `referenceRoots` widens reads *outside* the workspace, and only while the path fence is on.
- An `old/` directory inside the workspace is writable, and the shell is never fenced.

**A7. The onboarding pack's "propose-only".** Low; VERIFIED (reviewer). R4-14.
- `tutorial-writer` writes files, and unasked when delegated (§3 S2).
- `data-fetcher`'s `fetch_url` counts as read-only, but it makes network requests.

### B. Security-relevant: see §3

### C. Configs and commands that cannot work

**C1. Three language servers never start.** High; VERIFIED. R2-01, R3-03.
- perl (4408): `"command": "perlnavigator --stdio"`.
- r (4532): `"command": "R --slave -e languageserver::run()"`.
- haskell (5030): `"command": "haskell-language-server-wrapper --lsp"`.
- jichi passes `command` whole to `execvp` as the program's name (`src/lsp/jc_lsp.c:62–77`), and
  no such program exists. The arguments belong in `args`, as `LSP.md` shows. With the server
  installed, `doctor` still reports it "not on PATH", naming the whole string.

**C2. The racket pack names a program that does not exist.** High; VERIFIED against the upstream
README (not run). R2-02.
- `racket-langserver` (4776). The upstream README says to "set the command for the custom client
  to `racket -l racket-langserver`".

**C3. The music pack's audio playback is never registered.** High; VERIFIED. R2-04.
- It ships `"sound": { "play": "aplay" }` (4145).
- jichi reads `sound.play` only as an object (`jc_config.c:1546–1556`). A string is ignored
  without a word: `config validate` says OK, and `play_audio`, which `/hear` and `MUSIC.md`
  promise, is absent.
- The right form, as in `SOUND.md`: `{ "command": "aplay", "args": ["-q"] }`.

**C4. The Guile pack's test gate is green when tests fail.** High; VERIFIED on Guile 3.0.9.
R2-05.
- With SRFI-64's default runner, a failing test prints "# of unexpected failures 1" and exits 0.
- `testCommand` (`guile -L . tests/all-tests.scm`) becomes the verify gate when no `verify` is
  set, so the gate cannot go red on a test failure (a load error still exits non-zero).
- R2's fix is verified: take `(test-runner-current)` before `test-end`, and `exit` on its fail
  and xpass counts.

**C5. The cpp pack's test gate tests stale binaries.** High; VERIFIED against the ctest manual,
and since run on ctest 3.28.3: after editing a test without rebuilding, ctest exited 0 with "100%
tests passed"; with no tests registered, 0 with "No tests were found!!!"; with `--no-tests=error`,
8. R2-06.
- `ctest --test-dir build --output-on-failure` runs the test binaries that already exist; building
  is ctest's separate `--build-and-test` mode. So after an edit it tests the old code.
- A tree with no registered tests passes too.
- Fix: `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`.

**C6. Six skills' tool lists parse as empty.** Medium; VERIFIED. R1-06, R3-18.
- The skills: cpp `sanitizer-triage`, perl `perlcritic-triage`, r `r-cmd-check-triage`, haskell
  `haskell-warning-triage`, elixir `elixir-failure-triage`, erlang `erlang-failure-triage`.
- Each writes `allowed-tools: read_file, search_code, run_terminal_command`.
- jichi's YAML reader is block-style only, and `jc_yaml_seq_len` returns 0 for a scalar. So the
  tool hint disappears, and doctor's fence check sees nothing.
- The same would happen to any imported Claude-style skill that writes its list this way.

**C7. The course coach names a tool that does not exist.** Medium; VERIFIED. Three reviewers
(R1-04, R3-04, R4-07; R2 did not review course), and this session's check.
- course-coach (5798) lists `docs_search`; the tool is `search_docs`.
- The list is advisory, so nothing is blocked. But the only tool name in the skill whose method
  is "ground every claim in the snapshot" is wrong. A call returns "unknown tool" with no
  suggestion.
- `doctor` flags it only when run in that directory, and it calls the list a fence that "drops it
  silently", which is not what an advisory list does.

**C8. `@$ARGUMENTS` in a command reads a file named `$ARGUMENTS`.** High for `/triage-log`;
VERIFIED. R1-01, R1-10, R4-05.
- Command expansion handles `@path` before `$ARGUMENTS`, reading the token after `@` literally
  (`src/command/jc_command.c:295–308`).
- So `/triage-log /var/log/syslog` sends the prompt ending "[could not read $ARGUMENTS]", and the
  log is lost, in the TUI and headless alike.
- The same defect breaks the `@$1` example in `COMMANDS.md` and one in
  `examples/opencode.jsonc`.
- The assignments pack's `check.md` says "(a path or @file)", which expands to "[could not read
  file).]".

**C9. The course and platform-test pages tell learners to type a tool's name.** Medium; VERIFIED
(reviewer). R1-11.
- `COURSE.md` and `PLATFORM_TEST.md` say to type `> load_skill course-coach`. `load_skill` is a
  tool the model calls; typed, it is only a message.
- Neither page mentions the `/course` or `/platform-test` command the pack ships. Both commands
  lack `$ARGUMENTS`, so `/course controlflow` drops the section's name.

**C10. The C and C++ packs never create a compilation database.** Medium; VERIFIED (reviewer,
clangd 18 run locally). R2-07.
- Without `compile_commands.json`, clangd guesses the flags and reports errors that do not
  exist: "'util.h' file not found", and a header parsed as Objective-C++.
- jichi appends these diagnostics to every edit result, so the model is steered into "fixing"
  them.
- The cpp pack's `clang-tidy -p build` has no database to read, because the configure line lacks
  `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.

**C11. Command invocations that do not do what the text says.** Medium or low; VERIFIED
(reviewer). R2-11–14, R2-17, R2-20.
- perlcritic prints the policy name only at `--verbose` 8 to 11 (12 and up fall back to the
  default format), and the bracketed `[Policy::Name]` form only at 8 and 9; and the policy is
  `BuiltinFunctions::ProhibitStringyEval`, not `Subroutines::…`.
- `perltidy -b` leaves a `.bak` beside every file; add `-bext='/'`.
- `package-lint-batch-and-exit` is an Emacs Lisp function, not a shell command, and
  `batch-byte-compile` exits 0 on warnings, so "zero warnings is the bar" is not enforced.
- In an R package, `testthat::test_dir()` does not load the package's functions.
- The C++ pack lists "use-of-uninitialized" among ASan's errors (ASan does not detect it), and
  UBSan continues after an error unless built with `-fno-sanitize-recover`.
- The C pack's valgrind skill gives no invocation, and without `--error-exitcode=1` valgrind never
  fails a gate.

### D. Content that is out of date

**D1. Zig 0.15 and 0.16 changed the standard library, and the pack does not say so.** Medium;
VERIFIED (reviewer, on Zig 0.16.0). R2-08.
- `std.ArrayList(u8).init(alloc)` and `std.io.getStdOut()` no longer compile.
- Models trained on older Zig write exactly that. This project's own zigodot drive met the same
  failure.
- The pack should name the version, and show `.empty`, passing the allocator on each call, and
  buffered writers that must be flushed.

**D2. erlang_ls is archived.** Medium; VERIFIED (reviewer). R2-10.
- Its repository says "currently unmaintained. We recommend switching to the Erlang Language
  Platform (ELP)". ELP is launched as `elp server` (its CLI documentation, checked 2026-09-30).
- EDoc `%% @doc` is dated since OTP 27's `-doc` attributes.

**D3. TypeScript 7.** Medium; VERIFIED against the raw sources on 2026-09-30 (the TypeScript
blog and npm registry, typescript-go's source, typescript-language-server's README), not run end
to end. R2-16.
- TypeScript 7 (the native port) shipped on 2026-07-08 without a stable programmatic API, and
  typescript-language-server now installs `typescript@6`.
- TS 7 serves LSP itself: `tsc --lsp --stdio`.
- **Run on 2026-10-01** (Node 20 on the workstation, packages from npm, through `jichi lsp`):
  TypeScript 6 with typescript-language-server reports the error; with TypeScript 7 that server
  refuses to start and jichi shows only "no diagnostics received"; `tsc --lsp --stdio` answers
  only pull requests, which jichi does not send. npm's `typescript` `latest` is 7.0.2.
- **Decided on 2026-10-01, not built yet:** jichi reports a server's error reply, asks for pull
  diagnostics and falls back to the next server; then the pack ships TypeScript 7's server
  first. `DECISIONS.md` has the reasoning, `DEFERRED.md` def-291 to def-293 the work.

**D4. Godot.** Medium; VERIFIED except the bridge. R2-15.
- Godot's language server speaks over TCP to a running editor. jichi's client only spawns stdio
  programs, so a bridge such as `nc 127.0.0.1 6005` would be needed; this is UNCERTAIN.
- The pack gives no parse check (`--headless --check-only`) and no GDScript 4 guidance, although
  Godot 3 syntax is a model's usual mistake.
- **Run on 2026-10-01** (Godot 4.7.2 on the workstation, isolated, with the operator's agreement):
  a headless editor serves the language server (listening after about 2 s, diagnostics pushed);
  `jichi lsp` through `socat` or OpenBSD `nc` prints the errors in 0.6 s and 1.1 s; with no editor
  the server is `unavailable` in 11 ms; `--check-only` exits 1 in 213 ms. The bridge is no longer
  UNCERTAIN.
- **Decided on 2026-10-01:** the `--check-only` gate and GDScript 4 guidance in the pack; the
  bridge as a measured opt-in recipe (`GAME_DEVELOPMENT.md`); TCP in jichi's client later.
  `DECISIONS.md` has the reasoning, `DEFERRED.md` def-294 and def-295 the work.

**D5. Smaller currency items.** Low; VERIFIED (reviewer). R2-18, R2-19, R2-21 to R2-24.
- elixir-ls's binary name differs by install, and the official Expert server is alpha.
- The perl pack recommends Try::Tiny, but `try`/`catch` is stable since Perl 5.40.
- The racket pack says `match` "is exhaustive-checkable", but it fails only at runtime.
- The data pack's notebook advice assumes a notebook handling jichi does not have, and a
  `find_references` that exists only with a language server.
- The music pack gives only the soundfont path Debian uses.
- The guile pack promises a language server and ships none.
- The python pack ships pyright as a language server, so `format_file` IS offered -- but pyright
  does not format and there is no `formatCommand` fallback, so a format request does nothing.

**D6. The shipped glossary says telemetry is "opt-in".** Low; VERIFIED (reviewer). R1-12.
Metrics have been on by default since M599.

### E. The documentation about packs

**E1. "jichi loads exactly one config file", or a project file that "shadows" the global one, is
false, in ten packs and two pages.** Medium; VERIFIED. R2-09, R3-02.
- Without `--config` or `$JC_CONFIG`, jichi merges `~/.jichi` with `local/config.json` (or else
  `.jichi/config.json`). Scalar keys from the project win, and lists are joined
  (`jc_config.c:1112–1147`).
- The rust-cli, go-cli, web-ts, cpp, perl, r, haskell, elixir, erlang and elisp configs say a
  project file would "SHADOW" the global one, and six of their AGENTS.md repeat it. Only three
  (rust-cli, go-cli, web-ts) use the words "exactly ONE config file"; the other seven say
  "shadow(s)", so a lint for this must match both wordings.
- `SCAFFOLDING.md` (146–153) and `ARCHITECTURE.md` say it too. The c-cli, python-cli and
  zig-cli configs, written by the same binary, say the opposite, correctly.

**E2. The AGENTS.md most packs write says jichi also reads CLAUDE.md, and `init` silently turns
CLAUDE.md off.** High; VERIFIED. R3-05, R4-03.
- The stub says "jichi reads this file (and CLAUDE.md)" (23–24). The rules loader reads
  AGENTS.md and returns; CLAUDE.md is read only when no AGENTS.md exists
  (`src/chat/jc_rules.c:84–96`).
- So `jichi init` in a project with a CLAUDE.md makes the user's own rules vanish, with no
  warning. R3 and R4 reproduced it: after `init`, `jichi rules` no longer shows the rule.
- Worst in platform-test. Its steps run only inside the jichi checkout, and this checkout's
  CLAUDE.md forbids an AGENTS.md at its root for exactly this reason.
- **Decided on 2026-10-01, not built yet:** `init` will not write an AGENTS.md where the project
  has its own rules file; the pack's rules go to `JICHI.md`, jichi's own rules file, which is read
  in addition to the shared one. `DECISIONS.md` has both decisions, `DEFERRED.md` def-288 and
  def-289 the work.

**E3. The README and APPROACH.md say `jichi init learner`.** High; VERIFIED.
R3-01.
- `learner` is a setup preset, not a pack. The command prints "init: unknown pack 'learner'",
  exits 2 and writes nothing.
- These are the first commands a learner copies.

**E4. Composing packs loses the docs pack's audience routing, and `--dry-run` overstates.**
Medium; VERIFIED. R1-07, R3-06, R3-07.
- In a composed `init`, the first pack wins every shared path. `/proofread` and `/write-docs`
  come in two versions: only the docs pack's delegates to its six audience profiles.
- So `init default docs`, or `c-cli docs`, which `SCAFFOLDING.md` recommends, installs the six
  profiles with nothing to route to them. This session ran it: default's `/proofread`, six
  profiles, and "13 skipped".
- `SCAFFOLDING.md` calls the shared files "byte-identical, so their skips are cosmetic".
- A composed `--dry-run` checks only the disk, not what an earlier pack in the same call will
  write: for `init c-cli docs` it reported "47 to write, 0 skipped" for a run that wrote 34 and
  skipped 13 (for `init default docs`, 49 reported and 36 written).
- **Decided on 2026-10-01, not built yet:** one shared text for `/write-docs` and `/proofread` in
  every pack, routing to the audience profiles when they exist; a unit test for byte-identical
  shared paths; `init` naming a skipped file that differs. `SCAFFOLDING.md` and `SDLC.md` now say
  to put `docs` first. The `--dry-run` half stays open. `DECISIONS.md` has the reasoning,
  `DEFERRED.md` def-290 the work.

**E5. The domain benches start the reader in the wrong folder.** Medium; VERIFIED (this
session ran one). R3-08, R3-09, R3-16.
- Twelve of the thirteen `START_HERE.md` files (all but slm-lab's, which already says to copy
  first) say "Open jichi here (`cd` into this folder, run `jichi`)". In the bench
  folder the assets sit in `agents/`, `commands/` and `skills/`, which jichi does not read, so it
  reports "(no custom commands …)".
- The copy recipe `cp -r …/agents .jichi/agents` nests the copy as `.jichi/agents/agents/` once
  `.jichi/agents` exists, which it does after any `init`.
- The obvious correction overwrites same-named files. For example, the scheduling bench's
  writable `planner.md` would replace the pack's read-only one.
- There are 13 benches (`slm-lab` meets the contract), not the 12 that `init --list` and four
  pages say.

**E6. The setup wizard reaches 7 of the 17 language packs.** Medium; VERIFIED (reviewer).
R3-10, R3-11, R3-15.
- The interactive menu lists default plus seven language packs (c-cli, python-cli, zig-cli,
  godot, rust-cli, go-cli and web-ts). The other ten are reachable only with `--lang`, and no
  page says so.
- Detection maps `.cpp` files to `c-cli`, although a cpp pack exists.
- `--lang` is ignored without a word on a preset that does not ask for a language.
- The `composer` preset promises "the engraving gate as verify", and writes no `verify`.
- Several pages describe the wizard as it was before the journey, machine and stance questions:
  `TUTORIAL_BEGINNER.md`, `SETUP_WIZARD.md`, `SDLC.md` and `WORKFLOWS.md` ("five journeys";
  there are seven).

**E7. SCAFFOLDING.md is stale in several places.** Medium or low; VERIFIED (reviewer). R3-12 to
R3-14, R3-17, R3-19.
- It says only onboarding lacks the accessibility advisors; course and platform-test lack them
  too.
- "What the default pack installs" lists 16 of its 26 files.
- The music pack is the only pack never named.
- The tool-fence paragraph contradicts itself, points to a section that does not exist, and says
  skills enforce their lists, when a skill's list is advisory.
- `SDLC.md` lists a `test-writer` for the sdlc pack, which ships none.
- `jichi packages` has no documentation page.

### F. Fitness for the readers and their models

**F1. Delegation is left to the lead's initiative.** Medium; VERIFIED (reviewer). R4-13.
- Only three commands spell out a spawn: `/write-docs`, `/proofread` and `/investigate`. The
  sysadmin, devops, contributor and refactor packs rely on the lead deciding to delegate.
- In the operator's experience the reasoning model tends not to delegate, and in the weekend
  drive the coding model never did (`DRIVING_A_TEAM.md` §1).
- Many tasks are open-ended, with no checkable "done" for a fast model.

**F2. The admin packs do not say what leaves the machine, and some recipes are careless.**
Medium; LIKELY or VERIFIED (reviewer). R4-11, R4-12, R4-14.
- Logs, crontabs, unit `Environment=` lines and CI files all go into the prompt, and so to the
  model's server. No pack says so.
- ci-reviewer is told to find secrets and cite them by file and line, but not to withhold the
  values. env-check gets this right.
- backup-verify extracts a whole archive to a fixed `/tmp/restore_test`, with no space check and
  no cleanup.
- `journalctl -f` in the foreground blocks the turn, indefinitely: jichi's shell commands have no
  time limit unless the model passes `timeout` or the operator sets `runTimeout`.
- `privilegedCommandsAllow` is recommended without saying that entries match word prefixes: an
  entry `sudo systemctl` also admits `sudo systemctl disable --now sshd`.

**F3. The assignments pack's authoring gate is described, and never run.** Medium; VERIFIED
(reviewer). R4-08, R4-09.
- `/assign` says "the tutor will prove that with `jichi grade <file> --expect-fail`". Nothing
  runs that, and the human is not told to. That is the hollow-gate failure
  `CHOOSING_A_MODEL.md` §3 records for the coding model.
- Nothing withholds a `*.solution.md` from the tutor or from a learner's attempt worktree.

**F4. Course and assignments point at things only the jichi checkout has.** Low; VERIFIED
(reviewer). R4-15, R1-13.
- `jichi assignments --stage python` prints an empty list and exits 1 outside the checkout
  ("assignments: no stage 'python' here. Stages:").
- The code-reading skill sends the learner to `docs/CODE_REVIEW.md` and `docs/reading/traces/`.

**F5. Presets switch off what their pack needs.** Low; VERIFIED (reviewer). R1-09.
- The `small-local` preset's core tool profile advertises no `spawn_parallel`, which
  `/investigate` requires.
- The `contributor` preset starts in plan mode, where its test-running agents cannot run tests.

### G. Found on the way, outside the packs

**G1. `jichi describe --output json` omits five tools.** Medium; VERIFIED.
- `ask_for_help`, `board`, `hint`, `play_audio` and `record_audio` are defined in the source and
  absent from the contract that driving agents read.
- They are conditional tools, but the contract's `conditional_tools` list names 23 others.

**G2. Config values of the wrong shape are accepted silently.** Medium; VERIFIED.
- A string for `sound.play`, and an `lspServers` `command` that contains arguments, both pass
  `config validate`. The first then fails without a word; the second gets only `doctor`'s
  misleading "not found: perlnavigator --stdio" (C1, C3). A warning at load time would have
  saved every pack reader.

**G3. `spawn_parallel` reads its `write` argument strictly.** Low; VERIFIED (reviewer).
- It uses `cJSON_IsTrue` (`jc_tool_parallel.c:1296`), against CLAUDE.md's rule that a model's
  tool arguments are read leniently. It fails safe: a `"true"` string gives a read-only child.

**Also found on review (2026-09-29), not counted in the tallies above:**
- The docs pack's AGENTS.md says its audience agents "arrive in a later release"
  (`jc_scaffold.c:1145-1146`), but the pack ships all six.
- `@` file references in a command are cut at `JC_CMD_FILE_MAX` with no marker (R4-05). It matters
  for C8's fix: expanding `@$ARGUMENTS` in `jc_command_expand` instead of in the assets would
  inherit that silent cut.

---

## 3. Security-relevant findings, by class

The mechanisms, the evidence and the reproductions are in the internal note
`docs/internal/2026-09-29-scaffolding-security-findings.md`. That directory is not published,
until each is fixed.

- **S1.** A tool that runs a configured external command can lose an argument's quoting, so a
  crafted input value can inject a command. Two packs ship a command in that form. Reproduced
  harmlessly end to end (mock model); high. **Fixed at M847:** a `{}` inside quotes is refused and
  the shipped packs take the path as a trailing argument.
- **S2.** A shipped command runs an agent with every tool and no approval prompts, in chat mode.
  That is the mode where a user expects every change to ask first. `TOOL_DECISIONS.md` and
  `COMMANDS.md` describe this path in words that suggest the opposite. Reproduced with the mock
  model, headless and in the TUI's chat mode (plan mode and `--readonly` still refuse it); high.
  **Fixed at M848:** a subtask runs under the user's posture, and a delegate that asks nobody is
  path-fenced.
- **S4.** The admin packs give a delegate the shell, and the privileged allowlist admitted a
  delegate's command by prefix. **Fixed at M849:** a delegate's shell call follows the user's
  posture, and the allowlist is never a delegate's.
  **Decided on 2026-09-30:** the fix makes such a command follow the user's own approval mode,
  and turns the workspace path fence on for delegates; it lands with the other security fixes.
- **S3 -- withdrawn.** The suspected case (a shipped template executing a program found in the
  project directory) is **not reachable**: jichi always exports `JICHI_BIN`, so the branch that
  would run `./jichi` is dead code. Kept as low-priority hardening (delete the dead branch), not a
  vulnerability.
- **S4.** Two administration packs give a delegated agent the shell. After one approval, it runs
  commands on a live system without asking again. Privileged commands stay gated **unless** the
  pack's own `privilegedCommandsAllow` is filled, which both packs tell the user to do; high for
  the audience.

---

## 4. Recommendations

**P0 — before these packs are recommended to anyone else:**
1. Fix S1–S4 as the internal note proposes.
2. Split `command` and `args` in the perl, r and haskell LSP entries, and give racket
   `"command":"racket","args":["-l","racket-langserver"]` -- putting the whole string in `command`
   would reintroduce C1 (C1, C2).
3. Make the music pack's `sound.play` an object (C3). Make the Guile gate exit on SRFI-64
   failures (C4). Build before `ctest`, with `--no-tests=error` (C5).
4. `/triage-log`: replace `@$ARGUMENTS` with `$ARGUMENTS`, and tell the model to read that path
   (C8). Fix `check.md`'s `@file`, and the `@$1` example in `COMMANDS.md`.
5. Rename course-coach's `docs_search` to `search_docs` (C7), and write the six comma lists as
   YAML lists (C6).
6. Remove `run_terminal_command` and `run_tests` from the three read-only profiles. Either drop
   `readonly` where the shell is the point, or reword the promise (A1).
7. Correct the AGENTS.md stub's "(and CLAUDE.md)" (decided 2026-10-01, under E2 above). Have
   `init` warn, or not write AGENTS.md,
   where a CLAUDE.md exists (E2).
8. Replace `jichi init learner` with `jichi setup --preset learner` in the README and
   APPROACH.md (E3).

**P1 — make the promises true, or say what they are:**

9. Decide A2 in the code (decided 2026-09-30, under A2 above). Either apply a profile's `tools:`
   and `model:` to a command's `agent:`
   and to `attempt --agent`, or reword every asset that relies on them: the learner tiers, the
   `model:` hints and the release-manager.
10. Give the mentor `readonly: true`; its `output:` fallback still writes the draft. Give the
    sdlc drafting commands `subtask: true` and an `output:` (A5).
11. Label every text-only rule as a request, and say what enforces the rest (A4, A6, A7). For
    example: "The coach is told not to run it. If it tries, jichi asks you first; say no."
12. Make delegation structural in the packs that need it (F1). Add commands that spawn a named
    profile with a task card: the file, the test, and what "done" means. Use real selectors, not
    `fast` and `strong`. This follows the operator's model guidance of 2026-09-29: the coding
    model needs small tasks, strict guidance and supervision, and the reasoning model tends not to
    delegate by itself.
13. In the admin packs, add a line saying that everything read is sent to the configured model
    endpoint, and that a secret is reported by its location, never its value. Fix backup-verify
    and the `journalctl -f` recipe, and explain `privilegedCommandsAllow`'s word-prefix
    matching (F2).
14. Tell the `/assign` user to run `jichi grade <file> --expect-fail` themselves, and keep
    solutions out of the learner's tree (F3).

**P2 — currency and documentation:**

15. Update the Zig pack for 0.15 and 0.16, the Erlang pack for ELP, and the web-ts pack for
    TypeScript 6 and 7. Add GDScript 4 guidance and a parse check to the Godot pack. Correct the
    R, Perl, Emacs Lisp, C and C++ invocations in C11, and the smaller items in D5 and D6.
16. Rewrite `SCAFFOLDING.md` against the binary:
    - the overlay semantics (E1);
    - the composition order, and the loss of the docs pack's routing (E4);
    - the accessibility exceptions, the full default file list, the music pack, and the
      tool-fence paragraph (E7).
17. Give every bench's `START_HERE.md` a "step 0: copy these into your workspace". Use a copy
    recipe that merges and warns about collisions, and say 13 benches (E5).
18. Bring the wizard pages up to date, map `.cpp` to the cpp pack, warn when `--lang` is ignored,
    and fix or reword the composer preset (E6).
19. Add the five tools to `describe`'s contract (G1).

**Structural — the check that would have caught about a dozen of these.** Extend
`tests/test_scaffold.c` (or add one smoke driver) to scaffold every pack and assert:
- every tool name in `tools:` and `allowed-tools:` is a known tool (C7);
- a `readonly: true` profile lists only read-only tools (A1);
- a tool list is a YAML sequence, not a string (C6);
- no command body contains `@$`, and no stray `@` outside an intended include (C8);
- no `lspServers` `command` contains whitespace (C1);
- `sound.play` and `sound.record` are objects (C3);
- no scaffolded text contains the false one-config sentence (E1);
- and `jichi doctor` over each scaffolded pack reports no warnings.

Then add load-time warnings for the shapes in G2, so a user's own config gets the same
protection. Each check should be proven red first, by replanting its defect, as the testing
runbook requires.

---

## 5. What this review could not check

- **No model was called.** Everything this page says about what a model does with a pack is
  inferred from the code: S2, S4, A1's re-spawn route, and F1. None of it was observed in a run.
- **Several toolchains were not installed:** R, ctest and CMake, TypeScript 7 (run later, on
  2026-10-01: D3), Godot (run later too: D4), Elixir,
  Erlang and Perl's language server. Those findings rest on upstream documentation, checked on
  2026-09-29, and are marked LIKELY where nothing was run.
- **The reviewers are model agents.** Every high finding was re-checked, and every medium one is
  marked with who verified it. The remainder rest on the reviewer's reported run or reading. A
  second pass over the medium and low findings would be worth its cost before they are fixed in
  bulk.
- **One master.** The other machine commits to master continuously. A finding may already be
  fixed when this is read, and the fixes should be checked against the master of their day.
- **This session's own first check had a gap.** It parsed only block and bracketed tool lists,
  so it missed C6's six comma lists. A reviewer's reading found them. That is the "audit the
  universe" lesson (`TEST_INTEGRITY.md`) once more, and a reason for the lint in §4.

---

## 6. What is worth protecting

These were checked and found right. A fix for the findings above must not break them.
- **The accessibility assets** are identical in all 30 packs that ship them, and their "change
  nothing" is enforced twice: a read-only turn through the command, and a read-only tool list
  through a spawn.
- **Other read-only profiles** have the same double enforcement: reviewer, planner, ci-reviewer,
  threat-modeler, architecture-mapper, dependency-analyst, the docs proofreaders and several
  more.
- **`jichi attempt`** runs in a worktree with the path fence forced on, and reports test edits
  as TAINTED. That is the guard against the coding model's known way of passing a gate.
- **`/onboard` and `/learn`** write their answer to `output:` even when their profile cannot
  write, and never overwrite an existing file.
- **`init` itself** behaves as documented. `--list`, `--global`, `--force` and `--dry-run` (for
  one pack) do what they say. It is non-destructive and exits with the right code. A mistyped
  pack name is refused before anything is written.
- **Names that should match do.** No pack command shadows a TUI built-in. Every command's
  `agent:` names a profile in the same pack. Every flag, subcommand and config key the assets
  mention exists. Of 1,300 tool references, only one is a wrong name.
- **env-check** never prints secret values. The privileged-command claims in the admin packs'
  AGENTS.md are mostly accurate: `privilegedAudit` can switch the "always-on" log off, and an
  entry in `privilegedCommandsAllow` admits matching commands without a prompt (§3 S4).

---

## 7. The lesson

A pack is prose that describes fences, and the fences live in the code. The packs were written
when the code was younger, and nothing checks that the two still agree.

The existing lints check shapes: keys, names, shadowing and JSON validity. Every one of them
passes on all 33 packs, while `/triage-log` cannot receive a file, a gate cannot go red, and a
read-only analyst is told it has a shell. A green lint said its universe was clean, not that it
was the universe that mattered.

The fix is not more careful writing. It is the structural check in §4, which turns each of these
promises into something a test can fail.

## Read further

- [`SCAFFOLDING.md`](../SCAFFOLDING.md): the packs, as documented today.
- [`TOOL_DECISIONS.md`](../TOOL_DECISIONS.md): the nine mechanisms that decide whether a tool
  call runs.
- [`SUBAGENTS.md`](../SUBAGENTS.md) and [`COMMANDS.md`](../COMMANDS.md): profiles, delegation,
  and command frontmatter.
- [`TEST_INTEGRITY.md`](../TEST_INTEGRITY.md) and [`TESTING_RUNBOOK.md`](../TESTING_RUNBOOK.md):
  why a check must be proven red, and how.
- [`DRIVING_A_TEAM.md`](../DRIVING_A_TEAM.md) and [`CHOOSING_A_MODEL.md`](../CHOOSING_A_MODEL.md):
  why delegation must be structural for these models.
