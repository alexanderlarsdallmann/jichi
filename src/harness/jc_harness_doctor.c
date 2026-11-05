/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_doctor.c - `jichi-harness doctor` (M1052, H1b).
 *
 * The offline report on the drive's OWN configuration (the file the M1051
 * loader parses). Ported from the drive's live doctor
 * (docs/internal/drive/2026-10-23-harness/harness/doctor-live.sh, the
 * `jichi --config <file> doctor` spelling, "never prints the key") and from
 * jichi's own doctor's output shape (src/util/jc_doctor.c:113 the
 * `%d ok, %d warning%s, %d problem%s` summary line, :149 the
 * `{"ok":N,"warn":N,"fail":N,"exit":E,"checks":[...]}` JSON form).
 *
 * NEVERS (the design note): doctor NEVER opens a secret file and NEVER prints
 * a secret's contents. For modelKeyFile / gitTokenFile it prints the
 * reference's NAME and PATH (a path is not a secret) and decides ok/warn/fail
 * with access() only (set-but-absent -> warn; an absent reference -> fail,
 * because an absent reference is what "stopped before its request"). The
 * loader's refusals are doctor's first check -- a malformed config never
 * crashes, it names itself. A broken or missing config is a PROBLEM (exit 1),
 * never a rejected invocation (the exit-2 not-built class stays main.c's).
 */

#include "jc_harness_doctor.h"

#include "jc_harness_config.h"
#include "jc_json.h"
#include "jc_snprintf.h"
#include "jc_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* access(), F_OK */

/* The check statuses, mirroring JC_DOC_OK/WARN/FAIL (src/util/jc_doctor.c). */
#define HDOC_OK   0
#define HDOC_WARN 1
#define HDOC_FAIL 2

/* A bounded report line. The checks are the fixed seven of the design note
 * (the load verdict + six walk-checks, of which servers and tenants each
 * expand into one count line), so a fixed array replaces the vec; the count
 * stays bounded and C89 (no VLA). */
#define HDOC_MAX 16
#define HDOC_DET 1600   /* room for the detail of a servers/tenants count line */

struct hdoc_result {
    int   status;
    char  label[128];
    char  detail[HDOC_DET];
};

static void
hdoc_add(struct hdoc_result *r, int *n, int status, const char *label,
         const char *detail)
{
    int i = (*n)++;
    if (i >= HDOC_MAX) {
        i = HDOC_MAX - 1;   /* the report cannot silently exceed its bound */
    }
    r[i].status = status;
    jc_snprintf(r[i].label, sizeof r[i].label, "%s",
                (label != NULL) ? label : "");
    jc_snprintf(r[i].detail, sizeof r[i].detail, "%s",
                (detail != NULL) ? detail : "");
}

static int
hdoc_count(const struct hdoc_result *r, int n, int status)
{
    int i, k = 0;
    for (i = 0; i < n; i++) {
        if (r[i].status == status) {
            k++;
        }
    }
    return k;
}

/* Append to `dst` (capacity `cap`), bounded, from `off`; return the new
 * offset. (A second write into a just-filled buffer would have no room left
 * for the suffix, so the offset is tracked, not re-derived from strlen.) */
static jc_size
hdoc_cat(char *dst, jc_size cap, jc_size off, const char *fmt, ...)
{
    va_list ap;
    int len;
    if (off >= cap) {
        return off;
    }
    va_start(ap, fmt);
    len = jc_vsnprintf(dst + off, cap - off, fmt, ap);
    va_end(ap);
    if (len > 0) {
        off += (jc_size)len;
        if (off >= cap) {
            off = cap - 1;
        }
    }
    return off;
}

/* access(path, F_OK) == 0: the file a reference NAMES exists (doctor does not
 * open it; the loader never would have). */
static int
hdoc_exists(const char *path)
{
    return (path != NULL) ? (access(path, F_OK) == 0) : 0;
}

/* Append `s` as a JSON string body (no surrounding quotes), escaped -- the
 * json_str of src/util/jc_doctor.c:122. */
static void
hdoc_json_str(struct jc_sb *out, const char *s)
{
    const char *p;
    for (p = (s != NULL) ? s : ""; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            jc_sb_append_char(out, '\\');
            jc_sb_append_char(out, (char)c);
        } else if (c == '\n') {
            jc_sb_append(out, "\\n");
        } else if (c == '\t') {
            jc_sb_append(out, "\\t");
        } else if (c == '\r') {
            jc_sb_append(out, "\\r");
        } else if (c < 0x20) {
            jc_sb_append_fmt(out, "\\u%04x", (unsigned int)c);
        } else {
            jc_sb_append_char(out, (char)c);
        }
    }
}

