# jichi's session persistence & portability — the one thing definitely lacking

*Read-only gap analysis of `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD
`1b71247d`, M898-era), part of a series. Method as before: the finding is
verified against source, docs, and tests; figures in the appendix are
reproducible; the "definitely lacking" claim is the one feature the record
shows absent and the project's own comments say was meant to be present.*

## 1. TL;DR

jichi's session store is robust: it has a shape version (M606), a byte-stable
writer (M232), tolerant lenient parsing, and a warning when a *newer* jichi
saved it. What it **definitely lacks** is the thing it already carries the
data for: a signal to the user at `--resume` about *which build last saved the
session*. The `env.jichi` field is written (M290) with a comment stating its
purpose is exactly "a reader of a resumed session… what `--resume` across a
version change would otherwise hide" — but nothing on the load path reads it.
A resume across a build change is flagged **only when the schema version
changed**, not when the build changed.

## 2. What the store *does* have (for precision)

- **Shape versioning (M606).** `jc_session.c:321` writes `env.v =
  JC_SESSION_STORE_V`; `:495` reads it on load and warns (":496–501") when
  `v > this build`, i.e. a *newer* jichi saved it and "fields the newer shape
  added are not restored." This is the load path's only version signal.
- **Byte-stable writer (M232).** The metadata envelope is printed, then the
  history spliced in message-at-a-time; the comment at `:300–311` guarantees
  the output is **byte-identical** to the old whole-tree print, so old
  sessions remain readable.
- **Lenient parsing.** `jc_json_get_num_lenient` / `get_bool_lenient`
  tolerate drift; `:522` notes `isError` conversion is "protected at M519, on
  the reload path."
- **Corruption guard.** `:401` — "Don't return a truncated (corrupt)
  document"; `sessions_dir` moved `~/.continue` → `~/.jichi.d` (M204) with a
  migration comment.
- **Tests:** `sessions.sh`, `from_resume.sh`, `resume_drift.sh`,
  `sessions_export.sh` (list/export round-trip).

## 3. The gap, stated

The **build** a session was last saved by is recorded but never surfaced at
resume.

### 3.1 The field exists for exactly this, and is unread

`jc_session.c:323–326` (verbatim):

> `/* M290: the build that last SAVED this session. A session is rewritten`
> `after every turn, so this is the newest jichi that touched it -- which is`
> `what a reader of a resumed or archived session needs, and what --resume`
> `across a version change would otherwise hide. */`
> `cJSON_AddStringToObject(env, "jichi", JC_VERSION);`

A grep for the read of `env.jichi` in the session load path returns nothing.
`jc_json_get_str(o, "jichi", ...)` appears only in `jc_telemetry.c:168` and
`jc_runsview.c:104` — both reading the *event-level* build tag on journal /
runs rows, not the session store's `env.jichi`. The session-load path reads
`env.v` (schema), `mode`, `history`, `truncated`, `isError` — and stops. The
build that M290's comment names as the reason the field exists is written on
every save and read by no resume.

### 3.2 Why the schema warning does not cover it

M606's `v` bump only fires when a jichi build changes the *stored shape*.
Builds that change **behaviour** — a new tool, a changed message format, a
routing or memory semantics — do not bump `JC_SESSION_STORE_V` and so leave
`v` equal across the resume. M290's `env.jichi` is the field that *would*
catch exactly those: two different builds, same schema. It is the
`build_rev.sh` (M495) defect — "a binary 12 days behind printed the same
version, and the gap surfaced only as behaviour" — applied to the resume path,
with the data to detect it already present and unread.

### 3.3 The contrast that proves the point

The tree already solved this *class* of problem for **telemetry**: `jc_runs`
shows the `jichi` build per run because `jc_runsview.c` *reads* that field.
The session store is the one persisted format that writes a build tag it
never reads back. The gap is not "no version data"; it is "the build data is
written for a reader that does not exist yet."

## 4. Why "definitely lacking" (alternatives and their status)

| Candidate | Status |
|---|---|
| Corrupt / truncated session | **Handled** (`:401` guard; lenient parse; M519 `isError` protection). Not a hole. |
| Session saved by a *newer* schema | **Warned** (M606, `:496–501`). Recorded. |
| Session saved by a different *build*, same schema | **The gap** — `env.jichi` (M290) written, unread on load. |
| Cross-workspace / cross-user resume | **Recorded as non-feature** (scoping in `sessions_export.sh`, `workspace` field). A boundary, not a hole. |
| Byte drift breaking old reads | **Refused by construction** (M232 byte-identical writer). Not a hole. |

The build-field row is the one with a *written intent* (the M290 comment) and
a *missing mechanism* (no read). That is the sharpest form of "definitely
lacking": the code already says what it will do, and does not.

## 5. Evidence

- `jc_session.c:321` writes `env.v`; `:323–326` writes `env.jichi` (M290
  comment, verbatim above).
- `jc_session.c:495–501` reads `root.v` and warns on newer schema; **no read
  of `env.jichi` anywhere in the load path** (verified by grep).
- `jc_telemetry.c:168`, `jc_runsview.c:104` read the *event-level* `jichi`
  tag — proof the tree reads build tags where it wants them, and does not
  where M290 says it should.
- `build_rev.sh` (M495) — the same "behaviour gap behind an identical version
  string" defect, already instrumented for the binary, not for resume.
- `sessions.sh` / `from_resume.sh` / `resume_drift.sh` — the resume drivers
  assert content and export round-trips; none writes a session with a
  different `env.jichi` and asserts a warning, because the warning does not
  exist to assert.

## 6. Implications

1. **A resume can be silently a different jichi than the one that saved.**
   If the user's install is a build behind (the M495 scenario, common — the
   docs themselves note 0.12.0 predates M840), `--resume` loads a session the
   older build saved, and only a *schema* bump would say so. Behaviour
   changes between builds surface as "the resumed session acts oddly," the
   exact class of failure M495 instrumented for the CLI.
2. **The field's own comment is the spec.** M290 states the intent in code
   comments; the feature is one `get_str` and one `jc_logf` away, and its
   absence is a known, named, currently-unmet requirement.
3. **Asymmetry with the telemetry path** means `jichi runs` *can* tell you
   the build per run, but `--resume` *cannot* tell you the build of what it
   just loaded — the two reads of the same concept are wired to different
   degrees of completeness.

## 7. Recommendation

1. **Read `env.jichi` on load.** In the M606 block at `:495`, after the
   schema check, `jc_json_get_str(root, "jichi", NULL)`; if present and
   different from `JC_VERSION`, `jc_logf(WARN, "[session] %s was last saved
   by jichi %s, this build is %s; behaviour may differ")`. No field is
   dropped (the reader is permissive), so this is additive.
2. **Red-first test.** A driver that hand-writes a session JSON with
   `env.jichi` set to a different build, resumes it, and asserts the warning
   (and that the session still loads). Before the fix it is red (no
   warning); after, green. The M232 byte-stable writer means the hand-written
   doc must match the format — use `sessions.sh`'s real save, then `sed` the
   `jichi` field.
3. **Optionally tie the two.** `build_rev.sh` (M495) already compares two
   printed versions; a resume variant comparing `env.jichi` to the running
   build would close the class in one place.

Rejected, in the register's spirit:
* *Bump `JC_SESSION_STORE_V` on every build* — the schema version means
  "the stored shape changed"; making it a build counter would fire the M606
  warning on every resume across any build and lose its meaning. The
  `env.jichi` field is the right instrument.
* *Refuse to resume across builds* — the conversation is the user's and not
  regenerable (M606's own stated reason); a hard refusal is disproportionate.
  A warning is the calibrated response.

## 8. Limitations and honest counterarguments

1. **The warning may be considered nice-to-have.** M606 already handles the
   case that actually changes *parseability* (schema). The build gap is a
   *behaviour* gap, which the tree elsewhere (M495) treats as worth
   instrumenting — so "definitely lacking" is relative to the project's own
   standard for version gaps, not an absolute.
2. **I have not demonstrated a resumed session misbehaving.** §3.2 is a
   structural claim (field written, not read); whether a specific build
   change causes a user-visible resume bug depends on that change. The
   recommendation stands because the *detection* is absent regardless of any
   particular change.
3. **`env.jichi` may be intended for a future reader.** The comment says
   "a reader… needs it," which could mean an external archive tool, not the
   built-in resume. That is a reasonable reading; the counter is that the
   *in-tree* resume path is the most likely reader and does not use it.

## 9. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

```sh
# the field: written for a reader that does not read it
sed -n '318,327p' src/session/jc_session.c          # M606 (v) + M290 (jichi) write
sed -n '495,502p' src/session/jc_session.c          # M606 read + warn; no jichi read

# no read of env.jichi on the load path
grep -n 'get_str.*"jichi"' src/session/jc_session.c  # (empty)

# the tree DOES read the event-level build tag elsewhere
grep -n 'get_str(o, "jichi"' src/util/jc_telemetry.c src/util/jc_runsview.c

# the same "behaviour behind an identical version" defect, instrumented for the CLI
head -14 tests/smoke/build_rev.sh

# the resume drivers (assert content/export, not build)
head -8 tests/smoke/sessions.sh tests/smoke/from_resume.sh tests/smoke/resume_drift.sh
```

**Verdict line for the register this could seed:** *jichi's session store
writes, on every save, the build that last touched it (`env.jichi`, M290,
"what `--resume` across a version change would otherwise hide") and reads its
schema version (M606) on load — but the load path never reads the build.
A resume across a build change is flagged only when the stored *shape*
changed, not when the *behaviour* changed, the exact class of gap M495
instrumented for the binary. Revisit when: the M606 load block also reads
`env.jichi` and warns on a build mismatch, and a red-first driver hands
resume a session saved by a different build.*
