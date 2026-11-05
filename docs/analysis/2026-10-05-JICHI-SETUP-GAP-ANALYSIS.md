# jichi gap analysis — scaffolding / init / setup, and the generated start script

Read-only. All paths under `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`),
evidence cited, inference flagged, reproducible appendix. The user's suspicion —
"the run script the setup produces is error-prone" — is **confirmed**, and the
evidence is unusually strong: the project's *own tutorial* (CONFIG_TUTORIAL.md)
prescribes the four hardening rules the generated script omits, and the register
(ANECDOTES #57) records the exact misdirection one of those omissions caused.

## TL;DR

jichi's setup is a real, heavily-tested, honest wizard (`jichi setup`, the
`jc_setup` core with preset/journey/machine/stance axes, `local/config.json`
targeting, gap-fill merge M53, key-in-`~/.jichi.env` at 0600, a live
`doctor`-flavoured validation block, and a "left unset" report M326k). But the
**start script it generates** — `run.sh` / `task.sh` / `test.sh` / `review.sh`
(jc_setup.c:662-722) — is the weakest artifact the wizard ships, and it is
inconsistent with the project's own documented launcher standard. The generated
script (a) is **cwd-fragile**: it references `CONFIG="local/config.json"` and
`.jichi/` with no `cd "$(dirname "$0")"` and no anchor, so running it from any
other directory fails with a bare `config file not found` whose diagnosis
("wrong directory" vs "no config") the user cannot distinguish; (b) is **not
fail-fast**: no `set -eu`, so a failed step (the key source) is silently ignored
and the run continues into a *different* error (missing API key); (c) **always
overwrites the environment** with `~/.jichi.env` (unconditional source at
jc_setup.c:698), defeating the documented `VAR=... ./run.sh` and systemd/CI
override path; and (d) **fails without the fix**: none of its error states
(unknown `JICHI` binary, missing config, missing key) names the repair command.
CONFIG_TUTORIAL.md:714-723 lists exactly these four as the house rules for a
launcher ("`set -eu` and `cd` in every script"; "let the environment override
the file"; "fail with the fix") and shows a recommended version that follows
them — the generator does not. The register's ANECDOTES #57 (2026-08-17,
FreeBSD) is a measured instance of the *same shape, on the generated script
itself*: the driver ran `jichi setup` in a scratch workspace and exercised the
*produced* `run.sh` (`setup_keyfile.sh:185-192` — it even greps the generated
script for the key load), and with `JICHI` lost at the shell boundary the
script's `${JICHI:-jichi}` fallback fell through to the bare `jichi` that is
not on PATH, producing an *internally consistent but misleading* report —
key fine, file fine, sourcing fine, doctor fine — while the cause sat
unreported (3261-3324). And note the
incident's own verdict: the driver "asserted nothing" in one of five
same-shaped cases — a failure that passes is the error-prone shape made
structural. Of the wizard's three artifacts (config, assets, script) the
script is the only one that is *executed by the user's shell*; the config is
checked at generation time, and the tutorial's recommended script is
hand-written. What is error-prone is precisely the piece the user gets for
free and never reads.

## 1. What exists — and why the wizard itself is a genuine strength

- **The wizard is real and tested.** `jc_setup` (src/setup/jc_setup.c) builds
  the preset table, the config, and the start script; `tests/test_setup.c`
  covers the preset table, config round-trip, and script generation
  (including `script_name` mapping and the presence of the config path in the
  emitted text). The interactive path (main.c:~5665-5900) scaffolds the pack,
  writes the config with gap-fill merge (M53) or inheritance (`--from-global`,
  W5), offers the key store, runs `setup_validate`, prints "left unset"
  (M326k), and shows the file (M326n).