static const char *
hdoc_glyph(int status)
{
    return status == HDOC_OK ? "ok" :
           status == HDOC_WARN ? "!" : "x";
}

/* The text report: one line per check (glyph, label, optional detail) and the
 * jichi summary line (jc_doctor.c:113). */
static void
hdoc_render_text(const struct hdoc_result *r, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        printf("%s %s\n", hdoc_glyph(r[i].status), r[i].label);
        if (r[i].detail[0] != '\0') {
            printf("    %s\n", r[i].detail);
        }
    }
    printf("%d ok, %d warning%s, %d problem%s\n",
           hdoc_count(r, n, HDOC_OK),
           hdoc_count(r, n, HDOC_WARN),
           hdoc_count(r, n, HDOC_WARN) == 1 ? "" : "s",
           hdoc_count(r, n, HDOC_FAIL),
           hdoc_count(r, n, HDOC_FAIL) == 1 ? "" : "s");
}

/* The JSON report: the shape at jc_doctor.c:149, which parses as one document. */
static void
hdoc_render_json(const struct hdoc_result *r, int n)
{
    static const char *const NAMES[] = { "ok", "warn", "fail" };
    int i;
    struct jc_sb sb;
    jc_sb_init(&sb);
    jc_sb_append_fmt(&sb, "{\"ok\":%d,\"warn\":%d,\"fail\":%d,\"exit\":%d,"
                          "\"checks\":[",
                     hdoc_count(r, n, HDOC_OK),
                     hdoc_count(r, n, HDOC_WARN),
                     hdoc_count(r, n, HDOC_FAIL),
                     hdoc_count(r, n, HDOC_FAIL) > 0 ? 1 : 0);
    for (i = 0; i < n; i++) {
        int s = r[i].status;
        if (s < 0 || s > 2) {
            s = 2;
        }
        if (i > 0) {
            jc_sb_append_char(&sb, ',');
        }
        jc_sb_append_fmt(&sb, "{\"status\":\"%s\",\"label\":\"", NAMES[s]);
        hdoc_json_str(&sb, r[i].label);
        jc_sb_append(&sb, "\",\"detail\":\"");
        hdoc_json_str(&sb, r[i].detail);
        jc_sb_append(&sb, "\"}");
    }
    jc_sb_append(&sb, "]}\n");
    if (sb.data != NULL) {
        printf("%s", sb.data);
    }
    jc_sb_free(&sb);
}

int
jc_harness_doctor_run(const char *config_path, int output_json,
                      struct jc_arena *a)
{
    struct hdoc_result r[HDOC_MAX];
    int n = 0;
    struct jc_harness_config cfg;
    char det[HDOC_DET];
    jc_status st;
    int i, exit_code;

    memset(r, 0, sizeof r);
    memset(&cfg, 0, sizeof cfg);

    /* Check 1: the config loads. A missing --config is a PROBLEM (exit 1),
     * not the exit-2 not-built class; a load refusal is the M1051 verdict,
     * and no further check runs (there is no struct to read). */
    if (config_path == NULL) {
        hdoc_add(r, &n, HDOC_FAIL, "no configuration",
                 "pass --config <file> to point at the harness config");
        goto render;
    }

