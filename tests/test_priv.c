/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_priv.c - the privileged-launcher detector (M152).
 *
 * Table-driven: the cases jc_priv_detect MUST catch, the ordinary commands it
 * must NOT flag, and the obfuscations it is documented NOT to catch (encoded
 * here as expected misses, so a future change that "fixes" one is a conscious
 * decision, not an accident). */

#include "jc_test.h"
#include "jc_priv.h"

#include <string.h>

static void hit(const char *cmd, enum jc_priv_kind want)
{
    const char *tok = NULL;
    enum jc_priv_kind got = jc_priv_detect(cmd, &tok);
    JC_CHECK(got == want);
    if (want != JC_PRIV_NONE) {
        JC_CHECK(tok != NULL);                 /* the token is located */
    } else {
        JC_CHECK(tok == NULL);                 /* NONE => no token */
    }
}

void test_priv(void)
{
    /* The incident and its relatives. */
    hit("sudo apt-get update && sudo apt-get upgrade", JC_PRIV_SUDO);
    hit("sudo apt-get update && apt-get upgrade", JC_PRIV_SUDO);
    hit("apt-get update && sudo apt-get upgrade", JC_PRIV_SUDO); /* 2nd segment */
    hit("sudo -E make install", JC_PRIV_SUDO);
    hit("sudo -u root whoami", JC_PRIV_SUDO);
    hit("echo hi | sudo tee /etc/hosts", JC_PRIV_SUDO);          /* pipe segment */
    hit("make && sudo make install ; echo done", JC_PRIV_SUDO);

    /* Env assignments and transparent wrappers before the launcher. */
    hit("FOO=bar sudo systemctl restart x", JC_PRIV_SUDO);
    hit("env DEBIAN_FRONTEND=noninteractive sudo apt-get install -y x",
        JC_PRIV_SUDO);
    hit("nohup sudo long-job", JC_PRIV_SUDO);
    hit("time sudo make", JC_PRIV_SUDO);

    /* M1091 (B-05): a wrapper's OWN option must be skipped, and a launcher
     * named by an absolute path must be matched by its final component. */
    hit("nice -n5 sudo x", JC_PRIV_SUDO);          /* wrapper takes a value */
    hit("timeout 5 sudo x", JC_PRIV_SUDO);          /* wrapper takes a value */
    hit("nice -n1 doas x", JC_PRIV_DOAS);           /* doas, not sudo */
    hit("/usr/bin/sudo x", JC_PRIV_SUDO);           /* path-qualified launcher */
    hit("/usr/bin/doas pkg_add", JC_PRIV_DOAS);     /* path-qualified, doas */
    hit("nice -n5 sudo rm -rf /x", JC_PRIV_SUDO);   /* the incident's shape */

    /* M1091-b (SV:2258): `command -v` / `command -V` is a shell NAME LOOKUP
     * -- it prints where the name would resolve, it does not RUN the name, so
     * nothing privileged happens and the gate must not fire (auto/delegate
     * posture used to refuse these as delegate_refused). */
    hit("command -v sudo", JC_PRIV_NONE);
    hit("command -V sudo", JC_PRIV_NONE);
    hit("command -v sudo >/dev/null && echo yes", JC_PRIV_NONE);
    hit("builtin command -v sudo", JC_PRIV_NONE);
    /* ...but `command` WITHOUT the -v option DOES run the command, so the
     * launcher must still be flagged. */
    hit("command -p sudo x", JC_PRIV_SUDO);
    hit("command sudo x", JC_PRIV_SUDO);

    /* The other launchers. */
    hit("doas pkg_add curl", JC_PRIV_DOAS);
    hit("pkexec /usr/bin/whatever", JC_PRIV_PKEXEC);
    hit("su - root -c 'id'", JC_PRIV_SU);
    hit("run0 systemctl restart x", JC_PRIV_RUN0);
    hit("SUDO apt-get update", JC_PRIV_SUDO); /* case-insensitive launcher */

    /* Ordinary commands must NOT flag. */
    hit("apt-get update", JC_PRIV_NONE);
    hit("make && make test", JC_PRIV_NONE);
    hit("git commit -m 'wip'", JC_PRIV_NONE);
    hit("echo 'run sudo later'", JC_PRIV_NONE);        /* sudo only in a string */
    hit("echo \"please sudo this\"", JC_PRIV_NONE);    /* sudo in a dq string */
    hit("./sudoku --solve", JC_PRIV_NONE);             /* substring, not a word */
    hit("pseudo-tty-tool", JC_PRIV_NONE);
    hit("grep sudo /etc/log", JC_PRIV_NONE);           /* sudo is an argument */
    hit("SUDO_ASKPASS=/x askpass-helper", JC_PRIV_NONE); /* assignment only */
    hit("", JC_PRIV_NONE);
    hit(NULL, JC_PRIV_NONE);

    /* Documented NON-catches (heuristic limits) -- asserted as misses so the
     * contract is explicit. Defeating these needs the deferred shell sandbox. */
    hit("sh -c 'sudo apt-get update'", JC_PRIV_NONE);  /* inner script not descended */
    hit("S=sudo; $S apt-get update", JC_PRIV_NONE);    /* variable indirection */

    /* M1091-b (SV:2258): one level of obfuscation deeper than the two named
     * B-05 forms (a wrapper's SIMPLE option, an absolute path to a LAUNCHER).
     * These need a per-wrapper option/value grammar (timeout -s/-k, exec -a)
     * or path-qualified wrapper/env matching (/usr/bin/env, /usr/bin/nice) --
     * the over-fitting the B-05 note rejects ("a lexer can never be complete";
     * def-149 is the real fence). Documented misses, not defects: a later
     * change that catches one of these is a conscious decision. */
    hit("timeout -s KILL 5 sudo x", JC_PRIV_NONE);
        /* -s takes a value; KILL is read as the duration, 5 as the command */
    hit("timeout -k 5 10 sudo x", JC_PRIV_NONE);
        /* same through -k */
    hit("exec -a name sudo x", JC_PRIV_NONE);
        /* exec -a takes a value */
    hit("/usr/bin/env sudo x", JC_PRIV_NONE);
        /* path-qualified env is not recognised as env */
    hit("/usr/bin/nice -n5 sudo x", JC_PRIV_NONE);
        /* path-qualified nice is not recognised as a wrapper */

    /* kind_name round-trips. */
    JC_CHECK(strcmp(jc_priv_kind_name(JC_PRIV_SUDO), "sudo") == 0);
    JC_CHECK(strcmp(jc_priv_kind_name(JC_PRIV_PKEXEC), "pkexec") == 0);
    JC_CHECK(jc_priv_kind_name(JC_PRIV_NONE)[0] == '\0');

    /* M153: the operator prefix-allowlist, chain-safe. */
    {
        static const char *const allow[] = {
            "sudo systemctl restart myapp", "sudo journalctl"
        };
        int n = 2;
        /* Exact and prefix matches are allowed. */
        JC_CHECK(jc_priv_allowlisted("sudo systemctl restart myapp",
                                     allow, n) == 1);
        JC_CHECK(jc_priv_allowlisted("sudo journalctl -u x -b", allow, n) == 1);
        JC_CHECK(jc_priv_allowlisted("  sudo journalctl -f", allow, n) == 1);
        /* A different command is not allowlisted. */
        JC_CHECK(jc_priv_allowlisted("sudo rm -rf /", allow, n) == 0);
        /* Prefix must end on a word boundary (no `journalctlx`). */
        JC_CHECK(jc_priv_allowlisted("sudo journalctlx", allow, n) == 0);
        /* Chaining past the allowed prefix is refused (the key guard). */
        JC_CHECK(jc_priv_allowlisted(
            "sudo systemctl restart myapp ; sudo rm -rf /", allow, n) == 0);
        JC_CHECK(jc_priv_allowlisted(
            "sudo journalctl && sudo shutdown now", allow, n) == 0);
        /* Empty / NULL allowlist => nothing allowed. */
        JC_CHECK(jc_priv_allowlisted("sudo journalctl", NULL, 0) == 0);
        JC_CHECK(jc_priv_allowlisted("sudo journalctl", allow, 0) == 0);
    }

    /* M821 (N9): jc_env_block_has_name reads a /proc/<pid>/environ block --
     * NUL-separated NAME=VALUE entries -- and matches an entry's NAME exactly,
     * never its value. */
    {
        /* "JICHI_API_KEY=sekret\0PATH=/usr/bin\0LANG=C\0" */
        static const char blk[] =
            "JICHI_API_KEY=sekret\0PATH=/usr/bin\0LANG=C";
        jc_size n = (jc_size)(sizeof blk - 1);   /* drop the implicit final NUL */
        JC_CHECK(jc_env_block_has_name(blk, n, "JICHI_API_KEY") == 1);
        JC_CHECK(jc_env_block_has_name(blk, n, "PATH") == 1);
        JC_CHECK(jc_env_block_has_name(blk, n, "LANG") == 1);
        /* A prefix or suffix of a real NAME is not a match. */
        JC_CHECK(jc_env_block_has_name(blk, n, "JICHI_API") == 0);
        JC_CHECK(jc_env_block_has_name(blk, n, "API_KEY") == 0);
        JC_CHECK(jc_env_block_has_name(blk, n, "PAT") == 0);
        /* The NAME is never matched inside a VALUE (the security-relevant case):
         * a variable whose VALUE happens to be the key's name must not trip. */
        {
            static const char vblk[] = "OTHER=JICHI_API_KEY";
            JC_CHECK(jc_env_block_has_name(vblk, (jc_size)(sizeof vblk - 1),
                                           "JICHI_API_KEY") == 0);
        }
        /* Degenerate inputs. */
        JC_CHECK(jc_env_block_has_name(NULL, 10, "X") == 0);
        JC_CHECK(jc_env_block_has_name(blk, n, NULL) == 0);
        JC_CHECK(jc_env_block_has_name(blk, n, "") == 0);
        JC_CHECK(jc_env_block_has_name(blk, 0, "PATH") == 0);
        /* A malformed entry with no '=' is not a name. */
        {
            static const char noeq[] = "JUST_A_WORD";
            JC_CHECK(jc_env_block_has_name(noeq, (jc_size)(sizeof noeq - 1),
                                           "JUST_A_WORD") == 0);
        }
    }
}
