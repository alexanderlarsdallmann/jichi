/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_session.c - session save/load round-trip. */

#include "jc_test.h"
#include "jc_session.h"
#include "jc_version.h"
#include "jc_perm.h"
#include "jc_mem.h"
#include "jc_snprintf.h"
#include "jc_str.h"
#include "jc_json.h"
#include "jc_fault.h"
#include <stdlib.h>
#include <string.h>

void test_session(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_session s;
    struct jc_session loaded;
    struct jc_message *asst;
    char id_copy[64];
    const char *idp = id_copy; /* pointer form avoids array!=NULL warnings */

    /* Keep test artifacts out of the real home directory. */
    setenv("HOME", jc_test_tmp("jichi_home_test"), 1);

    jc_session_new(&s, jc_test_tmp("workdir"), a);
    JC_CHECK(s.mode == JC_MODE_CHAT); /* default for a fresh session */
    s.mode = JC_MODE_PLAN;            /* persisted across save/load */
    jc_history_add(&s.history, JC_ROLE_USER, "list the files");
    asst = jc_history_add(&s.history, JC_ROLE_ASSISTANT, NULL);
    jc_msg_add_tool_call(asst, "call_1", "list_files", "{\"path\":\".\"}");
    jc_history_add_tool_result(&s.history, "call_1", "a.txt\nb.txt", 0);
    jc_session_autotitle(&s);

    JC_CHECK_STR(s.title, "list the files");
    JC_CHECK(jc_session_save(&s) == JC_OK);
    strcpy(id_copy, s.id);

    /* M140: the save is unformatted -- no literal newlines/indentation (the
     * pretty printer doubled the string + file for an every-turn write).
     * Content newlines are JSON-escaped, so a raw '\n' would mean pretty. */
    {
        char spath[256];
        char *raw = NULL;
        jc_snprintf(spath, sizeof(spath),
                    "%s/jichi_home_test/.jichi.d/sessions/%s.json", jc_test_tmpdir(), idp);
        JC_CHECK(jc_read_file(spath, &raw, NULL, a) == JC_OK);
        JC_CHECK(raw != NULL && strchr(raw, '\n') == NULL);
        JC_CHECK(raw != NULL && strstr(raw, "\"history\":[") != NULL);
        /* M290: the build that last SAVED it. A session is rewritten every turn
         * and may be resumed or archived across a version change, so "which
         * jichi wrote this" is otherwise unrecoverable. Asserted against the
         * real file, since a hand-built fixture would prove nothing about the
         * writer -- the gap that let M290's first red-before-green pass report a
         * missing journal stamp as green. */
        JC_CHECK(raw != NULL &&
                 strstr(raw, "\"jichi\":\"" JC_VERSION "\"") != NULL);
    }

    /* Reload by id and verify the history round-trips. */
    JC_CHECK(jc_session_load_by_id(id_copy, &loaded, a) == JC_OK);
    JC_CHECK_STR(loaded.id, idp);
    JC_CHECK_STR(loaded.workspace, jc_test_tmp("workdir"));
    JC_CHECK(loaded.mode == JC_MODE_PLAN); /* mode survives the round-trip */
    JC_CHECK(jc_history_len(&loaded.history) == 3);

    {
        struct jc_message *m0 = jc_history_get(&loaded.history, 0);
        struct jc_message *m1 = jc_history_get(&loaded.history, 1);
        struct jc_message *m2 = jc_history_get(&loaded.history, 2);
        JC_CHECK(m0->role == JC_ROLE_USER);
        JC_CHECK_STR(m0->content, "list the files");
        JC_CHECK(m1->role == JC_ROLE_ASSISTANT);
        JC_CHECK(jc_msg_tool_call_count(m1) == 1);
        JC_CHECK_STR(jc_msg_tool_call_at(m1, 0)->name, "list_files");
        JC_CHECK(m2->role == JC_ROLE_TOOL);
        JC_CHECK_STR(m2->tool_call_id, "call_1");
        JC_CHECK_STR(m2->content, "a.txt\nb.txt");
    }

    /* Recent-load should find the one we just wrote. */
    {
        struct jc_session recent;
        JC_CHECK(jc_session_load_recent(&recent, a) == JC_OK);
        JC_CHECK_STR(recent.id, idp);
        jc_session_free(&recent);
    }

    /* Markdown export (M34b): the transcript carries the title, role headings,
     * content, a fenced tool call, and the tool result -- but not the system
     * prompt. */
    {
        struct jc_sb sb;
        jc_sb_init(&sb);
        JC_CHECK(jc_session_render(&loaded, JC_EXPORT_MD, &sb) == JC_OK);
        JC_CHECK(sb.data != NULL);
        if (sb.data != NULL) {
            JC_CHECK(strstr(sb.data, "# list the files") != NULL);
            JC_CHECK(strstr(sb.data, "## User") != NULL);
            JC_CHECK(strstr(sb.data, "## Assistant") != NULL);
            JC_CHECK(strstr(sb.data, "**Tool call:** `list_files`") != NULL);
            JC_CHECK(strstr(sb.data, "## Tool result") != NULL);
            JC_CHECK(strstr(sb.data, "a.txt\nb.txt") != NULL);
            JC_CHECK(strstr(sb.data, "**Mode:** plan") != NULL);
        }
        jc_sb_free(&sb);
    }

    /* HTML export: a self-contained document with escaped content. */
    {
        struct jc_session h;
        struct jc_message *u;
        struct jc_sb sb;
        jc_session_new(&h, jc_test_tmp("workdir"), a);
        u = jc_history_add(&h.history, JC_ROLE_USER, "tags <b> & <i> here");
        (void)u;
        jc_sb_init(&sb);
        JC_CHECK(jc_session_render(&h, JC_EXPORT_HTML, &sb) == JC_OK);
        if (sb.data != NULL) {
            JC_CHECK(strstr(sb.data, "<!doctype html>") != NULL);
            JC_CHECK(strstr(sb.data, "</html>") != NULL);
            /* The metacharacters are escaped, not emitted raw. */
            JC_CHECK(strstr(sb.data, "&lt;b&gt; &amp; &lt;i&gt;") != NULL);
            JC_CHECK(strstr(sb.data, "<b> &") == NULL);
        }
        jc_sb_free(&sb);
        jc_session_free(&h);
    }

    /* JSON export (M165): a machine projection a supervisor reads instead of
     * replaying a session. Valid JSON; system prompt omitted; the assistant's
     * tool_call round-trips id/name and its arguments as a *parsed object*
     * (not a re-escaped string); the tool result links by tool_call_id. */
    {
        struct jc_sb sb;
        cJSON *root, *msgs, *m, *tcs, *tc, *args;
        int saw_asst_call = 0, saw_tool = 0;
        int i, n;
        jc_sb_init(&sb);
        JC_CHECK(jc_session_render(&loaded, JC_EXPORT_JSON, &sb) == JC_OK);
        JC_CHECK(sb.data != NULL);
        root = cJSON_Parse(sb.data ? sb.data : "");
        JC_CHECK(root != NULL); /* the render is well-formed JSON */
        if (root != NULL) {
            JC_CHECK(cJSON_GetObjectItem(root, "v") != NULL);
            JC_CHECK_STR(cJSON_GetObjectItem(root, "id")->valuestring, idp);
            JC_CHECK_STR(cJSON_GetObjectItem(root, "workspace")->valuestring,
                         jc_test_tmp("workdir"));
            msgs = cJSON_GetObjectItem(root, "messages");
            JC_CHECK(cJSON_IsArray(msgs));
            n = cJSON_GetArraySize(msgs);
            JC_CHECK(n == 3); /* system prompt omitted; user/asst/tool kept */
            for (i = 0; i < n; i++) {
                const char *role;
                m = cJSON_GetArrayItem(msgs, i);
                role = cJSON_GetObjectItem(m, "role")->valuestring;
                if (strcmp(role, "assistant") == 0) {
                    tcs = cJSON_GetObjectItem(m, "tool_calls");
                    if (tcs != NULL && cJSON_GetArraySize(tcs) == 1) {
                        tc = cJSON_GetArrayItem(tcs, 0);
                        JC_CHECK_STR(cJSON_GetObjectItem(tc, "id")->valuestring,
                                     "call_1");
                        JC_CHECK_STR(
                            cJSON_GetObjectItem(tc, "name")->valuestring,
                            "list_files");
                        args = cJSON_GetObjectItem(tc, "arguments");
                        /* embedded as a parsed object, not a string blob */
                        JC_CHECK(cJSON_IsObject(args));
                        if (cJSON_IsObject(args)) {
                            JC_CHECK_STR(
                                cJSON_GetObjectItem(args, "path")->valuestring,
                                ".");
                        }
                        saw_asst_call = 1;
                    }
                } else if (strcmp(role, "tool") == 0) {
                    JC_CHECK_STR(
                        cJSON_GetObjectItem(m, "tool_call_id")->valuestring,
                        "call_1");
                    saw_tool = 1;
                }
            }
            JC_CHECK(saw_asst_call == 1);
            JC_CHECK(saw_tool == 1);
            cJSON_Delete(root);
        }
        jc_sb_free(&sb);
    }

    /* Fork (M35b): a new id, a deep copy of the history, and an independent
     * conversation -- appending to the fork must not touch the original. */
    {
        struct jc_session fork;
        JC_CHECK(jc_session_fork(&loaded, &fork, a) == JC_OK);
        JC_CHECK(fork.id != NULL && strcmp(fork.id, loaded.id) != 0);
        JC_CHECK(fork.mode == loaded.mode);
        JC_CHECK(jc_history_len(&fork.history) ==
                 jc_history_len(&loaded.history));
        /* The title carries a fork marker. */
        JC_CHECK(fork.title != NULL && strstr(fork.title, "(fork)") != NULL);
        /* Copied content matches. */
        {
            struct jc_message *o1 = jc_history_get(&loaded.history, 1);
            struct jc_message *f1 = jc_history_get(&fork.history, 1);
            JC_CHECK(f1->role == o1->role);
            JC_CHECK(jc_msg_tool_call_count(f1) == jc_msg_tool_call_count(o1));
            if (jc_msg_tool_call_count(f1) == 1) {
                JC_CHECK_STR(jc_msg_tool_call_at(f1, 0)->name, "list_files");
            }
        }
        /* Independence: growing the fork leaves the original unchanged. */
        {
            jc_size before = jc_history_len(&loaded.history);
            jc_history_add(&fork.history, JC_ROLE_USER, "fork-only message");
            JC_CHECK(jc_history_len(&loaded.history) == before);
            JC_CHECK(jc_history_len(&fork.history) == before + 1);
        }
        jc_session_free(&fork);
    }

    /* M108: alias validation (pure) + quick-find round-trip + delete. */
    JC_CHECK(jc_session_alias_valid("wip") == 1);
    JC_CHECK(jc_session_alias_valid("my-feature.2") == 1);
    JC_CHECK(jc_session_alias_valid("has space") == 0);
    JC_CHECK(jc_session_alias_valid("has/slash") == 0);
    JC_CHECK(jc_session_alias_valid("") == 0);
    JC_CHECK(jc_session_alias_valid(NULL) == 0);
    {
        struct jc_session named;
        char rid[64];
        jc_session_new(&named, jc_test_tmp("workdir"), a);
        jc_session_set_alias(&named, "quickname");
        JC_CHECK(named.alias != NULL);
        jc_session_save(&named);
        /* resolve by alias -> the session id */
        JC_CHECK(jc_session_resolve_alias("quickname", rid, sizeof rid, a) == 0);
        JC_CHECK(strcmp(rid, named.id) == 0);
        JC_CHECK(jc_session_resolve_alias("nope", rid, sizeof rid, a) == -1);
        /* M198: two sessions sharing an alias must be reported AMBIGUOUS (-2),
         * not silently resolved to whichever the scan reached first. Flagged as
         * unverified in the M198 write-up (the probe used `export @alias`, which
         * does not accept alias syntax and therefore proved nothing); checked
         * here at the API where the contract actually lives. */
        {
            struct jc_session dup;
            char aid[64];
            jc_session_new(&dup, jc_test_tmp("workdir"), a);
            jc_session_set_alias(&dup, "quickname");
            jc_session_save(&dup);
            JC_CHECK(jc_session_resolve_alias("quickname", aid, sizeof aid, a)
                     == -2);
            JC_CHECK(jc_session_delete(dup.id) == JC_OK);
            /* ...and with the duplicate gone it resolves cleanly again. */
            JC_CHECK(jc_session_resolve_alias("quickname", aid, sizeof aid, a)
                     == 0);
            jc_session_free(&dup);
        }
        /* delete removes the file; a second delete is idempotent (JC_OK). */
        JC_CHECK(jc_session_delete(named.id) == JC_OK);
        JC_CHECK(jc_session_delete(named.id) == JC_OK);
        JC_CHECK(jc_session_resolve_alias("quickname", rid, sizeof rid, a) == -1);
        jc_session_free(&named);
    }

#ifdef JC_FAULT
    /* M198: a failing atomic write must be REPORTED, not swallowed. Only
     * reachable with the injector compiled in (make FAULT=1); the site has no
     * non-networked caller, so this is its only home. See tests/smoke/faults.sh. */
    {
        struct jc_session fs;
        jc_session_new(&fs, jc_test_tmp("workdir"), a);
        jc_history_add(&fs.history, JC_ROLE_USER, "x");
        setenv("JICHI_FAULT_WRITE_AFTER", "0", 1);
        jc_fault_reset();
        JC_CHECK(jc_session_save(&fs) != JC_OK);
        /* ...and the failure is transient, not sticky: with the fault cleared
         * the same session saves normally. */
        unsetenv("JICHI_FAULT_WRITE_AFTER");
        jc_fault_reset();
        JC_CHECK(jc_session_save(&fs) == JC_OK);
        jc_session_delete(fs.id);
        jc_session_free(&fs);
    }
#endif

    jc_session_free(&loaded);
    jc_session_free(&s);
    jc_arena_free(a);
}

