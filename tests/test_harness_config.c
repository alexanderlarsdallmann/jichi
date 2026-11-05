/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_harness_config.c - the harness configuration loader (M1051, H1a), red-first.
 *
 * The design note docs/internal/drive/2026-10-23-harness/notes/M1051.md names the
 * format and the eleven behaviours; this file covers the note's "red-first test"
 * section, behaviour by behaviour:
 *
 *   1  a valid config (a server of each of the six kinds, tenants with prefixes,
 *      the drive) -> JC_OK, the counts and the user-name rules read back;
 *   3  a credential that holds a VALUE (a "token" string) -> refused, and the
 *      message names the field and the rule; and (behaviour 12) the message
 *      never echoes the VALUE: each value field (token/key/password) is loaded
 *      with a fresh random canary and the canary's absence from the message is
 *      checked (the teeth);
 *   4  a credential of an unknown "type" (and one with no type) -> refused,
 *      naming the type;
 *   6  a server of an unknown "kind" -> refused, naming the kind; a server with
 *      no "url" -> refused;
 *  13  a server "url" whose authority embeds a user:secret (an @ in the part
 *      after ://) -> refused, naming the rule and echoing NO part of the
 *      userinfo (a fresh random canary, checked absent from the message);
 *   7  a tenant with a non-local pushTarget and no branchPrefix -> refused,
 *      naming the tenant; the same without a name -> refused; a LOCAL push
 *      target without a prefix -> accepted (the local kind carries no remote);
 *   8  the drive's limits read leniently: "true" / 1 / "yes" are true, prose
 *      falls to the default (M519/M530);
 *   9  the env overrides JICHI_HARNESS_DIR / _MODEL_KEY_FILE / _GIT_TOKEN_FILE
 *      win over the config twins, and the struct names which source won;
 *  10  not JSON, an array in the object place, a missing dir, a missing file ->
 *      a jc_status, never a crash.
 *
 * A credential is a REFERENCE only (behaviours 2 and 11): the loader stores the
 * type and the name or path, never opens the file the reference names (that is
 * H3/H4, at the moment of use), and the tests check that the reference strings
 * come back byte-for-byte, not that anything was read from them.
 */

#include "jc_test.h"
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_snprintf.h"
#include "jc_harness_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Find a loaded server by name; NULL when absent. */
static const struct jc_harness_server *
find_server(const struct jc_harness_config *cfg, const char *name)
{
    int i;
    for (i = 0; i < cfg->n_servers; i++) {
        if (strcmp(cfg->servers[i].name, name) == 0) {
            return &cfg->servers[i];
        }
    }
    return NULL;
}

static const struct jc_harness_tenant *
find_tenant(const struct jc_harness_config *cfg, const char *name)
{
    int i;
    for (i = 0; i < cfg->n_tenants; i++) {
        if (strcmp(cfg->tenants[i].name, name) == 0) {
            return &cfg->tenants[i];
        }
    }
    return NULL;
}

/* A malloc'd copy of a string (strdup is POSIX, not C89; the copy is
 * bounded by the caller's exact allocation, so no unbounded call). */
static char *
strdup_env(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

/* One variable, restored exactly as found. `saved` is a malloc'd copy of the
 * old value, or NULL when the variable was unset (in which case this unsets
 * it again). */
static void
restore_env(const char *name, const char *saved, int had_saved)
{
    if (had_saved && saved != NULL) {
        setenv(name, saved, 1);
        free((char *)saved);
    } else {
        unsetenv(name);
    }
}

/* A valid config: one server of each of the six kinds (so behaviour 5's
 * user-name rule table is exercised end to end), the token server's credential
 * a tokenFile reference, one non-local tenant with its prefix and one local
 * tenant without (both must load), and the drive with its limits.
 *
 * The file the references name is never created: behaviour 11 says the loader
 * never opens a secret file. */
static int
write_valid_config(const char *path)
{
    FILE *f;

    f = fopen(path, "wb");
    if (f == NULL) {
        return 0;
    }
    /* Written in pieces (adjacent-literal runs kept short): a single run
     * over 509 chars is an overlength string under -Werror (CONTRIBUTING). */
    fputs("{"
        " \"dir\": \"/tmp/hc-run\","
        " \"modelKeyFile\": \"/tmp/hc-key\","
        " \"gitTokenFile\": \"/tmp/hc-token\","
        " \"jichiConfig\": \"/tmp/hc-jichi.json\","
        " \"servers\": ["
        "   {\"name\": \"gl\", \"kind\": \"gitlab\", \"url\": \"https://gl.example\","
        "    \"credential\": {\"type\": \"tokenFile\", \"path\": \"/tmp/hc-token\"}},"
        "   {\"name\": \"gh\", \"kind\": \"github\", \"url\": \"https://gh.example\","
        "    \"credential\": {\"type\": \"env\", \"name\": \"JICHI_HARNESS_GIT_TOKEN\"}},"
        , f);
    fputs("   {\"name\": \"gj\", \"kind\": \"gitea\", \"url\": \"https://gj.example\","
        "    \"credential\": {\"type\": \"helper\", \"command\": \"gh auth git-credential\"}},"
        , f);
    fputs("   {\"name\": \"bb\", \"kind\": \"bitbucket\", \"url\": \"https://bb.example\","
        "    \"credential\": {\"type\": \"sshKey\", \"path\": \"/tmp/hc-id_ed25519\"}},"
        "   {\"name\": \"ss\", \"kind\": \"ssh\", \"url\": \"host.example\","
        "    \"credential\": {\"type\": \"sshKey\", \"path\": \"/tmp/hc-id_ed25519\"}},"
        "   {\"name\": \"ld\", \"kind\": \"local\", \"url\": \"/srv/repos\","
        "    \"credential\": {\"type\": \"sshKey\", \"path\": \"/tmp/hc-id_ed25519\"}}"
        " ],"
        , f);
    fputs(" \"tenants\": ["
        "   {\"name\": \"team\", \"home\": \"/tmp/hc-team\", \"workdir\": \"/tmp/hc-team/wd\","
        "    \"mirror\": \"/tmp/hc-team/mirror.git\", \"pushTarget\": \"gl\","
        "    \"branchPrefix\": \"drive/2026-10-23-harness\", \"server\": \"gl\"},"
        "   {\"name\": \"localt\", \"home\": \"/tmp/hc-localt\", \"pushTarget\": \"ld\","
        "    \"server\": \"ld\"}"
        " ],"
        , f);
    fputs(" \"drive\": {"
        "   \"branchPrefix\": \"drive/2026-10-23-harness\","
        "   \"end\": \"when the operator stops it\","
        "   \"leadModel\": \"qwen27b\","
        "   \"limits\": {\"shellFence\": \"true\", \"pathFence\": 1, \"diskFence\": \"yes\"}"
        " }"
        "}"
        , f);
    fclose(f);
    return 1;
}

static int
write_file_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return 0;
    }
    fputs(text, f);
    fclose(f);
    return 1;
}

