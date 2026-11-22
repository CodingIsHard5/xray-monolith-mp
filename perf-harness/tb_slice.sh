#!/bin/bash
# tb_slice.sh -- overnight scheduler for the tier (b) CONTROL runs (PREREG-tierB-controls-2026-11-20, approved by the
# Overseer). Works through tools/tb_order.txt, one run at a time, in slices:
#   * wall-clock gate: a slice opens only between 01:00 and 08:00 local time;
#   * EXCLUSIVE claim through the WANT queue, held for one slice of at most 3 h, then released and re-queued;
#   * a run starts only if 22 min are left in the slice and before 08:00;
#   * protected-title and Roblox checks before every run (either one up = the slice ends);
#   * after the grant, waits up to 10 min inside the claim for the 1-min load to fall under 2;
#   * smoke run first; if its new dump fields are not filled, everything stops (prereg section 7);
#   * any run with rc != 0 ends the slice and is reported (no workaround); a DISTURBED run is re-queued once;
#   * notifies the Overseer at the first boot, at each slice end with the run count, and on any stop.
# State: out/tb_state/done.txt (tags finished), out/tb_state/slice.log. Gives up after TB_NIGHTS nights (default 4).
set -u; P=~/Projects/StalkerGameEngine; cd "$P"; T="$HOME/.claude/overseer/tools"; W=engineperf
EXE="${TB_EXE:-RefDet14.exe}"; SAVE=bench1; ST=${TB_STATE:-out/tb_state}; ORDER=${TB_ORDER:-tools/tb_order.txt}; mkdir -p $ST; touch $ST/done.txt $ST/rerun.txt
WHAT="EXCLUSIVE: headless tier (b) control runs (one engine at a time, lowest priority)"
END_ALL=$(( $(date +%s) + ${TB_NIGHTS:-4} * 86400 ))
log() { echo "$(date '+%F %T') $*" | tee -a $ST/slice.log; }
note() { timeout 60 "$T/notify-overseer.sh" $W - <<<"engineperf: $1" >/dev/null 2>&1; }
# TB_END_H (default 8): hour the nightly window closes. TB_ONE_SLICE=1: leave after one slice (or one failed attempt).
END_H="${TB_END_H:-8}"
# TB_ANYTIME=1 (Overseer 2026-11-21, Caden away 6 days): no wall-clock window; slices at any hour.
in_window() { [ "${TB_ANYTIME:-0}" = 1 ] && return 0; local h; h=$(date +%-H); [ "$h" -ge 1 ] && [ "$h" -lt "$END_H" ]; }
secs_to_8() { [ "${TB_ANYTIME:-0}" = 1 ] && { echo 999999; return; }; echo $(( $(date -d "today $END_H:00" +%s) - $(date +%s) )); }
# yield_gap (Overseer ruling 2026-11-21): after a slice, WANT again only when (a) TB_GAP s (default 1500) have
# passed, or (b) another worker has CLAIMED and since RELEASED (audit log, after our release) AND nobody other than
# us has been refused a claim in the last 5 min. TB_AUDIT_LOG exists for the self-test.
yield_gap() { local t0 since el cut who AL="${TB_AUDIT_LOG:-$HOME/.claude/overseer/quiet-audit.log}"
  t0=$(date +%s); since=$(date -d @$t0 '+%F %T')
  while :; do el=$(( $(date +%s) - t0 ))
    [ $el -ge "${TB_GAP:-1500}" ] && { log "gap: ${TB_GAP:-1500}s passed"; return; }
    who=$(tail -n 5000 "$AL" | awk -v s="$since" 'substr($0,1,19)>=s && $3=="CLAIM" && $4!="by=engineperf"{c[$4]=1} substr($0,1,19)>=s && $3=="RELEASE" && ($4 in c){w=$4} END{print w}')
    if [ -n "$who" ]; then cut=$(date -d "-300 sec" '+%F %T')
      if ! tail -n 5000 "$AL" | awk -v s="$cut" 'substr($0,1,19)>=s && /refused/ && $4!="by=engineperf"{f=1} END{exit !f}'; then
        log "gap: ${who#by=} claimed and released, nobody refused in the last 5 min; after ${el}s"; return; fi; fi
    sleep 30; done; }
