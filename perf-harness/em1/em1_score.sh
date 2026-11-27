#!/usr/bin/env bash
# em1 X side: score ONCE (registration section 7). Desk only: reads the run folder, starts no engine.
#   bash em1_score.sh <run dir> <report file>        (writes <report file> and <report file>.rc, nothing else)
# 1. The checker beside this file must hash to CHK_SHA (the registered checker). Mismatch: REFUSED, exit 2, no file made.
# 2. The report is opened no-clobber (set -C) BEFORE the checker runs. An existing report, or an existing .rc with no
#    report: REFUSED, exit 2, nothing written (never a status for a report this call did not write).
# 3. The checker runs with stdout into the open report; its exit code goes to <report>.rc (no-clobber) only after it ran.
# Exit: the checker's (0 PASS, 1 FAIL, 3 NO VERDICT, 2 REFUSED), or 2 when this script refuses.
set -u
CHK_SHA=5f8174aa6c3e238527766f8977e29cad3758bd399f806602aa4c3f5071622553
K="$(cd "$(dirname "$0")" && pwd)"; CHK="$K/em1_check.py"
[ $# -eq 2 ] && [ -n "$1" ] && [ -n "$2" ] || { echo "usage: em1_score.sh <run dir> <report file>" >&2; exit 2; }
R=$1; REP=$2
got=$(sha256sum < "$CHK" 2>/dev/null | cut -c1-64)
[ "$got" = "$CHK_SHA" ] || { echo "REFUSED: $CHK is not the registered checker ${CHK_SHA:0:8} (it is ${got:0:8}); nothing written" >&2; exit 2; }
[ -e "$REP.rc" ] && { echo "REFUSED: $REP.rc exists; nothing written" >&2; exit 2; }
set -C
{ exec 3> "$REP"; } 2>/dev/null || { echo "REFUSED: $REP exists or cannot be made; nothing written" >&2; exit 2; }
python3 -I "$CHK" "$R" >&3; rc=$?
exec 3>&-
{ echo "$rc" > "$REP.rc"; } 2>/dev/null || { echo "REFUSED: $REP.rc could not be written (rc $rc)" >&2; exit 2; }
echo "scored: $REP (rc $rc)"
exit "$rc"