/* A fresh random hex canary (behaviours 12/13). srand once per process
 * (time^getpid, the suite's idiom; getpid from <unistd.h>), then hex
 * characters two nibbles at a time -- never a fixed secret-looking literal,
 * and never the same literal twice (the M729 canary was vacuous for one
 * reason; the teeth must bite a fresh value every run). */
static void
fresh_canary(char *out, int n)
{
    static int seeded = 0;
    int i;
    if (!seeded) {
        srand((unsigned int)(time(NULL) ^ (int)getpid()));
        seeded = 1;
    }
    out[0] = '\0';
    for (i = 0; i + 2 < n; i += 2) {
        jc_snprintf(out + i, 3, "%.2x", rand() % 256);
    }
    out[i] = '\0';
}

/* Behaviour 1 (and 5, and 2's read-back): a well-formed config loads, reports
 * its counts, and carries each kind's user-name rule. */
static void
test_hc_valid(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_harness_config cfg;
    char path[1024];
    jc_status st;
    const struct jc_harness_server *s;
    const struct jc_harness_tenant *t;

    JC_REQUIRE(a != NULL);
    if (a == NULL) {
        return;
    }
    JC_CHECK(jc_snprintf(path, sizeof path, "%s", jc_test_tmp("jichi_hc_valid.json"))
             < (int)sizeof path);
    JC_REQUIRE(write_valid_config(path));

    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    JC_CHECK(st == JC_OK);
    /* A successful load carries no message. jc_harness_config_error() is
     * NULL on success BY DEFINITION, so it cannot catch a reference that a
     * bug resolved into a log; only this check can. (The probe's canary
     * perturbation -- the loader appends the model key file's contents to
     * cfg->message on the success path -- must make THIS line red.) */
    JC_CHECK(cfg.message == NULL);

    JC_CHECK(cfg.n_servers == 6);
    JC_CHECK(cfg.n_tenants == 2);
    JC_CHECK(cfg.has_dir == 1);
    JC_CHECK(cfg.has_model_key_file == 1);
    JC_CHECK(cfg.has_git_token_file == 1);
    JC_CHECK(cfg.has_jichi_config == 1);
    /* The pointer read-backs go behind the status guard (the probe's
     * segfault finding): under a load-failure perturbation the struct is
     * zeroed, and strcmp/strstr on a NULL pointer kills the whole suite --
     * and a dead suite is a vacuous one. The st == JC_OK line above stays
     * OUTSIDE the guard; it is the red signal under a failure perturbation.
     * Only the dereferences come in. */
    if (st == JC_OK) {
        JC_CHECK(cfg.drive_branch_prefix != NULL);
        JC_CHECK(strcmp(cfg.drive_branch_prefix, "drive/2026-10-23-harness") == 0);
        JC_CHECK(cfg.dir != NULL && strcmp(cfg.dir, "/tmp/hc-run") == 0);
        JC_CHECK(cfg.model_key_file != NULL && strcmp(cfg.model_key_file, "/tmp/hc-key") == 0);
        JC_CHECK(cfg.git_token_file != NULL && strcmp(cfg.git_token_file, "/tmp/hc-token") == 0);
        JC_CHECK(cfg.jichi_config != NULL && strcmp(cfg.jichi_config, "/tmp/hc-jichi.json") == 0);
        /* The references come back, and nothing more: a reference is not a
         * secret, but the file it names was never opened (behaviour 11). */
        JC_CHECK(strstr(cfg.model_key_file, "/tmp/hc-key") == cfg.model_key_file);
        JC_CHECK(strstr(cfg.git_token_file, "/tmp/hc-token") == cfg.git_token_file);
    }

    /* Behaviour 5: the kind fixes the user-name rule, all six kinds. */
    s = find_server(&cfg, "gl");
    JC_REQUIRE(s != NULL);
    if (s != NULL) {
        JC_CHECK(jc_harness_user_name_rule_of(s->kind) == JC_HARNESS_RULE_OAUTH2);
        JC_CHECK(s->kind == JC_HARNESS_KIND_GITLAB);
        JC_CHECK(s->credential.type == JC_HARNESS_CRED_TOKEN_FILE);
        JC_CHECK(strcmp(s->credential.ref, "/tmp/hc-token") == 0);
    }
    s = find_server(&cfg, "gh");
    JC_REQUIRE(s != NULL);
    if (s != NULL) {
        JC_CHECK(jc_harness_user_name_rule_of(s->kind) == JC_HARNESS_RULE_X_ACCESS_TOKEN);
        JC_CHECK(s->credential.type == JC_HARNESS_CRED_ENV);
        JC_CHECK(strcmp(s->credential.ref, "JICHI_HARNESS_GIT_TOKEN") == 0);
    }
    s = find_server(&cfg, "gj");
    JC_REQUIRE(s != NULL);
    if (s != NULL) {
        JC_CHECK(jc_harness_user_name_rule_of(s->kind) == JC_HARNESS_RULE_GIT);
        JC_CHECK(s->credential.type == JC_HARNESS_CRED_HELPER);
        JC_CHECK(strcmp(s->credential.ref, "gh auth git-credential") == 0);
    }
    s = find_server(&cfg, "bb");
    JC_REQUIRE(s != NULL);
    if (s != NULL) {
        JC_CHECK(jc_harness_user_name_rule_of(s->kind) == JC_HARNESS_RULE_BITBUCKET);
        JC_CHECK(s->credential.type == JC_HARNESS_CRED_SSH_KEY);
        JC_CHECK(strcmp(s->credential.ref, "/tmp/hc-id_ed25519") == 0);
    }
    s = find_server(&cfg, "ss");
    JC_REQUIRE(s != NULL);
    if (s != NULL) {
        JC_CHECK(jc_harness_user_name_rule_of(s->kind) == JC_HARNESS_RULE_NONE);
    }
    s = find_server(&cfg, "ld");
    JC_REQUIRE(s != NULL);
    if (s != NULL) {
        JC_CHECK(jc_harness_user_name_rule_of(s->kind) == JC_HARNESS_RULE_NONE);
    }

    t = find_tenant(&cfg, "team");
    JC_REQUIRE(t != NULL);
    if (t != NULL) {
        JC_CHECK(strcmp(t->branch_prefix, "drive/2026-10-23-harness") == 0);
        JC_CHECK(strcmp(t->server, "gl") == 0);
        JC_CHECK(strcmp(t->push_target, "gl") == 0);
    }
    t = find_tenant(&cfg, "localt");
    JC_REQUIRE(t != NULL);
    if (t != NULL) {
        JC_CHECK(t->branch_prefix == NULL); /* a local push target needs no prefix */
    }

    jc_arena_free(a);
}

