/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_cred.c - `jichi-harness credential-helper` (M1056, H3c): the C89
 * port of the drive's git-credential-from-file. See jc_harness_cred.h for the
 * contract. The helper reads ONLY its environment (git never passes a
 * --config flag on the helper's argv): JICHI_HARNESS_GIT_TOKEN_FILE (the
 * token file's PATH, not the token), JICHI_HARNESS_CRED_RULE (the user-name
 * rule) and JICHI_HARNESS_CRED_URL (the configured remote, parsed from the
 * stored config copy). On `get` it requires the stdin identity to match the
 * configured URL before it will serve a credential (the note's 01:37 item 2,
 * the race-free layer against a check-then-use race in the URL guard). The
 * token is read at the moment git asks, whitespace-trimmed (the reference's
 * tr -d ' \t\r\n'), and printed as the credential -- the helper's output is
 * the one designed channel to git. It is never on the argv, never in a log.
 */

#include "jc_harness_cred.h"

#include "jc_snprintf.h"
#include "jc_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The helper's env contract (the note's behaviour 11). The token-file name
 * matches the harness config's env setting so the push child and the config
 * loader agree on one name; the rule and the url are set by `mirror push`. */
#define ENV_TOKEN_FILE "JICHI_HARNESS_GIT_TOKEN_FILE"
#define ENV_CRED_RULE  "JICHI_HARNESS_CRED_RULE"
#define ENV_CRED_URL   "JICHI_HARNESS_CRED_URL"

/* The minimum token length after whitespace is removed (the reference's
 * "at least 12 bytes"; a shorter value is "could not be loaded"). */
#define TOKEN_MIN_LEN 12

/* Read all of stdin into `out` until EOF. Returns 0 on success, -1 on an
 * arena/grow failure. (The driver pipes a finite credential input and closes
 * the write end, so fread reaches EOF.) */
static int
read_all_stdin(struct jc_sb *out)
{
    char buf[4096];
    size_t n;
    for (;;) {
        n = fread(buf, 1, sizeof buf, stdin);
        if (n > 0) {
            if (jc_sb_append_n(out, buf, (jc_size)n) != JC_OK) {
                return -1;
            }
        }
        if (n < sizeof buf) {
            break;   /* a short read (or EOF) ends the input */
        }
    }
    return 0;
}

/* Arena-copy `s`, or NULL when s is NULL. */
static char *
dup_val(const char *s, struct jc_arena *a)
{
    size_t len;
    char *c;
    if (s == NULL) {
        return NULL;
    }
    len = strlen(s);
    c = (char *)jc_arena_calloc(a, len + 1);
    if (c == NULL) {
        return NULL;
    }
    memcpy(c, s, len);
    c[len] = '\0';
    return c;
}

/* Parse git's credential identity: key=value lines up to a blank line.
 * Fills protocol/host/path (an absent key stays NULL, an empty value is an
 * empty string). Mutates the NUL-terminated `text` in place. */
static void
parse_identity(char *text, char **proto, char **host, char **path,
               struct jc_arena *a)
{
    char *line = text;
    while (line != NULL && *line != '\0') {
        char *eol = strchr(line, '\n');
        if (eol != NULL) {
            *eol = '\0';
        }
        {
            size_t ll = strlen(line);
            if (ll > 0 && line[ll - 1] == '\r') {
                line[ll - 1] = '\0';
            }
        }
        if (line[0] == '\0') {
            break;   /* the blank line ends the credential block */
        }
        {
            char *eq = strchr(line, '=');
            if (eq != NULL) {
                *eq = '\0';
                {
                    const char *key = line;
                    const char *val = eq + 1;
                    if (strcmp(key, "protocol") == 0) {
                        *proto = dup_val(val, a);
                    } else if (strcmp(key, "host") == 0) {
                        *host = dup_val(val, a);
                    } else if (strcmp(key, "path") == 0) {
                        *path = dup_val(val, a);
                    }
                }
            }
        }
        line = (eol != NULL) ? eol + 1 : NULL;
    }
}

/* Classify the configured remote URL into the protocol, host and PATH git
 * would present in a credential identity for it (the push child carries
 * -c credential.useHttpPath=true, so the path is part of the identity).
 * A scheme form `xxx://host[/path]` yields protocol xxx, host the authority
 * up to the first '/', and path everything from that first '/' on
 * (WITH the leading '/'; EMPTY when the URL has no path part). git, under
 * -c credential.useHttpPath=true, sends the identity's path WITHOUT that
 * slash (path=drive/remote.git, never /drive/remote.git), so cred_get
 * strips the single leading '/' before the byte-exact comparison -- the
 * guard is byte-exact on GIT'S form. A bare local path (no `://`) is dialled
 * by git as protocol=file with an EMPTY host AND an EMPTY path -- that is
 * the identity the rehearsal's local stand-in presents, and the one the
 * driver's file-URL cells feed. */
