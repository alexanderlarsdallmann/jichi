/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_session.h - chat session persistence.
 *
 * Sessions are stored as JSON under ~/.jichi.d/sessions/<id>.json, holding the
 * session id, title, workspace directory, and the message history. One file per
 * session, which keeps a save an atomic single-file write.
 *
 * They lived under ~/.continue/sessions until M204 -- a leftover from jichi's
 * origin as a from-scratch reimplementation intended to sit beside Continue CLI.
 * The M170 rename moved every other path and missed this one; see the note on
 * sessions_dir in jc_session.c.
 */
#ifndef JC_SESSION_H
#define JC_SESSION_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_message.h"
#include "jc_str.h"
#include "jc_todo.h"

/* M606: the store's shape version, written as "v". Absent means 1 (every
 * session saved before M606). A file whose v is NEWER than this build's is
 * still loaded -- refusing would lose the user's conversation, which is not
 * regenerable the way a calibration file is -- with one warning that the
 * fields the newer build added are not restored. v2 added "todos". */
#define JC_SESSION_STORE_V 2

struct jc_session {
    char             *id;        /* uuid */
    char             *title;     /* first user message, truncated */
    char             *alias;     /* optional short quick-find name (M108), or NULL */
    char             *workspace; /* cwd at creation */
    int               mode;      /* enum jc_agent_mode active when last saved */
    struct jc_history history;
    /* M606: the task list (todowrite/todoread) BELONGS TO THE SESSION. It
     * lived on jc_app alone until then -- no codec saved it, no resume
     * restored it, no session switch cleared it -- while `todowrite` was the
     * tool the models called most (348 of ~408 tool events in one telemetry
     * log). app->todos points here while this session is live. */
    struct jc_todo_list todos;
    struct jc_arena  *arena;     /* for id/title/workspace strings */
    /* M218 dirty tracking: jc_session_save skips the write (a full history
     * re-serialize, 2-3x the session text) when nothing changed since the
     * last save -- several TUI sites can save at one turn boundary. */
    unsigned long     saved_gen; /* history.gen at the last successful save */
    unsigned long     saved_todos_gen; /* todos.gen at the last save (M606) */
    int               saved_mode;/* mode at the last successful save */
    int               meta_dirty;/* title/alias (or never-saved) changed */
};

/* Brief metadata for listing. */
struct jc_session_meta {
    char  *id;
    char  *title;
    char  *alias;     /* optional quick-find name (M108), or NULL              */
    char  *workspace; /* workspaceDirectory at last save (for scoping/display) */
    int    nmsgs;     /* stored (non-system) message count                    */
    double mtime;
};

/* Outcome of jc_session_open. */
enum jc_session_open_result {
    JC_SESSION_OPENED,    /* resumed an existing session into *s            */
    JC_SESSION_CREATED,   /* started a fresh session in *s                  */
    JC_SESSION_NONE,      /* an explicit id/prefix matched nothing (*s unset)*/
    JC_SESSION_AMBIGUOUS  /* an explicit prefix matched several (*s unset)   */
};

/* Initialise a fresh session with a new id and the given workspace. */
jc_status jc_session_new(struct jc_session *s, const char *workspace,
                         struct jc_arena *a);

/* Fork `src` into `dst`: a new session id, the same workspace/mode, the title
 * with a " (fork)" marker, and a deep copy of the conversation history (messages
 * + tool calls/results + image attachments) and of the task list (M606). `dst`
 * is independent of `src`, so continuing one leaves the other untouched. Backs
 * the TUI `/fork` (M35b). */
jc_status jc_session_fork(struct jc_session *src, struct jc_session *dst,
                          struct jc_arena *a);

void jc_session_free(struct jc_session *s);

/* Persist the session to its JSON file (creating the sessions dir). */
jc_status jc_session_save(struct jc_session *s);

/* M232: serialize the session to a heap JSON string (caller frees), streaming
 * the history one message at a time so the whole-history cJSON tree never
 * exists at once. Byte-identical to a whole-tree cJSON_PrintUnformatted.
 * Returns NULL on OOM. This is what jc_session_save writes. */