/* M232: the streamed serializer must produce output BYTE-IDENTICAL to a
 * whole-tree cJSON print. Proof without touching the (static) message_to_json:
 * parse the streamed output and re-print it canonically -- a correct stream is
 * already in cJSON's canonical unformatted form, so the two must be equal byte
 * for byte. Stressed with content that exercises JSON escaping (quotes,
 * backslashes, tabs, newlines) in both the metadata and the messages, plus a
 * tool call and an error tool result. A broken splice (missing comma, stray
 * brace) makes the streamed text invalid JSON, so cJSON_Parse returns NULL and
 * the test fails -- the guard has teeth. */
void test_session_serialize(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_session s;
    struct jc_message *asst;
    char *s1;
    cJSON *root;

    setenv("HOME", jc_test_tmp("jichi_home_test"), 1);
    jc_session_new(&s, "/tmp/work \"dir\"\\path", a); /* quotes + backslash */
    s.mode = JC_MODE_AUTO;
    jc_session_set_title(&s, "quote \" and \\ and\nnewline");
    jc_history_add(&s.history, JC_ROLE_USER,
                   "tabs\tquotes \" backslash \\ newline\n end");
    asst = jc_history_add(&s.history, JC_ROLE_ASSISTANT, "ok");
    jc_msg_add_tool_call(asst, "c1", "run", "{\"cmd\":\"echo \\\"hi\\\"\"}");
    jc_history_add_tool_result(&s.history, "c1", "err \" out", 1);

    s1 = jc_session_serialize(&s);
    JC_CHECK(s1 != NULL);
    root = cJSON_Parse(s1 != NULL ? s1 : "");
    JC_CHECK(root != NULL); /* the streamed output is valid JSON */
    if (root != NULL && s1 != NULL) {
        char *s2 = cJSON_PrintUnformatted(root);
        JC_CHECK(s2 != NULL);
        JC_CHECK(s2 != NULL && strcmp(s1, s2) == 0); /* == whole-tree print */
        /* the splice landed correctly */
        JC_CHECK(strstr(s1, "\"sessionId\":") != NULL);
        JC_CHECK(strstr(s1, ",\"history\":[") != NULL);
        JC_CHECK(strchr(s1, '\n') == NULL); /* unformatted (M140 preserved) */
        JC_CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(root, "history")) == 3);
        free(s2);
    }
    cJSON_Delete(root);
    free(s1);
    jc_session_free(&s);
    jc_arena_free(a);
}