/* A refused load must name the problem. Check st is not JC_OK and the message
 * carries the expected substring (the note: a refusal names the field / kind /
 * tenant it is about). */
static void
expect_refusal(const char *tag, jc_status st, const char *msg, const char *needle)
{
    (void)tag;
    JC_CHECK(st != JC_OK);
    if (st == JC_OK) {
        return;
    }
    JC_REQUIRE(msg != NULL);
    if (msg == NULL) {
        return;
    }
    JC_CHECK(strstr(msg, needle) != NULL);
}

/* Write a credential-value fixture: one gitlab server whose credential object
 * carries `field` set to `value`. The value is a fresh random canary, so the
 * teeth (behaviour 12) bite a value that was never written into the source. */
static int
write_credential_value(const char *path, const char *field, const char *value)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return 0;
    }
    fputs("{\"dir\": \"/tmp/hc-run\", \"servers\": [", f);
    fputs("   {\"name\": \"gl\", \"kind\": \"gitlab\", \"url\": \"https://gl.example\","
          " \"credential\": {\"type\": \"tokenFile\", \"path\": \"/tmp/hc-token\","
          "                  \"", f);
    fputs(field, f);
    fputs("\": \"", f);
    fputs(value, f);
    fputs("\"}} ]}", f);
    fclose(f);
    return 1;
}