char *jc_session_serialize(struct jc_session *s);

/* M218: 1 when something (history gen, mode, title/alias) changed since the
 * last successful save -- jc_session_save early-returns JC_OK when 0. Pure. */
int jc_session_needs_save(const struct jc_session *s);

/* M219: session-store hygiene (the rotation deferred at M197 -- retention is
 * fixed, but a 480 MB store still costs seconds of parsing per listing). */

/* Mark which of `metas[0..n)` (jc_session_list order: newest-first by mtime)
 * the prune should delete: keep the `keep` newest (keep < 0 = no keep
 * criterion) and/or those older than `cutoff` (an mtime; < 0 = no age
 * criterion). When BOTH are given a session must satisfy both to be deleted
 * (AND -- pruning is destructive, so the conservative combination); with
 * neither, nothing is selected. del[i] is set 0/1; returns the number
 * selected. Pure; unit-tested. */
jc_size jc_session_prune_select(const struct jc_session_meta *metas, jc_size n,
                                long keep, double cutoff, int *del);

/* Total file count and byte size of the session store (0/0 when absent).
 * Backs the doctor's store-size warning. */
void jc_session_store_stats(long *files_out, double *bytes_out);

/* Load a session by id. */
jc_status jc_session_load_by_id(const char *id, struct jc_session *s,
                                struct jc_arena *a);

/* Load the most-recently-modified session (for --resume). */
jc_status jc_session_load_recent(struct jc_session *s, struct jc_arena *a);

/* Like jc_session_load_recent but only consider sessions whose workspace equals
 * `workspace` (NULL => any workspace). */
jc_status jc_session_load_recent_scoped(const char *workspace,
                                        struct jc_session *s,
                                        struct jc_arena *a);

/* As jc_session_load_recent_scoped, but skip `exclude_id` (NULL => skip none).
 *
 * M198: the TUI's bare `/resume` saves the live session first, which sets its
 * mtime to now -- so a plain newest-first lookup ALWAYS returned the session the
 * user was already in, making bare /resume a silent no-op and the
 * "(no earlier session...)" branch unreachable. Excluding the live id makes
 * `/resume` mean "the previous one", which is what users expect.
 * See docs/proposals/2026-07-robustness-edge-cases.md (other findings). */
jc_status jc_session_load_recent_scoped_ex(const char *workspace,
                                           const char *exclude_id,
                                           struct jc_session *s,
                                           struct jc_arena *a);

/* Append metadata for every stored session to `out` (a jc_vec of
 * struct jc_session_meta), sorted newest-first. */
jc_status jc_session_list(struct jc_vec *out, struct jc_arena *a);

/* As jc_session_list, but also reports how many entries LOOKED like sessions
 * (a `*.json` name in the store) yet could not be listed -- unreadable, over the
 * 64 MiB read cap, corrupt JSON, or not a regular file.
 *
 * M198: this count is the difference between degrading and degrading SILENTLY.
 * Before it existed, an allocation failure, an I/O error, or a session larger
 * than JC_READ_FILE_MAX simply dropped that session from `/sessions` and `ls`
 * with a success exit code -- the user's sessions appeared to have vanished with
 * no diagnostic anywhere. `skipped` may be NULL if the caller does not care.
 * See docs/proposals/2026-07-robustness-edge-cases.md (#4, #7). */
jc_status jc_session_list_ex(struct jc_vec *out, struct jc_arena *a,
                             int *skipped);

/* Resolve a session id or unambiguous id-prefix to a full id in `out`.
 * Returns 0 on a unique match, -1 when none match, -2 when ambiguous. */
int jc_session_resolve_prefix(const char *prefix, char *out, jc_size cap,
                              struct jc_arena *a);

/* Resolve a session quick-find alias (exact, case-sensitive) to a full id in
 * `out` (M108). Returns 0 on a unique match, -1 when none, -2 when ambiguous. */
