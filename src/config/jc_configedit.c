/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_configedit.c - persist config changes (see jc_configedit.h). */

#include "jc_configedit.h"
#include "jc_config_trust.h"   /* M851 */
#include "jc_json.h"
#include "jc_jsonc.h"
#include "jc_path.h"
#include "jc_sha256.h"
#include "jc_str.h"
#include "jc_platform.h"
#include "jc_snprintf.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h> /* free */
#include <sys/stat.h> /* lstat (M1107, D-25) */

/* ---- pure setters (replace-or-add) --------------------------------------- */

void jc_configedit_set_bool(cJSON *root, const char *key, int val)
{
    if (root == NULL || key == NULL) return;
    if (cJSON_GetObjectItem(root, key) != NULL) {
        cJSON_ReplaceItemInObject(root, key, cJSON_CreateBool(val));
    } else {
        cJSON_AddBoolToObject(root, key, val);
    }
}

void jc_configedit_set_str(cJSON *root, const char *key, const char *val)
{
    if (root == NULL || key == NULL || val == NULL) return;
    if (cJSON_GetObjectItem(root, key) != NULL) {
        cJSON_ReplaceItemInObject(root, key, cJSON_CreateString(val));
    } else {
        cJSON_AddStringToObject(root, key, val);
    }
}

void jc_configedit_set_num(cJSON *root, const char *key, double val)
{
    if (root == NULL || key == NULL) return;
    if (cJSON_GetObjectItem(root, key) != NULL) {
        cJSON_ReplaceItemInObject(root, key, cJSON_CreateNumber(val));
    } else {
        cJSON_AddNumberToObject(root, key, val);
    }
}

/* ---- I/O ----------------------------------------------------------------- */

void jc_config_edit_target(const char *explicit_path, char *out, jc_size cap)
{
    if (out == NULL || cap == 0) return;
    if (explicit_path != NULL && explicit_path[0] != '\0') {
        jc_snprintf(out, cap, "%s", explicit_path);
        return;
    }
    /* M533: write to the file the READER will read, in the reader's own order.
     *
     * The loader takes `local/config.json` if it exists and `.jichi/config.json`
     * OTHERWISE -- exclusively, one or the other (jc_config.c). This function
     * used to name `local/config.json` unconditionally, so on a project whose
     * config lives in `.jichi/config.json` a single `config set` created a new
     * `local/config.json` holding only that one key, which then WON the reader's
     * exclusive choice and orphaned everything else.
     *
     * Measured on a project declaring a model, `configEditable`, `pathFence`,
     * `privilegedCommands: deny` and `permissions.deny`:
     *
     *     before:  active: projmodel (proj/x)   path fence on (all modes)
     *     $ jichi config set snapshots false
     *     after:   active: strong (global)      path fence auto
     *
     * One unrelated key edit and the project's model and all three fences
     * stopped applying, while the file still sat on disk unread. The message
     * ("set snapshots = false in local/config.json") reads as additive; it was
     * destructive. It also self-locked: `configEditable` was among the orphaned
     * keys, so the next `config set` was refused -- a symptom pointing away from
     * its cause.
     *
     * A new project still gets `local/config.json`, which is what the tutorial
     * and the setup wizard create. */
    if (!jc_file_exists("local/config.json") &&
        jc_file_exists(".jichi/config.json")) {
        jc_snprintf(out, cap, "%s", ".jichi/config.json");
        return;
    }
    jc_snprintf(out, cap, "%s", "local/config.json");
}

/* Load the JSON of `path` into a cJSON object.
 *
 * M924 (R07-F1): the READER strips // and block comments and trailing commas
 * before parsing (jc_jsonc_strip, M459 -- opencode configs are JSONC), so a
 * file the reader accepts USED TO FAIL HERE: a strict cJSON_Parse, whose
 * failure was silently mapped to a fresh empty object. `config set` would then
 * save that empty object plus the one key being set, wiping the model, fences
 * and permissions the file held -- while the file stayed trusted in its wiped
 * form (the reviewer measured it: R07-F1). The writer now runs the SAME strip
 * the reader does, so reader and writer cannot disagree about what the
 * operator wrote.
 *
 * The result is ALWAYS a JSON object: absent/empty/unparseable files and
 * non-object roots all yield a fresh empty object. Callers that must REFUSE to
 * edit a corrupt file check jc_configedit_path_is_object() first -- load
 * itself is allocation-only and never reports a parse failure, which is what
 * made the old silent-wipe possible.
 *
 * NULL only on allocation failure. Caller cJSON_Delete()s the result. */
