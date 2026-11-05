# JICHI DECISIONS.md — gap analysis (analysis 50)

## TL;DR

*Read-only survey of docs/DECISIONS.md at HEAD 1b71247d (3,527 lines), 2026-10-05. The register's own integrity machinery is strong and I verified most of it: the M628 supersede lint (tests/smoke/decisions_superseded_lint.sh) runs green, all 401 distinct detail anchors resolve, all 14 proposal links point to real files, and the M293-coverage + honest-counts preamble is intact.*

*The gap is in the one property a decision REGISTER most needs: unambiguous findability. Fourteen <a id> values are each defined twice, so 28 in-register links are ambiguous — every one resolves to the FIRST occurrence, and for half of them that is the wrong decision. The register silently misdirects a reader looking up a decision by its own index.*

## 1. What is verified true

- **M628 supersede lint runs green.** `decisions_superseded_lint.sh`: "ok 1 - 0 superseded marker(s), each in the form **Superseded at M<n>**" and "ok 2 - every superseding milestone has a ROADMAP entry". The only "Superseded at M" string in the file is the convention's example (line 28, prose — correctly excluded by the lint's `^| ` table-row grep).
- **All 401 distinct detail anchors resolve** to a defined `<a id>` (415 ids total, 401 unique) or a generated header.
- **All 14 `proposals/*.md` links point to real files.**
- **Honest-counts preamble intact**: "over 400 entries and over 35 proposals deep", "(Both counts are stated as bounds on purpose...)". 424 decision rows at HEAD — within the "over 400" bound.
- Section structure stable: Behaviour (37) / Safety (698) / Interfaces (1769) / Documentation (2052) / Testing (2362) / Platforms (3480).

## 2. The gap: 14 duplicated anchor IDs, 28 ambiguous links

`<a id="dec-m296">` is defined at both line 1975 ("The prompt keeps the tier, not the model id") and line 2561 ("Transcript assertions must be anchored on something unique"). Both are M296 decisions, in two different sections. The register's IDs are **keyed on milestone number only**, with a per-section `-2/-3/-4` suffix — but the suffix restarts in each section, so a milestone with decisions in two sections collides.

The full set (each x2): dec-m296, dec-m298, dec-m305, dec-m309, dec-m310, dec-m312, dec-m326s, dec-m326s-2, dec-m342, dec-m466, dec-m469, dec-m657, dec-m657-2, dec-m658.

The worst case is **M326s**: 4 decisions in "Documentation and teaching" (lines 2279-2297, ids dec-m326s … -4) **and** 2 in "Testing" (lines 2573-2580, ids dec-m326s, dec-m326s-2). The disambiguation suffix was applied, and it did not disambiguate — `dec-m326s-2` now points at two different probe/records decisions.

## 3. The consequence, measured

In-register links targeting a duplicated id: **28** (14 ids x 2 links each). Browser semantics resolve an in-document link to the *first* occurrence. Mapping each link to the definition in its own section:

- **14 links** target the first def → land correctly.
- **14 links** target the second def → **silently land on the first (wrong) decision**.

Example: the Testing-section one-liner for "A probe that mutates the buffer runs on a scratch..." links `#dec-m326s-2`; it resolves to the *Documentation* "records pages are ungraded reading" decision. A reader using the register's index to find a decision can be shown a different, equally-plausible decision with no indication anything went wrong.

## 4. Why no lint catches it

- `decisions_superseded_lint.sh` only checks the supersede-marker format (it is a targeted M628 lint, not a general anchor lint).
- The lints that open DECISIONS.md (`docs_table_rows_lint.sh`, `docs_counts_lint.sh`, `project_records_lint.sh`, etc.) check row width, count bounds, and record linkage — none check `<a id>` uniqueness or link resolution.
- `reading_refs_lint.sh` (which resolves backticked `file:function` refs) is scoped to `docs/reading/` only.
- The `<a id>` scheme is **not documented** in the register's own "How to add a row" section — so a contributor adding a second decision for an already-covered milestone has no rule telling them to extend the suffix *globally* rather than per-section.

## 5. The fix (small, mechanical)

1. **Document the ID scheme** in "How to add a row": `<a id="dec-m<NNN>[-k]">` where k is the 1-based ordinal of that milestone's Nth decision *across the whole file*, not per section.
2. **Renumber** the 14 colliding ids (their later occurrences) and update the 28 links. Mechanical — a script keyed on milestone number + global ordinal can do it, and it can be run under git to review every change.
3. **Add a lint** (extends `decisions_superseded_lint.sh` or a new `decisions_anchor_lint.sh`): "every `<a id>` in DECISIONS.md is unique" + "every `](#dec-...)` link resolves to exactly one defined id". The second property is the one that would have caught this; the first is cheap and prevents regressions.

No behavior change to the register's content, structure, or supersede convention. Pure index hygiene.

## 6. Blast radius + verdict

No external document links any of the 14 ambiguous ids (verified across docs/), so the misdirection is confined to the register's own one-liner index — which is exactly the surface a reader trusts for a 3,500-line document.

**Verdict: one real integrity gap, mechanical to fix.** DECISIONS.md is the strongest self-documenting register in the tree (its supersede lint, anchor resolution, and honest-counts preamble all hold), and it fails on the single property its stated purpose requires — that a decision found by the index is *the* decision. 14/28 in-register links (all of them the second-occurrence case) resolve to a different same-milestone decision. Root cause: the id is keyed on milestone number with a per-section ordinal that restarts across sections. Fix = document the global-ordinal scheme, renumber 14 ids + 28 links (scriptable, reviewable under git), and add a one-line uniqueness+resolution lint that would have caught this. No content, structure, or convention changes.
