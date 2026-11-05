#!/bin/sh
# smoke lint: no raw string calls (sprintf/strcpy/strcat/gets) in first-party
# shipped code (2026-08; widened to CLAUDE.md's full list in M1136/C-18, 2026-11-04).
#
# CONTRIBUTING.md's rule is "never call sprintf -- use jc_snprintf"
# (include/jc_snprintf.h); CLAUDE.md extends it to the other unbounded string
# calls (strcpy, strcat, gets). All four write an unbounded number of bytes
# into a fixed buffer: a format or argument the author did not size for is a
# silent overflow the C89 compiler does not diagnose. The rule was enforced
# only by reviewer memory; this makes it a lint (the house principle: prefer
# a lint to an audit -- an audit finds it once, a lint re-runs on every
# change). The pattern now catches a strcpy/strcat/gets the sprintf-only one
# could never match (run 0153's red-first leg proves it: a probe strcpy went
# from invisible to check 2 red).
#
# Scope: src/ and include/ (the shipped product), and since M728 the unit
# tests and their C helpers too. tests/ used to be excluded on the grounds
# that "fixtures legitimately use sprintf into known-safe buffers"; M728
# measured otherwise. Thirty-eight calls wrote $TMPDIR paths into fixed
# buffers, one of them `rm -rf %s` into 128 bytes, and a long TMPDIR
# overflowed them (a stack smash under a 327-character one). A fixture path
# is only as short as the TMPDIR it starts with, and nobody sized for that.
# The strcpy/strcat sites -- 71, audited 2026-11-04 (run 0153, LOG-3's M1136
# design note): every one is bounded by an explicit check at or above the
# site -- are allowlisted by exact line. `gets` has zero sites today; the
# pattern catches a regression.
# The one first-party exception is the self-contained JSON
# printer, which must stay jc_snprintf-free to remain swappable as a pair
# (CLAUDE.md M171); its four calls write fixed short formats into
# adequately-sized fixed buffers (tmp[8] for a 7-char \uXXXX, tmp[64] for the
# number renderings, M1076) and are allowlisted by exact line with that
# reason.
#
# Checks (t_plan 3): 1 the file-count floor; 2 no raw string call outside
# the allowlist; 3 allowlist completeness -- every allowlist entry matches a
# live site this run, so a moved or removed site turns the driver red (the
# raw-match count floor of 75 is asserted here, not as a separate check).
. "$(dirname "$0")/_smoke.sh"

t_plan 3
tmp=$(smoke_tmp)

