# jichi's journal & audit log: what it definitely lacks

*Read-only analysis. Subject: the run journal, the eventlog, the audit log —
the evidence layer — of jichi at `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey),
HEAD `1b71247d` (M898-era). Method as in the companion analyses: the one
property this surface definitely lacks, stated precisely, with verified
evidence. Inferences flagged. The survey in the appendix reproduces every
number.*

## 1. TL;DR

jichi's unattended runs are audited by a **journal**: a JSONL event log
("the evidence of what an unattended run actually did") plus a privilege
**audit log** ("always on"). M438 makes the journal's liveness deliberate:
it writes a line the instant the file exists, "so a supervisor's only honest
liveness signal" is not a 0-byte file.

What the evidence layer definitely lacks is **durability and self-
verification**: the journal is written with `fflush` but **no `fsync`**
anywhere in the path, and its reader **silently drops any line it cannot
parse**. A torn or truncated tail — the exact bytes that record a run's
stopping moment, after a power loss or a `kill -9` — disappears from both the
event count and the output, with no warning. The artifact whose purpose is
to be trusted forensics does not know when it has been cut short.

## 2. What the journal *does* have (for precision)

- **The write path** (`src/chat/jc_envelope.c`): one `FILE*` opened `ab`
  (`:973`); M438's `open` event "the instant the journal exists, so the file
  is never 0 bytes while the process is alive" (`:987`); every event
  `fputs + fputc('\n') + fflush` (`:1234–1237`).
- **A named purpose** (M438 comment): the journal exists so that "during that
  window a supervisor tailing" the file sees real activity, and the
  **`start`**/`stop_reason`/`end` records settle "the one question a journal
  exists to answer — **why did it stop?**"
- **An always-on audit log** (`src/util/jc_audit.c`): privileged-command
  attempts appended (`ab`), one line each.
- **A read-side schema** (`jc_runsview.c`): aggregates `events`, `ts_first`,
  `ts_last`, run id, outcome from the JSONL.

## 3. The gap, stated

The journal's *purpose* is to be the artifact a supervisor reads when a run
goes wrong — a torn run, a hung worker, a budget trip, a crash. Its
**last lines are the most important** (they carry the stop reason and the
outcome). Two facts about the implementation undercut exactly that purpose:

**3.1 No `fsync` — the durable record is not durable.**
Every journal event ends with `fflush` (`jc_envelope.c:1237`), which pushes
bytes to the OS. `grep -rn fsync` over `jc_envelope.c`, `jc_audit.c`,
`jc_eventlog.c` returns **nothing**. On a power loss or `kill -9` the kernel
may not have written the most recent events to disk. The run whose death is
precisely the event being forensically investigated is the one whose last
lines — the `stop_reason`, the `end` record, M438's own "so a supervisor
tailing" guarantee — are the most likely to be absent. M438 guarantees the
file is *not 0 bytes while alive*; it says nothing about the last N bytes
*after death*.

**3.2 The reader silently drops unparseable lines — a torn tail is
invisible.**
`jc_runsview.c:68` parses each line with `cJSON_Parse`; if it returns
`NULL`, the function `return`s immediately (`:75`) — **no counter, no flag,
no warning**. A torn last line (valid prefix, truncated JSON) is therefore
skipped silently: `events` counts only whole lines, `ts_last` stops at the
last *parseable* event, and the run's true stopping record is gone. There is
no test that feeds a torn or partial final line and asserts it is detected.

So the "evidence" layer has a **durability gap** (the bytes may never reach
disk) and a **detection gap** (the bytes that do reach disk, if torn, are
silently discarded by the reader that is supposed to expose them). A
supervisor reading the journal after the run died gets a clean, parseable
prefix and no indication that it is incomplete — the M326b shape, applied to
forensics: the check (a well-formed journal) passes because its universe
quietly shrank to the lines that survived.

## 4. Why "definitely lacking" (the alternatives and their status)

| Candidate | Status |
|---|---|
| Journal *existence* / liveness | **Built** (M438: never 0 bytes while alive; `open` event first). |
| Stop-reason recorded | **Built** (`journal_stop_reason.sh` asserts the `end` event's `stop_reason`). |
| Out-of-scope *tracked* file kinds | **Built** (M684, `journal_trackedness.sh` — "a measurement asking for a field"). |
| Privileged attempts logged | **Built** (audit log "always on"). |
| Durability of the last events | **Missing** — no `fsync` anywhere in the path (verified: 0 hits). |
| Detection of a torn/short tail by the reader | **Missing** — `cJSON_Parse` failure → silent `return` (verified at `:68–75`). |

The first rows are why the journal is a *strong* feature; the last two are
the ones that decide whether it is still a *trustworthy* one after the run
has failed in the way the journal exists to explain.

## 5. Implications

1. **The post-mortem may lie by omission.** The common case — a hung or
   killed unattended run — is exactly the case where the tail is torn or
   lost. The reader then presents a clean prefix, and "the evidence says
   why it stopped" is false: it stopped, and the stop is not in the file.
2. **`stop_reason` is only as durable as its `fflush`.** `journal_stop_reason.sh`
   proves the field is *written*, not that it is *present* after the failure
   it was written to explain.
3. **No self-check.** Nothing validates a journal at read time for a missing
   `end`, a `ts` gap, or a partial line. A run that crashed is
   indistinguishable from a run that recorded a crash, until a human counts
   lines.

## 6. Recommendation

1. **`fsync` at the two points that matter** — after M438's `open` line (so
   liveness is durable, not just buffered) and after the `end`/stop_reason
   line (so the death is durable). This is the whole fix for §3.1.
2. **Make the reader count what it cannot parse.** In `jc_runsview.c`, when
   `cJSON_Parse` returns NULL on a *non-empty* line, record a `torn_tail`
   flag and skip-count instead of silent `return`; surface it in `jichi runs`
   and `doctor`. A torn journal becomes a *known* torn journal.
3. **Red-first tests** in the M684 style: (a) append a valid event,
   truncate mid-line, run the reader, assert `torn_tail` is set and
   `events` reflects the drop; (b) write the `end` line, drop the process
   without `fsync` (FAULT=1 style), re-read, assert the stop reason is still
   present. Before the fix both are red.

Rejected: *write-through `O_DSYNC`* (the whole journal, not just the ends);
*accept the gap* (the artifact's purpose is forensics of failures, so the
failure mode is the product, not the exception).

## 6. Implications

1. **The evidence is as reliable as the last `fflush`.** For a run that
   completes and a machine that is shut down cleanly, the journal is
   complete and honest — that is the normal case and it is tested. The gap
   is the abnormal one, and it is *the one the journal exists for*: the
   kill, the crash, the power loss where the last few events were flushed
   to the kernel but not yet the disk.
2. **A forensic reader cannot tell "complete" from "torn."** Because the
   reader silently drops the bad tail, `jichi runs`/`journal` report the
   surviving events as if the run had only had that many. The person
   investigating the failure is looking at the very record that is
   missing the failure, with no signal that anything is missing.
3. **The `0 bytes` invariant and the torn-tail case are two sides of the
   same honesty problem, only one solved.** M438 fixed the "empty file
   looks dead" case. The "truncated file looks complete" case — the
   harder one — has no counterpart.

## 7. Recommendation

1. **`fsync` (or `fdatasync`) at the run's `end` event, and ideally on
   `kill`/crash signals.** The journal is low-traffic (one line per
   event, not per token), so the cost is negligible; the guarantee — the
   run's terminal state reaches the disk before the process can die — is
   the whole point of keeping it. A per-event `fsync` is the strong form;
   an end-event + signal-handler `fsync` is the minimum that closes the
   kill-9 case.
2. **Make the reader honest about a torn tail.** In `runsview`/the
   journal reader, when the *last* line fails to parse, do not silently
   `return`: record `torn_tail = 1` (or count a `partial` event) and
   surface it in the output and any `--output json` field, so "this run's
   journal is incomplete" is itself a reported fact. Mid-stream parse
   failures can stay lenient; the tail is the one that means something.
3. **Red-first test:** write a journal, append a valid `end`, then append
   a truncated line (no newline / cut JSON) and assert the reader flags
   `torn_tail`; separately, arm `SIGKILL` after a `fflush` and assert the
   on-disk file's last complete event is whatever was `fsync`ed. Both
   are offline and deterministic.
4. **One sentence in `SECURITY.md`/the envelope docs:** the journal is
   flushed per event and `fsync`ed at run end, and the reader reports a
   torn tail — so the claim "the record is what the run did" is scoped to
   what the disk held.

Rejected, in the register's spirit:

* *`fsync` every event* — the strongest guarantee at a cost the journal
  does not need; per-event `fsync` on a busy run is measurable latency for
  a window of a few lines. End-event + signal `fsync` closes the same
  case.
* *Warn on every unparseable line, mid-stream* — over-strict; a genuinely
  malformed event mid-file is a different, rarer defect and should not
  masquerade as "the run was torn." The tail is where the meaning is.

## 8. Limitations and honest counterarguments

1. **`fflush`-to-kernel is strong durability for *process* crash.** If the
   concern were only jichi segfaulting or being `SIGTERM`ed, the events
   already reached the kernel page cache and survive — the journal is
   complete in that case. The gap is specifically the *kernel* not
   reaching disk: power loss, `kill -9` on a host that then reboots, or a
   filesystem that is down. Real, but narrower than "the journal is
   unreliable."
2. **I have not measured the flush-to-disk window on this system.** The
   recommendation holds regardless (there *is* a window), but I have not
   timed it or reproduced a loss here; the claim is structural (no
   `fsync` in the path) + the reader's silent-return, both verified in
   source.
3. **The audit log and telemetry (eventlog) share the same no-`fsync`
   property, and I treated the journal as the load-bearing one** because
   the envelope's rollback and the forensic story hang on it. The same
   fix (end-of-run `fsync`) applies to all three; I note it is not
   applied to any of them.
4. **A completed, cleanly-exited run is fully honest.** This analysis is
   about the abnormal termination the journal exists to capture, not the
   common case.

## 9. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

```sh
# the write path: append mode, fflush per event, no fsync
sed -n '973p' src/chat/jc_envelope.c          # fopen(journal_path, "ab")
sed -n '1227,1242p' src/chat/jc_envelope.c    # jc_env_journal_end: fputs + fflush
grep -c fsync src/chat/jc_envelope.c src/util/jc_audit.c src/util/jc_eventlog.c   # 0 0 0

