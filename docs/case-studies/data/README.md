# The data behind the drive case studies

The tables the [overview](../JICHI_DRIVES.md) and the drives' pages draw on, one row per event, tab-separated.
Every per-drive sum equals the totals on the pages: landings, commits, model calls and tokens. The charts in
[../charts/](../charts/) are drawn from these files by `make_charts.py` there.

| File | One row per | Main columns |
|---|---|---|
| `landings.tsv` | landing on master, plus one row per drive for the time after its last landing | drive, batch, landed_at, ready_at, first_gate_start, gate_attempts, commits, model_calls, in_tok, cache_read_in, out_tok (the calls and tokens since the previous landing) |
| `runs.tsv` | run of the lead | drive, run, start, end, rc |
| `gates.tsv` | gate run (`make ci` and the gate's other stages) | drive, head_sha, by, start, end, verdict, on_ready_head, in_drive |
| `downtime.tsv` | span of more than 5 minutes when the loop ran no run, or was parked | drive, start, end, kind, cause |
| `lost-time.tsv` | set-back with a measured cost | id, drive, cause_class, hours, what_was_lost, how_measured, overlaps |
| `lost-time-unmeasured.tsv` | set-back the records cannot put a time on | id, drive, cause_class, why not measurable |
| `lost-time-notes.md` | | the totals, the overlap rule, and what one hour means in each kind of loss |
| `lost-time-stopped.tsv`, `lost-time-delay.tsv` | cause | the per-cause totals the two lost-time charts draw |
| `catchers.tsv` | confirmed defect | id, drive, defect, area, caught_by, missed_by, fixed_in |
| `catchers-notes.md` | | the counts, eight teaching examples, and what the counts cannot tell |
| `catchers-classes.tsv` | check | the per-check counts the defect chart draws |

Times carry their UTC offset. The harness-port drive is still running; its rows end at 2026-11-22 17:22:29 +0100.
The `source` columns name the record each value came from; host-side records are named by their drive folder.
