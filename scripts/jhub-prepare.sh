#!/bin/sh
# jhub-prepare.sh - download and stage everything needed to TEST jichi under
# JupyterHub, so the test can then be run offline and repeated exactly.
#
# WHY THIS EXISTS.  docs/plans/2026-08-jichi-with-jupyterhub.md answered the
# design question ("can jichi be used as a CLI tool with JupyterHub?") and
# marked every claim about JupyterHub's own behaviour [unverified], because
# there is no jupyter, jupyter-server or jupyterhub on this bench.  This script
# downloads what would make those claims measurable.  It measures nothing
# itself -- jhub-verify.sh does that -- it only stages artifacts and records
# what it staged.
#
# WHAT IT DELIBERATELY DOES NOT DO:
#   * it never installs anything system-wide and never wants root.  The bench
#     it was written on has no passwordless sudo, which is what chose the
#     shape: JupyterLab (single-user) runs from a venv HERE, and the real
#     multi-user Hub runs in a VM, where apt supplies configurable-http-proxy.
#   * it therefore never needs npm.  A Hub on this host would need
#     configurable-http-proxy -> npm -> a password.
#   * it never writes inside the jichi checkout, and REFUSES to run if asked
#     to (the rule scripts/pin-driver.sh already enforces, for the reason
#     docs/SESSION_RUNBOOK.md gives: a rig that dirties the tree it tests is
#     worthless in both directions).
#
# TWO WHEELHOUSES, ON PURPOSE.  `pip download` bakes in the interpreter version
# AND the platform.  The host set (cp314, x86-64) will not install in a Debian
# 12 guest (cp311).  One wheelhouse would be a silent assumption that only
# shows up as a confusing failure inside the VM.
#
# Usage:
#   sh jhub-prepare.sh                 # everything
#   sh jhub-prepare.sh --dry-run       # print every step, touch nothing
#   sh jhub-prepare.sh --only venv     # one stage (see STAGES below)
#   sh jhub-prepare.sh --skip guest    # skip a stage, repeatable
#
# STAGES, in order:
#   check  wheelhouse-host  venv  wheelhouse-guest  sdists  fixtures  vmbase  manifest
#
# Env:
#   JHUB_DIR    where artifacts land   (default ~/.cache/jichi-jupyterhub)
#   (the scripts live in scripts/; only JHUB_DIR is configurable)
#   TIER_V_DIR  reused for the VM base image
#                                      (default ~/.cache/jichi-tier-v)
#   JHUB_REPO   the jichi checkout to refuse writing into
#                                      (default: the parent of scripts/)
#
# Exit: 0 staged   1 a stage failed   2 usage   3 refused (path inside the repo)
set -eu

DRY=0
ONLY=""
SKIP=""

for a in "$@"; do
    case "$a" in
        --dry-run) DRY=1 ;;
        --only)    ONLY="__next__" ;;
        --skip)    SKIP="$SKIP __next__" ;;
        -h|--help) sed -n '2,45p' "$0"; exit 0 ;;
        -*)        echo "unknown option: $a" >&2; exit 2 ;;
        *)
            case "$ONLY" in __next__) ONLY="$a"; continue ;; esac
            case "$SKIP" in *__next__*) SKIP=$(printf '%s' "$SKIP" | sed 's/__next__/'"$a"'/'); continue ;; esac
            echo "unexpected argument: $a" >&2; exit 2 ;;
    esac
done

DIR="${JHUB_DIR:-$HOME/.cache/jichi-jupyterhub}"
STAGE=$(cd "$(dirname "$0")" && pwd)          # scripts/ -- this file lives there now
TIERV="${TIER_V_DIR:-$HOME/.cache/jichi-tier-v}"
REPO="${JHUB_REPO:-$(cd "$STAGE/.." && pwd -P)}"

VENV="$DIR/venv"
PY="$VENV/bin/python"
PIP="$VENV/bin/pip"

DEB_URL="https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2"

say()  { printf '%s\n' "$*"; }
step() { printf '\n=== %s ===\n' "$*"; }
run()  {
    if [ "$DRY" = 1 ]; then printf '  + %s\n' "$*"; return 0; fi
    printf '  + %s\n' "$*"
    "$@"
}
skipped() {
    case " $SKIP " in *" $1 "*) return 0 ;; esac
    [ -n "$ONLY" ] && [ "$ONLY" != "$1" ] && return 0
    return 1
}

