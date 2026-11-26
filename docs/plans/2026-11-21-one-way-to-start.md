# One way to start jichi: retire the interactive start scripts (design note, 2026-11-21)

*Designed, not built. Registered as def-419. The operator's question and decision are in section 1.*

## 1. The question, and the decision

On 2026-11-21 the operator asked, of the scripts `jichi setup` writes into a project: what were the design
decisions behind them, and what are the alternatives? They had felt the scripts never worked properly, and that
their comments and documentation confused self-learners. They had expected jichi simply to be started in the
project directory and to load the project's configuration.

The decision, the same day: **(A) plus (C) below.** jichi is started one way, `jichi` in the project directory, and
loads the key file itself. Setup writes a start script only for an unattended profile.

## 2. How jichi starts without a script

`jichi` started in a directory combines two configs. One is the global config, `~/.jichi`. The other is the project
config: `local/config.json`, or `.jichi/config.json` when there is no `local/` file. The project overlays the
global (`src/config/jc_config.c`, the file-based branch of the loader). A project file is applied in full only after
the trust step (M851), which `jichi trust` performs. An explicit `--config <file>` or `$JC_CONFIG` loads exactly one
file, with no combining and no trust step.

## 3. Why the scripts exist (the recorded decisions)

- **M48, the setup wizard.** It writes one POSIX `sh` wrapper per preset profile:
  - `run.sh` (interactive);
  - `review.sh` (`--plan`);
  - `task.sh` (`--auto`);
  - `test.sh` (`--auto --budget-tokens 400k --verify-every 8`, the last two from M357).
- **M326e, the key.** The wizard used to write a config naming a key variable and leave the user to export it.
  Exported in `~/.bashrc`, the key never reaches a cron or systemd run. Setup now offers to store the key in
  `~/.jichi.env` as `export NAME='VALUE'` (mode 600), and every script sources that file behind a `[ -f ... ]`
  guard. jichi itself does not read the file. Only setup's own model check does (`setup_jichi_env_lookup`,
  M1200).
- **M785, the docs.** A page told readers to run `./run.sh` without saying what it was. The fix was a sentence of
  explanation.

The 19 presets at `0ac55740` (`src/setup/jc_setup.c`):

| Profile | Script | Presets |
|---|---|---|
| interactive | `run.sh` | 14: composer, constrained, developer, devops, existing-tree, generic, instructor, learner, refactor, rewrite, small-local, small-project, support, technical-writer |
| plan | `review.sh` | 4: architect, contributor, data, reviewer |
| test | `test.sh` | 1: tester |
| auto | `task.sh` | 0: no preset uses it |

## 4. What goes wrong

Items 1-4 were run on 2026-11-21. The setup: the installed binary of master `34756498` from the G8 acceptance, a
copy of that run's 27B project and global config, HOME pointing at the copy's home, and jichi on PATH. `<copy>` stands for the
copy's directory on the workstation. The commands and their exact output:

```
$ cd <copy>/proj && jichi config path
[jichi] the project config <copy>/proj/local/config.json is not trusted, so 5 setting(s) from it are not applied: models, testCommand, logging, timeouts, _comment -- run `jichi trust` here to apply them (`jichi doctor` lists what each would do)
<copy>/home/.jichi + local/config.json (merged)
[exit 0]
$ cd <copy>/proj && ./run.sh config path
local/config.json
[exit 0]
$ cd <copy>/proj/sub && ../run.sh config path
[jichi error] config file not found: local/config.json
[jichi] could not load configuration: local/config.json -- fix the file above, or pass --config <file> to use another one
[exit 1]
$ cd <copy>/proj && JICHI_CONFIG=local/config.json jichi config path
[jichi] the project config <copy>/proj/local/config.json is not trusted, ...
<copy>/home/.jichi + local/config.json (merged)
[exit 0]
```

1. **The script starts a different configuration.** It passes `--config local/config.json`, so the global config
   is not combined in and the trust step is skipped: every key of the project file applies, unasked. M851 recorded
   this as "the start scripts `jichi setup` writes are unaffected (they pass `--config`)".
2. **The config's own comment is wrong under the script.** `setup --from-global` writes a `_comment` saying
   "runtime still merges ~/.jichi under this". That is true for `jichi` and false for `./run.sh`.