/* --- M197: listing footprint -------------------------------------------------
 *
 * jc_session_list used to read every session file's FULL TEXT onto the caller's
 * arena, which in the TUI is app->arena -- freed only at process exit. One
 * /sessions on a 250-file/17.9 MB store therefore retained 17.5 MB forever, and
 * so did every Tab press on `/resume `. It only ever needed four scalars and the
 * history array's length; the cJSON tree that held them was correctly deleted,
 * so the retention was exactly backwards.
 *
 * These assertions cannot be replaced by ASan or valgrind --leak-check: main
 * frees the arena on every exit path, and until then the blocks are reachable
 * from a live root, so both report ZERO. Only a footprint assertion sees it.
 */

/* Arena bytes charged by jc_session_list for a store of `n` sessions whose
 * assistant message is `payload` bytes. Also reports the cost of a SECOND scan
 * on the same arena via *second, and the cost of one jc_session_load_by_id via
 * *load_cost. Store lives under a tag-scoped HOME so runs never collide. */
static jc_size list_cost(const char *tag, int n, jc_size payload,
                         jc_size *second, jc_size *load_cost)
{
    char home[128];
    struct jc_arena *build;
    struct jc_arena *m;
    struct jc_vec v1, v2;
    char *big;
    char first_id[64];
    jc_size u1;
    int i;

    jc_snprintf(home, sizeof home, "%s/jichi_lf_%s", jc_test_tmpdir(), tag);
    setenv("HOME", home, 1);

    big = (char *)malloc(payload + 1);
    JC_CHECK(big != NULL);
    memset(big, 'a', payload);
    big[payload] = '\0';

    /* Build the store on its own arena so the build cost is not measured. */
    build = jc_arena_new(0);
    first_id[0] = '\0';
    for (i = 0; i < n; i++) {
        struct jc_session s;
        jc_session_new(&s, jc_test_tmp("lf-ws"), build);
        jc_history_add(&s.history, JC_ROLE_USER, "q");
        jc_history_add(&s.history, JC_ROLE_ASSISTANT, big);
        JC_CHECK(jc_session_save(&s) == JC_OK);
        if (first_id[0] == '\0') jc_snprintf(first_id, sizeof first_id, "%s", s.id);
        jc_session_free(&s);
    }
    free(big);
    jc_arena_free(build);

    m = jc_arena_new(0);
    jc_vec_init(&v1, sizeof(struct jc_session_meta));
    JC_CHECK(jc_session_list(&v1, m) == JC_OK);
    JC_CHECK(v1.len == (jc_size)n); /* the store really is what we think */
    u1 = jc_arena_used(m, NULL);

    /* A second scan on the same arena must cost the same again, not more --
     * catches a partial fix that caches but still grows. */
    jc_vec_init(&v2, sizeof(struct jc_session_meta));
    JC_CHECK(jc_session_list(&v2, m) == JC_OK);
    *second = jc_arena_used(m, NULL) - u1;

    /* The sibling site: jc_session_load_by_id read the file onto the caller's
     * arena too, though load_from_text copies all it needs into malloc'd
     * history. Resuming a 64 KB session must not cost 64 KB of session arena. */
    {
        struct jc_arena *m2 = jc_arena_new(0);
        struct jc_session ls;
        JC_CHECK(jc_session_load_by_id(first_id, &ls, m2) == JC_OK);
        JC_CHECK(jc_history_len(&ls.history) == 2); /* it really loaded */
        *load_cost = jc_arena_used(m2, NULL);
        jc_session_free(&ls);
        jc_arena_free(m2);
    }

    /* Leave the store clean for the next call and for repeat runs. */
    for (i = 0; i < (int)v1.len; i++) {
        struct jc_session_meta *mm =
            (struct jc_session_meta *)jc_vec_at(&v1, (jc_size)i);
        jc_session_delete(mm->id);
    }
    jc_vec_free(&v1);
    jc_vec_free(&v2);
    jc_arena_free(m);
    return u1;
}

