# Review 5 — State around the loop: sessions, memory, control and snapshots

*Part of the 2026-10-08 review of jichi at master `17bbb895` (M913); start at
[Review 0](2026-10-08-review-00-overview.md). This chapter rests on the complete
report for `src/chat/` outside the agent loop, plus `src/session/`,
`src/snapshot/`, `src/command/` and `src/skill/`: 21 files read in full. Its six
code excerpts were checked line for line against the source, and its highest
finding was re-read in the source. Written by Claude Opus 5.5 on threadwork,
2026-10-08.*

---

## Who this is for, and what you will be able to do

**For** a self-learner or junior developer who wants to know where jichi keeps
what it knows between turns, between runs, and between people: the saved
conversation, the files injected into every prompt, the channels an operator can
use to steer a running agent, and the undo net.

**After reading it you will be able to:**

- name every place jichi keeps state, how each is written, and what each is lost
  to;
- read a session file and say what it does and does not carry;
- explain how a supervisor can steer a running jichi, and why that channel can
  never *widen* what the run may do;
- explain "preserve before destroy", and why it lives in a chokepoint.

**What you need first:** [Annai 7](../reading/annai-07-memory-the-jichi-way.md)
on memory, and [Fukabori 9](../reading/fukabori-09-sessions-snapshots-two-histories.md)
on sessions and snapshots.

---

## 1. Where jichi keeps state

| State | Where | Written | Lost to |
|---|---|---|---|
| the conversation (session) | `~/.jichi.d/sessions/<uuid>.json` | atomically, 0600, at the **end** of each top-level turn | a SIGKILL during a long turn (F6) |
| rules | `AGENTS.md`, else `CLAUDE.md`, per directory; `~/.config/jichi/AGENTS.md` | by the user | — (32 KB cap; head kept) |
| memory | `.jichi/memory.md` | by the `remember` tool, **non-atomically** (F11) | a rollback (F4) |
| glossary | `.jichi/glossary.md` | by the user | — (8 KB, tail kept) |
| authored constraints | `.jichi/constraints.md` | by the user and jichi, non-atomically | a rollback (F4); F2 |
| board | `.jichi/board.json` | atomically, with a lock | a rollback (F4) |
| checkpoints | a shadow git repository under `~/.jichi.d/checkpoints/` | before the first mutating tool of a turn | — (refs are pinned) |
| the journal, telemetry | `~/.jichi.d/runs/`, `~/.jichi.d/telemetry/` | append-only | — |

**What a reader should notice.**

- **Outside the workspace:** sessions, journals and checkpoints. They are
  atomic and owner-only.
- **Inside it:** the `.jichi/` stores. They sit inside the rollback net, so a
  verify rollback can revert a lesson the agent just recorded. Some are written
  non-atomically, so a crash can leave half a file.

A maintainer adding a new store has no written rule to follow. The review
suggests one sentence in ARCHITECTURE.md: *where state goes, and how it is
written*.

**The flow, per top-level turn:**

```
            per top-level turn                         on disk
 user msg --> @refs expand ----+                ~/.jichi.d/sessions/<id>.json
 /command --> command expand --+--> history ----> (saved at the turn's END)
                               |       ^
 system prompt = rules + memory + glossary + constraints + board + skills
          (built ONCE per turn)                    <cwd>/.jichi/*.md, *.json
                               |       |           (inside the rollback net)
 control socket --> "[operator] ..." user message at a tool boundary
 hooks --> block, or add context   snapshots: shadow repo, reset --hard + clean
```

**What a session file carries:** the id, the title, the workspace, the mode, the
whole history with tool calls and results, and the todo list. Compaction
summaries are carried *inside* the first kept user message.

**What it does not carry:**

- the system prompt, which is rebuilt every time;
- image bytes;
- the envelope: its budgets, its journal, the last green commit;
- constraints inferred from the request;
- token totals and the model;
- any link to a predecessor or successor session.

The last two matter for the [context relay](../proposals/2026-10-context-relay.md)
(its §8.5).

---

## 2. Steering a running agent

**The control socket** (`src/chat/jc_control.c`) accepts one JSON line per
connection. Its verbs:

