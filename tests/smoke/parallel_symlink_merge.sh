#!/bin/sh
# M1099 (D-12): the parallel merge follows symlinks at both ends.
#
# apply_change() (src/tools/jc_tool_parallel.c) copies a child's change by
# jc_read_file(src) + jc_write_file(dst); both fopen() FOLLOW symlinks. The
# 2026-10-17 review (D-12) measured three shapes:
#   SRC: a child leaves a SYMLINK at a path (git: typechange T). The merge
#        reads THROUGH the link and writes the target's CONTENT into the
#        live tree -- a link becomes a copy of its target.
#   DST: the committed base has a TRACKED symlink; the child makes it a
#        regular file. The merge's write opens the LIVE path and writes
#        THROUGH the link into the outside target.
#   FIFO: a child leaves a symlink to a FIFO. The merge's read() of the
#        FIFO blocks until a writer appears -- the LEAD stalls mid-merge.
#   MODE: a child rewrites a 0644 base file as 0755. The unfixed merge's
#        jc_write_file(dst) truncates the LIVE 0644 file in place, so the
#        merged file keeps 0644 -- the child's mode is dropped.
#   FIFO-DIRECT: a child leaves a FIFO DIRECTLY at the path (no link). The
#        unfixed merge's jc_read_file(src) opens the FIFO and blocks; the
#        deadline kills the LEAD. (A link to a FIFO is leg 7's shape; this
#        is the pipe itself.)
#   NEWLINK: the child adds a RELATIVE symlink inside a directory the
#        committed base does not have (newd/lnk -> ../victim.txt). The
#        M1099 link arm's symlink(target, dst) needs the parent directory,
#        which it never creates: pre-M1099-c the symlink() fails (ENOENT),
#        apply_change returns 0, and the M927 conflict arm preserves the
#        worktree -- the link never lands in the live tree. M1099-c
#        (SV:1144.2) creates the parent, the same dstdir/jc_mkdir_p shape
#        the S_ISREG arm already uses.
# This driver drives all six through a real write child (mockmodel) and
# checks the effect on disk. On the unfixed (pre-M1099) tree checks 2, 5,
# 6, 7, 10 and 11 are red (SRC link-copy, DST write-through, the DST
# typechange never landing, the FIFO leg stalling to its 60 s deadline, the
# MODE leg keeping 0644 instead of the child's 0755, and the FIFO-DIRECT
# leg stalling to its 60 s deadline); 1, 3, 4, 8, 9 and 12 are green. After
# M1099 all 12 are green. M1099-c (SV:1144.2) adds the NEWLINK leg: on the
# M1099 tree (the M1099 fix present, M1099-c absent) check 13 is red and
# 1-12 green. After M1099-c all 13 are green.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not on PATH"

t_plan 13
smoke_home
tmp=$(smoke_tmp)

mm_cfg() {  # writes $tmp/cfg.json against the currently announced MM_PORT
    cat > "$tmp/cfg.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"toolProfile":"full",
 "maxRetries":0,"maxParallelAgents":1,"maxSubagentIters":3,"maxToolIters":4,
 "parallelTaskTimeout":120,"timeouts":{"stall":120}}
EOF
}

mm_rules() {  # $1 = the child's shell command (the swap) -> writes $tmp/r.mm
    cat > "$tmp/r.mm" <<EOF
wire openai
rule
  match "ESCAP"
  nomatch "\\"role\\":\\"tool\\""
  tool run_terminal_command {"command":"$1"}
rule
  match "ESCAP"
  text CHILD-DONE
rule
  match "LEAD"
  tool spawn_parallel {"tasks":[{"task":"ESCAP swap victim","write":true}]}
rule
  text LEAD-DONE
EOF
}

run_leg() {  # $1 = label; $2 = child command; $3 = deadline s
    local_ws="$tmp/ws.$1"
    mm_rules "$2"
    mm_start_unbounded "$tmp/r.mm" "$tmp/cap.$1"
    mm_cfg
    ( cd "$local_ws" && with_deadline "$3" "$BIN" --config "$tmp/cfg.json" \
        -q --no-session --auto --no-stdin -p "LEAD run one writer" \
        < /dev/null > "$tmp/out.$1.txt" 2>&1 )
    RC=$?
    mm_stop
}