# ---------------------------------------------------------------- check ------
# The refusal comes first and is not skippable: everything after it writes.
inside_repo() {
    p=$(cd "$1" 2>/dev/null && pwd -P) || p="$1"
    r=$(cd "$REPO" 2>/dev/null && pwd -P) || return 1
    case "$p/" in "$r"/*) return 0 ;; esac
    [ "$p" = "$r" ] && return 0
    return 1
}
# The ARTIFACTS may never land in the checkout; the SCRIPTS live there by
# design, so only $JHUB_DIR is checked. Before these graduated into scripts/
# this also guarded a staging directory, and keeping that check would now
# refuse every legitimate run.
if inside_repo "$DIR"; then
    echo "REFUSED: \$JHUB_DIR resolves inside the jichi checkout" >&2
    echo "  repo: $REPO" >&2
    echo "  dir:  $DIR" >&2
    echo "A rig that dirties the tree it tests contaminates both results:" >&2
    echo "your result is polluted by the build, and the build's by your files." >&2
    exit 3
fi

if ! skipped check; then
    step "check -- the host, stated before anything is downloaded"
    say "  repo (never written): $REPO"
    say "  artifacts:            $DIR"
    say "  staging:              $STAGE"
    for t in python3 curl sha256sum; do
        command -v "$t" >/dev/null 2>&1 || { echo "missing required tool: $t" >&2; exit 1; }
    done
    say "  python3:              $(python3 -V 2>&1)"
    say "  free on \$JHUB_DIR:    $(df -h "$DIR" | awk 'NR==2{print $4}')"
    for t in qemu-system-x86_64 qemu-img xorriso ssh; do
        if command -v "$t" >/dev/null 2>&1; then say "  vm tool $t: yes"
        else say "  vm tool $t: NO -- tier J2 (the VM hub) cannot run"; fi
    done
    [ -e /dev/kvm ] && say "  /dev/kvm:             present" || say "  /dev/kvm:             ABSENT (TCG only -- slow)"
    if [ "$DRY" = 0 ]; then mkdir -p "$DIR"; fi
fi

# ------------------------------------------------------- requirements --------
# Pinned. An unpinned wheelhouse is not a reproducible artifact, and this one is
# meant to be copyable to an air-gapped course server.
write_reqs() {
    cat > "$DIR/requirements-host.txt" <<'REQ'
# jichi x JupyterHub test bench -- HOST tier (this machine, cp314/x86-64).
# JupyterLab only: the Hub itself runs in the VM, because a Hub needs
# configurable-http-proxy -> npm -> root, and this bench has no passwordless sudo.
jupyterlab==4.6.3
jupyter-server-proxy==4.5.0
jupytext==1.19.5
nbstripout
nbconvert
nbformat
ipykernel
# jupyterhub is installed here for its ENTRY POINTS and its shipped docs only.
# `jupyterhub-singleuser` is what a real hub spawns, so having it lets the host
# tier exercise the same single-user server a hub would.
jupyterhub==5.5.1
# drives terminado's websocket, which is how the terminal contract is tested
# headlessly. Python's stdlib has no websocket client.
websocket-client
REQ
    cat > "$DIR/requirements-guest.txt" <<'REQ'
# jichi x JupyterHub test bench -- GUEST tier (Debian 12 VM, cp311/manylinux2014).
# A SEPARATE file because a wheelhouse is interpreter- and platform-specific.
jupyterhub==5.5.1
jupyterlab==4.6.3
jupyter-server-proxy==4.5.0
jupytext==1.19.5
nbstripout
nbformat
ipykernel
# MEASURED, not guessed: `pip download --python-version 3.11` does NOT resolve
# every marker-gated dependency of the target environment. jupyter-server needs
# `overrides>=5.0; python_version < "3.12"`, which the cp314 host never needs and
# pip therefore never fetched -- and the gap only surfaced when the wheelhouse was
# INSTALLED in the Debian 12 guest. A cross-version wheelhouse is not verified by
# a successful download; it is verified by an install on the target.
overrides>=5.0
REQ
}

# Written whenever ANY stage that consumes them runs -- not only inside the host
# stage. `--only wheelhouse-guest` used to read a STALE requirements-guest.txt
# left by an earlier run, so an edit to the pinned set silently did nothing.
if [ "$DRY" = 0 ]; then
    mkdir -p "$DIR"
    write_reqs
fi

if ! skipped wheelhouse-host; then
    step "wheelhouse-host -- download BEFORE install, so the install is repeatable"
    run mkdir -p "$DIR/wheelhouse-host"
    run python3 -m pip download -q -r "$DIR/requirements-host.txt" \
        -d "$DIR/wheelhouse-host"
    [ "$DRY" = 0 ] && say "  wheels: $(ls "$DIR/wheelhouse-host" | wc -l), $(du -sh "$DIR/wheelhouse-host" | cut -f1)"
fi

if ! skipped venv; then
    step "venv -- installed OFFLINE from the wheelhouse, which proves it is complete"
    run python3 -m venv "$VENV"
    run "$PIP" install -q --upgrade pip
    run "$PIP" install -q --no-index --find-links="$DIR/wheelhouse-host" \
        -r "$DIR/requirements-host.txt"
    if [ "$DRY" = 0 ]; then
        "$PIP" freeze > "$DIR/versions-host.txt"
        say "  jupyter lab:  $("$VENV/bin/jupyter-lab" --version 2>&1 | head -1)"
        say "  jupyterhub:   $("$VENV/bin/jupyterhub" --version 2>&1 | head -1)"
        say "  frozen:       $(wc -l < "$DIR/versions-host.txt") packages -> versions-host.txt"
    fi
fi

if ! skipped wheelhouse-guest; then
    step "wheelhouse-guest -- cp311/manylinux2014 for the Debian 12 VM"
    run mkdir -p "$DIR/wheelhouse-guest"
    # --only-binary=:all: is REQUIRED with --platform/--python-version: pip cannot
    # build a source dist for an interpreter it is not running. A package with no
    # matching wheel therefore FAILS here rather than silently producing a set that
    # will not install in the guest -- which is the outcome we want to know about.
    if [ "$DRY" = 1 ]; then
        run python3 -m pip download -r "$DIR/requirements-guest.txt" \
            --only-binary=:all: --python-version 3.11 \
            --platform manylinux2014_x86_64 -d "$DIR/wheelhouse-guest"
    else
        if python3 -m pip download -q -r "$DIR/requirements-guest.txt" \
             --only-binary=:all: --python-version 3.11 \
             --platform manylinux2014_x86_64 -d "$DIR/wheelhouse-guest" \
             2> "$DIR/wheelhouse-guest.err"; then
            say "  wheels: $(ls "$DIR/wheelhouse-guest" | wc -l), $(du -sh "$DIR/wheelhouse-guest" | cut -f1)"
            : > "$DIR/wheelhouse-guest.INCOMPLETE" && rm -f "$DIR/wheelhouse-guest.INCOMPLETE"
        else
            say "  INCOMPLETE -- pip could not satisfy every guest requirement as a wheel."
            say "  This is recorded, not papered over: the VM tier will install from its"
            say "  own network at provision time and jhub-verify.sh records what it got."
            sed 's/^/    | /' "$DIR/wheelhouse-guest.err" | head -12
            printf 'incomplete: see wheelhouse-guest.err\n' > "$DIR/wheelhouse-guest.INCOMPLETE"
        fi
    fi
fi

if ! skipped sdists; then
    step "sdists -- each project's OWN docs, version-matched and offline"
    run mkdir -p "$DIR/sdists" "$DIR/upstream-docs"
    # NOT `pip download --no-binary :all:`.  That flag does not mean "fetch the
    # source archive": it makes pip BUILD every sdist -- and every BUILD
    # DEPENDENCY's sdist -- just to read metadata.  Measured here: it ran for
    # 5m30s and then died on a build-tracker collision (jupyterlab_pygments
    # "is already being built"), having downloaded nothing.  We want the
    # tarball, so ask the index for the tarball: the PyPI JSON API names it.
    for spec in jupyterhub==5.5.1 jupyter-server-proxy==4.5.0 jupytext==1.19.5; do
        name=${spec%%==*}
        ver=${spec##*==}
        if [ "$DRY" = 1 ]; then
            say "  + curl https://pypi.org/pypi/$name/$ver/json -> sdist url -> $DIR/sdists/"
            continue
        fi
        url=$(curl -sS --max-time 30 "https://pypi.org/pypi/$name/$ver/json" \
              | python3 -c 'import json,sys; d=json.load(sys.stdin); print(next((u["url"] for u in d["urls"] if u["packagetype"]=="sdist"), ""))')
        if [ -z "$url" ]; then
            say "  $name $ver: no sdist published -- skipped (recorded, not assumed)"
            continue
        fi
        say "  $name $ver <- ${url##*/}"
        curl -fsSL --max-time 120 -o "$DIR/sdists/${url##*/}" "$url"
    done
    if [ "$DRY" = 0 ]; then
        for t in "$DIR"/sdists/*.tar.gz; do
            [ -e "$t" ] || continue
            tar -xzf "$t" -C "$DIR/upstream-docs" 2>/dev/null || true
        done
        say "  unpacked: $(ls "$DIR/upstream-docs" 2>/dev/null | wc -l) source trees"
        for d in "$DIR"/upstream-docs/*/docs; do
            [ -d "$d" ] && say "    docs: ${d#"$DIR"/upstream-docs/} ($(find "$d" -type f | wc -l) files)"
        done
    fi