/* Behaviour 3 + 12: a credential that carries a secret verbatim is refused,
 * the message names the FIELD and the rule, and NEVER the VALUE. All three
 * value fields (token, key, password) are checked, each with its own fresh
 * random canary and its own absence check on the message. */
static void
test_hc_credential_value(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_harness_config cfg;
    char path[1024];
    const char *fields[3] = { "token", "key", "password" };
    int i;

    JC_REQUIRE(a != NULL);
    if (a == NULL) {
        return;
    }
    JC_CHECK(jc_snprintf(path, sizeof path, "%s", jc_test_tmp("jichi_hc_credval.json"))
             < (int)sizeof path);

    for (i = 0; i < 3; i++) {
        char canary[48];
        jc_status st;
        fresh_canary(canary, sizeof canary);
        JC_REQUIRE(write_credential_value(path, fields[i], canary));

        memset(&cfg, 0, sizeof cfg);
        st = jc_harness_config_load(path, &cfg, a);
        expect_refusal("value", st, jc_harness_config_error(&cfg), fields[i]);
        /* The rule, not just the field (the note's wording). */
        JC_REQUIRE(st != JC_OK && cfg.message != NULL);
        if (st != JC_OK && cfg.message != NULL) {
            JC_CHECK(strstr(cfg.message, "secret") != NULL);
            /* behaviour 12's teeth: the VALUE must not be echoed. The field
             * NAME above is the pinned half; this absence check is what would
             * go red if the message started formatting the value. */
            JC_CHECK(strstr(cfg.message, canary) == NULL);
        }
    }
    jc_arena_free(a);
}

/* Behaviour 13: a server url whose authority embeds a user:secret is refused,
 * the message names the rule (the CONCEPT \"user:secret\") and echoes NO part
 * of the userinfo. A fresh random canary rides in the url's secret, and its
 * absence from the message is checked. A schemeless url (ssh host, local
 * path) is still accepted -- no authority to inspect. */
