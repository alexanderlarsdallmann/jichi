#!/bin/sh
# man-options-from-help.sh -- derive the man page's OPTIONS roff from `jichi --help`.
#
# WHAT THIS IS, AND IS NOT. An AID, not a build step. man/jichi.1 is hand-maintained:
# it carries prose (DESCRIPTION, INTERACTIVE KEYS, ENVIRONMENT, FILES) that no
# generator could write, and tests/smoke/man_page_lint.sh -- not this script -- is
# what keeps its flag coverage honest. Run it when a batch of flags has landed and
# you want the roff for them without typing 148 `.TP` blocks by hand; then EDIT the
# result. Nothing in `make` calls it.
#
# WHY IT EXISTS. At M539 the man page documented 77 of the 148 long flags the parser
# accepts -- 49% -- because it was the last user-facing namespace with no parity
# lint. Closing that by hand is a transcription job with 148 chances to introduce a
# typo the lint would then dutifully report as a phantom flag. Deriving it from
# --help, whose text is already written and already reviewed, removes those chances.
#
# USAGE
#   scripts/man-options-from-help.sh ./jichi            # whole OPTIONS block
#   scripts/man-options-from-help.sh ./jichi Options    # one --help section
#   scripts/man-options-from-help.sh ./jichi Commands   # the COMMANDS listing
#
# It reads `--help`'s own four section headings and emits a .SS per section, so the
# man page inherits the grouping a human already curated rather than inventing one.
set -e

BIN="${1:-./jichi}"
WANT="${2:-}"

"$BIN" --help 2>&1 | awk -v want="$WANT" '
# A section heading in --help is a non-indented line ending in ":".
/^[A-Za-z][^ ].*:$/ {
    sect = $0
    sub(/:$/, "", sect)
    insect = (want == "" || index(sect, want) == 1)
    if (insect) {
        printf ".SS %s\n", roff(sect)
    }
    next
}
# A flag line: two spaces, an optional short alias, then one or more long flags,
# then two-or-more spaces, then the description.
# A command line: two spaces then a lowercase name (no leading dash). Only read
# when the caller asked for Commands, so the flag sections are unaffected.
/^  [a-z][a-z-]*( |$)/ {
    if (!insect || want != "Commands") { next }
    line = $0
    i = 0; n = 0
    sub(/^ +/, "", line)
    if (match(line, /  +/)) {
        cmd  = substr(line, 1, RSTART - 1)
        cdsc = substr(line, RSTART + RLENGTH)
    } else {
        cmd = line; cdsc = ""
    }
    printf ".TP\n.B %s\n", roff(cmd)
    if (cdsc != "") { printf "%s\n", roff(cdsc) }
    pending = 1
    next
}
/^  +(-[a-zA-Z], )?-/ {
    if (!insect) { next }
    line = $0
    i = 0; n = 0
    sub(/^ +/, "", line)
    # Split flags from description. A run of 2+ spaces is the usual separator,
    # but not always: `--prompt-b64 <b64> Base64 of the prompt text` uses ONE
    # space, and splitting on the first run of two collapsed the whole line into
    # the .B, putting the description in bold. So consume leading tokens while
    # they still look like part of a synopsis -- a flag, a comma, a bare "-", or a
    # bracketed argument -- and call the rest the description.
    n = split(line, tok, " ")
    flags = ""; desc = ""
    for (i = 1; i <= n; i++) {
        if (desc == "" && (tok[i] ~ /^-/ || tok[i] ~ /^[<[]/ || tok[i] ~ /,$/ \
                           || tok[i] == "/")) {
            flags = (flags == "") ? tok[i] : flags " " tok[i]
        } else {
            desc = (desc == "") ? tok[i] : desc " " tok[i]
        }
    }
    if (flags == "") { flags = line; desc = "" }
    printf ".TP\n.B %s\n", roff(flags)
    if (desc != "") { printf "%s\n", roff(desc) }
    pending = 1
    next
}
# A continuation line: deeply indented, no leading flag. Appended to the previous
# description rather than dropped -- several of the most important notes in --help
# (the `ps` warning on --config-json, for one) live entirely on a continuation.
/^ {8,}[^ -]/ {
    if (!insect || !pending) { next }
    line = $0
    sub(/^ +/, "", line)
    printf "%s\n", roff(line)
    next
}
{ pending = 0 }

# roff-escape: every hyphen becomes \- (an unescaped one is a soft hyphen and may
# be rendered as a line break), and a leading . or \x27 would start a request.
function roff(s,   out, i, c) {
    out = ""
    for (i = 1; i <= length(s); i++) {
        c = substr(s, i, 1)
        if (c == "-") { out = out "\\-" }
        else if (c == "\\") { out = out "\\e" }
        else { out = out c }
    }
    if (substr(out, 1, 1) == "." || substr(out, 1, 1) == "\x27") {
        out = "\\&" out
    }
    return out
}
'
