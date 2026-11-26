#!/bin/sh
# guide_gen.sh - regenerate src/guide/jc_guide.c from the marked page sections.
# (M1196, batch G card 27.)
#
# The jichi guide is ONE compiled-in text served by two surfaces: the
# `jichi guide [topic]` verb and the built-in `jichi-guide` skill (load_skill).
# Its source is the marked sections of docs/ -- each bounded by two own-line
# HTML comments: the open marker `<!-- jichi-guide topic="T" name="N" -->`
# (on the line immediately after its `## ` heading) and the close marker
# `<!--/jichi-guide -->` (where the section's prose ends). This generator
# captures the lines strictly between the two markers for each of the five
# topics, trims the outer padding blanks, and emits the C data file in the
# scaffold idiom: one string literal per body line, `"` and `\` escaped, a
# trailing `\n`; a body line over 400 chars is split into adjacent literals so
# no source line exceeds the 509-char C89 bound even after escaping.
#
# POSIX sh + awk only -- no python3, no bashisms. Every `while read` loop reads
# from a temp FILE, never a pipe: a loop at the end of a pipeline runs in a
# SUBSHELL and would lose its counter, silently dropping the first chunk of a
# split line. Usage:
#     sh tests/tools/guide_gen.sh [ROOT]  >  src/guide/jc_guide.c
# ROOT defaults to the repo root (this script's ../..). The committed file must
# match the output byte-for-byte; tests/smoke/guide_lint.sh diffs it, so a page
# edit that is not regenerated fails the build. The topic -> sections mapping
# mirrors the note's table (the lint's expected universe); `limits` is the
# authored NEW section in AGENTS_GUIDE.md.

ROOT="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"

SECFILE=$(mktemp "${TMPDIR:-/tmp}/guide_sec.XXXXXX")
CHUNKFILE=$(mktemp "${TMPDIR:-/tmp}/guide_chunk.XXXXXX")
BODYFILE=$(mktemp "${TMPDIR:-/tmp}/guide_body.XXXXXX")
trap 'rm -f "$SECFILE" "$CHUNKFILE" "$BODYFILE"' EXIT

# --- escape one line into a C string-literal body (no quotes, no trailing \n)
g_escape() {
    printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'
}

# --- write the raw line $1 into CHUNKFILE as chunks of at most 400 chars (one
# per line). Returns the chunk count on stdout.
g_split() {
    printf '%s\n' "$1" | awk '{
        s = $0
        while (length(s) > 400) { print substr(s, 1, 400); s = substr(s, 401) }
        if (length(s) > 0) print s
    }' > "$CHUNKFILE"
    awk 'END { print NR }' "$CHUNKFILE"
}

# --- emit ONE body line as a C array element: `    "<esc>\n",` for a short
# line; for a long one, adjacent literals on the first/middle/last chunks.
# $1 = the raw line ("" for a blank).
g_emit_body_line() {
    _l="$1"
    if [ -z "$_l" ]; then
        printf '    "\\n",\n'
        return
    fi
    : > "$CHUNKFILE"
    _n=$(g_split "$_l")
    [ -n "$_n" ] || _n=1
    _i=0
    while IFS= read -r _c; do
        _i=$((_i + 1))
        _e=$(g_escape "$_c")
        if [ "$_i" -eq "$_n" ]; then
            printf '    "%s\\n",\n' "$_e"
        else
            printf '    "%s",\n' "$_e"
        fi
    done < "$CHUNKFILE"
}

# --- capture the trimmed body of a marked section: extract_body PAGE HEADING --
# The open marker must sit on the line immediately after the `## HEADING` line;
# the section runs to the next `<!--/jichi-guide -->`. Writes the body lines to
# BODYFILE. Fails loudly if the heading, marker, or close is missing (a marker
# drifted off its heading).
extract_body() {
    awk -v head="## $2" '
        !found && $0 == head { found = 1; expect = 1; next }
        found && expect {
            if ($0 ~ /^<!-- jichi-guide topic="[^"]*" name="[^"]*" -->$/) {
                open = 1; expect = 0; next
            }
            print "guide_gen: the open marker does not follow '" head "' on its own line" > "/dev/stderr"
            bad = 1; exit 2
        }
        open && $0 == "<!--/jichi-guide -->" { closed = 1; exit }
        open { lines[n++] = $0 }
        END {
            if (bad) exit 2
            if (!found)  { print "guide_gen: heading not found: " head > "/dev/stderr"; exit 3 }
            if (!open)   { print "guide_gen: no open marker for " head > "/dev/stderr"; exit 4 }
            if (!closed) { print "guide_gen: no close marker for " head > "/dev/stderr"; exit 5 }
            s = 0; while (s < n && lines[s] == "") s++
            e = n - 1; while (e >= s && lines[e] == "") e--
            for (i = s; i <= e; i++) print lines[i]
        }
    ' "$1" > "$BODYFILE"
}

