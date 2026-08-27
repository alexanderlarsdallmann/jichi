/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_vision.c - image data model, loader, and provider serialization (M29b). */

#include "jc_test.h"
#include "jc_message.h"
#include "jc_provider.h"
#include "jc_app.h"
#include "jc_mem.h"
#include "jc_platform.h"
#include "jc_snprintf.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static void set_model(struct jc_model_cfg *m, char *prov, char *id)
{
    memset(m, 0, sizeof(*m));
    m->provider = prov;
    m->model = id;
    m->api_key = (char *)"k";
    m->temperature = -1.0;
}

static void test_data_model(void)
{
    struct jc_history h;
    struct jc_message *m;

    jc_history_init(&h);
    m = jc_history_add(&h, JC_ROLE_USER, "look at this");
    JC_CHECK(jc_msg_image_count(m) == 0);
    JC_CHECK(jc_msg_add_image(m, "image/png", "QQ==") == JC_OK);
    JC_CHECK(jc_msg_add_image(m, "image/jpeg", "Zm8=") == JC_OK);
    JC_CHECK(jc_msg_image_count(m) == 2);
    JC_CHECK_STR(jc_msg_image_at(m, 0)->media_type, "image/png");
    JC_CHECK_STR(jc_msg_image_at(m, 1)->data, "Zm8=");
    jc_history_free(&h); /* frees the image strings (no leak under ASan) */
}

static void test_loader(void)
{
    struct jc_app app;
    struct jc_arena *a = jc_arena_new(0);
    struct jc_history h;
    struct jc_message *m;
    char path[256];
    long pid = (long)getpid();
    FILE *f;

    if (a == NULL) {
        return;
    }
    memset(&app, 0, sizeof(app));
    app.arena = a;
    /* config zeroed => path_fence == 0 (off), fs delegate NULL => disk. */

    jc_snprintf(path, sizeof(path), "%s/jichi_vis_%ld.png", jc_test_tmpdir(), pid);
    f = fopen(path, "wb");
    if (f != NULL) {
        fwrite("PNGDATA", 1, 7, f);
        fclose(f);
        jc_history_init(&h);
        m = jc_history_add(&h, JC_ROLE_USER, "");
        JC_CHECK(jc_app_load_image(&app, path, m) == JC_OK);
        JC_CHECK(jc_msg_image_count(m) == 1);
        if (jc_msg_image_count(m) == 1) {
            JC_CHECK_STR(jc_msg_image_at(m, 0)->media_type, "image/png");
            /* base64("PNGDATA") == "UE5HREFUQQ==" */
            JC_CHECK_STR(jc_msg_image_at(m, 0)->data, "UE5HREFUQQ==");
        }
        jc_history_free(&h);
        remove(path);
    }

    /* Unsupported extension is rejected before any read. */
    jc_history_init(&h);
    m = jc_history_add(&h, JC_ROLE_USER, "");
    JC_CHECK(jc_app_load_image(&app, "notes.txt", m) == JC_ERR_INVALID);
    JC_CHECK(jc_msg_image_count(m) == 0);
    jc_history_free(&h);

    /* An image over JC_IMAGE_MAX_BYTES is rejected by size (before the slurp),
     * not attached. */
    jc_snprintf(path, sizeof(path), "%s/jichi_vis_big_%ld.png", jc_test_tmpdir(), pid);
    f = fopen(path, "wb");
    if (f != NULL) {
        char buf[65536];
        long written = 0;
        long target = (long)JC_IMAGE_MAX_BYTES + 1024;
        memset(buf, 'x', sizeof(buf));
        while (written < target) {
            written += (long)fwrite(buf, 1, sizeof(buf), f);
        }
        fclose(f);
        jc_history_init(&h);
        m = jc_history_add(&h, JC_ROLE_USER, "");
        JC_CHECK(jc_app_load_image(&app, path, m) == JC_ERR_TOOBIG);
        JC_CHECK(jc_msg_image_count(m) == 0);
        jc_history_free(&h);
        remove(path);
    }

    jc_arena_free(a);
}

/* Build a request for a one-image user turn and return the malloc'd body. */
static char *build_image_body(char *prov, char *id)
{
    struct jc_model_cfg mc;
    struct jc_provider *p;
    struct jc_history h;
    struct jc_message *m;
    char *body = NULL;

    set_model(&mc, prov, id);
    p = jc_provider_create(&mc);
    if (p == NULL) {
        return NULL;
    }
    jc_history_init(&h);
    m = jc_history_add(&h, JC_ROLE_USER, "what is this");
    jc_msg_add_image(m, "image/png", "QQ==");
    p->vt->build_request(p, &h, "sys", NULL, 1, &body);
    jc_history_free(&h);
    p->vt->free(p);
    return body;
}

static void test_serialize(void)
{
    char openai[] = "openai";
    char anthropic[] = "anthropic";
    char gpt[] = "gpt-test";
    char claude[] = "claude-test";
    char *body;

    /* OpenAI: image_url part with a data: URI carrying the base64. */
    body = build_image_body(openai, gpt);
    JC_CHECK(body != NULL);
    if (body != NULL) {
        JC_CHECK(strstr(body, "\"type\":\"image_url\"") != NULL);
        JC_CHECK(strstr(body, "data:image/png;base64,QQ==") != NULL);
        JC_CHECK(strstr(body, "\"type\":\"text\"") != NULL);
        JC_CHECK(strstr(body, "what is this") != NULL);
        free(body);
    }

    /* Anthropic: image block with a base64 source. */
    body = build_image_body(anthropic, claude);
    JC_CHECK(body != NULL);
    if (body != NULL) {
        JC_CHECK(strstr(body, "\"type\":\"image\"") != NULL);
        JC_CHECK(strstr(body, "\"type\":\"base64\"") != NULL);
        JC_CHECK(strstr(body, "\"media_type\":\"image/png\"") != NULL);
        JC_CHECK(strstr(body, "\"data\":\"QQ==\"") != NULL);
        free(body);
    }
}

void test_vision(void)
{
    test_data_model();
    test_loader();
    test_serialize();
}
