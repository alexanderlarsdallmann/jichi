/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_imagegen.c - the generate_image tool (M32 + per-workflow selection
 * and image editing).
 *
 * Calls the OpenAI-compatible /v1/images/generations endpoint of an image-role
 * model, decodes the result, and saves it to a workspace path through the
 * path-fenced jc_app_write_file. The tool result is the saved path (never inline
 * base64). Registered only when an image-role model is configured. Mutating =>
 * permission-gated.
 *
 * The model is the first one declaring role "image" by default, or a specific
 * one named via the optional "model" argument (so users/agents can pick a
 * workflow-appropriate model — anime vs pixel vs watercolor vs icon). An
 * optional "source" workspace image turns the call into an edit (img2img / FLUX
 * Kontext): it is read through the path fence, base64-encoded into a data: URI,
 * and sent as the model's ref_images.
 *
 * The tool is a dynamic (ctx-carrying) tool so its schema can enumerate the
 * configured image models for the agent; ctx is the jc_app*.
 */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_imagegen.h"
#include "jc_image.h"
#include "jc_base64.h"
#include "jc_platform.h"
#include "jc_path.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

#define IMAGEGEN_MAX_BYTES (16 * 1024 * 1024)

/* Ensure the parent directory of `path` exists (same as the write tool). */
static jc_status ensure_parent(const char *path)
{
    char dir[JC_PATH_MAX];
    jc_size n = strlen(path);
    jc_size i;
    if (n >= sizeof(dir)) {
        return JC_ERR_TOOBIG;
    }
    memcpy(dir, path, n + 1);
    for (i = n; i > 0; i--) {
        if (dir[i] == '/') {
            dir[i] = '\0';
            jc_mkdir_p(dir);
            return JC_OK;
        }
    }
    return JC_OK;
}

static cJSON *imagegen_schema_ctx(void *ctx)
{
    struct jc_app *app = (struct jc_app *)ctx;
    cJSON *s = tu_schema_begin();
    struct jc_sb desc;

    tu_schema_string(s, "prompt", "A description of the image to generate "
        "(for an edit with \"source\", how to change it)", 1);
    tu_schema_string(s, "path",
        "Workspace path to save the image to; the extension (.png/.jpg/.webp) "
        "selects the format", 1);
    tu_schema_string(s, "size",
        "Optional dimensions like \"1024x1024\" (model-dependent)", 0);
    tu_schema_string(s, "source",
        "Optional workspace path to a source image to edit (img2img / Kontext); "
        "requires an editing-capable image model", 0);

    /* The model arg: enumerate the configured image-role models so the agent
     * can pick one per workflow. */
    jc_sb_init(&desc);
    jc_sb_append(&desc, "Optional: which configured image model to use "
        "(by name); omit for the default.");
    if (app != NULL) {
        struct jc_sb menu;
        jc_sb_init(&menu);
        jc_config_models_for_role_list(&app->config, JC_ROLE_IMAGE, &menu);
        if (menu.len > 0) {
            jc_sb_append(&desc, " Available models:\n");
            jc_sb_append(&desc, menu.data != NULL ? menu.data : "");
        }
        jc_sb_free(&menu);
    }
    tu_schema_string(s, "model", desc.data != NULL ? desc.data : "", 0);
    jc_sb_free(&desc);
    return s;
}

/* Resolve the image model: the "model" selector when given (validated to carry
 * the image role), else the first image-role model. Writes a tool-ready error
 * into `err` and returns NULL on failure. */
static struct jc_model_cfg *resolve_image_model(struct jc_app *app,
                                                const char *sel,
                                                char *err, jc_size errn)
{
    if (sel != NULL && sel[0] != '\0') {
        int idx = jc_config_find_model(&app->config, sel);
        struct jc_model_cfg *m;
        if (idx < 0) {
            jc_snprintf(err, errn, "error: no configured model matches '%s'",
                        sel);
            return NULL;
        }
        idx = jc_app_effective_model(app, idx);
        m = jc_config_model_at(&app->config, idx);
        if (m == NULL || (m->roles & JC_ROLE_IMAGE) == 0u) {
            jc_snprintf(err, errn,
                "error: model '%s' does not have the \"image\" role", sel);
            return NULL;
        }
        return m;
    }
    return jc_app_model_for_role(app, JC_ROLE_IMAGE);
}

/* Read `source` (path-fenced) and base64-encode it (standard alphabet, no
 * "data:" prefix) for the model's ref_images. The OpenAI-compatible image API
 * and LocalAI expect each ref_images entry to be a URL or a *raw* base64 string
 * -- a "data:<mime>;base64,..." URI fails their base64 decode and the source is
 * silently dropped (verified against LocalAI 4.5.0
 * core/http/endpoints/openai/image.go processImageFile) -- so we send the bare
 * encoding. Returns JC_OK + *out_b64 (caller frees), or an error with *out_b64
 * left NULL. */
