#!/usr/bin/env bash
# Offline tests of the kill channel script: every check on the REAL script must PASS, every mutant must make its
# check FAIL. Each run gets a fresh directory runs/r-<time>-<pid>/ beside this file (nothing is deleted).
#   bash kill4_test.sh [script] [lost3 script]
# Exit 0 only if all checks pass and all mutants are caught.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SCRIPT="${1:-$ROOT/harness/zz_p1_puppet_kill4.script}"
LOST3="${2:-$ROOT/harness/zz_p1_puppet_lost3.script}"
STUB="$HERE/kill4_stub.lua"
LUA=/usr/bin/lua5.1
bad=0
for f in "$SCRIPT" "$LOST3" "$STUB" "$LUA"; do
	if [ ! -f "$f" ]; then echo "FAIL missing file: $f"; bad=1; fi
done
[ "$bad" = 0 ] || { echo "RESULT: FAIL (missing files)"; exit 1; }
RUN="$HERE/runs/r-$(date +%Y%m%d-%H%M%S)-$$"
mkdir -p "$RUN" || { echo "FAIL cannot make $RUN"; exit 1; }
echo "script $(sha256sum "$SCRIPT" | cut -c1-16)  lost3 $(sha256sum "$LOST3" | cut -c1-16)  run dir $RUN"

prep() {   # fresh work dir with the subdirectories the stub wants
	local d="$1"
	mkdir -p "$d/a/v2" "$d/a/v3" "$d/b/v2" "$d/b/v3" "$d/c" || return 1
	: > "$d/a/.keep"; : > "$d/b/.keep"; : > "$d/c/.keep"
}

checks="$("$LUA" "$STUB" checks)" || { echo "FAIL stub does not list its checks"; exit 1; }
[ -n "$checks" ] || { echo "FAIL no checks listed"; exit 1; }
npass=0; nfail=0
echo "== checks on the real script"
for c in $checks; do
	d="$RUN/real-$c"; prep "$d" || { echo "FAIL $c: cannot prepare $d"; nfail=$((nfail + 1)); continue; }
	out="$("$LUA" "$STUB" run "$SCRIPT" "$c" "$d" "$LOST3" 2>&1)"; rc=$?
	if [ $rc -eq 0 ] && [ "$out" = "PASS $c" ]; then echo "PASS $c"; npass=$((npass + 1))
	else case "$out" in "ERROR $c: "*) echo "ERROR $c (rc=$rc): ${out#ERROR $c: }" ;; *) echo "FAIL $c (rc=$rc): $out" ;; esac; nfail=$((nfail + 1)); fi
done

# the mutant manifest, captured on its own: a stub that cannot list it stops the run (as the check list above)
mutants="$("$LUA" "$STUB" mutants)" || { echo "FAIL stub does not list its mutants"; exit 1; }
[ -n "$mutants" ] || { echo "FAIL no mutants listed"; exit 1; }
echo "== mutants (each must FAIL its check; an ERROR, a Lua exception or a setup failure, is never a catch)"
ncaught=0; nmissed=0
declare -A guarded
while read -r mu c; do
	[ -n "$mu" ] || continue
	guarded[$c]=1
	d="$RUN/mut-$mu"; prep "$d" || { echo "NOT CAUGHT $mu: cannot prepare $d"; nmissed=$((nmissed + 1)); continue; }
	if ! mk="$("$LUA" "$STUB" mkmutant "$mu" "$SCRIPT" "$d/mutant.script" 2>&1)"; then
		echo "NOT CAUGHT $mu -> $c: patch did not apply ($mk)"; nmissed=$((nmissed + 1)); continue; fi
	if [ ! -f "$d/mutant.script" ] || cmp -s "$SCRIPT" "$d/mutant.script"; then
		echo "NOT CAUGHT $mu -> $c: mutant file missing or identical"; nmissed=$((nmissed + 1)); continue; fi
	out="$("$LUA" "$STUB" run "$d/mutant.script" "$c" "$d" "$LOST3" 2>&1)"; rc=$?
	case "$out" in
		"FAIL $c: "*) if [ $rc -ne 0 ]; then echo "CAUGHT $mu by $c: ${out#FAIL $c: }"; ncaught=$((ncaught + 1))
			else echo "NOT CAUGHT $mu -> $c: FAIL printed but rc=0"; nmissed=$((nmissed + 1)); fi ;;
		"ERROR $c: "*) echo "NOT CAUGHT $mu -> $c: ERROR, not an outcome failure (${out#ERROR $c: })"; nmissed=$((nmissed + 1)) ;;
		*) echo "NOT CAUGHT $mu -> $c (rc=$rc): $out"; nmissed=$((nmissed + 1)) ;;
	esac
done <<< "$mutants"
for c in $checks; do [ -n "${guarded[$c]:-}" ] || { echo "NO MUTANT for check $c"; nmissed=$((nmissed + 1)); }; done

echo "== checks: $npass pass, $nfail fail; mutants: $ncaught caught, $nmissed not caught or missing"
if [ $nfail -eq 0 ] && [ $nmissed -eq 0 ]; then echo "RESULT: PASS"; exit 0; else echo "RESULT: FAIL"; exit 1; fi