fi

if ! skipped fixtures; then
    step "fixtures -- deterministic .ipynb, so the notebook cost is MEASURED"
    run mkdir -p "$DIR/fixtures"
    if [ "$DRY" = 0 ]; then
        "$PY" "$STAGE/jhub-make-fixtures.py" "$DIR/fixtures"
        ls -l "$DIR/fixtures" | sed 's/^/    /'
    else
        run "$PY" "$STAGE/jhub-make-fixtures.py" "$DIR/fixtures"
    fi
fi

if ! skipped vmbase; then
    step "vmbase -- reuse the tier-V image if it is already here"
    if [ -f "$TIERV/base-v2e.qcow2" ]; then
        say "  reusing $TIERV/base-v2e.qcow2 ($(du -h "$TIERV/base-v2e.qcow2" | cut -f1))"
        # CORRECTED: tier-v-vm.sh provisions the OVERLAY it creates
        # (`qemu-img create -b "$BASE" ... "$DISK"`), never the base. So this
        # image is a PRISTINE Debian 12 cloud image, which is what makes the
        # build-in-a-hub-image measurement meaningful -- it starts from nothing.
        say "  (a pristine Debian 12 cloud image -- tier-v-vm.sh provisions its overlay, not this)"
        [ "$DRY" = 0 ] && printf '%s\n' "$TIERV/base-v2e.qcow2" > "$DIR/vmbase.path"
    else
        run mkdir -p "$DIR"
        run curl -fL --progress-bar -o "$DIR/debian-12-genericcloud-amd64.qcow2" "$DEB_URL"
        [ "$DRY" = 0 ] && printf '%s\n' "$DIR/debian-12-genericcloud-amd64.qcow2" > "$DIR/vmbase.path"
    fi
