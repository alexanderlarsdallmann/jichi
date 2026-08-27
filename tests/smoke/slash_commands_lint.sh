#!/bin/sh
# smoke lint: every /command mentioned in a source string must actually resolve
# (M295).
#
# The check for the M292 defect: two shipped strings told people to run
# `/learn corrections`, which had never existed in either front-end. M292 found it
# by reading; M294 built the operation. This is the lint, because "the audit found
# what it knew to look for" (docs/TEST_INTEGRITY.md) and nobody re-reads 5,200
# strings. Replanting that exact defect in jc_memory.c's warning is one of this
# driver's proofs-of-teeth -- it is flagged, at its own file and line.
#
# GROUND TRUTH is the union of four places a `/name` may resolve, the same
# "completion table + dispatched names" basis M262's builtin_cmds_lint uses:
#   1. a TUI handler      -- strcmp/strncmp(line, "/x") in jc_tui.c. The AUTHORITY:
#                            it is what makes typing the command work.
#   2. the TUI completion table -- `static const char *TUI_CMDS[]` in
#      jc_tui.c (file-scope since M345, shared with the did-you-mean
#      suggester; the scrape's floor below caught the rename the same day).
#   3. a CLI subcommand   -- strcmp(args.pos[0], "x") in main.c, since jichi's docs
#                            and messages write `/learn analyze` and
#                            `learn analyze` for the same operation.
#   4. a scaffolded file command -- "commands/<name>.md" in a jc_scaffold.c pack.
# 1 and 2 are both consulted rather than one derived from the other: they disagreed
# when this lint was written (six handlers absent from the completion table,
# including the real `/route` -- since added, which in turn revealed `route` missing
# from jc_assetval's BUILTIN_CMDS[]; the easter eggs stay out on purpose), and a
# mention of a working command must not be an error because Tab does not complete it.
#
# TWO WORDS, because one word would not have caught the defect this exists for.
# `/learn corrections` yields `learn`, which resolves -- so a first-word-only lint
# is silent on exactly the string that motivated it. Commands with multi-word
# completion entries (today only `/learn`) get their SECOND word checked too. The
# contract that makes this exact rather than a guess about English: an UNDELIMITED
# `/cmd word` means a subcommand call, so a mention of the command itself in prose
# is backticked -- `/learn` -- which most of the tree already did.
#
# THE FALSE-POSITIVE PROBLEM, and how it was solved. A naive scan drowns in `/v1`,
# `/bin/sh`, `%s/.jichi/...`. Four rules, each a fact about C or about paths rather
# than a guess about English -- no run/type/via cue list was needed, and a cue list
# would have been the guess:
#   a. the '/' must OPEN a token: at string start, or after space ( ' " -- or after
#      a backtick that is itself not preceded by a word character, which admits
#      `/learn` while rejecting ``require`/alias`` (a '/' meaning "or" after a
#      CLOSING backtick: five of the six original findings were this one shape).
#   b. the token must not continue as a path or filename: no following / . _ or
#      further word characters. Kills /dev/null, /tmp/x, /v1/chat.
#   c. the containing string must contain a space -- be PROSE. A message telling a
#      user to run something is a sentence; a bare path literal ("/v1", "/proc",
#      "/embeddings") is a token. This is the lint's one real limitation: a mention
#      that is an entire string, with no surrounding prose, is not checked.
#   d. COMMENTS ARE NOT STRINGS. The first version of this lint reported a finding
#      against its own explanatory comment, because a regex for quoted text also
#      matches quoted text inside /* */ -- and this codebase's comments quote
#      messages constantly. Hence a small lexer, not a regex.
# What survived all four was one hit: the glossary defining "command" as "a /slash
# shortcut", REWORDED rather than excepted. This lint has NO exception list, which
# is the only reason to trust it -- an exception list is where a lint goes to die.
#
# Strings are reassembled first: C89 caps a literal at 509 chars, so jichi's
# messages are split across lines. The jc_memory.c warning behind M292 ends one
# literal with "(via " and starts the next with "/learn)".
#
# One awk pass does the lexing and the scanning. A shell loop forking two greps per
# record took 22 s here, which is ~10 minutes at the Pi Zero 2 W's measured x28
# (M272) -- and `make check-target` on a small board is a gate this project keeps.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
tmp=$(smoke_tmp)
root="$SMOKE_ROOT"

