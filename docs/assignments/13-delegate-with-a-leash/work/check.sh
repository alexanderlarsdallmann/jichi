#!/bin/sh
# The INNER task's verify: the agent's bounded run must make this pass.
cd "$(dirname "$0")" || exit 1
grep -qx 'delegation with verification' report.txt 2>/dev/null
