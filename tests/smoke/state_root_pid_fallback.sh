#!/bin/sh
# smoke: the pid-scoped HOME-fallback directory is CHECKED, not assumed (C-16/M1134).
#
# THE DEFECT THIS EXISTS FOR. With HOME unset and no password entry, jc_home_dir
# falls back to /tmp/jichi-<uid>; when that fails the owner/mode check it moves
# to /tmp/jichi-<uid>-<pid>. That second directory was mkdir'd and USED without
# the lstat/owner/mode check its sibling got, on the false assumption "a
# pid-scoped name cannot have been waiting for us". But pids are predictable: a
# hostile local user can pre-create /tmp/jichi-<uid>-<pid> (or a range of them)
# at 0777, and jichi would root its key file and sessions in a directory another
# user controls.
#
# THE FIX UNDER TEST (M1134): the pid path is judged by the SAME four-predicate
# check; when it is planted too, jichi creates a fresh unique directory with
# mkdtemp (0700, owned by us, by construction); when even that fails it
# degrades loudly and NEVER returns NULL.
#
# THE PROBE. Every bench has a passwd entry for its own uid, so the /tmp branch
# is unreachable without dropping privileges. Under a FAULT=1 build, env
# JICHI_FAULT_HOME_NOPASSWD=1 makes jc_home_dir answer "no password entry", so
# the branch runs deterministically. This driver skips on any other build (the
# probe is compiled out: a release binary cannot be degraded by the env). It is
# named in tests/smoke/run.sh (the FAULT-skipping list) and in the Makefile's
# smoke-faults target, so `make ci` builds the FAULT=1 binary it needs.
#
# THE CHILD'S PID IS KNOWN, DETERMINISTICALLY. The naive alternative --
# pre-plant a RANGE /tmp/jichi-<uid>-<p> for p in [driver pid, driver
# pid+4096) -- is self-defeating: the planting loop forks an external mkdir and
# chmod per directory, and those forks consume pids and push the jichi child
# past the end of the range it just planted, so the child's own pid dir is
# never the one that was planted and check 1 silently tests nothing. Instead
# this driver plants the child's EXACT pid dir: it launches `sh -c` which,
# knowing its own pid ($$) and that exec preserves it, creates
# /tmp/jichi-<uid>-$$ at 0777 and then execs jichi -- so jichi's getpid() is
# precisely the pid whose dir was just planted. No race, no range.
#
# IT NEVER DESTROYS A PRE-EXISTING DIRECTORY. If /tmp/jichi-<uid> already
# exists the driver records its original permissions and existence and
# restores both at the end, so a real jichi state root on a shared host is
# left exactly as found. If that pre-existing directory belongs to ANOTHER
# account the driver cannot safely make it 0777 (it would break the other
# user's state root), and it skips before it plants anything.
#
# See docs/internal/reviews/2026-10-17-hardening/C-network-secrets.md C-16.
. "$(dirname "$0")/_smoke.sh"

# A FAULT build announces itself in --version -- the reliable probe (M1087).
"$BIN" --version < /dev/null 2>/dev/null | grep -q "FAULT=1" \
    || t_skip "requires the FAULT=1 build (make clean && make WERROR=1 FAULT=1 jichi smoke-tools)"

uid=$(id -u)
# ls -ld prints the owner NAME, so the owner comparison must use the name too
# (id -un), not the numeric uid. Driver and jichi child run as the same account.
uid_name=$(id -un)
uid_dir="/tmp/jichi-$uid"

# Skip, before planting anything, if a pre-existing uid dir is not ours:
# making it 0777 would break another user's real state root. (smoke_lint check
# 20: a t_skip after t_plan emits a second TAP plan line, so this sits before
# the plan, beside the FAULT skip.)
if [ -d "$uid_dir" ]; then
    own=$(ls -ld "$uid_dir" 2>/dev/null | awk '{print $3}' | head -1)
    [ "$own" = "$uid_name" ] \
        || t_skip "pre-existing $uid_dir is not owned by $uid_name; unsafe to plant"
fi

t_plan 3
tmp=$(smoke_tmp)

cat > "$tmp/config.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:1/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"lowResource":false}
EOF

plant_file="$tmp/planted"
: > "$plant_file"

# perm_to_octal "rwx------" -> "700". Converts ls -ld's 9-char permission field
# to the octal form chmod accepts, so a pre-existing uid dir's original mode can
# be restored byte-for-byte. stat -c is banned by posix_utils_lint; this is the
# portable substitute.
perm_to_octal() {
    _p=$1
    _d() {
        _c=$(printf '%s' "$_p" | cut -c"$1")
        _n=0
        case $_c in r) _n=$((_n + 4));; esac
        case $_c in w) _n=$((_n + 2));; esac
        case $_c in x) _n=$((_n + 1));; esac
        printf '%s' "$_n"
    }
    printf '%s%s%s' "$(_d 1)" "$(_d 2)" "$(_d 3)"
}

uid_preexisted=0
uid_orig_perm=""
if [ -e "$uid_dir" ]; then
    uid_preexisted=1
    # The FULL 9-char permission field (cut -c2-10; ls -ld's col 1 is the file
    # type). cut -c5-10 would be only group/other.
    uid_orig_perm=$(ls -ld "$uid_dir" 2>/dev/null | cut -c2-10)
fi