# --- emit one topic's C array. $1 = topic. The section rows (`page|heading`)
# are read from SECFILE. Each section renders as `## <heading>`, a blank line,
# the body lines, a blank line -- exactly what `jichi guide <topic>` prints.
emit_topic() {
    _topic=$1
    _id=$(printf '%s' "$_topic" | tr 'a-z' 'A-Z')
    printf 'static const char *const GUIDE_%s[] = {\n' "$_id"
    while IFS='|' read -r _page _head; do
        [ -n "$_page" ] || continue
        _pagepath="$ROOT/$_page"
        [ -f "$_pagepath" ] || { printf 'guide_gen: missing page %s\n' "$_page" >&2; exit 1; }
        extract_body "$_pagepath" "$_head" || exit 1
        printf '    "## %s\\n",\n' "$(g_escape "$_head")"
        printf '    "\\n",\n'
        while IFS= read -r _bl; do
            g_emit_body_line "$_bl"
        done < "$BODYFILE"
        printf '    "\\n",\n'
    done < "$SECFILE"
    printf '    NULL,\n};\n\n'
}

# --- the topic -> sections table, in the note's order -------------------------
emit_topic_setup() {
    cat > "$SECFILE" <<'EOF'
docs/SETUP_WIZARD.md|The flow
docs/TUTORIAL_BEGINNER.md|3. Check your setup
EOF
    emit_topic "setup"
}

emit_topic_model() {
    cat > "$SECFILE" <<'EOF'
docs/TUTORIAL_BEGINNER.md|2. Set your API key (hosted models only)
docs/CONFIG_TUTORIAL.md|1. The API key: environment variables, step by step
EOF
    emit_topic "model"
}

emit_topic_learner() {
    cat > "$SECFILE" <<'EOF'
docs/CURRICULUM.md|The stages
docs/CURRICULUM.md|Getting started
EOF
    emit_topic "learner"
}

emit_topic_agents() {
    cat > "$SECFILE" <<'EOF'
docs/AGENTS_GUIDE.md|A good starting point
docs/AGENT_COLLABORATION.md|Supervising
EOF
    emit_topic "agents"
}

