# Autonomous jichi task-loop — reference example

Runnable artifacts for driving one or more `jichi --auto` instances as an
unattended loop over a task queue, reporting via file / database / HTTP. **Read
[`docs/AUTONOMOUS_LOOPS.md`](../../docs/AUTONOMOUS_LOOPS.md) first** — it explains
the design, the security model, and the hardening these files assume.

| File | What it is |
|------|------------|
| `loop.sh` | POSIX-sh supervisor: claims a task by atomic rename, runs a bounded `--auto` turn, routes on the exit code, retries with backoff, quarantines poison tasks. |
| `jichi-supervisor.c` | The same contract in dependency-free C89 (`make` to build). |
| `Makefile` | Builds `jichi-supervisor` under `-std=c89 -pedantic -Wall -Wextra`. |
| `config.autonomous.json` | Hardened posture (privileged **deny**, path fence on, revert-out-of-scope) + the three reporting tools. |
| `report.sh` / `db-report.sh` / `http-report.sh` | The reporting scripts the tools call — destinations **fixed by the operator**, never by the model. |
| `jichi-loop.service` | systemd unit with OS-level sandboxing. |
| `crontab.example` | Scheduled (periodic) alternative. |

## 30-second try (against your own model)

```sh
# 1. Put the reporting scripts on PATH and make them executable.
chmod +x *.sh && export PATH="$PWD:$PATH"

# 2. Point at a real model + your report file.
cp config.autonomous.json /tmp/cfg.json   # then edit the model block
export JICHI_CONFIG=/tmp/cfg.json JICHI_REPORT_FILE=/tmp/jichi-status.log

# 3. Seed a task and drain the queue once.
mkdir -p queue/pending
echo "Summarize README.md in three bullets, then call report_status." \
    > queue/pending/summarize.task
RUN_ONCE=1 WORKSPACE="$PWD/../.." ./loop.sh

# 4. See what happened.
cat /tmp/jichi-status.log ; ls queue/done
```

Every knob (`BUDGET_TOKENS`, `DEADLINE`, `VERIFY`, `EDIT_SCOPE`, `MAX_ATTEMPTS`,
`RUN_ONCE`, …) is an environment variable — see the top of `loop.sh`.
