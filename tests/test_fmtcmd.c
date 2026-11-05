/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_fmtcmd.c - the formatCommand shell builder (M263).
 *
 * The interesting cases are all adversarial: the path is model-supplied, so a
 * filename containing shell metacharacters must come out inert.
 */

#include "jc_test.h"
#include "jc_fmtcmd.h"

#include <string.h>

void test_fmtcmd(void)
{
    char b[512];

    /* ---- quoting -------------------------------------------------------- */
    JC_CHECK(jc_shell_quote("src/a.c", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "'src/a.c'") == 0);

    /* Spaces need no escaping inside single quotes -- only wrapping. */
    JC_CHECK(jc_shell_quote("my file.c", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "'my file.c'") == 0);

    /* Metacharacters are literal inside single quotes. */
    JC_CHECK(jc_shell_quote("a.c; rm -rf ~", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "'a.c; rm -rf ~'") == 0);
    JC_CHECK(jc_shell_quote("$(id).c", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "'$(id).c'") == 0);
    JC_CHECK(jc_shell_quote("`id`.c", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "'`id`.c'") == 0);

    /* The one character that cannot be quoted by wrapping: a single quote.
     * It must close, escape, and reopen -- otherwise the quoting is escapable
     * and everything above is worthless. */
    JC_CHECK(jc_shell_quote("it's.c", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "'it'\\''s.c'") == 0);
    JC_CHECK(jc_shell_quote("'; id; '", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "''\\''; id; '\\'''") == 0);

    /* Bounds: no partial write, no unterminated quote. */
    JC_CHECK(jc_shell_quote("abc", b, 4) == -1);
    JC_CHECK(jc_shell_quote(NULL, b, sizeof b) == -1);

    /* ---- command composition -------------------------------------------- */
    JC_CHECK(jc_fmtcmd_build("clang-format -i", "src/a.c", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "clang-format -i 'src/a.c'") == 0);

    /* A {} placeholder is substituted rather than appended -- the form
     * formatters that take the path mid-expression need. Safe only OUTSIDE the
     * author's quotes (M847); the once-shipped inside-quotes R form is refused
     * below. */
    JC_CHECK(jc_fmtcmd_build("prettier --write {} --loglevel warn",
                             "R/x.R", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "prettier --write 'R/x.R' --loglevel warn") == 0);

    /* Every occurrence, not just the first. */
    JC_CHECK(jc_fmtcmd_build("cp {} {}.bak", "a.c", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "cp 'a.c' 'a.c'.bak") == 0);

    /* The path is quoted in the placeholder form too. */
    JC_CHECK(jc_fmtcmd_build("fmt {}", "a.c; id", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "fmt 'a.c; id'") == 0);

    /* M847 (S1): a `{}` inside the author's own quotes is REFUSED (-2). The
     * substituted path is single-quoted, so inside a quoted span its opening
     * quote closes the author's and the path spills into the shell -- a space
     * splits it, a `;` runs. This was the R and elisp pack shape, and
     * FORMATTING.md taught it. Both quote kinds are refused; `{}` outside
     * quotes is still fine. */
    JC_CHECK(jc_fmtcmd_build("r -e 'f(\"{}\")'", "a.R", b, sizeof b) == -2);
    JC_CHECK(jc_fmtcmd_build("r -e \"f('{}')\"", "a.R", b, sizeof b) == -2);
    /* The ordinary break the refusal prevents: a path with a SPACE in the old
     * pack shape would have fallen out of the author's quotes. Refused. */
    JC_CHECK(jc_fmtcmd_build("r -e 'f(\"{}\")'", "my file.R", b, sizeof b) == -2);
    /* `{}` OUTSIDE quotes stays supported and safe: the quoted path is a single
     * shell word even with a space or a metacharacter. */
    JC_CHECK(jc_fmtcmd_build("fmt {} --write", "my file.c", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "fmt 'my file.c' --write") == 0);
    JC_CHECK(jc_fmtcmd_build("fmt {}", "a; id", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "fmt 'a; id'") == 0);
    /* A quote the author opened and CLOSED before `{}` leaves `{}` outside. */
    JC_CHECK(jc_fmtcmd_build("echo 'hi' && fmt {}", "a.c", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "echo 'hi' && fmt 'a.c'") == 0);
    /* The trailing form (no `{}`) is unaffected and the recommended shape. */
    JC_CHECK(jc_fmtcmd_build(
        "Rscript -e 'styler::style_file(commandArgs(trailingOnly=TRUE))'",
        "my file.R", b, sizeof b) == 0);
    JC_CHECK(strcmp(b,
        "Rscript -e 'styler::style_file(commandArgs(trailingOnly=TRUE))' "
        "'my file.R'") == 0);

    /* Rejections: nothing to run, nothing to format, no room. */
    JC_CHECK(jc_fmtcmd_build(NULL, "a.c", b, sizeof b) == -1);
    JC_CHECK(jc_fmtcmd_build("", "a.c", b, sizeof b) == -1);
    JC_CHECK(jc_fmtcmd_build("fmt", NULL, b, sizeof b) == -1);
    JC_CHECK(jc_fmtcmd_build("fmt", "", b, sizeof b) == -1);
    JC_CHECK(jc_fmtcmd_build("clang-format -i", "src/a.c", b, 8) == -1);
    JC_CHECK(jc_fmtcmd_build("fmt {}", "src/a.c", b, 8) == -1);
}
