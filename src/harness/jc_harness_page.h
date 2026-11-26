/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_page.h - the page / page-watch / probe verbs (M1213 T2, H12;
 * batch H12, drive of 2026-10-27 hardening-host drive's page-watch.sh,
 * 25 cells; SV:0433's option (a), page stretch S1). */

#ifndef JC_HARNESS_PAGE_H
#define JC_HARNESS_PAGE_H

int jc_harness_page_run(const char *config_path,
                        const char *state_path, const char *out_path,
                        long now, int after_s, int fresh_s,
                        int park,
                        void *a);

int jc_harness_page_watch_run(const char *config_path,
                              const char *state_path, const char *out_path,
                              long now, int after_s, int fresh_s,
                              int interval,
                              void *a);

int jc_harness_probe_run(const char *config_path,
                         const char *model, int probe, int model_probe,
                         int gateway_probe,
                         void *a);

#endif /* JC_HARNESS_PAGE_H */
