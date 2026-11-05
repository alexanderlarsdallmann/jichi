/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_handoff.c - the handoff section (M1058, H4 batch): the C89 port
 * of the drive's handoff.py -- what the previous run left the next one, built
 * from the previous run's OWN records (its stream.jsonl, its meta.txt, its
 * filed notes). The jichi-harness drive of 2026-10-23.
 *
 * Design note (the contract -- the fourteen numbered behaviours and the six
 * CHANGED lines): docs/internal/drive/2026-10-23-harness/notes/M1058.md.
 * Reference (read line by line; its comments name the incidents):
 * docs/internal/drive/2026-10-23-harness/harness/handoff.py.
 *
 * THE FAIL-OPEN CONTRACT (behaviour 1, CHANGED 5): the build returns JC_OK
 * with an EMPTY section on every drop condition -- a missing or unreadable
 * stream.jsonl (the reference's events() open), a notes directory that fails
 * to list, an out-of-memory build -- and the reason goes to the caller's
 * err_out in the reference's filed shape ("handoff: <reason>" + newline; the
 * reference's "handoff.py: <ExcType>: <msg>" keeps its observable: the
 * reason). A run must start without the section rather than not start: the
 * child's rc is the run's rc, with or without the section. What does NOT
 * drop the section: an unreadable meta.txt (an EMPTY meta -- the ended()
 * fields fall to ?/omit, behaviour 4/13) and an unreadable NOTE
 * ("(unreadable)", behaviour 9, SV:0319.4).
 *
 * THE CAP (behaviour 10): the finished section, when it exceeds 6000 BYTES,
 * is cut at the byte boundary (a partial UTF-8 sequence at the cut is
 * dropped -- the reference's decode("utf-8","ignore")), back to the last
 * newline before the cut, with the marker line appended. The section is
 * prompt BUDGET: a section that eats the model's context to report on the
 * previous run is worse than no section.
 *
 * The one model-written part of the section (the previous run's last
 * message) is QUOTED as such in the header's anti-hallucination guard
 * (behaviour 3, docstring 10-12: "It cannot invent a fact, but it can be
 * stale"). Everything else is mechanical, read from the records.
 *
 * This TU owns its memory with a LOCAL arena (freed before the return): the
 * caller's out_buf holds the section, and nothing the arena held outlives
 * the call. It reads NO git and NO network (behaviour 14).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "jc_platform.h"   /* jc_status, jc_read_file, jc_file_exists */
#include "jc_mem.h"        /* struct jc_arena, jc_arena_strdup */
#include "jc_str.h"        /* struct jc_sb */
#include "jc_vec.h"        /* struct jc_vec */
#include "jc_snprintf.h"   /* jc_snprintf, never sprintf */
#include "cJSON.h"
#include "jc_harness_handoff.h"

/* The reference's constants (handoff.py 23-33). */
#define HANDOFF_MAX_BYTES 6000
#define HANDOFF_TOP_FILES 25
#define HANDOFF_NOTE_CHARS 500
#define HANDOFF_MAX_NOTES 4
#define HANDOFF_MAX_TODOS 15
#define HANDOFF_MAX_TASKS 8

/* The reference's two tool tables (handoff.py 31-33). delete_file and
 * move_file are NOT jichi tools (SV:0319.1, verified: absent from
 * JC_ALL_TOOL_NAMES, src/tools/jc_tool.c, and from src/ entirely); the port
 * copies them as DEAD entries, named as such -- a table entry that never
 * fires is a faithful copy of the reference's curation, and a future jichi
 * that adds the tool needs no table change. */
static const char *const HANDOFF_PATH_TOOLS[] = {
    "read_file", "edit_file", "write_file", "apply_patch",
    "list_files", "search_code", "delete_file", "move_file"
};
static const char *const HANDOFF_EDIT_TOOLS[] = {
    "edit_file", "write_file", "apply_patch", "delete_file", "move_file"
};

/* A path entry of the file table: its read and changed counters, plus a
 * name (arena). The table is a linear vec (a run's stream names a bounded
 * few paths; the top-25 cut is after the sort). */
struct hh_path {
    const char *name;
    int reads;
    int edits;
};

/* One spawn_parallel call, in stream order (behaviour 8): its args (the JSON
 * string, arena, parsed a second time at build time) and its id (the
 * tool_result pairing key, arena; NULL when the provider sent none). */
struct hh_batch {
    const char *args;
    const char *id;
};

/* The build state gathered over the previous run's records. */
struct hh {
    struct jc_arena *a;
    /* meta.txt values ("" when the key is absent or the meta unreadable). */
    const char *rc;
    const char *start;
    const char *end;
    const char *team_head_after;
    /* done[-1].text (SV:0319.2: the LAST done event ALONE, no backward
     * scan); "" when there is none or its text is absent/empty. */
    const char *last_msg;
    /* The last todowrite's args (the JSON string; parsed a second time at
     * build time, behaviour 6); NULL when there was no todowrite. */
    const char *todo_args;
    /* Per-path file counters (behaviour 7). */
    struct jc_vec paths;
    /* The spawn_parallel calls, in stream order (behaviour 8). */
    struct jc_vec batches;
    /* The tool_result previews, by id, LAST WINS (the reference's dict
     * comprehension over the stream); NULL id entries are not stored. */
    struct jc_vec res_ids;
    struct jc_vec res_prevs;
    /* notes/ names (byte-sorted); an empty vec is the no-notes case, and a
     * notes directory that FAILS to list is a drop (the reference's events
     * class, behaviour 1) -- signalled through hh_notes_failed. */
    struct jc_vec notes;
    int hh_notes_failed;
};

/* The previous run's directory basename (the reference's
 * os.path.basename(os.path.normpath(run_dir)); "" when run_dir is empty). */
static const char *
hh_basename(const char *dir, struct jc_arena *a)
{
    const char *slash;
    if (dir == NULL || dir[0] == '\0') {
        return "";
    }
    slash = strrchr(dir, '/');
    if (slash != NULL && slash[1] != '\0') {
        return slash + 1;
    }
    if (slash != NULL) {   /* a trailing slash: the empty basename of "/x/" */
        return "";
    }
    (void)a;
    return dir;
}

/* Fail open (behaviour 1, CHANGED 5): the section is empty, the reason is
 * filed in the reference's observable shape ("handoff: <reason>" + newline),
 * and JC_OK is returned -- the caller starts the run WITHOUT the section and
 * the child's rc stays the run's rc. `err_out` left untouched when NULL
 * (reasons are not fatal to record, they are the record). */
static void
hh_fail(struct hh *h, char *err_out, size_t err_cap, const char *reason)
{
    (void)h;
    if (err_out != NULL && err_cap > 0) {
        int n = jc_snprintf(err_out, err_cap, "handoff: %s\n", reason);
        if (n < 0) {
            n = 0;
        }
        (void)n;
    }
}

/* meta.txt (behaviour 13, handoff.py 47-57): k=v lines, the split at the
 * FIRST '='; a missing or unreadable meta is NOT a failure -- the meta
 * stays empty and every ended() field falls to its ?/omit default
 * (behaviour 4/13). The values are arena strings, "" when the key is
 * absent. Lines without '=' are skipped (the reference's `if "=" in line`). */
static void
hh_read_meta(struct hh *h, const char *path)
{
    char *text = NULL;
    jc_size len = 0;
    const char *p;
    const char *end;

    h->rc = jc_arena_strdup(h->a, "");
    h->start = jc_arena_strdup(h->a, "");
    h->end = jc_arena_strdup(h->a, "");
    h->team_head_after = jc_arena_strdup(h->a, "");
    if (h->rc == NULL || h->start == NULL || h->end == NULL ||
        h->team_head_after == NULL) {
        return;   /* OOM: the arena is reset by the caller on the drop */
    }
    if (path == NULL || jc_read_file(path, &text, &len, h->a) != JC_OK) {
        return;   /* an EMPTY meta, the reference's except OSError: pass */
    }
    end = text + len;
    for (p = text; p < end; ) {
        const char *line = p;
        const char *line_end;
        const char *eq;
        jc_size klen;
        char *key;
        const char *val;
        jc_size vlen;
        while (p < end && *p != '\n') {
            p++;
        }
        line_end = p;
        if (p < end) {
            p++;
        }
        eq = NULL;
        {
            const char *q = line;
            while (q < p) {
                if (*q == '=') {
                    eq = q;
                    break;
                }
                q++;
            }
        }
        if (eq == NULL) {
            continue;
        }
        klen = (jc_size)(eq - line);
        key = jc_arena_strndup(h->a, line, klen);
        if (key == NULL) {
            return;
        }
        val = eq + 1;
        vlen = (jc_size)(line_end - val);
        if (strcmp(key, "rc") == 0) {
            h->rc = jc_arena_strndup(h->a, val, vlen);
        } else if (strcmp(key, "start") == 0) {
            h->start = jc_arena_strndup(h->a, val, vlen);
        } else if (strcmp(key, "end") == 0) {
            h->end = jc_arena_strndup(h->a, val, vlen);
        } else if (strcmp(key, "team_head_after") == 0) {
            h->team_head_after = jc_arena_strndup(h->a, val, vlen);
        } else {
            continue;
        }
        if (h->rc == NULL || h->start == NULL || h->end == NULL ||
            h->team_head_after == NULL) {
            return;
        }
    }
}

/* The per-path counter (behaviour 7): find or create the entry by name,
 * then increment its read or changed count. `edits` non-zero counts a
 * CHANGE (the EDIT_TOOLS set); zero counts a READ. Returns 0 on OOM. */
static int
hh_count_path(struct hh *h, const char *path, int edits)
{
    jc_size i;
    for (i = 0; i < h->paths.len; i++) {
        struct hh_path *e = (struct hh_path *)jc_vec_at(&h->paths, i);
        if (strcmp(e->name, path) == 0) {
            if (edits) {
                e->edits++;
            } else {
                e->reads++;
            }
            return 1;
        }
    }
    {
        struct hh_path *e = (struct hh_path *)jc_vec_push_slot(&h->paths);
        const char *dup;
        if (e == NULL) {
            return 0;
        }
        dup = jc_arena_strdup(h->a, path);
        if (dup == NULL) {
            jc_vec_clear(&h->paths);
            return 0;
        }
        e->name = dup;
        e->reads = 0;
        e->edits = 0;
        if (edits) {
            e->edits = 1;
        } else {
            e->reads = 1;
        }
        return 1;
    }
}

/* A PATH_TOOLS name (behaviour 7, handoff.py 31-32). */
static int
hh_is_path_tool(const char *name)
{
    int i;
    if (name == NULL) {
        return 0;
    }
    for (i = 0; i < (int)(sizeof HANDOFF_PATH_TOOLS / sizeof HANDOFF_PATH_TOOLS[0]);
         i++) {
        if (strcmp(name, HANDOFF_PATH_TOOLS[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* An EDIT_TOOLS name (behaviour 7, handoff.py 33). */
static int
hh_is_edit_tool(const char *name)
{
    int i;
    if (name == NULL) {
        return 0;
    }
    for (i = 0; i < (int)(sizeof HANDOFF_EDIT_TOOLS / sizeof HANDOFF_EDIT_TOOLS[0]);
         i++) {
        if (strcmp(name, HANDOFF_EDIT_TOOLS[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The tool_result preview map, by id, LAST WINS (the reference's dict
 * comprehension over the stream, handoff.py 106; behaviour 8's pairing key
 * M442). A result without an id is not stored (the reference's None key is
 * never looked up by a call's id). Returns 0 on OOM. */
static int
hh_note_result(struct hh *h, const char *id, const char *preview)
{
    jc_size i;
    const char *p;
    if (id == NULL || id[0] == '\0') {
        return 1;
    }
    p = (preview != NULL) ? preview : "";
    for (i = 0; i < h->res_ids.len; i++) {
        const char *stored = *(const char *const *)jc_vec_at(&h->res_ids, i);
        if (strcmp(stored, id) == 0) {
            /* LAST WINS: overwrite the existing preview slot (the reference's
             * dict[id] = preview keeps the id's length and replaces the
             * value). Pushing instead desyncs the two parallel arrays and
             * leaves the FIRST preview (the driver's batch previews are all
             * the last spawn's, the last-wins one). */
            char *dst;
            const char *np;
            np = jc_arena_strdup(h->a, p);
            if (np == NULL) {
                return 0;
            }
            dst = (char *)jc_vec_at(&h->res_prevs, i);
            if (dst == NULL) {
                return 0;
            }
            memcpy(dst, &np, sizeof(np));
            return 1;
        }
    }
    {
        const char *dup_id = jc_arena_strdup(h->a, id);
        const char *dup_pv;
        if (dup_id == NULL) {
            return 0;
        }
        dup_pv = jc_arena_strdup(h->a, p);
        if (dup_pv == NULL) {
            return 0;
        }
        /* Both pushes or neither (the id and its preview must stay
         * aligned: a partial pair would misquote a later result). */
        if (jc_vec_reserve(&h->res_ids, 1) != JC_OK ||
            jc_vec_reserve(&h->res_prevs, 1) != JC_OK) {
            return 0;
        }
        if (jc_vec_push(&h->res_ids, &dup_id) != JC_OK ||
            jc_vec_push(&h->res_prevs, &dup_pv) != JC_OK) {
            return 0;
        }
        return 1;
    }
}

/* The spawn_parallel table (behaviour 8): one entry per call, in stream
 * order, carrying the args (the JSON STRING -- parsed a second time at
 * build time) and the id (the result pairing key). Returns 0 on OOM. */
static int
hh_note_batch(struct hh *h, const char *args_str, const char *id)
{
    struct hh_batch *b;
    const char *dup_args = NULL;
    const char *dup_id = NULL;
    if (args_str != NULL) {
        dup_args = jc_arena_strdup(h->a, args_str);
        if (dup_args == NULL) {
            return 0;
        }
    }
    if (id != NULL && id[0] != '\0') {
        dup_id = jc_arena_strdup(h->a, id);
        if (dup_id == NULL) {
            return 0;
        }
    }
    b = (struct hh_batch *)jc_vec_push_slot(&h->batches);
    if (b == NULL) {
        return 0;
    }
    b->args = dup_args;
    b->id = dup_id;
    return 1;
}

/* One line of stream.jsonl (behaviour 1, 5, 6, 7, 8, 13): the fields the
 * handoff reads, per the measured shapes (M1058 note). `line` is NUL-
 * terminated (the caller arena-copies it: this cJSON has no length-taking
 * parse). A line that is not valid JSON is SKIPPED -- a stream cut mid-line
 * by a crash must not drop the section (behaviour 1's teeth). Returns 0 on
 * OOM (the caller drops fail-open); the json is always deleted before the
 * return. */
static int
hh_handle_line(struct hh *h, const char *line)
{
    cJSON *ev;
    const cJSON *type;
    const char *s;

    {
        const char *p = line;
        while (*p == ' ') {
            p++;
        }
        if (*p != '{') {
            return 1;   /* not a JSON object: skipped like the reference's
                           except ValueError */
        }
        ev = cJSON_Parse(p);
    }
    if (ev == NULL) {
        return 1;   /* the skip (behaviour 13) */
    }
    type = cJSON_GetObjectItem(ev, "type");
    s = (cJSON_IsString(type) && type->valuestring != NULL)
            ? type->valuestring : "";
    if (strcmp(s, "done") == 0) {
        /* done[-1] ALONE (SV:0319.2): the LAST done event's text, no
         * backward scan -- an earlier done with content is not quoted when
         * the last one is empty (behaviour 5). */
        const cJSON *t = cJSON_GetObjectItem(ev, "text");
        h->last_msg = jc_arena_strdup(h->a,
                                      (cJSON_IsString(t) &&
                                       t->valuestring != NULL)
                                          ? t->valuestring : "");
        if (h->last_msg == NULL) {
            cJSON_Delete(ev);
            return 0;
        }
    } else if (strcmp(s, "tool_call") == 0) {
        const cJSON *name_i = cJSON_GetObjectItem(ev, "name");
        const cJSON *args_i = cJSON_GetObjectItem(ev, "args");
        const cJSON *id_i = cJSON_GetObjectItem(ev, "id");
        const char *name = (cJSON_IsString(name_i) &&
                            name_i->valuestring != NULL)
                               ? name_i->valuestring : "";
        const char *args_str = (cJSON_IsString(args_i) &&
                                args_i->valuestring != NULL)
                                   ? args_i->valuestring : NULL;
        const char *id = (cJSON_IsString(id_i) && id_i->valuestring != NULL)
                             ? id_i->valuestring : NULL;
        if (strcmp(name, "todowrite") == 0) {
            /* The LAST todowrite's args (behaviour 6): a fresh JSON string,
             * parsed a second time at build time. */
            if (args_str != NULL) {
                h->todo_args = jc_arena_strdup(h->a, args_str);
                if (h->todo_args == NULL) {
                    cJSON_Delete(ev);
                    return 0;
                }
            } else {
                h->todo_args = NULL;
            }
        }
        if (strcmp(name, "spawn_parallel") == 0) {
            if (!hh_note_batch(h, args_str, id)) {
                cJSON_Delete(ev);
                return 0;
            }
        }
        if (hh_is_path_tool(name) && args_str != NULL) {
            /* The call's args are a JSON STRING (the measured shape): parse
             * a second time (behaviour 6). */
            cJSON *args = cJSON_Parse(args_str);
            const cJSON *a_path;
            const cJSON *a_file;
            const char *path = "";
            if (args == NULL) {
                path = "";   /* unparseable args: no path, like the
                                reference's args_of -> {} */
            } else {
                a_path = cJSON_GetObjectItem(args, "path");
                a_file = cJSON_GetObjectItem(args, "file");
                if (cJSON_IsString(a_path) && a_path->valuestring != NULL &&
                    a_path->valuestring[0] != '\0') {
                    path = a_path->valuestring;
                } else if (cJSON_IsString(a_file) &&
                           a_file->valuestring != NULL &&
                           a_file->valuestring[0] != '\0') {
                    path = a_file->valuestring;
                }
            }
            if (path[0] != '\0') {
                if (!hh_count_path(h, path,
                                   hh_is_edit_tool(name) ? 1 : 0)) {
                    if (args != NULL) {
                        cJSON_Delete(args);
                    }
                    cJSON_Delete(ev);
                    return 0;
                }
            } else if (strcmp(name, "apply_patch") == 0 && args != NULL) {
                /* CHANGED 6 (a reference defect, fixed in the port):
                 * apply_patch has NO top-level path in jichi -- its paths
                 * are edits[].path (verified against jc_tool_apply_patch.c
                 * and the schema's per-edit path), so the reference's
                 * args.path or args.file NEVER counts an apply_patch edit.
                 * Count EACH edits[].path as a CHANGE, the path/file keys
                 * first and edits[] when they are absent. */
                const cJSON *edits = cJSON_GetObjectItem(args, "edits");
                const cJSON *edit;
                if (cJSON_IsArray(edits)) {
                    cJSON_ArrayForEach(edit, edits) {
                        const cJSON *ep;
                        const char *epath;
                        if (!cJSON_IsObject(edit)) {
                            continue;
                        }
                        ep = cJSON_GetObjectItem(edit, "path");
                        epath = (cJSON_IsString(ep) && ep->valuestring != NULL)
                                    ? ep->valuestring : "";
                        if (epath[0] == '\0') {
                            continue;
                        }
                        if (!hh_count_path(h, epath, 1)) {
                            cJSON_Delete(args);
                            cJSON_Delete(ev);
                            return 0;
                        }
                    }
                }
            }
            if (args != NULL) {
                cJSON_Delete(args);
            }
        }
    } else if (strcmp(s, "tool_result") == 0) {
        const cJSON *id_i = cJSON_GetObjectItem(ev, "id");
        const cJSON *pv_i = cJSON_GetObjectItem(ev, "preview");
        const char *id = (cJSON_IsString(id_i) && id_i->valuestring != NULL)
                            ? id_i->valuestring : NULL;
        const char *pv = (cJSON_IsString(pv_i) && pv_i->valuestring != NULL)
                            ? pv_i->valuestring : NULL;
        if (!hh_note_result(h, id, pv)) {
            cJSON_Delete(ev);
            return 0;
        }
    }
    cJSON_Delete(ev);
    return 1;
}

/* stream.jsonl, line by line (behaviour 13, handoff.py 36-44): a missing or
 * unreadable stream DROPS the section (the reference's events() open
 * raising); a notes directory that fails to list does too (behaviour 1).
 * Returns 0 with `reason` set on a drop, 1 on success. */
static int
hh_read_stream(struct hh *h, const char *path, const char **reason)
{
    char *text;
    jc_size len;
    const char *p;
    const char *end;

    text = NULL;
    len = 0;
    if (jc_read_file(path, &text, &len, h->a) != JC_OK) {
        *reason = "the previous run's stream.jsonl is missing or unreadable";
        return 0;
    }
    end = text + len;
    for (p = text; p <= end; ) {
        const char *line = p;
        jc_size l;
        while (p < end && *p != '\n') {
            p++;
        }
        if (p < end) {
            p++;
        }
        l = (jc_size)(p - line);
        if (l > 0) {
            /* The vendored cJSON parses NUL-terminated strings: copy the
             * line into the arena (behaviour 13's skip semantics are on the
             * LINE, and a crash-cut partial line parses to NULL and is
             * skipped, never a failure). */
            const char *copy = jc_arena_strndup(h->a, line, l);
            if (copy == NULL) {
                *reason = "the handoff section ran out of memory";
                return 0;
            }
            if (!hh_handle_line(h, copy)) {
                *reason = "the handoff section ran out of memory";
                return 0;
            }
        }
        if (p == end) {
            break;
        }
    }
    return 1;
}

/* The notes/ directory listing (behaviour 9, handoff.py 147-148): the names
 * SORTED byte-wise (the reference's sorted(os.listdir())). A directory that
 * does not exist is the no-notes case (the reference's isdir check); one
 * that FAILS to list is a drop (behaviour 1's named class). Returns 0 with
 * `reason` set on a drop. */
static int
hh_list_notes(struct hh *h, const char *path, const char **reason)
{
    DIR *d;
    struct dirent *e;

    if (path == NULL || path[0] == '\0') {
        return 1;   /* no notes at all */
    }
    if (!jc_file_exists(path)) {
        return 1;   /* the reference's isdir(notes_dir) else [] */
    }
    d = opendir(path);
    if (d == NULL) {
        *reason = "the previous run's notes directory failed to list";
        return 0;
    }
    while ((e = readdir(d)) != NULL) {
        const char *dup;
        if (e->d_name[0] == '.' &&
            (e->d_name[1] == '\0' ||
             (e->d_name[1] == '.' && e->d_name[2] == '\0'))) {
            continue;   /* the reference's os.listdir: no dot entries */
        }
        dup = jc_arena_strdup(h->a, e->d_name);
        if (dup == NULL) {
            closedir(d);
            *reason = "the handoff section ran out of memory";
            return 0;
        }
        if (jc_vec_push(&h->notes, &dup) != JC_OK) {
            closedir(d);
            *reason = "the handoff section ran out of memory";
            return 0;
        }
    }
    closedir(d);
    /* The reference's sorted(): byte-wise ascending on the names. A notes
     * directory holds a handful of files, so a plain insertion sort over
     * the pointer words. */
    if (h->notes.len > 1) {
        jc_size i;
        for (i = 1; i < h->notes.len; i++) {
            const char *key = *(const char *const *)jc_vec_at(&h->notes, i);
            jc_size j = i;
            while (j > 0 &&
                   strcmp(*(const char *const *)jc_vec_at(&h->notes, j - 1),
                          key) > 0) {
                const char *prev =
                    *(const char *const *)jc_vec_at(&h->notes, j - 1);
                char *dst = (char *)jc_vec_at(&h->notes, j);
                if (dst == NULL) {
                    *reason = "the handoff section ran out of memory";
                    return 0;
                }
                memcpy(dst, &prev, sizeof(prev));
                j--;
            }
            {
                char *dst = (char *)jc_vec_at(&h->notes, j);
                if (dst == NULL) {
                    *reason = "the handoff section ran out of memory";
                    return 0;
                }
                memcpy(dst, &key, sizeof(key));
            }
        }
    }
    return 1;
}

/* one_line (behaviour 11, handoff.py 82-84, SV:0319.3): collapse every
 * ASCII whitespace run (space tab newlf carr ret vtab ff) to a single space
 * and strip both ends, then cap at n CODE POINTS -- decode UTF-8 by hand, a
 * multi-byte sequence is one character, and the cut never lands mid-
 * sequence (the reference's len/slice count code points; the driver's
 * 1800-byte e-dash note is the teeth). At exactly n the string passes;
 * above it the first n-1 characters plus the 3-byte UTF-8 ellipsis
 * (E2 80 A6). Python's split() collapses UNICODE whitespace (NBSP, U+3000);
 * the port collapses ASCII whitespace only -- a cosmetic difference the
 * note names rather than hides. The result is arena-allocated. */
static const char *
hh_one_line(struct hh *h, const char *s, int n)
{
    const unsigned char *in;
    unsigned char *buf;
    unsigned char *o;
    int cps;
    int ws;
    size_t inlen;
    size_t cap;

    if (s == NULL) {
        s = "";
    }
    inlen = strlen(s);
    /* inlen + 4, not inlen + 1: the collapse writes at most inlen bytes
     * plus the NUL, but the ellipsis branch overwrites the kept-prefix tail
     * with the 3-byte UTF-8 ellipsis (E2 80 A6) plus its NUL (4 bytes) at an
     * offset that can reach inlen, so the final NUL lands at inlen + 3 --
     * the 1-byte OOB the qwen27b review found. */
    cap = inlen + 4;
    buf = (unsigned char *)jc_arena_alloc(h->a, cap);
    if (buf == NULL) {
        return NULL;
    }
    in = (const unsigned char *)s;
    o = buf;
    ws = 0;   /* 1 while at the start of a collapsed run */
    {
        const unsigned char *p = in;
        while (*p != '\0') {
            unsigned char c = *p;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                c == '\v' || c == '\f') {
                ws = 1;
                p++;
                continue;
            }
            if (ws) {
                if (o > buf) {
                    *o++ = ' ';
                }
                ws = 0;
            }
            /* Copy the whole UTF-8 sequence as one unit (a multi-byte
             * sequence is one character, never cut mid-sequence). */
            *o++ = c;
            p++;
            if (c >= 0xC0 && c < 0xF0 && *p != '\0') {
                int k;
                int need = (c >= 0xE0) ? 2 : 1;
                for (k = 0; k < need && *p != '\0'; k++) {
                    unsigned char cc = *p;
                    if (cc < 0x80 || cc >= 0xC0) {
                        break;   /* a broken sequence: copy only the lead */
                    }
                    *o++ = cc;
                    p++;
                }
            } else if (c >= 0xF0 && c < 0xF5 && *p != '\0') {
                int k;
                for (k = 0; k < 3 && *p != '\0'; k++) {
                    unsigned char cc = *p;
                    if (cc < 0x80 || cc >= 0xC0) {
                        break;
                    }
                    *o++ = cc;
                    p++;
                }
            }
        }
    }
    *o = '\0';
    /* Count the code points of the collapsed result. */
    cps = 0;
    {
        const unsigned char *p = buf;
        while (*p != '\0') {
            unsigned char c = *p;
            cps++;
            if (c >= 0xC2 && c < 0xF0) {
                int k;
                int need = (c >= 0xE0) ? 2 : 1;
                for (k = 0; k < need && p[k + 1] != '\0'; k++) {
                    unsigned char cc = p[k + 1];
                    if (cc < 0x80 || cc >= 0xC0) {
                        break;
                    }
                    p++;
                }
            } else if (c >= 0xF0 && c < 0xF5) {
                int k;
                for (k = 0; k < 3 && p[k + 1] != '\0'; k++) {
                    unsigned char cc = p[k + 1];
                    if (cc < 0x80 || cc >= 0xC0) {
                        break;
                    }
                    p++;
                }
            }
            p++;
        }
    }
    if (cps <= n) {
        return (const char *)buf;
    }
    /* Above the cap: the first n-1 code points plus the 3-byte ellipsis,
     * never cutting inside a sequence. */
    {
        unsigned char *p = buf;
        int kept = 0;
        while (kept < n - 1 && *p != '\0') {
            unsigned char c = *p;
            size_t seq = 1;
            if (c >= 0xC2 && c < 0xF0) {
                int need = (c >= 0xE0) ? 2 : 1;
                int k;
                seq = 0;
                for (k = 0; k <= need; k++) {
                    if (p[k] == '\0' ||
                        (k > 0 && (p[k] < 0x80 || p[k] >= 0xC0))) {
                        break;
                    }
                    seq = (size_t)(k + 1);
                }
                if (seq < 2) {
                    seq = 1;
                }
            } else if (c >= 0xF0 && c < 0xF5) {
                int k;
                seq = 0;
                for (k = 0; k <= 3; k++) {
                    if (p[k] == '\0' ||
                        (k > 0 && (p[k] < 0x80 || p[k] >= 0xC0))) {
                        break;
                    }
                    seq = (size_t)(k + 1);
                }
                if (seq < 2) {
                    seq = 1;
                }
            }
            p += seq;
            kept++;
        }
        /* The kept prefix is buf[0 .. (p - buf)) (already in place); the
         * next byte on is the first char of what was cut. Overwrite it with
         * the 3-byte ellipsis and terminate. (The reference's s[: n - 1] +
         * "…".) */
        o = p;
        *o++ = 0xE2; *o++ = 0x80; *o++ = 0xA6;
        *o = '\0';
        return (const char *)buf;
    }
}

/* The UTC wall clock the meta stamps carry (YYYY-MM-DDTHH:MM:SSZ,
 * now_iso's shape) as whole seconds since the epoch, pure integer civil
 * date arithmetic (no tm, no locale, no timegm). Returns 0 for out-of-
 * range fields. */
static int
hh_civil_seconds(int Y, int M, int D, int h, int m, int s, long *out)
{
    long era;
    long yoe;
    long doy;
    long doe;
    long days;

    if (M < 1 || M > 12 || D < 1 || D > 31 ||
        h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) {
        return 0;
    }
    {
        long y = (long)Y;
        if (M <= 2) {
            y -= 1;
        }
        era = (y >= 0 ? y : y - 399) / 400;
        yoe = y - era * 400;
        doy = (153L * (long)(M + (M > 2 ? -3 : 9)) + 2) / 5 + (long)D - 1;
        doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        days = era * 146097 + doe - 719468;
    }
    *out = days * 86400L + (long)h * 3600L + (long)m * 60L + (long)s;
    return 1;
}

/* An ISO-8601 UTC stamp (YYYY-MM-DDTHH:MM:SSZ, the reference's
 * datetime.fromisoformat over the meta's start=/end=): parse to whole
 * seconds since the epoch. Returns 0 when the stamp is absent or
 * unparseable (the reference's except (KeyError, ValueError), behaviour
 * 4's omit). The fractional seconds and offsets fromisoformat would also
 * accept are not written by this harness's meta (now_iso's shape), and a
 * refusal to parse them is the same omit. */
static int
hh_parse_iso(const char *s, long *out)
{
    int Y, M, D, h, m, sec;

    if (s == NULL || out == NULL) {
        return 0;
    }
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &Y, &M, &D, &h, &m, &sec)
        != 6) {
        return 0;
    }
    if (!hh_civil_seconds(Y, M, D, h, m, sec, out)) {
        return 0;
    }
    return 1;
}

/* ended (behaviour 4, handoff.py 87-98): "rc <rc>< after <N> min>: <why>.
 * It ended at commit `<head>`." -- rc from the meta's rc= or "?", N the
 * round of (end - start) in integer minutes OMITTED when either stamp is
 * absent or unparseable, why the reference's map, head the meta's
 * team_head_after= or "?". Written into `out` (>= 256 bytes); 0 on
 * overflow. The duration rounds half-up in integer minutes (the reference's
 * Python round() is banker's -- the divergence at exact 30-second
 * boundaries is cosmetic in a display line, named in the note rather than
 * hidden). */
static int
hh_ended(struct hh *h, char *out, size_t cap)
{
    const char *rc;
    const char *why;
    long dur_min;
    int have_dur;

    rc = (h->rc != NULL && h->rc[0] != '\0') ? h->rc : "?";
    if (strcmp(rc, "0") == 0) {
        why = "it ended itself";
    } else if (strcmp(rc, "130") == 0) {
        why = "it was interrupted: the deadline, or an abort";
    } else if (strcmp(rc, "124") == 0) {
        why = "a time bound fired";
    } else if (strcmp(rc, "1") == 0) {
        why = "it ended with an error";
    } else {
        why = "see its records";
    }
    /* The 130/124 entries stay in the map even though Mode A never sets a
     * timeout wrapper: a map entry that can fire (130 as a signal death)
     * is kept, not a dead branch (behaviour 4's note). */
    have_dur = 0;
    dur_min = 0;
    {
        long a, b;
        if (hh_parse_iso(h->start, &a) && hh_parse_iso(h->end, &b) &&
            b >= a) {
            long secs = b - a;
            /* round((b-a)/60) half-up on the integer minutes: the reference's
             * Python round() is banker's -- the divergence at exact 30-second
             * boundaries is cosmetic in a display line, named in the note. */
            dur_min = (secs + 30) / 60;
            have_dur = 1;
        }
    }
    {
        int n;
        const char *head = (h->team_head_after != NULL &&
                            h->team_head_after[0] != '\0')
                              ? h->team_head_after : "?";
        if (have_dur != 0) {
            n = jc_snprintf(out, cap,
                            "rc %s after %ld min: %s. It ended at commit "
                            "`%s`.",
                            rc, dur_min, why, head);
        } else {
            n = jc_snprintf(out, cap, "rc %s: %s. It ended at commit `%s`.",
                            rc, why, head);
        }
        if (n < 0 || (size_t)n >= cap) {
            return 0;
        }
        return 1;
    }
}

/* The top-25 sort (behaviour 7, CHANGED 4): total count DESCENDING, ties
 * by path ASCENDING (byte-wise) -- the reference's sorted(key=-total)
 * breaks ties in Python's arbitrary set/hash order (per-process hash seed,
 * nondeterministic even for the reference); the deterministic one is what a
 * driver can pin. Returns 0 on OOM. */
static int
hh_sort_paths(struct hh *h)
{
    jc_size i;
    if (h->paths.len > 1) {
        /* Insertion sort over the entries (the vec holds them contiguously
         * and in place; a run's stream names a bounded few paths). */
        for (i = 1; i < h->paths.len; i++) {
            struct hh_path key = *(struct hh_path *)jc_vec_at(&h->paths, i);
            jc_size j = i;
            while (j > 0) {
                struct hh_path *prev =
                    (struct hh_path *)jc_vec_at(&h->paths, j - 1);
                int pt = prev->reads + prev->edits;
                int kt = key.reads + key.edits;
                int move = 0;
                if (pt < kt) {
                    move = 1;
                } else if (pt == kt && strcmp(prev->name, key.name) > 0) {
                    move = 1;
                }
                if (!move) {
                    break;
                }
                *(struct hh_path *)jc_vec_at(&h->paths, j) = *prev;
                j--;
            }
            *(struct hh_path *)jc_vec_at(&h->paths, j) = key;
        }
    }
    return 1;
}

/* The args of a tool_call (the reference's args_of, handoff.py 60-67): the
 * call's args are a JSON STRING in the measured shape (the reference parses
 * it a second time); unparseable args are an empty object. The caller
 * deletes the result (NULL on an empty/unparseable args). */
static cJSON *
hh_args_of(const char *args_str)
{
    if (args_str == NULL || args_str[0] == '\0') {
        return NULL;
    }
    return cJSON_Parse(args_str);
}

/* The write value of a batch task (SV:0319.6): the reference prints the
 * PARSED value with Python's %s -- a JSON true renders True, false False,
 * the number 1 renders 1, a string renders itself; an absent key renders
 * "not set". The port matches that table so a side-by-side does not show a
 * rendering difference. Written into `out` (>= 32 bytes); 0 on a value no
 * branch covers (an object/array: Python's %s would dump it -- not
 * produced by jichi's schema, rendered as the absent default). */
static int
hh_write_render(const cJSON *w, char *out, size_t cap)
{
    if (w == NULL || cJSON_IsInvalid(w) || cJSON_IsNull(w)) {
        jc_snprintf(out, cap, "not set");
        return 1;
    }
    if (cJSON_IsBool(w)) {
        jc_snprintf(out, cap, "%s", cJSON_IsTrue(w) ? "True" : "False");
        return 1;
    }
    if (cJSON_IsNumber(w)) {
        if (w->valuedouble == (double)w->valueint) {
            jc_snprintf(out, cap, "%d", w->valueint);
            return 1;
        }
        jc_snprintf(out, cap, "%g", w->valuedouble);
        return 1;
    }
    if (cJSON_IsString(w) && w->valuestring != NULL) {
        jc_snprintf(out, cap, "%s", w->valuestring);
        return 1;
    }
    jc_snprintf(out, cap, "not set");
    return 1;
}

/* A tool_result preview by id (behaviour 8's pairing, last-wins over the
 * stream): NULL when no result carries the id ("no result: the run ended
 * inside it"). */
static const char *
hh_result_preview(struct hh *h, const char *id)
{
    jc_size i;
    const char *found = NULL;
    if (id == NULL) {
        return NULL;
    }
    for (i = 0; i < h->res_ids.len; i++) {
        const char *stored =
            *(const char *const *)jc_vec_at(&h->res_ids, i);
        if (strcmp(stored, id) == 0) {
            found = *(const char *const *)jc_vec_at(&h->res_prevs, i);
        }
    }
    return found;
}

/* The manager's note text (behaviour 9, CHANGED 3 / def-358): a .gz note
 * renders (unreadable) BY NAME -- the port has no zlib (CLAUDE.md's
 * dependency rule is libcurl and NOTHING else), and an honest name for
 * "present but unreadable in this build" is never a silent skip; a non-.gz
 * unreadable note renders (unreadable) too, and the section otherwise
 * proceeds (one bad note never kills the handoff -- behaviour 1's class,
 * SV:0319.4). */
static const char *
hh_note_text(struct hh *h, const char *notes_dir, const char *name)
{
    const char *un;
    size_t nlen;
    char path[8192];
    char *text = NULL;
    jc_size len = 0;

    un = jc_arena_strdup(h->a, "(unreadable)");
    if (un == NULL) {
        return NULL;
    }
    nlen = strlen(name);
    if (nlen >= 3 && strcmp(name + nlen - 3, ".gz") == 0) {
        return un;   /* def-358: the gunzip lands with H5's snapshot */
    }
    if (jc_snprintf(path, sizeof path, "%s/%s", notes_dir, name)
        >= (int)sizeof path) {
        return un;
    }
    if (jc_read_file(path, &text, &len, h->a) != JC_OK) {
        return un;
    }
    /* The text is read as bytes; the reference's errors="replace" becomes
     * byte-preservation in one_line's collapse (a cosmetic difference on
     * non-UTF-8 bytes, named in the note, not a behaviour). */
    return text;
}

/* The section builder (STEP 2: the reference's out.append order,
 * handoff.py 101-155). UNBOUNDED: the 6000-byte cut is applied by the
 * wrapper (STEP 3), not here. Returns the finished section as a heap
 * string (NULL on OOM; the caller frees it). */
static char *
hh_build_section_unbounded(struct hh *h, const char *run_name,
                           const char *records_dir, const char *notes_dir)
{
    struct jc_sb sb;
    char ended_line[320];
    int ok = 1;

    jc_sb_init(&sb);
    /* The header block (behaviour 3, handoff.py 108-111), byte-for-byte. */
    ok &= jc_sb_append(&sb, "## What the previous run left you\n") == JC_OK;
    ok &= jc_sb_append(&sb, "\n") == JC_OK;
    ok &= jc_sb_append_fmt(&sb,
        "*Written by the harness from run %s's own records, mechanically. "
        "Only its last message is the model's, quoted. Verify before relying "
        "on it: the tree may have moved since.*\n", run_name) == JC_OK;
    ok &= jc_sb_append(&sb, "\n") == JC_OK;
    /* The How it ended bullet (behaviour 4). */
    if (!hh_ended(h, ended_line, sizeof ended_line)) {
        ok = 0;
    }
    ok &= jc_sb_append_fmt(&sb, "- **How it ended:** %s\n",
                           ended_line) == JC_OK;
    /* Its last message (behaviour 5, SV:0319.2): done[-1] ALONE, quoted
     * only when its text is non-empty after the collapse; capped 600. */
    {
        const char *lm = hh_one_line(h, h->last_msg, 600);
        if (lm == NULL) {
            ok = 0;
        } else if (lm[0] != '\0') {
            ok &= jc_sb_append_fmt(&sb, "- **Its last message:** \"%s\"\n",
                                   lm) == JC_OK;
        }
    }
    /* Its todo list (behaviour 6): the LAST todowrite's todos, first 15
     * dict items, capped 140. */
    if (h->todo_args != NULL) {
        cJSON *args = hh_args_of(h->todo_args);
        const cJSON *todos;
        if (args == NULL) {
            ok = 0;
        } else {
            todos = cJSON_GetObjectItem(args, "todos");
            if (cJSON_IsArray(todos) && cJSON_GetArraySize(todos) > 0) {
                const cJSON *item;
                int shown = 0;
                ok &= jc_sb_append(&sb,
                    "- **Its todo list, as it last wrote it:**\n") == JC_OK;
                cJSON_ArrayForEach(item, todos) {
                    const cJSON *st;
                    const cJSON *ct;
                    const char *status;
                    const char *content;
                    const char *co;
                    if (shown >= HANDOFF_MAX_TODOS) {
                        break;
                    }
                    if (!cJSON_IsObject(item)) {
                        continue;
                    }
                    st = cJSON_GetObjectItem(item, "status");
                    status = (cJSON_IsString(st) && st->valuestring != NULL
                              && st->valuestring[0] != '\0')
                                 ? st->valuestring : "?";
                    ct = cJSON_GetObjectItem(item, "content");
                    content = (cJSON_IsString(ct) && ct->valuestring != NULL)
                                  ? ct->valuestring : "";
                    co = hh_one_line(h, content, 140);
                    if (co == NULL) {
                        ok = 0;
                    } else {
                        ok &= jc_sb_append_fmt(&sb, "  - [%s] %s\n",
                                               status, co) == JC_OK;
                    }
                    shown++;
                }
            }
            cJSON_Delete(args);
        }
    }
    /* The files it read or changed (behaviour 7): the union of the two
     * counters, top 25 by total descending (ties by path ascending,
     * CHANGED 4), one indented line. */
    if (h->paths.len > 0) {
        jc_size i;
        jc_size limit;
        int first = 1;
        if (!hh_sort_paths(h)) {
            ok = 0;
        }
        limit = (h->paths.len < (jc_size)HANDOFF_TOP_FILES)
                    ? h->paths.len : (jc_size)HANDOFF_TOP_FILES;
        ok &= jc_sb_append(&sb,
            "- **The files it read or changed,** most used first (count of "
            "calls):\n") == JC_OK;
        ok &= jc_sb_append(&sb, "  ") == JC_OK;
        for (i = 0; ok && i < limit; i++) {
            struct hh_path *e = (struct hh_path *)jc_vec_at(&h->paths, i);
            char kinds[96];
            int klen = 0;
            if (e->reads > 0) {
                klen += jc_snprintf(kinds + klen, sizeof kinds - klen,
                                    "%sread %d", (klen > 0) ? ", " : "",
                                    e->reads);
            }
            if (e->edits > 0) {
                klen += jc_snprintf(kinds + klen, sizeof kinds - klen,
                                    "%schanged %d", (klen > 0) ? ", " : "",
                                    e->edits);
            }
            /* ONE indented line, the entries joined by "; " (the
             * reference's "; ".join over the top-25 list). */
            ok &= jc_sb_append_fmt(&sb, "%s`%s` (%s)",
                                   (first ? "" : "; "), e->name,
                                   kinds) == JC_OK;
            first = 0;
        }
        ok &= jc_sb_append(&sb, "\n") == JC_OK;
    }
    /* Its batches (behaviour 8): every spawn_parallel in stream order,
     * numbered from 1; the paired result's preview by id (last-wins), or
     * "no result: the run ended inside it"; the first 8 task dicts. */
    {
        jc_size i;
        for (i = 0; i < h->batches.len; i++) {
            struct hh_batch *b = (struct hh_batch *)jc_vec_at(&h->batches, i);
            cJSON *args;
            const cJSON *tasks;
            const char *head;
            const char *preview;
            int ntasks = 0;
            int ti = 0;
            if (!ok) {
                break;
            }
            args = hh_args_of(b->args);
            if (args == NULL) {
                ok = 0;
            }
            if (ok) {
                tasks = cJSON_GetObjectItem(args, "tasks");
                if (cJSON_IsArray(tasks)) {
                    ntasks = cJSON_GetArraySize(tasks);
                }
                preview = hh_result_preview(h, b->id);
                if (preview != NULL) {
                    head = hh_one_line(h, preview, 200);
                    if (head == NULL) {
                        ok = 0;
                    }
                } else {
                    head = "no result: the run ended inside it";
                }
                if (ok) {
                    ok &= jc_sb_append_fmt(
                        &sb, "- **Its batch %d:** %d task(s); the result "
                        "began: \"%s\"\n", (int)(i + 1), ntasks, head)
                        == JC_OK;
                }
                if (ok && cJSON_IsArray(tasks)) {
                    const cJSON *t;
                    cJSON_ArrayForEach(t, tasks) {
                        const cJSON *ag;
                        const cJSON *md;
                        const cJSON *w;
                        const cJSON *tk;
                        const char *agent;
                        const char *model;
                        const char *task;
                        char wbuf[128];
                        const char *wo;
                        if (ti >= HANDOFF_MAX_TASKS) {
                            break;
                        }
                        if (!cJSON_IsObject(t)) {
                            continue;
                        }
                        ag = cJSON_GetObjectItem(t, "agent");
                        agent = (cJSON_IsString(ag) && ag->valuestring
                                          != NULL)
                                    ? ag->valuestring : "-";
                        md = cJSON_GetObjectItem(t, "model");
                        model = (cJSON_IsString(md) && md->valuestring
                                          != NULL)
                                    ? md->valuestring : "-";
                        w = cJSON_GetObjectItem(t, "write");
                        if (!hh_write_render(w, wbuf, sizeof wbuf)) {
                            ok = 0;
                        }
                        wo = wbuf;
                        tk = cJSON_GetObjectItem(t, "task");
                        task = (cJSON_IsString(tk) && tk->valuestring != NULL)
                                   ? tk->valuestring : "";
                        if (!ok) {
                            break;
                        }
                        {
                            const char *to = hh_one_line(h, task, 110);
                            if (to == NULL) {
                                ok = 0;
                            } else {
                                ok &= jc_sb_append_fmt(
                                    &sb, "  - %s, %s, write=%s: %s\n",
                                    agent, model, wo, to) == JC_OK;
                            }
                        }
                        ti++;
                    }
                }
                if (args != NULL) {
                    cJSON_Delete(args);
                }
            }
        }
    }
    /* The manager's notes (behaviour 9, CHANGED 2): the byte-sorted names,
     * the FIRST 4 rendered, the rest "and N more"; the full-text path is
     * the Mode-A records path (NOT the reference's /records/runs). */
    if (h->notes.len > 0) {
        jc_size i;
        jc_size limit = (h->notes.len < (jc_size)HANDOFF_MAX_NOTES)
                            ? h->notes.len
                            : (jc_size)HANDOFF_MAX_NOTES;
        if (records_dir != NULL && records_dir[0] != '\0') {
            /* CHANGED 2: the full-text path is the Mode-A records path.
             * `records_dir` is the ABSOLUTE runs directory (the caller's
             * realpath of <dir>/runs), so the notes sit at
             * <records_dir>/<run>/notes/. */
            ok &= jc_sb_append_fmt(
                &sb, "- **The manager's notes it was given,** the start of "
                "each. The full text is in `%s/%s/notes/`:\n",
                records_dir, run_name) == JC_OK;
        } else {
            ok &= jc_sb_append_fmt(
                &sb, "- **The manager's notes it was given,** the start of "
                "each. The full text is in `/runs/%s/notes/`:\n",
                run_name) == JC_OK;
        }
        for (i = 0; ok && i < limit; i++) {
            const char *name = *(const char *const *)jc_vec_at(&h->notes, i);
            const char *nt;
            const char *no;
            nt = hh_note_text(h, notes_dir, name);
            if (nt == NULL) {
                ok = 0;
            } else {
                no = hh_one_line(h, nt, HANDOFF_NOTE_CHARS);
                if (no == NULL) {
                    ok = 0;
                } else {
                    ok &= jc_sb_append_fmt(&sb, "  - `%s`: \"%s\"\n", name,
                                           no) == JC_OK;
                }
            }
        }
        if (ok && h->notes.len > (jc_size)HANDOFF_MAX_NOTES) {
            ok &= jc_sb_append_fmt(&sb, "  - and %d more\n",
                                   (int)(h->notes.len
                                         - (jc_size)HANDOFF_MAX_NOTES))
                   == JC_OK;
        }
    }
    if (!ok) {
        return 0;
    }
    return jc_sb_finish(&sb);   /* the finished (UNCUT) section, heap, NUL
                                   terminated; the caller frees it */
}

/* The public builder (STEP 2, the reference's out.append order, then STEP 3,
 * the 6000-byte cut). The section is assembled unbounded, cut there, and the
 * cut result -- at most 6000 bytes + the 24-byte marker + a byte -- copied
 * into the caller's out_buf, PRECEDED by the reference's leading newline
 * (handoff.py's main: sys.stdout.write("\n" + build(...))): out_buf is thus
 * "\n" + the finished section, and the value returned is that FINISHED file's
 * byte count (the leading newline INCLUDED). out_buf is a cap for the whole
 * thing (behaviour 10), not for the pre-cut build (a full batch section can
 * exceed it; capping the build dropped the section then). Returns 0 on OOM
 * (the caller drops fail-open, behaviour 1 -- out_buf stays empty). */
static int hh_cut(char *buf, size_t cap, size_t len);

static int
hh_build_section(struct hh *h, const char *run_name,
                 const char *records_dir, const char *notes_dir,
                 char *out_buf, size_t out_cap)
{
    char *full;
    size_t full_len;
    int cut_len;

    if (out_cap < 2) {
        return 0;
    }
    full = hh_build_section_unbounded(h, run_name, records_dir, notes_dir);
    if (full == NULL) {
        return 0;
    }
    full_len = strlen(full);
    cut_len = hh_cut(full, full_len + 1, full_len);
    /* +2: the leading newline plus the NUL terminator (the value returned
     * excludes the NUL -- it is the file's byte count, the leading \n in). */
    if ((size_t)cut_len + 2 > out_cap) {
        free(full);
        return 0;
    }
    out_buf[0] = '\n';
    memcpy(out_buf + 1, full, (size_t)cut_len + 1);
    free(full);
    return cut_len + 1;
}

/* The 6000-byte cut (behaviour 10, handoff.py 157-158): when the finished
 * section exceeds 6000 BYTES, truncate to the first 6000 bytes, drop a
 * partial UTF-8 sequence at the cut (the reference's decode("utf-8",
 * "ignore")), back to the last newline (the reference's rsplit("\n", 1)[0]),
 * and append the marker line. The section is prompt BUDGET: a section that
 * eats the model's context to report on the previous run is worse than no
 * section. Returns the finished byte length (the cut section when it fired,
 * the original length otherwise). */
static int
hh_cut(char *buf, size_t cap, size_t len)
{
    size_t p;
    size_t content_end;
    size_t nidx;
    size_t i;
    int found;
    size_t newlen;

    if (len <= (size_t)HANDOFF_MAX_BYTES) {
        return (int)len;   /* no cut */
    }
    /* The first 6000 bytes, then drop a trailing partial UTF-8 sequence:
     * walk back over continuation bytes, then (if the last lead byte's
     * sequence does not fit within the cut) drop that sequence too. */
    p = (size_t)HANDOFF_MAX_BYTES;
    while (p > 0 && (buf[p - 1] & 0xC0) == 0x80) {
        p--;
    }
    if (p > 0) {
        unsigned char lead = (unsigned char)buf[p - 1];
        if (lead >= 0xC0 && lead < 0xF8) {
            size_t L = (lead < 0xE0) ? 2 : ((lead < 0xF0) ? 3 : 4);
            if ((p - 1) + L > (size_t)HANDOFF_MAX_BYTES) {
                p = p - 1;
            }
        }
    }
    /* Back to the last newline within buf[0..p) -- the reference's
     * rsplit("\n", 1)[0] drops the final newline and anything after it. */
    nidx = 0;
    found = 0;
    for (i = 0; i < p; i++) {
        if (buf[i] == '\n') {
            nidx = i;
            found = 1;
        }
    }
    content_end = (found != 0) ? nidx : p;
    /* Append the marker line, reserving its exact space so the append is
     * never truncated (cap >= 6144, content_end <= 6000, the marker is 24
     * bytes). */
    {
        const char *marker = "\n- *(cut at 6000 bytes)*\n";
        size_t mlen = strlen(marker);
        if (content_end + mlen + 1 > cap) {
            mlen = cap - content_end - 1;
        }
        memcpy(buf + content_end, marker, mlen);
        buf[content_end + mlen] = '\0';
        newlen = content_end + mlen;
    }
    buf[newlen] = '\0';
    return (int)newlen;
}

/* Collect the previous run's records into `h` (the arena is the caller's,
 * owned by the entry): meta.txt (an unreadable one is an EMPTY meta,
 * behaviour 4/13), stream.jsonl (a missing or unreadable one DROPS the
 * section, behaviour 1), notes/ (a directory that fails to list DROPS it,
 * behaviour 1). Returns 0 with `reason` set on a drop, 1 on success. */
static int
hh_collect(struct hh *h, const char *prev_run_dir, const char *prev_meta,
           const char *prev_notes_dir, const char **reason)
{
    char path[8192];
    h->rc = NULL; h->start = NULL; h->end = NULL; h->team_head_after = NULL;
    h->last_msg = jc_arena_strdup(h->a, "");
    h->todo_args = NULL;
    jc_vec_init(&h->paths, sizeof(struct hh_path));
    jc_vec_init(&h->batches, sizeof(struct hh_batch));
    jc_vec_init(&h->res_ids, sizeof(char *));
    jc_vec_init(&h->res_prevs, sizeof(char *));
    jc_vec_init(&h->notes, sizeof(char *));
    /* Reserve one slot so the empty case already has data (a push_slot or
     * push below returns NULL only on a real allocation failure). */
    if (h->last_msg == NULL ||
        jc_vec_reserve(&h->paths, 1) != JC_OK ||
        jc_vec_reserve(&h->batches, 1) != JC_OK ||
        jc_vec_reserve(&h->res_ids, 1) != JC_OK ||
        jc_vec_reserve(&h->res_prevs, 1) != JC_OK ||
        jc_vec_reserve(&h->notes, 1) != JC_OK) {
        *reason = "the handoff section ran out of memory";
        return 0;
    }
    /* meta.txt: the previous meta path is the caller's (the run knows it);
     * NULL or missing is the EMPTY meta. */
    if (prev_meta != NULL) {
        hh_read_meta(h, prev_meta);
    } else {
        char tmp[8192];
        if (jc_snprintf(tmp, sizeof tmp, "%s/meta.txt", prev_run_dir)
            < (int)sizeof tmp) {
            hh_read_meta(h, tmp);
        }
    }
    if (h->rc == NULL || h->start == NULL || h->end == NULL ||
        h->team_head_after == NULL) {
        *reason = "the handoff section ran out of memory";
        return 0;
    }
    if (jc_snprintf(path, sizeof path, "%s/stream.jsonl", prev_run_dir)
        >= (int)sizeof path) {
        *reason = "the previous run's path is too long";
        return 0;
    }
    if (!hh_read_stream(h, path, reason)) {
        return 0;
    }
    if (prev_notes_dir != NULL) {
        if (!hh_list_notes(h, prev_notes_dir, reason)) {
            return 0;
        }
    } else {
        if (jc_snprintf(path, sizeof path, "%s/notes", prev_run_dir)
            < (int)sizeof path) {
            if (!hh_list_notes(h, path, reason)) {
                return 0;
            }
        }
    }
    return 1;
}

jc_status
jc_harness_build_handoff(const char *prev_run_dir, const char *prev_meta,
                         const char *prev_notes_dir, const char *records_dir,
                         char *out_buf, size_t out_cap,
                         char *err_out, size_t err_cap,
                         int *out_bytes)
{
    struct hh h;
    struct jc_arena *a;
    const char *reason = NULL;
    const char *run_name;
    const char *notes_path;
    char notes_dir[8192];
    int ok;

    if (out_buf != NULL && out_cap > 0) {
        out_buf[0] = '\0';
    }
    if (err_out != NULL && err_cap > 0) {
        err_out[0] = '\0';
    }
    if (out_bytes != NULL) {
        *out_bytes = 0;
    }
    if (err_out == NULL || err_cap < 128 || out_buf == NULL ||
        out_cap < JC_HARNESS_HANDOFF_OUT_CAP) {
        return JC_OK;   /* the caller's buffers are its own contract */
    }
    /* Behaviour 2: no previous run (a first run, a run number of 0, a
     * non-numeric run name) is NOT a failure -- the section is empty, no
     * reason is filed, and the run proceeds. */
    if (prev_run_dir == NULL || prev_run_dir[0] == '\0') {
        return JC_OK;
    }
    a = jc_arena_new(0);
    if (a == NULL) {
        jc_snprintf(err_out, err_cap,
                    "handoff: the handoff section ran out of memory\n");
        return JC_OK;
    }
    memset(&h, 0, sizeof h);
    h.a = a;
    run_name = hh_basename(prev_run_dir, a);
    if (prev_notes_dir != NULL) {
        notes_path = prev_notes_dir;
    } else {
        if (jc_snprintf(notes_dir, sizeof notes_dir, "%s/notes",
                        prev_run_dir) >= (int)sizeof notes_dir) {
            jc_arena_free(a);
            return JC_OK;   /* too long: no section, fail open
                                 (a records path this long cannot serve) */
        }
        notes_path = notes_dir;
    }
    ok = hh_collect(&h, prev_run_dir, prev_meta, prev_notes_dir, &reason);
    if (!ok) {
        hh_fail(&h, err_out, err_cap, reason);
        /* The five jc_vec backings are arena-OWNED pointers, but their
         * realloc'd buffers are heap (the arena does not know them) -- free
         * them here before the arena, on every exit path (the qwen27b
         * review's leak). */
        jc_vec_free(&h.paths);
        jc_vec_free(&h.batches);
        jc_vec_free(&h.res_ids);
        jc_vec_free(&h.res_prevs);
        jc_vec_free(&h.notes);
        jc_arena_free(a);
        return JC_OK;
    }
    /* The section is assembled unbounded, cut at the 6000-byte boundary
     * (behaviour 10; a no-op when under the cap), and the cut result is
     * copied into out_buf; hh_build_section returns its finished byte
     * length, 0 on OOM (the drop fail-open, behaviour 1). */
    ok = hh_build_section(&h, run_name, records_dir, notes_path,
                          out_buf, out_cap);
    if (ok == 0) {
        hh_fail(&h, err_out, err_cap,
                "the handoff section did not fit its buffer");
        jc_vec_free(&h.paths);
        jc_vec_free(&h.batches);
        jc_vec_free(&h.res_ids);
        jc_vec_free(&h.res_prevs);
        jc_vec_free(&h.notes);
        jc_arena_free(a);
        return JC_OK;
    }
    if (out_bytes != NULL) {
        *out_bytes = ok;
    }
    jc_vec_free(&h.paths);
    jc_vec_free(&h.batches);
    jc_vec_free(&h.res_ids);
    jc_vec_free(&h.res_prevs);
    jc_vec_free(&h.notes);
    jc_arena_free(a);
    return JC_OK;
}