# ---- leg SRC: base victim.txt regular; child swaps it for a link -> our outside file ----
ws1="$tmp/ws.src"; mkdir -p "$ws1" "$tmp/outside"
printf 'outside-src-before\n' > "$tmp/outside/src-target.txt"
( cd "$ws1" && git init -q . && git config user.email t@t && git config user.name t \
    && printf 'live-victim\n' > victim.txt && git add -A && git commit -qm init )
# the child's command is embedded RAW in mockmodel's {"command":"..."} JSON, so
# it must carry no double quotes: use the unquoted absolute path (no whitespace).
run_leg src "rm -f victim.txt; ln -s $tmp/outside/src-target.txt victim.txt; echo FENCE-CMD-RAN" 90
if [ "$RC" = 0 ]; then
    t_ok "SRC: the lead finished (rc=0)"
else
    t_fail "SRC: the lead did not finish (rc=$RC)"
fi
if [ -L "$ws1/victim.txt" ] && [ "$(readlink "$ws1/victim.txt")" = "$tmp/outside/src-target.txt" ]; then
    t_ok "SRC: the child's link re-created as a link (victim.txt -> $tmp/outside/src-target.txt)"
else
    t_fail "SRC: the link landed wrong (a typechange T must stay a symlink): $(ls -l "$ws1/victim.txt" | sed 's/^ *//')"
fi
if [ -f "$tmp/outside/src-target.txt" ] && [ "$(cat "$tmp/outside/src-target.txt")" = "outside-src-before" ]; then
    t_ok "SRC: the outside file the link points at was not touched (still 'outside-src-before')"
else
    t_fail "SRC: the outside file the link points at was written: [$(cat "$tmp/outside/src-target.txt" 2>/dev/null)]"
fi

# ---- leg DST: base victim.txt a TRACKED link -> outside file; child makes it regular ----
printf 'outside-before\n' > "$tmp/outside/dst-target.txt"
ws2="$tmp/ws.dst"; mkdir -p "$ws2"
( cd "$ws2" && git init -q . && git config user.email t@t && git config user.name t \
    && ln -s "$tmp/outside/dst-target.txt" victim.txt && git add -A && git commit -qm init )
run_leg dst "rm -f victim.txt; printf 'CHILD-CONTENT\n' > victim.txt; echo FENCE-CMD-RAN" 90
if [ "$RC" = 0 ]; then
    t_ok "DST: the lead finished (rc=0)"
else
    t_fail "DST: the lead did not finish (rc=$RC)"
fi
if [ "$(cat "$tmp/outside/dst-target.txt")" = "outside-before" ]; then
    t_ok "DST: the outside target was NOT written through the link (still 'outside-before')"
else
    t_fail "DST: the merge wrote THROUGH the live link: outside target is now [$(cat "$tmp/outside/dst-target.txt")]"
fi
if [ ! -L "$ws2/victim.txt" ] && [ "$(cat "$ws2/victim.txt")" = "CHILD-CONTENT" ]; then
    t_ok "DST: the child's regular-file content reached the live tree ('CHILD-CONTENT')"
else
    t_fail "DST: victim.txt wrong after merge: $(ls -l "$ws2/victim.txt" | sed 's/^ *//')"
fi

# ---- leg FIFO: base victim.txt regular; child swaps it for a link -> an outside FIFO ----
mkfifo "$tmp/outside/pipe"
ws3="$tmp/ws.fifo"; mkdir -p "$ws3"
( cd "$ws3" && git init -q . && git config user.email t@t && git config user.name t \
    && printf 'live-victim\n' > victim.txt && git add -A && git commit -qm init )
run_leg fifo "rm -f victim.txt; ln -s $tmp/outside/pipe victim.txt; echo FENCE-CMD-RAN" 60
if [ "$RC" = 0 ]; then
    t_ok "FIFO: the lead finished within its deadline (rc=0; a merge blocked on the FIFO would be killed at 60 s)"
else
    t_fail "FIFO: the lead did not finish in 60 s (rc=$RC) -- the merge followed the link and read() blocked on the FIFO"
fi
# guard: the stall above is only meaningful if the FIFO target was actually
# present for the merge's read() to block on -- prove the setup was intact.
if [ -p "$tmp/outside/pipe" ]; then
    t_ok "FIFO: the outside FIFO target still exists (the stall, if any, was on a real pipe)"
