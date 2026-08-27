/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_transcribe.c - the transcribe_audio tool (M33).
 *
 * Reads an audio file from the workspace (through the path-fenced, capped
 * jc_app_read_file) and POSTs it to the OpenAI-compatible
 * /v1/audio/transcriptions endpoint of the model that declares the "transcribe"
 * role, returning the transcript text. Read-only (no file writes). Registered
 * only when a transcribe-role model is configured.
 */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_transcribe.h"
#include "jc_neterr.h"
#include "jc_audio.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

static cJSON *transcribe_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "path",
        "Workspace path to an audio file (.mp3/.wav/.m4a/.flac/.ogg/.webm)", 1);
    tu_schema_string(s, "language",
        "Optional ISO-639-1 language hint (e.g. \"en\") to improve accuracy", 0);
    return s;
}

static jc_status transcribe_run(const cJSON *args, struct jc_tool_result *out,
                                struct jc_app *app)
{
    const char *path = tu_arg_str(args, "path");
    const char *language = tu_arg_str(args, "language");
    const char *ctype;
    const char *base;
    const char *slash;
    struct jc_model_cfg *m;
    char *bytes = NULL;
    jc_size len = 0;
    jc_size cap;
    char *text = NULL;
    jc_status st;
    long http;
    char msg[300];

    if (path == NULL || path[0] == '\0') {
        tu_err(out, "error: 'path' is required");
        return JC_OK;
    }
    ctype = jc_audio_media_type(path);
    if (ctype == NULL) {
        tu_err(out, "error: unsupported audio extension "
                    "(use .mp3/.wav/.m4a/.flac/.ogg/.webm)");
        return JC_OK;
    }
    m = jc_app_model_for_role(app, JC_ROLE_TRANSCRIBE);
    if (m == NULL) {
        tu_err(out, "error: no transcription model configured (add a model "
                    "with role \"transcribe\")");
        return JC_OK;
    }
    if (jc_app_path_denied_ex(app, path, 0)) {
        tu_err_policy(out, "error: refused by safety fence (path outside workspace)");
        return JC_OK;
    }
    /* Read into the per-turn scratch arena (reclaimed next turn; the multipart
     * body copies the bytes). */
    st = jc_app_read_file(app, path, &bytes, &len, jc_app_tool_scratch(app));
    if (st != JC_OK) {
        tu_err(out, "error: could not read the audio file");
        return JC_OK;
    }
    cap = jc_config_cap(app->config.transcribe_max_bytes,
                        JC_TRANSCRIBE_MAX_BYTES);
    if (len > cap) {
        char msg[160];
        jc_snprintf(msg, sizeof(msg),
                    "error: audio file is %lu bytes, over the %lu-byte cap",
                    (unsigned long)len, (unsigned long)cap);
        tu_err(out, msg);
        return JC_OK;
    }

    slash = strrchr(path, '/');
    base = (slash != NULL) ? slash + 1 : path;
    http = 0;
    st = jc_transcribe_run(m, (const unsigned char *)bytes, len, base, ctype,
                           (language != NULL && language[0] != '\0') ? language
                                                                     : NULL,
                           &text, &app->abort_flag, &http);
    if (st != JC_OK || text == NULL) {
        free(text);
        tu_err(out, jc_neterr_render(msg, sizeof(msg), "transcription",
                                     http, "/audio/transcriptions"));
        return JC_OK;
    }
    tu_ok_owned(out, text);
    return JC_OK;
}

static const struct jc_tool TRANSCRIBE_TOOL = {
    "transcribe_audio",
    "Transcribe an audio file in the workspace to text (speech-to-text). The "
    "path's extension selects the format. Returns the transcript.",
    transcribe_schema,
    1, /* read-only */
    transcribe_run,
    NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_transcribe_audio(void)
{
    return &TRANSCRIBE_TOOL;
}
