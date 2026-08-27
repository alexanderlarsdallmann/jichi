/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_voice.c - speaking jichi's output aloud (see jc_voice.h). */

#include "jc_voice.h"
#include "jc_app.h"
#include "jc_str.h"
#include "jc_log.h"
#include "jc_snprintf.h"
#include "jc_audiogen.h"
#include "jc_neterr.h"
#include "jc_sound.h"
#include "jc_proc.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Is `p` at the start of a ``` fence line? */
static int at_fence(const char *p)
{
    return p[0] == '`' && p[1] == '`' && p[2] == '`';
}

/* Skip a fenced block starting at `p` (which is at the fence), counting its lines
 * and returning the position just past the closing fence (or at the NUL). */
static const char *skip_fence(const char *p, int *lines)
{
    int n = 0;
    /* past the opening fence line */
    while (*p != '\0' && *p != '\n') {
        p++;
    }
    if (*p == '\n') {
        p++;
    }
    while (*p != '\0') {
        if (at_fence(p)) {
            while (*p != '\0' && *p != '\n') {
                p++;
            }
            if (*p == '\n') {
                p++;
            }
            break;
        }
        n++;
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
    }
    *lines = n;
    return p;
}

void jc_voice_speakable(const char *text, struct jc_sb *out)
{
    struct jc_sb tmp;
    const char *p;
    int pending_space = 0;
    int at_line_start = 1;

    if (out == NULL || text == NULL || text[0] == '\0') {
        return;
    }
    jc_sb_init(&tmp);
    p = text;
    while (*p != '\0') {
        if (at_line_start && at_fence(p)) {
            int lines = 0;
            char note[64];
            p = skip_fence(p, &lines);
            jc_snprintf(note, sizeof(note), " (code block, %d line%s) ",
                        lines, lines == 1 ? "" : "s");
            jc_sb_append(&tmp, note);
            pending_space = 1;
            at_line_start = 1;
            continue;
        }
        if (*p == '\n') {
            /* Any run of newlines becomes one break; the reader hears a pause
             * from the punctuation, not from blank lines. */
            while (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t') {
                p++;
            }
            pending_space = 1;
            at_line_start = 1;
            continue;
        }
        if (at_line_start) {
            /* Leading markdown furniture on a line: heading hashes, list
             * bullets, blockquote markers. Dropped, not spoken. */
            while (*p == '#' || *p == '>' || *p == ' ' || *p == '\t') {
                p++;
            }
            if ((*p == '-' || *p == '*' || *p == '+') &&
                (p[1] == ' ' || p[1] == '\t')) {
                p += 2;
                while (*p == ' ' || *p == '\t') {
                    p++;
                }
            }
            at_line_start = 0;
            if (*p == '\0') {
                break;
            }
        }
        if (*p == ' ' || *p == '\t') {
            pending_space = 1;
            p++;
            continue;
        }
        /* Inline decoration carries no sound. A backtick, asterisk or underscore
         * read aloud is noise ("star star important star star"). */
        if (*p == '`' || *p == '*' || *p == '_') {
            p++;
            continue;
        }
        if (pending_space && tmp.len > 0) {
            jc_sb_append_char(&tmp, ' ');
        }
        pending_space = 0;
        jc_sb_append_char(&tmp, *p);
        p++;
    }

    /* Cap it. With no barge-in a long utterance cannot be escaped, so prefer a
     * sentence boundary and say that it was cut. */
    if (tmp.len > (jc_size)JC_VOICE_MAX_CHARS) {
        jc_size cut = (jc_size)JC_VOICE_MAX_CHARS;
        jc_size i = cut;
        while (i > (jc_size)(JC_VOICE_MAX_CHARS / 2)) {
            char c = tmp.data[i - 1];
            if (c == '.' || c == '!' || c == '?') {
                cut = i;
                break;
            }
            i--;
        }
        jc_sb_append_n(out, tmp.data, cut);
        jc_sb_append(out, " (truncated)");
    } else if (tmp.len > 0) {
        jc_sb_append_n(out, tmp.data, tmp.len);
    }
    jc_sb_free(&tmp);
}

