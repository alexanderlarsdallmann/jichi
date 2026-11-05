# JICHI man-page documentation — gap analysis

*Read-only survey of `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) at HEAD
`1b71247d` (2026-10-04). The one thing the man page definitely lacks: its
ENVIRONMENT section is the only prose surface with no parity lint — M539's
gate covers flags, subcommands, and exit codes, but of the 12 project
env-var knobs the code reads, the man page names only 5, so seven
`JICHI_*`/`JC_*` knobs
(`JICHI_SHELL`, `JICHI_SPAWN_BACKEND`, `JICHI_LANG`,
`JICHI_DUMP_REQUESTS`, `JICHI_DAEMON_SOCK`, `JC_PROVIDER`,
`JC_CONTROL_CLIENT_SECS`) live in the binary with no entry in the one
namespace whose whole job is to be the stable reference. Second surface,
same root: the 27 "default …" behavioral sentences in OPTIONS are
name-checked, not truth-checked — the lint proves every documented flag
*exists*, never that the documented default *matches*.*

## TL;DR

The man page is the register's **most-guarded** namespace, and the honest
headline starts there: the "man page is stale" gap was already **closed by
M539** — `tests/smoke/man_page_lint.sh` is a seven-check parity gate whose
own header records the incident it exists for ("documented 77 of the 148
long flags — 49% — and 38 of the 55 subcommands, and advertised one command,
`session`, that does not exist", plus a dropped exit code), and the roff
traps it hit ("`\-` truncates `\-\-budget\-tokens` to `--budget`… recurred
**THREE** times in one session") are written into the lint as standing
cautions. A derivation aid (`scripts/man-options-from-help.sh`, "an AID,
not a build step") removes the 148-typo transcription class.

What M539's seven checks **deliberately do not reach** is the seam:

1. **The ENVIRONMENT section has no parity check at all**
   (`grep -ic environment man_page_lint.sh` → 0). The code reads **18** env
   vars; the man page's ENVIRONMENT section names **5** (JC_CONFIG,
   NO_COLOR, JICHI_KEEP_DUMPABLE, ANTHROPIC_API_KEY, OPENAI_API_KEY) plus
   the standard POSIX/locale class it leaves implicit. Seven `JICHI_*`/
   `JC_*` knobs are invisible to the one artifact whose contract is "the
   stable interface": three are the **env side of flags the page's own
   OPTIONS documents** (`--lang`↔`JICHI_LANG`,
   `--dump-requests`↔`JICHI_DUMP_REQUESTS`, `--provider`↔`JC_PROVIDER` —
   all three flags exist in the parser; all three env vars undocumented),
   and four are **env-only** knobs with no flag form at all (`JICHI_SHELL`,
   `JICHI_SPAWN_BACKEND`, `JICHI_DAEMON_SOCK`, `JC_CONTROL_CLIENT_SECS` —
   `--shell` and `--spawn` are not in the parser). Some are documented
   *elsewhere* (`JICHI_SPAWN_BACKEND` in 5 doc files) but the man page is
   the reference a user is sent to, and it omits them.

2. **Behavioral prose is name-checked, not truth-checked.** The man page
   makes 27 testable claims ("default: auto; honors NO_COLOR", "default 3",
   "default ask; config `privilegedCommands`"). The lint verifies the flag
   *name* is on both sides; nothing verifies the *sentence* says what the
   code does. The OPTIONS text is derived from `--help` (main.c literals —
   four spot-checks match verbatim today), so the realistic drift path is a
   main.c default changing with its help sentence but the man page's copy
   going stale — undetectable by any current check.

The fix is small and in the register's exact spirit (extend the parity gate
one column): an env-var presence check (code's `getenv` set ↔ man's
ENVIRONMENT `.B` set, POSIX-standard vars exempt by a named allowlist) and a
help-text↔man-page sentence diff for OPTIONS — the `--help` text is already
the reviewed source the lint extracts flags from, so the sentence parity is
free.

## 1. What exists — the strongest documentation guard in the register

- **A real man page, `man/jichi.1`** (803 lines, roff, dated `.TH` — the
  lint enforces the date). Hand-maintained prose: DESCRIPTION, INTERACTIVE
  KEYS, ENVIRONMENT, FILES, EXIT STATUS — "that no generator could write"
  (man-options-from-help.sh:5-6). Installed by `make install`
  (Makefile:1159).
- **`tests/smoke/man_page_lint.sh` (M539)** — seven checks: (1) parser flags
  extracted from every `strcmp(a, "--x")` (floor 148), (2) man flags
  extracted (de-roffed first), (3) subcommands from `--help`'s command
  column (floors 55/55, "NOT a grep of main.c" — the lint's header records
  that trap costing `undo` and `rewind`), (4) every parser flag documented,
  (5) **no phantom flags** (the inverse — a documented flag the parser
  rejects; the header notes an "allowlist of flags allowed to stay
  undocumented … turned out to be unnecessary — writing all 148 was one
  pass"), (6) no invented subcommands, (7) exit codes match `describe`
  (0/1/2/130/143 — "143 was missing"). It builds the binary and runs
  `--help`, so it is a *behavioral* parity gate, not a text grep.
- **The roff-escaping trap, institutionalized** (man_page_lint.sh:32-37):
  "a pattern matched against the raw source truncates `\-\-budget\-tokens`
  to `--budget`. That produced a first measurement of '61 documented, 17
  phantoms' where the truth was 77 and 0, and it recurred THREE times in
  one session — once in this lint, once measuring for the ROADMAP, once
  floor-checking the generator." Every extraction "de-roffs FIRST and
  matches second." This is the register's standard move: a bug that bit it
  becomes a caution written where the next person works.
- **`scripts/man-options-from-help.sh`** — a derivation aid that reads
  `--help`'s own four section headings and emits `.TP` blocks, "so the man
  page inherits the grouping a human already curated". Explicitly *not*
  wired into `make` — "then EDIT the result."
- **The sentence-level source is shared**: four spot-checks of man-page
  OPTIONS prose against `--help` literals in main.c match verbatim
  ("print the reply and exit", "honors NO_COLOR", "bounds an unsupervised
  run", the json/jsonl line) — the man page *copied* the reviewed help text,
  which is exactly why sentence parity is the natural next check.

## 2. The one thing definitely lacking

**The ENVIRONMENT section is the only prose surface in the most-guarded
artifact with no guard.** M539's seven checks cover flags, subcommands, and
exit codes — and the lint's own header says so ("flags have docs_flags,
subcommands have subcommands_lint, [the man page] is the last user-facing
namespace with no parity lint"). ENVIRONMENT is a section of *that* page,
but none of the seven reads it: `grep -ic environment
tests/smoke/man_page_lint.sh` → 0. So the code's `getenv` set and the man
page's env set have no parity, in either direction.

The measured drift today: of **12 project env-var knobs** the code reads,
the man page names **5**. The seven missing are not obscure — three of them
are the env side of a flag the man page's own OPTIONS section *does*
document (`--lang`/`JICHI_LANG`, `--dump-requests`/`JICHI_DUMP_REQUESTS`,
`--provider`/`JC_PROVIDER` — each flag exists in the parser and each env
form is undocumented
everywhere on the page), and the rest are knobs a scripter or operator
needs (`JICHI_SHELL` — the shell the agent runs commands in, env-only, no
flag; `JICHI_SPAWN_BACKEND` — fork vs `posix_spawn`, env-only, documented
in doctor output but not on the page; `JICHI_DAEMON_SOCK` — the
control-socket path; `JC_PROVIDER`; `JC_CONTROL_CLIENT_SECS`). A user
reading the man page — the artifact they are sent to, the one with the
"stable interface" stamp on its EXIT STATUS section — cannot discover that
`JICHI_SHELL` exists.

The second surface, same root: **OPTIONS behavioral prose is name-checked,
not truth-checked.** 27 "default …" sentences assert code behavior; the
lint proves the flag *name* is documented, never that the *claim* holds.
Because the sentences are copies of main.c's `--help` literals (verified
verbatim today), this is not a live lie — it is a *latent* one: change a
default in main.c and the help sentence, and the man page's copy drifts
with no check to catch it.

## 3. Why this is "definitely lacking" — alternatives and their status

- **"The whole man page is stale"** — *closed*. M539's incident (77/148
  flags, 38/55 subcommands, phantom `session`, missing 143) is fixed and
  *gated*; the lint's floors (148/148, 55/55) fail on regression. I do not
  re-claim it.
- **"It should be generated"** — *rejected by the register, and rightly*.
  "An AID, not a build step" (man-options-from-help.sh:4): a generator
  would drop the prose sections (DESCRIPTION, INTERACTIVE KEYS, FILES) that
  "no generator could write", and the help text is a one-line-per-flag
  summary, not a manual. The aid + lint + edit loop is the right shape —
  the gap is that the loop's coverage stops at the OPTIONS/COMMANDS/EXIT
  sections.
- **"It's fine — env vars are in the docs"** — *partly true, and the
  partial is the problem*. `JICHI_SPAWN_BACKEND` is in 5 doc files,
  `JICHI_LANG` in 5, `JICHI_SHELL` in 2. But the man page is the
  *reference* surface (installed via `make install`, dated `.TH`, "stable
  interface"), and documentation *scattered across prose* is exactly the
  shape this register has paid for before: the collaboration mechanisms
  "were documented in eight different places" until AGENT_COLLABORATION.md
  drew the one map. The env section is the one place the man page could be
  the *complete* list — and it is the one place M539 doesn't look.
- **"The behavioral-prose check is overkill"** — *no: it is free*. The lint
  already builds the binary and extracts `--help`'s text (for flags and
  subcommands). Diffing the *sentences* between `--help` and the de-roffed
  OPTIONS section is the same pipeline, one comparison further. The
  register's own M530 standard (a preview must show *exactly* what apply
  will do) applied to docs: the documented sentence should be byte-identical
  to the reviewed help sentence unless deliberately expanded — and a
  deliberate expansion is a one-line allowlist note, not a silent drift.

## 4. Evidence (all verified against HEAD `1b71247d`)

- **`man/jichi.1`** — 803 lines, `.TH JICHI 1 "2026-08-22" "jichi 0.9.0"`;
  ENVIRONMENT section names exactly **five** project vars (`.B JC_CONFIG`,
  `.B NO_COLOR`, `.B JICHI_KEEP_DUMPABLE`, `.BR ANTHROPIC_API_KEY ", "
  OPENAI_API_KEY`) and **none** of the POSIX standards (a whole-page count of
  PATH/HOME/LANG/LC_ALL/COLUMNS/TZ/PREFIX is 0); OPTIONS carries 27
  "default …" sentences.
- **The code's env set** — `grep -rhoE 'getenv\("[A-Z_]+"\)' src/` → **18**
  distinct: 8 POSIX standards (COLUMNS, HOME, LANG, LC_ALL, LC_CTYPE, PATH,
  PREFIX, TZ) and **10 project knobs** (JC_CONFIG, JC_CONTROL_CLIENT_SECS,
  JC_PROVIDER, JICHI_DAEMON_SOCK, JICHI_DUMP_REQUESTS, JICHI_KEEP_DUMPABLE,
  JICHI_LANG, JICHI_SHELL, JICHI_SPAWN_BACKEND, NO_COLOR); plus the two API
  keys read via the provider helper (jc_config.c:64-67) = **12 project vars**
  in all. The man page names **5 of the 12**. Missing from it:
  **JC_CONTROL_CLIENT_SECS,
  JC_PROVIDER, JICHI_DAEMON_SOCK, JICHI_DUMP_REQUESTS, JICHI_LANG,
  JICHI_SHELL, JICHI_SPAWN_BACKEND** (7). (Verified by per-var grep.)
- **`tests/smoke/man_page_lint.sh` (M539)** — seven checks (lines 23-41
  header, 78-152 assertions): parser flags "148 today", subcommands floors
  "55/55", no-phantom check, exit codes "0 1 2 130 143" matching
  `describe`. `grep -ic environment` → **0**: the ENVIRONMENT section is
  outside the gate. (Verified.)
- **The incident it closed** — the lint header, lines 4-16: "77 of the 148
  long flags … 38 of the 55 subcommands, and advertised one command,
  `session`, that does not exist … Its EXIT STATUS section listed four codes
  where … all list five, the missing one [143]". (Verified — read.)
- **The roff trap, institutionalized** — lines 32-37: "recurred THREE times
  in one session … Every extraction here de\-roffs FIRST and matches
  second." (Verified — read.)
- **The aid** — `scripts/man-options-from-help.sh:4-6`: "An AID, not a
  build step … then EDIT the result. Nothing in `make` calls it."
  (Verified — read.)
- **Sentence parity today** — four spot-checks of man OPTIONS prose vs
  main.c `--help` literals: "print the reply and exit" (man=1, code=1),
  "honors NO_COLOR" (1/1), "bounds an unsupervised run" (1/1), the
  json/jsonl sentence (1/3 — help splits it; a substring match, not a lie).
  (Verified.)
- **Partial doc elsewhere** — JICHI_SPAWN_BACKEND in 5 doc files,
  JICHI_LANG in 5, JICHI_SHELL in 2 — scattered prose, not the reference.
  (Verified by `grep -rln` counts.)

## 5. Implications

- **The one stable reference has a blind column.** The man page's EXIT
  STATUS section stamps "The exit codes are a **stable** interface" — the
  page *is* the stable surface by its own declaration. A user scripting
  against jichi (the EMBEDDING.md audience) who reads the man page never
  learns of `JICHI_DAEMON_SOCK` — the exact variable a *systemd unit* or
  control-socket scripter needs (it has no flag at all, so the flag
  documentation cannot hint at it). Seven knobs, one grep away in the code,
  absent from the page.
- **The drift is *silent-by-construction*, not accidental.** Unlike the
  M539 era (77/148 — a big, visible hole), a man page that documents *most*
  env vars is worse: the reader infers completeness from the pattern. The
  three missing env forms of documented flags are the quiet ones — `--lang`
  is in OPTIONS with a sentence, and its env twin `JICHI_LANG` is not there.
- **The behavioral prose is a second, latent instance of the same class.**
  27 "default …" sentences are *claims about the code*. Today they match
  (they were copied from the reviewed help text); the lint cannot keep them
  matching. A future default change lands in main.c + help sentence; the
  man page's copy goes stale; no check fires. This is the M530 shape
  applied to docs: what is shown (the sentence) must equal what is true
  (the default), and the current gate only checks the *name*.
- **It composes with the register's own map lesson.** AGENT_COLLABORATION.md
  was created because mechanisms "were documented in eight different
  places. This is the one map." The env vars are the same pattern in
  miniature — five of the seven missing ones live in 2-5 prose files — and
  the ENVIRONMENT section is the *designed* location for the one map. It is
  the one column of the most-complete table the register built that no
  lint reads.

## 6. Recommendation

1. **An env-var parity check in `man_page_lint.sh`** — extract the code's
   `getenv` set (de-duped, same extraction discipline as the flags), the
   man page's ENVIRONMENT `.B`/`.BR` names (de-roffed first — the trap is
   written there for a reason), and fail on either direction: a `JICHI_*`/
   `JC_*` var read by the code and absent from the page, or a page entry
   the code does not read. POSIX-standard vars (PATH, HOME, LANG, LC_*,
   COLUMNS, TZ, PREFIX, NO_COLOR) exempt by a *named* allowlist in the
   lint, with the reason ("standard environment, not jichi's knob") — the
   M539 header's own resolution of the flag-allowlist question ("turned out
   to be unnecessary") applies: just *write* the nine, exempt the
   standards, no special cases. Red today: the seven missing vars. Green
   after: one pass adding them.
2. **A sentence-parity check for OPTIONS** — the lint already has `--help`
   text (it builds the binary, line 57). De-roff the man's OPTIONS section
   and require each help sentence's *flag description* to appear verbatim
   (or in a per-flag "deliberately expanded" note). Free pipeline, one more
   comparison. Red today: unknown (needs the first run — possibly green,
   which is the *correct* outcome for a check whose job is to guard a
   future change).
3. **One line in the lint header**: "ENVIRONMENT is checked for presence
   parity (M####); OPTIONS prose for sentence parity against --help. The
   M539 defect was 77/148 flags; the next defect class was env vars —
   seven of them the env side of flags or doctor lines this page
   documents."

Rejected, in the register's spirit:

* *Generate the whole page.* "An AID, not a build step" — the prose
  sections are the value; the gap is two missing checks, not a missing
  generator.
* *Point the man page at the docs.* "See docs/ENVIRONMENT…" — there is no
  ENVIRONMENT.md; creating one is the map move but it does not fix the
  *reference* being incomplete, and it adds the eight-places pattern the
  register just escaped for collaboration.
* *Skip check 2 as overkill.* The sentence is *already* the reviewed help
  text (verified verbatim); checking it is the difference between "the flag
  name exists" and "the documented default is the real one" — the M530
  contract for documentation.

## 7. Limitations and honest counterarguments

1. **This is the *most*-guarded artifact in the register** — I am claiming
   a seam inside an M539 seven-check gate, not that the man page is
   unguarded or stale. The flags/subcommands/exit-code coverage is real and
   strong; the honest statement is that its coverage *stops at three
   sections*.
2. **Seven missing env vars ≠ seven missing *documents*.** Five of the
   seven are documented in 2-5 prose files (JICHI_SPAWN_BACKEND ×5,
   JICHI_LANG ×5, JICHI_SHELL ×2, JICHI_DUMP_REQUESTS ×3 — verified). The
   claim is narrow: the *reference* page omits them, and the reference is
   the surface a user is sent to and `make install` ships.
3. **I have not run `man_page_lint.sh`** (read-only; it builds the binary
   and takes ~60 s). The "seven checks, no env check" claim is verified by
   reading the lint (lines 23-41, 78-152, and `grep -ic environment` → 0);
   the floors (148/148, 55/55) are its own `t_ok` strings.
4. **The sentence-parity check may be green today.** Four spot-checks
   matched verbatim; a full diff might pass on the first run. That is the
   *correct* result — the value is the guard on the *next* change — but I
   do not claim a live drift. The env-var check, by contrast, is red today
   (seven missing), verified by per-var grep.
5. **NO_COLOR, COLUMNS, TZ and the other standards-class vars are
   *deliberately* env, not jichi knobs.** The recommendation's allowlist
   keeps them exempt; I do not claim they need man entries — only that the
   exemption should be *named* so a future standard var is a one-line
   lint change, not a silent drift.

## 8. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

    # the page and its dated contract
    wc -l man/jichi.1 && head -1 man/jichi.1
    grep -n "stable" man/jichi.1 | head -3

    # M539's gate: seven checks, and the section it does not read
    sed -n '1,45p' tests/smoke/man_page_lint.sh   # the incident, the checks, the roff trap
    grep -ic "environment" tests/smoke/man_page_lint.sh   # (expect 0)
    grep -n "floor\|t_ok\|t_fail" tests/smoke/man_page_lint.sh | head -12

    # the env set, both directions
    grep -rhoE 'getenv\("[A-Z_][A-Z0-9_]*"\)' src/ --include=*.c | sort -u
    sed -n "$(grep -n '^\.SH ENVIRONMENT' man/jichi.1 | cut -d: -f1),+14p" man/jichi.1

    # the aid, and why it is not a generator
    sed -n '1,10p' scripts/man-options-from-help.sh

    # sentence parity today (four spot-checks)
    grep -c "honors NO_COLOR\|print the reply and exit\|bounds an unsupervised run" man/jichi.1 src/main.c

    # the 27 behavioral claims
    grep -c "default" man/jichi.1

**Verdict line for the register this could seed:** jichi's man page is the
register's *most*-guarded user-facing surface — M539's `man_page_lint.sh`
closes the documented 77-of-148-flags / 38-of-55-subcommands / phantom-
`session` / missing-143 incident with a seven-check *behavioral* parity
gate (it builds the binary, extracts `--help`), institutionalizes the
roff-escaping trap ("recurred THREE times in one session; de-roffs FIRST
and matches second"), and pairs with a `scripts/man-options-from-help.sh`
aid explicitly "not a build step". What the seven checks deliberately do
not reach is the seam: (1) the **ENVIRONMENT section has no parity check**
(`grep -ic environment` → 0) — of **12 project env-var knobs** the code
reads, the page names **5**, and the seven missing include two that are the
*env side of flags this same page documents in OPTIONS*
(`--lang`↔`JICHI_LANG`, `--dump-requests`↔`JICHI_DUMP_REQUESTS` — the
flags exist in the parser; the env forms are undocumented), plus the
interface knobs a scripter needs (`JICHI_SHELL` and
`JICHI_SPAWN_BACKEND`, both env-only with no flag; `JICHI_DAEMON_SOCK`,
`JC_PROVIDER`, `JC_CONTROL_CLIENT_SECS`); (2) **OPTIONS behavioral prose (27 "default …"
sentences) is name-checked, not truth-checked** — the lint proves each
documented flag *exists*, never that the documented default *matches*; the
sentences are verbatim copies of main.c's `--help` literals (four
spot-checks matched), so the drift is latent, not live. Definitely lacking:
an env-var presence check (code's `getenv` set ↔ page's ENVIRONMENT `.B`
set, POSIX-standard vars exempt by a named allowlist — red today on the
seven missing) and a `--help`-↔-OPTIONS sentence-parity check (free: the
lint already has the `--help` text). Revisit when: a `JICHI_*`/`JC_*` var
cannot be read by the code without an entry in the page (or a named
allowlist line), and an OPTIONS sentence cannot diverge from the reviewed
`--help` text — at which point the most-guarded namespace is also the most-
complete one, and "stable interface" (the page's own stamp on EXIT STATUS)
covers every section it declares.