cJSON *jc_config_edit_load(const char *path)
{
    char *text = NULL;
    jc_size len = 0;
    cJSON *root = NULL;
    struct jc_arena *a;

    if (path != NULL) {
        a = jc_arena_new(0);
        if (a != NULL) {
            if (jc_read_file(path, &text, &len, a) == JC_OK && len > 0 &&
                text != NULL) {
                char *clean = jc_jsonc_strip(text, a);
                if (clean != NULL) {
                    root = jc_json_parse(clean);
                } else {
                    root = jc_json_parse(text);
                }
            }
            jc_arena_free(a);
        }
    }
    if (root == NULL || !cJSON_IsObject(root)) {
        if (root != NULL) cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    return root;
}

/* M924 (R07-F1): does `path`, if present and non-empty, parse (via the same
 * comment-strip the reader uses) to a JSON OBJECT? The caller refuses to edit
 * a file that does not, because saving would overwrite it -- and the one case
 * where an empty-file fallback is a start-from-scratch (absent/whitespace-only)
 * is exactly the case this answers yes to. */
int jc_configedit_path_is_object(const char *path)
{
    char *text = NULL;
    jc_size len = 0;
    struct jc_arena *a;
    cJSON *root;
    jc_status st;
    int is_obj;
    int any_content = 0;

    if (path == NULL) {
        return 1;
    }
    a = jc_arena_new(0);
    if (a == NULL) {
        return 1; /* cannot check -> do not block the edit on OOM here */
    }
    st = jc_read_file(path, &text, &len, a);
    if (st == JC_ERR_IO) {
        /* M198: a directory (or a file whose read failed) is not
         * start-from-scratch -- it exists, and an edit over it would save an
         * empty object plus the one key, overwriting its content. Refuse. */
        jc_arena_free(a);
        return 0;
    }
    if (st != JC_OK || text == NULL || len == 0) {
        jc_arena_free(a);
        return 1; /* absent or empty: start-from-scratch is fine */
    }
    {
        char *clean = jc_jsonc_strip(text, a);
        const char *use = clean != NULL ? clean : text;
        jc_size p;
        /* A whitespace-only file holds no content: an edit over it is a
         * start-from-scratch, not an overwrite, so it passes. */
        for (p = 0; use[p] != '\0'; p++) {
            if (use[p] != ' ' && use[p] != '\t' &&
                use[p] != '\n' && use[p] != '\r') {
                any_content = 1;
                break;
            }
        }
        root = jc_json_parse(use);
    }
    if (!any_content) {
        if (root != NULL) cJSON_Delete(root);
        jc_arena_free(a);
        return 1;
    }
    is_obj = (root != NULL && cJSON_IsObject(root));
    if (root != NULL) {
        cJSON_Delete(root);
    }
    jc_arena_free(a);
    return is_obj;
}

int jc_configedit_path_has_comment(const char *path)
{
    char *text = NULL;
    jc_size len = 0;
    struct jc_arena *a;
    char *clean;
    int has;

    if (path == NULL) {
        return 0;
    }
    a = jc_arena_new(0);
    if (a == NULL) {
        return 0;
    }
    if (jc_read_file(path, &text, &len, a) != JC_OK || text == NULL || len == 0) {
        jc_arena_free(a);
        return 0;
    }
    clean = jc_jsonc_strip(text, a);
    /* The strip is idempotent on clean input: content differs from its
     * stripped form iff it carried // or block comments or a trailing comma. */
    has = (clean != NULL && clean != text &&
           strcmp(clean, text) != 0);
    jc_arena_free(a);
    return has;
}

/* Copy the parent-directory portion of `path` into `dir` (empty if none). */
static void parent_dir(const char *path, char *dir, jc_size cap)
{
    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        dir[0] = '\0';
        return;
    }
    {
        jc_size n = (jc_size)(slash - path);
        if (n >= cap) n = cap - 1;
        memcpy(dir, path, n);
        dir[n] = '\0';
    }
}

