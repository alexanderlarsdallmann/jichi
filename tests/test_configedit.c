/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_configedit.c - the M111 config-write foundation (pure parts). */

#include "jc_test.h"
#include "jc_configedit.h"
#include "jc_json.h"
#include "jc_snprintf.h"
#include "jc_platform.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h> /* remove */
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h> /* symlink, rmdir */

static void test_set_replace_or_add(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *it;

    /* add when absent */
    jc_configedit_set_bool(root, "snapshots", 1);
    it = cJSON_GetObjectItem(root, "snapshots");
    JC_CHECK(it != NULL && cJSON_IsTrue(it));

    /* replace (NOT duplicate) when present */
    jc_configedit_set_bool(root, "snapshots", 0);
    it = cJSON_GetObjectItem(root, "snapshots");
    JC_CHECK(it != NULL && cJSON_IsFalse(it));
    /* only one such key exists */
    {
        int count = 0;
        cJSON *c;
        for (c = root->child; c != NULL; c = c->next) {
            if (c->string != NULL && strcmp(c->string, "snapshots") == 0)
                count++;
        }
        JC_CHECK(count == 1);
    }

    jc_configedit_set_str(root, "logging", "off");
    jc_configedit_set_str(root, "logging", "metrics");
    it = cJSON_GetObjectItem(root, "logging");
    JC_CHECK(it != NULL && it->valuestring != NULL &&
             strcmp(it->valuestring, "metrics") == 0);

    jc_configedit_set_num(root, "contextLimit", 4000);
    jc_configedit_set_num(root, "contextLimit", 8000);
    it = cJSON_GetObjectItem(root, "contextLimit");
    JC_CHECK(it != NULL && (int)it->valuedouble == 8000);

    /* NULLs are safe no-ops */
    jc_configedit_set_bool(NULL, "x", 1);
    jc_configedit_set_str(root, NULL, "x");

    cJSON_Delete(root);
}

static void test_target(void)
{
    char out[256];
    jc_config_edit_target(jc_test_tmp("my.json"), out, sizeof out);
    JC_CHECK(strcmp(out, jc_test_tmp("my.json")) == 0);
    jc_config_edit_target(NULL, out, sizeof out);
    JC_CHECK(strcmp(out, "local/config.json") == 0);
    jc_config_edit_target("", out, sizeof out);
    JC_CHECK(strcmp(out, "local/config.json") == 0);
}

static void test_load_save_roundtrip(void)
{
    char path[256];
    cJSON *root, *loaded;
    cJSON *it;

    jc_snprintf(path, sizeof path, "%s/jichi_cfgedit_%d.json", jc_test_tmpdir(), (int)1234);

    /* load of a missing file yields a fresh empty object */
    root = jc_config_edit_load(path);
    JC_CHECK(root != NULL && cJSON_IsObject(root));

    jc_configedit_set_bool(root, "snapshots", 0);
    jc_configedit_set_str(root, "logging", "full");
    JC_CHECK(jc_config_edit_save(path, root) == JC_OK);
    cJSON_Delete(root);

    /* reload sees the written values */
    loaded = jc_config_edit_load(path);
    it = cJSON_GetObjectItem(loaded, "logging");
    JC_CHECK(it != NULL && it->valuestring != NULL &&
             strcmp(it->valuestring, "full") == 0);
    it = cJSON_GetObjectItem(loaded, "snapshots");
    JC_CHECK(it != NULL && cJSON_IsFalse(it));
    cJSON_Delete(loaded);

    /* an unknown key in an existing file is PRESERVED across an edit */
    remove(path);
    {
        const char *raw = "{\"customKey\":42,\"logging\":\"off\"}";
        jc_write_file(path, raw, (jc_size)strlen(raw));
    }
    root = jc_config_edit_load(path);
    jc_configedit_set_str(root, "logging", "metrics");
    JC_CHECK(jc_config_edit_save(path, root) == JC_OK);
    cJSON_Delete(root);
    loaded = jc_config_edit_load(path);
    it = cJSON_GetObjectItem(loaded, "customKey");
    JC_CHECK(it != NULL && (int)it->valuedouble == 42); /* preserved */
    cJSON_Delete(loaded);

    remove(path);
    {
        char bak[300];
        jc_snprintf(bak, sizeof bak, "%s.bak", path);
        remove(bak);
    }
}

