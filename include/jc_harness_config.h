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
    /* drive.landingPolicy (M1202 D1): the word auto or hold, read as-is; the
     * DEFAULT HOLD (D2, PROVISIONAL) is decided by the gate, not the loader. */
    char *drive_landing_policy;
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

    /* The host push block (M1147, batch H7): a top-level "host" object the
     * five `push` subcommands consume (the scheduled host push, the records
     * push, the backlog, the unit files, the run-drive argv). Every field is a
     * STRING read as-is (no bools in this block); each is NULL when the key is
     * absent. The whole block is optional -- a container config names no host
     * block at all, and an absent block leaves every field NULL (no error):
     * the COMMANDS refuse a missing host block with the named reason, not the
     * loader. `host_dir` is the host's drive dir, used for the run-drive argv:
     * when `host.dir` is absent the loader reuses the config's own top-level
     * `dir` (the note's "the config's own dir field is reused when absent"),
     * so `host_dir` falls back to `dir`. */
    int host_dir_present;         /* 1 when a "host" object was in the config */
    char *host_dir;               /* host.dir, else cfg->dir when the key is absent */
    char *host_push_home;         /* host.pushHome */
    char *host_image;             /* host.image */
    char *host_page_command;      /* host.pageCommand (optional; absent = line only) */
    char *host_push_oncalendar;   /* host.pushOnCalendar (SV:1639; absent = refuse) */
    /* The gate's notifier fields (M1152, batch H8, `notify`): both optional
     * strings read as-is, NULL when absent (the host block's opt_str idiom) --
     * the loader does NOT refuse them: the command decides (a gateHome default
     * <dir>/gate, the notifyCommand absent-runs-nothing, the pageCommand
     * idiom). `host_gate_home` is the digest home (digest.md lives there);
     * `host_notify_command` is the CHANGED operator hook (it receives the
     * digest path, not a message; a failure is logged, never fatal). */
    char *host_gate_home;         /* host.gateHome (optional; absent = <dir>/gate) */
    char *host_notify_command;    /* host.notifyCommand (optional; absent = none) */
    /* The gate watcher's fields (M1153, batch H9a, `watch gate` / `watch
     * units`): both optional, opt_str as-is, NULL when absent -- the other
     * host fields' idiom. The loader does NOT refuse them: the command decides.
     * `host_gate_ondcalendar` is the watcher timer's OnCalendar (SV:1639's
     * idiom: the schedule comes from the config, never a copy; absent ->
     * `watch units` refuses naming it). `host_gate_run` is the exec the
     * transient gate unit runs (`host.gateRun`; absent -> the start refuses
     * naming it, exit 1). */
    char *host_gate_oncalendar;   /* host.gateOnCalendar (optional; absent = refuse) */
    char *host_gate_run;          /* host.gateRun (optional; absent = refuse the start) */
    /* The gate watcher's shimmable host plumbing (M1153, batch H9a, `watch
     * gate`): all optional, opt_str as-is, NULL when absent -- the other host
     * fields' idiom. The container cannot run systemctl / systemd-run, so the
     * tick's three external host commands (is-active / list-units / systemd-run)
     * and its push.git are named by the config, each falling back to a
     * system default the code names (the pageCommand idiom: absent = the
     * default, never a refusal). `host_push_repo` names the host mirror
     * (default <host.pushHome>/push.git). */
    char *host_unit_state;        /* host.unitState (optional; absent = systemctl --user is-active -q) */
    char *host_unit_list;         /* host.unitList (optional; absent = systemctl --user list-units ...) */
    char *host_unit_run;          /* host.unitRun (optional; absent = systemd-run --user --collect --nice=19) */
    char *host_push_repo;         /* host.pushRepo (optional; absent = <host.pushHome>/push.git) */
    /* The gate RUN's ledger fields (M1154, batch H9b, `gate run --stage ledger`):
     * both optional, opt_str as-is, NULL when absent -- the other host fields'
     * idiom. The loader does NOT refuse them: the command decides (the required
     * --ledger-address refusal is the ledger stage's, T2). `host_ledger_address`
     * is the operator's address the ledger accepts (author/committer and every
     * Co-Authored-By); `host_ledger_accept` is the accept list (one full 40-hex
     * commit id per line, the DD11 missing-trailer exemption). */
    char *host_ledger_address;    /* host.ledgerAddress (optional; absent = refuse, T2) */
    char *host_land_run;          /* host.landRun (M1159; optional; the land step) --
                                    absent = the green-land-refused arm */
    char *host_repin_run;         /* host.repinRun (M1159; optional; the re-pin step) --
                                    absent = the NOT re-pinned half */
    char *host_ledger_accept;     /* host.ledgerAccept (optional; absent = no accept list) */
    /* The sweep stage's shimmable surfaces (M1154 T5, wave 2): all optional,
     * opt_str as-is, NULL when absent -- the other host fields' idiom. The
     * loader does NOT refuse them: the command decides (an absent host.bin
     * means the sweep writes its own stub binary; an absent host.buildRun /
     * mutantRun / unitRunSan / unitRunO2 / containerRun means the reference's
     * system make/podman command in the clone -- the host's at the
     * switch-over). The driver's sweep cells pass the flags, which win over
     * these config values. */
    char *host_bin;               /* host.bin (optional; absent = the sweep's stub binary) */
    char *host_build_run;         /* host.buildRun (optional; absent = system make in the clone) */
    char *host_mutant_run;        /* host.mutantRun (optional; absent = system make smoke-mutant) */
    char *host_unit_run_san;      /* host.unitRunSan (optional; absent = system make SAN=1 test) */
    char *host_unit_run_o2;       /* host.unitRunO2 (optional; absent = system make OPT=-O2 test) */
    char *host_container_run;     /* host.containerRun (optional; absent = system podman) */
    /* The ci stage's shimmable surfaces (M1154 T8, wave 3): all optional,
     * opt_str as-is, NULL when absent -- the other host fields' idiom. The
     * loader does NOT refuse them: the command decides (an absent
     * host.ciRun means the reference's `make ci`; an absent host.ciLogDir
     * means <work>/ci; an absent host.ciSmokeKeepGoing means "0" -- a
     * non-numeric value is the stage's usage refusal, exit 2, the sweep
     * idiom). The driver's ci cells pass the flags, which win over these
     * config values. */
    char *host_ci_run;            /* host.ciRun (optional; absent = make ci) */
    char *host_ci_log_dir;        /* host.ciLogDir (optional; absent = <work>/ci) */
    char *host_ci_smoke_keep_going; /* host.ciSmokeKeepGoing (optional; absent = "0") */
    /* The redfirst stage's shimmable surfaces (M1157 T9, wave 3): all
     * optional, opt_str as-is, NULL when absent -- the other host fields'
     * idiom. The loader does NOT refuse them: the command decides (an absent
     * host.redfirstRun means the reference's `TMPDIR=$W/tmp sh "$d"` driver
     * form; an absent host.redfirstUnitRun means the reference's
     * `make -j8 WERROR=1 test`). The driver's redfirst cells pass the flags,
     * which win over these config values. */
    char *host_redfirst_run;      /* host.redfirstRun (optional; absent = the reference's driver form) */
    char *host_redfirst_unit_run; /* host.redfirstUnitRun (optional; absent = make -j8 WERROR=1 test) */

    /* The re-pin's shimmable surfaces (M1203, H11, wave 2): all optional,
     * opt_str as-is, NULL when absent -- the M1147 redfirst idiom. The
     * loader does NOT refuse them: the command decides (an absent field
     * means the reference's own form: the podman+image doctor commands,
     * `make -j8 WERROR=1 jichi jichi-harness`, a real git clone, and the
     * <host.dir>/pin-build | pins | bin | backups | repin.out defaults the
     * port names). The driver's flags win over these config values. */
    char *host_repin_build_dir;   /* host.repinBuildDir (optional; absent = <host.dir>/pin-build) */
    char *host_repin_clone;       /* host.repinClone (optional; absent = the reference's git clone form) */
    char *host_repin_models;      /* host.repinModels (optional; absent = the named refusal) */
    char *host_repin_doctor;      /* host.repinDoctor (optional; absent = the reference's podman+image form) */
    char *host_repin_live_doctor; /* host.repinLiveDoctor (optional; absent = the reference's podman+image form; the <m> token is substituted per model) */
    char *host_repin_build_run;   /* host.repinBuildRun (optional; absent = make -j8 WERROR=1 jichi jichi-harness) */
    char *host_repin_out;         /* host.repinOut (optional; absent = <host.dir>/repin.out) */
    char *host_repin_pins_dir;    /* host.pinsDir (optional; absent = <host.dir>/pins) */
    char *host_repin_bin_dir;     /* host.binDir (optional; absent = <host.dir>/bin) */
    char *host_repin_backup_dir;  /* host.backupDir (optional; absent = <host.dir>/backups) */

    /* The page/park block (M1213, H12, wave 1): the shimmable host commands
     * the page / page-watch / probe verbs consume. All optional, opt_str
     * as-is, NULL when absent -- the other host fields' idiom. The loader
     * does NOT refuse them: the commands decide. */
    char *host_page_unit_state;   /* host.pageUnitState (optional; absent = systemctl --user is-active <U>) */
    char *host_page_unit_failed;  /* host.pageUnitFailed (optional; absent = systemctl --user is-failed -q <U>; E4: rc 0 == FAILED) */
    char *host_page_gateway_probe;/* host.pageGatewayProbe (optional; absent = the reference's podman+curl form) */
    char *host_page_model_probe;  /* host.pageModelProbe (optional; absent = the reference's podman doctor --live form) */
    char *host_page_gateway;      /* host.pageGateway (optional; absent = https://api.hrz.uni-giessen.de/v1/models) */
    char *host_page_unit_run;     /* host.pageUnitRun (optional; absent = systemd-run --user --unit) */
    char *host_page_container_gone; /* host.pageContainerGone (optional; absent = podman container exists) */
    char *host_page_pid_list;     /* host.pagePidList (optional; absent = ps -eo pid,args grep) */
    /* The park/restart intervals (M1213, H12, T3, wave 2): all optional, opt_str
     * as-is, NULL when absent -- the other host fields' idiom. The loader
     * does NOT refuse them: the command decides. */
    char *drive_post_start;       /* drive.postStart (optional; absent = none) */
    char *drive_wait_interval;    /* drive.waitInterval (optional; absent = 1) */
    char *drive_ready_interval;   /* drive.readyInterval (optional; absent = 1) */
    char *drive_not_ready_interval; /* drive.notReadyInterval (optional; absent = 1) */
    char *drive_loop_unit;        /* drive.loopUnit (optional; absent = jhd-hardening-loop) */
    char *drive_container_name;   /* drive.containerName (optional; absent = jhd-hardening) */

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

/* The gate watcher's entry points (M1153, batch H9a): declared here because
 * the two bodies live in harness-binary-only files (src/harness/jc_hp_
 * watchgate.c / jc_hp_watchunits.c) that the dispatch (jc_harness_main.c)
 * calls, and a dedicated header is not part of this batch's file set. Each
 * takes the --config path and its subcommand's flags (NULL = absent); the
 * shimmable host-command flags fall back to the config's host block and then
 * to the reference's argv. `a` owns every string read; the caller frees it.
 * The rc is the oneshot's journal: gate 0/1 (behaviour 16), units 0/2. */
int jc_hp_watchgate_run(const char *config_path, const char *log,
                        const char *state, const char *out, int dry,
                        const char *unit_state, const char *unit_list,
                        const char *unit_run, const char *pgrep,
                        struct jc_arena *a);
int jc_hp_watchunits_run(const char *config_path, const char *out_dir,
                         struct jc_arena *a);

/* The user-name rule a kind fixes (behaviour 5). Pure. */
jc_harness_user_name_rule
jc_harness_user_name_rule_of(jc_harness_kind kind);

#ifdef __cplusplus
}
#endif
#endif /* JC_HARNESS_CONFIG_H */