3. **It works only from the project root.** The script does not change to its own directory, and the path it passes
   is relative.
4. **Two names for one setting.** The script reads `JICHI_CONFIG`; jichi reads `JC_CONFIG`.
5. **The beginner tutorial mixes the two.** Read at `0ac55740`:
   - `TUTORIAL_BEGINNER.md:155` checks the key with `jichi doctor`, which combines the configs and has no key file
     loaded.
   - Line 156 then starts the session with `./run.sh`, which uses one file and does load the key.
   - Line 379 explains a 401 as a variable "not exported in *this* shell".
6. **It pointed a model at the key file.** In the G8 acceptance run (2026-11-19), jlu/qwen3.8-27b ran
   `jichi setup --from-global --preset learner`, printed the `run.sh` it wrote, and later checked whether
   `~/.jichi.env` existed and counted its lines.

## 5. The design

**(A) One way to start, for every interactive preset: `jichi` in the project directory.**
- **A1. jichi loads the key file itself.**
  - Order: a key variable already in the environment wins. Otherwise jichi looks in `~/.jichi.env` for the line
    `export NAME='VALUE'` for each `apiKeyEnv` the config names.
  - The parser is the existing `setup_jichi_env_lookup` (M1200), moved out of `src/main.c` into a module and
    unit-tested.
  - The file must belong to the user and be readable by no one else, the rule ssh applies to its keys. Otherwise
    jichi does not read it, and says so by name. On a platform without POSIX mode bits, the check is decided per
    platform, not skipped silently.
  - The value goes to the provider only. It never enters the environment jichi's child processes inherit, so the
    existing scrub of model shells has nothing new to remove.
  - `doctor` reports where each key came from (the environment or `~/.jichi.env`), never the value.
  - This serves M326e's reason without a script: a cron or systemd run of plain `jichi` reads the same file.
- **A2. The preset's mode goes into the project config.** The four plan presets write `"mode": "plan"`. The trust
  step already treats `mode` as narrowing (`jc_config_trust.c`), so an untrusted project file can set `plan`.
- **A3. Setup writes no script for an interactive preset.** It ends by saying how to start (`cd <dir>`, then
  `jichi`) and what the trust step is.
- **A4. The pages say one thing.** TUTORIAL_BEGINNER, SETUP_WIZARD, PREPARE_AND_BUILD, CONFIG_TUTORIAL section
  1.4, and the jichi-guide skill's `setup` topic.

**(C) A script only where it earns its place: an unattended run.**
- **C1. `test.sh` stays for the tester preset,** for cron and similar, where a fixed set of flags is the point.
  It changes:
  - it moves to its own directory first;
  - it passes no `--config`, so the normal combining and the trust step apply;
  - it no longer sources the key file, since A1 does that;
  - it keeps M357's `--budget-tokens 400k --verify-every 8`.
- **C2. `task.sh`, the `--auto` profile, is reached by no preset.** Either remove it, or keep it behind an explicit
  flag. The build decides which.
- **C3. Existing projects keep working.** Their scripts still pass `--config`. `doctor` notes a project start
  script that passes `--config`, and that the global config is then not combined.

**Rejected.**
- **(B) Keep a script for every preset and fix its four defects.** That still leaves two ways to start, and the
  docs would still have to teach both.
- **Better documentation alone.** M785 tried that, and the confusion in section 4 remains.

## 6. Open, for the build

- Whether setup trusts the project file it has just written, since the person ran setup, or leaves `jichi trust`
  to them as now.
- The key-file permission rule on DJGPP and the Windows layers, where the POSIX mode bits do not mean the same.
- Whether the G8 rubric's credential rule changes once no generated script names the key file. It is decided
  separately, as candidate 3 of the G8 acceptance.

## 7. What it would take

- **Red first, one driver per behaviour:**
  1. plain `jichi` in a project, with the key only in `~/.jichi.env`, authenticates to a local mock that checks the
     header;
  2. a group-readable key file is refused, by name;
  3. a key variable in the environment wins over the file;
  4. the key is in no child shell's environment;
  5. setup writes no script for an interactive preset;
  6. a plan preset starts in plan mode from its config;
  7. `test.sh` started from a subdirectory finds its project.
- Each check is watched failing without its change.
- The pages in A4 change in the same milestone as setup, so no page teaches a script setup no longer writes.