int jc_voice_available(struct jc_app *app, char *why, jc_size cap)
{
    const struct jc_model_cfg *m;
    int have_play;

    if (app == NULL) {
        if (why != NULL) {
            jc_snprintf(why, cap, "no application context");
        }
        return 0;
    }
    m = jc_app_model_for_role(app, JC_ROLE_AUDIO);
    have_play = (app->config.sound.play_command != NULL &&
                 app->config.sound.play_command[0] != '\0') ||
                (app->config.sound.play_shell != NULL &&
                 app->config.sound.play_shell[0] != '\0');
    if (m == NULL && !have_play) {
        if (why != NULL) {
            jc_snprintf(why, cap,
                "voice needs a model with the \"audio\" role (for speech) and a "
                "\"sound\": {\"play\": ...} command (to play it); neither is "
                "configured");
        }
        return 0;
    }
    if (m == NULL) {
        if (why != NULL) {
            jc_snprintf(why, cap,
                "voice needs a model declaring the \"audio\" role to synthesize "
                "speech; none is configured");
        }
        return 0;
    }
    if (!have_play) {
        if (why != NULL) {
            jc_snprintf(why, cap,
                "voice needs a \"sound\": {\"play\": \"aplay\"} (or ffplay/paplay) "
                "command to play the synthesized speech; none is configured");
        }
        return 0;
    }
    return 1;
}

jc_status jc_voice_say(struct jc_app *app, const char *text)
{
    const struct jc_model_cfg *m;
    struct jc_sb say;
    unsigned char *bytes = NULL;
    jc_size len = 0;
    jc_status st;
    long http;
    char path[1200];
    char name[128];

    if (!jc_voice_available(app, NULL, 0)) {
        return JC_ERR_INVALID;
    }
    jc_sb_init(&say);
    jc_voice_speakable(text, &say);
    if (say.len == 0) {
        jc_sb_free(&say);
        return JC_OK;                     /* nothing worth saying */
    }
    m = jc_app_model_for_role(app, JC_ROLE_AUDIO);
    http = 0;
    st = jc_audiogen_run(m, say.data, NULL, "wav", &bytes, &len,
                         &app->abort_flag, &http);
    jc_sb_free(&say);
    if (st != JC_OK || bytes == NULL || len == 0) {
        char why[300];
        free(bytes);
        /* Reported, not silent: a screen-less user must learn that speech failed
         * rather than wonder whether jichi is still thinking. M500: and WHICH
         * failure it was -- this is the one surface whose user cannot read the
         * screen to find out, so "synthesis failed (provider error)" was the
         * least useful place in the tree to drop a status code. */
        jc_neterr_render(why, sizeof(why), "speech synthesis", http,
                         "/audio/speech");
        jc_logf(JC_LOG_WARN, "voice: %s (%s)", why, jc_status_str(st));
        return st != JC_OK ? st : JC_ERR_PROVIDER;
    }
    /* Written to the private sink dir, not the workspace: a spoken reply is
     * jichi's own transient artefact and must not land in the user's tree (nor
     * inside a snapshot's blast radius -- the ANECDOTES #1 lesson). */
    jc_sound_default_name("speech", jc_now_seconds(), name, sizeof(name));
    {
        char dir[1100];
        jc_snprintf(dir, sizeof(dir), "%s/.jichi.d/voice", jc_home_dir());
        jc_mkdir_p(dir);
        jc_snprintf(path, sizeof(path), "%s/%s", dir, name);
    }
    if (jc_write_file_atomic(path, (const char *)bytes, len) != JC_OK) {
        free(bytes);
        jc_logf(JC_LOG_WARN, "voice: could not write %s", path);
        return JC_ERR_IO;
    }
    free(bytes);
    {
        struct jc_sb out;
        long timeout = app->config.sound.play_timeout > 0
                     ? app->config.sound.play_timeout : 120;
        int code;
        jc_sb_init(&out);
        code = jc_sound_run(app->config.sound.play_command,
                            &app->config.sound.play_args,
                            app->config.sound.play_shell, path, 0, timeout,
                            &out, &app->abort_flag);
        jc_sb_free(&out);
        remove(path);
        if (code != 0) {
            jc_logf(JC_LOG_WARN, "voice: playback command exited %d", code);
            return JC_ERR_IO;
        }
    }
    return JC_OK;
}
