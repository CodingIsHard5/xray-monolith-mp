#!/usr/bin/env bash
# DRAFT - NOT RUN, NOT REGISTERED. s4d2 series driver (PREREG-s4d2-npc-pose-DRAFT.md sections 2.1, 3, 4, 6; requirement
# R2 of 1.4). Written 2026-11-27 at the desk; only `bash -n` has been run on it.
#   wt-render-split/perf-harness/s4d2/s4d2_run.sh probe   [--preflight-only]   one boot: s4p_probe (NPC = Duty walker)
#   wt-render-split/perf-harness/s4d2/s4d2_run.sh probe_b [--preflight-only]   the one allowed re-probe (prereg 2.1):
#        renames out/det_s4p_probe to out/det_s4p_probe_a, then boots s4p_probe again with the fallback NPC
#   wt-render-split/perf-harness/s4d2/s4d2_run.sh series  [--preflight-only]   the 16 scored boots (part A, part S)
# Made from tools/s4c_partA.sh (flag preflight, cfg0, shared claim) and sf1/sf1_run.sh (pins, display :141 rules, stop
# record, one way out), with det_run.sh as the per-boot runner (DET_NOCLAIM=1: this driver holds the claim).
#
# Rules this driver enforces (the brief of 2026-11-27 and prereg 6):
#   - refuses to start when `grep -q EXCLUSIVE ~/.claude/overseer/QUIET-REQUIRED` matches (and stops before a boot if
#     it starts to match);
#   - display :141 only (BENCH_DISPLAY must be unset or :141; display :0 is never named or touched); the display must be
#     free (no socket, no lock) before every boot;
#   - every pinned file is checked before the first boot, by name; the driver refuses while any PIN_ME_ placeholder
#     remains (exe names and hashes, the stimulus hash), and in series mode also while any REG_ME_ value (set at
#     registration from the probe: the NPC, the timeout, the scorer and self-test hashes) remains;
#   - stops on the first crash dump, timeout, protected title, failed pre-check or failed in-run check;
#   - at most ONE retry in the whole series, `<tag>_r1`, and only for a boot that exited early (det_run.sh rc 4) with no
#     crash dump; a second failure stops; a boot is never re-run because of what its output shows (prereg 6.5);
#   - after every boot: the witness run/appdata/logs/synth_<tag>.txt is moved into out/det_<tag>/ (det_run.sh does not:
#     its lines 44-47); a lever boot whose witness is missing stops BY NAME;
#   - all times from `date`; no `rm` anywhere in this file.
# Not scored here. Probe reading and scoring are run by hand, once each:
#   python3 -I wt-render-split/perf-harness/s4d2/s4d2_score.py mode=probe root=out out=wt-render-split/perf-harness/s4d2/probe_readings.txt
#   python3 -I wt-render-split/perf-harness/s4d2/s4d2_score.py root=out out=wt-render-split/perf-harness/s4d2/s4d2_score_result.txt
set -u
cd "$(dirname "$0")/../../.." || { echo "REFUSED: cannot enter the project root"; exit 2; }   # project root
T="$HOME/.claude/overseer/tools"; W=engineperf; QR="$HOME/.claude/overseer/QUIET-REQUIRED"
S4=wt-render-split/perf-harness/s4d2; ST=out/s4d2_state; LOGS=run/appdata/logs; CFG=run/appdata/user.ltx

# ---- pins (PIN_ME_: before ANY boot, the probe included; prereg 1.2, 1.4 items 3-5) ----
EXE2=PIN_ME_EXE2_NAME            # build 2 exe in run/bin (proposed name SimTick19.exe), c216081fd + the W1 commit
EXE2_SHA=PIN_ME_EXE              # its sha256 (64 hex)
EXE1=PIN_ME_EXE1_NAME            # build 1 exe in run/bin (s4d1's SimTick18.exe), 7b1232ffc or 6acb77044
EXE1_SHA=PIN_ME_EXE_B1           # its sha256
STOCK=Stance14.exe; STOCK_PFX=b4ce9c77                          # part A stock (COPIED, tools/s4c_partA.sh:23)
STIM=run/gamedata/scripts/zz_eperf_synth.script; STIM_SHA=PIN_ME_STIM   # installed stimulus (s4d1 R3; the harness copy
                                                                 # harness/zz_eperf_synth.script was d7dbc6c5... on 11-27)
