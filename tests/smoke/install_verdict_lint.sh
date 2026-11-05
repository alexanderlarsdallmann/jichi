#!/bin/sh
# smoke lint: every install page states the verdict PLATFORMS.md gives its platform
# (M945; plan 2026-10-03, step 7.6).
#
# WHY THIS EXISTS. PLATFORMS.md has said macOS is "Partly verified, driven" since M817
# (2026-09-26). A week later install/macos.md, install/README.md, INSTALL.md and
# PREPARE_AND_BUILD.md still said "never compiled", and README.md said "verified";
# M943 fixed them by hand. The matrix is the page that owns the verdicts; the install
# pages repeat them for a reader who will never open the matrix, and nothing tied the
# copies to it.
#
# CHECKED      -- every page in docs/install/ except README.md, through the MAP below,
#                 which names the PLATFORMS.md row each page stands for. A row's
#                 verdict is the section it sits in (### Verified, ### Partly verified,
#                 ### Never compiled). A page's verdict is the first non-blank line of
#                 its "## 1. Status" section. A verdict is its class: "never compiled",
#                 else "partly verified", else "verified" -- in that order, so "partly
#                 verified" is never read as "verified".
#   1  every install page is in the MAP, and every MAP row is found in the matrix
#   2  each page's Status line has the class of its row's section
#   3  install/README.md's table gives each page that class
#   4  INSTALL.md's table, where its row names a class, gives each page that class
# THE FLOOR    -- 13 install pages on 2026-10-04.
# NOT CHECKED  -- "Driven" (the matrix keeps it in a separate register), the prose
#                 elsewhere on a page, and pages outside docs/install/ that mention a
#                 platform (PREPARE_AND_BUILD.md, README.md): those state verdicts in
#                 sentences a pattern cannot read without misreading some.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
root=$(cd "$(dirname "$0")/../.." && pwd)
P="$root/docs/PLATFORMS.md"
JC_IV_FLOOR=13

# page|a substring of its PLATFORMS.md matrix row (unique among the rows)
MAP='alpine|**The M542 architecture re-sweep
debian-ubuntu|**Linux, glibc** — the development box
fedora|**Linux, glibc** — the development box
raspberry-pi|**Raspberry Pi 400**
old-linux|**Linux, old userlands**
guix|**Guix System
gnu-hurd|**GNU/Hurd**
freebsd|| **FreeBSD** |
openbsd|| **OpenBSD** |
netbsd|| **NetBSD** |
illumos|**illumos / Solaris**
wsl2|**Windows 11 + WSL2**
macos|**macOS / Darwin**'

# class TEXT -> never | partly | verified | none
class() {
    printf '%s\n' "$1" | tr 'A-Z' 'a-z' | awk '
        /never compiled/ { c = "never"; exit }
        /partly verified/ { c = "partly"; exit }
        /verified/ { c = "verified"; exit }
        END { print (c == "" ? "none" : c) }'
}
# row_class KEY -> the class of the matrix section holding the row, or "missing"
row_class() {
    awk -v k="$1" '
        /^### Verified/ { s = "verified" } /^### Partly verified/ { s = "partly" }
        /^### Never compiled/ { s = "never" } /^### Per-platform|^### Driven/ { s = "" }
        /^\| / && s != "" && index($0, k) { print s; found = 1; exit }
        END { if (!found) print "missing" }' "$P"
}

pages=$(cd "$root/docs/install" && ls *.md | grep -v -x 'README.md' | sed 's/\.md$//' | sort)
npages=$(printf '%s\n' "$pages" | grep -c .)
mapped=$(printf '%s\n' "$MAP" | cut -d'|' -f1 | sort)

# --- 1: the MAP covers the pages, and its rows exist --------------------------------
unmapped=$(printf '%s\n' "$pages" | while IFS= read -r p; do
    printf '%s\n' "$mapped" | grep -q -x "$p" || printf '%s ' "$p"; done)
norow=$(printf '%s\n' "$MAP" | while IFS='|' read -r p k; do
    [ "$(row_class "$k")" = missing ] && printf '%s ' "$p"; done)
if [ "$npages" -ge "$JC_IV_FLOOR" ] && [ -z "$unmapped" ] && [ -z "$norow" ]; then
    t_ok "every install page maps to a matrix row ($npages pages, floor $JC_IV_FLOOR)"
else
    t_fail "$npages pages (floor $JC_IV_FLOOR); not in the MAP: ${unmapped:-none}; row not found: ${norow:-none}"
fi

# --- 2-4: the three copies agree with the matrix ------------------------------------
bad2=""; bad3=""; bad4=""
while IFS='|' read -r p k; do
    want=$(row_class "$k")
    st=$(awk '/^## 1\. Status/ { f = 1; next } f && /[^ \t]/ { print; exit }' "$root/docs/install/$p.md")
    got=$(class "$st")
    [ "$got" = "$want" ] || bad2="$bad2 $p(page $got, matrix $want)"
    ix=$(grep '^|' "$root/docs/install/README.md" | grep -F "($p.md)" | head -n 1 | cut -d'|' -f3)
    got=$(class "$ix")
    [ "$got" = "$want" ] || bad3="$bad3 $p(index $got, matrix $want)"
    ir=$(grep '^|' "$root/docs/INSTALL.md" | grep -F "(install/$p.md)" | head -n 1 | cut -d'|' -f2)
    got=$(class "$ir")
    [ "$got" = none ] || [ "$got" = "$want" ] || bad4="$bad4 $p(INSTALL.md $got, matrix $want)"
done <<EOF
$MAP
EOF

if [ -z "$bad2" ]; then t_ok "each install page's Status line states its row's verdict"
else t_fail "Status lines disagree with PLATFORMS.md:$bad2"; fi
if [ -z "$bad3" ]; then t_ok "install/README.md gives each page its row's verdict"
else t_fail "install/README.md disagrees with PLATFORMS.md:$bad3"; fi
if [ -z "$bad4" ]; then t_ok "INSTALL.md, where it names a verdict, gives its row's"
else t_fail "INSTALL.md disagrees with PLATFORMS.md:$bad4"; fi

t_done
