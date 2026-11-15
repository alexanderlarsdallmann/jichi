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
#include "jc_proc.h"           /* M1190 (F3): the memory protection (jc_proc_harden_*) */
#include "jc_mem.h"            /* struct jc_arena */
#include "jc_vec.h"            /* struct jc_vec (push's positional list, M1151 b) */
#include "jc_harness_doctor.h" /* M1052 (H1b): the doctor command */
#include "jc_harness_init.h"   /* M1053 (H2): the init command */
#include "jc_harness_mirror.h" /* M1055 (H3b): the mirror fetch/head commands */
#include "jc_harness_push.h"   /* M1056 (H3c): the mirror push command */
#include "jc_harness_cred.h"   /* M1056 (H3c): the credential-helper subcommand */
#include "jc_harness_run.h"    /* M1057 (H4): the run command */
#include "jc_harness_drive.h"  /* M1059 (H5): the drive (outer loop) command */
#include "jc_harness_notify.h" /* M1152 (H8): the notify command */
#include "jc_harness_config.h" /* M1153 (H9a): the watch command (the entry
                                * prototypes live here; the config struct) */
#include "jc_hp_gateledger.h"  /* M1154 (H9b T2): the ledger stage body */
#include "jc_hp_gateverdict.h" /* M1154 (H9b T3): the verdict stage body */
#include "jc_hp_gatesweep.h"   /* M1154 (H9b T5, wave 2): the sweep stage body */
#include "jc_hp_gateci.h"      /* M1154 (H9b T8, wave 3): the ci stage body */
#include "jc_hp_gateref.h"     /* M1157 (H9b T9, wave 3): the redfirst stage body */
#include "jc_hp_gate.h"        /* M1159 (H9b wave 4): the gate run orchestrator */
#include "jc_hp_gateunits.h"   /* M1159 (H9b wave 4): the gate units template (D13) */

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
    fprintf(f, "  push        The scheduled host push (BUILT, M1147/H7): the\n");
    fprintf(f, "              C89 port of the host's push.sh / push-tick.sh /\n");
    fprintf(f, "              push-backlog.sh -- records | tick | backlog |\n");
    fprintf(f, "              units | run-drive, each with --config <f> --\n");
    fprintf(f, "              the five subcommand bodies are live (the verb\n");
    fprintf(f, "              is BUILT, M1147/H7); a bare 'push' names the\n");
    fprintf(f, "              five subcommands and exits 2\n");
    fprintf(f, "  notify      Deliver a host-gate verdict to its readers at\n");
    fprintf(f, "              once, not at the next check-in (BUILT,\n");
    fprintf(f, "              M1152/H8): the C89 port of the hardening drive's\n");
    fprintf(f, "              gate-note.sh -- run --config <f> [--inbox <dir>]\n");
    fprintf(f, "              <SHA8> <TEXT> [ATTACH]; a missing SHA8 or TEXT\n");
    fprintf(f, "              is a usage refusal, exit 2\n");
    fprintf(f, "  watch       The scheduled host gate watcher (BUILT,\n");
    fprintf(f, "              M1153/H9a): the C89 port of the hardening\n");
    fprintf(f, "              drive's gate-watch.sh -- gate | units, each with\n");
    fprintf(f, "              --config <f> -- run gate --config <f>\n");
    fprintf(f, "              [--log <f>] [--state <f>] [--out <f>] [--dry];\n");
    fprintf(f, "              a bare 'watch' names the subcommands and\n");
    fprintf(f, "              exits 2\n");
    fprintf(f, "  gate        The gate RUN on exactly a READY commit (BUILT as\n");
    fprintf(f, "              a verb, M1154/H9b): run | units, each with\n");
    fprintf(f, "              --config <f>; the ledger, verdict, sweep,\n");
    fprintf(f, "              ci and redfirst stages are BUILT (T2 / T3 / T5 /\n");
    fprintf(f, "              T8 / T9, waves 1-3); the orchestrator (the full\n");
    fprintf(f, "              gate run, wave 4) is BUILT: run gate run\n");
    fprintf(f, "              --config <f> <SHA8> [BATCH] [flags]; the HOLD,\n");
    fprintf(f, "              land and repin arms are not built yet and refuse\n");
    fprintf(f, "              by name, exit 1; a bare 'gate' names the\n");
    fprintf(f, "              subcommands and exits 2\n");
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
    /* push's own args (M1147, H7): the subcommand (records|tick|backlog|
     * units|run-drive; a bare `push` is a usage refusal naming the five)
     * and the positionals after it (the records dir, the backlog's TIPs).
     * --config is accepted before OR after the subcommand, like mirror's.
     * -o <dir> (B4, units only): the directory the generated units are
     * written to; NULL when absent (units then prints them to stdout). It
     * is carried in push_out_dir and passed to units' entry as its
     * positional slot (units takes no other positional). */
    const char *push_sub = NULL;
    const char *push_arg1 = NULL;
    const char *push_out_dir = NULL;
    /* notify's own args (M1152, H8): --inbox <dir> (the scratch inbox, the
     * GATE_INBOX knob, behaviour 3) and the positionals SHA8, TEXT, [ATTACH].
     * --config is accepted before OR after the command, like the others. */
    const char *notify_inbox = NULL;
    const char *notify_sha8 = NULL;
    const char *notify_text = NULL;
    const char *notify_attach = NULL;
    /* watch's own args (M1153, H9a): the subcommand (gate|units; a bare
     * `watch` is a usage refusal naming the two) and the tick's flags.
     * --config is accepted before OR after the subcommand, like the others.
     * gate: --log <f> / --state <f> / --out <f> (the reference's GATE_LOG3 /
     * GATE_STATE / GATE_OUT knobs, named by flag) and --dry (GATE_DRY).
     * The shimmable host commands (--unit-state / --unit-list / --unit-run /
     * --pgrep) fall back to the reference's argv when absent (the
     * pageCommand idiom); the config's host block may name them too.
     * units: -o <dir> (the push units idiom; absent = print to stdout). */
    const char *watch_sub = NULL;
    const char *watch_log = NULL;
    const char *watch_state = NULL;
    const char *watch_out = NULL;
    int watch_dry = 0;
    const char *watch_unit_state = NULL;
    const char *watch_unit_list = NULL;
    const char *watch_unit_run = NULL;
    const char *watch_pgrep = NULL;
    const char *watch_out_dir = NULL;
    /* gate's own args (M1154, batch H9b): the subcommand (run | units; a bare
     * `gate` is a usage refusal naming the two) and the stage flags the
     * two PURE stages are invoked with (usage-only this wave -- the bodies
     * are T2 / T3). --config is accepted before OR after the subcommand, like
     * the others. `gate run --stage ledger <REPO> <BASE> <HEAD>` carries
     * --ledger-address / --ledger-accept; `gate run --stage verdict <FILE>
     * <SHA8>` carries --push-repo. The positionals ride in gate_pos (an
     * uncapped jc_vec, the push_extra idiom, M1151 b): every subcommand
     * validates its own count at the dispatch tail. */
    const char *gate_sub = NULL;
    const char *gate_stage = NULL;
    const char *gate_ledger_address = NULL;
    const char *gate_push_repo = NULL;
    const char *gate_ledger_accept = NULL;
    /* The sweep stage's shimmable surfaces (M1154 T5, wave 2): the flag wins
     * over the config's host.bin / buildRun / mutantRun / unitRunSan /
     * unitRunO2 / containerRun (the M1153 idiom; NULL = the config value,
     * NULL there = the reference's default: a stub binary, the system
     * make / podman in the clone). --work / --records are the scratch /
     * records homes. */
    const char *gate_work = NULL;
    const char *gate_records = NULL;
    const char *gate_bin = NULL;
    const char *gate_build_run = NULL;
    const char *gate_mutant_run = NULL;
    const char *gate_unit_run_san = NULL;
    const char *gate_unit_run_o2 = NULL;
    const char *gate_container_run = NULL;
    /* The ci stage's shimmable surfaces (M1154 T8, wave 3): the flag wins
     * over the config's host block, NULL = the config value, NULL there =
     * the reference's default (`make ci` in the clone, <work>/ci, "0").
     * The body (T8) reads them; --work / --records / --push-repo are the
     * shared stage flags above. */
    const char *gate_ci_run = NULL;
    const char *gate_ci_log_dir = NULL;
    const char *gate_ci_smoke_keep_going = NULL;
    /* The redfirst stage's shimmable surfaces (M1157 T9, wave 3): the flag
     * wins over the config's host.redfirstRun / host.redfirstUnitRun, which
     * win over the reference's real forms (the ci/sweep idiom). */
    const char *gate_redfirst_run = NULL;
    const char *gate_redfirst_unit_run = NULL;
    const char *gate_gate_home = NULL;
    const char *gate_state = NULL;
    const char *gate_land_run = NULL;
    const char *gate_repin_run = NULL;
    const char *gate_out = NULL;
    struct jc_vec gate_pos;
    /* push's later positionals (the records dir, the backlog's TIPs) as a
     * growing jc_vec of `const char *` (M1151 defect b): the pre-fix fixed
     * push_extra[8] refused a 10th positional as `unexpected argument`.
     * No cap here -- every subcommand validates its own count (backlog
     * takes ALL, the rest take one and refuse extras). The vector owns the
     * SLOTS only; the strings are the caller's argv (not copied). An
     * AUTOMATIC in this scope, like push_sub/push_arg1: allocation happens
     * only once `push` is the command and positionals arrive, so every
     * exit BEFORE that leaves data NULL (jc_vec_free on NULL is a no-op);
     * the two exits reachable AFTER pushes -- the in-loop unknown-option
     * refusal and the push dispatch -- free it. */
    struct jc_vec push_extra;

    /* M1190 (def-383, D-F8-1/D-F8-2, dec-2026-11-12-harness-protection): first
     * in main, so before any child can exist and for every command. The harness
     * holds the model key for a whole run (run) and the model key and the git
     * token in the push / credential-helper commands, and it runs as the
     * operator's user beside jichi's own processes, so a same-user process could
     * read the key out of /proc/<pid>/environ and readlink the exe while the
     * harness is alive. This makes the harness non-dumpable (Linux), which the
     * kernel undoes at exec, so the children it spawns stay ordinary. It honours
     * JICHI_KEEP_DUMPABLE itself (set it only to get a core dump or attach a
     * debugger); doctor reports the state it left. */
    jc_proc_harden_environ();

    jc_vec_init(&push_extra, sizeof (const char *));
    jc_vec_init(&gate_pos, sizeof (const char *));

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
            } else if (strcmp(a, "push") == 0) {
                /* The scheduled host push (M1147, H7): BUILT as a verb with
                 * five subcommands (records|tick|backlog|units|run-drive);
                 * a bare `push` (no subcommand) is a USAGE refusal naming
                 * the five, not the not-built class. */
                not_built_cmd = "push";
                continue;
            } else if (strcmp(a, "notify") == 0) {
                /* Deliver a host-gate verdict (M1152, H8): BUILT; the note, the
                 * digest line and the hook. A missing SHA8 or TEXT is a usage
                 * refusal (exit 2) before any write, not the not-built class. */
                not_built_cmd = "notify";
                continue;
            } else if (strcmp(a, "watch") == 0) {
                /* The scheduled host gate watcher (M1153, H9a): BUILT as a verb
                 * with two subcommands (gate|units); a bare `watch` is a usage
                 * refusal naming the two (the push subcommand idiom, M1147). */
                not_built_cmd = "watch";
                continue;
            } else if (strcmp(a, "gate") == 0) {
                /* The gate RUN (M1154, batch H9b): BUILT as a verb with two
                 * subcommands (run | units) -- usage refusals only in this
                 * wave; the ledger / verdict stage bodies (T2 / T3) and the
                 * orchestrator (wave 4) follow. A bare `gate` is a usage
                 * refusal naming the two (the watch subcommand idiom, M1153). */
                not_built_cmd = "gate";
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
        /* After the command. push's own flags: --config (accepted here too,
         * like init's/mirror's/run's/drive's) and -o <dir> (B4, the units'
         * output directory), before OR after the subcommand. Any other flag
         * after `push` is not recognised. */
        if (strcmp(not_built_cmd, "push") == 0) {
            if (strcmp(a, "--config") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "jichi-harness: --config needs a value <file>\n");
                    usage(argv[0]);
                    jc_vec_free(&push_extra);
                    return 2;
                }
                i++;
                config_path = argv[i];
                continue;
            }
            if (strcmp(a, "-o") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "jichi-harness: -o needs a value <dir>\n");
                    usage(argv[0]);
                    jc_vec_free(&push_extra);
                    return 2;
                }
                i++;
                push_out_dir = argv[i];
                continue;
            }
        }
        /* After the command. notify's own flags: --config (accepted here too,
         * like the others) and --inbox <dir> (the scratch inbox, behaviour 3).
         * Any other flag after `notify` is not recognised. */
        if (strcmp(not_built_cmd, "notify") == 0) {
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
            if (strcmp(a, "--inbox") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "jichi-harness: --inbox needs a value <dir>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                notify_inbox = argv[i];
                continue;
            }
        }
        /* After the command. watch's own flags: --config (accepted here too,
         * like the others) and the tick's knobs. --log <f> / --state <f> /
         * --out <f> are the reference's GATE_LOG3 / GATE_STATE / GATE_OUT
         * (named by flag); --dry is GATE_DRY (a flag, no value); --unit-state
         * / --unit-list / --unit-run / --pgrep name the shimmable host
         * commands (absent = the reference's argv default, the pageCommand
         * idiom); -o <dir> (units only) is the push units' output directory.
         * Any other flag after `watch` is not recognised. */
        if (strcmp(not_built_cmd, "watch") == 0) {
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
            if (strcmp(a, "--log") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --log needs a value "
                                    "<file>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                watch_log = argv[i];
                continue;
            }
            if (strcmp(a, "--state") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --state needs a value "
                                    "<file>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                watch_state = argv[i];
                continue;
            }
            if (strcmp(a, "--out") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --out needs a value "
                                    "<file>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                watch_out = argv[i];
                continue;
            }
            if (strcmp(a, "--dry") == 0) {
                watch_dry = 1;
                continue;
            }
            if (strcmp(a, "--unit-state") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --unit-state needs a "
                                    "value <cmd>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                watch_unit_state = argv[i];
                continue;
            }
            if (strcmp(a, "--unit-list") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --unit-list needs a "
                                    "value <cmd>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                watch_unit_list = argv[i];
                continue;
            }
            if (strcmp(a, "--unit-run") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --unit-run needs a "
                                    "value <cmd>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                watch_unit_run = argv[i];
                continue;
            }
            if (strcmp(a, "--pgrep") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --pgrep needs a value "
                                    "<cmd>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                watch_pgrep = argv[i];
                continue;
            }
            if (strcmp(a, "-o") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "jichi-harness: -o needs a value <dir>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                watch_out_dir = argv[i];
                continue;
            }
        }
        /* gate's own flags (M1154, batch H9b, usage-only this wave). --config
         * is accepted here too (before OR after the subcommand, like the
         * others). The stage flags are parsed and carried, but the wave-1
         * dispatch refuses every `gate run --stage` before they are read --
         * the bodies are T2 (ledger) and T3 (verdict). */
        if (strcmp(not_built_cmd, "gate") == 0) {
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
            if (strcmp(a, "--stage") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "jichi-harness: --stage needs a value "
                            "(ledger | verdict | sweep | ci | redfirst)\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_stage = argv[i];
                continue;
            }
            /* The stage value flags: consumed and stored -- the ledger body
             * (T2) reads them now (the flag wins; the config's host block is
             * the absent-flag default, the M1153 idiom); the verdict body
             * (T3) keeps --push-repo. */
            if (strcmp(a, "--ledger-address") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --ledger-address "
                                    "needs a value <addr>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_ledger_address = argv[i];
                continue;
            }
            if (strcmp(a, "--ledger-accept") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --ledger-accept "
                                    "needs a value <file>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_ledger_accept = argv[i];
                continue;
            }
            /* The sweep stage's shimmable surfaces (M1154 T5, wave 2):
             * consumed and stored -- the flag wins over the config's host
             * block, NULL = the config value, NULL there = the reference's
             * default (a stub binary, the system make / podman in the
             * clone). The body (T5) reads them. */
            if (strcmp(a, "--work") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --work needs a value "
                                    "<dir>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_work = argv[i];
                continue;
            }
            if (strcmp(a, "--records") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --records needs a value "
                                    "<dir>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_records = argv[i];
                continue;
            }
            if (strcmp(a, "--out") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --out needs a value "
                                    "<dir>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_out = argv[i];
                continue;
            }
            if (strcmp(a, "--bin") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --bin needs a value "
                                    "<path>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_bin = argv[i];
                continue;
            }
            if (strcmp(a, "--build-run") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --build-run needs a "
                                    "value <cmd>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_build_run = argv[i];
                continue;
            }
            if (strcmp(a, "--mutant-run") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --mutant-run needs a "
                                    "value <cmd>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_mutant_run = argv[i];
                continue;
            }
            if (strcmp(a, "--unit-run-san") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --unit-run-san needs a "
                                    "value <cmd>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_unit_run_san = argv[i];
                continue;
            }
            if (strcmp(a, "--unit-run-o2") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --unit-run-o2 needs a "
                                    "value <cmd>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_unit_run_o2 = argv[i];
                continue;
            }
            if (strcmp(a, "--container-run") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --container-run needs a "
                                    "value <cmd>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_container_run = argv[i];
                continue;
            }
            if (strcmp(a, "--push-repo") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr,
                            "jichi-harness: --push-repo needs a value <path>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_push_repo = argv[i];
                continue;
            }
            /* The ci stage's shimmable surfaces (M1154 T8, wave 3): the
             * --container-run shape; the body (T8) reads them. */
            if (strcmp(a, "--ci-run") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --ci-run needs a "
                                    "value <cmd>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_ci_run = argv[i];
                continue;
            }
            if (strcmp(a, "--ci-log-dir") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --ci-log-dir needs a "
                                    "value <dir>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_ci_log_dir = argv[i];
                continue;
            }
            if (strcmp(a, "--ci-smoke-keep-going") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --ci-smoke-keep-going "
                                    "needs a value <n>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_ci_smoke_keep_going = argv[i];
                continue;
            }
            if (strcmp(a, "--redfirst-run") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --redfirst-run "
                                    "needs a value <cmd>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_redfirst_run = argv[i];
                continue;
            }
            if (strcmp(a, "--redfirst-unit-run") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --redfirst-unit-run "
                                    "needs a value <cmd>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_redfirst_unit_run = argv[i];
                continue;
            }
            if (strcmp(a, "--gate-home") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --gate-home "
                                    "needs a value <dir>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_gate_home = argv[i];
                continue;
            }
            if (strcmp(a, "--state") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --state "
                                    "needs a value <f>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_state = argv[i];
                continue;
            }
            if (strcmp(a, "--land-run") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --land-run "
                                    "needs a value <cmd>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_land_run = argv[i];
                continue;
            }
            if (strcmp(a, "--repin-run") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "jichi-harness: --repin-run "
                                    "needs a value <cmd>\n");
                    usage(argv[0]);
                    return 2;
                }
                i++;
                gate_repin_run = argv[i];
                continue;
            }
        }
        /* Any other flag after a command is not recognised. Reaching this
         * under `push` (a flag after positionals) may have a live
         * push_extra allocation -- free it (a non-push run holds NULL). */
        if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "jichi-harness: unknown option '%s'\n", a);
            usage(argv[0]);
            jc_vec_free(&push_extra);
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
        if (strcmp(not_built_cmd, "push") == 0) {
            /* The first positional after `push` is the subcommand
             * (records|tick|backlog|units|run-drive); anything else is a
             * usage refusal naming the five. Later positionals are the
             * subcommand's own arguments (the records dir, the backlog's
             * TIPs). */
            if (push_sub == NULL) {
                if (strcmp(a, "records") != 0 && strcmp(a, "tick") != 0 &&
                    strcmp(a, "backlog") != 0 && strcmp(a, "units") != 0 &&
                    strcmp(a, "run-drive") != 0) {
                    fprintf(stderr,
                            "jichi-harness: unknown push subcommand '%s'\n", a);
                    usage(argv[0]);
                    return 2;
                }
                push_sub = a;
                continue;
            }
            if (push_arg1 == NULL) {
                push_arg1 = a;
                continue;
            }
            /* Every later positional rides in the (uncapped) push_extra
             * vector (M1151 defect b). A push OOM here is not a usage error:
             * refuse loudly, free the vector, exit 1. */
            if (jc_vec_push(&push_extra, &a) != JC_OK) {
                fprintf(stderr, "jichi-harness: out of memory holding push "
                                "positionals\n");
                jc_vec_free(&push_extra);
                return 1;
            }
            continue;
        }
        /* A bare word after `notify`: the positionals SHA8, TEXT, [ATTACH]
         * (behaviour 10: a missing SHA8 or TEXT is a usage refusal BEFORE any
         * write -- validated at the dispatch tail, not here). */
        if (strcmp(not_built_cmd, "notify") == 0) {
            if (notify_sha8 == NULL) {
                notify_sha8 = a;
                continue;
            }
            if (notify_text == NULL) {
                notify_text = a;
                continue;
            }
            if (notify_attach == NULL) {
                notify_attach = a;
                continue;
            }
            /* A fourth positional is not recognised. */
        }
        /* A bare word after `watch`: the subcommand gate | units (the first
         * positional); any second positional is not recognised (both
         * subcommands take flags only). A bad subcommand is a usage refusal
         * naming the two (the mirror verb idiom). */
        if (strcmp(not_built_cmd, "watch") == 0) {
            if (watch_sub == NULL) {
                if (strcmp(a, "gate") != 0 && strcmp(a, "units") != 0) {
                    fprintf(stderr,
                            "jichi-harness: watch: the subcommand must be "
                            "'gate' or 'units' (got '%s')\n", a);
                    usage(argv[0]);
                    return 2;
                }
                watch_sub = a;
                continue;
            }
            /* A second positional is not recognised. */
        }
        /* A bare word after `gate`: the subcommand run | units (the first
         * positional); any second positional is not recognised (both
         * subcommands take flags + the stage's own positionals, which ride in
         * gate_pos). A bad subcommand is a usage refusal naming the two (the
         * mirror / watch verb idiom). The stage positionals are NOT validated
         * here -- the dispatch tail does, per stage, the count the driver's
         * contract names. */
        if (strcmp(not_built_cmd, "gate") == 0) {
            if (gate_sub == NULL) {
                if (strcmp(a, "run") != 0 && strcmp(a, "units") != 0) {
                    fprintf(stderr,
                            "jichi-harness: gate: the subcommand must be "
                            "'run' or 'units' (got '%s')\n", a);
                    usage(argv[0]);
                    jc_vec_free(&gate_pos);
                    return 2;
                }
                gate_sub = a;
                continue;
            }
            /* Every positional after the subcommand rides in gate_pos (the
             * stage's own positionals: <REPO> <BASE> <HEAD> / <FILE> <SHA8> /
             * the full-run <SHA8> [BATCH]). The count is validated per stage
             * at the dispatch tail. A gate OOM here is not a usage error:
             * refuse loudly, free, exit 1 (the push_extra idiom). */
            if (jc_vec_push(&gate_pos, &a) != JC_OK) {
                fprintf(stderr, "jichi-harness: out of memory holding gate "
                                "positionals\n");
                jc_vec_free(&gate_pos);
                return 1;
            }
            continue;
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

    /* push is built as a VERB (M1147, H7) with five subcommands, each of
     * which takes --config (before or after the subcommand, the mirror-push
     * flag order). The subcommand BODIES are live (BUILT, M1147/H7), one file
     * per subcommand in the jc_hp_* files: jc_hp_records.c, jc_hp_tick.c,
     * jc_hp_backlog.c, jc_hp_units.c, jc_hp_rundrive.c (each exporting its
     * jc_harness_push_<sub>_run entry point; the shared M1056 git/credential
     * primitives they code against stay in jc_harness_push.c). A BARE `push`
     * (no subcommand) is a USAGE refusal naming the five -- the verb is
     * built, the subcommand is missing; it is NOT the not-built class (the
     * sibling driver checks 4-7 hold that refusal for the four other bare
     * forms). A `push <sub>` without --config is a usage refusal naming that
     * the config is required, before anything is loaded. */
    if (strcmp(not_built_cmd, "push") == 0) {
        if (push_sub == NULL) {
            fprintf(stderr,
                    "jichi-harness: push: needs a subcommand (records | tick "
                    "| backlog | units | run-drive)\n");
            usage(argv[0]);
            jc_vec_free(&push_extra);
            return 2;
        }
        if (config_path == NULL) {
            fprintf(stderr,
                    "jichi-harness: push %s: --config <path> is required\n",
                    push_sub);
            usage(argv[0]);
            jc_vec_free(&push_extra);
            return 2;
        }
        {
            struct jc_arena *a = jc_arena_new(0);
            int rc;
            /* The push_extra vector's backing buffer IS a C array of
             * `const char *` (elem size = sizeof(const char *)); hand the
             * subcommand its data pointer and count, the way the pre-fix
             * push_extra[8]/push_nextra were passed (M1151 defect b). */
            const char **push_extra_arr =
                (const char **)push_extra.data;
            int push_nextra = (int)push_extra.len;
            if (a == NULL) {
                fprintf(stderr, "jichi-harness: push: out of memory\n");
                jc_vec_free(&push_extra);
                return 1;
            }
            if (strcmp(push_sub, "records") == 0) {
                rc = jc_harness_push_records_run(config_path, push_arg1, a,
                                                 push_extra_arr, push_nextra);
            } else if (strcmp(push_sub, "tick") == 0) {
                rc = jc_harness_push_tick_run(config_path, push_arg1, a,
                                              push_extra_arr, push_nextra);
            } else if (strcmp(push_sub, "backlog") == 0) {
                rc = jc_harness_push_backlog_run(config_path, push_arg1, a,
                                                 push_extra_arr, push_nextra);
            } else if (strcmp(push_sub, "units") == 0) {
                /* B4: units takes no positional of its own; its `-o <dir>`
                 * (push_out_dir) rides in the positional slot. The dispatch
                 * passes NULL for it when `-o` was absent. */
                rc = jc_harness_push_units_run(config_path, push_out_dir, a,
                                               push_extra_arr, push_nextra);
            } else { /* run-drive */
                rc = jc_harness_push_rundrive_run(config_path, push_arg1, a,
                                                  push_extra_arr, push_nextra);
            }
            jc_arena_free(a);
            jc_vec_free(&push_extra);
            return rc;
        }
    }

    /* notify is built (M1152, H8): deliver one host-gate verdict -- the note
     * in the drive's inbox (and the inject when a run is LIVE), the line in the
     * gate home's digest.md, and the config-named hook. A missing --config is a
     * usage refusal naming that the config is required; a missing SHA8 or TEXT
     * is a usage refusal naming what is absent -- both BEFORE any write
     * (behaviour 10; the reference's ${1:?}/${2:?} class, exit 2). A non- /
     * empty ATTACH is not a refusal (it is skipped silently, behaviour 6). */
    if (strcmp(not_built_cmd, "notify") == 0) {
        if (config_path == NULL) {
            fprintf(stderr,
                    "jichi-harness: notify: --config <path> is required\n");
            usage(argv[0]);
            return 2;
        }
        if (notify_sha8 == NULL) {
            fprintf(stderr,
                    "jichi-harness: notify: usage: notify --config <f> "
                    "[--inbox <dir>] <SHA8> <TEXT> [ATTACH] -- a <SHA8> is "
                    "required\n");
            usage(argv[0]);
            return 2;
        }
        if (notify_text == NULL) {
            fprintf(stderr,
                    "jichi-harness: notify: usage: notify --config <f> "
                    "[--inbox <dir>] <SHA8> <TEXT> [ATTACH] -- a <TEXT> is "
                    "required\n");
            usage(argv[0]);
            return 2;
        }
        {
            struct jc_arena *a = jc_arena_new(0);
            int rc;
            if (a == NULL) {
                fprintf(stderr, "jichi-harness: notify: out of memory\n");
                return 1;
            }
            rc = jc_harness_notify(config_path, notify_inbox, notify_sha8,
                                   notify_text, notify_attach, a);
            jc_arena_free(a);
            return rc;
        }
    }

    /* watch is built (M1153, H9a): the scheduled host gate watcher, the C89
     * port of the hardening drive's gate-watch.sh. A BARE `watch` (no
     * subcommand) is a usage refusal naming the two (the push subcommand
     * idiom, M1147). A `watch <sub>` without --config is a usage refusal
     * naming that the config is required, before anything is loaded. gate:
     * one tick of the watcher (read the last READY line, wait for the remote,
     * the state word, the withdrawal, the master checks, the busy + smoke
     * waits, the start). units: the generated watcher's systemd units (the
     * push units idiom). The rc is the tick's rc (the oneshot's journal). */
    if (strcmp(not_built_cmd, "watch") == 0) {
        if (watch_sub == NULL) {
            fprintf(stderr,
                    "jichi-harness: watch: needs a subcommand (gate | units)\n");
            usage(argv[0]);
            return 2;
        }
        if (config_path == NULL) {
            fprintf(stderr,
                    "jichi-harness: watch: --config <path> is required\n");
            usage(argv[0]);
            return 2;
        }
        {
            struct jc_arena *a = jc_arena_new(0);
            int rc;
            if (a == NULL) {
                fprintf(stderr, "jichi-harness: watch: out of memory\n");
                return 1;
            }
            if (strcmp(watch_sub, "gate") == 0) {
                rc = jc_hp_watchgate_run(config_path, watch_log, watch_state,
                                         watch_out, watch_dry,
                                         watch_unit_state, watch_unit_list,
                                         watch_unit_run, watch_pgrep, a);
            } else { /* units */
                rc = jc_hp_watchunits_run(config_path, watch_out_dir, a);
            }
            jc_arena_free(a);
            return rc;
        }
    }

    /* gate is BUILT as a verb (M1154, batch H9b): the ledger, verdict,
     * sweep, ci and redfirst stages all have their bodies (T2 / T3 / T5 / T8
     * / T9), so their run forms dispatch below; only the ORCHESTRATOR body
     * (the full `gate run <SHA8> [BATCH]` that captures the ledger, runs the
     * sweep, says the verdict, writes the state file -- wave 4) is not
     * built; a full `gate run` without its `<SHA8> [BATCH]` positional is
     * the usage refusal ("needs a <SHA8> [BATCH]"), exit 2. A BARE `gate`
     * (no subcommand) is a
     * usage refusal naming the two; `gate run` with no stage and its SHA8
     * missing is a usage refusal (the full-run contract is <SHA8> [BATCH],
     * the stage contracts are <REPO> <BASE> <HEAD> for ledger and <FILE>
     * <SHA8> for verdict -- all non-empty, so an empty gate_pos means the
     * SHA8 / the stage positionals are absent); `gate <unknown-sub>` was
     * refused above. The ledger body carries its own required
     * --ledger-address refusal; the verdict body's --push-repo carries the
     * second-count repo (the config's host.pushRepo is the absent-flag
     * default). --config is required, checked before anything loads (the
     * watch idiom). */
    if (strcmp(not_built_cmd, "gate") == 0) {
        if (gate_sub == NULL) {
            fprintf(stderr,
                    "jichi-harness: gate: needs a subcommand (run | units)\n");
            usage(argv[0]);
            jc_vec_free(&gate_pos);
            return 2;
        }
        /* The full run's positional count (b15, BEFORE any file load: G2): at
         * most one positional after the SHA8 -- the BATCH. (The --stage forms
         * carry their OWN positionals and are not checked here.) */
        if (strcmp(gate_sub, "run") == 0 && gate_stage == NULL &&
            gate_pos.len > 2) {
            fprintf(stderr,
                    "jichi-harness: gate run: <SHA8> [BATCH] takes at most\n"
                    "one positional after the SHA8\n");
            usage(argv[0]);
            jc_vec_free(&gate_pos);
            return 2;
        }
        if (config_path == NULL) {
            fprintf(stderr,
                    "jichi-harness: gate: --config <path> is required\n");
            usage(argv[0]);
            jc_vec_free(&gate_pos);
            return 2;
        }
        if (strcmp(gate_sub, "run") == 0) {
            if (gate_stage != NULL) {
                if (strcmp(gate_stage, "ledger") != 0 &&
                    strcmp(gate_stage, "verdict") != 0 &&
                    strcmp(gate_stage, "sweep") != 0 &&
                    strcmp(gate_stage, "ci") != 0 &&
                    strcmp(gate_stage, "redfirst") != 0) {
                    /* A --stage that names none of the FIVE BUILT stages
                     * (ledger | verdict | sweep | ci | redfirst, the M1154
                     * note's addendum + T5 wave 2 + T8 wave 3 + T9 wave 3):
                     * a usage refusal naming them. */
                    fprintf(stderr,
                            "jichi-harness: gate run --stage: the stage must "
                            "be 'ledger', 'verdict', 'sweep', 'ci' or "
                            "'redfirst' (got '%s')\n",
                            gate_stage);
                    usage(argv[0]);
                    jc_vec_free(&gate_pos);
                    return 2;
                }
                if (strcmp(gate_stage, "ledger") == 0) {
                    /* The ledger stage body (M1154, T2): the C89 port of
                     * the reference's ledger.sh + the DD11 accept list.
                     * Its positionals are <REPO> <BASE> <HEAD> (the
                     * reference's argv, the driver's contract); the
                     * REQUIRED --ledger-address (the note's CHANGED: the
                     * reference hard-codes ADDR) is refused inside the body
                     * exit 2 naming the flag when absent in flag AND config.
                     * The body's rc is the stage's rc. */
                    const char *pos[3];
                    struct jc_arena *a;
                    int rc;

                    if (gate_pos.len != 3) {
                        fprintf(stderr,
                                "jichi-harness: gate run --stage ledger: "
                                "needs <REPO> <BASE> <HEAD> (got %d "
                                "positional(s))\n", (int)gate_pos.len);
                        usage(argv[0]);
                        jc_vec_free(&gate_pos);
                        return 2;
                    }
                    pos[0] = *(const char **)jc_vec_at(&gate_pos, 0);
                    pos[1] = *(const char **)jc_vec_at(&gate_pos, 1);
                    pos[2] = *(const char **)jc_vec_at(&gate_pos, 2);
                    jc_vec_free(&gate_pos);
                    a = jc_arena_new(0);
                    if (a == NULL) {
                        fprintf(stderr, "jichi-harness: gate: out of "
                                        "memory\n");
                        return 1;
                    }
                    rc = jc_hp_gateledger_run(config_path, pos[0], pos[1],
                                              pos[2], gate_ledger_address,
                                              gate_ledger_accept, a);
                    jc_arena_free(a);
                    return rc;
                }
                if (strcmp(gate_stage, "verdict") == 0) {
                    /* The verdict stage body (M1154, T3): the C89 port of
                     * the reference's sweep-verdict.sh + the 14:47
                     * uses_bin second count. Its positionals are <FILE>
                     * <SHA8> (the reference's argv, the driver's contract);
                     * --push-repo carries the second-count repo (the
                     * config's host.pushRepo is the absent-flag default).
                     * The body's rc is the stage's rc. */
                    const char *pos[2];
                    struct jc_arena *a;
                    int rc;

                    if (gate_pos.len != 2) {
                        fprintf(stderr,
                                "jichi-harness: gate run --stage verdict: "
                                "needs <FILE> <SHA8> (got %d positional(s))\n",
                                (int)gate_pos.len);
                        usage(argv[0]);
                        jc_vec_free(&gate_pos);
                        return 2;
                    }
                    pos[0] = *(const char **)jc_vec_at(&gate_pos, 0);
                    pos[1] = *(const char **)jc_vec_at(&gate_pos, 1);
                    jc_vec_free(&gate_pos);
                    a = jc_arena_new(0);
                    if (a == NULL) {
                        fprintf(stderr, "jichi-harness: gate: out of "
                                        "memory\n");
                        return 1;
                    }
                    rc = jc_hp_gv_run(config_path, pos[0], pos[1],
                                      gate_push_repo, a);
                    jc_arena_free(a);
                    return rc;
                }
                if (strcmp(gate_stage, "sweep") == 0) {
                    /* The sweep stage body (M1154, T5, wave 2): the C89 port
                     * of the reference's sweep.sh (the pre-gate sweep: the
                     * keyless driver loop, the smoke-mutant, the two unit
                     * suites, the two-userns container drivers, the records
                     * line -- the note's behaviour 3, its CHANGED lines:
                     * the `  build: <sha8>` line, the KEYS UNSET in the
                     * CHILD env, the shimmable unit / container). Its
                     * positional is the single <SHA8> (sweep.sh:2's argv);
                     * the scratch / records homes come from --work /
                     * --records (else the body's defaults), the shimmable
                     * commands from the flags (else the config's host block,
                     * else the reference's system make / podman in the
                     * clone), the clone's source from --push-repo (else
                     * host.pushRepo). The body's rc is the stage's rc. */
                    const char *pos[1];
                    struct jc_arena *a;
                    int rc;

                    if (gate_pos.len != 1) {
                        fprintf(stderr,
                                "jichi-harness: gate run --stage sweep: "
                                "needs a <SHA8> (got %d positional(s))\n",
                                (int)gate_pos.len);
                        usage(argv[0]);
                        jc_vec_free(&gate_pos);
                        return 2;
                    }
                    pos[0] = *(const char **)jc_vec_at(&gate_pos, 0);
                    jc_vec_free(&gate_pos);
                    a = jc_arena_new(0);
                    if (a == NULL) {
                        fprintf(stderr, "jichi-harness: gate: out of "
                                        "memory\n");
                        return 1;
                    }
                    rc = jc_hp_gatesweep_run(config_path, pos[0],
                                             gate_work, gate_records,
                                             gate_bin, gate_build_run,
                                             gate_mutant_run,
                                             gate_unit_run_san,
                                             gate_unit_run_o2,
                                             gate_container_run,
                                             gate_push_repo, a);
                    jc_arena_free(a);
                    return rc;
                }
                if (strcmp(gate_stage, "ci") == 0) {
                    /* The ci stage body (M1154, T8, wave 3): the C89 port
                     * of the reference's ci-gate.sh (the ci gate on exactly
                     * SHA8: the fresh clone of the push repo, the checkout
                     * verify, the origin/master merge-base, the keyless
                     * UNCAPPED make ci -- or the shimmable --ci-run -- with
                     * the log + meta files and the b6 rc / RED lines, the
                     * SV:1238 excerpt). Its positional is the single <SHA8>
                     * (ci-gate.sh:2's argv); the scratch / records homes
                     * come from --work / --records (else the body's
                     * defaults), the run from --ci-run (else the config's
                     * host.ciRun, else the reference's system make ci in
                     * the clone), the log dir from --ci-log-dir (else
                     * host.ciLogDir, else <work>/ci), the keep-going flag
                     * from --ci-smoke-keep-going (else host.
                     * ciSmokeKeepGoing, else "0" -- a non-numeric value is
                     * the body's usage refusal exit 2 naming it), the
                     * clone's source from --push-repo (else host.pushRepo,
                     * REQUIRED). The body's rc is the stage's rc. */
                    const char *pos[1];
                    struct jc_arena *a;
                    int rc;

                    if (gate_pos.len != 1) {
                        fprintf(stderr,
                                "jichi-harness: gate run --stage ci: "
                                "needs a <SHA8> (got %d positional(s))\n",
                                (int)gate_pos.len);
                        usage(argv[0]);
                        jc_vec_free(&gate_pos);
                        return 2;
                    }
                    pos[0] = *(const char **)jc_vec_at(&gate_pos, 0);
                    jc_vec_free(&gate_pos);
                    a = jc_arena_new(0);
                    if (a == NULL) {
                        fprintf(stderr, "jichi-harness: gate: out of "
                                        "memory\n");
                        return 1;
                    }
                    rc = jc_hp_gateci_run(config_path, pos[0],
                                          gate_work, gate_records,
                                          gate_ci_log_dir, gate_ci_run,
                                          gate_ci_smoke_keep_going,
                                          gate_push_repo, a);
                    jc_arena_free(a);
                    return rc;
                }
                if (strcmp(gate_stage, "redfirst") == 0) {
                    /* The redfirst stage body (M1157, T9, wave 3): the C89
                     * port of the reference's redfirst.sh (the red-first
                     * replay of BASE..HEAD from the push repo: the UNSTUBBED
                     * selection, the shimmed --redfirst-run /
                     * --redfirst-unit-run, the per-check verdict words, the
                     * summary, the records line). Its positionals are
                     * <BASE> <HEAD> (redfirst.sh:16's argv, the driver's
                     * contract); the scratch / out / records homes come from
                     * --work / --out / --records (else the body's defaults),
                     * the replay from --redfirst-run / --redfirst-unit-run
                     * (else the config's host.redfirstRun /
                     * host.redfirstUnitRun, else the reference's real forms),
                     * the clone's source from --push-repo (else host.pushRepo,
                     * REQUIRED). The body's rc is the stage's rc. */
                    const char *pos[2];
                    struct jc_arena *a;
                    int rc;

                    if (gate_pos.len != 2) {
                        fprintf(stderr,
                                "jichi-harness: gate run --stage redfirst: "
                                "needs <BASE> and <HEAD> (got %d "
                                "positional(s))\n", (int)gate_pos.len);
                        usage(argv[0]);
                        jc_vec_free(&gate_pos);
                        return 2;
                    }
                    pos[0] = *(const char **)jc_vec_at(&gate_pos, 0);
                    pos[1] = *(const char **)jc_vec_at(&gate_pos, 1);
                    jc_vec_free(&gate_pos);
                    a = jc_arena_new(0);
                    if (a == NULL) {
                        fprintf(stderr, "jichi-harness: gate: out of "
                                        "memory\n");
                        return 1;
                    }
                    rc = jc_hp_gateref_run(config_path, pos[0], pos[1],
                                           gate_work, gate_records,
                                           gate_out,
                                           gate_redfirst_run,
                                           gate_redfirst_unit_run,
                                           gate_push_repo, a);
                    jc_arena_free(a);
                    return rc;
                }
            }
            /* A full `gate run` (the orchestrator) needs its SHA8 [BATCH];
             * an empty gate_pos is the missing-SHA8 usage refusal. */
            if (gate_pos.len == 0) {
                fprintf(stderr,
                        "jichi-harness: gate run: needs a <SHA8> [BATCH]\n");
                usage(argv[0]);
                jc_vec_free(&gate_pos);
                return 2;
            }
        }
        /* `gate units` (the generated transient gate service template,
         * M1159 D13; the watch units idiom): read the config and print the
         * jhp-gate@.service template to stdout, or refuse exit 2 naming the
         * missing host.gateRun. It has NO positional and needs only a fresh
         * arena for the config load (the gate run dispatch's idiom), unlike
         * the full `gate run` which needs its run arena. */
        if (strcmp(gate_sub, "units") == 0) {
            struct jc_arena *a;
            int rc;
            a = jc_arena_new(0);
            if (a == NULL) {
                jc_vec_free(&gate_pos);
                fprintf(stderr, "jichi-harness: gate units: out of "
                        "memory\n");
                return 1;
            }
            rc = jc_hp_gateunits_run(config_path, a);
            jc_arena_free(a);
            jc_vec_free(&gate_pos);
            return rc;
        }
    }

    /* The full `gate run` ORCHESTRATOR (M1159, wave 4, Task B's core path):
     * the usage refusals above, the D8 keyless setup, the `gated` word (D9),
     * the five D1/D2 stage children, the RED / INVALID arms, and the terminal
     * green-land-refused. The HOLD / land / repin arms are Task C's (a named
     * HOLD refuses by name, exit 1); `gate units` is Task D's. The dispatch
     * gives it a fresh arena (the stage arms' idiom). */
    if (strcmp(not_built_cmd, "gate") == 0 && strcmp(gate_sub, "run") == 0) {
        const char *pos[3] = { NULL, NULL, NULL };
        struct jc_arena *a;
        int rc;

        pos[0] = *(const char **)jc_vec_at(&gate_pos, 0);
        if (gate_pos.len > 1) {
            pos[1] = *(const char **)jc_vec_at(&gate_pos, 1);
        }
        (void)pos[2];
        a = jc_arena_new(0);
        if (a == NULL) {
            jc_vec_free(&gate_pos);
            return 2;
        }
        rc = jc_hp_gate_full_run(config_path, pos[0], pos[1],
                                 gate_gate_home, gate_state, gate_push_repo,
                                 gate_ledger_address, gate_ledger_accept,
                                 gate_bin, gate_build_run, gate_mutant_run,
                                 gate_unit_run_san, gate_unit_run_o2,
                                 gate_container_run, gate_redfirst_run,
                                 gate_redfirst_unit_run, gate_records,
                                 gate_ci_run, gate_ci_log_dir,
                                 gate_ci_smoke_keep_going,
                                 gate_land_run, gate_repin_run, a);
        jc_arena_free(a);
        jc_vec_free(&gate_pos);
        return rc;
    }

    /* The other two are not built yet. */
    return not_built(not_built_cmd);
}
