/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_rundrive.c - the `push run-drive` subcommand body (B5, M1147, batch
 * H7), re-applied from the kept work dropped at the W2 merge (refs/jichi/
 * discarded/parallel-conflict/1793987033-3; the bodies' home was b4's shared
 * jc_harness_push.c, one file per subcommand now -- this owner each). The
 * port of run-drive.sh as THE ARGV IT WOULD BUILD: dry by default (it never
 * execs podman from inside this drive's container -- the operator's rule
 * "never run what may write to learn what it does"; a host install of the
 * port would exec). It prints the exact argv as ONE shell-quoted line on
 * stdout, then exits 0. Every constant from run-drive.sh's header is in the
 * argv (the limits 16/24g/16384, the caps, the pasta network with the
 * loopback port 1234), every mount from the reference (the drive dir rw,
 * conf ro, bin ro, the mirror's stored config copy ro, the model key file
 * ro) and the reference's invariant: NO mount of the git TOKEN file. A
 * config with no usable host block refuses exit 2 naming `no host block`
 * (the one site, jc_hp_units.c's push_refuse_host, re-derived here as a
 * file-local copy -- one file per owner, no cross-file private symbols). */

#include "jc_harness_push.h"
#include "jc_harness_config.h"
#include "jc_snprintf.h"
#include "jc_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Paths hang off the config's dir / the host dir; give the joins room, the
 * same bound jc_harness_push.c's PATH_CAP uses for the mirror joins. */
#define PATH_CAP 4352

/* The `push` verb's host-block refusal (the file-local twin of
 * jc_hp_units.c's push_refuse_host -- same line, same exit; one file per
 * owner, so no shared private symbol between the two subcommand files). */
static int
push_refuse_host(const struct jc_harness_config *cfg)
{
    if (cfg->host_dir_present == 0 ||
        cfg->host_dir == NULL || cfg->host_dir[0] == '\0') {
        fprintf(stderr, "jichi-harness: push: no host block in the config "
                "-- the host push commands are host-side by construction; "
                "a config that names no host dir must not guess one (the "
                "host object: pushHome, image, dir)\n");
        return 2;
    }
    return 0;
}

/* The `push` verb's stderr refusal line (b4's refuse, the push verb's
 * prefix -- NOT mirror's; M1056's static refuse is file-local to
 * jc_harness_push.c). */
static void
push_refuse_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "jichi-harness: push: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    fflush(stderr);
}

/* The last path component of `p` (the drive's short name for the podman
 * --name: the reference's jhd- <dir> basename, this drive's jhp-). Points
 * into `p`; never NULL. */
static const char *
units_base_name(const char *p)
{
    const char *slash;
    const char *q;
    if (p == NULL || p[0] == '\0') {
        return p != NULL ? p : "";
    }
    slash = strrchr(p, '/');
    q = (slash != NULL) ? slash + 1 : p;
    if (*q == '\0') {
        for (q--; q > p && q[-1] != '/'; q--) {
        }
        if (q > p) {
            return q;
        }
    }
    return q;
}

/* The podman run argv the host would build for this drive (B5), as ONE
 * shell-quoted line: the unquoted prefix `podman run` followed by every
 * token single-quoted. Every value from config (host_dir, host_image,
 * model_key_file); the limits and fences from the reference's constants
 * (16/24g/16384, cap-drop, no-new-privileges, pasta 1234); NO git-token
 * mount (the reference's invariant). The env is the reference's four
 * `-e NAME=value` pairs (HOME, TMPDIR, TZ, PATH) ALONE -- no bare value
 * after any pair (def-382: the stray bare tokens made podman read the first
 * directory path as the image name). The invocation ends `drive --tenant
 * lead '< /dev/null>'`. Returns 1 on success, 0 on a refusal (named) or an
 * overflow. */