static void
test_hc_server_url_userinfo(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_harness_config cfg;
    char path[1024];
    char canary[48];
    char url[128];
    jc_status st;

    JC_REQUIRE(a != NULL);
    if (a == NULL) {
        return;
    }
    JC_CHECK(jc_snprintf(path, sizeof path, "%s", jc_test_tmp("jichi_hc_userinfo.json"))
             < (int)sizeof path);

    fresh_canary(canary, sizeof canary);
    JC_CHECK(jc_snprintf(url, sizeof url,
                         "https://oauth2:%s@gl.example", canary)
             < (int)sizeof url);

    /* The userinfo fixture: a gitlab server, its url carrying the canary in
     * the authority, credential by reference. */
    {
        FILE *f;
        int ok = 0;
        f = fopen(path, "wb");
        if (f != NULL) {
            fputs("{\"dir\": \"/tmp/hc-run\", \"servers\": [", f);
            fputs("   {\"name\": \"gl\", \"kind\": \"gitlab\", \"url\": \"", f);
            fputs(url, f);
            fputs("\", \"credential\": {\"type\": \"tokenFile\", "
                  "\"path\": \"/tmp/hc-token\"}}", f);
            fputs(" ]}", f);
            fclose(f);
            ok = 1;
        }
        JC_REQUIRE(ok);
    }

    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    /* The url must be refused. */
    JC_CHECK(st != JC_OK);
    /* The message names the rule (the concept user:secret), and echoes no part
     * of the userinfo: the canary is the secret, it must not appear. */
    expect_refusal("userinfo", st, jc_harness_config_error(&cfg), "user:secret");
    JC_REQUIRE(st != JC_OK && cfg.message != NULL);
    if (st != JC_OK && cfg.message != NULL) {
        JC_CHECK(strstr(cfg.message, canary) == NULL);
        /* F5's pin: the canary is exactly 46 hex chars (fresh_canary fills
         * sizeof(canary)-1 = 47 bytes by 2, 46 nibble-pairs + terminator).
         * A PARTIAL echo of the canary (a truncated value in the message)
         * would still satisfy the absence check above; pinning the length
         * keeps that absence check honest. */
        JC_CHECK(strlen(canary) == 46);
    }

    /* A schemeless url (an ssh host) is accepted as-is: no authority part to
     * inspect, so no check, and the host rides on. */
    {
        FILE *f;
        int ok = 0;
        f = fopen(path, "wb");
        if (f != NULL) {
            fputs("{\"dir\": \"/tmp/hc-run\", \"servers\": [", f);
            fputs("   {\"name\": \"ss\", \"kind\": \"ssh\", \"url\": \"host.example\","
                  " \"credential\": {\"type\": \"sshKey\", \"path\": \"/tmp/hc-id\"}}",
                  f);
            fputs(" ]}", f);
            fclose(f);
            ok = 1;
        }
        JC_REQUIRE(ok);
    }
    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    JC_CHECK(st == JC_OK);
    /* F1: the acceptance is not just "not refused" -- the host url must come
     * back STORED. A perturbation that refuses or strips schemeless urls
     * (st != JC_OK, or a mutated url) would otherwise leave the st check
     * green. Guarded: a refusal must FAIL these checks, not dereference a
     * NULL servers table (the M729 class, the st check is the red signal). */
    if (st == JC_OK) {
        JC_CHECK(cfg.n_servers == 1);
        JC_CHECK(cfg.servers != NULL);
        if (cfg.n_servers == 1 && cfg.servers != NULL) {
            JC_CHECK(cfg.servers[0].url != NULL);
            JC_CHECK(strcmp(cfg.servers[0].url, "host.example") == 0);
        }
    }
    jc_arena_free(a);
}

/* Behaviour 4: the four reference types are the universe; anything else, and a
 * credential with no type, is refused. */
static void
test_hc_credential_unknown_type(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_harness_config cfg;
    char path[1024];
    jc_status st;

    JC_REQUIRE(a != NULL);
    if (a == NULL) {
        return;
    }
    JC_CHECK(jc_snprintf(path, sizeof path, "%s", jc_test_tmp("jichi_hc_credtype.json"))
             < (int)sizeof path);
    JC_REQUIRE(write_file_text(path,
        "{"
        " \"dir\": \"/tmp/hc-run\","
        " \"servers\": ["
        "   {\"name\": \"gl\", \"kind\": \"gitlab\", \"url\": \"https://gl.example\","
        "    \"credential\": {\"type\": \"inline\"}}"
        " ]"
        "}"));

    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    expect_refusal("unknown-type", st, jc_harness_config_error(&cfg), "inline");

    JC_REQUIRE(write_file_text(path,
        "{"
        " \"dir\": \"/tmp/hc-run\","
        " \"servers\": ["
        "   {\"name\": \"gl\", \"kind\": \"gitlab\", \"url\": \"https://gl.example\","
        "    \"credential\": {\"path\": \"/tmp/hc-token\"}}"
        " ]"
        "}"));
    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    expect_refusal("no-type", st, jc_harness_config_error(&cfg), "type");

    jc_arena_free(a);
}

