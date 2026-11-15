#!/bin/sh
# smoke: the ci 32-bit stage's multilib probe asks the right question (M1184,
# SV:2307 card 5, board card 62; B6-B8).
#
# THE INCIDENT. 2026-11-13 22:01, the host gate's `make ci` ran the 32-bit
# stage and FAILED with `asm/errno.h: No such file or directory`: the host had
# lost gcc-multilib (installing the m68k cross compiler removed it), and the
# stage's own translation units include <errno.h>, which pulls the kernel
# <asm/errno.h> -- the one file the removal left dangling. The guard probe
# `echo 'int main(void){return 0;}' | gcc -m32 -x c -` passed anyway (a
# trivial main includes no header), so the stage ran and failed instead of
# printing its SKIP line: a FALSE verdict from a probe that asked the wrong
# question. The note is docs/internal/drive/2026-11-05-harness-port/notes/
# M1184.md; its stand-in is this driver's shadow below.
#
# THE STAND-IN. A `gcc` wrapper on PATH that runs
# `<real gcc> -nostdinc -I <shadow> "$@"` with a <shadow>/errno.h containing
# exactly `#include <asm/errno.h>` and NO <shadow>/asm/ directory: under it,
# a trivial main still compiles (rc 0) while the errno program fails with the
# incident's exact message (`asm/errno.h: No such file or directory`). The
# wrapper appends -m32 only where the box's real gcc supports it and the
# caller (the recipe) did not pass it, so the errno leg reproduces the
# incident's message on a box that lacks -m32 too -- where the recipe's real
# probe would SKIP legitimately.
#
# Checks 4-6 are RED on the current Makefile (the trivial-main probe and the
# old SKIP wording); the fix is B1/B4/B5 of the note. B8: the driver never
# calls `gcc -m32` in its own legs -- its only direct -m32 call is the ONE
# guard that detects whether this box has multilib (trivial main), and the
# recipe's own probe runs only through the shadow (check 4), so a host
# without multilib is not probed by the driver twice.
. "$(dirname "$0")/_smoke.sh"

command -v gcc >/dev/null 2>&1 \
    || t_skip "no gcc on PATH (B7: a box where the ci 32-bit stage cannot be probed at all)"

t_plan 6
smoke_home
tmp=$(smoke_tmp)

realgcc=$(command -v gcc)
# THE ONE guard (B8): does this box's real gcc have -m32 at all? A trivial
# main -- no header involved -- is the question; a host without multilib
# SKIPs the stage legitimately and the driver stays green on it.
multilib=no
if echo 'int main(void){return 0;}' | "$realgcc" -m32 -x c - -o /dev/null >/dev/null 2>&1; then
    multilib=yes
fi
[ "$multilib" = yes ] && : > "$tmp/m32flag"

# --- the stand-in: shadow errno.h + a gcc wrapper on PATH ----------------------
mkdir -p "$tmp/shadow" "$tmp/shadow-bin"
printf '#include <asm/errno.h>\n' > "$tmp/shadow/errno.h"
cat > "$tmp/shadow-bin/gcc" <<EOF
#!/bin/sh
# shadow gcc: -nostdinc -I $tmp/shadow reproduces the half-installed multilib
# where only <asm/errno.h> is missing. -m32 is appended only where this box's
# real gcc supports it (marker $tmp/m32flag) and the caller did not pass it
# (the recipe does).
args="\$@"
case " \$args " in
  *" -m32 "*) ;;
  *) if [ -f "$tmp/m32flag" ]; then args="\$args -m32"; fi ;;
esac
exec "$realgcc" -nostdinc -I "$tmp/shadow" \$args
EOF
chmod +x "$tmp/shadow-bin/gcc"

TRIV='int main(void){return 0;}'
ERRNO_PROG='#include <errno.h>
int main(void){return 0;}'

# --- 1: the stand-in reproduces the host error ---------------------------------
printf '%s\n' "$TRIV" | PATH="$tmp/shadow-bin:$PATH" gcc -x c - -o /dev/null >/dev/null 2>&1
rc_t=$?
printf '%s\n' "$ERRNO_PROG" | PATH="$tmp/shadow-bin:$PATH" gcc -x c - -o /dev/null \
    > "$tmp/e.out" 2>&1
rc_e=$?
if [ "$rc_t" -eq 0 ] && [ "$rc_e" -ne 0 ] \
    && grep -q 'asm/errno\.h: No such file' "$tmp/e.out"; then
    t_ok "stand-in: trivial main rc 0, errno program rc $rc_e with 'asm/errno.h: No such file' (the 2026-11-13 22:01 host error)"