USERDET=out/user_det.ltx.orig; USERDET_PFX=3e0e0a6f1fe8010c     # det profile (det_run.sh:28 copies it in)
DETRUN=tools/det_run.sh; DETRUN_SHA=01fb40dc2891d0602f3f596b8cde9664d05edd89eaa42a664cc9a34fbdc70f38   # read 2026-11-27
PIDLIB=tools/lib_engine_pids.sh; PIDLIB_SHA=cb183b7b894eab3edb200e2c03a3a96f2e935cd2d1e1f402f48be7aad238ed6e
NOGUARD=tools/run_noguard.sh; NOGUARD_SHA=92729abc420784053cee4b1b1832f5d112f74979a4760e2173d45578358455d5
# ---- registration values (REG_ME_: series mode only; prereg 2.3, 8) ----
NPC=REG_ME_PROBE_NPC             # the NPC the GO probe named (its pose line)
TIMEOUT=REG_ME_BENCH_TIMEOUT     # max(2000, ceil(5 x PROBE_BOOT_S / 60) x 60)
SCORER_SHA=REG_ME_SCORER_SHA     # sha256 of s4d2_score.py as registered (CONST filled)
SELFTEST_SHA=REG_ME_SELFTEST_SHA # sha256 of s4d2_selftest.sh as registered
# ---- fixed by the prereg ----
NPC_PROBE=sim_default_duty_428175; NPC_PROBE_B=esc_2_12_stalker_fanat; TIMEOUT_PROBE=2000
BASE="-det_passes 100,200,320 -det_lua_streams -det_counters"
REF="-det_passes 50,600,1650 -det_save -det_lua_streams -det_counters"   # the 11-19 counter method (s4c_partA.sh:10-16)
ALLOWED="-:50,600,1650 -:-det_save +:100,200,320"
NTOK_B2=138; NTOK_B1=58          # witness tokens per line: build 2 with W1+W2, build 1 (prereg 1.1)
DISP=:141

MODE=${1:-}; PRE=${2:-}
case "$MODE" in probe|probe_b|series) ;; *) echo "usage: $0 probe|probe_b|series [--preflight-only]"; exit 2 ;; esac
case "$PRE" in ''|--preflight-only) ;; *) echo "usage: $0 probe|probe_b|series [--preflight-only]"; exit 2 ;; esac

bad=0
refuse() { echo "REFUSED: $*"; bad=1; }
quiet_excl() { grep -q EXCLUSIVE "$QR" 2>/dev/null; }
quiet_excl && { echo "REFUSED: $QR names EXCLUSIVE"; exit 2; }
[ "${BENCH_DISPLAY:-:141}" = ":141" ] || { echo "REFUSED: BENCH_DISPLAY '${BENCH_DISPLAY}' is not :141"; exit 2; }
for v in EXE2 EXE2_SHA EXE1 EXE1_SHA STIM_SHA; do
  case "${!v}" in PIN_ME_*) refuse "$v is still the placeholder ${!v}" ;; esac
done
if [ "$MODE" = series ]; then
  for v in NPC TIMEOUT SCORER_SHA SELFTEST_SHA; do
    case "${!v}" in REG_ME_*) refuse "$v is still the placeholder ${!v} (set at registration)" ;; esac
  done
  case "$TIMEOUT" in ''|*[!0-9]*) refuse "TIMEOUT '$TIMEOUT' is not a number" ;; *) [ "$TIMEOUT" -ge 2000 ] || refuse "TIMEOUT $TIMEOUT < 2000 (prereg 6.3)" ;; esac
fi
[ "$bad" = 0 ] || exit 2

