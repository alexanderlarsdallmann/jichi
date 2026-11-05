/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_main.c - the `jichi-harness` command, jichi's third C89 program
 * (M1049, the jichi-harness drive of 2026-10-23, batch H0a).
 *
 * At H0 the program PRINTS and REFUSES: it has a standalone main() (mirroring
 * src/convert/jc_convert_main.c) and a command dispatch over the five commands
 * the plan names (init, mirror, run, drive, doctor). None of them is built yet,
 * so every one of them answers a refusal on stderr -- exit 2, never a silent
 * success. It links $(LIB_OBJ) and thereby reuses src/util, src/json and
 * src/platform: the spawn primitive, the environment scrub, the redactor and
 * the per-platform memory protection, without re-declaring any of them (D4's
 * reason for making the harness C89).
 *
 * WHAT IT DELIBERATELY DOES NOT DO at H0 (the note's "What stays"): it takes no
 * --config argument (H1), never reads a secret, never runs git (H3) and never
 * spawns (H4). A behaviour that arrives early is a behaviour whose refusal list
 * the later notes must rewrite. The usage text names the five commands and
 * marks each BUILT with its milestone (doctor M1052, init M1053, mirror
 * M1055/M1056, run M1057/M1058, drive M1059); a bare form (no required flag)
 * still keeps the 'not built yet' refusal (M1059-b, SV:0317.1). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jc_version.h"
#include "jc_license.h"
#include "jc_buildrev.h"
#include "jc_mem.h"            /* struct jc_arena */
#include "jc_harness_doctor.h" /* M1052 (H1b): the doctor command */
#include "jc_harness_init.h"   /* M1053 (H2): the init command */
#include "jc_harness_mirror.h" /* M1055 (H3b): the mirror fetch/head commands */
#include "jc_harness_push.h"   /* M1056 (H3c): the mirror push command */
#include "jc_harness_cred.h"   /* M1056 (H3c): the credential-helper subcommand */
#include "jc_harness_run.h"    /* M1057 (H4): the run command */
#include "jc_harness_drive.h"  /* M1059 (H5): the drive (outer loop) command */

/* The five commands the plan names for this program, in the order its
 * section 3 table introduces them: init, doctor, mirror, run, drive. The
 * dispatch below knows all five (so it refuses each by name) and nothing else
 * (so an unknown command is a usage error, not a not-built refusal). */

static void usage_stream(const char *argv0, FILE *f)
{
    fprintf(f, "Usage: %s [--config <file>] <command> [options]\n\n", argv0);
    fprintf(f, "jichi-harness drives a jichi run: it lays out the drive, fetches the\n");
    fprintf(f, "team's repositories, starts runs and carries their records, so jichi\n");
    fprintf(f, "(or any agent) can drive itself. Each command below is wired into the\n");
    fprintf(f, "build; the ones not built yet refuse with exit status 2, never a\n");
    fprintf(f, "silent success.\n\n");
    fprintf(f, "Commands (see docs/HARNESS.md):\n");
    fprintf(f, "  init        Lay out a drive or rehearsal directory (BUILT, M1053):\n");
    fprintf(f, "              run init --config <f> <drive|rehearsal> DIR [--from <repo>]\n");
    fprintf(f, "              [--jichi-bin <p>]; a bare 'init' (no mode) keeps the\n");
    fprintf(f, "              'not built yet' refusal, exit 2\n");
    fprintf(f, "  doctor      Report on the drive's configuration (BUILT,\n");
    fprintf(f, "              M1052; needs --config)\n");
    fprintf(f, "  mirror      Fetch the team's branches (and the kept\n");
    fprintf(f, "              attempts) into a mirror only the harness\n");
    fprintf(f, "              writes, after its five integrity checks\n");
    fprintf(f, "              (BUILT, M1055): run mirror fetch --config <f>\n");
    fprintf(f, "              <mirror>; push the harness's refset to the\n");
    fprintf(f, "              remote after the key scan and size check\n");
    fprintf(f, "              (BUILT, M1056): run mirror push --config <f>\n");
    fprintf(f, "              <mirror>; a bare 'mirror' keeps the 'not built\n");
    fprintf(f, "              yet' refusal.\n");
    fprintf(f, "  credential-helper  A git credential helper spawned by the\n");
    fprintf(f, "              push (get|store|erase, BUILT, M1056); it is a\n");
    fprintf(f, "              subcommand git runs, not a command the operator\n");
    fprintf(f, "              types -- no --config on its argv\n");
    fprintf(f, "  run         Start one lead run and carry its handoff and inbox\n");
    fprintf(f, "              (BUILT, M1057/M1058): one lead run in Mode A, the\n");
    fprintf(f, "              C89 port of run-lead.sh; run --config <f>\n");
    fprintf(f, "              --tenant <name> --run <N>; a bare 'run' (no --tenant\n");
    fprintf(f, "              and no --run) keeps the 'not built yet' refusal,\n");
    fprintf(f, "              exit 2\n");
    fprintf(f, "  drive       The outer loop until the drive's end: fetch, run,\n");
    fprintf(f, "              push (BUILT, M1059): the C89 port of the drive's\n");
    fprintf(f, "              loop.sh -- fetch, run, save and snapshot; a bare\n");
    fprintf(f, "              'drive' (no --tenant and no --config) keeps the\n");
    fprintf(f, "              'not built yet' refusal, exit 2\n");
    fprintf(f, "Options:\n");
    fprintf(f, "  --config <file>  The harness configuration (global, before the command)\n");
    fprintf(f, "  --from <repo>    init: the source repo (the rehearsal's local stand-in)\n");
    fprintf(f, "  --jichi-bin <p>  init: the jichi binary to pin (default: next to the harness)\n");
    fprintf(f, "  --output json    Emit the report as JSON (doctor)\n");
    fprintf(f, "  -h, --help       Show this help\n");
    fprintf(f, "  -V, --version    Print version and exit\n");
}

static void usage(const char *argv0)
{
    usage_stream(argv0, stdout);
}

/* Print the version the way jichi's own `print_version` does (src/main.c):
 * the version from the single source of truth, the copyright/author/licence
 * lines from include/jc_license.h, and the build stamp from jc_build_rev() --
 * printed only when it is non-NULL, so a tarball build says nothing it cannot
 * back. Which build, not just which version: a binary can be 50 milestones
 * behind the tree and still print the same version string (M495). */
static void print_version(void)
{
    const char *rev = jc_build_rev();
    printf("jichi-harness %s\n", JC_VERSION);
    printf("%s\n", JC_COPYRIGHT);
    printf("%s\n", JC_AUTHOR);
    printf("licence: %s\n", JC_LICENSE_SPDX);
    if (rev != NULL) {
        printf("build: %s\n", rev);
    }
#ifdef JC_PUSH_BOUNDARY
    {
        int bt, be;
        /* def-350: as FAULT=1 (M198) and NOFORK=1 (M769) in src/main.c, a
         * measuring build says so. This one lowers PUSH_MAX_TIPS/PUSH_MAX_
         * EXCL so list_pushed lands exactly at its argv bound; the line is
         * also the driver's precondition gate (it must not pass on a 24/48
         * build). It reports the VALUES the build carries, not just the
         * marker: a hand-built -DJC_PUSH_BOUNDARY at 24/48 would print the
         * marker and pass the driver's gate vacuously otherwise. */
        jc_harness_push_caps(&bt, &be);
        printf("build: PUSH_BOUNDARY=1 (PUSH_MAX_TIPS=%d PUSH_MAX_EXCL=%d "
               "lowered for the argv-boundary test; see notes/def-350.md)\n",
               bt, be);
    }
#endif
}

/* A command the plan names but that is not built yet. Refusal on stderr, exit
 * 2, naming the command: `jichi-harness: init: not built yet (see
 * docs/HARNESS.md)`. Exit 2 is the refusal class jichi-convert uses for usage
 * errors and jichi uses for its refusals -- the class that says "this is a
 * usage problem, not a program that did nothing and pretended to." */
static int not_built(const char *cmd)
{
    fprintf(stderr, "jichi-harness: %s: not built yet (see docs/HARNESS.md)\n", cmd);
    return 2;
}

int main(int argc, char **argv)
{
    int i;
    const char *not_built_cmd = NULL; /* set once a command is seen */
    const char *config_path = NULL;   /* --config <file> (global, before the command) */
    int output_json = 0;              /* doctor --output json (after the command) */
    /* init's own flags (after the command): --config, --from, --jichi-bin,
     * and its two positionals, mode then dir. */
    const char *init_mode = NULL;
    const char *init_dir = NULL;
    const char *init_from = NULL;
    const char *init_jichi_bin = NULL;
    /* mirror's own args: the verb (fetch|head|push; a bare `mirror` keeps the
     * not-built refusal) and the mirror directory. */
    const char *mirror_sub = NULL;
    const char *mirror_dir = NULL;
    /* credential-helper's own arg: the action (get|store|erase). It is a
     * top-level verb git spawns, NOT under mirror (like init). */
    const char *cred_action = NULL;
    /* run's own flags (M1057, H4): --tenant <name> and --run <N>; --config is
     * accepted like init's (after the command, not only before it). */
    const char *run_tenant = NULL;
    const char *run_num = NULL;
    /* drive's own flags (M1059, H5): --tenant <name>, --once, --tick <s>;
     * --config is accepted like the others (after the command, not only
     * before it). */
    const char *drive_tenant = NULL;
    int drive_once = 0;
    int drive_tick = -1;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        /* GLOBAL options, before the command (the same place jichi takes its
         * --config, per tests/smoke/doctor.sh: `jichi --config X doctor`). */
        if (not_built_cmd == NULL && (strcmp(a, "-h") == 0 ||
                                      strcmp(a, "--help") == 0)) {
            usage(argv[0]);
            return 0;
        }
        if (not_built_cmd == NULL && (strcmp(a, "-V") == 0 ||
                                      strcmp(a, "--version") == 0)) {
            print_version();
            return 0;
        }
        if (not_built_cmd == NULL && strcmp(a, "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "jichi-harness: --config needs a value <file>\n");
                usage(argv[0]);
                return 2;
            }
            i++;
            config_path = argv[i];
            continue;
        }

        /* A command: parse its own flags from here on. */
        if (not_built_cmd == NULL) {
            if (strcmp(a, "init") == 0) {
                not_built_cmd = "init";
                continue;
            } else if (strcmp(a, "doctor") == 0) {
                not_built_cmd = "doctor";
                continue;
            } else if (strcmp(a, "mirror") == 0) {
                not_built_cmd = "mirror";
                continue;
            } else if (strcmp(a, "credential-helper") == 0) {
                /* A top-level verb git spawns (not under mirror, like init):
                 * it takes a single positional, the action. It is BUILT
                 * (M1056), so a bare `credential-helper` (no action) is a
                 * usage refusal, not the not-built class. */
                not_built_cmd = "credential-helper";
                continue;
            } else if (strcmp(a, "run") == 0) {
                not_built_cmd = "run";
                continue;
            } else if (strcmp(a, "drive") == 0) {
                not_built_cmd = "drive";
                continue;
            } else if (a[0] == '-' && a[1] != '\0') {
                /* a global option with no value, or an unknown global option */
                fprintf(stderr, "jichi-harness: unknown option '%s'\n", a);
                usage(argv[0]);
                return 2;
            } else {
                fprintf(stderr, "jichi-harness: unknown command '%s'\n", a);
                usage(argv[0]);
                return 2;
            }
        }

        /* After the command. doctor's flags; the not-built commands take none. */
        if (strcmp(not_built_cmd, "doctor") == 0) {
            if (strcmp(a, "--output") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "jichi-harness: --output needs a value "
                            "(json)\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                if (strcmp(argv[i], "json") != 0) {
                    fprintf(stderr,
                            "jichi-harness: doctor --output only supports "
                            "'json' (got '%s')\n", argv[i]);
                    return 2;
                }
                output_json = 1;
                continue;
            }
        }
        /* After the command. init's own flags: --config (accepted here too,
         * not only before the command), --from, --jichi-bin. */
        if (strcmp(not_built_cmd, "init") == 0) {
            if (strcmp(a, "--config") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "jichi-harness: --config needs a value <file>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                config_path = argv[i];
                continue;
            }
            if (strcmp(a, "--from") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "jichi-harness: --from needs a value <repo>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                init_from = argv[i];
                continue;
            }
            if (strcmp(a, "--jichi-bin") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --jichi-bin needs a "
                                    "value <path>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                init_jichi_bin = argv[i];
                continue;
            }
        }
        /* After the command. mirror's own args: the verb (fetch|head) and
         * the mirror directory; --config is accepted here too (not only
         * before the command), like init's. */
        if (strcmp(not_built_cmd, "mirror") == 0) {
            if (strcmp(a, "--config") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "jichi-harness: --config needs a value <file>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                config_path = argv[i];
                continue;
            }
            /* Any other flag after `mirror` is not recognised (the generic
             * unknown-option path below handles it). */
        }
        /* After the command. run's own flags: --config (accepted here too,
         * like init's/mirror's), --tenant <name>, --run <N>. */
        if (strcmp(not_built_cmd, "run") == 0) {
            if (strcmp(a, "--config") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "jichi-harness: --config needs a value <file>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                config_path = argv[i];
                continue;
            }
            if (strcmp(a, "--tenant") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "jichi-harness: --tenant needs a value <name>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                run_tenant = argv[i];
                continue;
            }
            if (strcmp(a, "--run") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --run needs a value "
                                    "<N>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                run_num = argv[i];
                continue;
            }
            /* Any other flag after `run` is not recognised. */
        }
        /* After the command. drive's own flags (M1059, H5): --config (accepted
         * here too, like init's/mirror's/run's), --tenant <name>, --once,
         * --tick <s>. */
        if (strcmp(not_built_cmd, "drive") == 0) {
            if (strcmp(a, "--config") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "jichi-harness: --config needs a value <file>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                config_path = argv[i];
                continue;
            }
            if (strcmp(a, "--tenant") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "jichi-harness: --tenant needs a value <name>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                drive_tenant = argv[i];
                continue;
            }
            if (strcmp(a, "--once") == 0) {
                drive_once = 1;
                continue;
            }
            if (strcmp(a, "--tick") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "jichi-harness: --tick needs a value <s>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                drive_tick = atoi(argv[i]);
                if (drive_tick < 0) {
                    fprintf(stderr,
                            "jichi-harness: --tick must be a non-negative "
                            "number of seconds (got '%s')\n", argv[i]);
                    usage(argv[0]);
                    return 2;
                }
                continue;
            }
            /* Any other flag after `drive` is not recognised. */
        }
        /* Any other flag after a command is not recognised. */
        if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "jichi-harness: unknown option '%s'\n", a);
            usage(argv[0]);
            return 2;
        }
        /* A bare word after the command: init's two positionals, mode then
         * dir; mirror's two positionals, verb (fetch|head) then dir; for the
         * other commands, not recognised. */
        if (strcmp(not_built_cmd, "init") == 0) {
            if (init_mode == NULL) {
                init_mode = a;
                continue;
            }
            if (init_dir == NULL) {
                init_dir = a;
                continue;
            }
        }
        if (strcmp(not_built_cmd, "mirror") == 0) {
            if (mirror_sub == NULL) {
                if (strcmp(a, "fetch") != 0 && strcmp(a, "head") != 0 &&
                    strcmp(a, "push") != 0) {
                    fprintf(stderr,
                            "jichi-harness: mirror: the verb must be 'fetch', "
                            "'head' or 'push' (got '%s')\n", a);
                    usage(argv[0]);
                    return 2;
                }
                mirror_sub = a;
                continue;
            }
            if (mirror_dir == NULL) {
                mirror_dir = a;
                continue;
            }
        }
        if (strcmp(not_built_cmd, "credential-helper") == 0) {
            /* A single positional: the action (get|store|erase). No --config
             * on the helper's argv (git does not pass one). */
            if (cred_action == NULL) {
                cred_action = a;
                continue;
            }
        }
        fprintf(stderr, "jichi-harness: unexpected argument '%s' after "
                        "command '%s'\n", a, not_built_cmd);
        usage(argv[0]);
        return 2;
    }

    if (not_built_cmd == NULL) {
        /* No command: print usage, exit 2. A subcommand-only tool is a usage
         * error when nothing is given, not a silent no-op that exits 0 (the
         * class the whole drive exists to stop). */
        fprintf(stderr, "jichi-harness: no command given\n");
        usage(argv[0]);
        return 2;
    }

    /* doctor is the one built command: run the offline report. A broken or
     * missing --config is a PROBLEM (the doctor's report, exit 1), never the
     * exit-2 not-built class. If --config was given but the file is unreadable,
     * doctor still runs -- the loader's refusal is the report, not a usage
     * error. */
    if (strcmp(not_built_cmd, "doctor") == 0) {
        struct jc_arena *a = jc_arena_new(0);
        int rc;
        if (a == NULL) {
            fprintf(stderr, "jichi-harness: doctor: out of memory\n");
            return 1;
        }
        rc = jc_harness_doctor_run(config_path, output_json, a);
        jc_arena_free(a);
        return rc;
    }

    /* init is built (M1053, H2): lay out the drive or rehearsal directory.
     * A fully-formed `init --config <f> <mode> DIR ...` runs. A BARE `init`
     * (no mode) keeps the H0 not-built refusal -- the sibling driver
     * tests/smoke/harness_third_program.sh (checks 4-7) runs bare `init` and
     * asserts that refusal, so it must stay; this file is not one this
     * milestone may edit. A mode given but no DIR is a usage refusal (exit 2,
     * nothing written). The rest of the refusals (an unknown mode, a missing
     * --from, a missing jichi-bin, a drive with no model-key reference) are
     * init's own, and all happen before any part is written. */
    if (strcmp(not_built_cmd, "init") == 0) {
        if (init_mode == NULL) {
            return not_built("init");
        }
        if (init_dir == NULL) {
            fprintf(stderr, "jichi-harness: init: needs a DIR after the mode "
                            "(drive|rehearsal)\n");
            usage(argv[0]);
            return 2;
        }
        {
            struct jc_arena *a = jc_arena_new(0);
            int rc;
            if (a == NULL) {
                fprintf(stderr, "jichi-harness: init: out of memory\n");
                return 1;
            }
            rc = jc_harness_init_run(config_path, init_mode, init_dir,
                                     init_from, init_jichi_bin, a);
            jc_arena_free(a);
            return rc;
        }
    }

    /* mirror is built (M1055, H3b): `mirror fetch` and `mirror head`.
     * A fully-formed `mirror <fetch|head> --config <f> <mirror-dir>` runs.
     * A BARE `mirror` (no verb) keeps the H0 not-built refusal -- the
     * sibling driver tests/smoke/harness_third_program.sh (checks 4-7) runs
     * bare `mirror` and asserts that refusal, so it must stay; this file is
     * one this milestone may edit, but the driver's contract is not. A verb
     * given but no mirror dir is a usage refusal (exit 2, nothing run). The
     * push is a DIFFERENT milestone (M1056, H3c) and is NOT built here. */
    if (strcmp(not_built_cmd, "mirror") == 0) {
        if (mirror_sub == NULL) {
            return not_built("mirror");
        }
        if (mirror_dir == NULL) {
            fprintf(stderr,
                    "jichi-harness: mirror: needs a <mirror-dir> after the "
                    "verb (%s)\n", mirror_sub);
            usage(argv[0]);
            return 2;
        }
        {
            struct jc_arena *a = jc_arena_new(0);
            int rc;
            if (a == NULL) {
                fprintf(stderr, "jichi-harness: mirror: out of memory\n");
                return 1;
            }
            if (strcmp(mirror_sub, "fetch") == 0) {
                rc = jc_harness_mirror_fetch_run(config_path, mirror_dir, a);
            } else if (strcmp(mirror_sub, "push") == 0) {
                rc = jc_harness_mirror_push_run(config_path, mirror_dir, a);
            } else {
                rc = jc_harness_mirror_head_run(config_path, mirror_dir, a);
            }
            jc_arena_free(a);
            return rc;
        }
    }

    /* run is built (M1057, H4): one lead run in Mode A, the C89 port of the
     * drive's run-lead.sh (the container half dropped). A fully-formed
     * `run --config <f> --tenant <name> --run <N>` runs; the pre-run refusals
     * (no --tenant, no tenant named, no workdir, the drive dir not set, a
     * missing promptFile, the model-key reference not set, a missing model-key
     * file, a missing bin) are run's own and all happen BEFORE any write or
     * spawn (exit 2, nothing created). A spawn that cannot start is exit 1;
     * otherwise the code returned is the CHILD's exit code. */
    if (strcmp(not_built_cmd, "run") == 0) {
        struct jc_arena *a = jc_arena_new(0);
        int rc;
        if (a == NULL) {
            fprintf(stderr, "jichi-harness: run: out of memory\n");
            return 1;
        }
        /* A BARE `run` (neither --tenant nor --run given) keeps the H0
         * not-built refusal -- init's precedent (a bare init keeps its H0
         * not-built refusal, see the init block above): the sibling driver
         * tests/smoke/harness_third_program.sh runs bare `run` and asserts
         * that refusal, and tests/smoke/harness_run.sh's R0 holds it. A
         * fully-formed `run --config <f> --tenant <n> --run <N>` passes the
         * guard and runs. */
        if (run_tenant == NULL && run_num == NULL) {
            jc_arena_free(a);
            return not_built("run");
        }
        rc = jc_harness_run(config_path, run_tenant, run_num, a);
        jc_arena_free(a);
        return rc;
    }

    /* drive is built (M1059, H5): the drive's outer loop until its end, the
     * C89 port of loop.sh (the container half dropped). A fully-formed
     * `drive --config <f> --tenant <name> [--once] [--tick <s>]` runs; the
     * refusals (no --config, no --tenant, a tenant the config does not name
     * or without a workdir, --tick 0) are drive's own and all happen BEFORE
     * any log line or run starts (exit 2, nothing created). The unparseable
     * drive.end is exit 1 (the reference's own class). A loop that ENDED
     * (STOP, deadline, or --once done) is exit 0, regardless of the runs'
     * rcs; the loop's own SIGINT is the default handler's 130 (the loop
     * installs no handler of its own -- behaviour 9's handler is the run's).
     * A BARE `drive` (no --tenant) keeps the H0 not-built refusal, the run
     * precedent: the sibling driver tests/smoke/harness_third_program.sh
     * runs bare `drive` and asserts that refusal, and this file's contract
     * is not one this milestone may break. */
    if (strcmp(not_built_cmd, "drive") == 0) {
        struct jc_arena *a = jc_arena_new(0);
        int rc;
        if (a == NULL) {
            fprintf(stderr, "jichi-harness: drive: out of memory\n");
            return 1;
        }
        if (drive_tenant == NULL && config_path == NULL) {
            jc_arena_free(a);
            return not_built("drive");
        }
        rc = jc_harness_drive(config_path, drive_tenant, drive_once,
                              drive_tick, a);
        jc_arena_free(a);
        return rc;
    }

    /* credential-helper is built (M1056, H3c): the git-spawned subcommand.
     * It takes a single positional (get|store|erase) and NO --config (git
     * does not pass one); it reads its configuration from the environment
     * the push child carries. A missing action is a usage refusal (exit 2). */
    if (strcmp(not_built_cmd, "credential-helper") == 0) {
        struct jc_arena *a = jc_arena_new(0);
        int rc;
        if (a == NULL) {
            fprintf(stderr, "jichi-harness: credential-helper: out of "
                            "memory\n");
            return 1;
        }
        rc = jc_harness_cred_helper_run(cred_action, a);
        jc_arena_free(a);
        return rc;
    }

    /* The other two are not built yet. */
    return not_built(not_built_cmd);
}
