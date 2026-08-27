# jichi examples

Runnable, ready-to-use companions to jichi. The largest group is the **domain
benches** — scaffold packs that turn an empty `.jichi/` into a guided workspace
for a whole domain. They ship here (copy-to-use) rather than compiled into the
binary, deliberately: they are large and per-interest, and jichi targets small
binaries on low-resource devices (the decision is recorded in
[`../docs/proposals/2026-08-domain-scaffolds.md`](../docs/proposals/2026-08-domain-scaffolds.md);
`jichi init --list` points here). Each bench honours a fixed **beginner-support
contract**: a numbered `START_HERE.md`, a guide agent + a read-only
domain-mistake reviewer, an `AGENTS.md` that names the domain's boundaries, four
commands, three skills, and a secret-free `config.example.json`. Each is kept
green by a smoke driver (`tests/smoke/example_<name>.sh`).

## Domain benches

Copy a bench into a workspace and follow its `START_HERE.md`:

```sh
mkdir my-project && cd my-project && mkdir -p .jichi
cp -r /path/to/jichi/examples/<bench>/agents   .jichi/agents
cp -r /path/to/jichi/examples/<bench>/commands .jichi/commands
cp -r /path/to/jichi/examples/<bench>/skills   .jichi/skills
cp    /path/to/jichi/examples/<bench>/AGENTS.md .
# then set up a model (see the bench's config.example.json) and read START_HERE.md
```

### Creative & technical

| Bench | For | The loop |
|---|---|---|
| [`data-analysis`](data-analysis/) | analyzing a dataset, honestly | look → question → clean → analyze → check → report |
| [`game-design`](game-design/) | designing a game (before code) | core fun → mechanic → scope → review → playtest |
| [`game-dev`](game-dev/) | building a game (the code half) | build → feature → bug → review → release |
| [`blender-python`](blender-python/) | scripting Blender with `bpy` | run → explore → make → tool → review → package |
| [`krita-python`](krita-python/) | scripting Krita with PyKrita | run → act → automate → review → package |

### Self-management (for the self-learner)

| Bench | For | The loop |
|---|---|---|
| [`project-management`](project-management/) | running a project solo, to done | charter → board → standup → review → retro |
| [`personal-finance`](personal-finance/) | budgeting your own money¹ | budget → check → track → review → goal |
| [`scheduling`](scheduling/) | time, deadlines, routines | deadlines → plan → block → review → measure |
| [`business-plan`](business-plan/) | pressure-testing a venture idea¹ | canvas → customer → pricing → challenge → cheap test |

### Student

| Bench | For | The loop |
|---|---|---|
| [`academic-writing`](academic-writing/) | essays, reports, a thesis² | thesis → outline → feedback → cite → review → proofread |
| [`research-notes`](research-notes/) | reading & organizing research² | annotate → summarize → matrix → synthesize → review |
| [`web-basics`](web-basics/) | your first web page, online | page → style → responsive → accessible → live |

¹ *Educational, not financial/business/legal advice* — these benches teach methods
you apply to your own numbers, keep data local, and defer real decisions to a
qualified professional (stated throughout each pack).
² *Coaches your own work; does not do it for you* — these benches help you write,
cite, and read honestly, and put academic integrity (and your institution's AI
policy) in your hands.

## Other examples

| Example | What it is |
|---|---|
| [`self-hosting`](self-hosting/) | a "jichi reviews jichi" dev pack — read-only C89/arena reviewers + a write-enabled slice (fenced to tests/docs, `verify=make test`). See [`../docs/proposals/2026-08-self-hosting-dev-pack.md`](../docs/proposals/2026-08-self-hosting-dev-pack.md). |
| [`autonomous-loop`](autonomous-loop/) | reference artifacts for running jichi as an unattended, supervised loop over a task queue (tmux/systemd/cron); the C89 supervisor is built by `make examples`. See [`../docs/AUTONOMOUS_LOOPS.md`](../docs/AUTONOMOUS_LOOPS.md). |
| [`web-bridge`](web-bridge/) | a Python-stdlib sidecar (`bridge.py`) that drives headless jichi and streams events as SSE to a browser — the minimal web front-end track. See [`../docs/WEB_FRONTEND.md`](../docs/WEB_FRONTEND.md). |
| [`robot-sim`](robot-sim/) | a simulated robot for the kinetic/robotics gate (`docs/ROBOTICS.md`) — jichi as the seconds-scale deliberative layer over an E-stop-protected device. |
| [`skills`](skills/) | example `SKILL.md` files illustrating the progressively-disclosed skills mechanism. |
| [`stress`](stress/) | stress/soak test products for the measurement harness. |

## Status of the domain benches

Pilots, proven here first (the "prove it in `examples/` before promoting" path,
as the self-hosting pack took). They are validated on every build by their smoke
drivers, and are fully usable by copying as above. Whether any graduate into
compiled-in `init` packs is a size tradeoff recorded in the proposal — for now the
lean binary wins and they live here, one `cp` away.