- **The key handling is careful.** `setup_keyfile` (main.c:~5388-5470)
  refuses a key containing a single quote (so it cannot break the `'...'` the
  file uses), writes with `umask 077`, `chmod 0600` even on append (so an
  existing file's mode is corrected), and reports the character count (M326n)
  as screen-reader feedback. The config stores only the *env-var name*
  (`apiKeyEnv`), never the value, so a generated config is safe to commit
  (SETUP_WIZARD.md:240-242).
- **The "left unset" report is honest** (M326k, main.c:~5815-5850): ten
  capabilities, each named as "what is absent" and "where to set it", with a
  separate M357 note that all-zero pricing is *not* a decline but a
  consequence of enabling telemetry without pricing.
- **The validation block is a real `doctor` pass** (setup_validate,
  main.c:5507-5580): config-is-valid-JSON, model-configured, API-key-present
  (three states: stored-in-`~/.jichi.env` / env-set / env-not-set-yet, with the
  M326f fix so a stored key is not nagged), and `start script: written`.
  Note: it validates the config and *notes the script exists*; it does not
  *execute* the script or check its semantics. That gap is the crux.
- **The tutorial is the house standard.** CONFIG_TUTORIAL.md §2.2 shows a
  recommended launcher with `set -eu`, `cd "$(dirname "$0")"`, an
  override-guarded key load, and an error that names the fix; §"Rules" (714-723)
  states them as universal for a launcher.

## 2. The one thing definitely lacking

**The generated start script does not follow the project's own launcher
standard, and the wizard neither validates nor self-documents its failure
modes.** Concretely, four omissions, each with a named house rule and (for the
first) a recorded incident:

1. **cwd-fragility (no `cd "$(dirname "$0")"` / anchor).** `CONFIG` defaults to
   `local/config.json` (a *relative* path) and the script `exec`s
   `"$JICHI" --config "$CONFIG" ...` from whatever cwd the user happens to be
   in. There is no `cd` and no `$(dirname "$0")` anchor. Run from anywhere but
   the project root and `local/config.json` does not exist, so jichi returns
   `JC_ERR_NOTFOUND` with a one-line "config file not found" (jc_config.c:1172).
   The tutorial's rule 2 ("`cd "$(dirname "$0")"` … makes the script work from
   any directory", 716-718) is precisely the fix, and its example (562)
   applies it.
2. **Not fail-fast (no `set -eu`).** The key source line,
   `[ -f "$HOME/.jichi.env" ] && . "$HOME/.jichi.env"` (jc_setup.c:698),
   *evaluates false* (and thus "succeeds" as the last-considered command) when
   the file is absent — fine on its own. The real `set -eu` cost is what the
   whole script lacks: with nothing to stop a failure, a broken step (a
   source that errors, an `exec` that cannot find `$JICHI`) is either
   swallowed or surfaces downstream as a *different* error than the one that
   caused it — e.g. the key-file problem becomes a jichi "no API key" report
   two layers later. The tutorial's rule 2 ("`set -eu` … stops a failed step
   from being ignored", 716-717) is the fix.
3. **Environment is always overridden (unconditional source).** The key file
   is sourced *unconditionally* (698), so `export JICHI_API_KEY=... ./run.sh`
   and any CI/systemd `EnvironmentFile` are silently clobbered by `~/.jichi.env`.
   The tutorial's rule 3 ("Let the environment override the file. One
   `[ -z "${VAR:-}" ]` guard …", 720-722) and its example (574-576:
   `if [ -z "${JICHI_API_KEY:-}" ] && [ -f "$KEY_FILE" ]; then . "$KEY_FILE"`)
   are the fix.
4. **Fails without the fix (no self-documenting errors).** The script has no
   error messages at all: unknown `JICHI` binary, missing `CONFIG` file, and
   missing key all produce *jichi's* or *the shell's* bare errors, none of which
   name the repair. The tutorial's rule 4 ("Fail with the fix. An error message
   containing the command that repairs it …", 722) and its example
   (`echo "run-jichi.sh: no JICHI_API_KEY." >&2`, 580) are the fix.

These are not four unrelated bugs; they are the four launcher house rules, and
the generated script violates all four while the *hand-written* tutorial script
follows all four. The gap is that the artifact the user is *given* (and told to
just run, per SETUP_WIZARD.md:246-249 "this is why `./run.sh` works") is the one
artifact that is below the project's own stated standard.

## 3. Why this is "definitely lacking" (alternatives and their status)

