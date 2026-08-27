/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_sound.c - pure helpers for the sound I/O tools (see jc_sound.h). */

#include "jc_sound.h"
#include "jc_platform.h"
#include <stdlib.h>
#include "jc_vec.h"
#include "jc_str.h"
#include "jc_proc.h"
#include "jc_snprintf.h"

long jc_sound_clamp_seconds(long req, long max)
{
    long eff = (max > 0) ? max : JC_SOUND_RECORD_DEFAULT;
    if (eff > JC_SOUND_RECORD_CAP) eff = JC_SOUND_RECORD_CAP;
    if (req <= 0) return eff;
    if (req > eff) return eff;
    return req;
}

void jc_sound_default_name(const char *kind, double ts, char *out, jc_size cap)
{
    long secs = (long)ts;
    if (secs < 0) secs = 0;
    jc_snprintf(out, cap, "%s-%ld.wav", (kind != NULL) ? kind : "audio", secs);
}

int jc_sound_build_argv(const char *command, char *const *args, int nargs,
                        const char *file, char **argv_out, int cap)
{
    int n = 0;
    int i;

    if (command == NULL || argv_out == NULL || cap < 2) {
        return -1;
    }
    /* Need room for command + nargs + optional file + the NULL terminator. */
    if (nargs < 0) nargs = 0;
    if (1 + nargs + (file != NULL ? 1 : 0) + 1 > cap) {
        return -1;
    }
    argv_out[n++] = (char *)command;
    for (i = 0; i < nargs; i++) {
        argv_out[n++] = args[i];
    }
    if (file != NULL) {
        argv_out[n++] = (char *)file;
    }
    argv_out[n] = NULL;
    return n;
}

/* --- running a configured play/record command (M303) ------------------------
 *
 * Moved here from jc_tool_sound.c so the VOICE mode (jc_voice.c) and the
 * play_audio/record_audio tools share one mechanism. Both need the same env
 * contract ($JICHI_AUDIO_FILE / $JICHI_AUDIO_SECONDS, which a shell-form command
 * relies on), and a second copy of that would drift. util owns the mechanism; the
 * tool layer owns the tool. */
#define SOUND_OUT_CAP 4096
#define SOUND_ARGV_CAP 64

static void sound_env(struct jc_vec *env, const char *file, long secs)
{
    struct jc_sb kv;
    jc_vec_init(env, sizeof(char *));
    jc_sb_init(&kv);
    jc_sb_append(&kv, "JICHI_AUDIO_FILE=");
    jc_sb_append(&kv, file);
    if (kv.data != NULL) jc_vec_push(env, &kv.data);
    if (secs > 0) {
        struct jc_sb sv;
        jc_sb_init(&sv);
        jc_sb_append_fmt(&sv, "JICHI_AUDIO_SECONDS=%ld", secs);
        if (sv.data != NULL) jc_vec_push(env, &sv.data);
    }
}

static void sound_env_free(struct jc_vec *env)
{
    jc_size i;
    for (i = 0; i < env->len; i++) free(*(char **)jc_vec_at(env, i));
    jc_vec_free(env);
}

int jc_sound_run(const char *command, const struct jc_vec *args,
                 const char *shell, const char *file, long secs,
                 long timeout, struct jc_sb *out, volatile int *abort_flag)
{
    struct jc_vec env;
    int code;

    sound_env(&env, file, secs);
    if (shell != NULL) {
        char *argv[4];
        argv[0] = (char *)jc_shell_path();
        argv[1] = (char *)"-c";
        argv[2] = (char *)shell;
        argv[3] = NULL;
        code = jc_proc_capture(argv, &env, NULL, out, SOUND_OUT_CAP,
                               timeout, abort_flag);
    } else {
        char *argv[SOUND_ARGV_CAP];
        char *aptr[SOUND_ARGV_CAP];
        int na = 0;
        jc_size i;
        for (i = 0; i < args->len && na < SOUND_ARGV_CAP - 4; i++) {
            aptr[na++] = *(char **)jc_vec_at((struct jc_vec *)args, i);
        }
        if (jc_sound_build_argv(command, aptr, na, file, argv,
                                SOUND_ARGV_CAP) < 0) {
            sound_env_free(&env);
            return -1;
        }
        code = jc_proc_capture(argv, &env, NULL, out, SOUND_OUT_CAP,
                               timeout, abort_flag);
    }
    sound_env_free(&env);
    return code;
}