/* M1107 (D-25): make the config write safe. The order is the fix:
 *
 *   refuse a symlink -> back up by COPY -> temp-first mode-preserving write.
 *
 * (a) A SYMLINKED config is refused up front (lstat, S_ISLNK): the reader's
 *     name is never replaced, and nothing is ever written through a link to
 *     its target (D-23's rule for this verb). D-25 measured the old sequence
 *     following the link and leaving the target edited while the reader's
 *     name had become a plain regular file.
 *
 * (b) The backup is a COPY of the pre-existing content to <path>.bak, made
 *     BEFORE the primary write. The old code RENAMED the live file to .bak
 *     first: on a subsequent write failure the path then held NO config at
 *     all ("No model is configured", the measured expB), and the rename
 *     carried the mode to the backup, leaving the successor to the umask
 *     (a 0600 config came back 0644). M1107's COPY wrote the .bak with
 *     jc_write_file (fopen "wb") + chmod: that OPEN FOLLOWS the name
 *     <path>.bak, so a planted SYMLINK/HARDLINK at the name was written
 *     through -- the canary it named was clobbered and chmod'd 0600 -- and a
 *     FIFO at the name BLOCKED the save in open (SV:1725 #1, a REGRESSION
 *     the OLD
 *     rename-based backup did not have). The .bak is now written by
 *     jc_write_file_atomic_mode -- the same writer as (c): a unique
 *     <path>.bak.tmp<pid> temp (O_EXCL), fchmod'd to the pre-existing
 *     file's bits BEFORE the rename. A rename REPLACES the name <path>.bak;
 *     it never OPENS it, so a planted symlink/hardlink/FIFO at the .bak is
 *     replaced, not followed or opened, and the copy carries the original's
 *     mode atomically (the separate chmod is dead -- fchmod is on the temp).
 *     M1107-c's remove-partial-.bak cleanup is dead too: the atomic writer's
 *     failure paths leave neither a .bak nor a temp (under the monotone
 *     JICHI_FAULT_WRITE_AFTER=0 knob it returns before opening any temp).
 *     The true invariant is unchanged: the ORIGINAL is untouched.
 *
 * (c) The primary write is jc_write_file_atomic_mode: unique <path>.tmp<pid>
 *     temp (O_EXCL), fchmod to the pre-existing file's mode BEFORE the
 *     rename, and the temp removed on every failure. The old fixed-name
 *     <path>.tmp write followed a planted link and failed (config gone)
 *     when a directory occupied the name; the unique suffix removes both.
 *
 * A failed save therefore always leaves the original in place, byte- and
 * mode-identical; the successor and its .bak both carry the original's mode.
 * The write status is returned as a VALUE for the caller
 * (jc_configedit_apply composes the message), never control flow. */
jc_status jc_config_edit_save(const char *path, cJSON *root)
{
    char dir[1024];
    char bak[1100];
    char *text;
    struct stat st;
    struct jc_arena *a;
    char *buf = NULL;
    jc_size buflen = 0;
    int mode = 0644;
    int preexists = 0;
    jc_status stx;

    if (path == NULL || root == NULL) return JC_ERR_INVALID;

    /* (a) refuse a symlink: live or broken, lstat sees the LINK, and the
     * edit must not touch it or its target. */
    if (lstat(path, &st) == 0 && S_ISLNK(st.st_mode)) {
        return JC_ERR_IO;
    }
    /* (b-prep) the pre-existing REGULAR file's bits, so the replacement
     * carries them; absent -> 0644. */
    if (lstat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        preexists = 1;
        mode = (int)(st.st_mode & 0777);
    }

    parent_dir(path, dir, sizeof dir);
    if (dir[0] != '\0') jc_mkdir_p(dir);

    /* (b) back up by COPY, before the primary write, so a failed save
     * leaves the original untouched. The copy is written by
     * jc_write_file_atomic_mode -- the same unique-temp + fchmod-before-
     * rename writer as the primary (c) -- so it carries the pre-existing
     * file's bits atomically (a 0600 config's .bak cannot come back 0644,
     * which would WIDEN a file that may hold an apiKey) and, crucially, a
     * rename REPLACES the name <path>.bak without OPENING it: a planted
     * symlink/hardlink/FIFO at the .bak is replaced, not followed or
     * opened (SV:1725 #1 -- M1107's jc_write_file+chmod followed the name
     * and blocked on a FIFO). NOT fatal: the true invariant on a failed
     * backup is that the ORIGINAL is untouched. The atomic writer's
     * failure paths leave neither a .bak nor a temp (under the monotone
     * JICHI_FAULT_WRITE_AFTER=0 knob it returns before opening any temp),
     * so M1107-c's remove-partial-.bak cleanup is dead -- a faulted
     * backup leaves NO .bak, the accepted post-fault state.
     */
    if (preexists) {
        a = jc_arena_new(0);
        if (a != NULL) {
            if (jc_read_file(path, &buf, &buflen, a) != JC_OK || buf == NULL) {
                fprintf(stderr,
                        "! could not back up %s to <path>.bak: the edit "
                        "proceeds without the backup\n", path);
            } else {
                jc_snprintf(bak, sizeof bak, "%s.bak", path);
                if (jc_write_file_atomic_mode(bak, buf, buflen, mode)
                        != JC_OK) {
                    /* The atomic writer left neither a .bak nor a temp on
                     * every failure path (and never opens <path>.bak), so
                     * there is no partial to remove and the ORIGINAL is
                     * untouched. */
                    fprintf(stderr,
                            "! could not back up %s to <path>.bak: no backup "
                            "was made; the edit proceeds\n", path);
                }
            }
            jc_arena_free(a);
        } else {
            fprintf(stderr,
                    "! could not back up %s to <path>.bak: the edit "
                    "proceeds without the backup\n", path);
        }
    }

    text = cJSON_Print(root);
    if (text == NULL) return JC_ERR_OOM;

    /* (c) temp-first, mode-preserving, unique-suffix write. */
    stx = jc_write_file_atomic_mode(path, text, (jc_size)strlen(text), mode);
    free(text);
    return stx;
}