/* M218: jc_session_save skips the write when nothing changed since the last
 * save. Up to six TUI call sites can fire at one turn boundary, each paying a
 * full history re-serialize (2-3x the session text) and a file rewrite; the
 * skip makes the extras free. Method: save, overwrite the file with a
 * sentinel byte string BEHIND the session's back, save again with no
 * changes -- the sentinel must survive (i.e. the redundant save was skipped);
 * then each kind of change (history append, elision-style content change via
 * the gen bump, mode flip, title) must make the next save write again. */
void test_session_dirty_skip(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_session s;
    char path[300];
    char *raw = NULL;

    setenv("HOME", jc_test_tmp("jichi_home_dirty_test"), 1);
    { char cmd[128]; sprintf(cmd, "rm -rf %s", jc_test_tmp("jichi_home_dirty_test"));
      if (system(cmd) != 0) { /* ignore */ } }

    jc_session_new(&s, jc_test_tmp("workdir"), a);
    jc_history_add(&s.history, JC_ROLE_USER, "hello");
    JC_CHECK(jc_session_save(&s) == JC_OK);
    jc_snprintf(path, sizeof(path),
                "%s/jichi_home_dirty_test/.jichi.d/sessions/%s.json", jc_test_tmpdir(), s.id);

    /* No changes => the save must be skipped (sentinel survives). */
    JC_CHECK(jc_write_file(path, "SENTINEL", 8) == JC_OK);
    JC_CHECK(jc_session_save(&s) == JC_OK);
    JC_CHECK(jc_read_file(path, &raw, NULL, a) == JC_OK);
    JC_CHECK(raw != NULL && strcmp(raw, "SENTINEL") == 0);

    /* History append => written again. */
    jc_history_add(&s.history, JC_ROLE_ASSISTANT, "hi");
    JC_CHECK(jc_session_save(&s) == JC_OK);
    raw = NULL;
    JC_CHECK(jc_read_file(path, &raw, NULL, a) == JC_OK);
    JC_CHECK(raw != NULL && strstr(raw, "\"history\"") != NULL);

    /* Mode change => written again. */
    JC_CHECK(jc_write_file(path, "SENTINEL", 8) == JC_OK);
    s.mode = JC_MODE_AUTO;
    JC_CHECK(jc_session_save(&s) == JC_OK);
    raw = NULL;
    JC_CHECK(jc_read_file(path, &raw, NULL, a) == JC_OK);
    JC_CHECK(raw != NULL && strstr(raw, "\"auto\"") != NULL);

    /* Title change => written again. */
    JC_CHECK(jc_write_file(path, "SENTINEL", 8) == JC_OK);
    jc_session_set_title(&s, "new title");
    JC_CHECK(jc_session_save(&s) == JC_OK);
    raw = NULL;
    JC_CHECK(jc_read_file(path, &raw, NULL, a) == JC_OK);
    JC_CHECK(raw != NULL && strstr(raw, "new title") != NULL);

    /* A gen bump without an append (the mid-turn elision path calls it on
     * content changes) => written again. */
    JC_CHECK(jc_write_file(path, "SENTINEL", 8) == JC_OK);
    s.history.gen++;
    JC_CHECK(jc_session_save(&s) == JC_OK);
    raw = NULL;
    JC_CHECK(jc_read_file(path, &raw, NULL, a) == JC_OK);
    JC_CHECK(raw != NULL && strstr(raw, "\"history\"") != NULL);

    jc_session_free(&s);
    { char cmd[128]; sprintf(cmd, "rm -rf %s", jc_test_tmp("jichi_home_dirty_test"));
      if (system(cmd) != 0) { /* ignore */ } }
    jc_arena_free(a);
}

