/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_sound.c - the play_audio / record_audio tools (M163b).
 *
 * Shell OUT to the configured `sound.play` / `sound.record` command (aplay,
 * arecord, ffplay, ...) -- jichi never links an audio library (the M42
 * external-extractor pattern). The audio file path is jichi-chosen and
 * workspace-fenced, exported as $JICHI_AUDIO_FILE and, in argv form, appended as
 * the final argv element (so a model argument never lands on the command
 * line). Record duration is enforced jichi-side via jc_proc_capture's timeout.
 * Both tools are mutating (ASK in chat, denied in PLAN); registered only when
 * their command is configured. */

#include "tool_util.h"
#include "jc_app.h"
#include "jc_sound.h"
#include "jc_proc.h"
#include "jc_str.h"
#include "jc_vec.h"
#include "jc_path.h"
#include "jc_platform.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

/* Small caps: these commands are drivers, not producers of text output. */
#define SOUND_OUT_CAP 4096
#define SOUND_ARGV_CAP 64

/* Build the env vec ("JICHI_AUDIO_FILE=..." [+ "JICHI_AUDIO_SECONDS=..."]).
 * Entries are heap-owned; freed by the caller. */


/* Run the play/record command (argv or shell form) with the file/seconds env
 * and the given timeout. Returns the child exit code (or -1/-2). */
/* M303: the runner and its env helpers moved to src/util/jc_sound.c as
 * jc_sound_run, so voice mode shares one mechanism. */
#define sound_run jc_sound_run

/* --- play_audio ---------------------------------------------------------- */

static cJSON *play_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_string(s, "path",
        "Workspace path to an audio file to play through the speaker", 1);
    return s;
}

static jc_status play_run(const cJSON *args, struct jc_tool_result *out,
                          struct jc_app *app)
{
    const char *path = tu_arg_str(args, "path");
    const struct jc_sound_cfg *sc = &app->config.sound;
    char abs[JC_PATH_MAX];
    struct jc_sb res;
    long timeout;
    int code;

    if (path == NULL || path[0] == '\0') {
        tu_err(out, "error: 'path' is required");
        return JC_OK;
    }
    if (sc->play_command == NULL && sc->play_shell == NULL) {
        tu_err(out, "error: no sound.play command configured");
        return JC_OK;
    }
    if (jc_app_path_denied_ex(app, path, 0)) {
        tu_err_policy(out, "error: refused by safety fence (path outside workspace)");
        return JC_OK;
    }
    if (jc_path_resolve(path, abs, sizeof(abs)) != JC_OK ||
        !jc_file_exists(abs)) {
        tu_err(out, "error: audio file does not exist");
        return JC_OK;
    }
    timeout = (sc->play_timeout > 0) ? sc->play_timeout
                                     : JC_SOUND_PLAY_TIMEOUT_DEFAULT;
    jc_sb_init(&res);
    code = sound_run(sc->play_command, &sc->play_args, sc->play_shell,
                     abs, 0, timeout, &res, &app->abort_flag);
    if (code == 0) {
        char msg[JC_PATH_MAX + 64];
        jc_snprintf(msg, sizeof(msg), "played %s", path);
        tu_ok_copy(out, msg);
    } else if (code == -2) {
        tu_err(out, "error: playback timed out and was stopped");
    } else {
        struct jc_sb e;
        jc_sb_init(&e);
        jc_sb_append_fmt(&e, "error: playback command failed (exit %d)", code);
        if (res.len > 0) { jc_sb_append(&e, "\n"); jc_sb_append(&e, res.data); }
        tu_err(out, e.data != NULL ? e.data : "error: playback failed");
        jc_sb_free(&e);
    }
    jc_sb_free(&res);
    return JC_OK;
}