pending() { { cat $ST/rerun.txt; grep -v '^#' $ORDER; } | while IFS='|' read -r tag env extra; do [ -n "$tag" ] && ! grep -qx "$tag" $ST/done.txt && echo "$tag|$env|$extra"; done; }
# smoke_ok [extra-file [dump-file]]: the engine (wine) writes CRLF; every field is read from a CR-stripped copy ($x).
smoke_ok() { local raw=${1:-out/tb_smoke/tb_extra_smoke_3.txt} dump=${2:-out/tb_smoke/alife_dump_smoke_3.txt} x
  [ -s "$dump" ] && [ -s "$raw" ] || { echo "smoke: dump 3 missing"; return 1; }
  x=$(mktemp); tr -d '\r' < "$raw" > "$x"
  local passes sq sqbad st mo sm; passes=$(awk '$1=="info" && $2=="passes"{print $3}' $x); sq=$(grep -c '^sq' $x); sqbad=$(awk -F'\t' '$1=="sq" && ($4=="-"||$4=="nil"||$4=="")' $x | wc -l)
  st=$(awk -F'\t' '$1=="cr" && $4=="stalker"' $x | wc -l); mo=$(awk -F'\t' '$1=="cr" && $4=="monster"' $x | wc -l); sm=$(grep -c '^sm' $x); rm -f "$x"
  echo "smoke: passes=$passes squads=$sq squads_without_level=$sqbad stalkers=$st monsters=$mo smarts=$sm"
  [ "${passes:-0}" -gt 0 ] && [ "$sq" -gt 50 ] && [ "$sqbad" -eq 0 ] && [ "$st" -gt 50 ] && [ "$mo" -gt 50 ] && [ "$sm" -gt 50 ]; }