pin() {   # <file> <full sha256>: by name
  local f=$1 want=$2 got
  [ -e "$f" ] || { echo "REFUSED: missing file $f"; return 1; }
  got=$(sha256sum -- "$f" 2>/dev/null | cut -d' ' -f1)
  [ "$got" = "$want" ] || { echo "REFUSED: $f is not ${want:0:16} (it is ${got:0:16})"; return 1; }
}
pin_pfx() {   # <file> <hex prefix>: by name
  local f=$1 want=$2 got
  [ -e "$f" ] || { echo "REFUSED: missing file $f"; return 1; }
  got=$(sha256sum -- "$f" 2>/dev/null | cut -c1-${#want})
  [ "$got" = "$want" ] || { echo "REFUSED: $f is not $want... (it is $got...)"; return 1; }
}
cfgval() { awk -v k="$1" '$1==k{gsub("\r",""); v=$2} END{print v}' "$CFG" 2>/dev/null; }
cfg0() {   # tools/s4c_partA.sh:23: the settings file has both switches at 0
  [ -s "$CFG" ] || { echo "REFUSED: missing file $CFG"; return 1; }
  [ "$(cfgval sim_tick)" = 0 ] || { echo "REFUSED: sim_tick is '$(cfgval sim_tick)' in $CFG, not 0"; return 1; }
  [ "$(cfgval r__framelimit)" = 0 ] || { echo "REFUSED: r__framelimit is '$(cfgval r__framelimit)' in $CFG, not 0"; return 1; }
}
disp_free() {   # the display must not exist and must not be locked (sf1_run.sh)
  local n=${DISP#:}
  [ -e "/tmp/.X11-unix/X$n" ] && { echo "REFUSED: display $DISP exists (/tmp/.X11-unix/X$n)"; return 1; }
  [ -e "/tmp/.X$n-lock" ] && { echo "REFUSED: display $DISP is locked (/tmp/.X$n-lock)"; return 1; }
  return 0
}
flags_ok() {   # s4c_partA.sh:10-16: BASE differs from the 11-19 method exactly by ALLOWED
  local d
  d=$( { for w in $REF; do case " $BASE " in *" $w "*) ;; *) echo "-:$w";; esac; done
         for w in $BASE; do case " $REF " in *" $w "*) ;; *) echo "+:$w";; esac; done; } | tr '\n' ' ' )
  [ "$(echo $d)" = "$ALLOWED" ] || { echo "REFUSED: base flags differ from the 11-19 method in an unlisted way: [$d] allowed [$ALLOWED]"; return 1; }
}
pins_all() {   # every pin, by name; 0 = all good
  local f=0
  pin "$DETRUN" "$DETRUN_SHA" || f=1
  pin "$PIDLIB" "$PIDLIB_SHA" || f=1
  pin "$NOGUARD" "$NOGUARD_SHA" || f=1
  pin "run/bin/$EXE2" "$EXE2_SHA" || f=1
  pin "$STIM" "$STIM_SHA" || f=1
  pin_pfx "$USERDET" "$USERDET_PFX" || f=1
  [ -e out/user.ltx.orig ] || { echo "REFUSED: missing file out/user.ltx.orig (det_run.sh restores it)"; f=1; }
  if [ "$MODE" = series ]; then
    pin "run/bin/$EXE1" "$EXE1_SHA" || f=1
    pin_pfx "run/bin/$STOCK" "$STOCK_PFX" || f=1
    pin "$S4/s4d2_score.py" "$SCORER_SHA" || f=1
    pin "$S4/s4d2_selftest.sh" "$SELFTEST_SHA" || f=1
  fi
  return $f
}

# ---- the boots: tag|exe|K|mask|extra (prereg 3 and 4; order 6.1) ----
lever() {   # <K> <mask> <npc> <extra...>
  local k=$1 m=$2 npc=$3; shift 3
  echo "-det_synth_k $k -sim_4d_mask $m -det_synth_tw -det_synth_walk -det_synth_tracer -det_synth_pose -det_synth_pose_npc $npc${*:+ $*}"
}
plan() {   # prints the boot list of this mode, one "tag|exe|lever-or-empty" per line
  case "$MODE" in
    probe)   echo "s4p_probe|$EXE2|$(lever 3 23 $NPC_PROBE)" ;;
    probe_b) echo "s4p_probe|$EXE2|$(lever 3 23 $NPC_PROBE_B)" ;;
    series)
      echo "s4p_s1|$STOCK|"; echo "s4p_o1|$EXE2|"; echo "s4p_s2|$STOCK|"; echo "s4p_o2|$EXE2|"; echo "s4p_s3|$STOCK|"; echo "s4p_o3|$EXE2|"
      echo "s4p_off2a|$EXE2|$(lever 2 7 $NPC)"
      echo "s4p_on2a|$EXE2|$(lever 2 23 $NPC)"
      echo "s4p_b1off2|$EXE1|$(lever 2 7 $NPC)"
      echo "s4p_off3|$EXE2|$(lever 3 7 $NPC)"
      echo "s4p_on3|$EXE2|$(lever 3 23 $NPC)"
      echo "s4p_off2b|$EXE2|$(lever 2 7 $NPC)"
      echo "s4p_on2b|$EXE2|$(lever 2 23 $NPC)"
      echo "s4p_wc2|$EXE2|$(lever 2 23 $NPC -det_synth_wallclock)"
      echo "s4p_brk16|$EXE2|$(lever 2 23 $NPC -sim_4d_break 16)"
      echo "s4p_brk32|$EXE2|$(lever 2 23 $NPC -sim_4d_break 32)" ;;
  esac
}

