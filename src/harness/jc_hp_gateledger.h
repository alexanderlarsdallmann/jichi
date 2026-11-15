/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hp_gateledger.h - the `gate run --stage ledger` body (M1154, batch
 * H9b, wave 1, task 2): the C89 port of the 2026-10-27 hardening-host
 * drive's ledger.sh, plus the DD11 accept list (the host's ledger-accept
 * beside the reference script; the port carries it, M1154 note behaviour
 * 2 CHANGED) and the host's ONE extra accepted shape beside the reference
 * -- DD6, the supervisor's docs/internal-only commit (M1154 T4, required
 * by SV:0137: the port must not DIFF from the host's own ledger.sh on the
 * first supervisor commit).
 *
 * `jc_hp_gateledger_run` is the dispatch entry: it loads the config,
 * resolves --ledger-address (the flag wins; the config's host.ledgerAddress
 * is the absent-flag default) and --ledger-accept the same way, runs the
 * ledger over REPO BASE..HEAD, prints the ledger lines to stdout, and
 * returns the ledger's rc (0 iff N > 0 and B == 0, 1 otherwise). Usage
 * refusals (no config, missing --ledger-address, a wrong positional count)
 * are exit 2.
 *
 * The classification core is PURE (no git, no I/O) and exported for the
 * unit table (tests/test_gateledger.c): the reasons are joined in
 * ledger.sh's fixed order (author, committer, co-authored-by, Jichi-Build,
 * Jichi-Model, priced-model) and the accept-list decision is
 * "full 40-hex id listed AND every reason is a missing-trailer reason". */

#ifndef JC_HP_GATELEDGER_H
#define JC_HP_GATELEDGER_H

#include <stddef.h>       /* size_t (the reasons buffer) */

#include "jc_mem.h"       /* struct jc_arena (the stage entry) */
#include "jc_vec.h"       /* struct jc_vec (the co-value list) */

/* The one commit's identity the classifier reads. `co` carries the
 * Co-Authored-By trailer values (valueonly, unfold, the non-empty lines);
 * jb / jm are the Jichi-Build / Jichi-Model values, "" when absent. */
struct jc_hp_gateledger_commit {
    const char *ae;                    /* %ae */
    const char *ce;                    /* %ce */
    struct jc_vec *co_list;            /* all co-author values (or NULL) */
    const char *jb;                    /* Jichi-Build value, "" absent */
    const char *jm;                    /* Jichi-Model value, "" absent */
    struct jc_vec *dd6_paths;          /* changed paths (DD6; NULL absent) */
};

/* Is `id` a priced provider's model id (ledger.sh:23's case: a substring
 * match on the six provider names -- anthropic, openai, vertex_ai, gemini,
 * bedrock, azure)? Pure. */
int jc_hp_gateledger_is_priced(const char *id);

/* The reasons joined in the reference's fixed order (a leading space per
 * reason, so the caller's `printf("%.8s%s\n", sha, why)` shape is byte-
 * identical to ledger.sh:24): ` author=<ae>`, ` committer=<ce>`,
 * ` co-authored-by=[<values joined |>]` (a co-author that lacks `<addr>`),
 * ` no-Co-Authored-By`, ` no-Jichi-Build`, ` no-Jichi-Model`,
 * ` priced-model=<jm>`. "" when the commit is clean. Pure (c needs at
 * least 64 + the reason text). */
void jc_hp_gateledger_reasons(struct jc_hp_gateledger_commit *c,
                              const char *addr, char *why, size_t cap);

/* Every reason is a missing-trailer reason (no-Co-Authored-By /
 * no-Jichi-Build / no-Jichi-Model -- the DD11 accept arm; a wrong address
 * or a priced model keeps the commit bad even when listed). Pure. */
int jc_hp_gateledger_all_missing_trailers(
    struct jc_hp_gateledger_commit *c, const char *addr);

/* Is the FULL 40-hex id in the accept list (the lines of `accept`, one id
 * per line, whole-line match)? Pure; `id` NULL/short -> 0. */
int jc_hp_gateledger_accept_listed(const char *id,
                                   const struct jc_vec *accept);

/* The accept list at `path` into `accept`, which is ALWAYS initialised
 * first, so a gate with no accept list (`path` NULL or empty) gets an EMPTY
 * vector the walk may read and free (M1178: it was initialised only inside
 * the read, and a run without a list read and freed an uninitialised one).
 * An unreadable file is an empty list too; -1 is out of memory only. */
int jc_hp_gateledger_load_accept(const char *path, struct jc_arena *a,
                                 struct jc_vec *accept);

/* The DD6 supervisor-commit shape (the host's ledger.sh beside the
 * reference; M1154 T4, SV:0137's measured cells), the per-commit walk's
 * arm after the author/committer reasons are empty and before the
 * co-authored-by reason. The CHEAP guard first (no git; the reference's
 * `[ -z "$why" ] && [ "$co" = "$SUP" ]`): author AND committer are `addr`,
 * and the split Co-Authored-By list is EXACTLY one value, byte-equal to
 * the supervisor's `Claude Opus 5.5 <noreply@anthropic.com>` (a second
 * Co-Authored-By line makes the list multi-line -> not equal -> the
 * normal co-authored-by reason). The classifier then: the changed paths
 * readable (`dd6_paths` non-NULL) and EVERY one of them starting
 * `docs/internal/` -> 1 (the accepted arm, printed, never counted); paths
 * unreadable (`dd6_paths` NULL) -> it fills `out` (cap) with the
 * reference's could-not-read reason and returns 0; a path outside
 * docs/internal/ -> 0 (the fall-through: the normal reasons). The
 * accepted line's exact bytes: SV:0137's. Pure. */
int jc_hp_gateledger_dd6_guard(struct jc_hp_gateledger_commit *c,
                               const char *addr);
int jc_hp_gateledger_dd6(struct jc_hp_gateledger_commit *c,
                         const char *addr, char *out, size_t cap);

/* The stage entry (the dispatch caller is jc_harness_main.c's gate arm).
 * addr / accept_file are the FLAG values (NULL = absent; the config's
 * host.ledgerAddress / host.ledgerAccept are the absent-flag defaults). */
int jc_hp_gateledger_run(const char *config_path,
                         const char *repo, const char *base, const char *head,
                         const char *addr, const char *accept_file,
                         struct jc_arena *a);

#endif /* JC_HP_GATELEDGER_H */