static void
classify_url(const char *url, char *proto, size_t pc, char *host, size_t hc,
             char *path, size_t psc)
{
    const char *sc;
    const char *hp;
    const char *slash;
    proto[0] = '\0';
    host[0] = '\0';
    path[0] = '\0';
    if (url == NULL) {
        return;
    }
    sc = strstr(url, "://");
    if (sc != NULL) {
        size_t plen = (size_t)(sc - url);
        if (plen > 0 && plen < pc) {
            memcpy(proto, url, plen);
            proto[plen] = '\0';
        }
        hp = sc + 3;
        slash = strchr(hp, '/');
        {
            size_t hlen = slash ? (size_t)(slash - hp) : strlen(hp);
            if (hlen < hc) {
                memcpy(host, hp, hlen);
                host[hlen] = '\0';
            }
        }
        {
            size_t alen = (slash != NULL) ? strlen(slash) : 0;
            if (alen < psc) {
                if (alen > 0) {
                    memcpy(path, slash, alen);
                }
                path[alen] = '\0';
            }
        }
    } else {
        jc_snprintf(proto, pc, "%s", "file");
        /* host stays empty: a local path has no authority; path stays
         * empty: git presents no credential path for a local path. */
    }
}

/* The `get` refusal for a PATH that does not match the configured remote's
 * path: exit 1, NO password= line, the refusal on stderr NAMING the remote
 * it will not serve and the requested path that did not match it (the F2
 * distinct tooth: with -c credential.useHttpPath=true, a request for a
 * DIFFERENT project on the SAME host must not be answered with the token). */
static int
refuse_path_mismatch(const char *url, const char *path_in)
{
    fputs("jichi-harness: credential-helper: get refused: the requested "
          "path does not match the configured remote's path; the credential "
          "will not be served for ", stderr);
    if (url != NULL && url[0] != '\0') {
        fputs(url, stderr);
    } else {
        fputs("(no configured remote)", stderr);
    }
    fputs(" (the requested path is not that remote's; requested path: ", stderr);
    if (path_in != NULL) {
        fputs(path_in, stderr);
    } else {
        fputs("(none)", stderr);
    }
    fputc(')', stderr);
    fputc('\n', stderr);
    fflush(stderr);
    return 1;
}

/* The `get` refusal for an identity that does not match the configured
 * remote: exit 1, NO password= line, the refusal on stderr NAMING the remote
 * it will not serve. */
static int
refuse_mismatch(const char *url)
{
    fputs("jichi-harness: credential-helper: get refused: the requested "
          "identity does not match the configured remote; the credential "
          "will not be served for ", stderr);
    if (url != NULL && url[0] != '\0') {
        fputs(url, stderr);
    } else {
        fputs("(no configured remote)", stderr);
    }
    fputc('\n', stderr);
    fflush(stderr);
    return 1;
}

/* Read the token from `tfile` at the moment git asks: whitespace-trimmed (the
 * reference's tr -d ' \t\r\n'). Returns 0 and sets *token (arena, NUL-
 * terminated, at least TOKEN_MIN_LEN bytes) on success. Returns -1 when the
 * file is missing/unreadable, -2 when the trimmed token is shorter than the
 * minimum ("could not be loaded"). */
static int
load_token(const char *tfile, char **token, struct jc_arena *a)
{
    char *raw = NULL;
    jc_size rlen = 0;
    char *trim;
    jc_size i;
    jc_size k = 0;
    jc_status st;
    st = jc_read_file(tfile, &raw, &rlen, a);
    if (st != JC_OK) {
        return -1;
    }
    trim = (char *)jc_arena_calloc(a, rlen + 1);
    if (trim == NULL) {
        return -1;
    }
    for (i = 0; i < rlen; i++) {
        char c = raw[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            trim[k++] = c;
        }
    }
    trim[k] = '\0';
    if (k < (jc_size)TOKEN_MIN_LEN) {
        return -2;
    }
    *token = trim;
    return 0;
}

/* `get`: match the stdin identity against the configured remote, then answer
 * with the token read at this moment. The identity is checked BEFORE the
 * token is read, so a remote it is not configured for never reaches the
 * token channel. */
