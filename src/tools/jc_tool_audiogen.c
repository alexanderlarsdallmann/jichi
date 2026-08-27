/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_audiogen.c - the generate_audio (text-to-speech) tool (M32).
 *
 * Calls the OpenAI-compatible /v1/audio/speech endpoint of the model that
 * declares the "audio" role, and saves the returned audio bytes to a workspace
 * path through the path-fenced jc_app_write_file. The result is the saved path.
 * Registered only when an audio-role model is configured. Mutating =>
 * permission-gated.
 */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_audiogen.h"
#include "jc_neterr.h"
#include "jc_platform.h"
#include "jc_path.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

#define AUDIOGEN_MAX_BYTES (32 * 1024 * 1024)

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

static cJSON *audiogen_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "text", "The text to synthesize as speech", 1);
    tu_schema_string(s, "path",
        "Workspace path to save the audio to; the extension "
        "(.mp3/.wav/.opus/.aac/.flac/.pcm) selects the format", 1);
    tu_schema_string(s, "voice",
        "Optional voice name (model-dependent, e.g. \"alloy\")", 0);
    return s;
}

static jc_status audiogen_run(const cJSON *args, struct jc_tool_result *out,
                              struct jc_app *app)
{
    const char *text = tu_arg_str(args, "text");
    const char *path = tu_arg_str(args, "path");
    const char *voice = tu_arg_str(args, "voice");
    const char *fmt;
    struct jc_model_cfg *m;
    unsigned char *bytes = NULL;
    jc_size len = 0;
    jc_size cap;
    char msg[1100];
    jc_status st;
    long http;

    if (text == NULL || text[0] == '\0' || path == NULL || path[0] == '\0') {
        tu_err(out, "error: 'text' and 'path' are required");
        return JC_OK;
    }
    fmt = jc_audiogen_format(path);
    if (fmt == NULL) {
        tu_err(out, "error: unsupported audio extension "
                    "(use .mp3/.wav/.opus/.aac/.flac/.pcm)");
        return JC_OK;
    }
    m = jc_app_model_for_role(app, JC_ROLE_AUDIO);
    if (m == NULL) {
        tu_err(out, "error: no audio-generation model configured (add a model "
                    "with role \"audio\")");
        return JC_OK;
    }
    if (jc_app_path_denied(app, path)) {
        tu_err_policy(out, "error: refused by safety fence (path outside workspace)");
        return JC_OK;
    }

    http = 0;
    st = jc_audiogen_run(m, text, voice, fmt, &bytes, &len, &app->abort_flag,
                         &http);
    if (st != JC_OK || bytes == NULL) {
        free(bytes);
        /* M500: name the status and what it implies. The old message was the
         * same sentence for a 500, a 401 and an unreachable host, so a model
         * could only respond by retrying the arguments -- measured: three
         * variations, 428 output tokens, then advice to fix a correct key. */
        tu_err(out, jc_neterr_render(msg, sizeof(msg), "audio generation",
                                     http, "/audio/speech"));
        return JC_OK;
    }
    cap = jc_config_cap(app->config.audio_gen_max_bytes, AUDIOGEN_MAX_BYTES);
    if (len > cap) {
        free(bytes);
        jc_snprintf(msg, sizeof(msg),
                    "error: generated audio is %lu bytes, over the %lu-byte cap",
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
    jc_snprintf(msg, sizeof(msg), "Saved generated audio (%lu bytes) to %s",
                (unsigned long)len, path);
    tu_ok_copy(out, msg);
    return JC_OK;
}

static const struct jc_tool AUDIOGEN_TOOL = {
    "generate_audio",
    "Synthesize speech from text and save it to a workspace path. The file "
    "extension (.mp3/.wav/.opus/.aac/.flac/.pcm) selects the format. Returns "
    "the saved path.",
    audiogen_schema,
    0, /* mutating */
    audiogen_run,
    NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_generate_audio(void)
{
    return &AUDIOGEN_TOOL;
}
