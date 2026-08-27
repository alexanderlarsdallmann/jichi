/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_configedit.c - persist config changes (see jc_configedit.h). */

#include "jc_configedit.h"
#include "jc_str.h"
#include "jc_platform.h"
#include "jc_snprintf.h"

#include <string.h>
#include <stdio.h>  /* rename */
#include <stdlib.h> /* free */

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
                root = cJSON_Parse(text);
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

jc_status jc_config_edit_save(const char *path, cJSON *root)
{
    char dir[1024];
    char tmp[1100];
    char bak[1100];
    char *text;
    jc_status st;

    if (path == NULL || root == NULL) return JC_ERR_INVALID;

    parent_dir(path, dir, sizeof dir);
    if (dir[0] != '\0') jc_mkdir_p(dir);

    text = cJSON_Print(root);
    if (text == NULL) return JC_ERR_OOM;

    /* Back up any existing content, then write tmp + atomic rename. */
    if (jc_file_exists(path)) {
        jc_snprintf(bak, sizeof bak, "%s.bak", path);
        rename(path, bak); /* best-effort */
    }
    jc_snprintf(tmp, sizeof tmp, "%s.tmp", path);
    st = jc_write_file(tmp, text, (jc_size)strlen(text));
    free(text);
    if (st != JC_OK) return st;
    if (rename(tmp, path) != 0) {
        return JC_ERR_IO;
    }
    return JC_OK;
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

jc_status jc_configedit_apply(const char *explicit_path, const char *key,
                              const char *value, char *msg, jc_size cap)
{
    char target[1100];
    cJSON *root;
    jc_status st;
    int b;
    double num;
    const char *typ;

    if (key == NULL || value == NULL) return JC_ERR_INVALID;
    if (strcmp(key, "apiKey") == 0) {
        jc_snprintf(msg, cap, "refusing to set a literal apiKey; use apiKeyEnv "
                    "(the name of an environment variable) instead");
        return JC_ERR_DENIED;
    }

    jc_config_edit_target(explicit_path, target, sizeof target);
    root = jc_config_edit_load(target);
    if (root == NULL) return JC_ERR_OOM;

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
            jc_snprintf(msg, cap, "failed to write %s", target);
            return st;
        }
        jc_snprintf(msg, cap,
            "set logging.level = %s in %s -- restart or /resume to apply",
            value, target);
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
        jc_snprintf(msg, cap, "failed to write %s", target);
        return st;
    }
    jc_snprintf(msg, cap, "set %s = %s (%s) in %s -- restart or /resume to apply",
                key, value, typ, target);
    return JC_OK;
}
