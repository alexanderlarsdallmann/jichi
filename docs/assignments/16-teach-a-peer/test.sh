#!/bin/sh
# The meta-grader for the authored assignment: structure, then the same
# two-sidedness bar every grader in this repository is held to -- the
# check must reject the task as shipped and accept the reference solution.
# The solve half runs on a throwaway COPY of task/, so grading never
# mutates the shipped task and re-grading stays honest.
cd "$(dirname "$0")" || exit 1

echo "1..5"
rc=0
if [ ! -f task/spec.md ]; then
    echo "not ok 1 - task/spec.md is missing"
    exit 1
fi
ok=1
for key in "title:" "audience:" "points:" "verify:"; do
    grep -q "^$key" task/spec.md || { ok=0; missing="$key"; }
done
if [ "$ok" = 1 ]; then
    echo "ok 1 - the spec's frontmatter is complete"
else
    echo "not ok 1 - spec frontmatter is missing '$missing'"
    rc=1
fi
if grep -q '^hints:' task/spec.md && \
   [ "$(grep -c '^  - ' task/spec.md)" -ge 2 ]; then
    echo "ok 2 - a hint ladder with at least two rungs"
else
    echo "not ok 2 - no hints: ladder with >= 2 rungs"
    rc=1
fi
if grep -q '^## Rubric' task/spec.md; then
    echo "ok 3 - the body carries a rubric"
else
    echo "not ok 3 - no '## Rubric' section in the spec body"
    rc=1
fi
if [ ! -f task/check.sh ] || [ ! -f task/solution.sh ]; then
    echo "not ok 4 - task/check.sh or task/solution.sh is missing"
    exit 1
fi
rm -rf _meta
cp -r task _meta
sh _meta/check.sh >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "ok 4 - the check rejects the task as shipped (unsolved)"
else
    echo "not ok 4 - the check passes on the unsolved task: hollow"
    rc=1
fi
sh _meta/solution.sh >/dev/null 2>&1 && sh _meta/check.sh >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "ok 5 - the check accepts the reference solution"
else
    echo "not ok 5 - the reference solution does not satisfy the check"
    rc=1
fi
rm -rf _meta
exit $rc