# ---- preflight (nothing below this block boots anything until it passed) ----
flags_ok || bad=1
cfg0 || bad=1
pins_all || bad=1
disp_free || bad=1
SD=$ST/$MODE                     # this mode's state folder: out/s4d2_state/<probe|probe_b|series>, must be new
[ -e "$SD" ] && refuse "$SD exists (the state folder of $MODE must be new)"
if [ "$MODE" = series ]; then TIMEOUT_RUN=$TIMEOUT; else TIMEOUT_RUN=$TIMEOUT_PROBE; fi
if [ "$MODE" = probe ]; then
  [ -e out/det_s4p_probe ] && refuse "out/det_s4p_probe exists"
  [ -e out/det_s4p_probe_a ] && refuse "out/det_s4p_probe_a exists"
fi
if [ "$MODE" = probe_b ]; then
  [ -d out/det_s4p_probe ] || refuse "missing folder out/det_s4p_probe (the first probe)"
  [ -e out/det_s4p_probe_a ] && refuse "out/det_s4p_probe_a exists: the one re-probe was used"
fi
[ -e "$LOGS/synth_s4p_probe.txt" ] && refuse "$LOGS/synth_s4p_probe.txt exists (a probe witness was not moved)"
if [ "$MODE" = series ]; then
  [ -e "$S4/probe_readings.txt" ] || refuse "missing file $S4/probe_readings.txt (the probe reading, prereg 2.1)"
  tr -d '\r' < "$S4/probe_readings.txt" 2>/dev/null | grep -qx "S4D2 PROBE: GO" || refuse "$S4/probe_readings.txt does not end in GO"
  for t in $(plan | cut -d'|' -f1); do
    for f in out/det_$t out/det_${t}_r1 $LOGS/synth_$t.txt $LOGS/synth_${t}_r1.txt; do [ -e "$f" ] && refuse "$f exists"; done
  done
fi
[ "$bad" = 0 ] || exit 2
[ "$PRE" = "--preflight-only" ] && { echo "preflight ok ($MODE): $(plan | wc -l) boot(s)"; plan | cut -d'|' -f1,2 | tr '\n' ' '; echo; exit 0; }

# ---- state, log, one way out ----
mkdir -p "$ST" 2>/dev/null && mkdir "$SD" 2>/dev/null && : > "$SD/series.log" && : > "$SD/done.txt" || {
  echo "REFUSED: cannot create the state folder $SD"; exit 2; }
log() { echo "$(date '+%F %T') $*" | tee -a "$SD/series.log"; }
sc=""; stop=""; rc=0; CLAIMED=0; RETRIED=0; LIVE=""
halt() { sc=$1; stop=$2; rc=1; }
finish() { trap - EXIT TERM INT
  [ -n "$sc" ] || halt fault "driver exited without a recorded stop"
  printf 'class=%s%s reason=%s\n' "$sc" "${LIVE:+ interrupted=$LIVE}" "$(printf '%s' "${stop:-list finished}" | tr '\n\r' '  ')" > "$SD/stop.txt" \
    || echo "stop record NOT written ($SD/stop.txt)" >&2
  "$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
  [ "$CLAIMED" = 1 ] && { "$T/quiet-required.sh" release $W >/dev/null 2>&1 || log "release exit code $? (claim may have expired)"; }
  log "end ($MODE): $(grep -c . "$SD/done.txt") boot(s) done, retries $RETRIED of 1. $(cat "$SD/stop.txt" 2>/dev/null)"
  exit $rc; }
trap finish EXIT; trap 'halt box "cancelled${LIVE:+ during $LIVE}"; rc=143; finish' TERM INT