    st = jc_harness_config_load(config_path, &cfg, a);
    if (st != JC_OK) {
        const char *msg = jc_harness_config_error(&cfg);
        hdoc_add(r, &n, HDOC_FAIL, "the config loads",
                 (msg != NULL) ? msg
                               : "the harness config could not be loaded");
        goto render;
    }

    /* Check 2: the drive's dir is set. doctor does not create it (that is
     * init, H2); a set-but-not-yet-existing dir is a warn, an absent one a fail.
     */
    if (cfg.has_dir && cfg.dir != NULL && cfg.dir[0] != '\0') {
        jc_snprintf(det, sizeof det,
                    "%s%s",
                    cfg.dir,
                    cfg.dir_source == JC_HARNESS_SRC_ENV
                        ? " (env override JICHI_HARNESS_DIR)" : "");
        if (hdoc_exists(cfg.dir)) {
            hdoc_add(r, &n, HDOC_OK, "the drive dir is set", det);
        } else {
            jc_snprintf(det, sizeof det,
                        "%s -- set, but not yet created (init will make it)",
                        cfg.dir);
            if (cfg.dir_source == JC_HARNESS_SRC_ENV) {
                hdoc_cat(det, sizeof det, (jc_size)strlen(det),
                         " (env override JICHI_HARNESS_DIR)");
            }
            hdoc_add(r, &n, HDOC_WARN, "the drive dir is set", det);
        }
    } else {
        hdoc_add(r, &n, HDOC_FAIL, "the drive dir is set",
                 "the drive dir (JICHI_HARNESS_DIR) is not set");
    }

    /* Check 3: the model key reference. A path is not a secret; the key's
     * value is never opened or printed. */
    if (cfg.has_model_key_file && cfg.model_key_file != NULL) {
        jc_snprintf(det, sizeof det, "%s (JICHI_HARNESS_MODEL_KEY_FILE)%s",
                    cfg.model_key_file,
                    cfg.model_key_file_source == JC_HARNESS_SRC_ENV
                        ? " (env override)" : "");
        if (hdoc_exists(cfg.model_key_file)) {
            hdoc_add(r, &n, HDOC_OK, "the model key file is set", det);
        } else {
            jc_snprintf(det, sizeof det,
                        "%s (JICHI_HARNESS_MODEL_KEY_FILE) does not exist yet "
                        "(the run will create it, or the reference is wrong)",
                        cfg.model_key_file);
            hdoc_add(r, &n, HDOC_WARN, "the model key file is set", det);
        }
    } else {
        hdoc_add(r, &n, HDOC_FAIL, "the model key file is set",
                 "the model key file (JICHI_HARNESS_MODEL_KEY_FILE) is not "
                 "set, so the key scan has nothing to look for");
    }

    /* Check 4: the git token reference (same shape as 3). */
    if (cfg.has_git_token_file && cfg.git_token_file != NULL) {
        jc_snprintf(det, sizeof det, "%s (JICHI_HARNESS_GIT_TOKEN_FILE)%s",
                    cfg.git_token_file,
                    cfg.git_token_file_source == JC_HARNESS_SRC_ENV
                        ? " (env override)" : "");
        if (hdoc_exists(cfg.git_token_file)) {
            hdoc_add(r, &n, HDOC_OK, "the git token file is set", det);
        } else {
            jc_snprintf(det, sizeof det,
                        "%s (JICHI_HARNESS_GIT_TOKEN_FILE) does not exist yet "
                        "(the run will create it, or the reference is wrong)",
                        cfg.git_token_file);
            hdoc_add(r, &n, HDOC_WARN, "the git token file is set", det);
        }
    } else {
        hdoc_add(r, &n, HDOC_FAIL, "the git token file is set",
                 "the git token file (JICHI_HARNESS_GIT_TOKEN_FILE) is not "
                 "set, so the push has nothing to read");
    }