/* M1107 (D-25): the save's failure, worded by what it saw. A symlinked
 * target is refused by the save itself, so the message must name the link
 * (the driver greps for it) and say the name and its target were left
 * untouched; any other failure is the plain write error. */
static void save_failure_msg(const char *target, char *msg, jc_size cap)
{
    struct stat st;
    if (lstat(target, &st) == 0 && S_ISLNK(st.st_mode)) {
        jc_snprintf(msg, cap,
            "refusing to edit %s: it is a symlink. config set refuses to "
            "follow a link, so the name and its target are left untouched.",
            target);
        return;
    }
    jc_snprintf(msg, cap, "failed to write %s", target);
}

/* A boolean value on the `config set` command line; numeric -> *num (is_num).
 *
 * M534: the SHARED dialect (jc_str.h), not a fourth private word list. This
 * function was case-sensitive and accepted a spelling -- `on`/`off` -- that no
 * READER accepted, so a human copying the spelling jichi's own command blesses
 * into a config file by hand got `"pathFence": "on"`, which read as false and
 * turned the fence off. One dialect means the command and the loader cannot
 * disagree about what the operator wrote. */
static int value_is_bool(const char *v, int *b)
{
    return jc_bool_from_word(v, b);
}

static int value_is_num(const char *v, double *num)
{
    const char *p = v;
    int digits = 0;
    if (*p == '-' || *p == '+') p++;
    while (*p != '\0') {
        if ((*p >= '0' && *p <= '9')) { digits = 1; p++; }
        else if (*p == '.') p++;
        else return 0;
    }
    if (!digits) return 0;
    *num = strtod(v, NULL);
    return 1;
}

/* M851 (CFG-1): trust is bound to a project config's CONTENT, so an edit
 * makes it untrusted again -- that is what stops someone else's change from
 * inheriting the user's trust. But an edit the user makes through jichi's own
 * `config set` (or the TUI's /config set) is theirs: when the file was trusted
 * immediately before it, the new content is trusted too. An untrusted file
 * stays untrusted; the user's command does not vouch for keys it did not
 * write. */
static int file_trust_state(const char *path, char *abs, jc_size abscap)
{
    struct jc_arena *a;
    char *raw = NULL;
    jc_size len = 0;
    char sha[JC_SHA256_HEX_LEN + 1];
    int trusted = 0;

    if (jc_path_resolve(path, abs, abscap) != JC_OK) {
        return 0;
    }
    a = jc_arena_new(0);
    if (a == NULL) {
        return 0;
    }
    if (jc_read_file(path, &raw, &len, a) == JC_OK && raw != NULL) {
        jc_sha256_hex(raw, len, sha);
        trusted = jc_cfgtrust_is_trusted(abs, sha);
    }
    jc_arena_free(a);
    return trusted;
}