static void test_apply(void)
{
    char path[256];
    char msg[512];
    cJSON *loaded, *lg, *it;

    jc_snprintf(path, sizeof path, "%s/jichi_cfgapply_%d.json", jc_test_tmpdir(), (int)5678);
    remove(path);

    /* logging is a nested object: telemetry level lands at logging.level */
    JC_CHECK(jc_configedit_apply(path, "logging", "full", msg, sizeof msg)
             == JC_OK);
    /* a scalar bool via type inference */
    JC_CHECK(jc_configedit_apply(path, "snapshots", "false", msg, sizeof msg)
             == JC_OK);
    /* a literal apiKey is refused */
    JC_CHECK(jc_configedit_apply(path, "apiKey", "sk-secret", msg, sizeof msg)
             == JC_ERR_DENIED);

    loaded = jc_config_edit_load(path);
    lg = cJSON_GetObjectItem(loaded, "logging");
    JC_CHECK(lg != NULL && cJSON_IsObject(lg));
    it = cJSON_GetObjectItem(lg, "level");
    JC_CHECK(it != NULL && it->valuestring != NULL &&
             strcmp(it->valuestring, "full") == 0);
    it = cJSON_GetObjectItem(loaded, "snapshots");
    JC_CHECK(it != NULL && cJSON_IsFalse(it));
    JC_CHECK(cJSON_GetObjectItem(loaded, "apiKey") == NULL); /* never written */
    cJSON_Delete(loaded);

    remove(path);
    { char bak[300]; jc_snprintf(bak, sizeof bak, "%s.bak", path); remove(bak); }
}

/* M924 (R07-F1): the writer must read what the reader reads. A config with
 * // comments or a trailing comma parses for the reader (jc_jsonc_strip,
 * M459); the writer used to strict-JSON-parse it, fail, fall back to an EMPTY
 * object, and then overwrite the file with just the one key being set -- the
 * measured R07-F1 wipe (model, pathFence, permissions.deny gone; file stayed
 * trusted in its wiped form). */
static void test_apply_commented_config(void)
{
    char path[300];
    char bak[400];
    char msg[512];
    const char *raw =
        "{\n"
        "  // the operator's note\n"
        "  \"model\": \"jlu/strong-model\",\n"
        "  \"pathFence\": 1,\n"
        "  \"permissions\": { \"deny\": [\"run_terminal_command\"], },\n"
        "  \"logging\": \"off\"\n"
        "}\n";

    jc_snprintf(path, sizeof path, "%s/jichi_cfgcomment_%d.json",
                jc_test_tmpdir(), (int)7123);
    jc_snprintf(bak, sizeof bak, "%s.bak", path);
    remove(path);
    remove(bak);
    JC_CHECK(jc_write_file(path, raw, (jc_size)strlen(raw)) == JC_OK);

    /* the guard: a commented object IS a parseable object */
    JC_CHECK(jc_configedit_path_is_object(path) == 1);
    /* and it IS detected as commented (drives the re-serialise note) */
    JC_CHECK(jc_configedit_path_has_comment(path) == 1);

    /* an edit succeeds ... */
    JC_CHECK(jc_configedit_apply(path, "logging", "full", msg, sizeof msg)
             == JC_OK);

    /* ... and every other key survives. */
    {
        cJSON *loaded = jc_config_edit_load(path);
        const cJSON *m = cJSON_GetObjectItem(loaded, "model");
        const cJSON *p = cJSON_GetObjectItem(loaded, "pathFence");
        const cJSON *perm = cJSON_GetObjectItem(loaded, "permissions");
        const cJSON *lg = cJSON_GetObjectItem(loaded, "logging");
        JC_CHECK(m != NULL && m->valuestring != NULL &&
                 strcmp(m->valuestring, "jlu/strong-model") == 0);
        JC_CHECK(p != NULL && p->valueint == 1);
        JC_CHECK(perm != NULL && cJSON_IsObject(perm) &&
                 cJSON_GetArraySize(cJSON_GetObjectItem(perm, "deny")) == 1);
        JC_CHECK(lg != NULL && cJSON_IsObject(lg) &&
                 cJSON_IsString(cJSON_GetObjectItem(lg, "level")) &&
                 strcmp(cJSON_GetObjectItem(lg, "level")->valuestring,
                        "full") == 0);
        cJSON_Delete(loaded);
    }

    /* The ORIGINAL content (with its comments) is kept in the .bak backup. */
    {
        FILE *f = fopen(bak, "rb");
        int c;
        JC_CHECK(f != NULL);
        if (f != NULL) {
            c = fgetc(f);
            JC_CHECK(c == '{');
            JC_CHECK(fclose(f) == 0);
        }
    }
    remove(path);
    remove(bak);
}

