# Where to start

jichi's tutorials each cover one leg of the road; this page only routes.

**New here?** Read the row that matches what you want to do — or take the
shortest path at the bottom of this page.

**Not sure you want to invest an hour yet?** The first row is the one to read: it
says what this project believes and how it works, in two pages, before asking you
to install anything.

| You want to… | Read |
|---|---|
| **Understand what kind of project this is** — the values, and the eight working practices they cash out as: design before code, born-red tests, lints over audits, plain-text registers, saying plainly what is unverified | [`docs/PHILOSOPHY.md`](docs/PHILOSOPHY.md) (the why) · [`docs/APPROACH.md`](docs/APPROACH.md) (the how) |
| **Build jichi from source** — toolchain, clone, `make`, verify; written for someone who has never compiled C. Linux and WSL2 are both verified paths, executed end to end; the macOS instructions have **never been executed** ([`docs/PLATFORMS.md`](docs/PLATFORMS.md)) | [`docs/PREPARE_AND_BUILD.md`](docs/PREPARE_AND_BUILD.md) |
| **Set up a working bench in minutes** — the guided wizard, role presets, validation | [`docs/SETUP_WIZARD.md`](docs/SETUP_WIZARD.md) |
| **Have your first session** — chat, approve an edit, undo it, run tests | [`docs/TUTORIAL_BEGINNER.md`](docs/TUTORIAL_BEGINNER.md) |
| **Understand or hand-write the config** — models, roles, routing, budgets; from zero to the full dev config | [`docs/CONFIG_TUTORIAL.md`](docs/CONFIG_TUTORIAL.md) |
| **Use an institutional gateway** (a university/company LLM proxy) | [`docs/curriculum/INSTITUTIONAL.md`](docs/curriculum/INSTITUTIONAL.md) |
| **Convert an existing Continue / opencode / Claude Code setup** | [`docs/CONVERT.md`](docs/CONVERT.md) |
| **Go deep** — autonomy envelope, routing, parallel agents, retrieval, headless scripting | [`docs/TUTORIAL_ADVANCED.md`](docs/TUTORIAL_ADVANCED.md) |
| **Explore the boundaries** — build it as C++, build it with zig, or survey where POSIX ends on Windows (honest verdicts included) | [`docs/CPP_BUILD.md`](docs/CPP_BUILD.md) · [`docs/ZIG_BUILD.md`](docs/ZIG_BUILD.md) · [`docs/PORTING_WINDOWS.md`](docs/PORTING_WINDOWS.md) |
| **Learn the design-and-modelling craft** (reading-first) — tests, pseudocode, UML, use cases, domain modelling, architecture | [`docs/TESTING_TUTORIAL.md`](docs/TESTING_TUTORIAL.md) · [`docs/PSEUDOCODE_TUTORIAL.md`](docs/PSEUDOCODE_TUTORIAL.md) · [`docs/UML_TUTORIAL.md`](docs/UML_TUTORIAL.md) · [`docs/USE_CASE_TUTORIAL.md`](docs/USE_CASE_TUTORIAL.md) · [`docs/DOMAIN_MODELLING_TUTORIAL.md`](docs/DOMAIN_MODELLING_TUTORIAL.md) · [`docs/ARCHITECTURE_TUTORIAL.md`](docs/ARCHITECTURE_TUTORIAL.md) |
| **Learn the craft itself, with graded assignments** | [`docs/CURRICULUM.md`](docs/CURRICULUM.md) |
| **See the whole road to mastery** (the practice behind the course) | [`docs/JOURNEY.md`](docs/JOURNEY.md) |
| **Find the workflow for your role** (learner, instructor, solo dev, team, supervisor) | [`docs/WORKFLOWS.md`](docs/WORKFLOWS.md) |

Shortest useful path:
[`docs/PREPARE_AND_BUILD.md`](docs/PREPARE_AND_BUILD.md) → `jichi setup` →
`jichi doctor` (read every line) → your first session
([`docs/TUTORIAL_BEGINNER.md`](docs/TUTORIAL_BEGINNER.md)).

Stuck anywhere? `jichi doctor` first — it checks config, key, server
reachability, roles, git/snapshots, and MCP/LSP, with fix hints
([`docs/DOCTOR.md`](docs/DOCTOR.md)).