| Verb | Effect |
|---|---|
| `status` | a snapshot: calls, tokens, budgets |
| `inject` | an `[operator] …` user message, delivered at the next tool boundary |
| `pause` / `resume` | holds the loop; `pause --extend` credits the paused time back to the deadline |
| `abort` | the graceful Ctrl-C path |
| `mode` | changes the mode, toward plan only |

**Its founding rule** (`include/jc_control.h:24-26`): *nothing on this channel
can widen what the run may do*. There is no approve verb and no budget or scope
change. `pause --extend` is the only thing allowed to stretch a deadline. The
rule is enforced by one predicate shared with the saved-mode and `/plan` paths
(§3.2).

**Hooks** (`src/chat/jc_hooks.c`) are shell commands run at SessionStart,
UserPromptSubmit, PreToolUse, PostToolUse and Stop. They can only block or add
context, and only at the top level.

**Snapshots.** Before the first mutating tool of a top-level turn, jichi commits
the workspace to a shadow repository whose work tree *is* the workspace. Undo,
rewind and rollback are a `reset --hard` plus a `clean`, and always preceded by
**preserve before destroy**: the state about to be lost is committed and pinned
under `refs/jichi/discarded/<why>/…` (§3.5).

---

## 3. Code to study

Six excerpts, each copied exactly from `17bbb895`.

### 3.1 Redact, replace atomically, then mark clean — `src/session/jc_session.c:437-458`

```c
    if (jc_redact_active()) {
        char *red = (char *)malloc(strlen(text) + 1);
        if (red != NULL) {
            jc_redact_secrets(text, red, (jc_size)strlen(text) + 1);
            free(text);
            text = red;
        }
    }
    /* M146: atomic replace -- an every-turn write that external readers (a
     * second jichi, a supervisor, a backup job) may race; they now see the old
     * session or the new one, never a torn file. mkstemp's 0600 carries over
     * (M132's owner-only requirement, guaranteed rather than patched after). */
    st = jc_write_file_atomic(path, text, strlen(text));
    free(text);
    if (st == JC_OK) {
        jc_make_private(path); /* belt-and-suspenders; mkstemp already 0600 */
        s->saved_gen = s->history.gen;
        s->saved_todos_gen = s->todos.gen;
        s->saved_mode = s->mode;
        s->meta_dirty = 0;
    }
    return st;
```

**What it teaches.** Three disciplines in twenty lines:

1. **Write to a temporary file, then rename it.** A reader sees the old file or
   the new one, never half of each.
2. **Mark the session clean only after the write succeeded.** A failed save is
   then retried next time instead of forgotten: the dirty flag is a promise about
   the disk, not about the attempt.
3. **The error is a value** the caller can act on.

**Ask critically:** what happens when that `malloc` fails? The unredacted text
is written. That is a *fail-open* branch, and every branch like it deserves a
reviewer's question. Here, should a failed redaction refuse the save instead?

### 3.2 An explicit order instead of enum arithmetic — `src/chat/jc_perm.c:75-91`

```c
int jc_perm_mode_rank(enum jc_agent_mode mode)
{
    /* An explicit table, not arithmetic on the enum: the enum is declared CHAT,
     * PLAN, AUTO, so any expression using its values directly would get the safety
     * ordering wrong in a way that reads as correct. */
    switch (mode) {
    case JC_MODE_AUTO: return 0;   /* widest: no approval prompts        */
    case JC_MODE_CHAT: return 1;   /* asks before mutating               */
    case JC_MODE_PLAN: return 2;   /* narrowest: read-only               */
    default:           return 1;   /* unknown: treat as the middle       */
    }
}

int jc_perm_mode_narrows(enum jc_agent_mode from, enum jc_agent_mode to)
{
    return jc_perm_mode_rank(to) > jc_perm_mode_rank(from) ? 1 : 0;
}
```

**What it teaches.** An enum's numbers are an accident of declaration order; a
safety order is a *decision*. Here the enum is declared CHAT, PLAN, AUTO, which
is not the order of permissiveness, so arithmetic on the values would be wrong
"in a way that reads as correct".

