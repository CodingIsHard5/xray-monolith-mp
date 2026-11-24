#!/usr/bin/env bash
# T3 pre-flight E (prereg section 46): three X-only boots, each its own WANT slice (tools/t2_preflight.sh), in this
# order: god_off_norefill (witness), god_off (witness), prot. Stops after a boot whose t2_xray rc is not 0.
set -u
cd "$(dirname "$0")/.."
for md in god_off_norefill god_off prot; do
  O=out/t3pfE_$md; [ -e "$O" ] && { echo "$O exists, not overwriting"; exit 2; }
  PF_OUT=$O PF_DMG=$md PF_EXE=Stance14.exe PF_PAUSE=0 PF_KILL_AT=99999 PF_RUN_S=330 PF_ABSORB=0 PF_POSE_ARG=nofreeze tools/t2_preflight.sh
  grep -q 't2_xray rc=0' "$O/preflight.log" || { echo "boot $md: t2_xray rc not 0, stopping"; exit 1; }
  echo "boot $md done"
done
