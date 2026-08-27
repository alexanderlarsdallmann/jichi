/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_fmtcmd.h - build the shell command for the configured `formatCommand`.
 *
 * jichi formats through a language server (textDocument/formatting). Languages
 * with no LSP formatter -- Emacs Lisp, Racket, shell, plenty of others -- had no
 * path at all, so `formatCommand` gives `format_file` a second backend: a shell
 * command that rewrites a file in place.
 *
 * It is a SHELL string, not an argv, deliberately: that is what `testCommand`,
 * `verify` and `notify` already are, and real formatter invocations need shell
 * quoting (`Rscript -e 'styler::style_file'`). The cost of that choice is that
 * the file path -- which the MODEL supplies -- is interpolated into a string the
 * shell will parse. So quoting it is not a nicety here; a path is a filename,
 * and a filename may legally contain `;`, `$(...)`, or a quote. Both functions
 * below are pure and unit-tested for exactly that.
 */
#ifndef JC_FMTCMD_H
#define JC_FMTCMD_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"

/* Single-quote `s` for /bin/sh, writing to `out` (capacity `cap`, always
 * NUL-terminated on success). Every character inside single quotes is literal
 * to the shell except `'` itself, which is emitted as the standard `'\''`
 * (close, escaped quote, reopen). Returns 0 on success, -1 if `s` is NULL or
 * the result would not fit. */
int jc_shell_quote(const char *s, char *out, jc_size cap);

/* Compose the command that formats `path` with `cmd` (the configured
 * `formatCommand`), writing to `out` (capacity `cap`).
 *
 *   - `cmd` containing `{}`  -> every occurrence is replaced by the quoted path
 *     (`Rscript -e 'styler::style_file("{}")'`);
 *   - otherwise              -> the quoted path is appended as a final argument
 *     (`clang-format -i` becomes `clang-format -i 'src/a.c'`).
 *
 * The path is quoted in both forms. Returns 0 on success, -1 on NULL/empty
 * input or overflow. Pure. */
int jc_fmtcmd_build(const char *cmd, const char *path, char *out, jc_size cap);

#ifdef __cplusplus
}
#endif
#endif /* JC_FMTCMD_H */