**The decision as a table** makes it:

- **reviewable:** you can see it;
- **testable:** all nine pairs are tested;
- **safe from reordering:** someone reordering the enum cannot change it.

The function's name states the question ("does this narrow?"), so every call
site reads as policy.

### 3.3 One convention for text that arrives mid-turn — `src/chat/jc_message.c:105-121`

```c
struct jc_message *jc_history_add_operator(struct jc_history *h,
                                           const char *text)
{
    struct jc_sb msg;
    struct jc_message *m = NULL;
    if (h == NULL || text == NULL || text[0] == '\0') {
        return NULL;
    }
    jc_sb_init(&msg);
    jc_sb_append(&msg, "[operator] ");
    jc_sb_append(&msg, text);
    if (msg.data != NULL) {
        m = jc_history_add(h, JC_ROLE_USER, msg.data);
    }
    jc_sb_free(&msg);
    return m;
}
```

**What it teaches.** Two producers, the control socket and the TUI's type-ahead
queue, needed the same shape for text that arrives while the agent works. The
shape lives in one function instead of two copies that could drift apart.

- **Ownership is simple:** the buffer is freed here, and the history copies the
  text.
- **The `[operator]` prefix** makes steering visible to the model and in
  transcripts.
- **It appends a user message and never edits the system prompt,** so the cached
  prompt prefix stays byte-identical.

The relay proposal's notes seed is meant to use exactly this kind of helper.

### 3.4 An allow-list before a path is built — `src/session/jc_session.c:1030-1041`

```c
static int id_is_plain(const char *id)
{
    jc_size i;
    if (id == NULL) return 0;
    for (i = 0; id[i] != '\0'; i++) {
        char c = id[i];
        int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return 0;
    }
    return i > 0;
}
```

**What it teaches.** A session id typed by a user goes into
`~/.jichi.d/sessions/%s.json`. An id containing `/` or `..` could otherwise walk
out of the directory (path traversal).

The check is an **allow-list** (what is permitted), not a deny-list of known-bad
characters, which is the robust direction. It uses explicit character ranges
rather than `isalnum`, so a locale cannot widen the accepted set. And it rejects
the empty string.

### 3.5 Preserve before destroy, at the chokepoint — `src/snapshot/jc_snapshot.c:660-684`

```c
    if (m == NULL) {
        return;
    }
    m->preserve_last[0] = '\0';   /* each restore reports only its own */
    if (!m->preserve) {
        return;
    }
    if (m->preserve_skip_once) {
        /* The caller wrote a richer record for this same state already. */
        m->preserve_skip_once = 0;
        return;
    }
    m->preserve_n++;
    jc_snprintf(ref, sizeof(ref), "refs/jichi/discarded/%s/%lu-%d", why,
                (unsigned long)jc_now_seconds(), m->preserve_n);
    jc_snprintf(label, sizeof(label),
                "discarded by %s\n\nmechanism: %s\nworkspace: %s",
                why, why, m->work_tree);
    if (jc_snapshot_preserve(m, label, ref, sha, sizeof(sha)) == JC_OK
            && sha[0] != '\0') {
        jc_snprintf(m->preserve_last, sizeof(m->preserve_last), "%s", sha);
        jc_logf(JC_LOG_INFO, "snapshots: preserved the state %s is discarding "
                "at %.12s", why, sha);
    }
}
```

**What it teaches.**

- **Where it sits.** Every destructive restore calls this before `reset --hard`,
  so a sixth destructive caller added next year is safe by default. *A call site
  can be forgotten; a chokepoint cannot.*
- **The order.** The ref is written *before* the destruction, because after the
  reset the commit is reachable only through it.
- **The name.** The ref's name carries the time and a counter, so refs from
  different sessions never overwrite each other.
- **A stated trade-off.** If preservation fails, the restore the user asked for
  still happens.

### 3.6 Roll back a half-done switch — `src/chat/jc_app.c:1091-1115`

