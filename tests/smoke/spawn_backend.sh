#!/bin/sh
# M832 (def-33): the posix_spawn backend of jc_spawn -- children without fork().
#
# Every program jichi starts now goes through one primitive, jc_spawn, with two
# backends: fork (the default, the historical child ritual moved verbatim) and
# posix_spawn (JICHI_SPAWN_BACKEND=spawn), which is what the fork-less rows --
# AROS/AmigaOS, FreeDOS+DJGPP, NuttX -- will run on. A backend that only ever
# runs on those rows is an untested guard that looks tested (the M400 class), so
# this driver flips the env var against the NORMAL binary, every gate run, and
# asserts the load-bearing rituals BY EFFECT on each backend:
#
#   the loop closes    a shell tool's arithmetic marker reaches the tool result
#   the scrub (M130)   the registered key variable is absent in the child
#   SIGPIPE (M461)     a child shell dies to its own `kill -s PIPE $$`
#   the pgroup (M461)  a timed-out pipeline leaves no member behind
#   popen (M472)       search_code's grep pipeline works (jc_proc_popen/pclose)
#   stdin feeding      a Stop hook receives its event JSON on stdin
#   argv exec          the git tool reports a repo's status
#
# Skips whole where the build lacks the probe (doctor: "posix_spawn not built"):
# there the env var is inert by design and the fork tier already covers jc_spawn.
. "$(dirname "$0")/_smoke.sh"

# Asked of the BUILD, with the backend pinned to fork: doctor names "posix_spawn
# available" only while the resolved backend is fork, so with JICHI_SPAWN_BACKEND=
# spawn inherited (the M832 measurement ran the whole tier that way) an unpinned
# question read "no" and this driver SKIPPED on the very build it exists for -- a
# skip counted as a pass (M698). Found by that measurement; child_fds.sh asks the
# same way.
if ! env JICHI_SPAWN_BACKEND=fork "$BIN" doctor 2>/dev/null | grep -q "posix_spawn available"; then
    t_skip "no posix_spawn in this build (doctor: process backend says so)"
fi

t_plan 9
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)
hookbytes="$tmp/hookbytes.txt"

# --- 1: the env flip takes (resolution, not just parsing) --------------------
if JICHI_SPAWN_BACKEND=spawn "$BIN" doctor 2>/dev/null \
        | grep -q "process backend: posix_spawn"; then
    t_ok "JICHI_SPAWN_BACKEND=spawn resolves to the posix_spawn backend"
else
    t_fail "doctor does not report the posix_spawn backend under the env var"
fi

# --- runs A (spawn) and B (fork): marker + scrub + hook stdin ----------------
# The marker is made by arithmetic so it can appear only in real child output,
# never in the command (docs/TESTING_RUNBOOK.md); the scrub check reads the
# count `env | grep -c` prints, so the key's VALUE never travels anywhere.
cat > "$tmp/a.mm" <<'EOF'
wire openai
rule
  match "RUNMARK"
  match "\"role\":\"tool\""
  text MARKDONE
rule
  match "RUNMARK"
  tool run_terminal_command {"command":"echo M_$((40+2))_OK; env | grep -c JICHI_API_KEY; true"}
EOF

run_mark() {  # $1 = backend word for the env var, $2 = capture dir
    mm_start "$tmp/a.mm" "$2" 4
    # runTimeout routes run_terminal_command through the WATCHED runner -- the
    # direct jc_spawn path -- rather than the popen path, whose caller-side
    # `unset` prefix (M724) is a second env guard that would satisfy check 3
    # whatever the spawn backend did. Here the backend's own scrub must carry
    # the fence alone, which is what makes the check able to fail.
    write_config "$tmp/cfg.json" "$MM_PORT" \
        '"runTimeout":30,"hooksEnabled":true,"hooks":{"Stop":[{"commands":[{"shell":"wc -c > '"$hookbytes"'"}]}]}'
    ( cd "$ws" && with_deadline 60 env "JICHI_SPAWN_BACKEND=$1" JICHI_API_KEY=hunter2 \
        "$BIN" --config "$tmp/cfg.json" -q --no-session --auto \
        -p "RUNMARK run it" < /dev/null > "$tmp/out.$1.txt" 2>&1 )
    mm_stop
}

