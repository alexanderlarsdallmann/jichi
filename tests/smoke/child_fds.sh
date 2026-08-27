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
# NOT asserted: the popen path's total. jichi runs a command without a timeout
# through jc_proc_popen, whose fork happens inside libc, so no jichi code runs
# between fork and exec and the close-range backstop cannot reach it. One pipe
# pair still arrives there -- libcurl's, created without O_CLOEXEC where jichi
# cannot mark it. It is not a sink, a socket or a secret; it is stated here and
# in DEFERRED.md rather than papered over.
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
t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# `timeout` on the tool call routes through run_command_watched -- jichi's own
# fork/exec, where jc_proc_child_close_fds() runs. Without it the command goes
# through popen; see the note above.
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

mm_start "$tmp/replies.mm" "$tmp/cap" 8
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF

(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
      --no-session --auto \
      --journal "$tmp/journal.jsonl" --log "$tmp/telemetry.jsonl" \
      -p probe < /dev/null >/dev/null 2>&1)
mm_stop

fd_exec=$(cat "$ws/fd_exec.txt" 2>/dev/null)
fd_popen=$(cat "$ws/fd_popen.txt" 2>/dev/null)
if [ -z "$fd_exec" ] || [ -z "$fd_popen" ]; then
    t_fail "the probe commands did not run (no fd listing written)"
    t_fail "(same cause)"
    t_fail "(same cause)"
    t_done
fi

# 1. The fork/exec path: stdio and nothing else. `ls` holds one descriptor of its
#    own for the directory it is reading, so the expected set is 0 1 2 plus that
#    one -- hence "no fd above 3", not "no fd above 2".
leaked=$(printf '%s\n' "$fd_exec" | \
         sed -n 's/^[lcrwx.-]* .* \([0-9][0-9]*\) -> .*/\1/p' | \
         awk '$1 > 3' | sort -un | tr '\n' ' ')
if [ -z "$leaked" ]; then
    t_ok "the fork/exec child sees stdio only (no descriptor above 3)"
else
    t_fail "descriptors leaked to the model's shell: $leaked"
fi

# 2/3. The two named sinks reach NEITHER path. These are the ones with a
#      demonstrated exploit -- a forged audit record from a one-line `>&3`.
if printf '%s\n%s\n' "$fd_exec" "$fd_popen" | grep -q 'journal.jsonl'; then
    t_fail "the run journal descriptor reached a model-issued shell"
else
    t_ok "the run journal is not inherited"
fi

if printf '%s\n%s\n' "$fd_exec" "$fd_popen" | grep -q 'telemetry.jsonl'; then
    t_fail "the telemetry descriptor reached a model-issued shell"
else
    t_ok "the telemetry sink is not inherited"
fi

t_done