/* M219: the prune selector. Pure: given listing metas (newest-first, the
 * jc_session_list order), keep-N and older-than-cutoff criteria, mark which
 * entries should be deleted. Both criteria must agree when both are given
 * (AND -- pruning is destructive, so the conservative combination). */
void test_session_prune_select(void)
{
    struct jc_session_meta m[5];
    int del[5];
    jc_size i, n;

    /* mtimes newest-first: 1000, 900, 800, 700, 600 */
    for (i = 0; i < 5; i++) {
        memset(&m[i], 0, sizeof(m[i]));
        m[i].mtime = 1000.0 - (double)(i * 100);
    }

    /* keep 2 newest => 3 deletions (indices 2,3,4). */
    n = jc_session_prune_select(m, 5, 2, -1.0, del);
    JC_CHECK(n == 3);
    JC_CHECK(del[0] == 0 && del[1] == 0 && del[2] && del[3] && del[4]);

    /* older than mtime 750 => indices 3,4. */
    n = jc_session_prune_select(m, 5, -1, 750.0, del);
    JC_CHECK(n == 2);
    JC_CHECK(!del[0] && !del[1] && !del[2] && del[3] && del[4]);

    /* both: keep 1 AND older-than 650 => only index 4 (3 is kept by age). */
    n = jc_session_prune_select(m, 5, 1, 650.0, del);
    JC_CHECK(n == 1);
    JC_CHECK(!del[3] && del[4]);

    /* no criteria => nothing selected (refuse-by-default). */
    n = jc_session_prune_select(m, 5, -1, -1.0, del);
    JC_CHECK(n == 0);

    /* keep >= count => nothing. */
    n = jc_session_prune_select(m, 5, 10, -1.0, del);
    JC_CHECK(n == 0);
}

void test_session_footprint(void)
{
    jc_size small, large, s2, l2, sload, lload;

    /* 8 sessions of ~0.5 KB vs 8 of 64 KB: a 128x payload difference. */
    small = list_cost("s", 8, 512, &s2, &sload);
    large = list_cost("l", 8, 64u * 1024u, &l2, &lload);

    /* (1) Absolute: eight sessions' worth of METADATA (id + title + workspace
     *     + dirent name) is far under 8 KB, whatever the files contain. */
    JC_CHECK(large < 8u * 1024u);
    JC_CHECK(small < 8u * 1024u);

    /* (2) Shape -- the assertion that survives a refactor. A 128x bigger
     *     payload must not make LISTING cost more: jc_session_list needs four
     *     scalars and cJSON_GetArraySize(history), never the file text. Before
     *     M197 this was ~530 KB vs ~12 KB. */
    JC_CHECK(large < small + 4096u);

    /* (3) Per-call stability: the Nth scan costs what the 1st did. */
    JC_CHECK(s2 < 8u * 1024u);
    JC_CHECK(l2 < 8u * 1024u);
    JC_CHECK(l2 < s2 + 4096u);

    /* (4) Resuming must not retain the session file either. */
    JC_CHECK(lload < 8u * 1024u);
    JC_CHECK(lload < sload + 4096u);
}

/* --- M206: foreign non-session .json files must not be listed ---------------
 *
 * A store can hold `.json` files that are not jichi sessions: Continue CLI's
 * sessions.json index (a JSON ARRAY), a stray config, an editor scratch file.
 * jc_session_list took identity from the FILENAME stem (M198), so a foreign file
 * whose content had no sessionId was still listed as an "(untitled)" row named
 * after its stem -- e.g. Continue's sessions.json surfaced as a phantom
 * "sessions" session. It must be skipped, and NOT counted as an unreadable
 * session (it was never one).
 */
void test_session_foreign_file(void)
{
    struct jc_arena *a = jc_arena_new(0);
    char dir[256];
    char path[400];
    struct jc_session real;
    struct jc_vec metas;
    int skipped = -1;

    setenv("HOME", jc_test_tmp("jichi_foreign_test"), 1);
    { char cmd[300]; sprintf(cmd, "rm -rf %s", jc_test_tmp("jichi_foreign_test"));
      if (system(cmd) != 0) { /* ignore */ } }
    jc_snprintf(dir, sizeof dir,
                jc_test_tmp("jichi_foreign_test/.jichi.d/sessions"));
    JC_CHECK(jc_mkdir_p(dir) == JC_OK);

    /* One genuine session. */
    jc_session_new(&real, jc_test_tmp("ws"), a);
    jc_history_add(&real.history, JC_ROLE_USER, "hi");
    jc_session_autotitle(&real);
    JC_CHECK(jc_session_save(&real) == JC_OK);

    /* Continue CLI's index: a top-level JSON ARRAY (hits the cJSON fallback,
     * which the M202 scanner declines because it is not an object). */
    jc_snprintf(path, sizeof path, "%s/sessions.json", dir);
    {
        const char *idx =
            "[{\"sessionId\":\"x\",\"title\":\"t\",\"messageCount\":1}]";
        JC_CHECK(jc_write_file(path, idx, (jc_size)strlen(idx)) == JC_OK);
    }

    /* A top-level OBJECT with a history array but NO sessionId (exercises the
     * scanner path, which returns has_history=1 has_id=0). Also not a session. */
    jc_snprintf(path, sizeof path, "%s/notasession.json", dir);
    {
        const char *o = "{\"foo\":1,\"history\":[{\"role\":\"user\"}]}";
        JC_CHECK(jc_write_file(path, o, (jc_size)strlen(o)) == JC_OK);
    }

    /* Only the real session is listed; the two foreign files are skipped and
     * NOT counted as unreadable (skipped stays 0 -- they were never sessions). */
    jc_vec_init(&metas, sizeof(struct jc_session_meta));
    JC_CHECK(jc_session_list_ex(&metas, a, &skipped) == JC_OK);
    JC_CHECK(metas.len == 1);
    JC_CHECK(skipped == 0);
    {
        struct jc_session_meta *m =
            (struct jc_session_meta *)jc_vec_at(&metas, 0);
        JC_CHECK_STR(m->id, real.id);       /* the real one, by its uuid stem */
        JC_CHECK_STR(m->title, "hi");
    }

    /* And a genuinely corrupt session (unparseable) is STILL counted -- the
     * M198 "sessions that cannot be read are reported" behaviour is unchanged;
     * only files that are clearly not sessions become silent. */
    jc_snprintf(path, sizeof path, "%s/cccccccc-0000-4000-8000-000000000009.json",
                dir);
    { const char *bad = "{\"sessionId\":\"cccccccc-";
      JC_CHECK(jc_write_file(path, bad, (jc_size)strlen(bad)) == JC_OK); }
    {
        struct jc_vec m2;
        int sk2 = 0;
        jc_vec_init(&m2, sizeof(struct jc_session_meta));
        JC_CHECK(jc_session_list_ex(&m2, a, &sk2) == JC_OK);
        JC_CHECK(m2.len == 1);   /* still just the real one */
        JC_CHECK(sk2 == 1);      /* the corrupt file IS reported */
        jc_vec_free(&m2);
    }

    jc_vec_free(&metas);
    jc_session_free(&real);
    { char cmd[300]; sprintf(cmd, "rm -rf %s", jc_test_tmp("jichi_foreign_test"));
      if (system(cmd) != 0) { /* ignore */ } }
    jc_arena_free(a);
}