else
    t_fail "FIFO: the outside FIFO target is missing -- the leg did not test what it claims"
fi

# ---- leg MODE: base victim.txt a 0644 regular file; the child rewrites it 0755 ----
ws4="$tmp/ws.mode"; mkdir -p "$ws4"
( cd "$ws4" && git init -q . && git config user.email t@t && git config user.name t \
    && printf 'live-victim\n' > victim.txt && chmod 644 victim.txt \
    && git add -A && git commit -qm init )
run_leg mode "rm -f victim.txt; printf 'CHILD-755\n' > victim.txt; chmod 755 victim.txt; echo FENCE-CMD-RAN" 60
if [ -f "$ws4/victim.txt" ] && [ ! -L "$ws4/victim.txt" ]; then
    t_ok "MODE: victim.txt exists and is a regular file after the merge"
else
    t_fail "MODE: victim.txt missing or not a regular file after the merge: $(ls -l "$ws4/victim.txt" 2>&1 | sed 's/^ *//')"
fi
# stat -c is banned by posix_utils_lint; the portable probe (the
# parallel_transcript idiom) is ls -l's mode field: 0755 is exactly -rwxr-xr-x.
mode755=$(ls -l "$ws4/victim.txt" 2>/dev/null | cut -c1-10)
if [ "$mode755" = "-rwxr-xr-x" ]; then
    t_ok "MODE: the merged file keeps the child's 0755 mode (ls -l: -rwxr-xr-x)"
else
    t_fail "MODE: the merged file is '$mode755' (want -rwxr-xr-x) -- the unfixed merge truncates the live 0644 file in place and drops the child's mode"
fi

# ---- leg FIFO-DIRECT: base victim.txt regular; the child leaves a FIFO DIRECTLY at the path ----
ws5="$tmp/ws.fifo2"; mkdir -p "$ws5"
( cd "$ws5" && git init -q . && git config user.email t@t && git config user.name t \
    && printf 'live-victim\n' > victim.txt && git add -A && git commit -qm init )
run_leg fifo2 "rm -f victim.txt; mkfifo victim.txt; echo FENCE-CMD-RAN" 60
if [ "$RC" = 0 ]; then
    t_ok "FIFO-DIRECT: the lead finished (rc=0; a merge reading the FIFO would be killed at its 60 s deadline)"
else
    t_fail "FIFO-DIRECT: the lead did not finish (rc=$RC) -- the merge's read of the FIFO blocked until the deadline"
fi
if [ -f "$ws5/victim.txt" ] && [ ! -p "$ws5/victim.txt" ] && [ "$(cat "$ws5/victim.txt")" = "live-victim" ]; then
    t_ok "FIFO-DIRECT: a refused merge must not touch the live victim.txt (still the committed regular file 'live-victim'; the worktree is preserved, cf. the result's 'Kept' line)"
else
    t_fail "FIFO-DIRECT: the live victim.txt was touched: $(ls -l "$ws5/victim.txt" 2>&1 | sed 's/^ *//')"
fi

# ---- leg NEWLINK: base victim.txt regular; child adds a RELATIVE link in a NEW directory ----
# (M1099-c, SV:1144.2: a link in a new directory must land. The target is a
# relative path, like a real repo-internal link; it resolves against the
# link's own directory, newd/.)
ws6="$tmp/ws.newd"; mkdir -p "$ws6"
( cd "$ws6" && git init -q . && git config user.email t@t && git config user.name t \
    && printf 'live-victim\n' > victim.txt && git add -A && git commit -qm init )
run_leg newd "mkdir -p newd; ln -s ../victim.txt newd/lnk; echo FENCE-CMD-RAN" 90
if [ -L "$ws6/newd/lnk" ] && [ "$(readlink "$ws6/newd/lnk")" = "../victim.txt" ]; then
    t_ok "NEWLINK: a link in a NEW directory lands (newd/lnk -> ../victim.txt, relative target)"
else
    t_fail "NEWLINK: the link in a new directory never landed: $(ls -l "$ws6/newd/lnk" 2>&1 | sed 's/^ *//') (M1099-c: the S_ISLNK arm's symlink() needs newd/ in the live tree; the M927 conflict arm preserved the worktree instead)"
fi

t_done