emit_topic_limits() {
    cat > "$SECFILE" <<'EOF'
docs/AGENTS_GUIDE.md|What an agent must not do (jichi's limits)
EOF
    emit_topic "limits"
}

# M1199 (G4): the sixth topic. The TUI's slash commands -- the page that
# documents them (TUTORIAL_BEGINNER.md, "6. Handy commands") is the source,
# and guide_lint cell 11 resolves every listed /name against TUI_CMDS, so the
# list cannot name a command that does not exist (the M292 defect).
emit_topic_commands() {
    cat > "$SECFILE" <<'EOF'
docs/TUTORIAL_BEGINNER.md|6. Handy commands
EOF
    emit_topic "commands"
}

# --- file header + includes ---------------------------------------------------
printf '/* SPDX-License-Identifier: Apache-2.0\n'
printf ' * Copyright (c) 2026 Justus-Liebig-Universität Gießen\n'
printf ' * Author: Alexander-Lars Dallmann */\n'
printf '/* jc_guide.c - the compiled-in jichi guide data.\n'
printf ' *\n'
printf ' * GENERATED by tests/tools/guide_gen.sh -- do not edit; run the generator\n'
printf ' * and commit the result (guide_lint checks it). The sections come from\n'
printf ' * the marked page sections in docs/ (the `<!-- jichi-guide -->` comments);\n'
printf ' * a topic array holds its sections in order, each as `## <page heading>`,\n'
printf ' * a blank line, the body lines, a blank line.\n'
printf ' */\n'
printf '#include "jc_guide.h"\n'
printf '#include "jc_skill.h"\n'
printf '#include "jc_str.h"\n'
printf '#include "jc_mem.h"\n\n'
printf '#include <string.h>\n#include <stdlib.h>\n\n'

# --- the six topic arrays -----------------------------------------------------
emit_topic_setup
emit_topic_model
emit_topic_learner
emit_topic_agents
emit_topic_limits
emit_topic_commands

# --- the topic table + the accessors ------------------------------------------
printf 'struct jc_guide_topic GUIDE_TOPICS[] = {\n'
printf '    {"setup", GUIDE_SETUP},\n'
printf '    {"model", GUIDE_MODEL},\n'
printf '    {"learner", GUIDE_LEARNER},\n'
printf '    {"agents", GUIDE_AGENTS},\n'
printf '    {"limits", GUIDE_LIMITS},\n'
printf '    {"commands", GUIDE_COMMANDS},\n'
printf '    {NULL, NULL},\n'
printf '};\n\n'
printf 'struct jc_guide_topic *jc_guide_topics(void)\n'
printf '{\n'
printf '    return GUIDE_TOPICS;\n'
printf '}\n\n'
printf 'const char *const *jc_guide_lines(const char *name)\n'
printf '{\n'
printf '    struct jc_guide_topic *t;\n'
printf '\n'
printf '    if (name == NULL) {\n'
printf '        return NULL;\n'
printf '    }\n'
printf '    for (t = jc_guide_topics(); t->name != NULL; t++) {\n'
printf '        if (strcmp(t->name, name) == 0) {\n'
printf '            return t->lines;\n'
printf '        }\n'
printf '    }\n'
printf '    return NULL;\n'
printf '}\n\n'
printf 'void jc_guide_append_topic(struct jc_sb *out, const char *name)\n'
printf '{\n'
printf '    const char *const *lines;\n'
printf '\n'
printf '    if (name == NULL) {\n'
printf '        return;\n'
printf '    }\n'
printf '    lines = jc_guide_lines(name);\n'
printf '    if (lines == NULL) {\n'
printf '        return;\n'
printf '    }\n'
printf '    for (; *lines != NULL; lines++) {\n'
printf '        jc_sb_append(out, *lines);\n'
printf '    }\n'
printf '}\n\n'
printf 'void jc_guide_skill_body(struct jc_sb *out)\n'
printf '{\n'
printf '    jc_sb_append(out, "# jichi guide\\n");\n'
printf '    jc_sb_append(out, "\\n");\n'
printf '    jc_sb_append(out, "## setup\\n");\n'
printf '    jc_guide_append_topic(out, "setup");\n'
printf '    jc_sb_append(out, "## model\\n");\n'
printf '    jc_guide_append_topic(out, "model");\n'
printf '    jc_sb_append(out, "## learner\\n");\n'
printf '    jc_guide_append_topic(out, "learner");\n'
printf '    jc_sb_append(out, "## agents\\n");\n'
printf '    jc_guide_append_topic(out, "agents");\n'
printf '    jc_sb_append(out, "## limits\\n");\n'
printf '    jc_guide_append_topic(out, "limits");\n'
printf '    jc_sb_append(out, "## commands\\n");\n'
printf '    jc_guide_append_topic(out, "commands");\n'
printf '    jc_sb_append(out, "Project-specific skills live in .jichi/skills/ and "\n'
printf '                    "are listed by `jichi skills`.\\n");\n'
printf '}\n\n'
printf 'void jc_skill_add_builtin(struct jc_skill_set *s, struct jc_arena *a)\n'
printf '{\n'
printf '    struct jc_skill sk;\n'
printf '    struct jc_sb body;\n'
printf '    char *b;\n'
printf '    int i;\n'
printf '    int found;\n'
printf '\n'
printf '    if (s == NULL || a == NULL) {\n'
printf '        return;\n'
printf '    }\n'
printf '    jc_sb_init(&body);\n'
printf '    jc_guide_skill_body(&body);\n'
printf '    b = jc_sb_finish(&body);\n'
printf '\n'
printf '    memset(&sk, 0, sizeof(sk));\n'
printf '    jc_vec_init(&sk.tools, sizeof(char *));\n'
printf '    sk.name = jc_arena_strdup(a, "jichi-guide");\n'
printf '    sk.description = jc_arena_strdup(a,\n'
printf '        "Set up or explain jichi. Load before configuring jichi, or "\n'
printf '        "before telling anyone how to use it.");\n'
printf '    sk.body = jc_arena_strdup(a, b != NULL ? b : "");\n'
printf '    free(b);\n'
printf '    sk.dir = jc_arena_strdup(a, "");\n'
printf '\n'
printf '    /* Add-or-replace, mirroring jc_skill.c: a project skill named\n'
printf '     * jichi-guide loaded AFTER this wins on find; the built-in is\n'
printf '     * skipped when one already occupies the name. */\n'
printf '    found = 0;\n'
printf '    for (i = 0; i < jc_skill_count(s); i++) {\n'
printf '        if (strcmp(jc_skill_at(s, i)->name, "jichi-guide") == 0) {\n'
printf '            found = 1;\n'
printf '            break;\n'
printf '        }\n'
printf '    }\n'
printf '    if (!found) {\n'
printf '        jc_vec_push(&s->skills, &sk);\n'
printf '    }\n'
printf '}\n'
