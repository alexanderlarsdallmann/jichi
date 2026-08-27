#!/bin/sh
# smoke: an unset HOME does not silently relocate jichi's state to /tmp (M472).
#
# THE DEFECT THIS EXISTS FOR. jc_home_dir() was `getenv("HOME")` or else the
# literal "/tmp", and 49 call sites root everything private at it: the config,
# ~/.jichi.env (the API key), sessions, telemetry, the run journals, the audit
# log. /tmp is world-writable and sticky -- and the sticky bit stops another user
# DELETING your files, not CREATING them at a path you have not used yet. So a
# local attacker could pre-create /tmp/.jichi.env, or make /tmp/.jichi.d a
# symlink into a directory they own, and M132's 0600/0700 could not help: those
# modes are applied to whatever the path RESOLVES to.
#
# It failed silently, which is the worst property it could have -- a loop that
# has been writing its audit trail to a shared /tmp for a month looks exactly
# like one that has not. And HOME is unset in precisely the unattended
# environments AUTONOMOUS_LOOPS.md recommends.
#
# See docs/analysis/2026-08-17-source-hardening-audit.md §M4.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)

cat > "$tmp/config.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:1/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"lowResource":false}
EOF

# 1. With HOME unset, the resolved state root is NOT /tmp. `env -u` is not POSIX,
#    so unset it in a subshell instead.
out=$(unset HOME; "$BIN" doctor --config "$tmp/config.json" 2>&1)
root=$(printf '%s\n' "$out" | sed -n 's/.*state resolved to \([^.]*\)\..*/\1/p' | head -1)
case "$root" in
    /tmp|/tmp/)
        t_fail "state root fell back to /tmp (world-writable): $root" ;;
    "")
        t_fail "no state root reported at all: $(printf '%s' "$out" | tail -c 200)" ;;
    *)
        t_ok "HOME unset resolves away from /tmp ($root)" ;;
esac

# 2. It is REPORTED, not silent. The old behaviour's real damage was that
#    nothing said anything.
if printf '%s\n' "$out" | grep -q 'HOME is not set'; then
    t_ok "the unset HOME is reported on stderr"
else
    t_fail "an unset HOME produced no notice"
fi

# 3. doctor carries it as a posture check, so it is visible in the report and not
#    only in a startup line somebody scrolled past.
if printf '%s\n' "$out" | grep -q 'state root: HOME not set'; then
    t_ok "doctor reports it as a posture check"
else
    t_fail "doctor has no state-root check"
fi

# 4. --unattended escalates it to a FAIL (M158b's explicit per-check pattern),
#    so a loop supervisor gating on the exit code stops instead of running on in
#    the wrong state root. Asserted on the JSON report's status for THIS check
#    rather than on the process exit code: --unattended also judges the whole
#    unattended-loop posture, so the exit code is a sum of other checks and would
#    make this one pass or fail for reasons that have nothing to do with HOME.
st_plain=$(unset HOME; "$BIN" doctor --output json --config "$tmp/config.json" \
           2>/dev/null | tr ',' '\n' | grep -B1 '"label":"state root' | \
           sed -n 's/.*"status":"\([a-z]*\)".*/\1/p' | head -1)
st_unatt=$(unset HOME; "$BIN" doctor --unattended --output json \
           --config "$tmp/config.json" 2>/dev/null | tr ',' '\n' | \
           grep -B1 '"label":"state root' | \
           sed -n 's/.*"status":"\([a-z]*\)".*/\1/p' | head -1)
if [ "$st_plain" = "warn" ] && [ "$st_unatt" = "fail" ]; then
    t_ok "the check is a WARN interactively and a FAIL under --unattended"
else
    t_fail "escalation wrong: plain=$st_plain (want warn), unattended=$st_unatt (want fail)"
fi

t_done
