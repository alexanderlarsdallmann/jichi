/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_push.h - `jichi-harness mirror push` (M1056, H3c): the C89 port of
 * the drive's mirror.sh mirror_push + pre-push-keyscan.sh + pre-push-sizecheck.sh
 * (the push half of the mirror; the fetch/head half is M1055, jc_harness_mirror).
 *
 * THE MIRROR IS THE PUSH CLIENT. The remote is the rehearsal stand-in (the
 * drive's GitLab over HTTPS in the real loop, never touched by a rehearsal).
 * In Mode A there is no container, so the two pre-push gates (the key scan,
 * the size check) are C logic the harness runs BEFORE the push, not hooks in
 * the mirror the agent could edit (the note's Decision H3c). The push runs
 * the SAME mirror-integrity precheck the fetch runs (M1055, the five checks,
 * git-free, exit 2 naming the check), then the URL guard (the effective push
 * URL `git remote get-url --push origin` must equal the stored copy's
 * `remote.origin.url` byte-exactly, exit 2 naming both on a mismatch, before
 * any ls-remote/push child), then the key scan and size check (each exit 1
 * naming a path or a size, never a secret), then the push by the stored
 * absolute URL (never the `origin` name), never --force, never a master
 * refspec.
 *
 * Exit classes (the note's behaviour 14): precheck or URL-guard or master-
 * guard failure = exit 2, the check named; a key-scan or size-check refusal =
 * exit 1 (the push did not happen, a path or size named, no secret); a push
 * that git refused (non-FF, a bad remote) = exit 1 with git's first 200 bytes;
 * a push that landed = exit 0. A scan that CANNOT run (fail-closed) = exit 1,
 * the missing secret or object named -- never exit 0.
 */
#ifndef JC_HARNESS_PUSH_H
#define JC_HARNESS_PUSH_H

#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"

struct jc_sb; /* jc_str.h: the growable string builder jc_hp_blob_read fills */

/* `mirror push --config <file> <mirror-dir>`. `config_path` may be NULL only
 * when the caller has already loaded the config (the dispatch passes the
 * --config path). `mirror_dir` is the bare mirror. `a` owns every string the
 * command reads; the caller frees it. Returns the process exit code (0 / 1 /
 * 2 as above). */
int jc_harness_mirror_push_run(const char *config_path,
                               const char *mirror_dir,
                               struct jc_arena *a);

/* def-350: the PUSH_MAX_TIPS / PUSH_MAX_EXCL values THIS build was compiled
 * with (24/48 in every default build; the smoke-push-boundary stage lowers
 * them to 1/1). print_version reports them under #ifdef JC_PUSH_BOUNDARY
 * and the driver's gate 2 greps the values, so a marker without the lowered
 * values cannot pass the gate. */
void jc_harness_push_caps(int *tips, int *excl);

/* ------------------------------------------------------------------------
 * The `push` verb (M1147, batch H7): the scheduled host push, a new
 * top-level command alongside init/doctor/mirror/credential-helper/run/
 * drive, with five subcommands:
 *
 *   push records    the records push (def-360 retired; B1)
 *   push tick       one tick under the lock, with paging (B2)
 *   push backlog    a refused backlog, pushed in fast-forward steps (B3)
 *   push units      the systemd unit files, generated not hand-written (B4)
 *   push run-drive  the podman argv it WOULD build, printed (B5)
 *
 * Each takes `--config <path>` (accepted before OR after the subcommand,
 * the mirror-push flag order). `config_path` may be NULL only when the
 * caller has already loaded the config; `positional` is the subcommand's
 * own positional argument (the records dir for `records`, a TIP for
 * `backlog`; NULL when none was given) and `rest`/`n_rest` carry the
 * positionals after it (`backlog` takes several). `a` owns every string
 * the command reads; the caller frees it.
 *
 * `jc_harness_push_units_run` has NO positional argument of its own; the
 * dispatch passes the `-o <dir>` value (NULL when absent) in its
 * `positional` slot: B4 prints the two generated units to stdout by
 * default and, with `-o`, writes them under that directory (plus a
 * confirmation line). The other four entry points are unchanged.
 *
 * BATCH H7: the five entry points are wired into the dispatch. The B4
 * (`units`) and B5 (`run-drive`) bodies are built (they generate the
 * unit files / print the podman argv, respectively); the B1 (`records`),
 * B2 (`tick`) and B3 (`backlog`) bodies remain the NOT-YET-WIRED stubs --
 * each prints `jichi-harness: push <sub>: not wired yet (M1147, batch
 * H7)` to stderr and returns 2 (the WIRE class: the verb is built, the
 * subcommand's body is not). The stubs land in W2 of the same batch.
 * ------------------------------------------------------------------------ */
int jc_harness_push_records_run(const char *config_path, const char *positional,
                                struct jc_arena *a, const char **rest,
                                int n_rest);
int jc_harness_push_tick_run(const char *config_path, const char *positional,
                             struct jc_arena *a, const char **rest, int n_rest);
int jc_harness_push_backlog_run(const char *config_path, const char *positional,
                                struct jc_arena *a, const char **rest,
                                int n_rest);
int jc_harness_push_units_run(const char *config_path, const char *positional,
                              struct jc_arena *a, const char **rest, int n_rest);
int jc_harness_push_rundrive_run(const char *config_path, const char *positional,
                                 struct jc_arena *a, const char **rest,
                                 int n_rest);

/* ------------------------------------------------------------------------
 * M1147 (H7): the push subcommand bodies -- shared helpers.
 *
 * The five subcommand bodies above live in their OWN files (one file per
 * owner, so the parallel pool's per-file merge can land all of them:
 * jc_hp_records.c / jc_hp_tick.c / jc_hp_backlog.c / jc_hp_units.c /
 * jc_hp_rundrive.c). The bodies are NOT wired yet in this batch (each prints
 * the not-wired refusal and returns 2); the four that need M1056's machinery
 * (records, tick, backlog, run-drive) code against the exported helpers
 * below. They are the M1056 `mirror push` git-child / credential / secret /
 * range-rule primitives EXPORTED from jc_harness_push.c (the M1056 code itself
 * is unchanged; the helpers are thin wrappers that call it).
 *
 * All `a` arena parameters own the strings the helper returns; the caller
 * frees the arena. `e_global` / `e_system` are the "GIT_CONFIG_GLOBAL=<path>"
 * / "GIT_CONFIG_SYSTEM=<path>" env-slot strings M1056's make_empty_env_files
 * builds (behaviour 8's isolation); pass them to the range scans.
 * ------------------------------------------------------------------------ */

/* One git child, exported (M1056's static `git_child` :466 reshaped to the
 * workers' argv). Builds:
 *   git -c core.fsmonitor= -c log.showSignature=false
 *       -c commit.gpgsign=false -c core.hooksPath=/dev/null
 *       -c core.alternateRefsCommand= [-C <workdir>]
 *       [-c <cred_flags[i]> ...] <args[0]> ... <args[n_args-1]>
 * i.e. the five M2 -c flags FIRST, then -C workdir (omitted when workdir is
 * NULL), then the credential -c flags (each a full `key=value` string, the
 * -c prefix added here), then the caller's args (args[0] is `git`'s own
 * subcommand). The env carries the two GIT_CONFIG_ clearing slots (pointing
 * at /dev/null -- CHOOSE: M1056 uses empty files under cfg.dir, but this
 * export has no dir parameter; /dev/null gives the same empty-config
 * isolation file-free) plus the n_env credential env slots (in order).
 * `out` (caller buffer of at least max_out) receives the child's combined
 * stdout+stderr, truncated to max_out; `err_line` (caller buffer of at least
 * 512) receives the FIRST non-empty line (for refusals), truncated to 512.
 * CHOOSE (timeout): M1056 caps its git children at PUSH_TIMEOUT (120 s); the
 * reference host scripts run git with no cap. This export KEEPS M1056's 120 s
 * cap (jc_proc_capture timeout 120L) so a hung git child cannot wedge a host
 * tick. Returns jc_proc_capture's code: the child's exit status, -1 on spawn
 * failure, -2 on timeout/abort. */
int jc_hp_git_run(struct jc_arena *a, const char *workdir,
                  char *cred_flags[4], int n_cred,
                  const char *cred_env[4], int n_env,
                  const char *args[], int n_args,
                  int max_out, char *out, char *err_line);

/* The credential -c flags, exported: ONE THIN wrapper over M1056's fixed
 * static `credential_args` (the M1147 setflag fix, SV:1939 -- the ONE
 * helper, not a second copy). Fills `flags` with the three -c VALUES, in
 * order:
 *   flags[0] = "credential.helper="                              (the RESET)
 *   flags[1] = "credential.helper=<harness_path> credential-helper" (the SET,
 *             the two-word helper spec git runs as <harness> credential-helper
 *             <action>)
 *   flags[2] = "credential.useHttpPath=true"                     (path identity)
 * and returns the count (3), or -1 on a NULL / too-short argument (the
 * overflow refusal is credential_args' own). flags[3] is NULL on success.
 * LIFETIME: `flags[1]` is the SET-flag string ALLOCATED IN THE ARENA `a`, so
 * it survives every git child the caller runs with it (the dangling-setflag
 * fix: the M1056 local no longer returns a pointer into its dead stack frame
 * -- it allocates in the caller's arena). `a` must outlive the last child and
 * the caller frees it. The real three flags need only the harness path (no
 * token_file / user_rule / url -- those were a1's wrong-guess params, dropped
 * when this became a thin export). */
int jc_hp_cred_flags(struct jc_arena *a, const char *harness_path,
                     char *flags[4]);

/* The three credential identity env vars, exported (M1056's static
 * `credential_env` :565 + the NAME=value strings M1056 builds in
 * build_exclusion :1171-1187 / the push child :1851-1868). Formats into the
 * caller's three 512-byte buffers, in order:
 *   env_out[0] = "JICHI_HARNESS_GIT_TOKEN_FILE=<token_file>"
 *   env_out[1] = "JICHI_HARNESS_CRED_RULE=<none|oauth2>"   (rule from url:
 *               none for a file/local URL, oauth2 for a scheme URL)
 *   env_out[2] = "JICHI_HARNESS_CRED_URL=<url>"
 * (the caller passes n_env = 3). `env_out` is `char **`: the caller supplies
 * the storage (three 512-byte buffers); this export only formats, so the
 * strings outlive every git child the caller runs. On overflow a buffer is
 * left empty -- the caller must refuse. `user_rule` is reserved and ignored.
 * M1056's local credential_env is UNCHANGED. */
void jc_hp_cred_env(const char *token_file, const char *user_rule,
                    const char *url, char *env_out[4]);

/* Load one secret by reference, exported (M1056's static `load_secret` :680
 * + SECRET_MIN_LEN 12 + the `tr -d ' \t\r\n'` trim). 0 = ok: *out is the
 * arena-allocated trimmed secret (NUL-terminated), *out_len its trimmed byte
 * length. 1 = refusal: the file is missing/unreadable, or the trimmed value
 * is shorter than the minimum; *out is set to the path (or "" when no path)
 * so the caller's path-named refusal line is never NULL, and *out_len is 0.
 * The bytes are never used on refusal. THIS PRINTS NOTHING -- the caller
 * prints the path-named line. M1056's local load_secret is UNCHANGED. */
int jc_hp_load_secret(struct jc_arena *a, const char *path, char **out,
                      int *out_len);

/* A NUL-safe substring search over raw bytes, exported (M1056's static
 * `bytes_contains` :727). `needle_len` is the needle's byte length (0 always
 * found); the needle is NUL-terminated, the hay may hold NULs. Returns 1 when
 * the needle occurs in the hay, 0 otherwise. M1056's local bytes_contains is
 * UNCHANGED. */
int jc_hp_bytes_contains(const char *hay, int hay_len, const char *needle,
                         int needle_len);

/* Read ONE blob by its object SHA into a fresh jc_sb, exported (M1147, H7;
 * SV:2239) -- the per-blob cat-file the TWO key scans share. Both the M1056
 * `mirror push` key scan (jc_harness_push.c key_scan) and the records key
 * scan (jc_hp_records.c records_key_scan) read every pushed file version with
 * the same idiom -- `git cat-file blob <sha>` (the five M2 -c flags, no
 * credential flags on a read child, M1056's 120 s cap, the capture into a
 * jc_sb). They were BOTH refusing a 0-byte blob fail-closed on the
 * `blob.data == NULL || blob.len == 0` clause: git cat-file of an EMPTY object
 * (e69de29..., the drive's runs/<N>/.started) SUCCEEDS (rc 0) and writes no
 * bytes, so the capture stays jc_sb_init's untouched state (data==NULL,
 * len==0) -- the legitimate empty read was mistaken for a read failure. ONE
 * helper so the two copies cannot drift apart again (the records side was
 * fixed in edb2d992; the M1056 side -- the SHIPPED 0.13.0 defect,
 * jc_harness_push.c:1506 -- had not been).
 *
 * `out` (caller's struct jc_sb) is initialised and receives the blob's bytes;
 * the caller frees it with jc_sb_free. `mirror` is the repo the blob is read
 * from (the M1056 mirror, or the records recs repo). `e_global` / `e_system`
 * are the "GIT_CONFIG_GLOBAL=<path>" / "GIT_CONFIG_SYSTEM=<path>" env-slot
 * strings (behaviour 8's isolation). `sha` is the 40-char object SHA.
 * `raw_rc` (may be NULL) receives the cat-file child's rc on a failure, so
 * the caller's `(cat-file rc=%d: %s)` refusal clause keeps its ORIGINAL
 * number, byte-identical to the per-loop idiom.
 *
 * Returns:
 *   0 = the blob was read (the bytes, whatever their length, are in *out).
 *       An EMPTY blob (len 0, data NULL) IS a valid read -- that is the fix:
 *       a 0-byte object cannot hold a secret, so the caller's secret check
 *       sees 0 bytes and is clean.
 *   1 = a genuine read failure (cat-file rc != 0, a spawn failure, or a
 *       timeout): the caller's existing fail-closed refusal, its message
 *       unchanged, prints the path and the child's output, never a secret.
 * On a refusal `*out` holds the child's stderr for the caller's `cut_copy`
 * detail. The cap is PUSH_BLOB_CAP (64 MiB). */
int jc_hp_blob_read(struct jc_sb *out, const char *mirror,
                    const char *e_global, const char *e_system,
                    const char *sha, int *raw_rc);

/* (a) The records range rule, exported (B1, notes/M1147.md): the unique
 * object OIDs reachable from the explicit tip ref `refs/heads/<prefix>
 * /records` in the recs repo, minus the remote's current tips (ls-remote of
 * the stored recs URL) and minus the recs repo's tracking refs -- M1056's
 * exact range rule ("a ref new to the remote counts everything"). Calls
 * M1056's build_exclusion (:1135) + list_pushed (:1311). Parameters:
 *   a              -- the arena owning the returned OID array.
 *   mirror         -- the recs repo (a plain non-bare repo, B1.1).
 *   tip_ref        -- the explicit tip, e.g. "refs/heads/<prefix>/records".
 *   stored_url     -- the recs stored origin URL (from
 *                     recs.git-config.harness-copy), the ls-remote target.
 *   e_global       -- the "GIT_CONFIG_GLOBAL=<path>" env-slot string.
 *   e_system       -- the "GIT_CONFIG_SYSTEM=<path>" env-slot string.
 *   git_token_file -- the git-token file PATH (build_exclusion's ls-remote
 *                     child's identity env).
 *   harness_path   -- the harness's own path (the credential.helper flag).
 *   out_shas       -- receives the arena-allocated pushed-object OID array.
 *   out_count      -- receives its length (0 when nothing is new).
 * Returns 0 on success (the list may be empty), -1 on a git / arena failure
 * (fail-closed: the caller refuses -- a scan that cannot run is never a pass,
 * M1056 behaviour 14). */
int jc_hp_records_scan(struct jc_arena *a, const char *mirror,
                       const char *tip_ref, const char *stored_url,
                       const char *e_global, const char *e_system,
                       const char *git_token_file, const char *harness_path,
                       char ***out_shas, int *out_count);

/* (b) The backlog step's delta, exported (B3, "both checks on"): the unique
 * object OIDs reachable from an explicit TIP commit, minus the current remote
 * main tip (the step's lower bound) plus list_pushed's `--not --remotes=
 * origin` (the remote's tracking refs) -- i.e. "reachable from the TIP and
 * not on the remote", the key scan + size check range for one fast-forward
 * step. Calls M1056's set_add (:907) + list_pushed (:1311). Parameters:
 *   a            -- the arena owning the returned OID array.
 *   mirror       -- the team repo (push.git / the mirror, B3).
 *   tip_commit   -- the TIP's 40-char SHA (a commit on refs/heads/<BR>/main).
 *   remote_main  -- the current remote <BR>/main tip's 40-char SHA (the
 *                   single ^-exclusion; after a fetch,
 *                   `git rev-parse refs/remotes/origin/<BR>/main`).
 *   e_global     -- the "GIT_CONFIG_GLOBAL=<path>" env-slot string.
 *   e_system     -- the "GIT_CONFIG_SYSTEM=<path>" env-slot string.
 *   out_shas     -- receives the arena-allocated pushed-object OID array.
 *   out_count    -- receives its length (0 when the TIP adds nothing).
 * Returns 0 on success (the list may be empty), -1 on a git / arena failure
 * (fail-closed). */
int jc_hp_tip_delta_scan(struct jc_arena *a, const char *mirror,
                         const char *tip_commit, const char *remote_main,
                         const char *e_global, const char *e_system,
                         char ***out_shas, int *out_count);

/* (c) The blob-version list + its parser, exported (M1147, H7; SV:0038).
 *
 * SV:0038: a secret scan kept in TWO copies drifts. b1's re-apply of the
 * records push duplicated M1056's blob-version machinery (its own
 * `struct blob_list` / `blob_push` / `parse_blobs`), because a1's exports
 * handed the range over as OIDs only and the records file re-listed the range
 * and parsed it with its own copies. The duplicate's `strcpy(p, path)` (in its
 * own blob_push) was the 00:27 sprintf_lint check-2 red -- M1056's strcpy is
 * allowlisted, the copy's was not. The fix is to export the ONE copy:
 *
 *   `struct jc_hp_blob_list` is M1056's former static `struct blob_list`
 *   (jc_harness_push.c), now defined HERE so both the M1056 `mirror push`
 *   key scan / size check and the records key scan name the same type.
 *   `jc_hp_parse_blobs` is M1056's `parse_blobs` (the `blob_push` it feeds
 *   stays M1056's static, its strcpy stays on the lint allowlist). The
 *   records scan DELETED its own struct / blob_push / parse_blobs and now
 *   parses its batch-check capture with the shared jc_hp_parse_blobs.
 *
 * A `struct jc_hp_blob_list` is a growing list of (sha, path, size) blob
 * versions parsed from a `cat-file --batch-check='%(objectname)
 * %(objecttype) %(objectsize) %(rest)'` capture. `a` owns every string the
 * list grows; the caller memsets the struct to 0, sets `bl.a`, and frees the
 * arena. Only `blob` lines of the capture become entries (a commit line is
 * `<oid> commit <size>` with no path, a tree's type is not `blob`). */
struct jc_hp_blob_list {
    char **shas;
    char **paths;
    unsigned long *sizes;
    int n;
    int cap;
    struct jc_arena *a;
};

/* Parse the batch-check capture into `bl` (the exported jc_hp_blob_list) --
 * M1056's `parse_blobs`. Returns 0 on success, -1 on an arena failure.
 * `text` is NUL-terminated; each line is `<sha> <type> <size> <path...>`
 * (the path is everything after the third field, and may itself hold spaces);
 * only `blob` lines are kept. `bl` must be zeroed and `bl.a` set by the
 * caller. The two key scans share this ONE parser so the blob-version
 * machinery cannot be kept in two copies (SV:0038). */
int jc_hp_parse_blobs(const char *text, struct jc_hp_blob_list *bl);

#ifdef __cplusplus
}
#endif
#endif /* JC_HARNESS_PUSH_H */