/* Behaviour 6: an unknown server kind is refused, naming the kind; a server
 * with no url is refused. */
static void
test_hc_server_unknown_kind(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_harness_config cfg;
    char path[1024];
    jc_status st;

    JC_REQUIRE(a != NULL);
    if (a == NULL) {
        return;
    }
    JC_CHECK(jc_snprintf(path, sizeof path, "%s", jc_test_tmp("jichi_hc_kind.json"))
             < (int)sizeof path);
    JC_REQUIRE(write_file_text(path,
        "{"
        " \"dir\": \"/tmp/hc-run\","
        " \"servers\": ["
        "   {\"name\": \"cf\", \"kind\": \"codeflow\", \"url\": \"https://cf.example\","
        "    \"credential\": {\"type\": \"tokenFile\", \"path\": \"/tmp/hc-token\"}}"
        " ]"
        "}"));

    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    expect_refusal("unknown-kind", st, jc_harness_config_error(&cfg), "codeflow");

    JC_REQUIRE(write_file_text(path,
        "{"
        " \"dir\": \"/tmp/hc-run\","
        " \"servers\": ["
        "   {\"name\": \"gl\", \"kind\": \"gitlab\","
        "    \"credential\": {\"type\": \"tokenFile\", \"path\": \"/tmp/hc-token\"}}"
        " ]"
        "}"));
    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    expect_refusal("no-url", st, jc_harness_config_error(&cfg), "url");

    jc_arena_free(a);
}

/* Behaviour 7: a non-local push target without a branch prefix is refused,
 * naming the tenant; a tenant without a name is refused; a local push target
 * without a prefix is accepted. */
static void
test_hc_tenant_branch_prefix(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_harness_config cfg;
    char path[1024];
    jc_status st;

    JC_REQUIRE(a != NULL);
    if (a == NULL) {
        return;
    }
    JC_CHECK(jc_snprintf(path, sizeof path, "%s", jc_test_tmp("jichi_hc_prefix.json"))
             < (int)sizeof path);
    JC_REQUIRE(write_file_text(path,
        "{"
        " \"dir\": \"/tmp/hc-run\","
        " \"servers\": ["
        "   {\"name\": \"gl\", \"kind\": \"gitlab\", \"url\": \"https://gl.example\","
        "    \"credential\": {\"type\": \"tokenFile\", \"path\": \"/tmp/hc-token\"}}"
        " ],"
        " \"tenants\": ["
        "   {\"name\": \"orphan\", \"pushTarget\": \"gl\", \"server\": \"gl\"}"
        " ]"
        "}"));

    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    expect_refusal("no-prefix", st, jc_harness_config_error(&cfg), "orphan");

    JC_REQUIRE(write_file_text(path,
        "{"
        " \"dir\": \"/tmp/hc-run\","
        " \"servers\": ["
        "   {\"name\": \"gl\", \"kind\": \"gitlab\", \"url\": \"https://gl.example\","
        "    \"credential\": {\"type\": \"tokenFile\", \"path\": \"/tmp/hc-token\"}}"
        " ],"
        " \"tenants\": ["
        "   {\"pushTarget\": \"gl\", \"branchPrefix\": \"drive/x\", \"server\": \"gl\"}"
        " ]"
        "}"));
    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    expect_refusal("no-name", st, jc_harness_config_error(&cfg), "name");

    /* A local push target needs no prefix: the kind says there is no remote
     * to push to. */
    JC_REQUIRE(write_file_text(path,
        "{"
        " \"dir\": \"/tmp/hc-run\","
        " \"servers\": ["
        "   {\"name\": \"ld\", \"kind\": \"local\", \"url\": \"/srv/repos\","
        "    \"credential\": {\"type\": \"sshKey\", \"path\": \"/tmp/hc-id_ed25519\"}}"
        " ],"
        " \"tenants\": ["
        "   {\"name\": \"localt\", \"pushTarget\": \"ld\", \"server\": \"ld\"}"
        " ]"
        "}"));
    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    JC_CHECK(st == JC_OK);
    JC_CHECK(cfg.n_tenants == 1);

    jc_arena_free(a);
}