# ---- one SHARED claim for the whole run (prereg 7: SHARED; exit codes only) ----
WHAT="headless Xvfb GAMMA A-Life equivalence series s4d2 ($MODE, $(plan | wc -l) boots, display $DISP)"
"$T/quiet-required.sh" want $W >/dev/null 2>&1
CLAIM_MIN=$(( (TIMEOUT_RUN + 59) / 60 + 10 ))   # one boot's timeout + 10 min; renewed before every boot
until ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null && ! quiet_excl && \
      "$T/quiet-required.sh" claim $W "$CLAIM_MIN" "$WHAT" >/dev/null 2>&1; do sleep 60; done
CLAIMED=1
"$T/quiet-required.sh" withdraw $W >/dev/null 2>&1
log "claim granted ($MODE), load $(cut -d' ' -f1-3 /proc/loadavg), timeout per boot ${TIMEOUT_RUN}s"

# ---- per boot ----
gates() {   # <tag> <exe>: every pre-boot check; 1 = stop (halt set)
  local t=$1 e=$2 r
  quiet_excl && { halt box "$QR names EXCLUSIVE before $t"; return 1; }
  "$T/protected-title-check.sh" >/dev/null 2>&1 && { halt box "protected title running before $t"; return 1; }
  "$T/quiet-required.sh" renew $W "$CLAIM_MIN" "$WHAT" >/dev/null 2>&1 || { halt box "claim renewal refused before $t"; return 1; }
  for _ in 1 2 3 4 5 6 7 8 9 10; do disp_free >/dev/null 2>&1 && break; sleep 2; done
  r=$(disp_free) || { halt fault "$r (before $t)"; return 1; }
  r=$(cfg0) || { halt fault "$r (before $t)"; return 1; }
  r=$(pins_all) || { halt fault "pins before $t: $(echo "$r" | tr '\n' ' ')"; return 1; }
  [ -e "run/bin/$e" ] || { halt fault "missing file run/bin/$e (before $t)"; return 1; }
  [ -e "out/det_$t" ] && { halt fault "out/det_$t exists before its boot"; return 1; }
  [ -e "$LOGS/synth_$t.txt" ] && { halt fault "$LOGS/synth_$t.txt exists before its boot"; return 1; }
  return 0
}
after() {   # <tag> <lever>: move the witness, check what every boot must leave; 1 = stop (halt set)
  local t=$1 lv=$2 d=out/det_$1 w
  [ -d "$d" ] || { halt fault "missing folder $d after the boot"; return 1; }
  if [ -e "$LOGS/synth_$t.txt" ]; then
    mv -- "$LOGS/synth_$t.txt" "$d/" || { halt fault "cannot move $LOGS/synth_$t.txt into $d/"; return 1; }
    log "moved $LOGS/synth_$t.txt -> $d/"
  fi
  for f in "$d/xray.log" "$d/det_alife_counts_${t}_1.txt" "$d/det_alife_counts_${t}_3.txt" "$d/det_counters_${t}_3.txt"; do
    [ -s "$f" ] || { halt fault "missing file $f after $t"; return 1; }
  done
  [ -z "$lv" ] && return 0
  w=$d/synth_$t.txt
  [ -s "$w" ] || { halt fault "missing file $w (the witness of lever boot $t)"; return 1; }
  return 0
}
inrun_check() {   # <tag> <exe> <lever>: prereg 6.2 in-run preflight on s4p_off2a (and on the probe): the registered shape
  local t=$1 e=$2 lv=$3 d=out/det_$1 n want m
  want=$NTOK_B2; [ "$e" = "$EXE1" ] && want=$NTOK_B1
  n=$(tr -d '\r' < "$d/synth_$t.txt" | awk -v w="$want" 'NF && NF != w {b++} END {print b+0}')
  [ "$n" = 0 ] || { echo "witness $d/synth_$t.txt: $n lines without $want tokens"; return 1; }
  grep -a "command line" "$d/xray.log" | head -1 | tr -d '\r' | grep -qiF -- "$BASE" || { echo "engine command line lacks [$BASE]"; return 1; }
  grep -a "command line" "$d/xray.log" | head -1 | tr -d '\r' | grep -qiF -- "$lv" || { echo "engine command line lacks [$lv]"; return 1; }
  m=$(echo "$lv" | sed -n 's/.*-sim_4d_mask \([0-9]*\).*/\1/p')
  grep -aq "det_synth: k [0-9]* wallclock [01] tw 1 break [0-9]* mask $m" "$d/xray.log" || { echo "no det_synth line with mask $m in $d/xray.log"; return 1; }
  if [ "$e" != "$EXE1" ]; then
    grep -aq "det_synth: pose witness on, npc $(echo "$lv" | sed -n 's/.*-det_synth_pose_npc \([^ ]*\).*/\1/p')" "$d/xray.log" || { echo "no pose witness line naming the NPC in $d/xray.log"; return 1; }
  fi
  return 0
}
boot() {   # <tag> <exe> <lever>: one det_run.sh boot; returns det_run.sh's rc (and 9 for a crash dump)
  local t=$1 e=$2 lv=$3 r t0
  t0=$(date +%s)
  log "boot $t exe=$e extra=[$BASE${lv:+ $lv}] display=$DISP timeout=${TIMEOUT_RUN}s load $(cut -d' ' -f1-3 /proc/loadavg)"
  LIVE=$t
  DET_NOCLAIM=1 BENCH_DISPLAY=$DISP BENCH_TIMEOUT=$TIMEOUT_RUN DET_EXTRA="$BASE${lv:+ $lv}" \
    "$DETRUN" "$e" "$t" 60 3 > "$SD/run_$t.log" 2>&1; r=$?
  LIVE=""
  log "boot $t rc=$r wall=$(( $(date +%s) - t0 ))s: $(grep -aE '^rc=|TIMEOUT|ABORT|CRASH|REFUSED|exited early' "$SD/run_$t.log" | tr '\n' ' ')"
  { grep -aq "CRASH DUMP present" "$SD/run_$t.log" || ls out/det_"$t"/*.mdmp >/dev/null 2>&1; } && return 9
  return $r
}

plan > "$SD/plan.txt" || { halt fault "cannot write $SD/plan.txt"; finish; }
if [ "$MODE" = probe_b ]; then
  mv -- out/det_s4p_probe out/det_s4p_probe_a || { halt fault "cannot rename out/det_s4p_probe to out/det_s4p_probe_a"; finish; }
  log "renamed out/det_s4p_probe -> out/det_s4p_probe_a (prereg 2.1: the re-probe is read as det_s4p_probe)"
fi
while IFS='|' read -r tag exe lv; do
  t=$tag
  while :; do
    gates "$t" "$exe" || finish
    boot "$t" "$exe" "$lv"; brc=$?
    echo "$t" >> "$SD/done.txt" || { halt fault "cannot append to $SD/done.txt"; finish; }
    case $brc in
      0) ;;
      9) halt fault "crash dump after $t"; finish ;;
      5) halt fault "timeout of $t (${TIMEOUT_RUN}s)"; finish ;;
      3) halt box "protected title or no quiet slot during $t (det_run.sh rc 3)"; finish ;;
      4) if [ "$MODE" = series ] && [ "$RETRIED" = 0 ] && [ "$t" = "$tag" ]; then
           RETRIED=1; t=${tag}_r1; log "boot $tag exited early (rc 4, no crash dump): the one retry of the series, as $t"
           # the failed boot may have left a witness in the logs folder: keep it with its own folder
           [ -e "$LOGS/synth_$tag.txt" ] && [ -d "out/det_$tag" ] && mv -- "$LOGS/synth_$tag.txt" "out/det_$tag/"
           continue
         fi
         halt fault "boot $t exited early (rc 4)$([ "$RETRIED" = 1 ] && echo ', and the one retry of the series is used')"; finish ;;
      *) halt fault "boot $t ended rc=$brc (pre-check inside det_run.sh)"; finish ;;
    esac
    after "$t" "$lv" || finish
    if [ "$tag" = s4p_off2a ] || [ "$tag" = s4p_probe ]; then
      r=$(inrun_check "$t" "$exe" "$lv") || { halt fault "in-run preflight on $t failed: $r"; finish; }
      log "in-run preflight on $t ok"
    fi
    break
  done
done < "$SD/plan.txt"
sc=done; stop=""
log "all boots of $MODE done. Next (by hand, once): $([ "$MODE" = series ] && echo "python3 -I $S4/s4d2_score.py root=out out=$S4/s4d2_score_result.txt" || echo "python3 -I $S4/s4d2_score.py mode=probe root=out out=$S4/probe_readings.txt")"
finish
