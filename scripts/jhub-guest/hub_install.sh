#!/bin/sh
# runs as root: install JupyterHub in the guest.
#
# Debian 12's archive has NO jupyterhub (measured), so it comes from the cp311
# wheelhouse built on the host -- which is what that second wheelhouse is FOR.
# configurable-http-proxy is a node package and DOES come from the archive, via
# npm; the hub cannot route without it.
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get install -y -qq python3-venv npm >/dev/null 2>&1 || true

if [ -d /tmp/wheelhouse-guest ]; then
    python3 -m venv /opt/jhub >/dev/null 2>&1 || true
    /opt/jhub/bin/pip install -q --no-index --find-links=/tmp/wheelhouse-guest \
        jupyterhub jupyterlab > /tmp/pip.log 2>&1 || true
fi

# configurable-http-proxy: offline is not possible for an npm tree here, so this
# uses the guest's NAT. A truly air-gapped course server needs `npm pack` output
# staged the same way the wheelhouse is -- stated rather than pretended.
if ! command -v configurable-http-proxy >/dev/null 2>&1; then
    npm install -g configurable-http-proxy > /tmp/npm.log 2>&1 || true
fi

printf 'jupyterhub: '; /opt/jhub/bin/jupyterhub --version 2>/dev/null || echo none
printf 'chp: ';        command -v configurable-http-proxy || echo none
