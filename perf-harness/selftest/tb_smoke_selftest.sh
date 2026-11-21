#!/bin/bash
# Self-test for smoke_ok in tools/tb_slice.sh. Fixture lines are CRLF, as the engine under wine writes them.
# PASS case: a filled CRLF fixture must be green. Witnesses that must go red:
#   W1 the CR strip removed (must fail with "integer expression expected", the 2026-11-21 01:03 defect)
#   W2 one squad with level "-" (must fail: squads_without_level=1)
cd "$(dirname "$0")/../.." || exit 2
F=$(mktemp -d); trap 'rm -rf "$F"' EXIT
mk() { { printf 'info passes 382\r\n'
  for i in $(seq 1 60); do printf 'sq\t%d\tsquad_x\t%s\t1\tsmart\t1\r\n' $i "${1:-l01_escape}"; done
  for i in $(seq 1 60); do printf 'cr\t%d\tst\tstalker\t1\tl01_escape\t1\t-\t-\r\n' $i; printf 'cr\t%d\tmo\tmonster\t1\tl01_escape\t1\t-\t-\r\n' $((i+100)); done
  for i in $(seq 1 60); do printf 'sm\t%d\tsmart_%d\tl01_escape\t1\r\n' $i $i; done; }; }
mk > $F/good.txt; { mk; } | sed '2s/\tl01_escape\t/\t-\t/' > $F/nolevel.txt; printf 'x\r\n#actor\r\n' > $F/dump.txt
grep -q $'\r' $F/good.txt || { echo "FIXTURE BROKEN: no CR"; exit 2; }
get() { sed -n '/^# smoke_ok/,/^  \[ "\${passes/p' "$1"; }
fail=0
r=$(bash -c "$(get tools/tb_slice.sh)"$'\n'"smoke_ok $F/good.txt $F/dump.txt" 2>&1) && echo "PASS good CRLF fixture: $r" || { echo "FAIL good CRLF fixture: $r"; fail=1; }
r=$(bash -c "$(get tools/tb_slice.sh | sed "s/tr -d '\\\\r' < \"\$raw\" > \"\$x\"/cat \"\$raw\" > \"\$x\"/")"$'\n'"smoke_ok $F/good.txt $F/dump.txt" 2>&1)
if [ $? -ne 0 ] && grep -q 'integer expression expected' <<<"$r"; then echo "PASS witness W1 (strip removed) red: ${r##*: }"; else echo "FAIL witness W1 not red as expected: $r"; fail=1; fi
r=$(bash -c "$(get tools/tb_slice.sh)"$'\n'"smoke_ok $F/nolevel.txt $F/dump.txt" 2>&1)
if [ $? -ne 0 ] && grep -q 'squads_without_level=1' <<<"$r"; then echo "PASS witness W2 (squad without level) red"; else echo "FAIL witness W2: $r"; fail=1; fi
python3 -I - "$F/good.txt" "$PWD/tools" <<'PY' || fail=1
import sys; sys.path.insert(0, sys.argv[2]); import tier_b
e = tier_b.parse_extra(sys.argv[1])
ok = e["info"]["passes"] == "382" and len(e["sq"]) == 60 and all(v["npcs"] == "1" for v in e["sq"].values()) and all(v["smart"] == "-" for v in e["cr"].values())
print(("PASS" if ok else "FAIL") + " tier_b.parse_extra on CRLF fixture (last fields free of CR)"); sys.exit(0 if ok else 1)
PY
exit $fail
