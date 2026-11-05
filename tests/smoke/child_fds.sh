#!/bin/sh
# smoke: a model-issued shell inherits none of jichi's descriptors (M472).
#
# THE DEFECT THIS EXISTS FOR. exec() replaces the code, not the descriptor table,
# and there was no CLOEXEC anywhere in src/. Measured, by having a model-issued
# run_terminal_command list /proc/self/fd:
#
#   3 -> run.jsonl        the run journal        WRITABLE
#   4 -> telemetry.jsonl  the telemetry sink     WRITABLE
#   7 -> socket           the live provider connection, READ/WRITE
#
# and then `echo {*FORGED*} >&3` put a forged record in the middle of the run
# journal -- the sink `jichi runs` and `doctor --unattended` read to gate an
# unattended loop. M130 scrubs the child's ENVIRONMENT and M132 sets FILE MODES;
# a descriptor obeys neither, because the permission check happened at open()
# time in the parent and the child holds the result.
#
# WHAT IS ASSERTED, and the one thing that is not. Check 1 pins the total on
# jichi's own fork/exec path (a `timeout` on the tool selects it): stdio and
# nothing else. A total is deliberately stronger than a blocklist -- it catches
# descriptors nobody has thought of yet, which is the whole point. Checks 2 and 3
# pin the two named sinks on BOTH paths.
#
# Check 4 (M832) pins the popen path's total too. Until M832 it could not be
# asserted: jc_proc_popen wrapped libc popen, whose fork happens inside libc, so
# no jichi code ran between fork and exec and the backstop could not reach it --
# and one pipe pair arrived there, libcurl's own, created with pipe2(fds, 0)
# inside curl_easy_perform where jichi cannot mark it (DEFERRED def-151,
# measured again at M832 on the installed M831 binary: fds 4 and 5). M832
# routes popen through jc_spawn, which runs the backstop in the child (fork
# backend) or marks every descriptor CLOEXEC before the spawn (posix_spawn
# backend, which has no child-side hook). Both backends are asserted: run this
# driver with JICHI_SPAWN_BACKEND=spawn, as tests/smoke/spawn_backend.sh
# requires of the tier's descriptor fence, and the same four checks hold.
#
# See docs/analysis/2026-08-17-source-hardening-audit.md §H2.
. "$(dirname "$0")/_smoke.sh"

# WHERE THIS CAN RUN. procfs, not Linux -- and the old text, "needs
# /proc/self/fd (Linux)", got it wrong in the direction that discourages the
# measurement: it told a reader this guarantee could only ever be checked on
# one kernel. M480 ran this driver GREEN on NetBSD 10.1, whose procfs supplies
# /proc/<pid>/fd, making it the first non-Linux kernel on which M472's
# descriptor fence is verified rather than assumed. NetBSD ships that mount
# `noauto` in /etc/fstab, so scripts/tier-v-netbsd.sh mounts it and the row
# records that it did -- proven both ways: unmounted, this driver declines;
# mounted, it reports three checks. FreeBSD and OpenBSD have no procfs and
# still decline, which is a gap in coverage, not a pass.
[ -d /proc/self/fd ] || t_skip "needs procfs (/proc/<pid>/fd): Linux, or NetBSD with mount_procfs"
t_plan 8
smoke_home
tmp=$(smoke_tmp)

# `timeout` on the tool call routes through run_command_watched -- jichi's own
# child ritual in jc_spawn, where the backstop runs. Without it the command goes
# through jc_proc_popen, which since M832 is jc_spawn too; see the note above.
# Each command REDIRECTS to a file rather than piping: a pipeline makes the shell
# create its own pipe, whose ends the child then sees, and that is not a leak from
# jichi. A `>` redirect is opened onto fd 1 and adds nothing. The driver then reads
# the files instead of parsing numbers out of a JSON request body -- an earlier cut
# did the latter and matched a "17" from an unrelated field.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"ls -l /proc/self/fd > fd_exec.txt","timeout":20}
rule
  count 2
  tool run_terminal_command {"command":"ls -l /proc/self/fd > fd_popen.txt"}
rule
  text DONE
EOF

