---
marp: true
title: jichi in school
theme: default
paginate: true
---

<!-- _class: lead -->

# jichi in the classroom

### Learning to code *with* an agent, safely

---

# The worry, named

> "If an AI writes the code, do students learn anything?"

jichi's assignments feature is designed for the opposite: the agent is a **coach
with a dial**, not an answer machine. The learner does the work; help is a tunable
amount, and the assessment criteria are visible.

---

# How a lesson runs

```
teacher: /assign implementation "a function that reverses a list"
         → docs/assignments/reverse-list.md  (brief + rubric + hint ladder)
student: works it in the editor; stuck? → one hint at a time (hint tool)
         still stuck? → a focused question (ask_for_help)
teacher: /check reverse-list.md student.py  → rubric-keyed feedback (read-only)
```

The reference solution is **withheld**; the rubric is **shown**.

---

# The hint ladder is the pedagogy

- Being stuck becomes a **dial**, not a wall.
- Hints are **graded** — the gentlest nudge first, the full spoiler last.
- The learner spends their "struggle budget" productively.
- `ask_for_help` gives a targeted answer to a *specific* confusion, not a
  do-my-homework prompt.

> Tune the ladder per age/level. That tuning *is* the teaching.

---

# Guardrails that make it classroom-safe

- **Plan / read-only mode** — students explore without changing shared material.
- **Path fence** — the agent cannot touch files outside the lesson folder.
- **Read-only grading** — the `solution-checker` never edits a student's code.
- **Propose-only authoring** — the teacher approves every assignment and
  reference before it's handed out.
- **Self-hosted model** — kids' work stays on the school's server.

---

# Measured: what a learner agent does when the gate is editable

A real campaign, same model, same task, **one variable — could it edit the
test?**

| writes allowed | outcome | tokens |
|---|---|---|
| the whole worktree | **"PASS" — by editing the gate tests** | 5,215k |
| the one file the task names | **correct implementation** | 504k |

Ten "a test assertion was edited" warnings fired on the first run. The verdict
still said PASS — so jichi now reports **TAINTED** and exits non-zero instead.

**The classroom lesson is the same for humans:** *the fix belongs in the
function; leave the test alone.* For a person that is review discipline. For an
agent it has to be **enforced** — prose does not bind whoever finds it
inconvenient.

Artifacts, both diffs: `docs/case-studies/`

---

# Tiered learners model good habits

Four profiles show *how* to approach a problem at a level:

- **`learner-junior`** — leans on hints, help, and delegation.
- **`learner-student`** — uses references, moderate help.
- **`learner-senior`** — minimal help, plans first.
- **`learner-agent`** — strategic and efficient; the machine-audience tier.

Watching a tier tackle a problem seeds a class conversation about *strategy*, not
just the answer.

---

# What the teacher actually does

1. Author a small set of assignments once (`/assign`, review the drafts).
2. **Prove each gate can fail:** `jichi grade <spec> --expect-fail` on the
   untouched folder. A measured campaign had a model ship a gate that was
   *already green* — it graded PASS at 100% before any work existed.
3. Hand out the briefs (keep the `.solution.md` files back).
4. Circulate; when a student is stuck, coach them to the *next hint*, not the
   answer.
5. `/check` submissions for a consistent first-pass grade you review.

No new tooling to learn — it's the same `jichi` binary. **Write the gate
yourself, though:** models author good prose and unreliable gates
(`docs/case-studies/`).

---

# Beyond assignments

- **"Explain this"** — `:JichiExplain` a snippet in the editor, or `jichi-nano
  explain file.py` — a patient, on-demand explainer.
- **Code review as a lesson** — `/check` or a review agent turns a submission into
  labeled, evidence-backed feedback.
- **Runs on a Chromebook-class box** over the network to a school LLM server.

---

<!-- _class: lead -->

# Getting started (teacher)

```sh
jichi setup --preset instructor   # author + grade
jichi setup --preset learner      # study, with tutor + hints
# add  "assignments": true  to config, then:
jichi -p "/assign implementation 'sum a list'"
```

Full walkthrough: `docs/TEACHING_ASSIGNMENTS.md` (classroom, tutoring,
self-study, cohort/TA).