# unplant: remove the planted pid dirs (recorded in $plant_file) and the jichi
# state roots this driver's own runs created (root1, root2), and restore the
# uid dir to its recorded existence/permissions. rm -rf on root1/root2 is safe:
# they are directories jichi created fresh during THIS driver's runs (a planted
# pid dir refused-then-recreated, or a mkdtemp dir), never the uid dir (excluded).
unplant() {
    while IFS= read -r _cp; do
        rm -rf "$_cp" 2>/dev/null
    done < "$plant_file"
    [ -n "${root1:-}" ] && [ "$root1" != "$uid_dir" ] && rm -rf "$root1" 2>/dev/null
    [ -n "${root2:-}" ] && [ "$root2" != "$uid_dir" ] && rm -rf "$root2" 2>/dev/null
    if [ "$uid_preexisted" = 1 ]; then
        [ -d "$uid_dir" ] || mkdir "$uid_dir" 2>/dev/null
        chmod "$(perm_to_octal "$uid_orig_perm")" "$uid_dir" 2>/dev/null
    else
        rm -rf "$uid_dir" 2>/dev/null
    fi
}
trap unplant EXIT INT TERM

# dir_rule DIR -- succeeds when DIR is a directory, owned by this account,
# mode 0700 -- the same four predicates the C fix applies, asserted from the
# shell. (stat -c is banned by posix_utils_lint; ls -ld is the portable idiom;
# its owner field is the NAME, so compare against id -un, not the numeric uid.)
dir_rule() {
    [ -d "$1" ] || return 1
    [ "$(ls -ld "$1" 2>/dev/null | awk '{print $3}' | head -1)" = "$uid_name" ] || return 1
    # 0700 = the full 9-char permission field "rwx------" (cut -c2-10).
    [ "$(ls -ld "$1" 2>/dev/null | cut -c2-10)" = "rwx------" ]
}

# state_root_of FILE -- doctor's reported state root, or "".
state_root_of() {
    sed -n 's/.*state resolved to \([^.]*\)\..*/\1/p' "$1" | head -1
}

# is_planted PATH -- 0 when PATH is the uid dir or one of our plant list.
is_planted() {
    [ "$1" = "$uid_dir" ] && return 0
    [ -n "$1" ] && grep -qxF "$1" "$plant_file" 2>/dev/null && return 0
    return 1
}

# run_doctor PLANT -- run `jichi doctor` with HOME unset and the probe armed.
# If PLANT=1, the child pre-plants its OWN pid dir at 0777 before exec (so
# jichi's exact /tmp/jichi-<uid>-<pid> is a pre-planted 0777 dir); otherwise it
# does not (so jichi's pid dir is created fresh by jichi at 0700). Writes the
# created pid dir, if any, to $plant_file. Output goes to $tmp/out_cur.
run_doctor() {
    plant=$1
    sh -c '
        uid=$1; bin=$2; cfg=$3; pf=$4; plant=$5
        if [ "$plant" = 1 ]; then
            d="/tmp/jichi-$uid-$$"
            mkdir "$d" 2>/dev/null
            chmod 0777 "$d" 2>/dev/null
            printf "%s\n" "$d" >> "$pf"
        fi
        export JICHI_FAULT_HOME_NOPASSWD=1
        unset HOME
        exec "$bin" doctor --config "$cfg" < /dev/null
    ' _ "$uid" "$BIN" "$tmp/config.json" "$plant_file" "$plant" > "$tmp/out_cur" 2>&1
}

# ============ 1. THE RED LINE (the mkdtemp leg) ============================
# Make the uid dir unusable (0777) AND pre-plant the child's exact pid dir at
# 0777, so BOTH of jichi's deterministic candidates are planted. A pre-fix
# jichi refuses the uid dir (0777 fails its check) and returns the first pid
# path it finds UNCHECKED -- the planted 0777 dir; the fixed jichi refuses both
# and falls through to a fresh mkdtemp directory that passes dir_rule.
mkdir "$uid_dir" 2>/dev/null
chmod 0777 "$uid_dir" 2>/dev/null
run_doctor 1
root1=$(state_root_of "$tmp/out_cur")
if [ -n "$root1" ] && ! is_planted "$root1" && dir_rule "$root1"; then
    t_ok "1 planted uid+pid dirs refused; resolved to a fresh 0700 dir ($root1)"
else
    t_fail "1 resolved to a planted/unsafe root (root1='$root1') -- the pid-scoped HOME fallback is used unchecked"
fi

# ============ 2. the happy pid leg (pid dir NOT planted) ===================
# uid dir still 0777 (unusable), child's pid dir NOT pre-planted -> jichi
# refuses the uid dir, mkdir's its own /tmp/jichi-<uid>-<pid> at 0700 and the
# check passes: the checked pid leg through the real binary.
run_doctor 0
root2=$(state_root_of "$tmp/out_cur")
if [ -n "$root2" ] && [ "$root2" != "$uid_dir" ] && dir_rule "$root2"; then
    t_ok "2 happy pid leg: a checked 0700 dir ($root2)"
else
    t_fail "2 pid leg did not resolve to a checked 0700 dir (root2='$root2')"
fi

# ============ 3. the probe is what changes the leg =========================
# Without the probe env (still FAULT build, HOME unset) getpwuid answers the
# account home, so the /tmp branch is NOT reached: root must not be under
# /tmp. Proves the probe, not the build, is what flips the leg.
(
    unset HOME
    "$BIN" doctor --config "$tmp/config.json" < /dev/null > "$tmp/out_cur" 2>&1
)
root3=$(state_root_of "$tmp/out_cur")
case "$root3" in
    /tmp|/tmp/*)
        t_fail "3 without the probe, the /tmp branch was reached anyway (root3='$root3')" ;;
    "")
        t_fail "3 no state root reported without the probe: $(tail -c 200 "$tmp/out_cur")" ;;
    *)
        t_ok "3 without the probe, getpwuid resolves the account home (root3='$root3', not /tmp)" ;;
esac

t_done