static void retrust_after_edit(const char *path, const char *abs)
{
    struct jc_arena *a = jc_arena_new(0);
    char *raw = NULL;
    jc_size len = 0;
    char sha[JC_SHA256_HEX_LEN + 1];

    if (a == NULL) {
        return;
    }
    if (jc_read_file(path, &raw, &len, a) == JC_OK && raw != NULL) {
        jc_sha256_hex(raw, len, sha);
        (void)jc_cfgtrust_add(abs, sha);
    }
    jc_arena_free(a);
}

jc_status jc_configedit_apply(const char *explicit_path, const char *key,
                              const char *value, char *msg, jc_size cap)
{
    char target[1100];
    char target_abs[1100];
    int was_trusted;
    cJSON *root;
    jc_status st;
    int b;
    double num;
    const char *typ;
    int had_comment = 0;

    if (key == NULL || value == NULL) return JC_ERR_INVALID;
    if (strcmp(key, "apiKey") == 0) {
        jc_snprintf(msg, cap, "refusing to set a literal apiKey; use apiKeyEnv "
                    "(the name of an environment variable) instead");
        return JC_ERR_DENIED;
    }

    jc_config_edit_target(explicit_path, target, sizeof target);
    was_trusted = file_trust_state(target, target_abs, sizeof target_abs);

    /* M924 (R07-F1): refuse to edit a target whose content does not parse to a
     * JSON object (after the same comment-strip the reader uses). jc_config_
     * edit_load maps such a file to a fresh EMPTY object -- saving that would
     * overwrite the file's whole content with the single key being set. An
     * absent or empty file is exactly the start-from-scratch case and passes.
     * A non-object root (an array) is refused the same way: it would be
     * replaced, not edited. */
    if (!jc_configedit_path_is_object(target)) {
        jc_snprintf(msg, cap,
            "refusing to edit %s: it is not empty and does not parse to a JSON "
            "object (after stripping // and block comments and trailing "
            "commas). Saving would overwrite its content with just this one "
            "key. Fix the file (its .bak, if any, or the version control copy) "
            "and retry.", target);
        return JC_ERR_PARSE;
    }
    root = jc_config_edit_load(target);
    if (root == NULL) return JC_ERR_OOM;
    /* M924: captured BEFORE the save, which replaces the file -- so the
     * success message can say the edit re-serialised a commented file. */
    had_comment = jc_configedit_path_has_comment(target);

    /* `logging` is a nested object {level,path}, not a scalar -- set its `level`. */
    if (strcmp(key, "logging") == 0) {
        cJSON *lg = cJSON_GetObjectItem(root, "logging");
        if (lg == NULL || !cJSON_IsObject(lg)) {
            lg = cJSON_CreateObject();
            if (cJSON_GetObjectItem(root, "logging") != NULL) {
                cJSON_ReplaceItemInObject(root, "logging", lg);
            } else {
                cJSON_AddItemToObject(root, "logging", lg);
            }
        }
        jc_configedit_set_str(lg, "level", value);
        st = jc_config_edit_save(target, root);
        cJSON_Delete(root);
        if (st != JC_OK) {
            save_failure_msg(target, msg, cap);   /* M1107 (D-25) */
            return st;
        }
        if (was_trusted) {
            retrust_after_edit(target, target_abs);   /* M851 */
        }
        jc_snprintf(msg, cap,
            "set logging.level = %s in %s -- restart or /resume to apply%s",
            value, target,
            had_comment
                ? " [note: the file carried // or block comments or trailing "
                  "commas; it was re-serialised without them -- the original "
                  "is in <path>.bak]"
                : "");
        return JC_OK;
    }

    if (value_is_bool(value, &b)) {
        jc_configedit_set_bool(root, key, b);
        typ = "bool";
    } else if (value_is_num(value, &num)) {
        jc_configedit_set_num(root, key, num);
        typ = "number";
    } else {
        jc_configedit_set_str(root, key, value);
        typ = "string";
    }

    st = jc_config_edit_save(target, root);
    cJSON_Delete(root);
    if (st != JC_OK) {
        save_failure_msg(target, msg, cap);   /* M1107 (D-25) */
        return st;
    }
    if (was_trusted) {
        retrust_after_edit(target, target_abs);   /* M851 */
    }
    jc_snprintf(msg, cap,
        "set %s = %s (%s) in %s -- restart or /resume to apply%s",
        key, value, typ, target,
        had_comment
            ? " [note: the file carried // or block comments or trailing "
              "commas; it was re-serialised without them -- the original is "
              "in <path>.bak]"
            : "");
    return JC_OK;
}
