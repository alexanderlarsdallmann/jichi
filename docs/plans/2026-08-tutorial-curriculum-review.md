# Tutorial & curriculum beginner-review (2026-08-02)

Three specialist reviewers read the onboarding tutorials, the curriculum
framework + module pages, and the graded assignment specs against one rubric:
**(L1) step-by-step for beginners · (L2) mermaid-diagram opportunities · (L3)
context clarity — what / on which system / by whom / when.** This records their
findings and tracks what is fixed. Fixes are weighted toward the **absolute
first-boot** experience and toward building **critical-thinking developers**.

## Fixed in the first pass (this commit)

- **The `./jichi` vs `jichi` first-boot trap** (all three reviewers' #1). A
  freshly-built beginner has `./jichi` in the build folder, but the guides used
  bare `jichi`, so their very first command failed. `TUTORIAL_BEGINNER.md` §0
  now tells them to make `jichi` a real command (`sudo make install` or
  `export PATH`), or write the full path.
- **`TUTORIAL_BEGINNER.md` §0 pointed at `INSTALL.md`** (the requirements
  reference) instead of `PREPARE_AND_BUILD.md` (the from-nothing walkthrough) —
  repointed.
- **The per-terminal API-key trap** — `export KEY=…` dies with the terminal, a
  silent next-day 401. Callouts added in `TUTORIAL_BEGINNER.md` and
  `PREPARE_AND_BUILD.md` (re-export or add to `~/.bashrc`; never in the config).
- **`--config local/config.json` vs auto-load ambiguity** — dropped the flag in
  the beginner tutorial; it auto-loads from the project directory.
- **Critical-thinking framing** — an opening "you are the developer, not a
  spectator" box; "read the diff before you press `y`"; "spot-check a confident
  summary"; "read every `doctor` line"; a closing pointer to the curriculum.
- **Mermaid diagrams** (only 2 of ~50 docs had any):
  - `CURRICULUM.md` — the stage/gate/severability map (shu-ha-ri arc, module
    order, the two mid-course gates with their pass thresholds, M5 required),
    plus a one-sentence **shu-ha-ri gloss** (it read as decoration before).
  - `docs/assignments/INDEX.md` — the load→hint→attempt→grade→off task loop,
    and the shell-vs-TUI split made explicit (the `/`-commands go to the running
    agent, not the shell).
  - `PREPARE_AND_BUILD.md` — the key / own-key / no-key decision flowchart.
- **Factual bug** — `curriculum/04-debugging-as-science.md` cross-referenced
  "Module 7" for the failed-for-the-wrong-reason idea; it is **Module 3
  (assignment 07)**. Fixed.

## Remaining (prioritized, for the next pass)

Onboarding:
- `LOCAL_MODELS.md` — add numbered install + **model-download** prerequisites per
  backend (llama.cpp / Ollama / LocalAI); the serve commands assume the runtime
  and the multi-GB `.gguf` already exist. Gate the "no tool call at all" section
  as advanced. Add a backend-chooser diagram.
- `CONFIG_TUTORIAL.md` — a prerequisite banner ("you've built + done `setup`");
  replace `$EDITOR` with a concrete `nano …`; add the fallback+routing decision
  diagram.
- `PREPARE_AND_BUILD.md` — a build-order overview diagram; a sample `doctor`
  output block.

Curriculum **(done — second pass, commit below)**:
- **Systemic:** prev/next footer links added to all twelve module pages. The
  "This module, in order" checklist was **not** added as a separate block: each
  module already carries a numbered `## The work` list, and the
  gated-but-ungraded steps the reviewer named are already bolded *and* echoed in
  the stage-gate clause (M2 "**and** you have done the break-and-undo practice",
  M6 "**and** `/check` run on your document", M10 is authoring end to end) — a
  second checklist would duplicate, not clarify.
- Module 0 — the "one full turn" block is now split into an explicit **shell**
  block and a **TUI** block with a paragraph naming which prompt is which; the
  two-benches (`--preset learner` vs own-directory `cp -r … docs/assignments`)
  distinction is carried by `CURRICULUM.md` §"Getting started".
- `INSTRUCTOR.md` — stateDiagram of the authoring↔tutor stance flip added.
- Per-module diagrams added: M8 the autonomy-envelope + fix-forward loop; M4 the
  scientific-method cycle; `CURRICULUM.md` the grading floor/feedback/judgment
  flow. (D2, the RPN data-flow diagram in task 34, landed in the assignments
  commit.)

Assignments (systemic — a shared footer + runner guards):
- **S1** — the beginner C tasks (06/07/08, 14, 20/21/22) grade by compiling but
  never state they need `cc`, and their runners have **no toolchain guard** (a
  missing compiler looks like a test failure). Mirror the Racket runner's
  `command -v raco || fail`: add `command -v cc || fail` and a "Prerequisite:
  `cc`" line + INDEX prerequisite columns for Set A / Set D. *(Touches test.sh
  runners — re-run `curriculum_graders.py` after.)*
- **S2** — every spec's paths are project-root-relative but only INDEX says so;
  add a standard footer to each spec: run-from directory + both grade
  front-ends (`jichi grade …` / `/grade`) + the hint command.
- **S5** — "who does the work" drifts from "have the agent…" (00–05) to "Write
  X" (06+); make each spec explicit about hands-on vs agent-driven.
- **S6** — normalize the Racket set's difficulty labels (beginner/intermediate)
  onto the C set's scale (intro/easy/medium/hard).
- **D2** — a small data-flow diagram for the RPN capstone (34).

## Note

The remaining assignment-runner changes (S1) modify `test.sh` files that
`curriculum_graders.py` exercises, so that pass must re-run the full two-sided
grader proof. The first-pass fixes above are documentation-only and verified by
the `docs_flags` lint.
