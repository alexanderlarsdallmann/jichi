#!/bin/sh
# smoke: search_code still works where grep does not accept -I (M658).
#
# THE DEFECT THIS EXISTS FOR, and it is the second of its kind. `search_code`
# runs `grep -rnI`, and the comment at that line records that the flags were
# "checked against each usage string, not assumed" for GNU, FreeBSD, NetBSD and
# OpenBSD grep. The first illumos row added a fifth grep: /usr/bin/grep and
# /usr/xpg4/bin/grep both accept -r and reject -I --
#
#     usage:  grep [-E|-F] [-bchHilLnoqrRsvx] [-A num] [-B num] ...
#
# -- so every search exited 2 and the tool was dead on that platform. That is
# the same shape as M461's `--color` defect one platform later: four greps
# accepting a flag is not every grep accepting it.
#
# The fix is a one-shot capability probe (`grep -I -e x /dev/null`) whose answer
# selects the prefix. The point of THIS driver is that the fallback branch can
# only occur on a platform nobody here runs, so it is exercised with a grep that
# behaves like illumos's: a shim on PATH that rejects -I and otherwise defers to
# the real grep. A branch that only runs on a machine we do not have is a branch
# nobody has watched work.
#
# WHAT IS AND IS NOT COVERED:
#   checked -- the shim really rejects -I (so check 3 cannot pass vacuously);
#              search_code finds a match with the normal grep; it still finds the
#              same match with the shim; and the shim was actually asked without
#              -I, which is what proves the FALLBACK ran rather than some other
#              path happening to succeed.
#   not checked -- that binary files are skipped. They are not, on the fallback
#              branch, and that is the stated cost of the flag being unavailable
#              (src/tools/jc_tool_search.c says so at the probe).
. "$(dirname "$0")/_smoke.sh"

t_plan 7

smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
bin="$tmp/shimbin"
mkdir -p "$bin" "$ws/src"

REAL_GREP=$(command -v grep)
printf 'int marker_needle(void) { return 1; }\n' > "$ws/src/x.c"

# The shim: illumos's grep, in the one respect that matters. It logs every call
# so check 4 can prove which form jichi actually used.
cat > "$bin/grep" <<SHIM
#!/bin/sh
echo "\$@" >> "$tmp/greplog"
for a in "\$@"; do
    case "\$a" in
        -*I*)
            echo "usage:  grep [-E|-F] [-bchHilLnoqrRsvx] [-A num] [-B num]" >&2
            exit 2 ;;
    esac
done
exec "$REAL_GREP" "\$@"
SHIM
chmod +x "$bin/grep"

# --- 1: the fixture is real ---------------------------------------------------
# Without this the whole driver can pass while testing nothing: a shim that
# quietly accepted -I would make check 3 a restatement of check 2.
if "$bin/grep" -rnI -e marker_needle "$ws" >/dev/null 2>&1; then
    t_fail "the shim ACCEPTED -I -- it is not standing in for illumos's grep, \
and check 3 below would pass without exercising the fallback at all"
else
    t_ok "the shim rejects -I, as illumos's grep does"
fi

run_search() {
    cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool search_code {"pattern":"$2","path":"src"}
rule
  text SEARCH_DONE
EOF
    mm_start "$tmp/replies.mm" "$tmp/cap" 9
    write_config "$tmp/config.json" "$MM_PORT"
    # `env PATH=...`, not a prefixed assignment: FreeBSD's /bin/sh does not
    # export an assignment prefixed to a SHELL FUNCTION, so the shim would be
    # silently absent from the child's PATH and check 3 would test nothing.
    # smoke_lint check 13 exists for this and caught it here.
    (cd "$ws" && with_deadline 90 env PATH="$1" "$BIN" --config "$tmp/config.json" \
        -q --no-session --auto -p "find it" < /dev/null) > /dev/null 2>&1
    mm_stop
}

# M956: the same runner, but the pattern is a parameter. Used by the busybox-shim
# checks 5-7, which stand in for a platform whose /usr/bin/grep is busybox
# (Alpine). It delegates to the REAL busybox binary, not a shell emulation -- a
# shim that special-cased the sentinel would pass by echoing the string it was
# written to emit, not by exercising jichi's classification of a real busybox
# output. (The runbook: make a fixture behave like the real peer.)
run_search_pat() { run_search "$1" "$2"; }

# --- 2: control -- the normal grep finds it -----------------------------------
# The tool RESULT travels in the next request body, so the captured requests are
# the ground truth for "the model was told" (the M429 lesson). The evidence is
# grep's own `x.c:1:` prefix and NOT the pattern: the first cut asserted on
# "marker_needle", which is also in the tool CALL, so checks 2 and 3 passed
# while the search was returning an error. Check 4 caught it. Assert on what
# only the result can contain.
rm -rf "$tmp/cap"; run_search "$PATH" marker_needle
if grep -l 'x\.c:1:' "$tmp"/cap/req.* >/dev/null 2>&1; then
    t_ok "control: search_code finds the match with this platform's own grep"
else
    t_fail "control failed -- the fixture or the mock is wrong, not the fallback"