# scan FILES... -> "K<TAB>name<TAB>loc" records, K = F (command) or S (subcommand).
# Lexes C, reassembles adjacent literals, applies rules (a)-(d).
scan() {
    awk '
      function flush() {
        if (buf != "") { emit(buf, FILENAME ":" start); buf = ""; start = "" }
      }
      function opener(s, i,    p, q) {          # rule (a)
        if (i == 1) return 1
        p = substr(s, i - 1, 1)
        if (p == " " || p == "(" || p == "\"" || p == "'"'"'") return 1
        if (p != "`") return 0
        if (i == 2) return 1
        q = substr(s, i - 2, 1)
        return (q !~ /[A-Za-z0-9_)]/)
      }
      function emit(s, loc,    i, L, j, w, nx, k, w2) {
        if (index(s, " ") == 0) return           # rule (c): prose only
        L = length(s); i = 1
        while (i <= L) {
          if (substr(s, i, 1) != "/" || !opener(s, i)) { i++; continue }
          j = i + 1; w = ""
          while (j <= L && substr(s, j, 1) ~ /[a-z0-9-]/) { w = w substr(s, j, 1); j++ }
          if (w == "" || w !~ /^[a-z]/) { i++; continue }
          nx = (j <= L) ? substr(s, j, 1) : ""
          if (nx ~ /[a-z0-9\/._-]/) { i = j; continue }   # rule (b)
          print "F\t" w "\t" loc
          if (nx == " ") {                       # a possible subcommand
            k = j + 1; w2 = ""
            while (k <= L && substr(s, k, 1) ~ /[a-z0-9-]/) { w2 = w2 substr(s, k, 1); k++ }
            if (w2 ~ /^[a-z]/) print "S\t" w " " w2 "\t" loc
          }
          i = j
        }
      }
      FNR == 1 { flush(); st = 0 }
      {
        acc = ""; n = 0; i = 1; L = length($0)
        while (i <= L) {
          ch = substr($0, i, 1)
          if (st == 0) {                                   # code
            if (ch == "/" && substr($0, i + 1, 1) == "*") { st = 2; i += 2; continue }
            if (ch == "\"") { st = 1; i++; continue }
            if (ch == "'"'"'") { st = 3; i++; continue }
            i++
          } else if (st == 1) {                            # inside "..."
            if (ch == "\\") { acc = acc substr($0, i, 2); i += 2; continue }
            if (ch == "\"") { st = 0; n++; i++; continue }
            acc = acc ch; i++
          } else if (st == 2) {                            # inside /* */  (rule d)
            if (ch == "*" && substr($0, i + 1, 1) == "/") { st = 0; i += 2; continue }
            i++
          } else {                                         # inside char constant
            if (ch == "\\") { i += 2; continue }
            if (ch == "'"'"'") { st = 0 }
            i++
          }
        }
        if (n > 0) { buf = buf acc; if (start == "") start = FNR }
        else if (st != 1) { flush() }
      }
      END { flush() }
    ' "$@"
}

# --- ground truth --------------------------------------------------------------
grep -ohE '(strcmp|strncmp)\(line, "/[a-z0-9-]+' "$root/src/tui/jc_tui.c" \
    | grep -oE '"/[a-z0-9-]+' | sed 's|"/||' | sort -u > "$tmp/tui_handled"
sed -n '/static const char \*TUI_CMDS\[\] = {/,/^};$/p' "$root/src/tui/jc_tui.c" \
    | grep -oE '"/[a-z0-9-]+' | sed 's|"/||' | sort -u > "$tmp/tui_table"
grep -ohE 'strcmp\(args\.pos\[0\], "[a-z0-9-]+"\)' "$root/src/main.c" \
    | grep -oE '"[a-z0-9-]+"' | tr -d '"' | sort -u > "$tmp/cli"
grep -ohE '"commands/[a-z0-9-]+\.md"' "$root/src/scaffold/jc_scaffold.c" \
    | sed 's|"commands/||; s|\.md"||' | sort -u > "$tmp/filecmds"
sed -n '/static const char \*TUI_CMDS\[\] = {/,/^};$/p' "$root/src/tui/jc_tui.c" \
    | grep -oE '"/[a-z0-9-]+ [a-z0-9-]+"' | tr -d '"' | sed 's|^/||' \
    | sort -u > "$tmp/two_word"
awk '{print $1}' "$tmp/two_word" | sort -u > "$tmp/has_subs"

cat "$tmp/tui_handled" "$tmp/tui_table" "$tmp/cli" "$tmp/filecmds" \
    | sed 's/ .*//' | sort -u > "$tmp/known"
nk=$(grep -c . "$tmp/known" || true)
nh=$(grep -c . "$tmp/tui_handled" || true)
ncli=$(grep -c . "$tmp/cli" || true)
n2=$(grep -c . "$tmp/two_word" || true)

# A shrinking extraction must fail LOUDLY. If a dispatch shape changes, the universe
# silently empties and every mention becomes an error -- or the scan empties too and
# the lint passes while checking nothing (the M285 discipline).
if [ "$nh" -ge 40 ] && [ "$ncli" -ge 30 ] && [ "$nk" -ge 60 ] && [ "$n2" -ge 3 ]; then
    t_ok "ground truth: $nh TUI handlers, $ncli CLI subcommands, $nk names, $n2 two-word"
else
    t_fail "suspiciously few commands extracted (handlers=$nh cli=$ncli total=$nk
 two-word=$n2) -- did a dispatch shape change? Fix the extraction, do not relax
 the floor: a lint with an empty universe flags everything, or nothing"
fi

# --- candidates ----------------------------------------------------------------
find "$root/src" -name '*.c' | sort > "$tmp/files"
nf=$(grep -c . "$tmp/files" || true)
# shellcheck disable=SC2046
scan $(cat "$tmp/files") > "$tmp/hits"
nhit=$(grep -c . "$tmp/hits" || true)
if [ "$nf" -ge 100 ] && [ "$nhit" -ge 60 ]; then
    t_ok "scanned $nf sources, $nhit /command mention(s) in reassembled strings"
else
    t_fail "scan looks broken (files=$nf mentions=$nhit) -- a lint that examines
 nothing is not a passing lint"
fi

awk -F'\t' '$1=="F" {print $2}' "$tmp/hits" | sort -u > "$tmp/cands"
ncand=$(grep -c . "$tmp/cands" || true)
if [ "$ncand" -ge 40 ]; then
    t_ok "found $ncand distinct /command name(s) mentioned"
else
    t_fail "only $ncand distinct mentions -- the scanner is under-reading"
fi

# --- check 1: the command exists -----------------------------------------------
comm -23 "$tmp/cands" "$tmp/known" > "$tmp/unresolved"
nu=$(grep -c . "$tmp/unresolved" || true)
if [ "$nu" -eq 0 ]; then
    t_ok "every /command mentioned in a source string exists"
else
    t_fail "$nu /command mention(s) name nothing a user can run:"
    while read -r w; do
        printf '    | /%s  (e.g. %s)\n' "$w" \
            "$(awk -F'\t' -v W="$w" '$1=="F" && $2==W {print $3; exit}' "$tmp/hits")"
    done < "$tmp/unresolved"
fi

# --- check 2: its subcommand exists too ----------------------------------------
: > "$tmp/bad_subs"
awk -F'\t' '$1=="S" {print $2}' "$tmp/hits" | sort -u | while read -r m; do
    grep -qx "${m%% *}" "$tmp/has_subs" || continue   # no declared subcommands
    grep -qx "$m" "$tmp/two_word" || printf '%s\n' "$m" >> "$tmp/bad_subs"
done
nb=$(grep -c . "$tmp/bad_subs" 2>/dev/null || true)
[ -z "$nb" ] && nb=0
if [ "$nb" -eq 0 ]; then
    t_ok "every mentioned subcommand of a multi-word command exists"
else
    t_fail "$nb mention(s) name a subcommand that does not exist:"
    while read -r m; do
        printf '    | /%s  (e.g. %s)\n' "$m" \
            "$(awk -F'\t' -v M="$m" '$1=="S" && $2==M {print $3; exit}' "$tmp/hits")"
    done < "$tmp/bad_subs"
    echo "    | Two fixes; which applies is a question about the prose:" >&2
    echo "    |   * a real promise of a subcommand that does not exist -- build" >&2
    echo "    |     it, or name the operation that does (this is the M292 bug)" >&2
    echo "    |   * the next word is just English (\"the /learn command\") --" >&2
    echo "    |     backtick the command so it reads as a unit: \`/learn\`." >&2
fi

# --- the lint's own teeth ------------------------------------------------------
# A lint nobody has watched fail is a lint nobody has watched work. Both fixtures
# go through the same scan() the tree does.
mkdir -p "$tmp/fake"
cat > "$tmp/fake/probe.c" <<'EOF'
static const char *msg =
    "memory.md is too large; supersede stale notes by running "
    "/learn consolidate-everything and then carry on";
static const char *m2 = "unknown state: try /frobnicate to recover";
/* A comment mentioning "/alsofake nonsense" must NOT be scanned (rule d). */
EOF
scan "$tmp/fake/probe.c" > "$tmp/fake/h"
if awk -F'\t' '$1=="F" && $2=="frobnicate"' "$tmp/fake/h" | grep -q . &&
   awk -F'\t' '$1=="S" && $2=="learn consolidate-everything"' "$tmp/fake/h" \
       | grep -q . &&
   ! grep -q "alsofake" "$tmp/fake/h" &&
   ! grep -qx "learn consolidate-everything" "$tmp/two_word"; then
    t_ok "scanner sees an unknown command and subcommand, and skips comments"
else
    t_fail "the scanner missed a planted mention -- it would miss a real one"
    sed 's/^/    | /' "$tmp/fake/h"
fi

# And a path must NOT be read as a command, or the lint cries wolf and gets ignored
# wholesale (the M203 reasoning behind narrowing doctor's verify warning).
cat > "$tmp/fake/paths.c" <<'EOF'
static const char *a = "POST to http://host/v1/chat for the completion";
static const char *b = "spawning /bin/sh -c for the command";
static const char *c = "reading /dev/null and .jichi/agents/mentor.md now";
static const char *d = "prefer `Data.Map`/assoc lists in hot paths";
static const char *e = "/v1";
static const char *f = "/embeddings";
static const char *g = "see `/learn` for the mentor";
EOF
scan "$tmp/fake/paths.c" | awk -F'\t' '$1=="F" {print $2}' | sort -u \
    > "$tmp/fake/pc"
# `/learn` IS a real mention and must be seen; nothing else here may be.
if [ "$(cat "$tmp/fake/pc")" = "learn" ]; then
    t_ok "paths, URLs, bare literals and \`x\`/y are not commands; \`/learn\` is"
else
    t_fail "wrong candidate set from the paths fixture:"
    sed 's:^:    | /:' "$tmp/fake/pc"
fi

t_done
