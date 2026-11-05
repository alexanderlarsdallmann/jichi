#!/bin/sh
# M833 (def-263, def-264): the Makefile's probes read the compiler's VERDICT, not its
# chatter, and a flag that needs a runtime is on the line that links it.
#
# THE DEFECTS THIS EXISTS FOR, both found by the DJGPP step 0 (plans/
# 2026-09-29-the-fork-less-rows.md section 5) and neither of them DOS's:
#
#   1. DJGPP's gcc prints "-fPIC ignored (not supported for DJGPP)" on STDOUT and exits
#      0 even under -Werror. cc_warn_ok discarded only stderr, so the sentence joined
#      the probe's own `echo` and HARDENFLAGS held it: all 322 compiles of the build
#      died in one second on a shell syntax error at "(". The twenty-one feature probes
#      had the same shape -- `$(CC) ... 2>/dev/null && echo yes` -- so under any
#      compiler that writes to stdout each would read "<notice> yes", fail
#      `ifeq (...,yes)`, and report the feature ABSENT, silently.
#   2. -fstack-protector-strong was on the compile line only. gcc's driver adds the
#      runtime (libssp) when the flag is on the LINK line; glibc and musl carry it in
#      libc, which hid the omission, and DJGPP does not: 5,397 undefined references.
#
# THE INSTRUMENTS are two wrapper compilers that delegate to the host's `cc`: one that
# says, on stdout, that it ignores -fPIE (DJGPP's behaviour, exactly), and one that
# prints a line on stdout for EVERY call. `make info` runs from a scratch directory
# with `-f`, so no probe file is ever written into the tree.
. "$(dirname "$0")/_smoke.sh"

# The make this project needs (M479): OpenBSD's /bin/make is BSD make.
_mk=""
for _c in "${MAKE:-}" make gmake gnumake; do
    [ -n "$_c" ] || continue
    if "$_c" --version 2>/dev/null | grep -q 'GNU Make'; then _mk=$_c; break; fi
done
[ -n "$_mk" ] || t_skip "no GNU make found (tried \$MAKE, make, gmake, gnumake)"
_cc=""
for _c in cc gcc clang; do
    if command -v "$_c" >/dev/null 2>&1; then _cc=$(command -v "$_c"); break; fi
done
[ -n "$_cc" ] || t_skip "no C compiler on PATH for the wrappers to delegate to"

t_plan 5
tmp=$(smoke_tmp)
MK="$SMOKE_ROOT/Makefile"

cat > "$tmp/pie-cc" <<EOF
#!/bin/sh
# Says on STDOUT that it ignores -fPIE, and still exits 0 -- as DJGPP's gcc does.
for a in "\$@"; do [ "\$a" = "-fPIE" ] && echo "-fPIC ignored (not supported here)"; done
exec "$_cc" "\$@"
EOF
cat > "$tmp/chatty-cc" <<EOF
#!/bin/sh
# Prints a notice on STDOUT on every call, then compiles normally.
echo "chatty-cc: a notice on stdout"
exec "$_cc" "\$@"
EOF
chmod +x "$tmp/pie-cc" "$tmp/chatty-cc"
mkdir -p "$tmp/run"

info() {  # $1 = compiler -> the `make info` lines, run from the scratch dir
    "$_mk" -s -C "$tmp/run" -f "$MK" info CC="$1" HAVE_CURL= 2>/dev/null
}
hf_pie=$(info "$tmp/pie-cc" | sed -n 's/^HARDENFLAGS *= *//p')
vs_chatty=$(info "$tmp/chatty-cc" | sed -n 's/^HAVE_VSNPRINTF *= *//p')
vs_real=$(info "$_cc" | sed -n 's/^HAVE_VSNPRINTF *= *//p')
real=$(info "$_cc")
hf_real=$(printf '%s\n' "$real" | sed -n 's/^HARDENFLAGS *= *//p')
hl_real=$(printf '%s\n' "$real" | sed -n 's/^HARDENLDFLAGS *= *//p')
printf '# pie-cc HARDENFLAGS: %s\n# real HARDENFLAGS:   %s\n# real HARDENLDFLAGS: %s\n' \
    "$hf_pie" "$hf_real" "$hl_real"

# 1. A notice on stdout never becomes a flag.
case "$hf_pie" in
    *ignored*|*"not supported"*|*"("*)
        t_fail "the compiler's stdout reached HARDENFLAGS: '$hf_pie'" ;;
    *)  t_ok "a compiler's stdout notice does not reach the probed flags" ;;
esac

# 2. A flag the compiler comments on is not accepted -- and 3, the instrument is not
#    vacuous: the flags the compiler takes silently are still probed in.
case " $hf_pie " in
    *" -fPIE "*) t_fail "-fPIE was accepted although the compiler said it ignores it" ;;
    *)           t_ok "a flag the compiler says it ignores is not accepted (-fPIE)" ;;
esac
case " $hf_real " in
    *" -fstack-protector-strong "*|*" -Wformat "*)
        case " $hf_pie " in
            *" -Wformat "*) t_ok "the silent flags are still accepted under the same wrapper" ;;
            *) t_fail "the wrapper lost flags the compiler takes silently: '$hf_pie'" ;;
        esac ;;
    *)  t_skip_one "this compiler takes none of the hardening flags, so there is nothing to keep" ;;
esac

# 4. A feature probe reads the exit status, not stdout: a compiler that prints on
#    every call must not turn a present feature absent.
if [ -z "$vs_real" ]; then
    t_fail "make info reports no HAVE_VSNPRINTF line (extraction broke)"
elif [ "$vs_chatty" = "$vs_real" ]; then
    t_ok "a chatty compiler leaves the feature verdicts alone (HAVE_VSNPRINTF = $vs_real)"
else
    t_fail "a notice on stdout changed a feature verdict: HAVE_VSNPRINTF '$vs_real' -> '$vs_chatty'"
fi

# 5. The stack protector is on the link line whenever it is on the compile line.
case " $hf_real " in
    *" -fstack-protector-strong "*)
        case " $hl_real " in
            *" -fstack-protector-strong "*)
                t_ok "-fstack-protector-strong is on the link line too, so libssp can be pulled in" ;;
            *)  t_fail "-fstack-protector-strong compiles in but is missing from the link line: '$hl_real'" ;;
        esac ;;
    *)  t_skip_one "this compiler does not take -fstack-protector-strong" ;;
esac
t_done
