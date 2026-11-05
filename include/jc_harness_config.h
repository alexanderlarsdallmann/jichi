/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_config.h - the harness configuration file and its loader (M1051,
 * H1a).
 *
 * The jichi-harness drive of 2026-10-23, batch H1 (the configuration). This is
 * a pure LIBRARY: `jc_harness_config_load` parses the drive's harness config
 * (a single JSON file) into `struct jc_harness_config`. H1b (M1052) builds
 * `jichi-harness doctor` on top of it; H2's `init` consumes it. At H1a nothing
 * else calls it yet, and it is deliberately NOT linked into `jichi` itself
 * (see the Makefile wiring: it is a prerequisite of the harness binary and the
 * test suite only, never of LIB_OBJ).
 *
 * What this file is, and is not (the design note's split):
 *   IS: the format (servers, tenants, drive), the loader's refusals, and the
 *       three JICHI_HARNESS_* env settings. Credentials and secrets are read
 *       BY REFERENCE ONLY -- the loader stores a reference (type + name/path)
 *       and NEVER a secret value, and NEVER opens a secret file (that is H3/H4,
 *       "at the moment of use"; the config is read before the secrets exist).
 *   IS NOT: `doctor`, `init`, reading modelKeyFile/gitTokenFile, git, or the
 *       jichi config's own schema (jichi's, not the harness's).
 */
#ifndef JC_HARNESS_CONFIG_H
#define JC_HARNESS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"   /* jc_status, jc_size */
#include "jc_mem.h"        /* struct jc_arena */

/* The three JICHI_HARNESS_* settings (D13). One name per secret; a secret's
 * name says it is one. These are constants in the header so M1052's lint can
 * prove they appear in docs/HARNESS.md's settings table, and so a later rename
 * is one change. */
#define JC_HARNESS_ENV_DIR              "JICHI_HARNESS_DIR"
#define JC_HARNESS_ENV_MODEL_KEY_FILE   "JICHI_HARNESS_MODEL_KEY_FILE"
#define JC_HARNESS_ENV_GIT_TOKEN_FILE   "JICHI_HARNESS_GIT_TOKEN_FILE"

/* A server's kind (D4: "GitLab ... GitHub, Gitea/Forgejo, Bitbucket, or plain
 * ssh ..."). The six kinds are the universe; an unknown kind is refused
 * (behaviour 6). */
typedef enum {
    JC_HARNESS_KIND_GITLAB    = 0,
    JC_HARNESS_KIND_GITHUB    = 1,
    JC_HARNESS_KIND_GITEA     = 2,
    JC_HARNESS_KIND_BITBUCKET = 3,
    JC_HARNESS_KIND_SSH       = 4,
    JC_HARNESS_KIND_LOCAL     = 5
} jc_harness_kind;

/* A credential, read BY REFERENCE ONLY (behaviour 2). The four reference types
 * are the universe (behaviour 4); anything else, and a credential with no
 * type, is refused. `ref` holds the NAME (env), PATH (tokenFile/sshKey) or
 * COMMAND (helper) the reference points at -- never a secret. */
typedef enum {
    JC_HARNESS_CRED_NONE       = 0,
    JC_HARNESS_CRED_TOKEN_FILE = 1,
    JC_HARNESS_CRED_ENV        = 2,
    JC_HARNESS_CRED_HELPER     = 3,
    JC_HARNESS_CRED_SSH_KEY    = 4
} jc_harness_cred_type;

/* The user-name rule a kind fixes (behaviour 5): the username the credential
 * helper answers "get" with, paired with a token. gitlab -> oauth2, github ->
 * x-access-token, gitea -> git, bitbucket -> bitbucket, ssh/local -> none (a
 * key or a path, not a token). Stored so H3's helper can apply it; H1a does
 * not print it. */
typedef enum {
    JC_HARNESS_RULE_OAUTH2        = 0,
    JC_HARNESS_RULE_X_ACCESS_TOKEN = 1,
    JC_HARNESS_RULE_GIT           = 2,
    JC_HARNESS_RULE_BITBUCKET     = 3,
    JC_HARNESS_RULE_NONE          = 4
} jc_harness_user_name_rule;

/* Which source supplied a top-level reference (behaviour 9): the env setting
 * won, or the config's own value was the default. `doctor` uses it to say "the
 * env override". */
typedef enum {
    JC_HARNESS_SRC_CONFIG = 0,
    JC_HARNESS_SRC_ENV    = 1
} jc_harness_source;

struct jc_harness_credential {
    jc_harness_cred_type type;
    char *ref;           /* name/path/command; a reference, never a secret */
};

struct jc_harness_server {
    char *name;          /* the tenant's key into it */
    jc_harness_kind kind;
    char *url;           /* the server's base URL (ssh: a host, local: a path) */
    struct jc_harness_credential credential;
};

struct jc_harness_tenant {
    char *name;
    char *home;
    char *jichi_config;  /* may inherit the top-level */
    char *workdir;
    char *mirror;
    char *push_target;   /* a server `name`; NULL when the tenant does not push */
    char *branch_prefix; /* REQUIRED for a non-local push target (behaviour 7) */
    char *server;
};

struct jc_harness_config {
    /* The counts the loader read (behaviour 1). */
    int n_servers;
    int n_tenants;
    struct jc_harness_server *servers;   /* arena; NULL when n_servers == 0 */
    struct jc_harness_tenant *tenants;   /* arena; NULL when n_tenants == 0 */

    /* Top-level references and their source (behaviours 2, 9). */
    char *dir;
    jc_harness_source dir_source;
    char *model_key_file;
    jc_harness_source model_key_file_source;
    char *git_token_file;
    jc_harness_source git_token_file_source;
    char *jichi_config;
    /* The push's kept-attempts gate (M1056, the port of mirror.sh's
     * PUSH_KEPT=1): when true, `mirror push` also pushes
     * the kept-wildcard refspec (refs/kept,star to refs/heads/<prefix>/kept);
     * the DEFAULT is false
     * (absent key = off). A CONFIG KEY ONLY -- it is NOT a JICHI_HARNESS_*
     * env setting (the doctor's settings-table lint reads exactly the three
     * JC_HARNESS_ENV_* names above out of this header). */
    int push_kept;

    /* Whether each top-level reference is set (behaviour 1's "is set"). */
    int has_dir;
    int has_model_key_file;
    int has_git_token_file;
    int has_jichi_config;

    /* The drive's identity. */
    char *drive_branch_prefix;
    char *drive_end;
    char *drive_lead_model;
    int limits_shell_fence;  /* read leniently (behaviour 8); 0 default */
    int limits_path_fence;
    int limits_disk_fence;
    /* The team push's switch (M1059 behaviour 3, SV:2037.1, the port of
     * loop.sh's `TEAM_PUSH=${ZD_TEAM_PUSH:-1}`): a lenient BOOL read under
     * `drive`, default TRUE (the reference's push-on default). When FALSE the
     * loop's save_team still does `mirror fetch` but SKIPS `mirror push` and
     * logs `team push off: save_team fetched only`. A CONFIG KEY ONLY -- no
     * JICHI_HARNESS_* env twin (pushKept's rule, the doctor's settings-table
     * lint reads exactly the three JC_HARNESS_ENV_* names). */
    int drive_team_push;

    /* The refusal's message, naming the problem (behaviours 3,4,6,7,10).
     * NULL on JC_OK. Carries no secret (a reference path is not a secret). */
    char *message;

    /* The status the load finished with (JC_OK or a refusal). */
    jc_status status;
};

/* Parse the harness config at `path` into `out`, allocating from `a`.
 *
 * Returns JC_OK on a well-formed file (counts and fields read back). Any
 * problem -- not JSON, not an object, an array in the object place, a missing
 * required top-level (`dir`), a credential that holds a secret verbatim, an
 * unknown credential type, an unknown server kind, a server with no url, a
 * tenant missing its name or (for a non-local push target) its branch prefix --
 * is a VALUE, not a crash: a non-JC_OK status, with `out->message` naming the
 * problem. The loader never opens a secret file and never runs git (behaviour
 * 11); it stores references.
 *
 * `out` is fully overwritten (zeroed first). On failure the caller should read
 * `jc_harness_config_error(out)`. `a` must outlive the config's use (the
 * strings and the arrays are arena-owned). */
jc_status jc_harness_config_load(const char *path,
                                 struct jc_harness_config *out,
                                 struct jc_arena *a);

/* The refusal's message, or NULL on a successful load. */
const char *jc_harness_config_error(const struct jc_harness_config *cfg);

/* The user-name rule a kind fixes (behaviour 5). Pure. */
jc_harness_user_name_rule
jc_harness_user_name_rule_of(jc_harness_kind kind);

#ifdef __cplusplus
}
#endif
#endif /* JC_HARNESS_CONFIG_H */