rm -f "$hookbytes"
run_mark spawn "$tmp/capA"

if grep -rq "M_42_OK" "$tmp/capA" 2>/dev/null; then
    t_ok "spawn: the shell ran and its output closed the loop (marker in tool result)"
else
    t_fail "spawn: no M_42_OK in any captured request (out: $(head_bytes 200 "$tmp/out.spawn.txt"))"
fi
if grep -rq "M_42_OK..0" "$tmp/capA" 2>/dev/null; then
    t_ok "spawn: JICHI_API_KEY absent from the child environment (grep -c printed 0)"
else
    t_fail "spawn: the child's env grep did not print 0 -- the scrub did not hold on the spawn backend"
fi

if [ -s "$hookbytes" ] && [ "$(tr -cd '0-9' < "$hookbytes")" -gt 0 ] 2>/dev/null; then
    t_ok "spawn: the Stop hook received its event JSON on stdin ($(tr -cd '0-9' < "$hookbytes") bytes)"
else
    t_fail "spawn: the Stop hook read nothing on stdin (jc_proc_capture's stdin feed broke)"
fi

rm -f "$hookbytes"
run_mark fork "$tmp/capB"
if grep -rq "M_42_OK" "$tmp/capB" 2>/dev/null; then
    t_ok "fork: the same turn holds on the default backend (funnel parity)"
else
    t_fail "fork: no M_42_OK -- the jc_spawn funnel broke the DEFAULT backend (out: $(head_bytes 300 "$tmp/out.fork.txt"))"
fi

# --- 5 (spawn): SIGPIPE is at default in the child ---------------------------
# The child shell signals ITSELF with PIPE. At SIG_DFL it dies before the echo,
# so the survival marker must be absent; if the child inherited jichi's SIG_IGN
# (the M461 defect, on the backend that cannot run the child-side reset), the
# echo runs and the marker appears.
cat > "$tmp/p.mm" <<'EOF'
wire openai
rule
  match "RUNPIPE"
  match "\"role\":\"tool\""
  text PIPEDONE
rule
  match "RUNPIPE"
  tool run_terminal_command {"command":"kill -s PIPE $$; echo SURV_$((1200+34))"}
EOF
mm_start "$tmp/p.mm" "$tmp/capP" 4
write_config "$tmp/cfgp.json" "$MM_PORT"
( cd "$ws" && with_deadline 60 env JICHI_SPAWN_BACKEND=spawn "$BIN" \
    --config "$tmp/cfgp.json" -q --no-session --auto \
    -p "RUNPIPE run it" < /dev/null > "$tmp/out.pipe.txt" 2>&1 )
mm_stop
if grep -rq "SURV_1234" "$tmp/capP" 2>/dev/null; then
    t_fail "spawn: the child SURVIVED its own SIGPIPE -- it inherited SIG_IGN (M461 reopened on this backend)"
else
    t_ok "spawn: a child shell dies to SIGPIPE (disposition reset held)"
fi

# --- 6 (spawn): a timed-out pipeline is killed as a GROUP --------------------
# M461's measured failure: only the shell died and every other member of `a | b`
# was orphaned and kept running (eight `yes` processes, 3h50m). Both sleeps wear
# a distinctive duration; after the timeout kill, none may remain. The ps
# pattern brackets its first letter so it cannot match itself (the pgrep
# self-match class).
cat > "$tmp/k.mm" <<'EOF'
wire openai
rule
  match "RUNKILL"
  match "\"role\":\"tool\""
  text KILLDONE
rule
  match "RUNKILL"
  tool run_terminal_command {"command":"sleep 370 | sleep 370"}
