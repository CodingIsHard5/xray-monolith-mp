#!/usr/bin/env bash
# One boot of the co-op second-actor series: coop_series.sh <tag> [extra server flags]. Steps N = 0,0,1,2,4,(check),8.
# EXCLUSIVE claim from before the server start until after the stop. Windows: 30 s settle + 120 s sampled.
set -u
cd "$HOME/Projects/StalkerGameEngine" || exit 2
T="$HOME/.claude/overseer/tools"; L=tools/coop_load.sh; S=coopload; TAG="$1"; shift; O="out/coop_$TAG"; WIN="${COOP_WIN:-120}"
rm -rf "$O"; mkdir -p "$O"
"$T/protected-title-check.sh" >/dev/null 2>&1 && { echo "REFUSED: protected title"; exit 3; }
pgrep -x sober >/dev/null && { echo "REFUSED: sober"; exit 3; }
"$T/quiet-required.sh" want engineperf EXCLUSIVE >"$O/claim.txt" 2>&1
"$T/quiet-required.sh" claim engineperf 45 "EXCLUSIVE: co-op server + up to 8 headless clients, series $TAG (scratch copy, port 5461)" >>"$O/claim.txt" 2>&1 || { echo "REFUSED claim: $(tail -1 "$O/claim.txt")"; exit 3; }
trap '$L stop; "$T/quiet-required.sh" release engineperf >/dev/null 2>&1' EXIT
rm -f $S/sv/appdata/logs/*.log $S/sv/appdata/logs/sv_binder.csv $S/c*/appdata/logs/*.log
echo "start $(date '+%F %T') load $(cut -d' ' -f1-3 /proc/loadavg) tag=$TAG flags=$*" | tee "$O/conditions.txt"
$L server "$@"
SV=""; LOG=""
for i in $(seq 1 90); do sleep 2; LOG=$(ls $S/sv/appdata/logs/xray_*.log 2>/dev/null | head -1); [ -n "$LOG" ] && grep -aq "sv_binder: first update of actor id" "$LOG" && break; done
SV=$($L svpid); echo "server pid=$SV rss=$(awk '/VmRSS/{print $2}' /proc/$SV/status) kB ready after $((i*2))s"
[ -n "$SV" ] && grep -aq "sv_binder: first update" "$LOG" || { echo "FAIL: server not ready"; tail -5 $S/sv/appdata/logs/stderr.log; exit 1; }
CL=()
window() { # label
  sleep 30; cp $S/sv/appdata/logs/sv_binder.csv "$O/sv_binder_$1_start.csv" 2>/dev/null
  python3 tools/coop_sample.py "$WIN" "$O/samples.csv" "$1" "$SV" "${CL[@]}"
  cp $S/sv/appdata/logs/sv_binder.csv "$O/sv_binder_$1_end.csv" 2>/dev/null
  grep -a "COOP(clframe)" "$LOG" | tail -1 | sed "s/^/$1 /" >> "$O/frames.txt"
  kill -0 "$SV" 2>/dev/null || { echo "FAIL: server died at $1"; exit 1; }
  echo "window $1 done $(date '+%T') load $(cut -d' ' -f1-3 /proc/loadavg)"
}
join() { # n
  $L client "$1"
  for i in $(seq 1 120); do sleep 2; [ "$(grep -ac 'co-op actor spawned' "$LOG")" -ge "$1" ] && break; done
  [ "$(grep -ac 'co-op actor spawned' "$LOG")" -ge "$1" ] || { echo "FAIL: client $1 never joined in 240 s"; exit 1; }
  p=$($L clpid "$1"); CL+=("$p"); echo "client $1 joined after $((i*2))s pid=$p"
}
window n0a; window n0b
join 1; window n1
join 2; window n2
join 3; join 4; window n4
python3 - "$O/samples.csv" <<'PY' || { echo "CHECK at N=4 FAILED: stopping before 8"; exit 4; }
import csv,sys,statistics as st
v=[r for r in csv.reader(open(sys.argv[1])) if r[0]=='n4' and len(r)>=8]
idle=st.mean(float(r[7]) for r in v); rss=max(float(r[5]) for r in v)
cl=[st.mean(float(r[6].split(';')[i]) for r in v) for i in range(4)]
print('CHECK N=4: cores 0-1 idle %.0f%% (need >70), server rss %.0f MiB (need <6144), clients ms/s %s (need <500)'%(idle,rss,[round(x) for x in cl]))
sys.exit(0 if idle>70 and rss<6144 and max(cl)<500 else 1)
PY
join 5; join 6; join 7; join 8; window n8
cp "$LOG" "$O/server.log"; echo "RETIRED lines: $(grep -ac 'binder RETIRED' "$LOG")  lua errors: $(grep -ac 'LUA error' "$LOG")"
echo "end $(date '+%F %T') load $(cut -d' ' -f1-3 /proc/loadavg)" | tee -a "$O/conditions.txt"
