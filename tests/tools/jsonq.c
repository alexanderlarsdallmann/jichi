/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jsonq - dot-path JSON extraction for the smoke tier (tests/tools, M209).
 *
 *   jsonq [-l] [-t string|number|bool|object|array|null] [-q] PATH [FILE]
 *
 * Reads one JSON document (or, with -l, JSONL: one document per non-blank
 * line) from FILE or stdin, extracts PATH (see jq_core.h for the syntax),
 * and prints the value: strings raw (unquoted), everything else as compact
 * JSON. -t additionally requires the value's type; -q suppresses output
 * (pure predicate).
 *
 * Exit codes: 0 found (every line, under -l); 1 missing path or type
 * mismatch; 2 usage / parse error.
 *
 * Links src/json/cJSON.c -- the same parser jichi ships, so the smoke tier
 * asserts through production code (M209 decision D8). Test-only; never
 * installed.
 */

#include "jq_core.h"
#include "tt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *g_prog = "jsonq";

static void usage(void)
{
    fprintf(stderr,
            "usage: %s [-l] [-t string|number|bool|object|array|null] "
            "[-q] PATH [FILE]\n", g_prog);
}

/* Read the whole stream into a malloc'd NUL-terminated buffer. */
static char *read_all(FILE *f, size_t *outlen)
{
    size_t cap = 8192, len = 0;
    char *buf = (char *)malloc(cap);
    if (buf == NULL)
        return NULL;
    for (;;) {
        size_t n;
        if (len + 4096 + 1 > cap) {
            char *nb;
            cap *= 2;
            nb = (char *)realloc(buf, cap);
            if (nb == NULL) {
                free(buf);
                return NULL;
            }
            buf = nb;
        }
        n = fread(buf + len, 1, 4096, f);
        len += n;
        if (n < 4096) {
            if (ferror(f)) {
                free(buf);
                return NULL;
            }
            break;
        }
    }
    buf[len] = '\0';
    if (outlen != NULL)
        *outlen = len;
    return buf;
}

/* Extract from one parsed document. Returns 0 found, 1 missing/mismatch. */
static int extract(cJSON *doc, const struct jq_path *path,
                   const char *type, int quiet)
{
    cJSON *hit = jq_lookup(doc, path);
    if (hit == NULL)
        return 1;
    if (type != NULL && jq_type_matches(hit, type) != 1)
        return 1;
    if (!quiet) {
        if (cJSON_IsString(hit)) {
            printf("%s\n", hit->valuestring != NULL ? hit->valuestring : "");
        } else {
            char *s = cJSON_PrintUnformatted(hit);
            if (s == NULL)
                return 1;
            printf("%s\n", s);
            cJSON_free(s);
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    int lines = 0, quiet = 0;
    const char *type = NULL;
    const char *pathstr = NULL;
    const char *filename = NULL;
    struct jq_path path;
    char err[128];
    FILE *in = stdin;
    char *input;
    int rc = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            lines = 1;
        } else if (strcmp(argv[i], "-q") == 0) {
            quiet = 1;
        } else if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 >= argc) {
                usage();
                return TT_EXIT_USAGE;
            }
            type = argv[++i];
        } else if (pathstr == NULL) {
            pathstr = argv[i];
        } else if (filename == NULL) {
            filename = argv[i];
        } else {
            usage();
            return TT_EXIT_USAGE;
        }
    }
    if (pathstr == NULL) {
        usage();
        return TT_EXIT_USAGE;
    }
    if (type != NULL &&
        strcmp(type, "string") != 0 && strcmp(type, "number") != 0 &&
        strcmp(type, "bool") != 0 && strcmp(type, "object") != 0 &&
        strcmp(type, "array") != 0 && strcmp(type, "null") != 0) {
        fprintf(stderr, "%s: unknown type '%s'\n", g_prog, type);
        return TT_EXIT_USAGE;
    }
    if (jq_path_parse(pathstr, &path, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s: bad path '%s': %s\n", g_prog, pathstr, err);
        return TT_EXIT_USAGE;
    }
    if (filename != NULL) {
        in = fopen(filename, "rb");
        if (in == NULL) {
            fprintf(stderr, "%s: cannot open %s\n", g_prog, filename);
            jq_path_free(&path);
            return TT_EXIT_USAGE;
        }
    }
    input = read_all(in, NULL);
    if (filename != NULL)
        fclose(in);
    if (input == NULL) {
        fprintf(stderr, "%s: read failed\n", g_prog);
        jq_path_free(&path);
        return TT_EXIT_USAGE;
    }

    if (!lines) {
        cJSON *doc = cJSON_Parse(input);
        if (doc == NULL) {
            fprintf(stderr, "%s: input is not valid JSON\n", g_prog);
            rc = TT_EXIT_USAGE;
        } else {
            rc = extract(doc, &path, type, quiet);
            cJSON_Delete(doc);
        }
    } else {
        char *p = input;
        while (*p != '\0') {
            char *eol = strchr(p, '\n');
            if (eol != NULL)
                *eol = '\0';
            /* skip blank lines */
            {
                const char *s = p;
                while (*s == ' ' || *s == '\t' || *s == '\r')
                    s++;
                if (*s != '\0') {
                    cJSON *doc = cJSON_Parse(p);
                    if (doc == NULL) {
                        fprintf(stderr,
                                "%s: a line is not valid JSON: %.80s\n",
                                g_prog, p);
                        rc = TT_EXIT_USAGE;
                        break;
                    }
                    if (extract(doc, &path, type, quiet) != 0)
                        rc = (rc == 0) ? 1 : rc;
                    cJSON_Delete(doc);
                }
            }
            if (eol == NULL)
                break;
            p = eol + 1;
        }
    }

    free(input);
    jq_path_free(&path);
    return rc;
}
