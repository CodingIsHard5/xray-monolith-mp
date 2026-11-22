#!/bin/bash
# Self-test for statistic C (tools/tier_b.py invariants/stat_C, PREREG-tierB-statC).
# Fixture: a stock-like tb_extra (CRLF) and a copy where ONE squad member's smart is 65535 (a lost registration).
# PASS: C(stock, stock) = 0; C(stock, lost) = 1 with only I5 moved. Witness W4: I5 check removed -> C = 0 (red).
cd "$(dirname "$0")/../.." || exit 2
F=$(mktemp -d); trap 'rm -rf "$F"' EXIT
mk() { mkdir -p $F/$1; { printf 'info passes 100\r\n'
  printf 'sq\t10\tsquad_a\tl01_escape\t5\t500\t2\r\n'
  printf 'cr\t11\tst\tstalker\t1\tl01_escape\t5\t10\t500\r\n'
  printf 'cr\t12\tst\tstalker\t1\tl01_escape\t5\t10\t%s\r\n' "$2"
  printf 'sm\t500\tsmart_a\tl01_escape\t2\r\n'; } > $F/$1/tb_extra_$1_3.txt; printf '#actor\r\n' > $F/$1/alife_dump_$1_3.txt; }
mk ok 500; mk lost 65535
r=$(python3 -I - "$PWD/tools" "$F" <<'PY'
import sys; sys.path.insert(0, sys.argv[1]); import tier_b
a, b = tier_b.Run(sys.argv[2] + "/ok"), tier_b.Run(sys.argv[2] + "/lost")
print(tier_b.invariants(a, 3), tier_b.invariants(b, 3), tier_b.stat_C(a, a, 3), tier_b.stat_C(a, b, 3))
src = open(sys.argv[1] + "/tier_b.py").read().replace('i[4] += c["smart"] != q["smart"]', 'pass')
ns = {"__name__": "w4"}; exec(compile(src, "w4", "exec"), ns)
print(ns["stat_C"](ns["Run"](sys.argv[2] + "/ok"), ns["Run"](sys.argv[2] + "/lost"), 3))
PY
)
echo "$r"; fail=0
grep -q '^\[0, 0, 0, 0, 0, 0\] \[0, 0, 0, 0, 1, 0\] 0 1$' <<<"$(head -1 <<<"$r")" && echo "PASS C sees one lost registration (I5 only)" || { echo "FAIL C"; fail=1; }
[ "$(tail -1 <<<"$r")" = 0 ] && echo "PASS witness W4 (I5 removed) blind -> red" || { echo "FAIL witness W4"; fail=1; }
exit $fail