fi

# --- 3: the property -- it still finds it where -I is rejected ----------------
rm -rf "$tmp/cap" "$tmp/greplog"; run_search "$bin:$PATH" marker_needle
if grep -l 'x\.c:1:' "$tmp"/cap/req.* >/dev/null 2>&1; then
    t_ok "search_code still finds the match where grep rejects -I"
else
    t_fail "search_code found nothing with an illumos-shaped grep -- this is \
the defect the first illumos row found, and it is back"
fi

# --- 4: it got there by the FALLBACK, not by luck -----------------------------
# A search that succeeded while still passing -I would mean the shim was never
# consulted; a search that never asked without -I would mean something else
# produced the match. The literal is the exact cluster jichi sends without -I:
# `-rnE` since M714 (extended regex), `-rn ` before it -- which is why this check
# went red when -E was added, and why it pins the form rather than a prefix.
if [ -f "$tmp/greplog" ] && grep -q -- '-rnE ' "$tmp/greplog" && \
   ! grep -q -- '-rnI' "$tmp/greplog"; then
    t_ok "jichi asked without -I after the probe refused it (the fallback ran)"
else
    t_fail "the grep log does not show a -rnE call without -I: $(tr '\n' ';' \
< "$tmp/greplog" 2>/dev/null | cut -c1-200)"
fi

# --- 5-7: the busybox-shaped grep classifies a bad pattern as an error --------
#
# The second non-GNU grep the campaign met (2026-10-15, Alpine): /usr/bin/grep
# is BUSYBOX's, and it reports a bad regex ONLY once it has read real content
# -- against /dev/null it exits 1 silently, so jichi's old probe (pattern_probe
# on /dev/null, shared with M732) never saw the error, and a malformed pattern
# came back as "grep exited with an error reading the path (the pattern itself
# is valid)" -- affirming to the model that its pattern was fine. The fix
# (M956) gives the invalid-detection probe a readable line (printf x | grep);
# busybox then reports "bad regex '<pat>'" with rc 2, the same way GNU reports
# "Unmatched ( or \(". This driver stands in for that platform with a shim that
# delegates to the REAL busybox binary (peer fidelity, not shell emulation).
#
# The three checks are skipped as a unit when busybox is absent (t_skip_one x3
# counts against the plan, so t_done's denominator stays honest). On a box with
# busybox they prove: 5 the shim really reports the bad pattern (so 7 cannot
# pass vacuously), 6 the shim does not break a VALID search (the control), and
# 7 the property -- a malformed pattern returns the pattern-invalid error, not
# the "pattern is valid" mis-telling. Check 7 is the one that was RED pre-fix.
BB=$(command -v busybox 2>/dev/null)
if [ -n "$BB" ]; then
    bbshim="$tmp/bbshim"
    mkdir -p "$bbshim"
    # The shim delegates to the REAL busybox binary found on PATH (peer
    # fidelity, not a shell emulation of its rc/vocabulary).
    printf '#!/bin/sh\nexec "%s" grep "$@"\n' "$BB" > "$bbshim/grep"
    chmod +x "$bbshim/grep"

    # 5: the shim is really busybox -- it reports the bad pattern with rc 2
    # (and reads it from a pipe, the form jichi's probe now uses). A shim that
    # did not would make check 7 a restatement of nothing.
    if printf x | "$bbshim/grep" -E -e 'unbalanced(' 2>&1 | grep -q 'bad regex'; then
        t_ok "busybox shim reports the bad pattern (peer fidelity)"
    else
        t_fail "the busybox shim did not report 'bad regex' -- it is not standing in for Alpine"
    fi

    # 6: CONTROL -- with the busybox-shaped grep, a VALID pattern still finds
    # the match. Proves the shim does not break normal operation.
    rm -rf "$tmp/cap"; run_search_pat "$bbshim:$PATH" marker_needle
    if grep -l 'x\.c:1:' "$tmp"/cap/req.* >/dev/null 2>&1; then
        t_ok "control: search_code finds the match under the busybox-shaped grep"
    else
        t_fail "control failed -- the busybox shim broke a valid search"
    fi

    # 7: the PROPERTY -- a malformed pattern returns the pattern-invalid error
    # (jichi's own portable substring), not the "pattern is valid" mis-telling.
    # Pre-fix this is RED: the old /dev/null probe is blind on busybox, so
    # pattern_error() returns 0 and the tool takes the else-branch.
    rm -rf "$tmp/cap"; run_search_pat "$bbshim:$PATH" 'unbalanced('
    if grep -l 'not a valid extended regular expression' "$tmp"/cap/req.* >/dev/null 2>&1; then
        t_ok "a malformed pattern is named as the problem under the busybox-shaped grep"
    else
        t_fail "under busybox-shaped grep the malformed pattern was NOT named as the problem -- the /dev/null probe is blind on it"
    fi
else
    t_skip_one "no busybox on this host -- the Alpine grep stand-in (5-7) is not available"
    t_skip_one "control under busybox-shim (skipped, no busybox)"
    t_skip_one "malformed-pattern classification under busybox-shim (skipped, no busybox)"
fi

t_done
