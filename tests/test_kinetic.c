/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_kinetic.c - the pure kinetic shadow-matcher (M163a). */

#include "jc_test.h"
#include "jc_kinetic.h"

#include <string.h>

static void test_shell_match(void)
{
    const char *prefixes[3];
    const char *hit;
    prefixes[0] = "./motor.sh";
    prefixes[1] = "ros2 topic pub";
    prefixes[2] = "gpioset";

    /* Direct + path variants of a script prefix all hit (basename-tolerant). */
    JC_CHECK(jc_kinetic_shell_match("./motor.sh 1 1 0.5", prefixes, 3, &hit));
    JC_CHECK(hit != NULL && strcmp(hit, "./motor.sh") == 0);
    JC_CHECK(jc_kinetic_shell_match("/opt/robot/motor.sh 1", prefixes, 3, 0));
    JC_CHECK(jc_kinetic_shell_match("motor.sh", prefixes, 3, 0));

    /* Interpreter-invoked scripts resolve to the script word. */
    JC_CHECK(jc_kinetic_shell_match("sh ./motor.sh", prefixes, 3, 0));
    JC_CHECK(jc_kinetic_shell_match("bash -e /x/motor.sh", prefixes, 3, 0));

    /* env-prefixed + wrapper chains. */
    JC_CHECK(jc_kinetic_shell_match("env FOO=1 ./motor.sh", prefixes, 3, 0));
    JC_CHECK(jc_kinetic_shell_match("nohup motor.sh &", prefixes, 3, 0));

    /* Multi-token prefix: all prefix tokens must be present in order. */
    JC_CHECK(jc_kinetic_shell_match("ros2 topic pub /cmd_vel ...",
                                    prefixes, 3, 0));
    JC_CHECK(!jc_kinetic_shell_match("ros2 topic echo /odom", prefixes, 3, 0));
    JC_CHECK(!jc_kinetic_shell_match("ros2 node list", prefixes, 3, 0));

    /* A kinetic command hidden after a chain operator is still caught. */
    JC_CHECK(jc_kinetic_shell_match("echo hi && ./motor.sh 1", prefixes, 3, 0));
    JC_CHECK(jc_kinetic_shell_match("ls; gpioset gpiochip0 17=1",
                                    prefixes, 3, 0));

    /* Non-kinetic commands do not hit. */
    JC_CHECK(!jc_kinetic_shell_match("ls -la", prefixes, 3, 0));
    JC_CHECK(!jc_kinetic_shell_match("cat sensors.json", prefixes, 3, 0));
    /* A quoted mention is not an invocation. */
    JC_CHECK(!jc_kinetic_shell_match("echo './motor.sh'", prefixes, 3, 0));
    JC_CHECK(!jc_kinetic_shell_match("grep motor.sh log.txt", prefixes, 3, 0));

    /* Degenerate inputs. */
    JC_CHECK(!jc_kinetic_shell_match(NULL, prefixes, 3, 0));
    JC_CHECK(!jc_kinetic_shell_match("./motor.sh", NULL, 0, 0));
    JC_CHECK(!jc_kinetic_shell_match("", prefixes, 3, 0));
}

static void test_name_allowlist(void)
{
    const char *allow[2];
    allow[0] = "stop_all";
    allow[1] = " emergency_stop ";  /* trimmed */

    JC_CHECK(jc_kinetic_name_allowlisted("stop_all", allow, 2));
    JC_CHECK(jc_kinetic_name_allowlisted("emergency_stop", allow, 2));
    JC_CHECK(!jc_kinetic_name_allowlisted("drive_motor", allow, 2));
    /* Exact match only -- a prefix is not enough for a tool name. */
    JC_CHECK(!jc_kinetic_name_allowlisted("stop_all_now", allow, 2));
    JC_CHECK(!jc_kinetic_name_allowlisted("stop", allow, 2));
    JC_CHECK(!jc_kinetic_name_allowlisted(NULL, allow, 2));
    JC_CHECK(!jc_kinetic_name_allowlisted("stop_all", NULL, 0));
}

void test_kinetic(void)
{
    test_shell_match();
    test_name_allowlist();
}