fds_above_3() {  # $1 = an `ls -l /proc/self/fd` listing -> the numbers above 3
    printf '%s\n' "$1" | sed -n 's/^[lcrwx.-]* .* \([0-9][0-9]*\) -> .*/\1/p' | \
        awk '$1 > 3' | sort -un | tr '\n' ' '
}

# probe BACKEND -- one probe turn with JICHI_SPAWN_BACKEND=BACKEND, then the four
# checks, each labelled with the backend. A fresh mock per backend: its `count N`
# routes by request index (see docs/internal notes on mockmodel), so one instance
# cannot serve two turns.
probe() {
    _b=$1
    _ws=$(smoke_tmp)
    mm_start "$tmp/replies.mm" "$tmp/cap_$_b" 8
    cat > "$tmp/config_$_b.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF
    (cd "$_ws" && with_deadline 90 env "JICHI_SPAWN_BACKEND=$_b" "$BIN" \
          --config "$tmp/config_$_b.json" --no-session --auto \
          --journal "$tmp/journal_$_b.jsonl" --log "$tmp/telemetry_$_b.jsonl" \
          -p probe < /dev/null >/dev/null 2>&1)
    mm_stop

    fd_exec=$(cat "$_ws/fd_exec.txt" 2>/dev/null)
    fd_popen=$(cat "$_ws/fd_popen.txt" 2>/dev/null)
    if [ -z "$fd_exec" ] || [ -z "$fd_popen" ]; then
        t_fail "$_b: the probe commands did not run (no fd listing written)"
        t_fail "$_b: (same cause)"
        t_fail "$_b: (same cause)"
        t_fail "$_b: (same cause)"
        return
    fi

    # 1. The watched (fork/exec or spawn) path: stdio and nothing else. `ls` holds
    #    one descriptor of its own for the directory it is reading, so the expected
    #    set is 0 1 2 plus that one -- hence "no fd above 3", not "no fd above 2".
    leaked=$(fds_above_3 "$fd_exec")
    if [ -z "$leaked" ]; then
        t_ok "$_b: the watched-path child sees stdio only (no descriptor above 3)"
    else
        t_fail "$_b: descriptors leaked to the model's shell: $leaked"
    fi

    # 2/3. The two named sinks reach NEITHER path. These are the ones with a
    #      demonstrated exploit -- a forged audit record from a one-line `>&3`.
    if printf '%s\n%s\n' "$fd_exec" "$fd_popen" | grep -q "journal_$_b.jsonl"; then
        t_fail "$_b: the run journal descriptor reached a model-issued shell"
    else
        t_ok "$_b: the run journal is not inherited"
    fi
    if printf '%s\n%s\n' "$fd_exec" "$fd_popen" | grep -q "telemetry_$_b.jsonl"; then
        t_fail "$_b: the telemetry descriptor reached a model-issued shell"
    else
        t_ok "$_b: the telemetry sink is not inherited"
    fi

    # 4 (M832). The popen path's TOTAL, the same rule as check 1. The descriptor
    #   this was written for is libcurl's internal pipe, which a blocklist could
    #   not have named in advance -- the argument for a total, again.
    leaked_p=$(fds_above_3 "$fd_popen")
    if [ -z "$leaked_p" ]; then
        t_ok "$_b: the popen-path child sees stdio only (no descriptor above 3)"
    else
        t_fail "$_b: descriptors leaked to the model's shell on the popen path: $leaked_p"
    fi
}

probe fork
# The spawn backend has no child-side hook, so its fence is a DIFFERENT mechanism
# (jc_proc_parent_cloexec_all) and needs its own proof -- every gate run, not
# only on the fork-less ports it exists for. Asked of the build, never of the
# resolved backend: the doctor line names "posix_spawn available" only while the
# backend is fork, so the question is asked with the backend pinned to fork.
if env JICHI_SPAWN_BACKEND=fork "$BIN" doctor 2>/dev/null | grep -q "posix_spawn available"; then
    probe spawn
else
    for _c in 1 2 3 4; do
        t_skip_one "spawn: check $_c needs a posix_spawn build (doctor: process backend)"
    done
fi

t_done
