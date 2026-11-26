/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_park.h - the park / restart verbs (M1213 T3, H12;
 * batch H12, drive of 2026-10-27 hardening-host drive's park-resume.sh
 * and restart-loop.sh; 9 cells). */

#ifndef JC_HARNESS_PARK_H
#define JC_HARNESS_PARK_H

/* Validate numeric flag: all digits, no negative. Returns 0 if valid, 2 if invalid. */
int park_validate_numeric(const char *val, const char *name);

int jc_harness_park_run(const char *config_path,
                        const char *state_path, const char *out_path,
                        int park_parked, int park_interval,
                        int park_wait_interval, int park_ready_interval,
                        int park_not_ready_interval, const char *park_post_start,
                        void *a);

int jc_harness_restart_run(const char *config_path,
                           const char *state_path, const char *out_path,
                           int restart_wait_interval, const char *restart_post_start,
                           void *a);

#endif /* JC_HARNESS_PARK_H */