- *"The tutorial's recommended script is the standard, but it's an
  *alternative* the user can opt into by hand."* That is the strongest
  counter, and it is real: §2.2 is "the one to copy". But the generated script
  is what the *default, free* path produces, and SETUP_WIZARD.md:246-249
  actively *recommends* the generated one ("the generated start script
  (`run.sh`) sources that file, which is why `./run.sh` works"). A user who
  takes the recommended path gets the weaker artifact; a user who also reads
  CONFIG_TUTORIAL §2.2 and hand-writes one gets the stronger one. The wizard
  ships the thing its own docs say is sub-standard, and says nothing to
  reconcile the two. (The reconciliation, or a generator that meets the
  standard, is the fix.)
- *"The script is trivially small; `set -eu`/`cd` are two lines."* True — which
  is *why* it is a gap rather than a limitation: the fix is cheap, the standard
  is already written and demonstrated, and the cost of not applying it to the
  generated artifact is a runtime failure the user cannot diagnose (see
  §4/incident). Small and wrong is worse than large and right.
- *"`$HOME/.jichi.env` is absolute, so the key path isn't cwd-fragile."*
  Correct — the *key* file is anchored. The *config* file (`local/config.json`)
  is the cwd-fragile part, and it is the one that resolves through the shell's
  current directory. I am not claiming the key load breaks on cwd; I am
  claiming the `--config` argument does.
- *"The validation block already checks the config is valid JSON and the key
  is present."* It does, *at generation time, in the wizard's own cwd*. It does
  not check the *script's* runtime behavior, which is exactly where the four
  omissions bite (a different cwd, a different shell, an absent key file, an
  override). setup_validate's `start script: written` line (5573-5575) is a
  `jc_file_exists` — it confirms the file is on disk, not that it works.
- *"This is a docs issue, not a code issue."* Half right: part of the fix is
  making the generator emit the standard (code); the other part is the wizard
  telling the user the generated script assumes the project root (docs). I
  frame it as a *code* gap with a docs component because the tutorial already
  states the standard — the generated artifact should meet a standard the
  project has already written down.

## 4. Evidence

- **The generated script, in full (all four profiles).**
  `src/setup/jc_setup.c:673-722` (`jc_setup_start_script`):
  `#!/bin/sh`; `JICHI="${JICHI:-jichi}"` (687);
  `[ -f "$HOME/.jichi.env" ] && . "$HOME/.jichi.env"` (698, *unconditional*);
  `CONFIG="${JICHI_CONFIG:-local/config.json}"` (699, relative); then
  `exec "$JICHI" --config "$CONFIG" [--plan|--auto] ...` — `test.sh` adds
  `--budget-tokens 400k --verify-every 8 -p "${1:-run the tests and fix any
  failures}"`. **No `set -eu`, no `cd`, no `$(dirname "$0")`, no error branch,
  no message** (verified by reading the whole function; `grep -c "set -eu"`
  over 673-722 = 0).
- **The house standard, four rules, in the project's own docs.**
  `docs/CONFIG_TUTORIAL.md` "### 2.6 Recommendations" (714-723):
  (1) commit the launcher, ignore the config; (2) "`set -eu` and
  `cd "$(dirname "$0")"` in every script. The first stops a failed step from
  being ignored, the second makes the script work from any directory";
  (3) "Let the environment override the file. One `[ -z "${VAR:-}" ]` guard
  makes the same script usable by a developer, CI, and systemd"; (4) "Fail
  with the fix. An error message containing the command that repairs it is
  worth more than any amount of README". The §2.2 example (558-580) applies all
  of them: `set -eu` (558), `cd "$(dirname "$0")"` (562), the guard (574-576),
  `echo "run-jichi.sh: no JICHI_API_KEY." >&2` (577). (Verified.)
- **The wizard *recommends* the weaker artifact.** `docs/SETUP_WIZARD.md:246-249`:
  "The generated start script (`run.sh`) sources that file, which is why
  `./run.sh` works in a shell where you never exported anything." — no
  mention of project-root assumption, overrides, or any of the four rules.
  (Verified.)
- **The validation block does not execute the script.** `setup_validate`
  (main.c:5507-5580): config JSON parse, model configured, API key present
  (three states, M326f), `jc_is_dir(".jichi")`, and `start script: written` =
  `jc_file_exists(scriptname)` (5573-5575). Nothing runs `sh -n`, nothing
  simulates a different cwd, nothing checks `command -v "$JICHI"`.
  (Verified.)
- **The failure semantics the script relies on.** An explicit `--config` that
  does not exist is a *hard error*: `jc_config.c:1170-1172` — "config file not
  found: %s", returns `JC_ERR_NOTFOUND`. (So the cwd-fragile script's failure
  is a one-line error with no hint.) A key containing `'` is refused by the
  writer (main.c:5432-5437); `setup_secret` trims trailing CR/whitespace
  (main.c:~5405) — the key path is careful, which makes the *launcher* path
  the odd one out. (Verified.)
