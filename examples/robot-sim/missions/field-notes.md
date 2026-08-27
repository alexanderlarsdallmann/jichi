Field-notes pattern (on-system learning). After a patrol, append one JSONL line
to `field-notes.jsonl` for each notable event — `{"ts":...,"event":"obstacle",
"pose_x":...}` — using write_file/edit_file. Over many runs this file becomes a
data set:

  * point a docs source at it (`docs: [{ "name": "field", "path":
    "field-notes.jsonl" }]`) so `search_docs` / `@docs:field` recall past
    situations, and
  * run `jichi learn analyze` over the run journals + telemetry to draft
    propose-only lessons; review, then `learn apply` to commit memory/skills
    that steer the next patrol.

The robot corrects itself the same way the coding agent does — from its own
logs, human-gated. See docs/ROBOTICS.md and docs/LEARNING.md.
