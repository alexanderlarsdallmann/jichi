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
     * formatters that take the path mid-expression need. */
    JC_CHECK(jc_fmtcmd_build("Rscript -e 'styler::style_file(\"{}\")'",
                             "R/x.R", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "Rscript -e 'styler::style_file(\"'R/x.R'\")'") == 0);

    /* Every occurrence, not just the first. */
    JC_CHECK(jc_fmtcmd_build("cp {} {}.bak", "a.c", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "cp 'a.c' 'a.c'.bak") == 0);

    /* The path is quoted in the placeholder form too. */
    JC_CHECK(jc_fmtcmd_build("fmt {}", "a.c; id", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "fmt 'a.c; id'") == 0);

    /* The quoting pitfall worth pinning: {} expands to a SINGLE-quoted path, so
     * an expression wrapped in single quotes concatenates correctly (the R and
     * elisp pack examples), while one wrapped in DOUBLE quotes would embed the
     * quotes literally and hand the formatter a filename that does not exist.
     * Documented in docs/FORMATTING.md; asserted here so the shape is fixed. */
    JC_CHECK(jc_fmtcmd_build("r -e 'f(\"{}\")'", "a.R", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "r -e 'f(\"'a.R'\")'") == 0);   /* concatenates -> f("a.R") */
    JC_CHECK(jc_fmtcmd_build("r -e \"f('{}')\"", "a.R", b, sizeof b) == 0);
    JC_CHECK(strcmp(b, "r -e \"f(''a.R'')\"") == 0);   /* literal quotes: the trap */

    /* Rejections: nothing to run, nothing to format, no room. */
    JC_CHECK(jc_fmtcmd_build(NULL, "a.c", b, sizeof b) == -1);
    JC_CHECK(jc_fmtcmd_build("", "a.c", b, sizeof b) == -1);
    JC_CHECK(jc_fmtcmd_build("fmt", NULL, b, sizeof b) == -1);
    JC_CHECK(jc_fmtcmd_build("fmt", "", b, sizeof b) == -1);
    JC_CHECK(jc_fmtcmd_build("clang-format -i", "src/a.c", b, 8) == -1);
    JC_CHECK(jc_fmtcmd_build("fmt {}", "src/a.c", b, 8) == -1);
}