static int
build_rundrive_line(char *line, size_t cap,
                    const struct jc_harness_config *cfg, const char *host_dir)
{
    char name[PATH_CAP + 16];
    char mdir[PATH_CAP + 16];
    char mconf[PATH_CAP + 16];
    char mbin[PATH_CAP + 16];
    char mcopy[PATH_CAP + 16];
    char mkey[PATH_CAP + 16];
    char home[PATH_CAP + 16];
    char homekv[PATH_CAP + 16];
    char pathkv[PATH_CAP + 16];
    char image[PATH_CAP + 16];
    char binh[PATH_CAP + 16];
    char cfgjson[PATH_CAP + 16];
    const char *toks[56];
    int nt = 0;
    size_t off;
    int w;
    int i;
    const char *s;

    if (cfg->host_image == NULL || cfg->host_image[0] == '\0') {
        push_refuse_line("the run-drive argv needs the podman image "
            "(host.image); the config's host block names none");
        return 0;
    }
    s = cfg->model_key_file;
    if (s == NULL || s[0] == '\0') {
        push_refuse_line("the run-drive argv needs the model key file "
            "(modelKeyFile) to mount read-only; the config names none");
        return 0;
    }
    jc_snprintf(name, sizeof name, "jhp-%s", units_base_name(host_dir));
    if (jc_snprintf(mdir, sizeof mdir, "%s:%s:rw", host_dir, host_dir)
            >= (int)sizeof mdir ||
        jc_snprintf(mconf, sizeof mconf, "%s/conf:%s/conf:ro", host_dir,
                    host_dir) >= (int)sizeof mconf ||
        jc_snprintf(mbin, sizeof mbin, "%s/bin:%s/bin:ro", host_dir,
                    host_dir) >= (int)sizeof mbin ||
        jc_snprintf(mcopy, sizeof mcopy,
                    "%s/mirror.git-config.harness-copy:"
                    "%s/mirror.git-config.harness-copy:ro",
                    host_dir, host_dir) >= (int)sizeof mcopy ||
        jc_snprintf(mkey, sizeof mkey, "%s:%s:ro", s, s)
            >= (int)sizeof mkey ||
        jc_snprintf(home, sizeof home, "%s/home", host_dir)
            >= (int)sizeof home ||
        jc_snprintf(homekv, sizeof homekv, "HOME=%s", home)
            >= (int)sizeof homekv ||
        jc_snprintf(pathkv, sizeof pathkv,
                    "PATH=%s/bin:/usr/local/bin:/usr/bin:/bin", host_dir)
            >= (int)sizeof pathkv ||
        jc_snprintf(image, sizeof image, "%s", cfg->host_image)
            >= (int)sizeof image ||
        jc_snprintf(binh, sizeof binh, "%s/bin/jichi-harness", host_dir)
            >= (int)sizeof binh ||
        jc_snprintf(cfgjson, sizeof cfgjson, "%s/conf/harness.json",
                    host_dir) >= (int)sizeof cfgjson) {
        push_refuse_line("the host dir path is too long to build the "
            "run-drive argv");
        return 0;
    }
    /* The flat token order (the reference's argv; see the driver's cells). */
    toks[nt++] = "--rm";
    toks[nt++] = "--init";
    toks[nt++] = "--name";
    toks[nt++] = name;
    toks[nt++] = "--userns=keep-id";
    toks[nt++] = "--security-opt=no-new-privileges";
    toks[nt++] = "--cap-drop=all";
    toks[nt++] = "--network=pasta:-T,1234";
    toks[nt++] = "--cpus";
    toks[nt++] = "16";
    toks[nt++] = "--memory";
    toks[nt++] = "24g";
    toks[nt++] = "--pids-limit";
    toks[nt++] = "16384";
    toks[nt++] = "-v";
    toks[nt++] = mdir;
    toks[nt++] = "-v";
    toks[nt++] = mconf;
    toks[nt++] = "-v";
    toks[nt++] = mbin;
    toks[nt++] = "-v";
    toks[nt++] = mcopy;
    toks[nt++] = "-v";
    toks[nt++] = mkey;
    toks[nt++] = "-e";
    toks[nt++] = homekv;
    toks[nt++] = "-e";
    toks[nt++] = "TMPDIR=/tmp";
    toks[nt++] = "-e";
    toks[nt++] = "TZ=Europe/Berlin";
    toks[nt++] = "-e";
    toks[nt++] = pathkv;
    toks[nt++] = "-w";
    toks[nt++] = host_dir;
    toks[nt++] = image;
    toks[nt++] = binh;
    toks[nt++] = "--config";
    toks[nt++] = cfgjson;
    toks[nt++] = "drive";
    toks[nt++] = "--tenant";
    toks[nt++] = "lead";
    toks[nt++] = "< /dev/null";

    w = jc_snprintf(line, cap, "podman run");
    if (w < 0 || (size_t)w >= cap) {
        push_refuse_line("the run-drive argv line buffer is too small");
        return 0;
    }
    off = (size_t)w;
    for (i = 0; i < nt; i++) {
        int t = jc_snprintf(line + off, cap - off, " '%s'", toks[i]);
        if (t < 0 || (size_t)t >= cap - off) {
            push_refuse_line("the run-drive argv line buffer is too small");
            return 0;
        }
        off += (size_t)t;
    }
    line[off] = '\0';
    return 1;
}

/* `push run-drive`: the podman argv it would build, PRINTED not run (B5).
 * Dry by default: it never execs podman (the operator's rule "never run what
 * may write to learn what it does"); a host install of the port would exec.
 * It prints the exact argv as ONE shell-quoted line on stdout, then exits 0.
 * `positional` is unused for run-drive (it takes no positional). A config
 * with no usable host block refuses exit 2 naming `no host block` (the one
 * site, push_refuse_host). */
int
jc_harness_push_rundrive_run(const char *config_path, const char *positional,
                             struct jc_arena *a, const char **rest, int n_rest)
{
    struct jc_harness_config cfg;
    char line[PATH_CAP * 4 + 4096];
    jc_status st;
    int rc;
    (void)positional;
    (void)rest;
    (void)n_rest;

    if (config_path == NULL) {
        push_refuse_line("needs --config <file> (the harness configuration)");
        return 2;
    }
    st = jc_harness_config_load(config_path, &cfg, a);
    if (st != JC_OK) {
        const char *msg = jc_harness_config_error(&cfg);
        push_refuse_line("%s",
            (msg != NULL) ? msg : "could not read the harness config");
        return 2;
    }
    rc = push_refuse_host(&cfg);
    if (rc != 0) {
        return rc;
    }
    if (!build_rundrive_line(line, sizeof line, &cfg, cfg.host_dir)) {
        return 2;
    }
    fputs(line, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    return 0;
}
