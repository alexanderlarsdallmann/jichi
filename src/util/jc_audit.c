/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_audit.c - always-on privileged-command audit (see jc_audit.h). */

#include "jc_audit.h"
#include "jc_version.h"
#include "jc_app.h"
#include "jc_perm.h"
#include "jc_eventlog.h"
#include "jc_platform.h"
#include "jc_snprintf.h"
#include "jc_log.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Generous cap: forensics want the whole command line, unlike the eventlog's
 * 16 KB content cap. Still redacted + UTF-8-safe via jc_eventlog_add_text. */
#define JC_AUDIT_CMD_MAX 65536

static const char *policy_name(int p)
{
    if (p == 1) return "deny";
    if (p == 2) return "allow";
    return "ask";
}

/* Shared writer: append one audit record to ~/.jichi.d/audit/<file>,
 * 0700 dir / 0600 file created-before-write, secret-scrubbed. `posture` is the
 * resolved policy name for this audit kind. Best-effort. */
static void audit_write(struct jc_app *app, const char *file,
                        const char *launcher, const char *detail,
                        const char *decision, const char *posture)
{
    char dir[1100];
    char path[1200];
    cJSON *o;
    char *line;
    FILE *f;

    jc_snprintf(dir, sizeof(dir), "%s/.jichi.d/audit", jc_home_dir());
    if (jc_mkdir_p_private(dir) != JC_OK) { /* 0700 on what we create (M488) */
        jc_logf(JC_LOG_WARN, "audit: could not create %s (attempt NOT recorded "
                "to jichi's log; the OS log still has it)", dir);
        return;
    }
    jc_snprintf(path, sizeof(path), "%s/%s", dir, file);

    o = cJSON_CreateObject();
    if (o == NULL) {
        return;
    }
    cJSON_AddNumberToObject(o, "v", 1);
    /* M290: the build, per entry. This log is append-only and shared across every
     * run in $HOME, so "which jichi decided this" is provenance a reader cannot
     * reconstruct otherwise. */
    cJSON_AddStringToObject(o, "jichi", JC_VERSION);
    cJSON_AddNumberToObject(o, "ts", jc_now_seconds());
    if (app->session_id != NULL) {
        cJSON_AddStringToObject(o, "sid", app->session_id);
    }
    cJSON_AddStringToObject(o, "launcher", launcher != NULL ? launcher : "");
    cJSON_AddStringToObject(o, "decision", decision != NULL ? decision : "");
    cJSON_AddStringToObject(o, "mode",
                            jc_agent_mode_name((enum jc_agent_mode)app->mode));
    cJSON_AddNumberToObject(o, "agent_depth", (double)app->agent_depth);
    cJSON_AddStringToObject(o, "cwd", app->cwd);
    cJSON_AddStringToObject(o, "posture", posture);
    /* Whole subject, secret-scrubbed (reuses the eventlog's redacting adder). */
    jc_eventlog_add_text(o, "command", detail != NULL ? detail : "",
                         JC_AUDIT_CMD_MAX);

    line = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (line == NULL) {
        return;
    }

    /* Append-only, created 0600, flushed per line. A brand-new file must be
     * made private BEFORE the secret-bearing line is written. */
    if (!jc_file_exists(path)) {
        f = fopen(path, "a");
        if (f != NULL) {
            fclose(f);
        }
        jc_make_private(path); /* 0600 while still empty */
    }
    f = fopen(path, "a");
    if (f == NULL) {
        jc_logf(JC_LOG_WARN, "audit: could not append to %s (attempt NOT "
                "recorded to jichi's log)", path);
        free(line);
        return;
    }
    fputs(line, f);
    fputc('\n', f);
    fclose(f);
    free(line);
}

void jc_audit_privileged(struct jc_app *app, const char *launcher,
                         const char *command, const char *decision)
{
    if (app == NULL || !app->config.privileged_audit) {
        return; /* explicitly disabled (privilegedAudit: false) */
    }
    audit_write(app, "privileged.jsonl", launcher, command, decision,
                policy_name(app->config.privileged_commands));
}

void jc_audit_kinetic(struct jc_app *app, const char *launcher,
                      const char *detail, const char *decision)
{
    if (app == NULL || !app->config.kinetic_audit) {
        return; /* explicitly disabled (kineticAudit: false) */
    }
    audit_write(app, "kinetic.jsonl", launcher, detail, decision,
                policy_name(app->config.kinetic_commands));
}