static jc_status build_source_b64(struct jc_app *app, const char *source,
                                  char **out_b64)
{
    char *data = NULL;
    jc_size len = 0;
    jc_size cap;
    char *b64;
    jc_status st;

    *out_b64 = NULL;
    st = jc_app_read_file(app, source, &data, &len, jc_app_tool_scratch(app));
    if (st != JC_OK) {
        return st;
    }
    cap = jc_base64_encoded_len(len) + 1;
    b64 = (char *)malloc(cap);
    if (b64 == NULL) {
        return JC_ERR_OOM;
    }
    if (jc_base64_encode((const unsigned char *)data, len, b64, cap) != JC_OK) {
        free(b64);
        return JC_ERR_TOOBIG;
    }
    *out_b64 = b64;
    return JC_OK;
}

static jc_status imagegen_run_ctx(void *ctx, const cJSON *args,
                                  struct jc_tool_result *out,
                                  struct jc_app *app)
{
    const char *prompt = tu_arg_str(args, "prompt");
    const char *path = tu_arg_str(args, "path");
    const char *size = tu_arg_str(args, "size");
    const char *model_sel = tu_arg_str(args, "model");
    const char *source = tu_arg_str(args, "source");
    const char *fmt;
    struct jc_model_cfg *m;
    unsigned char *bytes = NULL;
    jc_size len = 0;
    jc_size cap;
    char *ref_b64 = NULL;
    const char *ref_images[1];
    int n_ref = 0;
    char msg[1100];
    jc_status st;

    (void)ctx;
    if (prompt == NULL || prompt[0] == '\0' || path == NULL || path[0] == '\0') {
        tu_err(out, "error: 'prompt' and 'path' are required");
        return JC_OK;
    }
    fmt = jc_image_gen_format(path);
    if (fmt == NULL) {
        tu_err(out, "error: unsupported image extension (use .png/.jpg/.webp)");
        return JC_OK;
    }
    m = resolve_image_model(app, model_sel, msg, sizeof(msg));
    if (m == NULL) {
        if (model_sel == NULL || model_sel[0] == '\0') {
            tu_err(out, "error: no image-generation model configured (add a "
                        "model with role \"image\")");
        } else {
            tu_err(out, msg);
        }
        return JC_OK;
    }
    /* Fence the destination before spending an API call on a doomed write. */
    if (jc_app_path_denied(app, path)) {
        tu_err_policy(out, "error: refused by safety fence (path outside workspace)");
        return JC_OK;
    }
    /* An edit: read the source image (path-fenced), base64 it for ref_images. */
    if (source != NULL && source[0] != '\0') {
        st = build_source_b64(app, source, &ref_b64);
        if (st != JC_OK) {
            jc_snprintf(msg, sizeof(msg),
                        "error: could not read source image '%s'", source);
            tu_err(out, msg);
            return JC_OK;
        }
        ref_images[0] = ref_b64;
        n_ref = 1;
    }

    st = jc_imagegen_run(m, prompt, size, fmt,
                         n_ref > 0 ? ref_images : NULL, n_ref,
                         1, &bytes, &len, &app->abort_flag);
    free(ref_b64);
    if (st != JC_OK || bytes == NULL) {
        free(bytes);
        tu_err(out, "error: image generation request failed");
        return JC_OK;
    }
    cap = jc_config_cap(app->config.image_gen_max_bytes, IMAGEGEN_MAX_BYTES);
    if (len > cap) {
        free(bytes);
        jc_snprintf(msg, sizeof(msg),
                    "error: generated image is %lu bytes, over the %lu-byte cap",
                    (unsigned long)len, (unsigned long)cap);
        tu_err(out, msg);
        return JC_OK;
    }
    if (ensure_parent(path) != JC_OK) {
        free(bytes);
        tu_err(out, "error: path is too long");
        return JC_OK;
    }
    st = jc_app_write_file(app, path, (const char *)bytes, len);
    free(bytes);
    if (st != JC_OK) {
        jc_snprintf(msg, sizeof(msg), "error: could not write '%s'", path);
        tu_err(out, msg);
        return JC_OK;
    }
    jc_snprintf(msg, sizeof(msg), "Saved generated image (%lu bytes) to %s",
                (unsigned long)len, path);
    tu_ok_copy(out, msg);
    return JC_OK;
}

const struct jc_tool *jc_tool_generate_image(struct jc_app *app)
{
    struct jc_tool *t;
    if (app == NULL || app->arena == NULL) {
        return NULL;
    }
    t = (struct jc_tool *)jc_arena_calloc(app->arena, sizeof(*t));
    if (t == NULL) {
        return NULL;
    }
    t->name = "generate_image";
    t->description =
        "Generate an image from a text prompt (or edit a source image) and save "
        "it to a workspace path. The file extension (.png/.jpg/.webp) selects "
        "the format. Optionally pick a configured image model with \"model\" "
        "and edit an existing image with \"source\". Returns the saved path.";
    t->ctx = app;
    t->schema_ctx = imagegen_schema_ctx;
    t->run_ctx = imagegen_run_ctx;
    return t;
}