int jc_session_resolve_alias(const char *alias, char *out, jc_size cap,
                             struct jc_arena *a);

/* Delete the stored session file for `id`. JC_OK on success (or if already
 * gone), JC_ERR_* otherwise. Does not touch checkpoints (those are separate). */
jc_status jc_session_delete(const char *id);

/* True when `alias` is a valid quick-find name: non-empty, <= 64 chars, and
 * only [A-Za-z0-9._-] (no spaces/slashes, so it can't collide with a path or an
 * id-prefix ambiguity). Pure; unit-tested (M108). */
int jc_session_alias_valid(const char *alias);

/* Set (override) the session's quick-find alias (validated by the caller). */
void jc_session_set_alias(struct jc_session *s, const char *alias);

/* Resolve which session to use and load/create it into *s. With a non-empty
 * `id_or_prefix`, resume that one (else JC_SESSION_NONE/_AMBIGUOUS, *s left
 * unset). Otherwise, when `resume_recent`, resume the most recent session
 * matching `scope_ws` (NULL => any); failing that, or when not resuming, start
 * a fresh session in `new_ws`. */
enum jc_session_open_result jc_session_open(struct jc_session *s,
                                            const char *id_or_prefix,
                                            int resume_recent,
                                            const char *scope_ws,
                                            const char *new_ws,
                                            struct jc_arena *a);

/* Set the session title from the first user message if not already set. */
void jc_session_autotitle(struct jc_session *s);

/* Set (override) the session title. */
void jc_session_set_title(struct jc_session *s, const char *title);

/* M350: the file paths this conversation's SUCCESSFUL file-tool calls
 * touched (read_file/write_file/edit_file `path`, apply_patch
 * `edits[].path`), deduplicated, spelled as the model spelled them. Errored
 * calls are excluded -- a failed write left no belief worth checking.
 * Bounded to the first 128 distinct paths. Out receives malloc'd char*
 * entries (caller frees each, then the vec). Pure; unit-tested. */
void jc_session_believed_paths(const struct jc_history *hist,
                               struct jc_vec *out);

/* M350: which believed paths moved while the conversation slept -- strictly
 * newer mtime than the session's own file (its last save), or gone from disk
 * (both mean earlier tool results may describe stale content). Relative
 * paths resolve against the session's workspace. Appends the drifted names
 * newline-separated. JC_ERR_NOTFOUND when the session has no on-disk file
 * yet: a never-saved session has no "last ran" to drift from. */
jc_status jc_session_drift_names(const struct jc_session *s,
                                 struct jc_sb *out);

/* M350: render the [resume] note a resumed conversation gets when files it
 * worked with changed on disk in the meantime -- a human edit, a git pull, a
 * CLI `undo`. The restored history is full of tool results describing the
 * files as they WERE; the model deserves the same courtesy the /undo notice
 * gives it (M349): name what moved, ask for a re-read. At most 8 names, then
 * "+K more". Appends NOTHING when `names` is empty -- an unchanged workspace
 * needs no note. Pure; unit-tested. */
void jc_session_drift_render(const char *names, struct jc_sb *out);

/* Export format for jc_session_render (M34b; M165 adds JSON). */
enum jc_export_format {
    JC_EXPORT_MD,   /* GitHub-flavored Markdown transcript          */
    JC_EXPORT_HTML, /* a self-contained, styled HTML document       */
    JC_EXPORT_JSON  /* a structured transcript projection (M165)    */
};

/* Pure: render the session's transcript (user/assistant/tool messages; the
 * system prompt is omitted) to `out` (an initialised jc_sb) in `format`. No
 * I/O. Always JC_OK. The JSON form is a machine projection a supervisor reads
 * instead of replaying a session: {"v":1,"id","title","workspace","mode",
 * "messages":[{role, content?, tool_calls?:[{id,name,arguments}], tool_call_id?,
 * is_error?}...]}. */
jc_status jc_session_render(struct jc_session *s, int format,
                            struct jc_sb *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_SESSION_H */
