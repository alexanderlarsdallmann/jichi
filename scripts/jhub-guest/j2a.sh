#!/bin/sh
# runs as root: STAGE THE COLLISION.
#
# stud1 starts a run that stalls inside the model call, so it sits there holding
# its lease.  While it is held, stud2 starts a run on the SAME tree with
# `--lease fail` -- the strictest setting there is.  If the lease arbitrated
# between users, stud2 would refuse to start.
#
# Both runs are launched from FILES, not from nested `sudo sh -c "su -c \"...\""`
# quoting.  tier-v-vm.sh states the rule ("gr wraps in single quotes, so its
# argument must not contain one") and this script exists because ignoring it
# produced three checks that failed for quoting reasons and looked like findings.
set -eu

su - stud1 -c 'cd /srv/shared && nohup timeout 150 jichi \
  --config /home/stud1/config.json --auto -q -p "HOLD_THE_LEASE" \
  --edit-scope "a-*" --lease fail >/tmp/stud1.out 2>&1 &' || true

sleep 15

echo "--- leases while stud1 runs ---"
echo "stud1: $(ls /home/stud1/.jichi.d/leases/ 2>/dev/null | tr '\n' ' ')"
echo "stud2: $(ls /home/stud2/.jichi.d/leases/ 2>/dev/null | tr '\n' ' ')"
echo "stud1 alive: $(pgrep -u stud1 -c -f jichi 2>/dev/null || echo 0)"

echo "--- stud2 now runs on the SAME tree with --lease fail ---"
su - stud2 -c 'cd /srv/shared && timeout 90 jichi \
  --config /home/stud2/config.json --auto -q -p "write the file" \
  --edit-scope "a-*" --lease fail >/tmp/stud2.out 2>&1' || true
echo "stud2 exit recorded"

echo "--- leases with BOTH involved ---"
echo "stud1: $(ls /home/stud1/.jichi.d/leases/ 2>/dev/null | tr '\n' ' ')"
echo "stud2: $(ls /home/stud2/.jichi.d/leases/ 2>/dev/null | tr '\n' ' ')"
echo "--- checkpoints ---"
echo "stud1: $(ls /home/stud1/.jichi.d/checkpoints/ 2>/dev/null | tr '\n' ' ')"
echo "stud2: $(ls /home/stud2/.jichi.d/checkpoints/ 2>/dev/null | tr '\n' ' ')"
echo "--- stud2 output ---"
tail -5 /tmp/stud2.out 2>/dev/null || true
pkill -u stud1 -f jichi 2>/dev/null || true
echo "J2A_DONE"