static const struct jc_tool PLAY_TOOL = {
    "play_audio",
    "Play an audio file from the workspace through the configured speaker "
    "(text-to-speech output, alerts, status). Returns when playback finishes.",
    play_schema,
    0, /* mutating: emits sound into the shared environment */
    play_run,
    NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_play_audio(void)
{
    return &PLAY_TOOL;
}

/* --- record_audio -------------------------------------------------------- */

static cJSON *record_schema(void)
{
    cJSON *s = tu_schema_begin();
    tu_schema_int(s, "seconds",
        "How long to record (clamped to the configured maximum)", 1);
    tu_schema_string(s, "path",
        "Optional workspace path for the recording (default recording-<ts>.wav)",
        0);
    return s;
}

static jc_status record_run(const cJSON *args, struct jc_tool_result *out,
                            struct jc_app *app)
{
    const char *path = tu_arg_str(args, "path");
    int req = tu_arg_int(args, "seconds", 0);
    const struct jc_sound_cfg *sc = &app->config.sound;
    char namebuf[64];
    char abs[JC_PATH_MAX];
    struct jc_sb res;
    long secs;
    long timeout;
    long size = 0;
    int code;

    if (sc->record_command == NULL && sc->record_shell == NULL) {
        tu_err(out, "error: no sound.record command configured");
        return JC_OK;
    }
    if (path == NULL || path[0] == '\0') {
        jc_sound_default_name("recording", jc_now_seconds(),
                              namebuf, sizeof(namebuf));
        path = namebuf;
    }
    if (jc_app_path_denied(app, path)) {
        tu_err_policy(out, "error: refused by safety fence (path outside workspace)");
        return JC_OK;
    }
    if (jc_path_resolve(path, abs, sizeof(abs)) != JC_OK) {
        tu_err(out, "error: could not resolve the output path");
        return JC_OK;
    }
    secs = jc_sound_clamp_seconds((long)req, sc->record_max);
    /* Enforce the duration jichi-side: the recorder is stopped after `secs`
     * (+ a small grace so a well-behaved `-d N` can exit on its own). */
    timeout = secs + 5;
    jc_sb_init(&res);
    code = sound_run(sc->record_command, &sc->record_args, sc->record_shell,
                     abs, secs, timeout, &res, &app->abort_flag);
    /* A jichi-stopped recorder (timeout, code -2) is EXPECTED and fine as long as
     * it produced a file -- that is how `arecord out.wav` (no -d) terminates. */
    if (jc_file_exists(abs)) {
        char *bytes = NULL;
        jc_size blen = 0;
        if (jc_read_file(abs, &bytes, &blen, jc_app_scratch(app)) == JC_OK) {
            size = (long)blen;
        }
    }
    if (size > 0) {
        struct jc_sb m;
        long cap = (long)jc_config_cap(app->config.transcribe_max_bytes,
                                       25L * 1024 * 1024);
        jc_sb_init(&m);
        jc_sb_append_fmt(&m, "recorded %ld second(s) to %s (%ld bytes)",
                         secs, path, size);
        if (size > cap) {
            jc_sb_append_fmt(&m, "\nnote: larger than the transcribe cap "
                             "(%ld bytes); transcription will refuse it", cap);
        }
        tu_ok_copy(out, m.data != NULL ? m.data : "recorded");
        jc_sb_free(&m);
    } else if (code == -1) {
        tu_err(out, "error: could not start the record command");
    } else {
        struct jc_sb e;
        jc_sb_init(&e);
        jc_sb_append(&e, "error: recording produced no file");
        if (res.len > 0) { jc_sb_append(&e, "\n"); jc_sb_append(&e, res.data); }
        tu_err(out, e.data != NULL ? e.data : "error: recording failed");
        jc_sb_free(&e);
    }
    jc_sb_free(&res);
    return JC_OK;
}

static const struct jc_tool RECORD_TOOL = {
    "record_audio",
    "Record from the configured microphone to a workspace file for the given "
    "number of seconds (speech-to-text input, ambient capture). Returns the "
    "saved path; feed it to transcribe_audio.",
    record_schema,
    0, /* mutating: captures the environment + writes a file */
    record_run,
    NULL, NULL, NULL,
    0 /* main_agent_only (M436) */
};

const struct jc_tool *jc_tool_record_audio(void)
{
    return &RECORD_TOOL;
}
