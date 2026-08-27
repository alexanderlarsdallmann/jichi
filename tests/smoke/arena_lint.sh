#!/bin/sh
# smoke lint: no per-call data on the session arena (M199/M200; port of
# tests/e2e/arena_lint.py, M210 -- python-free).
#
# `app->arena` lives for the whole process. Using it for something a
# single tool call consumes is the bug class that cost M197/M198 (17.5 MB
# per /sessions keypress; a 502 MB peak on one keystroke). Scope: the
# layers where per-call work happens in a LONG-LIVED process --
# src/tools/, src/lsp/, and the TUI. Everything not on the allowlist
# belongs on jc_app_tool_scratch(app) (reset per tool call),
# jc_app_scratch(app) (per turn, survives a nested agent run), or a local
# arena the caller frees.
#
# src/main.c is deliberately NOT linted: its app->arena uses live in
# short-lived subcommands where the session arena is correct (see
# docs/analysis/2026-07-29-tool-arena.md).
#
# The allowlist is keyed by (file, stripped source-line text): a use whose
# text is not on the list is flagged, wherever it appears. Two byte-identical
# app->arena lines in one file therefore share one entry (both are the same
# allocation and were read together); a line with any DIFFERENT text is a new
# site and must be read and added. Every entry was arrived at by reading the
# site -- the reasons live as comments below. If your allocation really is
# session-lived, add its stripped line here with a reason.
#
# M610: the scan is now EVERY src/*.c file except main.c (the documented
# short-lived-subcommand exception), not a hand-picked list of eight dirs --
# so a new directory cannot silently fall outside the universe, which is how
# src/util/jc_learn.c's session-lived skill load sat unaudited. The floor is
# the exact file count today; a new source file bumps it, on purpose (the
# "audit the universe" tripwire, CLAUDE.md).
. "$(dirname "$0")/_smoke.sh"

t_plan 2
tmp=$(smoke_tmp)