EOF
mm_start "$tmp/k.mm" "$tmp/capK" 4
write_config "$tmp/cfgk.json" "$MM_PORT" '"runTimeout":2'
# TWO conditions, because each alone can pass vacuously (both shapes were seen
# while this check was given its teeth): survivors-only is masked by
# with_deadline -- GNU timeout group-kills everything jichi started when the
# 60s fallback fires -- and a pipeline that never STARTED also leaves none. So
# the run must also come back on the runTimeout (seconds), which only happens
# when jichi's own kill(-pid) took the whole pipeline: without the group, the
# runner blocks in waitpid until the fallback. 25s of slack covers a slow box;
# the two failure shapes land at ~60s.
_t0=$(date +%s)
( cd "$ws" && with_deadline 60 env JICHI_SPAWN_BACKEND=spawn "$BIN" \
    --config "$tmp/cfgk.json" -q --no-session --auto \
    -p "RUNKILL run it" < /dev/null > "$tmp/out.kill.txt" 2>&1 )
_t1=$(date +%s)
mm_stop
sleep 1
left=$(ps -eo args 2>/dev/null | grep -c "^[s]leep 370")
if [ "$left" -eq 0 ] && [ $((_t1 - _t0)) -lt 25 ]; then
    t_ok "spawn: runTimeout killed the whole pipeline ($((_t1 - _t0))s, no member left)"
else
    t_fail "spawn: group kill did not hold -- $left member(s) left, run took $((_t1 - _t0))s (fast+empty means the child led its own group; slow means the runner blocked past its kill)"
fi
ps -eo pid,args 2>/dev/null | grep "^ *[0-9]* [s]leep 370" | while read -r zpid zrest; do
    kill "$zpid" 2>/dev/null
done

# --- 7 (spawn): the popen path (search_code -> jc_proc_popen/jc_proc_pclose) -
printf 'alpha SPAWNNEEDLE_%s beta\n' "$((90+9))" > "$ws/needle.txt"
cat > "$tmp/s.mm" <<'EOF'
wire openai
rule
  match "RUNSEARCH"
  match "\"role\":\"tool\""
  text SEARCHDONE
rule
  match "RUNSEARCH"
  tool search_code {"pattern":"SPAWNNEEDLE"}
EOF
mm_start "$tmp/s.mm" "$tmp/capS" 4
write_config "$tmp/cfgs.json" "$MM_PORT"
( cd "$ws" && with_deadline 60 env JICHI_SPAWN_BACKEND=spawn "$BIN" \
    --config "$tmp/cfgs.json" -q --no-session --auto \
    -p "RUNSEARCH run it" < /dev/null > "$tmp/out.search.txt" 2>&1 )
mm_stop
if grep -rq "SPAWNNEEDLE_99" "$tmp/capS" 2>/dev/null; then
    t_ok "spawn: search_code found the needle (jc_proc_popen stream over the spawn backend)"
else
    t_fail "spawn: search_code returned no SPAWNNEEDLE_99 (out: $(head_bytes 200 "$tmp/out.search.txt"))"
fi

# --- 8+9 (spawn): the git tool (argv exec, no shell) -------------------------
( cd "$ws" && git init -q . && git add needle.txt && \
  git -c user.email=t@e -c user.name=t commit -qm "c_$((70+7))_subject" ) 2>/dev/null
cat > "$tmp/g.mm" <<'EOF'
wire openai
rule
  match "RUNGIT"
  match "\"role\":\"tool\""
  text GITDONE
rule
  match "RUNGIT"
  tool git_log {"limit":1}
EOF
mm_start "$tmp/g.mm" "$tmp/capG" 4
write_config "$tmp/cfgg.json" "$MM_PORT"
( cd "$ws" && with_deadline 60 env JICHI_SPAWN_BACKEND=spawn "$BIN" \
    --config "$tmp/cfgg.json" -q --no-session --auto \
    -p "RUNGIT run it" < /dev/null > "$tmp/out.git.txt" 2>&1 )
mm_stop
if grep -rq "c_77_subject" "$tmp/capG" 2>/dev/null; then
    t_ok "spawn: git_log returned the c_77_subject commit (argv exec path)"
else
    t_fail "spawn: git_log did not return the c_77_subject commit (out: $(head_bytes 200 "$tmp/out.git.txt"))"
fi

t_done
