#!/bin/sh
# smoke: E-01 — an inherited SIGCHLD=SIG_IGN makes a failing verifier PASS
#
# THE DEFECT (E-01). When a parent ignores SIGCHLD, POSIX lets the kernel reap
# its children itself; waitpid then has nothing to collect and fails with ECHILD.
# jichi's two waitpid callers (jc_envelope.c:1213, jc_app.c:821) start with
# status = 0 and do not check whether waitpid succeeded, so a status of 0
# decodes as "exited normally with code 0". Thus a failing verifier (exit 1)
# is read as passing when SIGCHLD is ignored.
#
# The C launcher (not perl's trap, which does not reproduce the defect) is:
#   #include <signal.h>
#   #include <unistd.h>
#   int main(int argc, char **argv) {
#     signal(SIGCHLD, SIG_IGN);
#     if (argc > 1) execvp(argv[1], &argv[1]);
#     return 127;
#   }
#
# The smoker's red evidence must be exactly this shape:
#   === plain ===            baseline exit  : 1 (RED)
#   === perl SIGCHLD=IGNORE ===  baseline exit  : 0 (GREEN)
#   === C launcher ===       baseline exit  : 0 (GREEN)
. "$(dirname "$0")/_smoke.sh"

# Decide FIRST whether this build can run, then declare a plan.
_cc=""
for _c in cc gcc c99; do
    if command -v "$_c" >/dev/null 2>&1; then _cc=$(command -v "$_c"); break; fi
done
[ -n "$_cc" ] || t_skip "no C compiler on PATH (tried cc gcc c99)"

t_plan 4
smoke_home
tmp=$(smoke_tmp)

# Write a brief.md (one line) into tmp.
printf 'Make the tests pass.\n' > "$tmp/brief.md"

# Compile the C launcher into $tmp (plain C, no flags needed).
cat > "$tmp/launcher.c" <<'EOF'
#include <signal.h>
#include <unistd.h>
int main(int argc, char **argv) {
  signal(SIGCHLD, SIG_IGN);
  if (argc > 1) execvp(argv[1], &argv[1]);
  return 127;
}
EOF
"$_cc" -o "$tmp/launcher" "$tmp/launcher.c" || {
    t_fail "C compiler failed to build launcher"
    t_done
}

# Guarded grep (portability_lint 36): a system without the merged /usr (Debian 9)
# has no /usr/bin/grep; the bare path would make the condition false and print
# t_fail -- a false red on that platform, not a green.
_grep() {
    G=/usr/bin/grep
    [ -x "$G" ] || G=grep
    "$G" "$@"
}

# CHECK 1: control — proves the instrument works. A failing verifier should be RED.
_out1=$(cd "$tmp" && exec "$BIN" brief-check "$tmp/brief.md" --verify "exit 1" < /dev/null 2>&1)
if printf '%s\n' "$_out1" | _grep -q 'baseline exit  : 1' && ! printf '%s\n' "$_out1" | _grep -q 'baseline exit  : 0'; then
    t_ok 'a failing verifier is RED without an inherited ignore'
else
    t_fail "a failing verifier should be RED: $(printf '%s' "$_out1" | _grep 'baseline exit' | head -1)"
fi

# CHECK 2: the defect — the launcher ignores SIGCHLD, so the verifier's waitpid
# reads status=0 and reports the failure as GREEN. THIS IS RED ON THE UNFIXED TREE.
# Two distinct bad outcomes, named apart (a missing line is a harness problem;
# a green/unknown line is the product):
_out2=$(cd "$tmp" && exec "$tmp/launcher" "$BIN" brief-check "$tmp/brief.md" --verify "exit 1" < /dev/null 2>&1)
if printf '%s\n' "$_out2" | _grep -q 'baseline exit  : 1'; then
    t_ok 'a launcher that ignores SIGCHLD no longer reads the failure as green'
elif printf '%s\n' "$_out2" | _grep -q 'baseline exit'; then
    t_fail "the launcher's SIGCHLD=IGN made a failing verifier not report exit 1: $(printf '%s' "$_out2" | _grep 'baseline exit' | head -1) (the pre-fix misread prints 'baseline exit  : 0 (GREEN)'; the checked reap prints 'baseline exit  : -1')"
else
    t_fail "the launcher run produced no baseline line at all -- the harness, not the product, failed: $(printf '%s' "$_out2" | head -3 | tr '\n' ' ')"
fi

# CHECK 3: the launcher really ignored it — teeth against a vacuous launcher.
# Run the launcher with a trivial child that exits 0 and one that exits non-zero.
# If execvp works, ./launcher true should exit 0, ./launcher false should exit non-zero.
"$tmp/launcher" true
_ret3a=$?
"$tmp/launcher" false
_ret3b=$?
if [ "$_ret3a" -eq 0 ] && [ "$_ret3b" -ne 0 ]; then
    t_ok 'the launcher correctly execs its arguments (true→0, false→non-zero)'
else
    t_fail "launcher exec failed: true rc=$_ret3a (want 0), false rc=$_ret3b (want non-zero)"
fi

# CHECK 4: a second verifier flavour — same defect should appear with a different exit code.
# The defect is that the launcher's SIGCHLD=IGN makes the verifier report exit 0 regardless
# of the actual exit code. With --verify "sh -c 'exit 3'", the buggy version shows
# "baseline exit  : 0 (GREEN)" instead of "baseline exit  : 3 (RED)".
_out4=$(cd "$tmp" && exec "$tmp/launcher" "$BIN" brief-check "$tmp/brief.md" --verify "sh -c 'exit 3'" < /dev/null 2>&1)
# The positive teeth assert the run really produced the baseline line at all,
# so the negative assertion below cannot pass on an empty/erroring run.
if printf '%s\n' "$_out4" | _grep -q 'baseline exit  : 3' \
   && ! printf '%s\n' "$_out4" | _grep -q 'baseline exit  : 0'; then
    t_ok 'a launcher that ignores SIGCHLD reports a failing exit-3 verifier as exit 3'
elif printf '%s\n' "$_out4" | _grep -q 'baseline exit  : 0'; then
    t_fail "the launcher's SIGCHLD=IGN made exit 3 appear as 0 (the pre-fix misread): $(printf '%s' "$_out4" | _grep 'baseline exit' | head -1)"
else
    t_fail "the exit-3 launcher run did not report exit 3: $(printf '%s' "$_out4" | _grep 'baseline exit' | head -1)"
fi

t_done