```c
jc_status jc_app_switch_model(struct jc_app *app, int i)
{
    struct jc_provider *p;
    int prev = app->config.active;

    if (jc_config_set_active(&app->config, i) != JC_OK) {
        return JC_ERR_INVALID;
    }
    p = jc_provider_create(&app->config.model);
    if (p == NULL) {
        /* M718: PUT THE PREVIOUS ENTRY BACK. The old provider reads the active
         * entry through a pointer to config.model, so returning with the new
         * entry active would send the OLD dialect to the NEW entry's endpoint
         * with its key -- a dialect nobody chose for it. Latent while creation
         * failed only on OOM; reachable once an entry naming no provider is
         * refused rather than guessed. */
        (void)jc_config_set_active(&app->config, prev);
        return JC_ERR_INVALID;
    }
    if (app->provider != NULL) {
        app->provider->vt->free(app->provider);
    }
    app->provider = p;
    return JC_OK;
}
```

**What it teaches.** A change in two steps, first the config and then the
provider, must leave the system consistent when the second step fails.

- **Undo the first step on failure.** If the provider cannot be created, the
  previous config entry is put back.
- **Acquire before release.** The old provider is freed only after the new one
  exists.

**Why it matters here.** The comment names the aliasing: the provider holds a
pointer *into* `config.model`. A half-switched config would silently change what
the *old* provider talks to: the old dialect, sent to the new endpoint, with the
new entry's key.

---

## 4. What the review found

Fifteen findings: one high, seven medium, six low, plus notes. Status is the
reviewer's.

### High

- **F1 — a secret registered for redaction can be lost within one turn**
  (security class; verified by the reviewer, and re-read in the source by the
  author of this chapter). A secret alias resolved by an `@ref:` is registered
  for redaction from the per-turn scratch arena. The registry's contract says
  the pointer must outlive the process (`include/jc_log.h:50-53`). The arena is
  reset at the start of the next turn, after which the secret is no longer
  reliably redacted. The trigger is narrow; the mechanism is kept with the
  maintainers.

### Medium

- **F2** — only the first `note` line of `.jichi/constraints.md` is loaded, and
  the next save **erases** the others from the file.
- **F3** — `jc_snapshot_changed_since` returns OK when git itself failed, and
  the edit-scope sweep then reports "nothing changed outside the scope". This is
  the false-success theme of [Review 0](2026-10-08-review-00-overview.md) §5.1.
- **F4** — durable state (memory, board, constraints, plan) lives in
  `.jichi/`, inside the rollback net. A verify rollback discards new lessons
  without preserving them.
- **F5** — a hook's `session_id` is the `--session` argument from the command
  line, not the live session's id.
- **F6** — a session is saved only at the end of a top-level turn, so a long
  single-turn `--auto` run loses its whole history to a SIGKILL or the OOM
  killer. No DEFERRED row records this.
- **F7** — with no `.git`, the rules loader climbs to `/` and reads every
  ancestor's `AGENTS.md` or `CLAUDE.md`, including one any local user could have
  written in `/tmp` (security class).
- **F8** — an authored `deny-cmd commit` does not refuse the `git_commit` tool.

### Low (selected)

- **F9** — the constraint parser drops the documented `; text` and cuts lines at
  511 bytes.
- **F11** — memory and constraints are rewritten non-atomically.
- **F13** — the control channel's `mode` verb is applied in the middle of a
  call, though its docs say it is not.
- **F14** — MEMORY.md says a remembered note is visible for the rest of the
  current turn. It is not: the system prompt is built once per turn.

### What is good, said plainly

The reviewer singles out:

- the session codec: streamed, atomic, 0600, redacted, warns rather than
  refuses on a newer format;
- the allocation-free listing scanner;
- the board's lock, reload, apply, save cycle, which refuses to overwrite a
  damaged file;
- the wire-shape validator `jc_history_check`;
- the mode-rank table;
- the preserve-before-destroy chokepoints.

Each is careful, and each records the incident that motivated it.

---

## 5. The design, seen whole

**What the reviewer would refactor first, with the cost:**

