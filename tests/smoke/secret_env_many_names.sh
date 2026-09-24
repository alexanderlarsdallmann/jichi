#!/bin/sh
# smoke: however many key variables a config names, and however long, a
# model-issued command runs without them -- on the popen path and the fork path,
# and the scrub fails CLOSED, never open (M724).
#
# THE DEFECTS, two holes in one promise. The registry of names to scrub held 32
# names of under 128 bytes and IGNORED anything past either bound, so a 33rd
# configured apiKeyEnv reached every child jichi forks. And the popen path of
# `run_terminal_command` (the default: no per-call timeout, no memory budget),
# which cannot scrub its child itself -- popen execs the shell directly -- built
# its `unset NAME ...;` prefix into a 1 KB buffer; when that did not fit,
# jc_proc_secret_env_prefix returned 0, the same answer as "nothing registered",
# and the command ran with NO prefix: every key the config named, and every
# built-in one, stayed in the environment of a command the model chose. Found
# while fixing M721 and recorded in docs/DEFERRED.md as "read, not reproduced";
# twelve names of ~100 bytes reproduce it.
#
# The probe is tests/smoke/secret_env_subcommands.sh's: the command writes the
# COUNT of matching variables in its own environment to a file the driver reads,
# so "unread" means the command never ran, and the values -- set only in jichi's
# environment, never in the command text -- cannot be matched by the command's
# own words (the M721 trap).
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# A config naming one model per key variable in $names; the first answers chat.
config_for() { # config_for FILE PORT
    {
        printf '{"models":['
        first=1
        for n in $names; do
            [ "$first" -eq 1 ] || printf ','
            if [ "$first" -eq 1 ]; then roles=',"roles":["chat"]'; else roles=''; fi
            printf '{"name":"m_%s","provider":"openai","model":"mock","apiBase":"http://127.0.0.1:%s/v1","apiKeyEnv":"%s"%s}' \
                "$n" "$2" "$n" "$roles"
            first=0
        done
        printf '],"snapshots":false,"repoMap":false,"references":false,"toolProfile":"full",'
        printf '"lowResource":false,"maxRetries":0}\n'
    } > "$1"
}

# The environment jichi runs in: every name in $names set to a canary.
canary_env() {
    for n in $names; do
        printf '%s=canary\n' "$n"
    done
}

# probe TAG PATTERN [TIMEOUT] -- one headless turn whose single tool call counts
# the variables matching PATTERN in its own environment. A timeout sends the call
# down the watched FORK path; without one it takes the popen path. Extra
# environment for jichi comes from $extra_env. Sets $count.
probe() {
    if [ -n "${3:-}" ]; then targ=",\"timeout\":$3"; else targ=''; fi
    cat > "$tmp/$1.mm" <<MM
wire openai
rule
  count 1
  tool run_terminal_command {"command":"env | grep -c -e '$2' > '$tmp/$1.n'; exit 0"$targ}
rule
  text SCRUB_DONE
MM
    mm_start "$tmp/$1.mm" "$tmp/$1.cap" 2
    config_for "$tmp/$1.json" "$MM_PORT"
    # shellcheck disable=SC2046 -- each canary is one word by construction
    (cd "$ws" && with_deadline 60 env $(canary_env) $extra_env "$BIN" --config "$tmp/$1.json" \
        -q --no-session --auto -p "check the env" < /dev/null) > "$tmp/$1.out" 2> "$tmp/$1.err"
    rc=$?
    mm_stop
    count=$(cat "$tmp/$1.n" 2>/dev/null || echo unread)
}

# Twelve names of 104 bytes: with the built-ins, a prefix near 1.5 KB.
pad=PADDINGPADDINGPADDINGPADDINGPADDINGPADDINGPADDINGPADDINGPADDINGPADDINGPADDINGPADDINGPADDINGPA
names=''
i=10
while [ "$i" -le 21 ]; do names="$names JC_M724_${i}_$pad"; i=$((i + 1)); done
extra_env=''

# --- 1: the denominator -- the command ran at all --------------------------------
probe long '^JC_M724_'
if [ "$count" != "unread" ] && grep -q SCRUB_DONE "$tmp/long.out"; then
    t_ok "the model-issued command ran on the popen path (rc=$rc)"
else
    t_fail "the command never ran (count $count, rc=$rc) -- checks 2-5 prove nothing: \
$(head_bytes 200 < "$tmp/long.err" | tr '\n' ' ')"
fi

# --- 2: none of twelve long configured names reached it --------------------------
if [ "$count" = "0" ]; then
    t_ok "none of the 12 long configured key variables reached a popen-path command"
else
    t_fail "$count of 12 configured key variables reached a model-issued command -- the \
unset prefix did not fit and was dropped whole (M724)"
fi

# --- 3: nor the built-ins, in the same run ---------------------------------------
# The built-in half of the same prefix: before the fix it went with the rest.
extra_env='JICHI_API_KEY=canary-builtin'
probe builtin '^JICHI_API_KEY='
extra_env=''
if [ "$count" = "0" ]; then
    t_ok "a stray JICHI_API_KEY is dropped in the same many-names run"
else
    t_fail "JICHI_API_KEY reached the command beside the twelve (count=$count)"
fi

# --- 4 and 5: the 33rd name, on both paths -- the registry stopped at 32 ----------
# Thirty-three short names: the prefix FITS, so this is the registry's cap alone.
names=''
i=10
while [ "$i" -le 42 ]; do names="$names JC_M724B_$i"; i=$((i + 1)); done
probe cap33 '^JC_M724B_'
if [ "$count" = "0" ]; then
    t_ok "all 33 configured key variables are dropped from a popen-path command"
else
    t_fail "$count of 33 configured key variables reached a popen-path command -- the \
registry held 32 and ignored the rest (M724)"
fi
probe cap33fork '^JC_M724B_' 30
if [ "$count" = "0" ]; then
    t_ok "all 33 are dropped on the watched fork path too (a timeout set)"
else
    t_fail "$count of 33 configured key variables reached a fork-path command -- the \
child's scrub never knew the 33rd name (M724)"
fi

t_done