fi

if ! skipped manifest; then
    step "manifest -- the evidence for the reproducibility claim"
    if [ "$DRY" = 0 ]; then
        {
            printf '# jichi x JupyterHub test bench -- staged artifacts\n\n'
            printf 'Produced by `jhub-prepare.sh` on %s by `%s`.\n\n' \
                   "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$(uname -srm)"
            printf 'Host interpreter: %s\n\n' "$(python3 -V 2>&1)"
            printf '## Versions actually installed (host venv)\n\n```\n'
            cat "$DIR/versions-host.txt" 2>/dev/null || printf '(venv stage skipped)\n'
            printf '```\n\n## Checksums\n\n```\n'
            # NOT `xargs -r`: BSD xargs has no such flag (and does not need
            # it -- it simply does not run the command on empty input). Test
            # the list first, which is portable and says what it means. The
            # sort is load-bearing: a manifest that reorders between runs is
            # not a reproducibility artifact.
            ( cd "$DIR" && find wheelhouse-host wheelhouse-guest sdists fixtures \
                 -type f 2>/dev/null | sort > "$DIR/.sumlist"
              if [ -s "$DIR/.sumlist" ]; then
                  xargs sha256sum < "$DIR/.sumlist"
              fi
              rm -f "$DIR/.sumlist" )
            printf '```\n'
        } > "$DIR/MANIFEST.md"
        say "  MANIFEST.md: $(wc -l < "$DIR/MANIFEST.md") lines, $(grep -c '  ' "$DIR/MANIFEST.md" || true) checksummed files"
    else
        run sh -c "write $DIR/MANIFEST.md"
    fi
fi

step "done"
say "artifacts: $DIR"
say "next:      sh $STAGE/jhub-verify.sh --dry-run"