# --- the allowlist: FILE<TAB>exact-stripped-line -----------------------------
# tools/LSP: a dynamic tool's registration record, created once and held
# by the registry for the process lifetime (+ null-guards that allocate
# nothing). jc_tui.c: the session's own strings; resolvers whose arena
# parameter is ignored since M197; config values set by a slash command;
# the once-per-process wisdom entries; the ACTIVE assignment's spec.
# M218 widening (src/chat, src/provider, src/net, src/session, src/index --
# the last four scan clean with no entries): jc_app.c -- the read-set path
# copy (deduped via jc_app_was_read, paths only), the scratch-accessor
# fallback + /context gauge (no allocation), the reachability cache + the
# constraints file (once per process), the constraint adopt (deduped,
# bounded by JC_CONSTRAINT_MAX); jc_rules/jc_glossary -- loaded once per
# session by main; jc_repomap.c -- jc_repomap_build's session copy is the
# app->repo_map prompt cache (transient consumers use jc_repomap_render).
tab=$(printf '\t')
cat > "$tmp/allow" <<EOF
jc_tool_mcp.c${tab}t = (struct jc_tool *)jc_arena_calloc(app->arena, sizeof(*t));
jc_tool_imagegen.c${tab}t = (struct jc_tool *)jc_arena_calloc(app->arena, sizeof(*t));
jc_tool_mcp.c${tab}if (app == NULL || app->arena == NULL) {
jc_tool_imagegen.c${tab}if (app == NULL || app->arena == NULL) {
jc_tui.c${tab}app->session_all ? NULL : app->cwd, app->cwd, app->arena);
jc_tui.c${tab}jc_session_new(&session, app->cwd, app->arena);
jc_tui.c${tab}&tmp, app->arena)
jc_tui.c${tab}app->arena);
jc_tui.c${tab}r = jc_session_open(&tmp, arg, 0, NULL, app->cwd, app->arena);
jc_tui.c${tab}if (jc_session_fork(&session, &fork, app->arena) == JC_OK) {
jc_tui.c${tab}? jc_session_resolve_alias(what + 1, one, sizeof one, c->app->arena)
jc_tui.c${tab}: jc_session_resolve_prefix(what, one, sizeof one, c->app->arena);
jc_tui.c${tab}sizeof resolved, app->arena) == 0)) {
jc_tui.c${tab}app->arena) != 0) {
jc_tui.c${tab}app->config.language = jc_arena_strdup(app->arena, arg);
jc_tui.c${tab}char *copy = jc_arena_strdup(app->arena, sel);
jc_tui.c${tab}if (jc_read_file(path, &text, &len, c->app->arena) != JC_OK ||
jc_tui.c${tab}line = jc_arena_strdup(c->app->arena,
jc_tui.c${tab}char **out = (char **)jc_arena_alloc(c->app->arena,
jc_tui.c${tab}if (jc_read_file(arg, &atext, NULL, app->arena) != JC_OK) {
jc_tui.c${tab}app->arena) != JC_OK) {
jc_tui.c${tab}brief = jc_assign_render(&g_assignment, app->arena);
jc_app.c${tab}copy = jc_arena_strdup(app->arena, path);
jc_app.c${tab}return app->scratch != NULL ? app->scratch : app->arena;
jc_app.c${tab}app->reach = (signed char *)jc_arena_calloc(app->arena,
jc_app.c${tab}if (jc_read_file(path, &data, &len, app->arena) != JC_OK || len == 0) {
jc_app.c${tab}JC_CONSTRAINT_MAX, app->arena);
jc_app.c${tab}? jc_arena_strdup(app->arena, c->subject) : NULL;
jc_app.c${tab}dup.text = (c->text != NULL) ? jc_arena_strdup(app->arena, c->text) : NULL;
jc_context.c${tab}jc_size aused = jc_arena_used(app->arena, &acap);
jc_glossary.c${tab}if (jc_read_file(path, &data, &len, app->arena) != JC_OK || len == 0) {
jc_glossary.c${tab}result = jc_arena_strdup(app->arena, text);
jc_rules.c${tab}c.a = app->arena;
jc_rules.c${tab}char *d = jc_arena_strdup(app->arena, cur);
jc_rules.c${tab}result = jc_arena_strdup(app->arena, c.sb.data);
jc_repomap.c${tab}result = jc_arena_strdup(app->arena, rendered);
jc_learn.c${tab}jc_skill_load(&app->skills, app->cwd, app->arena);
EOF

# The whole tree, minus src/main.c (short-lived subcommands; app->arena is
# correct there -- docs/analysis/2026-07-29-tool-arena.md). Complete by
# construction: no dir list to fall out of.
targets=$(ls "$SMOKE_ROOT/src/"*/*.c 2>/dev/null | grep -v '/main\.c$')
nfiles=$(printf '%s\n' "$targets" | grep -c .)
# Floor at today's exact count (CLAUDE.md): a new src file must bump this, which
# is the point -- it forces a look at whether the new file's app->arena uses (if
# any) belong on the allowlist.
if [ "$nfiles" -ge 174 ]; then
    t_ok "scanning $nfiles src/*.c files (every dir except main.c; floor 174)"
else
    t_fail "scanned only $nfiles files (floor 174) -- glob broke or a dir vanished"
fi

awk -v allowf="$tmp/allow" '
BEGIN {
    FS = "\t"
    while ((getline l < allowf) > 0) {
        ti = index(l, "\t")
        if (ti > 0)
            A[substr(l, 1, ti - 1) SUBSEP substr(l, ti + 1)] = 1
    }
}
FNR == 1 {
    inblk = 0
    name = FILENAME
    sub(/.*\//, "", name)
}
{
    line = $0
    stripped = line
    sub(/^[ \t]+/, "", stripped)
    sub(/[ \t]+$/, "", stripped)
    # skip comments: fixed sites deliberately EXPLAIN the old misuse in
    # prose, and a lint that flagged its own documentation would be
    # self-defeating
    if (inblk) {
        if (index(stripped, "*/") > 0)
            inblk = 0
        next
    }
    if (stripped ~ /^\/\*/) {
        if (index(stripped, "*/") == 0)
            inblk = 1
        next
    }
    if (stripped ~ /^\*/)
        next
    code = line
    ci = index(code, "/*")
    if (ci > 0)
        code = substr(code, 1, ci - 1)
    # \b(app|m->app|c->app)->arena\b -- every alternative ends in "app",
    # so one word-boundary-emulated pattern covers all three
    if (code !~ /(^|[^A-Za-z0-9_])app->arena([^A-Za-z0-9_]|$)/)
        next
    if ((name SUBSEP stripped) in A)
        next
    print FILENAME ":" FNR ": " stripped
}' $targets > "$tmp/offenders"

if [ ! -s "$tmp/offenders" ]; then
    t_ok "no per-call app->arena use outside the audited allowlist"
else
    t_fail "per-call data on the session arena ($(grep -c . "$tmp/offenders") site(s)) -- use jc_app_tool_scratch/jc_app_scratch"
    sed 's/^/# /' "$tmp/offenders" | head -20
fi

t_done
