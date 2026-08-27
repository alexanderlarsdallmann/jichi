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

void test_configedit(void)
{
    test_set_replace_or_add();
    test_target();
    test_load_save_roundtrip();
    test_apply();
}