1. **`jc_constraint.c` is two modules in one file of 2,549 lines.** About 2,000
   lines guess at natural language (the scanner, extended milestone by
   milestone), and about 300 are the binding part: the store, the blocking
   check, the judgement. Since M734 only the small part refuses anything, and F2,
   F8 and F9 all sit in it, easy to miss beside the heuristics. Splitting it puts
   the fence on one screen. About a day; no behaviour change.
2. **`jc_app.c` mixes eight concerns:** mode switching, the path fence, file I/O,
   images, two shell runners, model switching, the constraint store and the
   design loader. The shell runner is the most security-relevant part and
   deserves its own file. Half a day.

**Three observations that teach something general:**

- **Results passed by convention.** About 40 "read right after the run" fields
  in the shared `struct jc_app` are saved and restored by hand around every tool
  call. One `struct jc_run_result` would make that a struct copy, and every new
  field safe by construction. [Review 1](2026-10-08-review-01-the-agent-loop.md)
  §6 reaches the same conclusion from the other side.
- **Four copies of one loader.** Commands, agent profiles, output styles and
  skills each have their own loader with the same three functions. One shared
  loader would give one place for the regular-file check and a size limit; today
  each reads up to 64 MB onto the session arena.
- **Coupled size limits.** `app->cwd` holds 1,024 bytes while `app->root` holds
  4,096, and session paths use 1,100-byte buffers. These work only because the
  limits happen to line up. One `JC_PATH_MAX` would remove a class of silent
  truncation.

---

## 6. What this means for the context relay

The reviewer's map for the relay
([proposal](../proposals/2026-10-context-relay.md) §6.6–6.7):

- **memory.md is a poor home for relay notes.** It is project-wide, loads only
  its tail, sits inside the rollback net (F4) and is written non-atomically
  (F11). Hence the proposal's notes directory under `~/.jichi.d/`, written
  atomically.
- **The notes belong in the successor's first user message,** as with §3.3's
  helper, not in the system prompt, which is built once per turn and heads the
  cached prefix.
- **AUTO mode scans every user message for prohibitions.** A seed made of notes
  would produce inferred rules, so the proposal exempts it from the scan.
- **There is no "hand over now" verb,** and `status` reports no context
  pressure. The proposal adds both, within the never-widen rule.
- **A fixed `--control` path has an unlink race** between a predecessor and its
  successor. The predecessor must close its socket first.
- **F6 matters most:** a long leg must not lose its history to a crash. It is on
  the proposal's list of prerequisites.

---

## 7. Something to do

**Without a bench (the read-only twin):**

1. Draw §1's table again, adding a column "survives a verify rollback?". Which
   rows say no? Which of those would you move, and where?
2. In §3.2, write the nine (from, to) pairs and mark which ones `narrows`
   returns 1 for. Then reorder the enum to AUTO, CHAT, PLAN and check that
   nothing changes. That is the point of the table.
3. In §3.1, decide what the save should do when `malloc` fails: write
   unredacted, refuse, or retry. Write down the argument for your choice.

**With a bench:** look at a real session file. `jichi ls --all` only reads; it
lists your saved sessions, and inside a project `jichi ls` lists that project's
own. Open the newest file under `~/.jichi.d/sessions/` in a viewer.

Its top-level keys, read from a session saved by `17bbb895` on 2026-10-08, are
`history`, `jichi`, `mode`, `sessionId`, `title`, `todos`, `v` and
`workspaceDirectory`; an `alias` appears when one is set. Confirm that nothing in
it names a budget or a predecessor.

## 8. Read further

- [MEMORY.md](../MEMORY.md), [CONTROL.md](../CONTROL.md),
  [SNAPSHOTS.md](../SNAPSHOTS.md), [REWIND.md](../REWIND.md),
  [HOOKS.md](../HOOKS.md): the references for this chapter. *Primary for jichi.*
- [Fukabori 9](../reading/fukabori-09-sessions-snapshots-two-histories.md): the
  two histories, the conversation and the files, and why both are kept.
  *Primary for jichi.*
- The POSIX specification of `rename()`: the atomic replacement §3.1 relies on,
  within one file system. *Primary.*
- OWASP, "Path Traversal": the attack §3.4 prevents. *Secondary.*
