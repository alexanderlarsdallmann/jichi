#!/bin/sh
# smoke: a config PATH that is a DIRECTORY is reported, not swallowed.
#
# Found on a hardware bench, on the operator's own machine: ~/.jichi had become
# a directory (a stray "config (Copy).json" was dropped into it), and every
# subcommand that loads config -- doctor, context, map, status -- exited 1 with
# ZERO output on BOTH streams. Nothing said why.
#
# Two defects compounded. M198 had already made jc_read_file REJECT a directory
# instead of returning an empty string, correctly turning a silent wrong answer
# into an error; but config_parse_file dropped that error without a word (it
# logged only for malformed JSON), and main's primary jc_config_load call site
# returned 1 without reporting -- while the other two call sites (advisor,
# onboard) both report. Either fix alone leaves a message; the pair left none.
#
# This driver pins BOTH halves and the control, because the failure is silence:
# an exit code alone cannot distinguish "explained" from "said nothing", which
# is exactly why it survived.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home

# smoke_home pins $JC_CONFIG at a null config, which takes the explicit-path
# branch and never consults ~/.jichi. This driver is about the GLOBAL discovery
# path, so unset it -- and run from a scratch cwd so an ambient
# local/config.json or .jichi/config.json cannot answer instead.
unset JC_CONFIG
ws=$(smoke_tmp)
out=$(smoke_tmp)

# ---- control: no ~/.jichi at all. Absent is not an error; built-in defaults.
rm -rf "$HOME/.jichi"
# The control asserts the SUMMARY, not the exit code. doctor exits nonzero when
# it finds a problem, and "the active model's server is unreachable" is a problem
# on any machine with no network -- which is exactly the machine this tier exists
# to validate. Keying on rc==0 made this check depend on reachability; it red on
# Guix (M450) for that reason and would red on any offline board. What the
# control actually needs to establish is that an ABSENT config still lets jichi
# run and report, as against the directory case below, which produces no output
# at all. The summary line is that property; the exit code is not.
(cd "$ws" && "$BIN" doctor) > "$out/a.out" 2> "$out/a.err"; a_rc=$?
if grep -qE '[0-9]+ ok,' "$out/a.out"; then
    t_ok "control: absent ~/.jichi still runs on built-in defaults (rc=$a_rc)"
else
    t_fail "control: absent ~/.jichi produced no doctor summary (rc=$a_rc)"
fi

# ---- the case: ~/.jichi is a directory holding a config-looking file.
rm -rf "$HOME/.jichi"
mkdir -p "$HOME/.jichi"
# A realistic stray: someone's config, copied INTO the path that must be a file.
# lowResource is pinned only to satisfy smoke_lint's inline-config rule -- this
# file is never read (that is the whole point of the case), so its contents
# cannot affect the result either way.
printf '%s' '{"models":[],"lowResource":false}' > "$HOME/.jichi/config (Copy).json"

(cd "$ws" && "$BIN" doctor) > "$out/b.out" 2> "$out/b.err"; b_rc=$?

if [ "$b_rc" -ne 0 ]; then
    t_ok "a directory at the config path fails (rc=$b_rc)"
else
    t_fail "a directory at the config path was accepted (rc=0)"
fi

# The whole point: it must not be silent.
if [ -s "$out/b.err" ]; then
    t_ok "it is not silent: stderr carries an explanation"
else
    t_fail "SILENT FAILURE: rc=$b_rc with nothing on stderr (the original bug)"
fi

# Half one: the loader names the cause, at the chokepoint that knows it.
if grep -q "is a directory" "$out/b.err"; then
    t_ok "the cause is named: the config path is a directory"
else
    t_fail "stderr never says the path is a directory: $(tr '\n' ' ' < "$out/b.err")"
fi

# Half two: the caller gives the context and the way out.
if grep -q "could not load configuration" "$out/b.err"; then
    t_ok "the caller reports the failure and how to override it"
else
    t_fail "stderr has no caller-level report: $(tr '\n' ' ' < "$out/b.err")"
fi

t_done