tab=$(printf '\t')
cat > "$tmp/allow" <<EOF
cJSON.c${tab}sprintf(tmp, "\\\\u%04x", (unsigned int)ch);
cJSON.c${tab}sprintf(tmp, "%.0f", d);
cJSON.c${tab}sprintf(tmp, "%.15g", d);
cJSON.c${tab}sprintf(tmp, "%.17g", d);
jc_rules.c${tab}strcpy(tmp_cur, app->cwd);
jc_harness_push.c${tab}strcpy(p, path);
cJSON.c${tab}strcpy(n->valuestring, string);
cJSON.c${tab}strcpy(item->string, string);
jc_lexical.c${tab}strcpy(qterms[nq], tok);
jc_rss.c${tab}strcpy(open + 1, tag);
jc_rss.c${tab}strcpy(closebuf + 2, tag);
jc_log.c${tab}strcpy(cp, secret);
jc_auditview.c${tab}strcpy(r->cmd + sizeof(r->cmd) - 4, "...");
jc_auditview.c${tab}strcpy(r->cmd, cmd);
jc_jsonrepair.c${tab}strcpy(work, s);
jc_relay.c${tab}strcpy(outp, inp);
jc_runsview.c${tab}strcpy(run + 19, "...");
jc_tool_parallel.c${tab}strcat(held, ", ");
jc_tool_parallel.c${tab}strcat(held, c->path);
jc_tool_parallel.c${tab}strcat(s_pinned, " ");
jc_tool_parallel.c${tab}strcat(s_pinned, ref_out);
test_lease.c${tab}strcpy(in.run, "3f6b1c22-0000-4000-8000-abcdefabcdef");
test_lease.c${tab}strcpy(in.mode, "auto");
test_redact.c${tab}strcpy(msg + 4080, secret);  /* 4080 + 22 = 4102 < sizeof(msg) */
test_redact.c${tab}strcpy(buf, body);
test_redact.c${tab}strcpy(buf, "SHORTER");  /* 7 chars, longer than secret */
test_redact.c${tab}strcpy(buf, "prefix REGRESS-SECRET-123 suffix");
test_redact.c${tab}strcpy(buf, sec);
test_redact.c${tab}strcpy(buf, sec);
test_redact.c${tab}strcpy(s, "ARENA-SECRET-M926-42");
test_redact.c${tab}strcpy(s, "OVERRUN-OTHER-SECRET-7");
test_insights.c${tab}strcpy(t.name, name);
test_insights.c${tab}strcpy(t.name, name);
test_insights.c${tab}strcpy(m.name, name);
test_insights.c${tab}strcpy(t.name, "run_tests");
test_insights.c${tab}strcpy(t.name, "format_file");
test_session.c${tab}strcpy(id_copy, s.id);
test_patch.c${tab}strcpy(app->cwd, jc_test_tmpdir());
test_subagent.c${tab}strcpy(app->cwd, jc_test_tmpdir());
test_perm.c${tab}strcpy(c, s);
test_tool.c${tab}strcpy(app->cwd, jc_test_tmpdir());
test_tool.c${tab}strcpy(deny_name, "write_file");
test_tool.c${tab}strcpy(text, "hello\n\`\`\`json");
test_tool.c${tab}strcpy(text, "x\n\`\`\`read_file");
test_glossary.c${tab}strcpy(app.cwd, dir);
test_glossary.c${tab}strcpy(app.cwd, symlink_ws);
test_glossary.c${tab}strcpy(app.cwd, symlink_ctrl_ws);
test_glossary.c${tab}strcpy(app.cwd, tail_ws);
test_multipart.c${tab}strcat(close, "--");
test_refs.c${tab}strcpy(app.cwd, "/nonexistent-cwd"); /* force absolute-path resolution */
test_git.c${tab}strcpy(app.cwd, dir);
test_cacheaudit.c${tab}strcpy(m.name, "jlu/gemma");
test_memory.c${tab}strcpy(app.cwd, dir);
test_memory.c${tab}strcpy(rm, "{\"note\":\"prefer small commits\"}");
test_memory.c${tab}strcpy(app.cwd, dir);
test_runsview.c${tab}strcpy(J, J1);
test_runsview.c${tab}strcat(J, J2);
sockq.c${tab}strcpy(addr.sun_path, path);
xdrive.c${tab}strcpy(buf, spec);
test_rules.c${tab}strcpy(app.cwd, jc_test_tmp("jichi_rules_test/proj/sub"));
test_rules.c${tab}strcpy(app6.cwd, jc_test_tmp("jichi_rules_symlink/ws6"));
test_rules.c${tab}strcpy(app7.cwd, jc_test_tmp("jichi_rules_symlink_control/ws7"));
test_rules.c${tab}strcpy(app8.cwd, ws8);
test_rules.c${tab}strcpy(app2.cwd, jc_test_tmp("jichi_rules_cap/p"));
test_sysmsg.c${tab}strcpy(app.cwd, jc_test_tmpdir());
test_sysmsg.c${tab}strcpy(app.cwd, "/w");
test_sysmsg.c${tab}strcpy(app.root, "/w");
test_sysmsg.c${tab}strcpy(app.cwd, "/w");
test_sysmsg.c${tab}strcpy(app.root, "/w");
test_sysmsg.c${tab}strcpy(app.cwd, "/w");
test_sysmsg.c${tab}strcpy(app.root, "/w");
test_sysmsg.c${tab}strcpy(app.cwd, "/w");
test_sysmsg.c${tab}strcpy(app.root, "/w");
test_sysmsg.c${tab}strcpy(app.cwd, "/w");
test_sysmsg.c${tab}strcpy(app.root, "/w");
EOF

# The tests' own C is the top level, tests/tools and tests/fuzz. NOT the rest of
# tests/: tests/bench holds task fixtures a model edits and the workspaces a
# benchmark run leaves behind, which are data, and a recursive find met a
# model's own sprintf in one.
targets=$(find "$SMOKE_ROOT/src" "$SMOKE_ROOT/include" \
    \( -name '*.c' -o -name '*.h' \) 2>/dev/null)
