# Localized documentation

jichi's documentation is localized here, one subdirectory per language:

- `en/` — English (the **canonical source**)
- `de/` — Deutsch
- `es/` — Español
- `ja/` — 日本語
- `zh/` — 中文 (简体)
- `ko/` — 한국어 (**partial**: `GETTING_STARTED.md` only, M523)

Each language directory mirrors the same basenames, so **adding a language is
purely mechanical** — create `docs/i18n/<lang>/` (plus a `presentations/`
subdir) and copy the English basenames in, translated. Current localized set per
language:

```
docs/i18n/<lang>/
  GETTING_STARTED.md        # onboarding
  PHILOSOPHY.md             # tracks ../../PHILOSOPHY.md
  JOURNEY.md                # tracks ../../JOURNEY.md
  PROJECT_TIMELINE.md       # tracks ../../PROJECT_TIMELINE.md
  presentations/
    00-super-features.md … 06-building-with-ai.md   # the Marp decks
```

**Coverage is uneven, and stating it is part of the policy.** `de/`, `es/`, `ja/`
and `zh/` carry the full set above; `ko/` carries `GETTING_STARTED.md` only. A
partial language is better than none and much better than four unreviewed pages
presented as complete — but a reader must be able to tell which they are looking
at, so the list above says so and every translated page carries the machine-draft
banner.

Practical CJK setup — terminal, font, IME, CJK filenames and identifiers, and how
to verify your own machine — is [../CJK.md](../CJK.md) (English).

The English canonical copies live at `docs/PHILOSOPHY.md`, `docs/JOURNEY.md`,
`docs/PROJECT_TIMELINE.md`, and `docs/presentations/` — **not** under `en/`.
`make slides` renders every localized deck into `docs/presentations/out/<lang>/`.

> **The program side:** the `language` config key makes the *agent answer* in
> your language, and the highest-traffic UI strings (the tool-approval prompt,
> the working indicator) are translated into five languages (Korean is
> documentation-only so far) — see
> [../LANGUAGE.md](../LANGUAGE.md) (M135/M137). Runtime strings follow this
> directory's phased policy: safety-surface first, reference English-canonical,
> machine drafts marked for native review.

## Policy (phased localization)

Translating and *maintaining* all **~285** English docs × 5 languages would
drift out of sync faster than it could be kept accurate — and that number has
grown from ~60 since this policy was written, which only sharpens the point. So
localization is **phased**:

1. **Structure + the onboarding & outreach set first.** The high-traffic,
   narrative material is translated into every language: getting-started
   (`GETTING_STARTED.md`), the philosophy and journey, and the outreach docs —
   the project timeline and the presentation decks. These are *narrative*, not
   safety-critical reference, so a translation lag is low-risk.
2. **Reference docs stay English-canonical.** The deep guides under `docs/*.md`
   remain in English; they're linked from the localized onboarding pages. This is
   deliberate — an out-of-date translation of a safety-critical reference is worse
   than an accurate English one.
3. **Contributions welcome.** To add or extend a translation, mirror `en/` into
   your language's directory and note, at the top of each file, the **source
   commit** it tracks (so reviewers can diff what changed upstream).

## Translation status & provenance

| Lang | Getting started | Philosophy | Journey | Timeline | Slides | Notes |
|------|---------|------------|---------|----------|--------|-------|
| en   | ✅ canonical | ✅ ([`../PHILOSOPHY.md`](../PHILOSOPHY.md)) | ✅ ([`../JOURNEY.md`](../JOURNEY.md)) | ✅ ([`../PROJECT_TIMELINE.md`](../PROJECT_TIMELINE.md)) | ✅ ([`../presentations/`](../presentations/)) | source of truth |
| de   | ✅ | ✅ | ✅ | ⚠️ figures behind | ⚠️ 4 decks short a slide | figures corrected M582; German prose reviewed by the maintainer |
| es   | ✅ | ✅ | ✅ | ⚠️ figures behind | ⚠️ 4 decks short a slide | figures corrected M582; prose machine-drafted, native review welcome |
| ja   | ⚠️ draft | ⚠️ draft | ⚠️ draft | ⚠️ draft, figures behind | ⚠️ draft, 4 decks short a slide | machine-drafted; native review welcome |
| zh   | ⚠️ draft | ⚠️ draft | ⚠️ draft | ⚠️ draft, figures behind | ⚠️ draft, 4 decks short a slide | machine-drafted; native review welcome |
| ko   | ⚠️ draft | — | — | — | — | `GETTING_STARTED.md` only, by design |

("Timeline" = `PROJECT_TIMELINE.md`; "Slides" = the seven decks under
`presentations/`.) The Philosophy / Journey / Timeline canonical copies live at
`docs/*.md` and the decks at `docs/presentations/`, not under `en/` — every
translation carries a `<!-- tracks: <relpath> @ <commit> -->` header.

**Charts in the timeline & slides** (mermaid + ASCII bars): translations render
the human-readable **titles, axis/legend labels, and table headers** but keep
**numbers, dates, `--<flag>` tokens, file paths, code, commit hashes, and
mermaid node IDs verbatim** — a corrupted flag would fail the docs↔flags lint.
Slide translations keep the **same slide count** as the English deck and must
not overflow the 16:9 frame (verify with `make slides`).

The ⚠️ drafts are honest about their status: they convey the content but a native
speaker should review the phrasing. Safety-relevant wording (constraints,
autonomy, budgets) should be checked especially carefully before relying on a
translation over the English source.

## Keeping translations in sync

When an `en/` page changes, bump the "tracks: <commit>" line at the top of each
translated copy **when you update it** — never before. A marker bumped ahead of
the content is worse than a stale one: it certifies a currency that does not
exist.

**This is now a gate, not a convention.** `tests/smoke/i18n_tracks_lint.sh` (M582)
checks four things, all by reading file content and never git history, so it gives
the same verdict here and in the published snapshot:

1. every translated page carries a well-formed `tracks:` marker;
2. the path that marker names resolves;
3. a deck carries the same number of slide separators as its English counterpart,
   **or** declares the gap with `<!-- slides-behind: N -->` where N is exact;
4. a translation introduces no figure of three digits or more that its English
   counterpart lacks, **or** declares the gap with `<!-- figures-behind: N -->`
   where N is exact.

Checks 3 and 4 are two-state on purpose, the shape `license_lint.sh` uses: a gap
may be **declared** instead of closed, but the declaration carries the count and
the count is verified — so it cannot be written once and forgotten. When the
English page moves again, the number stops matching and the gate fires.

**Why this exists.** Measured at M582: all forty commit-bearing markers were
behind, five files had no marker, and four decks had been telling a German,
Spanish, Japanese and Chinese audience that the binary is ~700 KB (measured:
1.2 MB size-optimized, 1.7 MB as built) and that the test suite has 7,170 checks
(measured: 12,960). The convention above had been written down since the
translations were created. Nothing checked it, so nothing happened.