/* Behaviour 8: the drive's limits are read leniently. "true" / 1 / "yes"
 * (case-insensitive) are true; prose falls to the default, which for a fence
 * means OFF is the written word, never a silent inversion (M519). */
static void
test_hc_limits_lenient(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_harness_config cfg;
    char path[1024];
    jc_status st;

    JC_REQUIRE(a != NULL);
    if (a == NULL) {
        return;
    }
    JC_CHECK(jc_snprintf(path, sizeof path, "%s", jc_test_tmp("jichi_hc_limits.json"))
             < (int)sizeof path);
    JC_REQUIRE(write_file_text(path,
        "{"
        " \"dir\": \"/tmp/hc-run\","
        " \"drive\": {\"limits\": {\"shellFence\": \"true\", \"pathFence\": 1,"
        "                          \"diskFence\": \"YES\"}}"
        "}"));

    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    JC_CHECK(st == JC_OK);
    JC_CHECK(cfg.limits_shell_fence == 1);
    JC_CHECK(cfg.limits_path_fence == 1);
    JC_CHECK(cfg.limits_disk_fence == 1);

    /* Prose is not a boolean: it falls to the default, 0. */
    JC_REQUIRE(write_file_text(path,
        "{"
        " \"dir\": \"/tmp/hc-run\","
        " \"drive\": {\"limits\": {\"shellFence\": \"a fence we like\"}}"
        "}"));
    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    JC_CHECK(st == JC_OK);
    JC_CHECK(cfg.limits_shell_fence == 0);

    /* No limits object at all: every flag at its default. The zeros below
     * are the memset default, not a lenient parse -- when the `limits` object
     * is absent the lenient reader is never called, so do not read them as a
     * read of missing keys. */
    JC_REQUIRE(write_file_text(path,
        "{"
        " \"dir\": \"/tmp/hc-run\","
        " \"drive\": {\"branchPrefix\": \"drive/x\"}"
        "}"));
    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    JC_CHECK(st == JC_OK);
    JC_CHECK(cfg.limits_shell_fence == 0);
    JC_CHECK(cfg.limits_path_fence == 0);
    JC_CHECK(cfg.limits_disk_fence == 0);

    jc_arena_free(a);
}

/* Behaviour 9: the env override wins over the config twin, and the struct
 * says which source won, so doctor can print "the env override". */