/* M924 (R07-F1): a non-empty file that does NOT parse to a JSON object
 * (after the comment strip) must be REFUSED, not silently overwritten. The
 * file must be byte-unchanged. */
static void test_apply_refuses_unparseable(void)
{
    char path[300];
    char bak[400];
    char msg[512];
    const char *trunc = "{\"model\": \"jlu/strong\", \"pathFence\": 1";
    char before[512];
    char after[512];
    jc_size blen = 0, alen = 0;

    jc_snprintf(path, sizeof path, "%s/jichi_cfgcorrupt_%d.json",
                jc_test_tmpdir(), (int)9456);
    jc_snprintf(bak, sizeof bak, "%s.bak", path);
    remove(path);
    remove(bak);
    JC_CHECK(jc_write_file(path, trunc, (jc_size)strlen(trunc)) == JC_OK);

    /* the guard answers: non-empty, not an object */
    JC_CHECK(jc_configedit_path_is_object(path) == 0);

    /* an array root is refused too: it would be replaced, not edited */
    {
        char apath[300];
        jc_snprintf(apath, sizeof apath, "%s/jichi_cfgarray_%d.json",
                    jc_test_tmpdir(), (int)9457);
        remove(apath);
        JC_CHECK(jc_write_file(apath, "[1,2,3]", 7) == JC_OK);
        JC_CHECK(jc_configedit_path_is_object(apath) == 0);
        JC_CHECK(jc_configedit_apply(apath, "snapshots", "false",
                                     msg, sizeof msg) == JC_ERR_PARSE);
        remove(apath);
    }

    {
        FILE *f = fopen(path, "rb");
        JC_CHECK(f != NULL);
        if (f != NULL) {
            blen = (jc_size)fread(before, 1, sizeof before - 1, f);
            fclose(f);
        }
    }
    /* refused ... */
    JC_CHECK(jc_configedit_apply(path, "snapshots", "false",
                                 msg, sizeof msg) == JC_ERR_PARSE);
    /* ... and the file is byte-unchanged, with no .bak side effect */
    {
        FILE *f = fopen(path, "rb");
        JC_CHECK(f != NULL);
        if (f != NULL) {
            alen = (jc_size)fread(after, 1, sizeof after - 1, f);
            fclose(f);
        }
    }
    JC_CHECK(blen == alen && memcmp(before, after, blen) == 0);
    JC_CHECK(!jc_file_exists(bak));

    /* absent and empty files pass the guard (start-from-scratch) */
    {
        char nopath[300];
        jc_snprintf(nopath, sizeof nopath, "%s/jichi_cfgabsent_%d.json",
                    jc_test_tmpdir(), (int)9458);
        remove(nopath);
        JC_CHECK(jc_configedit_path_is_object(nopath) == 1);
        JC_CHECK(jc_write_file(nopath, "   \n", 4) == JC_OK);
        JC_CHECK(jc_configedit_path_is_object(nopath) == 1);
        remove(nopath);
    }

    /* a DIRECTORY target is not start-from-scratch: the read fails (JC_ERR_IO,
     * M198), the loader would fall back to an empty object, and the save would
     * overwrite the path. Refused. */
    {
        char dpath[300];
        jc_snprintf(dpath, sizeof dpath, "%s/jichi_cfgdir_%d.json",
                    jc_test_tmpdir(), (int)9459);
        remove(dpath);
        JC_CHECK(mkdir(dpath, 0700) == 0);
        JC_CHECK(jc_configedit_path_is_object(dpath) == 0);
        JC_CHECK(jc_configedit_apply(dpath, "snapshots", "false",
                                     msg, sizeof msg) == JC_ERR_PARSE);
        JC_CHECK(remove(dpath) == 0); /* remove() takes empty dirs too */
    }

    remove(path);
    remove(bak);
}

