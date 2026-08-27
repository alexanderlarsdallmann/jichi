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
}