/* M606: the store's shape version, and a pre-M606 file. A session saved by a
 * NEWER jichi loads anyway (with a warning the test cannot see -- the
 * conversation is the user's), a file with no "v" and no "todos" loads as v1
 * with an empty list, and the todos reader is lenient: an item without a
 * string content is skipped, a status alias is understood. */
void test_session_store_version(void)
{
    struct jc_arena *a = jc_arena_new(0);
    char dir[256];
    char path[400];
    struct jc_session s;

    setenv("HOME", jc_test_tmp("jichi_storev_test"), 1);
    { char cmd[300]; sprintf(cmd, "rm -rf %s", jc_test_tmp("jichi_storev_test"));
      if (system(cmd) != 0) { /* ignore */ } }
    jc_snprintf(dir, sizeof dir,
                jc_test_tmp("jichi_storev_test/.jichi.d/sessions"));
    JC_CHECK(jc_mkdir_p(dir) == JC_OK);

    /* A file from a newer jichi: v99, a todos array with one bad item. */
    jc_snprintf(path, sizeof path, "%s/aaaaaaaa-0000-4000-8000-000000000001.json",
                dir);
    {
        const char *j =
            "{\"sessionId\":\"aaaaaaaa-0000-4000-8000-000000000001\",\"v\":99,"
            "\"workspaceDirectory\":\"/w\",\"mode\":\"auto\","
            "\"history\":[{\"role\":\"user\",\"content\":\"hi\"}],"
            "\"todos\":[{\"status\":\"pending\"},"
            "{\"content\":\"kept\",\"status\":\"done\"},"
            "{\"content\":\"untyped\"}]}";
        JC_CHECK(jc_write_file(path, j, (jc_size)strlen(j)) == JC_OK);
    }
    memset(&s, 0, sizeof s);
    if (JC_REQUIRE(jc_session_load_by_id("aaaaaaaa-0000-4000-8000-000000000001",
                                         &s, a) == JC_OK)) {
        JC_CHECK(jc_history_len(&s.history) == 1);
        JC_CHECK(s.todos.items.len == 2); /* the content-less item is skipped */
        if (s.todos.items.len == 2) {
            struct jc_todo_item *i0 = (struct jc_todo_item *)jc_vec_at(&s.todos.items, 0);
            struct jc_todo_item *i1 = (struct jc_todo_item *)jc_vec_at(&s.todos.items, 1);
            JC_CHECK_STR(i0->content, "kept");
            JC_CHECK(i0->status == JC_TODO_DONE);      /* "done" is an alias */
            JC_CHECK_STR(i1->content, "untyped");
            JC_CHECK(i1->status == JC_TODO_PENDING);   /* no status: pending */
        }
        JC_CHECK(!jc_session_needs_save(&s));
        jc_session_free(&s);
    }

    /* A pre-M606 file: no "v", no "todos". */
    jc_snprintf(path, sizeof path, "%s/bbbbbbbb-0000-4000-8000-000000000002.json",
                dir);
    {
        const char *j =
            "{\"sessionId\":\"bbbbbbbb-0000-4000-8000-000000000002\","
            "\"workspaceDirectory\":\"/w\",\"mode\":\"chat\",\"history\":[]}";
        JC_CHECK(jc_write_file(path, j, (jc_size)strlen(j)) == JC_OK);
    }
    memset(&s, 0, sizeof s);
    if (JC_REQUIRE(jc_session_load_by_id("bbbbbbbb-0000-4000-8000-000000000002",
                                         &s, a) == JC_OK)) {
        JC_CHECK(s.todos.items.len == 0);
        /* ...and saving it writes the current shape: "v":2 and a todos array,
         * so the next reader can tell "no list" from "written before v2". */
        {
            char *text = jc_session_serialize(&s);
            JC_CHECK(text != NULL);
            if (text != NULL) {
                JC_CHECK(strstr(text, "\"v\":2") != NULL);
                JC_CHECK(strstr(text, ",\"todos\":[]}") != NULL);
                free(text);
            }
        }
        jc_session_free(&s);
    }

    { char cmd[300]; sprintf(cmd, "rm -rf %s", jc_test_tmp("jichi_storev_test"));
      if (system(cmd) != 0) { /* ignore */ } }
    setenv("HOME", jc_test_tmp("jichi_home_test"), 1);
    jc_arena_free(a);
}

/* M350: resume drift -- believed-path extraction (errored calls excluded,
 * apply_patch fans out, duplicates collapse, non-file tools ignored) and the
 * note renderer (bounded, honest zero). Deref checks are combined with their
 * guards: JC_CHECK counts and CONTINUES, so a separate null check would not
 * stop the next line from dereferencing NULL (the M349 rule). */
