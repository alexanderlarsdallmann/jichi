# Glossary — the feature for your project's terms

> **Looking for what *jichi's own* words mean** — posture, fence, envelope, green,
> TAP, hint ladder? That is [VOCABULARY.md](VOCABULARY.md). **This** page documents
> a feature: a file you write so the agent speaks *your* project's vocabulary.
> (The name is a trap, and this is the sign in front of it.)

A glossary is a plain-markdown list of **domain terms and their definitions**
that jichi injects into the system prompt, so the agent speaks your project's
vocabulary — the names of your subsystems, acronyms, house conventions, the
difference between a "job" and a "task" in *your* codebase.

It complements the other context sources:

- **rules** (`AGENTS.md`) — how to *behave*.
- **memory** (`.jichi/memory.md`) — durable notes the agent *learned*.
- **glossary** (`.jichi/glossary.md`) — *reference* vocabulary you maintain.

## Where it lives

Two files are loaded and concatenated (global first):

| Location | Scope |
| --- | --- |
| `~/.config/jichi/glossary.md` | house-wide terms shared across projects |
| `<workspace>/.jichi/glossary.md` | terms specific to this project |

Both are optional; with neither present the feature is inert (nothing is
injected). The combined text is bounded (8 KB, tail-kept) so a large glossary
can't crowd out the conversation.

A **starter glossary of jichi's own terms** (turn, checkpoint, envelope,
fence, hint ladder, …) ships with the `assignments` scaffold pack (M175):
`init assignments` — and therefore the `learner`/`instructor` setup presets —
installs it at `.jichi/glossary.md`, so a learner's agent can define the words
the curriculum uses — and since M603 the **`default` pack** ships it too, because
the `/learn` mentor now reads it: `learn.md` inlines `.jichi/glossary.md` (through
a shell block, so a project without one gets nothing rather than a
"could not read" note), and the lessons it drafts use the words the notes and the
`learn analyze` report use, instead of paying the private-vocabulary tax blind.
It is a normal glossary file afterwards: edit it, replace
it with your domain's terms, or delete it.

## Format

Just markdown — write it however reads well. A term/definition list is typical:

```markdown
# Glossary

- **Frob**: the core scheduling unit; one frob owns one work queue.
- **Reaper**: the GC pass that retires drained frobs (see src/reaper.c).
- **NRT**: "near-real-time" — our 250 ms p99 budget, not hard-real-time.
```

It is injected verbatim under a `# Glossary` heading. The user edits the files
directly; jichi never writes them (unlike memory, which the `remember` tool
appends to).

## Inspecting it

```sh
jichi glossary    # print the merged glossary (global + project)
jichi sysmsg      # show the full system prompt, glossary included
```

## Internals

- **`jc_glossary_load(app)`** (`src/chat/jc_glossary.c`) — reads the global +
  project files into one arena-owned string (global first, blank-line
  separated), bounded to `JC_GLOSSARY_MAX`; NULL when both are absent/empty.
- It is loaded into `app->glossary` at startup and injected by `jc_sysmsg_build`
  after the remembered notes, before the repository map.
- Tests: `tests/test_glossary.c` (absent → NULL; a project file loads).
