/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_version.h - THE version of jichi (M178). Single source of truth: both
 * binaries (`jichi -V`, `jichi-convert -V`), the `describe` interface
 * contract, and `doctor`'s header print this string; nothing else defines
 * one.
 *
 * Scheme: Semantic Versioning, pre-1.0.
 *   - MINOR bumps for a completed capability cluster or any breaking change
 *     (config keys, CLI, wire/JSONL contracts); PATCH bumps for fixes.
 *   - THE FIRST PUBLIC RELEASE IS PRE-1.0 (decided 2026-08-19). This comment
 *     used to reserve 1.0.0 for it, and CHANGELOG.md said the same -- but 1.0
 *     is a claim about interface stability, and the honest moment to make it
 *     is after the interfaces have survived contact with someone else's
 *     machine, not on the day they first meet one. docs/EMBEDDING.md's four
 *     stability tiers are what a first release actually promises.
 *   - So the release commit bumps this constant to the next MINOR and retitles
 *     the CHANGELOG's [Unreleased] section to match. Note that section
 *     currently carries M327 onward ABOVE a 0.9.0 heading dated 2026-07-28, so
 *     tagging the literal v0.9.0 would date 150-odd milestones into a released
 *     section; the number to pick is the next one, not the current one.
 *   - A release = bump this constant + retitle the CHANGELOG's [Unreleased]
 *     section, in the same commit. The changelog (/CHANGELOG.md) is the
 *     user-facing record; docs/ROADMAP.md stays the engineering log.
 *
 * Versions 0.1.0-0.8.0 are retrospective labels over the milestone bands
 * (never tagged at the time); 0.9.0 is the first version stamped when it
 * was current. See CHANGELOG.md's honesty note.
 */
#ifndef JC_VERSION_H
#define JC_VERSION_H


#ifdef __cplusplus
extern "C" {
#endif
#define JC_VERSION "0.9.0"

#ifdef __cplusplus
}
#endif
#endif /* JC_VERSION_H */