    /* Check 5: the jichi config reference (a path; doctor only stats it). */
    if (cfg.has_jichi_config && cfg.jichi_config != NULL) {
        jc_snprintf(det, sizeof det, "%s (a path; doctor never parses it)",
                    cfg.jichi_config);
        if (hdoc_exists(cfg.jichi_config)) {
            hdoc_add(r, &n, HDOC_OK, "the jichi config is set", det);
        } else {
            jc_snprintf(det, sizeof det,
                        "%s does not exist yet (a run has no models, no "
                        "fences, no test command until it does)",
                        cfg.jichi_config);
            hdoc_add(r, &n, HDOC_WARN, "the jichi config is set", det);
        }
    } else {
        hdoc_add(r, &n, HDOC_FAIL, "the jichi config is set",
                 "the jichi config (jichiConfig) is not set -- a run has no "
                 "models, no fences, no test command");
    }

    /* Check 6: the servers. Each already passed the loader's refusals (a
     * credential is by reference, the kind is known, no embedded userinfo),
     * so doctor reports the count and, for each, that its credential is by
     * reference (the type, never the value). */
    {
        int n_none = 0;
        char kinds[HDOC_DET];
        jc_size off = 0;
        int first = 1;
        kinds[0] = '\0';
        for (i = 0; i < cfg.n_servers; i++) {
            const struct jc_harness_server *s = &cfg.servers[i];
            const char *ck;
            if (s->credential.type == JC_HARNESS_CRED_NONE) {
                n_none++;
            }
            switch (s->kind) {
            case JC_HARNESS_KIND_GITLAB:    ck = "gitlab"; break;
            case JC_HARNESS_KIND_GITHUB:    ck = "github"; break;
            case JC_HARNESS_KIND_GITEA:     ck = "gitea"; break;
            case JC_HARNESS_KIND_BITBUCKET: ck = "bitbucket"; break;
            case JC_HARNESS_KIND_SSH:       ck = "ssh"; break;
            case JC_HARNESS_KIND_LOCAL:     ck = "local"; break;
            default:                        ck = "?"; break;
            }
            if (first) {
                off = hdoc_cat(kinds, sizeof kinds, off, "%s", ck);
                first = 0;
            } else {
                off = hdoc_cat(kinds, sizeof kinds, off, ", %s", ck);
            }
        }
        off = hdoc_cat(det, sizeof det, 0,
                       "%d server(s) configured (kinds: %s); every credential "
                       "is by reference (the type, never the value)",
                       cfg.n_servers, (cfg.n_servers > 0) ? kinds : "-");
        if (n_none > 0) {
            hdoc_cat(det, sizeof det, off,
                     "; %d server(s) have no credential reference (a push "
                     "will fail until one is named)", n_none);
            hdoc_add(r, &n, HDOC_WARN, "the servers", det);
        } else {
            hdoc_add(r, &n, HDOC_OK, "the servers", det);
        }
    }

    /* Check 7: the tenants and the drive identity. The prefix and the lead
     * model are identifiers, not secrets; the loader already refused a
     * non-local push target with no branch prefix. */
    {
        const char *pre = (cfg.drive_branch_prefix != NULL)
                              ? cfg.drive_branch_prefix : "(none)";
        const char *end = (cfg.drive_end != NULL) ? cfg.drive_end : "(none)";
        const char *lead = (cfg.drive_lead_model != NULL)
                               ? cfg.drive_lead_model : "(none)";
        jc_snprintf(det, sizeof det,
                    "%d tenant(s); drive branch prefix %s; end %s; "
                    "lead model %s",
                    cfg.n_tenants, pre, end, lead);
        hdoc_add(r, &n, HDOC_OK, "the tenants and the drive identity", det);
    }

render:
    exit_code = hdoc_count(r, n, HDOC_FAIL) > 0 ? 1 : 0;
    if (output_json) {
        hdoc_render_json(r, n);
    } else {
        hdoc_render_text(r, n);
    }
    return exit_code;
}
