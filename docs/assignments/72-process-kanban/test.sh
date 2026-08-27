#!/bin/sh
# Structural floor for a kanban board: Todo / Doing / Done columns, a WIP limit
# declared, Doing not overflowing it (<= 3), and every Doing card tracing to a
# requirement id (R<n>). Whether the board is HONEST is your judgment; the
# structure and the WIP discipline a script can check.
cd "$(dirname "$0")" || exit 1
grep -qE '^##+ *Todo'  BOARD.md || { echo "FAIL: no Todo column"; exit 1; }
grep -qE '^##+ *Doing' BOARD.md || { echo "FAIL: no Doing column"; exit 1; }
grep -qE '^##+ *Done'  BOARD.md || { echo "FAIL: no Done column"; exit 1; }
grep -qiE 'wip' BOARD.md || { echo "FAIL: no WIP limit declared -- state a limit on Doing"; exit 1; }
doing=$(awk '/^##+ *Doing/{f=1;next} /^##+ /{f=0} f' BOARD.md)
nd=$(printf '%s\n' "$doing" | grep -cE '^[-*] ')
[ "$nd" -le 3 ] || { echo "FAIL: $nd cards in Doing -- the WIP limit is blown; finish before you start"; exit 1; }
[ "$nd" -ge 1 ] || { echo "FAIL: nothing in Doing"; exit 1; }
untraced=$(printf '%s\n' "$doing" | grep -E '^[-*] ' | grep -cvE '(^|[^A-Za-z0-9_])R[0-9]+([^A-Za-z0-9_]|$)')
[ "$untraced" -eq 0 ] || { echo "FAIL: a Doing card does not trace to a requirement (R<n>) -- work should serve the goal"; exit 1; }
echo "PASS: valid columns, WIP respected ($nd in Doing), every card traces to a requirement"
