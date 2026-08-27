/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_convert_main.c - the `jichi-convert` command.
 *
 * Converts a Continue (config.yaml / legacy config.json) or opencode
 * (opencode.json / .jsonc) configuration into a jichi JSON config, and
 * optionally writes a .jichi/ asset tree (agents, commands, AGENTS.md).
 */

#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_str.h"
#include "jc_convert.h"
#include "convert_internal.h"
#include "jc_version.h"
#include "jc_snprintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0)
{
    printf("Usage: %s [input|-] [-o config.json] [--assets <dir>]\n\n", argv0);
    printf("Convert a Continue (config.yaml/config.json), opencode\n");
    printf("(opencode.json/.jsonc), or Claude Code (a project/global config\n");
    printf("tree: .claude/ + CLAUDE.md) config into a jichi config.\n\n");
    printf("Arguments:\n");
    printf("  input          Path to the source config file, a directory for a\n");
    printf("                 Claude Code tree, or '-' for stdin. If omitted,\n");
    printf("                 tries ~/.continue/, ./opencode.json, ./.claude,\n");
    printf("                 then ~/.claude/.\n");
    printf("Options:\n");
    printf("  -o <path>      Write the config to <path> (default: stdout,\n");
    printf("                 or <dir>/config.json with --assets)\n");
    printf("  --assets <dir> Also write the .jichi/ asset tree + AGENTS.md\n");
    printf("                 under <dir> (agents, commands, rules)\n");
    printf("  --emit-assets  Shorthand for --assets .\n");
    printf("  --force        Overwrite existing files (default: skip)\n");
    printf("  --dry-run      Show what would be written; write nothing\n");
    printf("  -h, --help     Show this help\n");
    printf("  -V, --version  Print version and exit\n");
}

/* Find a default input if none was given. Returns 1 if one was found. */
static int default_input(char *buf, jc_size cap)
{
    jc_snprintf(buf, cap, "%s/.continue/config.yaml", jc_home_dir());
    if (jc_file_exists(buf)) {
        return 1;
    }
    jc_snprintf(buf, cap, "%s/.continue/config.json", jc_home_dir());
    if (jc_file_exists(buf)) {
        return 1;
    }
    jc_snprintf(buf, cap, "opencode.json");
    if (jc_file_exists(buf)) {
        return 1;
    }
    /* Claude Code: a project tree in the cwd, then the global one. */
    if (jc_file_exists(".claude") || jc_file_exists("CLAUDE.md")) {
        jc_snprintf(buf, cap, ".");
        return 1;
    }
    jc_snprintf(buf, cap, "%s/.claude", jc_home_dir());
    if (jc_file_exists(buf)) {
        return 1;
    }
    return 0;
}

/* A Claude Code config tree: a dir containing .claude/ or CLAUDE.md, or a path
 * ending in ".claude". */
static int is_claude_dir(const char *p)
{
    char sub[1200];
    jc_size n;
    if (p == NULL || p[0] == '\0' || strcmp(p, "-") == 0) {
        return 0;
    }
    jc_snprintf(sub, sizeof sub, "%s/.claude", p);
    if (jc_file_exists(sub)) {
        return 1;
    }
    jc_snprintf(sub, sizeof sub, "%s/CLAUDE.md", p);
    if (jc_file_exists(sub)) {
        return 1;
    }
    n = (jc_size)strlen(p);
    return (n >= 7 && strcmp(p + n - 7, ".claude") == 0);
}

static char *read_stdin(struct jc_arena *a)
{
    struct jc_sb sb;
    char buf[4096];
    size_t n;
    char *out;
    jc_sb_init(&sb);
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
        jc_sb_append_n(&sb, buf, (jc_size)n);
    }
    out = jc_arena_strdup(a, sb.data != NULL ? sb.data : "");
    jc_sb_free(&sb);
    return out;
}

/* Ensure the parent directory of `path` exists. */
static jc_status mkparent(const char *path)
{
    char dir[1024];
    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        return JC_OK;
    }
    jc_snprintf(dir, sizeof(dir), "%.*s", (int)(slash - path), path);
    return jc_mkdir_p(dir);
}

/* Write `contents` to `path`, honoring --force/--dry-run and printing a
 * one-char status to stderr. Returns 0 on success. */
static int write_out(const char *path, const char *contents, int force,
                     int dry)
{
    int exists = jc_file_exists(path);
    char tag = exists ? (force ? '~' : '=') : '+';

    if (exists && !force) {
        fprintf(stderr, "  = %s (exists; use --force)\n", path);
        return 0;
    }
    if (dry) {
        fprintf(stderr, "  %c %s\n", tag, path);
        return 0;
    }
    if (mkparent(path) != JC_OK) {
        fprintf(stderr, "error: could not create directory for '%s'\n", path);
        return 1;
    }
    if (jc_write_file(path, contents, strlen(contents)) != JC_OK) {
        fprintf(stderr, "error: could not write '%s'\n", path);
        return 1;
    }
    fprintf(stderr, "  %c %s\n", tag, path);
    return 0;
}

/* Compute the on-disk destination for an asset relpath under `base`:
 * a nested path (agents/x.md) lands under <base>/.jichi/, a top-level file
 * (AGENTS.md) lands at <base>/. */