tests_c=$(for f in "$SMOKE_ROOT"/tests/*.c "$SMOKE_ROOT"/tests/*.h \
                   "$SMOKE_ROOT"/tests/tools/*.c "$SMOKE_ROOT"/tests/tools/*.h \
                   "$SMOKE_ROOT"/tests/fuzz/*.c "$SMOKE_ROOT"/tests/fuzz/*.h; do
              [ -f "$f" ] && printf '%s\n' "$f"
          done)
nfiles=$(printf '%s\n' "$targets" | grep -c .)
ntests=$(printf '%s\n' "$tests_c" | grep -c .)
targets="$targets
$tests_c"
if [ "$nfiles" -ge 100 ] && [ "$ntests" -ge 151 ]; then
    t_ok "scanning $nfiles product source files and $ntests of the tests' own"
else
    t_fail "scanned $nfiles product files and $ntests test files (floors 100, 151) -- src/include/tests layout moved?"
fi

# Comment-aware scan (skip /* */ blocks and lines starting with *), matching a
# `sprintf(`, `strcpy(`, `strcat(` or `gets(` not preceded by an identifier
# char (so e.g. jc_strcpy/strdup can never match; snprintf/vsnprintf do not
# contain "sprintf" as a token anyway).
#
# Pass 1 writes the raw match list ($tmp/raw: one `basename<TAB>stripped-line`
# per hit, before the allowlist filter) and, from it, the offenders (the hits
# the allowlist does not cover).
awk -v allowf="$tmp/allow" -v rawf="$tmp/raw" '
BEGIN {
    FS = "\t"
    while ((getline l < allowf) > 0) {
        ti = index(l, "\t")
        if (ti > 0) A[substr(l, 1, ti - 1) SUBSEP substr(l, ti + 1)] = 1
    }
}
FNR == 1 { inblk = 0; name = FILENAME; sub(/.*\//, "", name) }
{
    line = $0
    stripped = line
    sub(/^[ \t]+/, "", stripped); sub(/[ \t]+$/, "", stripped)
    if (inblk) { if (index(stripped, "*/") > 0) inblk = 0; next }
    if (stripped ~ /^\/\*/) { if (index(stripped, "*/") == 0) inblk = 1; next }
    if (stripped ~ /^\*/) next
    code = line
    ci = index(code, "/*"); if (ci > 0) code = substr(code, 1, ci - 1)
    if (code !~ /(^|[^A-Za-z0-9_])(sprintf|strcpy|strcat|gets)\(/) next
    print name "\t" stripped > rawf
    if ((name SUBSEP stripped) in A) next
    print FILENAME ":" FNR ": " stripped
}' $targets < /dev/null > "$tmp/offenders"
[ -s "$tmp/raw" ] || : > "$tmp/raw"

if [ ! -s "$tmp/offenders" ]; then
    t_ok "no raw string calls (sprintf/strcpy/strcat/gets) outside the allowlist (use jc_snprintf or a bounded copy)"
else
    t_fail "raw string calls in first-party code ($(grep -c . "$tmp/offenders")) -- use jc_snprintf or a bounded copy:"
    sed 's/^/# /' "$tmp/offenders" | head -20
fi

# Check 3: allowlist completeness. Every allowlist entry must match a LIVE
# site in this run's raw match list; a moved or removed site leaves a stale
# line and turns the driver red, so the allowlist cannot rot silently. A raw
# match list below 75 (the 4 sprintf + 71 strcpy/strcat sites) means a live
# site vanished without its line being read as stale -- the same red.
raw_n=$(grep -c . "$tmp/raw")
allow_n=$(grep -c . "$tmp/allow")
awk -v rawf="$tmp/raw" '
BEGIN {
    FS = "\t"
    while ((getline l < rawf) > 0) {
        ti = index(l, "\t")
        if (ti > 0) R[substr(l, 1, ti - 1) SUBSEP substr(l, ti + 1)] = 1
    }
}
{
    ti = index($0, "\t")
    if (ti > 0) {
        k = substr($0, 1, ti - 1) SUBSEP substr($0, ti + 1)
        if (!(k in R)) { print; stale = 1 }
    }
}
END { exit stale }
' "$tmp/allow" > "$tmp/stale"
if [ "$raw_n" -ge 75 ] && [ ! -s "$tmp/stale" ]; then
    t_ok "allowlist complete: all $allow_n entries match live sites ($raw_n raw matches, floor 75)"
else
    t_fail "allowlist completeness ($raw_n raw matches -- floor 75; $allow_n allowlist entries, $(grep -c . "$tmp/stale") stale):"
    sed 's/^/# stale: /' "$tmp/stale" | head -20
fi

t_done
