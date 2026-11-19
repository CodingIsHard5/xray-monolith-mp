#!/usr/bin/env bash
# Run several equivalence-harness runs under ONE quiet claim (so the slot is not lost between runs), then diff
# every run against the first.  usage: tools/det_pair.sh <interval_s> <checkpoints> <exe>:<tag> <exe>:<tag> ...
set -u
P="$(cd "$(dirname "$0")/.." && pwd)"; T="$HOME/.claude/overseer/tools"; W=engineperf
INT="$1"; N="$2"; shift 2
if "$T/protected-title-check.sh" >/dev/null 2>&1; then echo "REFUSED: protected title running"; exit 3; fi
"$T/quiet-required.sh" claim $W $(( $# * (${DET_TIMEOUT:-480} / 60 + 1) + 5 )) "headless Xvfb GAMMA A-Life equivalence series ($# runs)" > "$P/out/det_pair_claim.txt" 2>&1 \
  || { echo "REFUSED: no quiet slot ($(tail -1 "$P/out/det_pair_claim.txt"))"; exit 3; }
trap '"$T/quiet-required.sh" release $W >/dev/null 2>&1' EXIT
first=""
for pair in "$@"; do
  exe="${pair%%:*}"; tag="${pair##*:}"; tag="${tag,,}"
  DET_NOCLAIM=1 BENCH_TIMEOUT="${DET_TIMEOUT:-480}" "$P/tools/det_run.sh" "$exe" "$tag" "$INT" "$N" 2>&1 | tail -1 || true
  if [ -z "$first" ]; then first="$tag"; else echo "--- $first vs $tag"; python3 "$P/tools/det_diff.py" "$P/out/det_$first" "$P/out/det_$tag" | cut -c1-230; fi
done