/* M1107 (D-25): the save must be SAFE in the three shapes the finding measured
 * on the rename-backup-then-fixed-temp sequence. R1: the pre-existing regular
 * file's mode survives the edit, and <path>.bak is a byte-identical COPY of the
 * pre-edit content (the old code RENAMED the live file to .bak, carrying its
 * mode to the backup and leaving the successor to the umask). R2: a symlinked
 * config is refused -- the link and its target are untouched (the old code
 * followed the link and replaced it with a regular file). R3: a DIRECTORY
 * planted at the fixed temp name <path>.tmp no longer breaks the save -- the
 * writer now uses a unique <path>.tmp<pid> name, so the planted name stays a
 * directory and the edit still lands. (R4, a failed save leaving the original
 * byte-identical, is the fault driver's leg: the unit suite cannot force the
 * atomic write to fail.) */
static void test_save_safe(void)
{
    char path[300];
    char bak[400];
    char tmp[400];
    char real[400];
    struct stat st;
    struct stat st_bak;
    const char *raw =
        "{\"model\":\"jlu/strong-model\",\"configEditable\":true,"
        "\"snapshots\":true}";
    const char *realsrc = "{\"model\":\"jlu/strong-model\"}";

    jc_snprintf(path, sizeof path, "%s/jichi_cfgsafe_%d.json",
                jc_test_tmpdir(), (int)2341);
    jc_snprintf(bak, sizeof bak, "%s.bak", path);
    jc_snprintf(tmp, sizeof tmp, "%s.tmp", path);
    jc_snprintf(real, sizeof real, "%s.real.json", path);
    remove(path);
    remove(bak);
    remove(tmp);
    remove(real);
    rmdir(tmp);

    /* ---- R1: a 0600 config keeps its mode; .bak is a byte-identical copy ---- */
    JC_CHECK(jc_write_file(path, raw, (jc_size)strlen(raw)) == JC_OK);
    JC_CHECK(chmod(path, 0600) == 0);
    {
        cJSON *root = jc_config_edit_load(path);
        JC_REQUIRE(root != NULL);
        jc_configedit_set_bool(root, "snapshots", 0);
        JC_CHECK(jc_config_edit_save(path, root) == JC_OK);
        cJSON_Delete(root);
    }
    JC_CHECK(lstat(path, &st) == 0);
    JC_CHECK(S_ISREG(st.st_mode));
    JC_CHECK((int)(st.st_mode & 0777) == 0600); /* mode kept, not umask'd */
    JC_CHECK(jc_file_exists(bak));             /* backup exists (by copy) */
    JC_CHECK(lstat(bak, &st_bak) == 0);
    /* M1107-b (D-25): the .bak is a COPY created at the umask, so it must be
     * fchmod'd to the pre-existing config's bits -- a 0600 config's .bak
     * coming back 0644 would WIDEN a file that may hold an apiKey. */
    JC_CHECK((int)(st_bak.st_mode & 0777) == 0600);
    {
        struct jc_arena *a = jc_arena_new(0);
        char *cfg = NULL, *bk = NULL;
        jc_size cl = 0, bl = 0;
        jc_status s1 = JC_ERR_IO, s2 = JC_ERR_IO;
        if (a != NULL) {
            s1 = jc_read_file(path, &cfg, &cl, a);
            s2 = jc_read_file(bak, &bk, &bl, a);
        }
        JC_CHECK(s1 == JC_OK && s2 == JC_OK);
        if (s1 == JC_OK && s2 == JC_OK && cfg != NULL && bk != NULL) {
            /* .bak is a byte-identical copy of the PRE-EDIT content (raw) */
            JC_CHECK((jc_size)strlen(raw) == bl);
            JC_CHECK((jc_size)strlen(raw) == bl
                     && memcmp(bk, raw, bl) == 0);
            /* and the live config now HOLDS THE NEW (edited) content, which
             * differs from the .bak -- the two must NOT be identical. */
            JC_CHECK(cl != bl || memcmp(cfg, bk, cl) != 0);
        }
        /* free the arena only AFTER the checks: cfg/bk/cl/bl are owned by a
         * (jc_read_file read them into it), so freeing a first would make the
         * memcmps above read freed heap (SV:1739, the -O2/ASan CI-stage
         * heap-use-after-free). */
        if (a != NULL) jc_arena_free(a);
    }
    remove(path);
    remove(bak);

    /* ---- R2: a symlinked config is refused; link + target untouched ---- */
    JC_CHECK(jc_write_file(real, realsrc, (jc_size)strlen(realsrc)) == JC_OK);
    remove(path);
    JC_CHECK(symlink(real, path) == 0);
    {
        struct jc_arena *a = jc_arena_new(0);
        char *after = NULL;
        jc_size alen = 0;
        jc_status rs = JC_ERR_IO;
        if (a != NULL) {
            struct jc_arena *b = jc_arena_new(0);
            char *rt = NULL;
            jc_size rl = 0;
            cJSON *root = NULL;
            if (b != NULL && jc_read_file(real, &rt, &rl, b) == JC_OK && rt != NULL)
                root = jc_json_parse(rt);
            if (root == NULL) root = cJSON_CreateObject();
            rs = jc_config_edit_save(path, root);
            cJSON_Delete(root);
            if (b != NULL) jc_arena_free(b);
        }
        JC_CHECK(rs != JC_OK);
        /* still a symlink, not replaced by a regular file */
        JC_CHECK(lstat(path, &st) == 0);
        JC_CHECK(S_ISLNK(st.st_mode));
        /* target byte-identical to its pre-edit content */
        if (a != NULL) {
            jc_status s = jc_read_file(real, &after, &alen, a);
            JC_CHECK(s == JC_OK);
        }
        if (after != NULL) {
            JC_CHECK(alen == (jc_size)strlen(realsrc));
            JC_CHECK(alen == (jc_size)strlen(realsrc)
                     && memcmp(after, realsrc, alen) == 0);
        }
        /* free the arena only AFTER the check: after is owned by a (jc_read_
         * file read it into it), so freeing a first would make the memcmp
         * above read freed heap (SV:1739, the -O2/ASan CI-stage
         * heap-use-after-free). */
        if (a != NULL) jc_arena_free(a);
    }
    remove(path);
    remove(bak);
    remove(real);

    /* ---- R3: a DIRECTORY at <path>.tmp no longer breaks the save ---- */
    JC_CHECK(jc_write_file(path, raw, (jc_size)strlen(raw)) == JC_OK);
    JC_CHECK(chmod(path, 0600) == 0);
    JC_CHECK(mkdir(tmp, 0700) == 0); /* planted dir at the OLD fixed temp name */
    {
        cJSON *root = jc_config_edit_load(path);
        JC_REQUIRE(root != NULL);
        jc_configedit_set_bool(root, "snapshots", 0);
        JC_CHECK(jc_config_edit_save(path, root) == JC_OK);
        cJSON_Delete(root);
    }
    /* the planted <path>.tmp is STILL a directory (untouched) */
    JC_CHECK(lstat(tmp, &st) == 0);
    JC_CHECK(S_ISDIR(st.st_mode));
    /* the edit landed, and the mode was kept */
    {
        cJSON *root2 = jc_config_edit_load(path);
        const cJSON *it =
            root2 != NULL ? cJSON_GetObjectItem(root2, "snapshots") : NULL;
        JC_CHECK(it != NULL && cJSON_IsFalse(it));
        if (root2 != NULL) cJSON_Delete(root2);
        JC_CHECK(lstat(path, &st) == 0);
        JC_CHECK((int)(st.st_mode & 0777) == 0600);
    }
    JC_CHECK(rmdir(tmp) == 0);
    remove(path);
    remove(bak);

    /* no unique <path>.tmp<pid> left behind (the writer cleans its temp on
     * every path; the suite is single-process, so our pid is the one it used) */
    {
        char probe[400];
        jc_snprintf(probe, sizeof probe, "%s.tmp%ld", path, (long)getpid());
        JC_CHECK(!jc_file_exists(probe));
    }
}

void test_configedit(void)
{
    test_set_replace_or_add();
    test_target();
    test_load_save_roundtrip();
    test_apply();
    test_apply_commented_config();
    test_apply_refuses_unparseable();
    test_save_safe();
}