else
    t_fail "stand-in did not reproduce the host error (trivial rc $rc_t, errno rc $rc_e, multilib=$multilib): $(head_bytes 200 "$tmp/e.out")"
fi

# --- 2: the stand-in discriminates (the real gcc answers) ----------------------
if [ "$multilib" = yes ] && printf '%s\n' "$ERRNO_PROG" | "$realgcc" -m32 -x c - -o /dev/null >/dev/null 2>&1; then
    t_ok "discriminates: the errno program compiles under the real gcc -m32 (a complete multilib answers; the shadow is only the half-installed case)"
elif [ "$multilib" = no ]; then
    t_ok "no multilib here (the stage SKIPs legitimately): cell 1's shadow shape stands on its own, the real gcc cannot answer -m32"
else
    t_fail "multilib present but the errno program fails under the real gcc -m32 -- the stand-in is not discriminating (box state?)"
fi

# --- 3: the shadow shape (the instrument's own self-check) ---------------------
nshadow=$(grep -c . "$tmp/shadow/errno.h")
if [ -f "$tmp/shadow/errno.h" ] && [ "$nshadow" -eq 1 ] \
    && grep -qx '#include <asm/errno.h>' "$tmp/shadow/errno.h" \
    && [ ! -d "$tmp/shadow/asm" ]; then
    t_ok "shadow shape: errno.h is exactly one line '#include <asm/errno.h>', no shadow asm/ directory (a shadow that gained asm/ would be fake)"
else
    t_fail "shadow shape wrong: errno.h has $nshadow line(s), shadow asm/ dir present? $([ -d "$tmp/shadow/asm" ] && echo yes || echo no)"
fi

# --- 4: the RECIPE's own probe, run under the shadow ---------------------------
# Extracted VERBATIM from the Makefile's ci 32-bit stage (the line with
# `gcc -m32 -x c` and the probe pipe): the probe is the recipe's, not a
# retyped copy (the shared-not-copied rule; the note's rejected alternative
# was a second probe body in a make target). The wrapper on PATH is the ONLY
# thing that makes the recipe's by-name `gcc` the stand-in's.
probe_line=$(grep 'gcc -m32 -x c' "$SMOKE_ROOT/Makefile" \
    | sed -n 's/^[[:space:]]*if //; s/; then[[:space:]]*$//; s/; then[[:space:]]*\\[[:space:]]*$//p' \
    | head -n 1)
if [ -z "$probe_line" ]; then
    t_fail "could not extract the recipe's probe line from Makefile (no 'gcc -m32 -x c' pipe found)"
else
    rc_p=0
    PATH="$tmp/shadow-bin:$PATH" sh -c "$probe_line" < /dev/null > "$tmp/p.out" 2>&1 || rc_p=$?
    if [ "$rc_p" -ne 0 ]; then
        t_ok "recipe probe under the shadow FAILS (rc $rc_p) -- the stage would print its SKIP line, not run and fail (B1)"
    else
        t_fail "recipe probe under the shadow PASSED (rc 0) -- the probe compiles a program with no <errno.h>, so a half-installed multilib false-RUNS the stage (B1 unfixed; the 2026-11-13 host incident)"
    fi
fi

# --- 5: the M1129 comment says the probe's question (B5) -----------------------
# Its probe sentence must name the errno program and must NOT still say
# 'trivial main' (the 2026-11-13 host incident was the proof the old question
# was wrong); M1184/SV:2307 are named there too.
if grep -q 'compiles a program that includes <errno\.h>' "$SMOKE_ROOT/Makefile" \
    && grep -q 'M1184' "$SMOKE_ROOT/Makefile" \
    && grep -q 'SV:2307' "$SMOKE_ROOT/Makefile" \
    && ! grep -q 'compiles a trivial main' "$SMOKE_ROOT/Makefile"; then
    t_ok "M1129 comment names the errno program as the probe question (M1184/SV:2307; the 'trivial main' wording is gone)"
else
    t_fail "M1129 comment does not carry the errno-program question: $(grep -n 'trivial main' "$SMOKE_ROOT/Makefile" | head -n 1)"
fi

# --- 6: the SKIP line names the right question (B4) ----------------------------
skip_line=$(grep 'ci: SKIP 32-bit stage' "$SMOKE_ROOT/Makefile" | head -n 1)
if [ -n "$skip_line" ] && printf '%s\n' "$skip_line" | grep -q 'errno\.h' \
    && ! printf '%s\n' "$skip_line" | grep -q 'trivial main'; then
    t_ok "SKIP line names the errno program as the reason (B4)"
else
    t_fail "SKIP line does not name the errno program: $(printf '%s\n' "$skip_line" | head -n 1)"
fi

t_done