void test_session_drift(void)
{
    struct jc_history h;
    struct jc_message *asst;
    struct jc_vec paths;
    struct jc_sb sb;
    jc_size i;

    jc_history_init(&h);
    jc_history_add(&h, JC_ROLE_USER, "work");
    asst = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);
    jc_msg_add_tool_call(asst, "c1", "read_file", "{\"path\":\"src/a.c\"}");
    jc_history_add_tool_result(&h, "c1", "1  content", 0);
    asst = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);
    jc_msg_add_tool_call(asst, "c2", "edit_file",
        "{\"path\":\"gone.c\",\"old_string\":\"x\",\"new_string\":\"y\"}");
    jc_history_add_tool_result(&h, "c2", "error: old_string not found", 1);
    asst = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);
    jc_msg_add_tool_call(asst, "c3", "apply_patch",
        "{\"edits\":[{\"path\":\"src/b.c\"},{\"path\":\"src/a.c\"}]}");
    jc_history_add_tool_result(&h, "c3", "ok", 0);
    asst = jc_history_add(&h, JC_ROLE_ASSISTANT, NULL);
    jc_msg_add_tool_call(asst, "c4", "run_terminal_command",
        "{\"command\":\"cat src/z.c\"}");
    jc_history_add_tool_result(&h, "c4", "zzz", 0);

    jc_vec_init(&paths, sizeof(char *));
    jc_session_believed_paths(&h, &paths);
    /* src/a.c (read; deduped against the patch) and src/b.c (patch). The
     * errored edit and the shell command contribute no believed path. */
    JC_CHECK(paths.len == 2);
    if (JC_REQUIRE(paths.len >= 1)) {
        JC_CHECK_STR(JC_VEC_STR(&paths, 0), "src/a.c");
    }
    if (JC_REQUIRE(paths.len >= 2)) {
        JC_CHECK_STR(JC_VEC_STR(&paths, 1), "src/b.c");
    }
    for (i = 0; i < paths.len; i++) {
        free(JC_VEC_STR(&paths, i));
    }
    jc_vec_free(&paths);
    jc_history_free(&h);

    /* The renderer: an empty drift renders NOTHING (an unchanged workspace
     * needs no note), and a populated one is bounded and actionable. */
    jc_sb_init(&sb);
    jc_session_drift_render("", &sb);
    JC_CHECK(sb.len == 0);
    jc_session_drift_render(NULL, &sb);
    JC_CHECK(sb.len == 0);
    jc_session_drift_render("src/a.c\nMakefile\n", &sb);
    JC_CHECK(sb.data != NULL && strncmp(sb.data, "[resume]", 8) == 0);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "2 file(s)") != NULL);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "src/a.c, Makefile") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "re-read") != NULL);
    jc_sb_free(&sb);
    jc_sb_init(&sb);
    jc_session_drift_render("a\nb\nc\nd\ne\nf\ng\nh\ni\nj\n", &sb);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "10 file(s)") != NULL);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "(+2 more)") != NULL);
    JC_CHECK(sb.data == NULL || strstr(sb.data, ", i") == NULL);
    jc_sb_free(&sb);
}

/* M367: field-fidelity round-trip. Every jc_message field must survive
 * save -> load, or a resumed conversation silently changes meaning -- the
 * survey found the M334 `truncated` flag carried NOWHERE (save omitted it,
 * load never read it), so a session cut at the output ceiling reloaded as if
 * it had completed. This builds a maximally-populated history, saves it
 * through the real store, loads it back, and compares field by field;
 * session_fields_lint.sh is the build-time tripwire for the NEXT field. */
