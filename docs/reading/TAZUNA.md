# 手綱（たづな）*Tazuna* — the harness

*The fourth of the jichi source reading guides.
- The beginner edition is [案内（あんない）*Annai* — the guided tour](ANNAI.md).
- The expert edition is [深掘り（ふかぼり）*Fukabori* — the deep dive](FUKABORI.md).
- The traced runs are [追跡（ついせき）*Tsuiseki*](TSUISEKI.md).
- [記録（きろく）*Kiroku*](KIROKU.md) is a reading guide too, but it reads the
  record, not the code.
- This guide reads a different program: `jichi-harness`, the drive harness. Its
  reference is [`docs/HARNESS.md`](../HARNESS.md).*

A *tazuna* is the reins: the thing in the driver's hands that steers a team
which does the pulling itself. That is what `jichi-harness` is for.

A jichi **drive** is a team of AI agents, a lead and its workers, working on a
repository for hours or days without a person at the keyboard. Someone has to:
- start the lead's runs, one after another, and stop them when told;
- keep what the agents did, even when a run dies;
- push their work to a remote **without** giving them the credential;
- notice when they declare a batch ready, and put that batch through a gate
  before it may reach `master`;
- deliver the verdict back to the team.

None of that may be in the agents' own hands, because the agents are what it
guards against. `jichi-harness` is the program that does it. It is written in
the same strict C89 as jichi, and it sits in `src/harness/`.

## Why read it

- **It is a complete example of a supervisor.** It is not a framework. Every
  decision a supervisor makes is a function you can read: when to start a run,
  what a run may see, when a verdict is green, what counts as "landed".
- **It was built by porting shell scripts.** Its commands are C ports of the
  scripts the drives ran on the host. For the scheduled ones (`push`, `notify`,
  `watch`) the rule is a shadow: the port runs beside its script on real events,
  and the script stays in charge until the two agree. Chapter 10 tells that
  method, including the day a replay showed that agreement had not been enough.
- **Its defects are teaching material.** Each chapter ends with something that
  went wrong in the code it describes: a dangling pointer, an argv without a
  NULL terminator, a filter blind to two leading spaces. Each is told from the
  record, or measured in the chapter and marked as such, with the fix where
  there is one, and said plainly where there is not.

## Who this is for

Anyone who has finished [Annai](ANNAI.md) chapters 1 to 5. Two other readers
are in mind too:
- **a junior developer** asked to change one of the harness's commands;
- **an agent** that has to understand the program supervising it, which is the
  rarer and stranger reader.

You do not need to have run a drive. The exercises build `jichi-harness` and
run it in a throwaway directory, never against a live drive.

## Before you start

```sh
# in the jichi checkout
make jichi-harness
./jichi-harness --help < /dev/null
```

Have `docs/HARNESS.md` open beside the chapters: it is the harness's reference,
and the chapters are its reading companion. Every `file.c:function` anchor
below resolves in the tree, and every quoted block is copied from its source
line for line. On every `make smoke`, `tests/smoke/reading_refs_lint.sh` checks
the anchors, and `tests/smoke/reading_quotes_lint.sh` checks that each quoted
line is still in its file and that the tag names the function the block is in.
Neither lint checks the order of the quoted lines.

## Chapters

| # | Chapter | What it reads |
|---|---|---|
| 1 | [What a harness is for](tazuna-01-what-a-harness-is-for.md) | the threat model, the commands, the map of `src/harness/` |
| 2 | [`init`: laying out a drive](tazuna-02-init-laying-out-a-drive.md) | `jc_harness_init.c`, `jc_harness_config.c` |
| 3 | [The mirror and the credential helper](tazuna-03-the-mirror-and-the-credential-helper.md) | `jc_harness_mirror.c`, `jc_harness_cred.c` |
| 4 | [`run` and the handoff](tazuna-04-run-and-the-handoff.md) | `jc_harness_run.c`, `jc_harness_handoff.c` |
| 5 | [`drive`: the outer loop](tazuna-05-drive-the-outer-loop.md) | `jc_harness_drive.c`, `jc_hp_rundrive.c`, `jc_hp_records.c` |
| 6 | [`push`: the scheduled host push](tazuna-06-push-the-scheduled-host-push.md) | `jc_harness_push.c`, `jc_hp_tick.c`, `jc_hp_backlog.c`, `jc_hp_units.c` |
| 7 | [`notify` and `watch`](tazuna-07-notify-and-watch.md) | `jc_harness_notify.c`, `jc_hp_watchgate.c`, `jc_hp_watchunits.c` |
| 8 | [`gate run`](tazuna-08-gate-run.md) | `jc_hp_gate.c`, its five stage files and `jc_hp_gateunits.c` |
| 9 | [`main`, the settings, and `doctor`](tazuna-09-main-config-and-doctor.md) | `jc_harness_main.c`, `jc_harness_config.c`, `jc_harness_doctor.c` |
| 10 | [Porting scripts into C](tazuna-10-porting-scripts-into-c.md) | the method: dry shadows, shimmable host commands, real events |

## How this guide was written

The chapters were drafted on 2026-11-13 from the tree that became jichi 0.14.0.
The work was split across several Claude agents, each given two chapters and the
same rules: quote only what the code says, run every published command first,
claim nothing unmeasured. The supervising session then reviewed every chapter
against the code, ran both reading lints, and corrected what it found.

The record of what was corrected is kept with the guide's plan,
[`docs/plans/2026-11-13-tazuna-the-harness-course.md`](../plans/2026-11-13-tazuna-the-harness-course.md).
If you find a sentence the code contradicts, the code wins. Please report it.