- **A recorded instance, on the generated script itself.** ANECDOTES #57
  (3261-3330, FreeBSD, 2026-08-17): the driver ran real `jichi setup` in a
  scratch workspace (`setup_keyfile.sh:181`) and invoked *the produced*
  `run.sh` — which lost `JICHI` at the shell boundary ("FreeBSD `/bin/sh`":
  `FOO=bar f` does *not* export, 3305-3310), fell through its
  `${JICHI:-jichi}` fallback to a bare `jichi` not on PATH, and reported
  "internally consistent and still misleading" (3316-3319); five more
  same-shaped sites, one of which "asserted nothing" (3326-3328). The
  incident's own opening (3263-3268) names the generated artifact: "the
  wizard wrote `~/.jichi.env`, it was mode 600, the generated `run.sh`
  sourced it, and `doctor` printed 'API key present for the active model'" —
  every element true, the cause unreported. (Verified.)
- **Tests cover generation, not behavior.** `tests/test_setup.c`
  (`test_start_script`, 265-283): asserts the *name* mapping
  (review/task/test/run) and that the emitted text *contains* the config path —
  no `sh -n`, no run-from-subdir, no override case, no missing-config case.
  (Verified.)

## 5. Implications

- **The user's first command after onboarding fails in exactly the cases the
  onboarding never rehearsed.** `./run.sh` from the project root, in the
  wizard's shell, with the key stored: works. Move one directory (very common:
  you are in `src/` when you type `../run.sh` — which is *correct*, but the
  script's `local/config.json` is now relative to `src/`), unset the
  `JICHI` override you set up for a local build, or rely on `JICHI_API_KEY`
  being exported in CI: each produces a bare error whose cause is not
  stated. The onboarding promise ("this is why `./run.sh` works") is true
  only in the one situation the wizard itself just created.
- **It is the artifact with the most runtime, the least testing, and the
  least standard.** The config: checked at generation (JSON parse, doctor
  pass), documented to the key, trusted-recorded (M851). The assets: the
  scaffold tables with overwrite policy (M53 gap-fill, `--force` named in the
  docs). The script: generated, `chmod +x`'d, noted as "written", and then —
  unlike the tutorial's own example — below the project's written launcher
  standard. The wizard's care is visible everywhere except the one file the
  user actually executes.