void test_session_roundtrip(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_session s;
    struct jc_session back;
    struct jc_message *m;
    int ok;

    /* Own store, and deleted on the way out: this test SAVES, and the shared
     * /tmp/jichi_home_test store feeds test_session's recent-load check --
     * accumulated same-second files there make the mtime tie-break flip on a
     * SECOND ./run_tests in a row (observed: green under make test, red
     * standalone right after). Isolation plus cleanup keeps every run
     * identical to the first. */
    /* `back` is freed on every path below, so it must be safe to free before it
     * is ever populated: jc_session_load_by_id returns early on failure WITHOUT
     * touching its out-param (correct, and the product's own callers all bail
     * before using or freeing it -- checked, M488), which left this test freeing
     * uninitialised stack memory. That, not the reads after it, is what produced
     * `free(): invalid pointer` and SIGABRT. */
    memset(&back, 0, sizeof back);

    setenv("HOME", jc_test_tmp("jichi_home_rt"), 1);
    jc_session_new(&s, jc_test_tmp("rt_work"), a);
    s.mode = JC_MODE_AUTO;
    jc_session_set_title(&s, "round trip");

    jc_history_add(&s.history, JC_ROLE_USER, "the task");
    m = jc_history_add(&s.history, JC_ROLE_ASSISTANT, "calling");
    jc_msg_add_tool_call(m, "c1", "read_file", "{\"path\":\"a b\"}");
    jc_msg_add_tool_call(m, "c2", "run", "{\"cmd\":\"echo \\\"x\\\"\"}");
    m->truncated = 1;                       /* the M334 verdict            */
    jc_history_add_tool_result(&s.history, "c1", "bytes here", 0);
    jc_history_add_tool_result(&s.history, "c2", "boom", 1); /* is_error   */
    m = jc_history_add(&s.history, JC_ROLE_ASSISTANT, "done");
    JC_CHECK(m != NULL && m->truncated == 0);

    /* M606: the task list rides with the session -- three items, one per
     * state, one with a quote in it (the codec must escape it). */
    {
        struct jc_todo_item it;
        it.content = jc_strdup("write the codec");
        it.status = JC_TODO_IN_PROGRESS;
        jc_vec_push(&s.todos.items, &it);
        it.content = jc_strdup("prove it red");
        it.status = JC_TODO_PENDING;
        jc_vec_push(&s.todos.items, &it);
        it.content = jc_strdup("survey \"quotes\"");
        it.status = JC_TODO_DONE;
        jc_vec_push(&s.todos.items, &it);
        s.todos.gen++;
    }

    /* PRECONDITIONS, not assertions (M488). These two were JC_CHECK, which
     * deliberately RECORDS AND CONTINUES -- so when the save failed, everything
     * below went on to read a `back` that jc_session_load_by_id had never
     * populated, and the suite died on `free(): invalid pointer` / SIGSEGV.
     * Deterministic, and reachable without root: pre-create this test's own
     * sessions directory read-only and it reproduces every time (exit 139).
     *
     * Worse than the crash: abort() discards block-buffered stdout, so the four
     * FAIL lines naming the cause VANISH and the reader gets a bare "Aborted".
     * `stdbuf -o0` was needed to recover them while diagnosing this.
     *
     * JC_REQUIRE is the harness verb for exactly this, and its own comment
     * records an audit that found 19 such sites across 8 files (M452). This was
     * the twentieth, and the audit did not see it -- which is the argument for
     * the greppable idiom over remembering to write the `if`.
     *
     * Both are recorded before branching, so a reader still sees whether the
     * save or the load was the failure. */
    ok = JC_REQUIRE(jc_session_save(&s) == JC_OK);
    ok = JC_REQUIRE(jc_session_load_by_id(s.id, &back, a) == JC_OK) && ok;
    if (!ok) {
        setenv("HOME", jc_test_tmp("jichi_home_test"), 1);
        jc_session_free(&back);
        jc_session_free(&s);
        jc_arena_free(a);
        return;
    }

    JC_CHECK(jc_history_len(&back.history) == 5);
    if (jc_history_len(&back.history) == 5) {
        struct jc_message *u = jc_history_get(&back.history, 0);
        struct jc_message *a1 = jc_history_get(&back.history, 1);
        struct jc_message *t1 = jc_history_get(&back.history, 2);
        struct jc_message *t2 = jc_history_get(&back.history, 3);
        struct jc_message *a2 = jc_history_get(&back.history, 4);

        JC_CHECK(u->role == JC_ROLE_USER &&
                 strcmp(u->content, "the task") == 0);

        JC_CHECK(a1->role == JC_ROLE_ASSISTANT);
        JC_CHECK(a1->truncated == 1);       /* the field M367 fixed        */
        JC_CHECK(jc_msg_tool_call_count(a1) == 2);
        if (jc_msg_tool_call_count(a1) == 2) {
            struct jc_tool_call *tc = jc_msg_tool_call_at(a1, 0);
            JC_CHECK(strcmp(tc->id, "c1") == 0);
            JC_CHECK(strcmp(tc->name, "read_file") == 0);
            JC_CHECK(strcmp(tc->arguments_json, "{\"path\":\"a b\"}") == 0);
            tc = jc_msg_tool_call_at(a1, 1);
            JC_CHECK(strcmp(tc->id, "c2") == 0);
            JC_CHECK(strcmp(tc->arguments_json,
                            "{\"cmd\":\"echo \\\"x\\\"\"}") == 0);
        }

        JC_CHECK(t1->role == JC_ROLE_TOOL && t1->is_error == 0 &&
                 strcmp(t1->tool_call_id, "c1") == 0 &&
                 strcmp(t1->content, "bytes here") == 0);
        JC_CHECK(t2->role == JC_ROLE_TOOL && t2->is_error == 1 &&
                 strcmp(t2->tool_call_id, "c2") == 0);

        JC_CHECK(a2->role == JC_ROLE_ASSISTANT && a2->truncated == 0);
    }
    JC_CHECK(back.mode == JC_MODE_AUTO);

    /* M606: the task list round-trips with its CONTENT and its STATE. Before
     * M606 nothing here existed: the list lived on jc_app, the codec never saw
     * it, and a resumed conversation read "(todo list is empty)" beside a
     * history that said otherwise. */
    JC_CHECK(back.todos.items.len == 3);
    if (back.todos.items.len == 3) {
        struct jc_todo_item *i0 = (struct jc_todo_item *)jc_vec_at(&back.todos.items, 0);
        struct jc_todo_item *i1 = (struct jc_todo_item *)jc_vec_at(&back.todos.items, 1);
        struct jc_todo_item *i2 = (struct jc_todo_item *)jc_vec_at(&back.todos.items, 2);
        JC_CHECK_STR(i0->content, "write the codec");
        JC_CHECK(i0->status == JC_TODO_IN_PROGRESS);
        JC_CHECK_STR(i1->content, "prove it red");
        JC_CHECK(i1->status == JC_TODO_PENDING);
        JC_CHECK_STR(i2->content, "survey \"quotes\"");
        JC_CHECK(i2->status == JC_TODO_DONE);
    }
    /* memory equals disk after a load: no save is due... */
    JC_CHECK(!jc_session_needs_save(&back));

    /* And the reloaded history satisfies the M364 wire contract. */
    JC_CHECK(jc_history_check(&back.history, NULL) == 0);

    /* The FORK copy loop is a third codec site, and it had the same hole as
     * save/load (every field copied except truncated) -- caught by reading
     * the reverted file during this milestone's own teeth run. */
    {
        struct jc_session fk;
        JC_CHECK(jc_session_fork(&back, &fk, a) == JC_OK);
        JC_CHECK(jc_history_len(&fk.history) == 5);
        if (jc_history_len(&fk.history) == 5) {
            JC_CHECK(jc_history_get(&fk.history, 1)->truncated == 1);
            JC_CHECK(jc_history_get(&fk.history, 4)->truncated == 0);
            JC_CHECK(jc_history_get(&fk.history, 3)->is_error == 1);
        }
        /* M606: the fork carries the task list, as its OWN copy -- clearing
         * the original leaves the fork's three items, and marks the original
         * as needing a save (a replaced list is a changed session). */
        JC_CHECK(fk.todos.items.len == 3);
        jc_todo_clear(&back.todos);
        JC_CHECK(back.todos.items.len == 0);
        JC_CHECK(jc_session_needs_save(&back));
        JC_CHECK(fk.todos.items.len == 3);
        jc_session_free(&fk);
    }

    JC_CHECK(jc_session_delete(s.id) == JC_OK);
    setenv("HOME", jc_test_tmp("jichi_home_test"), 1); /* the suite's shared store */
    jc_session_free(&back);
    jc_session_free(&s);
    jc_arena_free(a);
}