static void
test_hc_env_override(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_harness_config cfg;
    char path[1024];
    const char *saved_dir, *saved_key, *saved_tok;
    jc_status st;

    JC_REQUIRE(a != NULL);
    if (a == NULL) {
        return;
    }
    JC_CHECK(jc_snprintf(path, sizeof path, "%s", jc_test_tmp("jichi_hc_env.json"))
             < (int)sizeof path);
    JC_REQUIRE(write_file_text(path,
        "{"
        " \"dir\": \"/tmp/hc-cfg-dir\","
        " \"modelKeyFile\": \"/tmp/hc-cfg-key\","
        " \"gitTokenFile\": \"/tmp/hc-cfg-token\""
        "}"));

    {
        const char *g;
        g = getenv(JC_HARNESS_ENV_DIR);
        saved_dir = (g != NULL) ? strdup_env(g) : NULL;
        g = getenv(JC_HARNESS_ENV_MODEL_KEY_FILE);
        saved_key = (g != NULL) ? strdup_env(g) : NULL;
        g = getenv(JC_HARNESS_ENV_GIT_TOKEN_FILE);
        saved_tok = (g != NULL) ? strdup_env(g) : NULL;
    }
    unsetenv(JC_HARNESS_ENV_DIR);
    unsetenv(JC_HARNESS_ENV_MODEL_KEY_FILE);
    unsetenv(JC_HARNESS_ENV_GIT_TOKEN_FILE);

    /* Unset: the config value is the default, and the source says config. */
    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    JC_CHECK(st == JC_OK);
    /* Same guard class as test_hc_valid (the probe's segfault finding): a
     * failed load zeroed the struct, strcmp on NULL would segfault. */
    if (st == JC_OK) {
        JC_CHECK(strcmp(cfg.dir, "/tmp/hc-cfg-dir") == 0);
        JC_CHECK(cfg.dir_source == JC_HARNESS_SRC_CONFIG);
        JC_CHECK(strcmp(cfg.model_key_file, "/tmp/hc-cfg-key") == 0);
        JC_CHECK(cfg.model_key_file_source == JC_HARNESS_SRC_CONFIG);
        JC_CHECK(strcmp(cfg.git_token_file, "/tmp/hc-cfg-token") == 0);
        JC_CHECK(cfg.git_token_file_source == JC_HARNESS_SRC_CONFIG);
    }

    /* Set: the env value wins, and the source says env. */
    setenv(JC_HARNESS_ENV_DIR, "/tmp/hc-env-dir", 1);
    setenv(JC_HARNESS_ENV_MODEL_KEY_FILE, "/tmp/hc-env-key", 1);
    setenv(JC_HARNESS_ENV_GIT_TOKEN_FILE, "/tmp/hc-env-token", 1);
    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    JC_CHECK(st == JC_OK);
    /* Same guard class as test_hc_valid (the probe's segfault finding). */
    if (st == JC_OK) {
        JC_CHECK(strcmp(cfg.dir, "/tmp/hc-env-dir") == 0);
        JC_CHECK(cfg.dir_source == JC_HARNESS_SRC_ENV);
        JC_CHECK(strcmp(cfg.model_key_file, "/tmp/hc-env-key") == 0);
        JC_CHECK(cfg.model_key_file_source == JC_HARNESS_SRC_ENV);
        JC_CHECK(strcmp(cfg.git_token_file, "/tmp/hc-env-token") == 0);
        JC_CHECK(cfg.git_token_file_source == JC_HARNESS_SRC_ENV);
    }

    restore_env(JC_HARNESS_ENV_DIR, saved_dir, saved_dir != NULL);
    restore_env(JC_HARNESS_ENV_MODEL_KEY_FILE, saved_key, saved_key != NULL);
    restore_env(JC_HARNESS_ENV_GIT_TOKEN_FILE, saved_tok, saved_tok != NULL);

    jc_arena_free(a);
}

/* Behaviour 10: a malformed config is a value, not a crash. */
static void
test_hc_malformed(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_harness_config cfg;
    char path[1024];
    jc_status st;

    JC_REQUIRE(a != NULL);
    if (a == NULL) {
        return;
    }
    JC_CHECK(jc_snprintf(path, sizeof path, "%s", jc_test_tmp("jichi_hc_bad.json"))
             < (int)sizeof path);

    /* A file that is not there at all. Remove it first: this suite writes
     * the same path later (the not-json / not-object / no-dir cases below)
     * and never deletes it, so on a warm $TMPDIR the "missing" load would
     * read the leftover instead. Ignored return: on a cold run it is
     * already absent. The needle "could not read" is unique to the
     * read-failure guard; the old "config" also matched the no-dir
     * message, which the leftover would have produced. */
    (void)remove(path);
    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    expect_refusal("missing-file", st, jc_harness_config_error(&cfg), "could not read");

    /* Not JSON. */
    JC_REQUIRE(write_file_text(path, "definitely { not json"));
    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    /* "valid JSON" is unique to the not-json guard; the bare "JSON" also
     * sat in the not-object guard's message, so that check was vacuous. */
    expect_refusal("not-json", st, jc_harness_config_error(&cfg), "valid JSON");

    /* An array in the object place. */
    JC_REQUIRE(write_file_text(path, "[1, 2, 3]"));
    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    expect_refusal("not-object", st, jc_harness_config_error(&cfg), "object");

    /* A missing required top-level: dir. */
    JC_REQUIRE(write_file_text(path, "{\"jichiConfig\": \"/tmp/hc-jichi.json\"}"));
    memset(&cfg, 0, sizeof cfg);
    st = jc_harness_config_load(path, &cfg, a);
    expect_refusal("no-dir", st, jc_harness_config_error(&cfg), "dir");

    jc_arena_free(a);
}

void test_harness_config(void)
{
    test_hc_valid();
    test_hc_credential_value();
    test_hc_credential_unknown_type();
    test_hc_server_unknown_kind();
    test_hc_server_url_userinfo();
    test_hc_tenant_branch_prefix();
    test_hc_limits_lenient();
    test_hc_env_override();
    test_hc_malformed();
}