static int
cred_get(struct jc_arena *a)
{
    char exp_proto[128];
    char exp_host[1024];
    char exp_path[1024];
    char *proto_in = NULL;
    char *host_in = NULL;
    char *path_in = NULL;
    const char *url;
    const char *tfile;
    const char *rule;
    char *token = NULL;
    struct jc_sb in;
    int rc;

    url = getenv(ENV_CRED_URL);
    tfile = getenv(ENV_TOKEN_FILE);

    classify_url(url, exp_proto, sizeof exp_proto, exp_host, sizeof exp_host,
                 exp_path, sizeof exp_path);

    /* classify_url kept the URL's path part WITH its leading '/'
     * (/drive/remote.git). git, under -c credential.useHttpPath=true,
     * sends the identity's path WITHOUT that slash (path=drive/remote.git
     * -- measured on git 2.43.0 and 2.53.0, offline). Strip the single
     * leading '/' so the comparison below is byte-exact on GIT'S form, not
     * on the URL's. The guard is NOT loosened: a different project
     * (other/project.git) still refuses, and the file-URL case is still
     * empty==empty; a literal slash-prefixed request (no real git sends
     * that) now refuses -- it is not git's form. */
    if (exp_path[0] == '/') {
        jc_size i;
        for (i = 0; exp_path[i + 1] != '\0'; i++) {
            exp_path[i] = exp_path[i + 1];
        }
        exp_path[i] = '\0';
    }

    jc_sb_init(&in);
    if (read_all_stdin(&in) != 0) {
        fputs("jichi-harness: credential-helper: could not read the "
              "credential input\n", stderr);
        jc_sb_free(&in);
        return 1;
    }
    if (in.data == NULL || in.data[0] == '\0') {
        /* No identity at all: nothing to match, nothing to serve. */
        jc_sb_free(&in);
        return refuse_mismatch(url);
    }
    parse_identity(in.data, &proto_in, &host_in, &path_in, a);
    jc_sb_free(&in);

    /* The identity must match the configured remote BEFORE the token is read:
     * a remote the helper is not configured for never reaches the token
     * channel (the note's 01:37 item 2, the race-free layer). The PATH is
     * part of the identity (the push child carries
     * -c credential.useHttpPath=true): it is compared byte-exactly, too, in
     * GIT'S form -- the path WITHOUT the URL's leading '/' (useHttpPath sends
     * path=drive/remote.git, never /drive/remote.git; exp_path was normalised
     * to that form above) -- so a request for a different project on the same
     * host (or a file/local request carrying a non-empty path) is refused,
     * never answered. */
    if (proto_in == NULL || strcmp(proto_in, exp_proto) != 0) {
        return refuse_mismatch(url);
    }
    if (host_in == NULL) {
        host_in = "";
    }
    if (strcmp(host_in, exp_host) != 0) {
        return refuse_mismatch(url);
    }
    if (path_in == NULL) {
        path_in = "";
    }
    if (strcmp(path_in, exp_path) != 0) {
        return refuse_path_mismatch(url, path_in);
    }

    /* Matched: the token file must exist and be readable. */
    if (tfile == NULL || tfile[0] == '\0') {
        fputs("jichi-harness: credential-helper: " ENV_TOKEN_FILE
              " is not set, so the token cannot be served\n", stderr);
        fflush(stderr);
        return 1;
    }
    rc = load_token(tfile, &token, a);
    if (rc == -1) {
        fputs("jichi-harness: credential-helper: the token file could not "
              "be read, so the token cannot be served\n", stderr);
        fflush(stderr);
        return 1;
    }
    if (rc == -2) {
        fputs("jichi-harness: credential-helper: the token could not be "
              "loaded (shorter than "
              "" "12 bytes), so the credential cannot be served\n", stderr);
        fflush(stderr);
        return 1;
    }
    rule = getenv(ENV_CRED_RULE);
    if (rule == NULL || rule[0] == '\0') {
        rule = "";   /* 'none' -> an empty username, per the note */
    }
    /* The credential IS the output channel: exactly two lines, nothing else
     * on stdout. */
    fputs("username=", stdout);
    fputs(rule, stdout);
    fputc('\n', stdout);
    fputs("password=", stdout);
    fputs(token, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    return 0;
}

/* `store` and `erase`: git must NEVER be able to persist the token. Read and
 * discard the input, print nothing, exit 0. */
static int
cred_noop(void)
{
    struct jc_sb in;
    jc_sb_init(&in);
    if (read_all_stdin(&in) != 0) {
        /* A stdin read failure is not a reason to fail store/erase: nothing
         * is written either way. */
    }
    jc_sb_free(&in);
    return 0;
}

int
jc_harness_cred_helper_run(const char *verb, struct jc_arena *a)
{
    if (verb == NULL || verb[0] == '\0') {
        fputs("jichi-harness: credential-helper: needs an action "
              "(get|store|erase)\n", stderr);
        return 2;
    }
    if (strcmp(verb, "get") == 0) {
        return cred_get(a);
    }
    if (strcmp(verb, "store") == 0 || strcmp(verb, "erase") == 0) {
        return cred_noop();
    }
    fputs("jichi-harness: credential-helper: unknown action '", stderr);
    fputs(verb, stderr);
    fputs("' (want get|store|erase)\n", stderr);
    return 2;
}