static void asset_dest(char *buf, jc_size cap, const char *base,
                       const char *relpath)
{
    if (strchr(relpath, '/') != NULL) {
        jc_snprintf(buf, cap, "%s/.jichi/%s", base, relpath);
    } else {
        jc_snprintf(buf, cap, "%s/%s", base, relpath);
    }
}

int main(int argc, char **argv)
{
    const char *input = NULL;
    const char *output = NULL;
    const char *assets_dir = NULL;
    int force = 0;
    int dry = 0;
    char path[1024];
    struct jc_arena *arena;
    char *text;
    struct jc_convert_result res;
    enum jc_src_format fmt;
    jc_status st;
    int i;
    int rc = 0;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0) {
            printf("jichi-convert %s\n", JC_VERSION);
            return 0;
        } else if (strcmp(a, "-o") == 0 || strcmp(a, "--output") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: -o requires an argument\n");
                return 2;
            }
            output = argv[++i];
        } else if (strcmp(a, "--assets") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --assets requires a directory\n");
                return 2;
            }
            assets_dir = argv[++i];
        } else if (strcmp(a, "--emit-assets") == 0) {
            assets_dir = ".";
        } else if (strcmp(a, "--force") == 0) {
            force = 1;
        } else if (strcmp(a, "--dry-run") == 0) {
            dry = 1;
        } else if (strcmp(a, "-") == 0) {
            input = a;
        } else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "error: unknown option '%s'\n", a);
            return 2;
        } else if (input == NULL) {
            input = a;
        }
    }

    arena = jc_arena_new(0);
    if (arena == NULL) {
        fprintf(stderr, "error: out of memory\n");
        return 1;
    }

    text = NULL;
    if (input != NULL && strcmp(input, "-") == 0) {
        text = read_stdin(arena);
        input = "(stdin)";
        fmt = jc_convert_detect(NULL, text, arena);
    } else {
        if (input == NULL) {
            if (!default_input(path, sizeof(path))) {
                fprintf(stderr, "error: no input given and no config found "
                                "under ~/.continue/, ~/.claude/, ./opencode.json"
                                ", or ./.claude\n");
                jc_arena_free(arena);
                return 1;
            }
            input = path;
        }
        if (is_claude_dir(input)) {
            fmt = JC_SRC_CLAUDE; /* a config TREE, not a single file */
        } else {
            if (jc_read_file(input, &text, NULL, arena) != JC_OK) {
                fprintf(stderr, "error: could not read '%s'\n", input);
                jc_arena_free(arena);
                return 1;
            }
            fmt = jc_convert_detect(input, text, arena);
        }
    }

    if (fmt == JC_SRC_UNKNOWN) {
        fprintf(stderr, "error: could not recognise '%s' as a Continue, "
                        "opencode, or Claude Code config\n", input);
        jc_arena_free(arena);
        return 1;
    }
    st = (fmt == JC_SRC_CLAUDE)
         ? jc_convert_run_claude(input, &res, arena)
         : jc_convert_run(text, fmt, &res, arena);
    if (st == JC_ERR_NOTFOUND) {
        fprintf(stderr, "error: no models found in '%s'\n", input);
        jc_arena_free(arena);
        return 1;
    }
    if (st != JC_OK) {
        fprintf(stderr, "error: failed to convert '%s' (%s)\n",
                input, jc_status_str(st));
        jc_arena_free(arena);
        return 1;
    }

    /* Diagnostics to stderr so stdout stays clean for piping. */
    fprintf(stderr, "Converted %d model(s) from %s; selected '%s'.\n",
            res.model_count, jc_src_format_name(fmt),
            res.model_name != NULL ? res.model_name : "(unnamed)");
    for (i = 0; i < res.warning_count; i++) {
        fprintf(stderr, "note: %s\n", res.warnings[i]);
    }

    if (assets_dir != NULL) {
        /* Config + asset tree under assets_dir. */
        const char *cfg = output;
        char cfgbuf[1024];
        if (cfg == NULL) {
            jc_snprintf(cfgbuf, sizeof(cfgbuf), "%s/config.json", assets_dir);
            cfg = cfgbuf;
        }
        rc |= write_out(cfg, res.json, force, dry);
        for (i = 0; i < res.ir->asset_count; i++) {
            char dest[1024];
            asset_dest(dest, sizeof(dest), assets_dir,
                       res.ir->assets[i]->relpath);
            rc |= write_out(dest, res.ir->assets[i]->contents, force, dry);
        }
    } else if (output != NULL) {
        if (jc_write_file(output, res.json, strlen(res.json)) != JC_OK) {
            fprintf(stderr, "error: could not write '%s'\n", output);
            rc = 1;
        } else {
            fprintf(stderr, "Wrote %s\n", output);
        }
        if (res.ir->asset_count > 0) {
            fprintf(stderr, "note: %d asset(s) not written; pass --assets "
                            "<dir> to emit the .jichi/ tree.\n",
                    res.ir->asset_count);
        }
    } else {
        printf("%s\n", res.json);
        if (res.ir->asset_count > 0) {
            fprintf(stderr, "note: %d asset(s) available; pass --assets <dir> "
                            "to write the .jichi/ tree.\n", res.ir->asset_count);
        }
    }

    free(res.json);
    jc_arena_free(arena);
    return rc;
}