# the reader: silent return on a torn line
sed -n '66,75p' src/util/jc_runsview.c        # o = cJSON_Parse(buf); if NULL return; events++ after

# the honesty purpose (M438)
sed -n '987,995p' src/chat/jc_envelope.c      # "so the file is never 0 bytes"

# what IS tested (presence/naming, not integrity)
grep -l "stop_reason\|tracked" tests/smoke/journal_stop_reason.sh tests/smoke/journal_trackedness.sh
grep -rn "torn\|truncat\|partial" tests/smoke/journal*.sh   # (empty: no torn-tail test)

# the audit log: always-on, append, also no fsync
grep -n "always-on audit" SECURITY.md
sed -n '85,95p' src/util/jc_audit.c          # fopen "ab", fputs, fclose
```

**Verdict line for the register this could seed:** *jichi's run journal —
the record the envelope's rollback and the whole forensics story hang on —
is written append-only and `fflush`ed per event (`jc_envelope.c:973,1237`)
but never `fsync`ed (0 `fsync` across the journal, audit and telemetry
paths), and its reader silently `return`s on a line that fails to parse
(`jc_runsview.c:68–72`), so a torn tail is dropped from both the event
count and the output with no flag. M438 fixed the "empty file looks dead"
case ("so the file is never 0 bytes"); the harder "truncated file looks
complete" case has no counterpart, and no test drives a torn tail or a
kill-mid-write. Revisit when: the run's `end` event is `fsync`ed (and
signal handlers `fsync` on crash), the reader reports `torn_tail` for an
unparseable final line, and a red-first test asserts both.*