- **The `JICHI` override is documented, half-working, and unreported.** The
  comment in the script says "jichi is expected on PATH; set JICHI to a local
  build if not" — the developer-build path. It works *only* if the caller's
  shell passes it correctly (the #57 boundary lesson), and when it does not,
  the failure is a bare `jichi: not found` from *inside* an `exec` chain the
  user did not write, with no message connecting it to the script's own
  comment.
- **It composes with the other seams.** In CI (#57's home), a launcher that
  clobbers the environment (rule 3) is *actively wrong*: the `EnvironmentFile`
  the ops person set is silently replaced by the developer's
  `~/.jichi.env`. The same script is the natural entry for `jichi -p`
  fleet-driving (SCRIPTING.md) — a script that cannot be overridden or
  diagnosed from a different directory is a fleet entry point with a
  hidden tripwire.

## 6. Recommendation

1. **Make the generated script follow the tutorial's four rules** — the
   standard is already written (CONFIG_TUTORIAL.md:714-723) and demonstrated
   (558-580); the generator (jc_setup.c:673-722) should emit it:
   `set -eu` after the shebang; `cd "$(dirname "$0")"` (with the tutorial's
   one-line rationale) so `local/config.json` and `.jichi/` resolve from any
   invocation directory; the key source guarded by
   `if [ -z "${VAR:-}" ] && [ -f "$KEY_FILE" ]` with `KEY_FILE` defaulting to
   `$HOME/.jichi.env` (the wizard knows the var name — it asked for it); and a
   final guard that `echo`s a named error ("`run.sh`: no $VAR set — store it
   with `jichi setup` or export it") instead of letting jichi report it two
   layers down. ~15 lines; the generator already knows everything needed.
2. **A red-first driver: run the generated script from a subdirectory.**
   Generate a setup in a scratch repo (the suite already does this — ANECDOTES
   #57's driver exercised the generated `run.sh`), then run
   `(cd src && ../run.sh --help)` (or the cheapest no-model jichi invocation)
   and assert it behaves identically to `(cd . && ./run.sh ...)`. Before the
   fix: red (`config file not found`). After: green. Add the sibling cases:
   `JICHI_API_KEY=explicit ./run.sh` must keep the explicit value (rule 3),
   and a script whose key file is absent must print *the script's* message
   (rule 4), not jichi's.
3. **`setup_validate` should at minimum `sh -n` the script it just wrote and
   say so** — it already runs the config through a JSON parse and the doctor
   block; a syntax check on the one artifact it does *not* check closes the
   "written ≠ works" gap at generation time. (Executing it is out of scope for
   a validator — it would need a model and a key; the *user* does the
   executing.)
4. **One line in SETUP_WIZARD.md** next to the "this is why `./run.sh` works"
   paragraph (246-249): the script assumes the project root and lets the
   environment override the key file — the two facts that will otherwise
   surface as the user's first unexplained error.

Rejected, in the register's spirit:

* *Make the config path absolute in the script.* It would fix cwd-fragility
  for *this machine* — but the wizard's own recommendation is "commit the
  launcher, ignore the config that has local paths in it" (rule 1, 715-716):
  an absolute path in a committed script is exactly the leak rule 1 exists to
  prevent. `cd "$(dirname "$0")"` is the portable form.
* *Document the four rules as "hand-edit your script" instructions.* The
  register's standing position is that a *generator* that can emit a
  sub-standard artifact is the defect, not the user who takes the default:
  the wizard is deliberately honest about what it does (M326k/m/n) — the
  generated script meeting the same standard is in that spirit, and it costs
  the user nothing.
* *Add `set -e` only, and keep the unconditional source.* `set -e` without
  the guard would make a *missing* key file a hard error *only if* the source
  could fail — it cannot (the `[ -f ]` test already no-ops cleanly) — so the
  guard (rule 3) is the change that actually alters behavior, and the message
  (rule 4) is the one that alters diagnosis.

## 7. Limitations and honest counterarguments

1. **The script is *small*, and each omission is *cheap*.** I am not claiming
   a design failure; I am claiming the free artifact is below the project's
   own written standard, and that the cost shows up at the user's first
   unexplained runtime error. The register's own framing for this class of
   gap is exactly "small and wrong is worse than large and right" — the fix
   is ~15 lines of the generator plus one driver.
2. **The tutorial script is a *recommended example*, not a *spec*.** §2.2 says
   "the one to copy" — it is advice, not a contract the generator must
   satisfy. But §2.6's "Recommendations" (714-723) state the four rules as
   universal ("in every script"), and the incident register (ANECDOTES #57)
   paid the cost of the shape on the generated script itself. A standard the
   project writes down, demonstrates, and then *does not apply to the artifact
   it ships for free* is a standard with one hole — the hole is the shipped
   artifact.
3. **I have not executed the generated script in the wrong cwd** (read-only
   analysis). The claim "fails with `config file not found`" is verified by
   reading the generator (relative `CONFIG`, no `cd`) *and* the loader
   (`jc_config.c:1170-1172`, hard error on missing explicit config). The
   *message text* and *exit behavior* I describe are the direct consequence;
   step 2's driver executes it.
4. **The wizard is genuinely careful everywhere else** — key quoting (5432),
   umask+chmod (5444-5460), M326f's no-nag, M326k's honest "left unset",
   M851's trust gating of *created* vs *merged* configs. This analysis does
   not flatten that into "setup is weak"; it says the *one* artifact that is
   executed by the user's shell is the one place the care stops.
5. **`JICHI_CONFIG` override exists** (699): an advanced user *can* point the
   script at a config from any cwd. That is a workaround, not the standard —
   the tutorial's point is the script should *not need one* — and it is
   undocumented in the wizard's "next steps" (it is only in the script's own
   comment, which a user who has not opened the script will not have read).

## 8. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

    # the generated script, in full (all four profiles)
    sed -n '662,722p' src/setup/jc_setup.c
    grep -n 'JICHI:-jichi\|HOME/.jichi.env\|JICHI_CONFIG\|set -eu' src/setup/jc_setup.c
                                                          # 687, 698, 699, (0 = no set -eu)

    # the project's own launcher standard + the example that follows it
    sed -n '714,723p' docs/CONFIG_TUTORIAL.md            # rules 1-4
    sed -n '558,580p' docs/CONFIG_TUTORIAL.md            # set -eu / cd / guard / named error

    # the wizard recommends the weaker artifact
    sed -n '246,249p' docs/SETUP_WIZARD.md               # "this is why ./run.sh works"

    # the validation block: config checked, script only "written"
    sed -n '5507,5580p' src/main.c                       # setup_validate; 5573-5575 = jc_file_exists

    # the failure semantics the script relies on
    sed -n '1170,1172p' src/config/jc_config.c           # "config file not found", JC_ERR_NOTFOUND

    # the key writer (the careful sibling, for contrast)
    sed -n '5429,5470p' src/main.c                       # quote-refusal, umask 077, chmod 0600, count

    # the recorded instance of the shape (FreeBSD, the generated run.sh)
    sed -n '3261,3268p' docs/ANECDOTES.md                # "the generated run.sh sourced it"
    sed -n '3305,3328p' docs/ANECDOTES.md                # the boundary table + fix + 5 sites

    # tests: generation, not behavior
    sed -n '265,283p' tests/test_setup.c                 # name mapping + config path present

**Verdict line for the register this could seed:** jichi's setup wizard is a
genuine strength — tested core (`tests/test_setup.c`), honest reporting (M326k
"left unset", M326n showing the file), careful key handling (quote-refusal,
umask 077, chmod 0600, M326f no-nag), trust-gated configs (M851), and a real
doctor-style validation block (main.c:5507). The error-prone artifact the
suspicion points at is confirmed: the **generated start script**
(`jc_setup.c:673-722`; `run.sh`/`task.sh`/`test.sh`/`review.sh`) violates all
four of the project's own launcher rules as written in
CONFIG_TUTORIAL.md:714-723 — (1) no `set -eu`, so a failed step is ignored
and the error surfaces two layers downstream as a *different* one; (2) no
`cd "$(dirname "$0")"`, so the relative `CONFIG="local/config.json"` is
cwd-fragile and fails with the bare `config file not found`
(jc_config.c:1172) that does not say "run from the project root"; (3) an
*unconditional* `[ -f "$HOME/.jichi.env" ] && . ...` (698) that clobbers an
explicit `JICHI_API_KEY` or CI/systemd override — the exact rule 3 the
tutorial's §2.2 example guards against; (4) no self-documenting error, so
the three failure states (unknown `JICHI`, missing config, missing key) all
name their cause to no one. The wizard validates the config (JSON parse,
doctor block) and the script's *existence* (`jc_file_exists`, 5573) but never
its *behavior*; the test covers generation (name mapping, config path
present), not execution. The register has already paid for the shape once:
ANECDOTES #57 (FreeBSD) — the generated `run.sh`'s `${JICHI:-jichi}` fallback
hit a `jichi` not on PATH, the report was "internally consistent and still
misleading", and one of five same-shaped sites "asserted nothing".
SETUP_WIZARD.md:246-249 actively recommends this artifact ("this is why
`./run.sh` works") without stating the project-root assumption or the
override semantics. Definitely lacking: the generator emitting the four
rules (≈15 lines, every input already known to the wizard: the var name, the
config path, the key-file convention), a red-first driver that runs the
generated script from a subdirectory and with an env override (both red
before, green after), and `setup_validate` running `sh -n` on the script it
just wrote. Revisit when: `./run.sh` works from any directory, the
environment wins over `~/.jichi.env`, a missing key produces the *script's*
named error, and the driver proves all three — at which point the wizard's
free artifact meets the standard its own tutorial already wrote.