# settle: before EVERY run wait until the 1-minute load < 2 AND no other heavy process (Overseer ruling 2026-11-21).
# Cap TB_SETTLE_MAX s (default 300, checked every 15 s); past the cap the run starts anyway and tb_run.sh marks it DISTURBED as before.
# Prints "settle wait=<s>s load=<l1> heavy=[...] <ok|cap>". TB_LOADAVG_FILE / TB_HEAVY_CMD / TB_SETTLE_POLL exist for the self-test.
heavy_ps() { ps -eo pcpu,pid,comm,args --sort=-pcpu | awk 'NR>1 && $1>=100 && $3!~/wineserver|Xvfb/ {print $1"%:"$3}' | head -5 | tr '\n' ' '; }
settle() { local t0 l1 hv; t0=$(date +%s)
  while :; do l1=$(cut -d' ' -f1 "${TB_LOADAVG_FILE:-/proc/loadavg}"); hv=$(${TB_HEAVY_CMD:-heavy_ps})
    awk -v l="$l1" 'BEGIN{exit !(l<2)}' && [ -z "${hv// /}" ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[] ok"; return 0; }
    [ $(( $(date +%s) - t0 )) -ge "${TB_SETTLE_MAX:-300}" ] && { echo "settle wait=$(( $(date +%s) - t0 ))s load=$l1 heavy=[$hv] cap"; return 1; }
    sleep "${TB_SETTLE_POLL:-15}"; done; }
# pending() lists the _rr re-runs before the rest of the order (Overseer 2026-11-22: re-runs are the data).
# TB_WALK_TIMEOUT (default 1500 s): run cap for the walk smoke only; all other runs keep TB_TIMEOUT.
# smokec_ok: statistic C smoke (PREREG-tierB-statC section 6): the smoke fields as before, plus I5 >= 50 at the last
# dump and the "tb_lostreg: done n=100" line in the engine log.
smokec_ok() { local d=${1:-out/tb_smokec} t=${2:-smokec} x r i5; x=$d/tb_extra_${t}_3.txt
  r=$(smoke_ok "$x" $d/alife_dump_${t}_3.txt) || { echo "$r"; return 1; }
  i5=$(awk -f engine/perf-harness/tb_invariants.awk "$x" | grep -o 'I5 smart=[0-9]*' | cut -d= -f2)
  grep -qa 'tb_lostreg: done n=100' $d/xray.log || { echo "$r I5=$i5; no 'tb_lostreg: done n=100' line"; return 1; }
  echo "$r I5=$i5"; [ "${i5:-0}" -ge 50 ]; }
FIRST=1; total_done=0; HARD=0
[ "${TB_GAP_FIRST:-0}" = 1 ] && yield_gap        # restarted right after a slice: honour the gap first
while [ -n "$(pending)" ]; do
  [ $(date +%s) -ge $END_ALL ] && { log "GIVE UP: nights used up, $(pending | wc -l) runs left"; note "tier (b) controls: scheduler GAVE UP after ${TB_NIGHTS:-4} nights, $(pending | wc -l) runs left."; exit 5; }
  until in_window && [ $(secs_to_8) -gt 1500 ]; do sleep 120; done          # wall-clock gate
  # WANT only if we do not already hold the box (a restart while holding queued a WANT that then sat in the queue
  # for the whole slice and handed the box back to us at release: 2026-11-22 07:46-10:33, Overseer fairness ruling)
  "$T/quiet-required.sh" check 2>/dev/null | awk -v w=$W '$1=="HELD:" && $4==w{f=1} END{exit !f}' || "$T/quiet-required.sh" want $W EXCLUSIVE >/dev/null 2>&1; ok=0
  while in_window && [ $(secs_to_8) -gt 1500 ]; do
    if ! "$T/protected-title-check.sh" >/dev/null 2>&1 && ! pgrep -x sober >/dev/null; then
      "$T/quiet-required.sh" claim $W 30 "$WHAT" 2>&1 | grep -q '^claimed' && { ok=1; "$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; break; }; fi   # holding now: no queue entry of ours may remain
    sleep 60; done
  [ $ok = 1 ] || { "$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; log "no slot before $END_H:00 tonight"; note "tier (b) controls: no exclusive slot inside tonight's window; nothing run in this attempt."; [ "${TB_ONE_SLICE:-0}" = 1 ] && exit 6; sleep 600; continue; }
  SL_START=$(date +%s); SL_END=$(( SL_START + 3*3600 )); n=0; stop=""
  log "slice granted, load $(cut -d' ' -f1-3 /proc/loadavg)"
  for i in $(seq 1 20); do awk -v l="$(cut -d' ' -f1 /proc/loadavg)" 'BEGIN{exit !(l<2)}' && break; sleep 30; done
  tools/tb_hash.sh "$EXE" $SAVE > $ST/hash_$(date +%m%d_%H%M).txt 2>&1
  while :; do
    line=$(pending | head -1); [ -n "$line" ] || break
    IFS='|' read -r tag env extra <<<"$line"
    log "$(settle) before $tag"                                  # wait counts against the guards below
    [ $(( SL_END - $(date +%s) )) -lt 1320 ] && { stop="slice time used"; break; }
    [ $(secs_to_8) -lt 1320 ] && { stop="$END_H:00 reached"; break; }
    "$T/protected-title-check.sh" >/dev/null 2>&1 && { stop="protected title running"; break; }
    pgrep -x sober >/dev/null && { stop="Roblox running"; break; }
    "$T/quiet-required.sh" claim $W 30 "$WHAT" >/dev/null 2>&1          # same-text renewal for this run
    log "run $tag env=[$env] extra=[$extra] load $(cut -d' ' -f1-3 /proc/loadavg)"
    [ $FIRST = 1 ] && { FIRST=0; note "tier (b) controls: FIRST BOOT now ($tag) under my EXCLUSIVE claim, slice granted $(date -d @$SL_START +%T)."; }
    tmo=${TB_TIMEOUT:-1800}; [ "$tag" = walk ] && tmo=${TB_WALK_TIMEOUT:-1500}
    env TB_NOCLAIM=1 TB_EXE="$EXE" TB_TIMEOUT=$tmo $env tools/tb_run.sh "$tag" $SAVE $extra > $ST/run_$tag.log 2>&1; rc=$?
    log "run $tag rc=$rc $(grep -a '^end ' out/tb_$tag/conditions.txt 2>/dev/null | cut -c1-90)"
    [ $rc = 0 ] || { stop="run $tag ended rc=$rc (3 = protected title, 4 = engine exited early, 5 = timeout)"; [ $rc = 3 ] || HARD=1; break; }
    if [ "$tag" = smokec ]; then if r=$(smokec_ok); then log "$r"; else log "$r"; stop="SMOKE FAILED its field check: $r"; HARD=1; break; fi; fi
    if [ "$tag" = smoke ]; then if r=$(smoke_ok); then log "$r"; else log "$r"; stop="SMOKE FAILED its field check: $r"; HARD=1; break; fi; fi
    echo "$tag" >> $ST/done.txt; n=$((n+1)); total_done=$((total_done+1))
    if grep -q '^DISTURBED' out/tb_$tag/conditions.txt 2>/dev/null && [ "$tag" != smoke ] && [[ "$tag" != *_rr ]]; then
      grep -v '^#' $ORDER | awk -F'|' -v t="$tag" '$1==t{print t"_rr|"$2"|"$3}' >> $ST/rerun.txt; log "run $tag DISTURBED (load at start >= 2): excluded, re-run queued as ${tag}_rr"; fi
  done
  "$T/quiet-required.sh" withdraw $W >/dev/null 2>&1; "$T/quiet-required.sh" release $W >/dev/null 2>&1   # withdraw FIRST, so release wakes the next worker, never us
  left=$(pending | wc -l)
  log "slice end: $n runs this slice, $(grep -c . $ST/done.txt) done in all, $left left. ${stop:-list finished}"
  note "tier (b) controls: slice ended $(date +%T): $n runs this slice, $(grep -c . $ST/done.txt) of $(grep -vc '^#' $ORDER) done, $left left. Reason: ${stop:-list finished}."
  [ "${HARD:-0}" = 1 ] && { log "STOPPED on a failed run; not continuing without a ruling"; exit 4; }
  [ "${TB_ONE_SLICE:-0}" = 1 ] && [ "$left" -gt 0 ] && { log "one slice only tonight (TB_ONE_SLICE): leaving, $left left for a later night"; exit 0; }
  [ "$left" -gt 0 ] && yield_gap          # gap between slices so queued workers get the box
done
log "all runs done"; note "tier (b) controls: ALL runs done ($(grep -c . $ST/done.txt)). Scoring next, as registered."; exit 0
