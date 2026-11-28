#!/usr/bin/env bash
# Mutant self-test of common/box_preflight.sh (v2), and of the refusals of s4d2_run.sh and tools/det_run.sh. FIXTURES ONLY.
#   nice -n 19 bash wt-render-split/perf-harness/common/box_preflight_selftest.sh
# Needs no box claim and starts no game. Every input of box_preflight.sh is pointed at a fixture in this run's own
# output folder (common/selftest-runs/<stamp>-<pid>/fx): the play lock, the /proc stand-ins, the title checker, the
# .protected-seen record, pgrep, flatpak, systemctl, the claim-file folder (slot files, quiet-claims records, queue), the
# X root, the run tree, wineserver and kill. The fixture tools are small /bin/sh scripts that only exit, print a canned
# text, or append a line to a log in the fixture folder. The test never creates or removes the real lock, never calls
# the claim tool (no verb at all), never runs the real title checker, pgrep, flatpak, systemctl or wineserver, never
# sends a signal (the kill stand-in only logs). s4d2_run.sh runs only as `--preflight-only` and `probe
# --preflight-only`; tools/det_run.sh only as `--preflight-only`. What happens after their preflight is tested through
# the shared helpers they call (box_guarded, box_rm_name, box_dumps_newer, box_wine_kill, box_kill_own,
# box_engines_in_prefix) and by a structural check that every write under run/ in both drivers goes through
# box_guarded. NOT tested (would need a real run): the refusal of a real (non-preflight) run while BOX_* overrides are set.
# Prints one line per case and "BOX SELFTEST: PASS|FAIL (n/N)".
set -u
C=$(cd "$(dirname "$0")" && pwd)
LIB=$C/box_preflight.sh; RUNNER=$C/../s4d2/s4d2_run.sh; DETRUN=$(cd "$C/../../.." && pwd)/tools/det_run.sh
OUT=$C/selftest-runs/$(date +%Y%m%d-%H%M%S)-$$
mkdir -p "$OUT" || { echo "cannot make $OUT"; exit 2; }
FX=$OUT/fx; mkdir -p "$FX/bin" "$FX/xroot/.X11-unix" "$FX/xbusy/.X11-unix" "$FX/run/bin" "$FX/run/prefix" "$FX/run/prefix_play" || exit 2
: > "$FX/xbusy/.X11-unix/X141"
LIBSHA=$(sha256sum < "$LIB" | cut -d' ' -f1)
NOW=$(date +%s); H=$(date -d "@$NOW" '+%F %H:%M:%S'); HOLD=$(date -d "@$((NOW - 4 * 3600))" '+%F %H:%M:%S')

# ---- /proc stand-ins ----
mkproc() {   # <dir>: pid 1 = systemd, 4242 live (the lock fixture), 5151 our engine, 5252 a watcher, 5353 Caden's engine
  local d=$FX/$1
  mkdir -p "$d/1" "$d/4242" "$d/5151" "$d/5252" "$d/5353" "$d/8080" "$d/8081"
  echo systemd > "$d/1/comm"; echo '0::/init.scope' > "$d/1/cgroup"
  ln -s /fixture/not/a/real/wine64-preloader "$d/5151/exe"; printf 'WINEPREFIX=%s\0DISPLAY=:141\0' "$FX/run/prefix" > "$d/5151/environ"
  ln -s /fixture/not/a/real/bash "$d/5252/exe"
  ln -s /fixture/not/a/real/wine64-preloader "$d/5353/exe"; printf 'WINEPREFIX=%s\0DISPLAY=:0\0' "$FX/run/prefix_play" > "$d/5353/environ"
  echo '8080 (Xvfb) S 7777 8080 8080 0 -1' > "$d/8080/stat"; echo '8081 (Xvfb) Z 7777 8081 8081 0 -1' > "$d/8081/stat"
  for p in 4242 5151 5252 5353; do echo "0::/user.slice/user-1000.slice/user@1000.service/app.slice/tmux-spawn-x.scope" > "$d/$p/cgroup"; done
}
mkproc proc
mkproc proc_sober_cg;   mkdir -p "$FX/proc_sober_cg/7000";   echo '0::/user.slice/user-1000.slice/user@1000.service/app.slice/app-flatpak-org.vinegarhq.Sober-123456.scope' > "$FX/proc_sober_cg/7000/cgroup"
mkproc proc_sober_info; mkdir -p "$FX/proc_sober_info/7001/root"; echo '0::/somewhere/else' > "$FX/proc_sober_info/7001/cgroup"
printf '[Application]\nname=org.vinegarhq.Sober\nruntime=runtime/org.freedesktop.Platform\n' > "$FX/proc_sober_info/7001/root/.flatpak-info"
mkproc proc_lookalike;  mkdir -p "$FX/proc_lookalike/7002";  echo '0::/x/app.slice/app-flatpak-org.vinegarhq.SoberX.scope' > "$FX/proc_lookalike/7002/cgroup"
mkproc proc_nons;  echo bash > "$FX/proc_nons/1/comm"
mkproc proc_nocg;  rm -f "$FX/proc_nocg/1/cgroup"

# ---- fixture tools ----
mk() { printf '#!/bin/sh\n%s\n' "$2" > "$FX/bin/$1"; chmod +x "$FX/bin/$1"; }
mk title_none   'exit 1'
mk title_play   'exit 0'
mk title_fail   'exit 3'
mk title_envdump 'env > "'"$FX"'/title_env.txt"; exit 1'
# pgrep: -x sober -> no match (the Sober game process is "Main"); -f <pattern> -> the pattern is matched (grep -E) against
# canned command lines, the Windows form of the play launcher's and det_run.sh's engine start in the fixture run tree
printf '%s\n' "Z:${FX//\//\\}\\run\\bin\\Warm18.exe -nointro -ltx user_play.ltx" > "$FX/cmd_engine.txt"
printf '%s\n' "python3 watch.py --log $FX/runX/bin_not_this" > "$FX/cmd_other.txt"
mk pgrep_clean  'exit 1'
mk pgrep_sober  'case " $* " in *" sober "*) exit 0 ;; esac; exit 1'
mk pgrep_engine 'case "$1" in -f) grep -Eq -- "$2" "'"$FX"'/cmd_engine.txt" && { echo 5151; exit 0; }; exit 1 ;; esac; exit 1'
mk pgrep_nomatch 'case "$1" in -f) grep -Eq -- "$2" "'"$FX"'/cmd_other.txt" && { echo 5151; exit 0; }; exit 1 ;; esac; exit 1'
mk pgrep_caden  'case "$1" in -f) echo 5353; exit 0 ;; esac; exit 1'
mk pgrep_both   'case "$1" in -f) echo 5151; echo 5353; exit 0 ;; esac; exit 1'
mk pgrep_watcher 'case "$1" in -f) echo 5252; exit 0 ;; esac; exit 1'
mk pgrep_garbage 'case "$1" in -f) echo abc; exit 0 ;; esac; exit 1'
mk pgrep_err    'exit 2'
mk flatpak_clean 'echo org.mozilla.firefox; exit 0'
mk flatpak_sober 'printf "org.mozilla.firefox\norg.vinegarhq.Sober\n"; exit 0'
mk flatpak_lookalike 'echo org.vinegarhq.SoberX; exit 0'
mk flatpak_fail  'exit 1'
mk sysctl_clean  'exit 0'
mk sysctl_sober  'echo "app-flatpak-org.vinegarhq.Sober-123456.scope loaded active running Sober"; exit 0'
mk sysctl_fail   'exit 1'
mk ws_rec  'echo "WS WINEPREFIX=$WINEPREFIX $*" >> "'"$FX"'/ws.log"; exit 0'
mk kill_rec 'echo "KILL $*" >> "'"$FX"'/kill.log"; exit 0'
echo "$((NOW - 30)) PROTECTED: Helldivers 2 pid=1 basename=helldivers2 cpu=50%" > "$FX/pseen_recent"
echo "$((NOW - 1000)) PROTECTED: Helldivers 2 pid=1 basename=helldivers2 cpu=50%" > "$FX/pseen_old"
echo "garbage" > "$FX/pseen_garbage"

# ---- claim files (the claim tool's own formats: slot lines, quiet-claims/<slot> records, QUIET-QUEUE) ----
q() { mkdir -p "$FX/$1"; }
for d in q_clean q_excl q_slot_excl q_expired q_unread q_none q_other_job q_mine q_handoff q_slot_ours q_mine_and_other \
         q_nested q_slot1_expired q_badline q_queue1 q_queue2; do q "$d"; done
echo "$H rootsim 90 headless: rootsim full game" > "$FX/q_clean/QUIET-REQUIRED"
echo "$H rootsim 90 headless: rootsim full game (Ollama GPU) EXCLUSIVE" > "$FX/q_excl/QUIET-REQUIRED"
echo "$H rootsim 90 headless: rootsim full game" > "$FX/q_slot_excl/QUIET-REQUIRED"
echo "$H other 30 EXCLUSIVE timing" > "$FX/q_slot_excl/QUIET-REQUIRED-2"
echo "$H rootsim 90 headless: rootsim full game" > "$FX/q_expired/QUIET-REQUIRED"
echo "$H old 30 EXCLUSIVE timing" > "$FX/q_expired/QUIET-REQUIRED-3.expired"
mkdir -p "$FX/q_unread/QUIET-REQUIRED"          # a folder where the claim file should be: not a readable file
echo "$H engineperf 45 headless Xvfb GAMMA A-Life equivalence series s4d2 (series) s4d2-series-1-1" > "$FX/q_other_job/QUIET-REQUIRED"
echo "$H engineperf 45 headless series s4d2 (probe) TOKMINE" > "$FX/q_mine/QUIET-REQUIRED"
echo "$H engineperf 5 EXCLUSIVE handoff pre-claim (claim within 5 min)" > "$FX/q_handoff/QUIET-REQUIRED"
echo "$H rootsim 90 headless rootsim game" > "$FX/q_slot_ours/QUIET-REQUIRED"
echo "$H engineperf 45 headless other engineperf job" > "$FX/q_slot_ours/QUIET-REQUIRED-2"
echo "$H engineperf 45 headless series s4d2 (probe) TOKMINE" > "$FX/q_mine_and_other/QUIET-REQUIRED"
echo "$H engineperf 30 headless another engineperf job" > "$FX/q_mine_and_other/QUIET-REQUIRED-2"
# S-7: two engineperf claims NESTED on slot 1; the slot line shows only ours (the newest record's <what>)
L1="$H engineperf 45 headless series s4d2 (probe) TOKMINE"
echo "$L1" > "$FX/q_nested/QUIET-REQUIRED"; mkdir -p "$FX/q_nested/quiet-claims"
printf '#slot %s\nc1.1.1 engineperf %s 60 111:222 0 headless det_run other job det_run-x-1\nc2.2.2 engineperf %s 45 333:444 0 headless series s4d2 (probe) TOKMINE\n' \
  "$L1" "$((NOW - 600))" "$((NOW - 60))" > "$FX/q_nested/quiet-claims/1"
echo "$HOLD engineperf 30 headless old engineperf job EXCLUSIVE" > "$FX/q_slot1_expired/QUIET-REQUIRED"   # N-11: expired
echo "not a claim line" > "$FX/q_badline/QUIET-REQUIRED"
printf 'rootsim %s EXCLUSIVE\nengineperf %s\n' "$NOW" "$NOW" > "$FX/q_queue1/QUIET-QUEUE"
printf 'engineperf %s\nengineperf %s\n' "$NOW" "$NOW" > "$FX/q_queue2/QUIET-QUEUE"
echo 4242 > "$FX/lock_live"; echo 999999 > "$FX/lock_dead"; echo abc > "$FX/lock_garbage"; : > "$FX/lock_empty"
printf '4242\r\n' > "$FX/lock_live_crlf"
printf 'BOX_PREFLIGHT_VERSION=2\n' > "$FX/lib_nomarker.sh"
printf 'BOX_PREFLIGHT_MARKER=engineperf-box-preflight-v2\nBOX_PREFLIGHT_VERSION=1\n' > "$FX/lib_oldversion.sh"
{ cat "$LIB"; echo 'unset -f box_ok_claims'; } > "$FX/lib_nofunc.sh"
{ cat "$LIB"; echo 'unset -f box_guarded'; } > "$FX/lib_noguard.sh"

# the clean environment: EVERY input overridden (a missing override would read a real input)
CLEAN=(BOX_PLAY_LOCK="$FX/no_lock" BOX_PROC="$FX/proc" BOX_TITLE_CHECK="$FX/bin/title_none" BOX_PSEEN="$FX/no_pseen"
       BOX_PGREP="$FX/bin/pgrep_clean" BOX_FLATPAK="$FX/bin/flatpak_clean" BOX_SYSTEMCTL="$FX/bin/sysctl_clean"
       BOX_QUIET_DIR="$FX/q_clean" BOX_XROOT="$FX/xroot" BOX_RUN="$FX/run" BOX_WINESERVER="$FX/bin/ws_rec"
       BOX_KILL="$FX/bin/kill_rec" BENCH_DISPLAY=:141)
# refuse to go on if a fixture path could be a real one
for kv in "${CLEAN[@]}"; do case "$kv" in BENCH_DISPLAY=*) continue ;; esac; case "${kv#*=}" in "$OUT"/*) ;; *) echo "fixture outside $OUT: $kv"; exit 2 ;; esac; done
for v in $(compgen -v PTC_); do echo "this shell sets $v: unset it first (every title check would refuse)"; exit 2; done

n=0; ok=0
case_() {   # <name> <want rc> <must text ('' = none)> <mustnot text ('' = none)> <env...> -- <command words...>
  local name=$1 want=$2 must=$3 mustnot=$4 envs=() out r good=1; shift 4
  while [ "$1" != -- ]; do envs+=("$1"); shift; done; shift
  out=$(env "${CLEAN[@]}" "${envs[@]}" "$@" 2>&1 </dev/null); r=$?
  [ "$r" = "$want" ] || good=0
  [ -z "$must" ] || printf '%s\n' "$out" | grep -qF -- "$must" || good=0
  [ -z "$mustnot" ] || ! printf '%s\n' "$out" | grep -qF -- "$mustnot" || good=0
  n=$((n + 1)); [ $good = 1 ] && ok=$((ok + 1))
  printf '%-4s %-36s rc %s (want %s) | %s\n' "$([ $good = 1 ] && echo ok || echo BAD)" "$name" "$r" "$want" \
    "$(printf '%s\n' "$out" | grep -v '^BOX OVERRIDES' | tr '\n' ' ' | cut -c1-150)"
  printf '== %s rc %s\n%s\n' "$name" "$r" "$out" >> "$OUT/cases.log"
}
ALL=(bash -c '. "$0"; box_preflight_all "$@"' "$LIB")        # + <class> <display> <worker> [<token>]
ONE=(bash -c '. "$0"; f=$1; shift; "$f" "$@"; r=$?; echo "WHY: $BOX_WHY"; exit $r' "$LIB")
SEQ=(bash -c '. "$0"; . "$1"' "$LIB")                          # + <sequence script>: source the lib, then the script

# ---- the checks one by one, through box_preflight_all (refusal BY NAME) ----
case_ clean_all_overridden 0 "BOX OVERRIDES: BOX_PLAY_LOCK BOX_PROC BOX_TITLE_CHECK BOX_PSEEN BOX_PGREP BOX_FLATPAK BOX_SYSTEMCTL BOX_QUIET_DIR BOX_XROOT BOX_RUN BOX_WINESERVER BOX_KILL" "BOX REFUSED" -- "${ALL[@]}" shared :141 engineperf
case_ hostns_not_systemd   2 "BOX REFUSED: host_pid_ns:" "" BOX_PROC="$FX/proc_nons" -- "${ALL[@]}" shared :141 engineperf
case_ lock_live            1 "BOX REFUSED: play_lock: Caden's play session is running" "" BOX_PLAY_LOCK="$FX/lock_live" -- "${ALL[@]}" shared :141 engineperf
case_ lock_live_crlf       1 "BOX REFUSED: play_lock: Caden's play session" "" BOX_PLAY_LOCK="$FX/lock_live_crlf" -- "${ALL[@]}" shared :141 engineperf
case_ lock_garbage         2 "BOX REFUSED: play_lock:" "" BOX_PLAY_LOCK="$FX/lock_garbage" -- "${ALL[@]}" shared :141 engineperf
case_ lock_empty           2 "holds no pid" "" BOX_PLAY_LOCK="$FX/lock_empty" -- "${ALL[@]}" shared :141 engineperf
case_ lock_is_folder       2 "not a readable file" "" BOX_PLAY_LOCK="$FX/proc" -- "${ALL[@]}" shared :141 engineperf
case_ lock_stale_accepted  0 "" "BOX REFUSED" BOX_PLAY_LOCK="$FX/lock_dead" -- "${ALL[@]}" shared :141 engineperf
case_ title_playing        1 "BOX REFUSED: protected_title: a protected title is playing" "" BOX_TITLE_CHECK="$FX/bin/title_play" -- "${ALL[@]}" shared :141 engineperf
case_ title_check_fails    2 "BOX REFUSED: protected_title: the title checker exited 3" "" BOX_TITLE_CHECK="$FX/bin/title_fail" -- "${ALL[@]}" shared :141 engineperf
case_ title_check_missing  2 "BOX REFUSED: protected_title: the title checker" "" BOX_TITLE_CHECK="$FX/bin/no_such_tool" -- "${ALL[@]}" shared :141 engineperf
case_ title_seen_30s_ago   1 "BOX REFUSED: protected_title: a protected title was seen 30s ago" "" BOX_PSEEN="$FX/pseen_recent" -- "${ALL[@]}" shared :141 engineperf
case_ title_seen_long_ago  0 "" "BOX REFUSED" BOX_PSEEN="$FX/pseen_old" -- "${ALL[@]}" shared :141 engineperf
case_ title_seen_garbage   2 "holds no time" "" BOX_PSEEN="$FX/pseen_garbage" -- "${ALL[@]}" shared :141 engineperf
case_ title_ptc_env_refused 2 "BOX REFUSED: protected_title: the caller's environment sets PTC_MIN_PCT" "" PTC_MIN_PCT=99 -- "${ALL[@]}" shared :141 engineperf
case_ title_env_scrubbed   0 "WHY: no protected title playing" "" BOX_TITLE_CHECK="$FX/bin/title_envdump" INTERVAL=0 MIN_PCT=99 -- "${ONE[@]}" box_ok_title
case_ title_env_scrubbed_chk 0 "SCRUBBED" "" -- bash -c '! grep -q "^INTERVAL=\|^MIN_PCT=" "$0" && grep -q "^XDG_RUNTIME_DIR=/run/user/$UID$" "$0" && echo SCRUBBED' "$FX/title_env.txt"
case_ roblox_pgrep_name    1 "BOX REFUSED: roblox: Sober (Roblox) is running: a process named sober" "engine_running" BOX_PGREP="$FX/bin/pgrep_sober" -- "${ALL[@]}" shared :141 engineperf
case_ roblox_cgroup_Main   1 "BOX REFUSED: roblox: Sober (Roblox) is running: a process in a Sober flatpak scope" "" BOX_PROC="$FX/proc_sober_cg" -- "${ALL[@]}" shared :141 engineperf
case_ roblox_flatpak_info  1 "a process of the Sober flatpak" "" BOX_PROC="$FX/proc_sober_info" -- "${ALL[@]}" shared :141 engineperf
case_ roblox_flatpak_ps    1 "flatpak ps lists org.vinegarhq.Sober" "" BOX_FLATPAK="$FX/bin/flatpak_sober" -- "${ALL[@]}" shared :141 engineperf
case_ roblox_systemd_scope 1 "an active Sober scope" "" BOX_SYSTEMCTL="$FX/bin/sysctl_sober" -- "${ALL[@]}" shared :141 engineperf
case_ roblox_lookalike_ok  0 "" "BOX REFUSED" BOX_PROC="$FX/proc_lookalike" BOX_FLATPAK="$FX/bin/flatpak_lookalike" -- "${ALL[@]}" shared :141 engineperf
case_ roblox_flatpak_fails 2 "BOX REFUSED: roblox: cannot tell whether Sober (Roblox) runs: flatpak ps exited 1" "" BOX_FLATPAK="$FX/bin/flatpak_fail" -- "${ALL[@]}" shared :141 engineperf
case_ roblox_systemctl_fails 2 "systemctl --user list-units exited 1" "" BOX_SYSTEMCTL="$FX/bin/sysctl_fail" -- "${ALL[@]}" shared :141 engineperf
case_ roblox_fail_but_seen 1 "BOX REFUSED: roblox: Sober (Roblox) is running" "" BOX_FLATPAK="$FX/bin/flatpak_fail" BOX_SYSTEMCTL="$FX/bin/sysctl_sober" -- "${ALL[@]}" shared :141 engineperf
case_ roblox_proc_unreadable 2 "cgroup is unreadable" "" BOX_PROC="$FX/proc_nocg" -- "${ALL[@]}" shared :141 engineperf
case_ pgrep_fails          2 "BOX REFUSED: engine_running: pgrep -f" "" BOX_PGREP="$FX/bin/pgrep_err" -- "${ALL[@]}" shared :141 engineperf
case_ engine_real_pattern  1 "BOX REFUSED: engine_running: an engine from" "roblox" BOX_PGREP="$FX/bin/pgrep_engine" -- "${ALL[@]}" shared :141 engineperf
case_ engine_pattern_nomatch 0 "" "BOX REFUSED" BOX_PGREP="$FX/bin/pgrep_nomatch" -- "${ALL[@]}" shared :141 engineperf
case_ engine_watcher_ignored 0 "" "BOX REFUSED" BOX_PGREP="$FX/bin/pgrep_watcher" -- "${ALL[@]}" shared :141 engineperf
case_ engine_pgrep_garbage 2 "BOX REFUSED: engine_running: pgrep printed 'abc'" "" BOX_PGREP="$FX/bin/pgrep_garbage" -- "${ALL[@]}" shared :141 engineperf
case_ claim_other_job      1 "BOX REFUSED: own_claim: a live claim under 'engineperf'" "" BOX_QUIET_DIR="$FX/q_other_job" -- "${ALL[@]}" shared :141 engineperf
case_ claim_other_job_tok  1 "BOX REFUSED: own_claim:" "" BOX_QUIET_DIR="$FX/q_other_job" -- "${ALL[@]}" shared :141 engineperf TOKMINE
case_ claim_mine_by_token  0 "" "BOX REFUSED" BOX_QUIET_DIR="$FX/q_mine" -- "${ALL[@]}" shared :141 engineperf TOKMINE
case_ claim_mine_no_token  1 "BOX REFUSED: own_claim:" "" BOX_QUIET_DIR="$FX/q_mine" -- "${ALL[@]}" shared :141 engineperf
case_ claim_handoff_pre    0 "" "BOX REFUSED" BOX_QUIET_DIR="$FX/q_handoff" -- "${ALL[@]}" exclusive :141 engineperf
case_ claim_ours_in_slot   1 "BOX REFUSED: own_claim:" "" BOX_QUIET_DIR="$FX/q_slot_ours" -- "${ALL[@]}" shared :141 engineperf
case_ claim_other_worker   0 "" "BOX REFUSED" BOX_QUIET_DIR="$FX/q_clean" -- "${ALL[@]}" shared :141 engineperf
case_ claim_nested_hidden  1 "2 record(s) under 'engineperf', 1 carry" "" BOX_QUIET_DIR="$FX/q_nested" -- "${ALL[@]}" shared :141 engineperf TOKMINE
case_ claim_slot1_expired  0 "" "BOX REFUSED" BOX_QUIET_DIR="$FX/q_slot1_expired" -- "${ALL[@]}" shared :141 engineperf
case_ claim_line_garbage   2 "BOX REFUSED: own_claim:" "" BOX_QUIET_DIR="$FX/q_badline" -- "${ALL[@]}" shared :141 engineperf
case_ exclusive_named      1 "BOX REFUSED: exclusive:" "" BOX_QUIET_DIR="$FX/q_excl" -- "${ALL[@]}" shared :141 engineperf
case_ exclusive_in_slot    1 "claim slot 2 names EXCLUSIVE" "" BOX_QUIET_DIR="$FX/q_slot_excl" -- "${ALL[@]}" shared :141 engineperf
case_ exclusive_expired_ok 0 "" "BOX REFUSED" BOX_QUIET_DIR="$FX/q_expired" -- "${ALL[@]}" shared :141 engineperf
case_ exclusive_unreadable 2 "BOX REFUSED: exclusive:" "" BOX_QUIET_DIR="$FX/q_unread" -- "${ALL[@]}" shared :141 engineperf
case_ exclusive_no_files   0 "" "BOX REFUSED" BOX_QUIET_DIR="$FX/q_none" -- "${ALL[@]}" shared :141 engineperf
case_ exclusive_class_skip 0 "" "BOX REFUSED" BOX_QUIET_DIR="$FX/q_excl" -- "${ALL[@]}" exclusive :141 engineperf
case_ display_0            1 "BOX REFUSED: display: display :0 is Caden's screen" "" -- "${ALL[@]}" shared :0 engineperf
case_ display_00           1 "Caden's screen" "" -- "${ALL[@]}" shared :00 engineperf
case_ display_host0        2 "BOX REFUSED: display: display 'localhost:0'" "" -- "${ALL[@]}" shared localhost:0 engineperf
case_ display_screen0      2 "BOX REFUSED: display:" "" -- "${ALL[@]}" shared :0.0 engineperf
case_ class_bogus          2 "BOX REFUSED: claim_class:" "" -- "${ALL[@]}" EXCL :141 engineperf
case_ two_refusals_named   1 "BOX REFUSED: protected_title:" "" BOX_PLAY_LOCK="$FX/lock_live" BOX_TITLE_CHECK="$FX/bin/title_play" -- "${ALL[@]}" shared :141 engineperf
case_ two_refusals_lock    1 "BOX REFUSED: play_lock:" "" BOX_PLAY_LOCK="$FX/lock_live" BOX_TITLE_CHECK="$FX/bin/title_play" -- "${ALL[@]}" shared :141 engineperf
# ---- the helpers outside box_preflight_all ----
case_ display_free_ok      0 "WHY: display :141 free" "" -- "${ONE[@]}" box_ok_display_free :141
case_ display_socket       1 "WHY: display :141 exists" "" BOX_XROOT="$FX/xbusy" -- "${ONE[@]}" box_ok_display_free :141
case_ token_held           0 "WHY: a claim under 'engineperf' carries TOKMINE" "" BOX_QUIET_DIR="$FX/q_mine" -- "${ONE[@]}" box_claim_has_token engineperf TOKMINE
case_ token_nested_held    0 "carries TOKMINE (2 record(s)" "" BOX_QUIET_DIR="$FX/q_nested" -- "${ONE[@]}" box_claim_has_token engineperf TOKMINE
case_ token_not_held       1 "WHY: no claim under 'engineperf' carries TOKMINE" "" BOX_QUIET_DIR="$FX/q_other_job" -- "${ONE[@]}" box_claim_has_token engineperf TOKMINE
case_ token_files_bad      2 "cannot tell" "" BOX_QUIET_DIR="$FX/q_unread" -- "${ONE[@]}" box_claim_has_token engineperf TOKMINE
case_ queue_count_1        0 "WHY: 1 queue entr" "" BOX_QUIET_DIR="$FX/q_queue1" -- "${ONE[@]}" box_queue_count engineperf
case_ queue_count_2        0 "WHY: 2 queue entr" "" BOX_QUIET_DIR="$FX/q_queue2" -- "${ONE[@]}" box_queue_count engineperf
case_ queue_count_none     0 "WHY: no queue file" "" -- "${ONE[@]}" box_queue_count engineperf
case_ prefix_ours_left     1 "pid 5151" "5353" BOX_PGREP="$FX/bin/pgrep_both" -- "${ONE[@]}" box_engines_in_prefix "$FX/run/prefix"
case_ prefix_caden_ignored 0 "WHY: no engine of the run tree uses" "" BOX_PGREP="$FX/bin/pgrep_caden" -- "${ONE[@]}" box_engines_in_prefix "$FX/run/prefix"
case_ fresh_engine_caden   1 "WHY: engine_running:" "" BOX_PGREP="$FX/bin/pgrep_caden" -- "${ONE[@]}" box_fresh

# ---- B-1 / B-2: the guarded write (box_guarded) and what a stop leaves (fixture files only) ----
W1=$FX/w1; mkdir -p "$W1"
cat > "$FX/seq_lock_after_claim.sh" <<EOF
# a fixture 'claim' step during which Caden's lock appears (the hook), then the guarded delete and a later write
mkdir -p "$W1/logs"; echo log > "$W1/logs/xray_caden.log"; echo cfg > "$W1/user.ltx"; rm -f "$FX/lock_dyn"
fake_claim() { echo 4242 > "$FX/lock_dyn"; }
fake_claim
box_guarded "remove the log" box_rm_name "$W1/logs" xray_caden.log && echo "DELETED"
rm -f "$FX/lock_dyn"   # the lock goes away again: the stop is sticky, the later write is still skipped
box_guarded "restore user.ltx" cp -- "$FX/lock_dead" "$W1/user.ltx" && echo "WROTE"
[ -e "$W1/logs/xray_caden.log" ] && [ "\$(cat "$W1/user.ltx")" = cfg ] && echo "UNTOUCHED: \$BOX_STOPPED | skipped: \$BOX_SKIPPED"
EOF
case_ guard_lock_after_claim 0 "UNTOUCHED: before 'remove the log': play_lock: Caden's play session" "DELETED" BOX_PLAY_LOCK="$FX/lock_dyn" -- "${SEQ[@]}" "$FX/seq_lock_after_claim.sh"
case_ guard_lock_sticky    0 "skipped: remove the log; restore user.ltx" "WROTE" BOX_PLAY_LOCK="$FX/lock_dyn" -- "${SEQ[@]}" "$FX/seq_lock_after_claim.sh"
W2=$FX/w2; mkdir -p "$W2"
cat > "$FX/seq_clear.sh" <<EOF
mkdir -p "$W2/logs"; echo log > "$W2/logs/xray_caden.log"
box_guarded "remove the log" box_rm_name "$W2/logs" xray_caden.log && [ ! -e "$W2/logs/xray_caden.log" ] && echo "DELETED"
EOF
case_ guard_clear_deletes  0 "DELETED" "" -- "${SEQ[@]}" "$FX/seq_clear.sh"
case_ guard_title_stops    1 "" "DELETED" BOX_TITLE_CHECK="$FX/bin/title_play" -- "${SEQ[@]}" "$FX/seq_clear.sh"
case_ guard_sober_stops    1 "" "DELETED" BOX_PROC="$FX/proc_sober_cg" -- "${SEQ[@]}" "$FX/seq_clear.sh"
W3=$FX/w3; mkdir -p "$W3/logs" "$W3/saves" "$W3/out"
cat > "$FX/seq_lockstop.sh" <<EOF
# det_run.sh's stop path: a stop is recorded (the lock appeared mid-boot); the collect and restore steps must not run
for f in logs/xray_caden.log logs/alife_dump_t1_1.txt logs/det_counters_t1_1.txt saves/det_t1_1.scop user.ltx; do echo "\$f" > "$W3/\$f"; done
before=\$(cd "$W3" && find . -type f | sort | xargs sha256sum)
BOX_STOPPED="play_lock during the boot: fixture"
box_guarded "collect" mv -- "$W3/logs/alife_dump_t1_1.txt" "$W3/out/"
box_guarded "restore user.ltx" cp -- "$FX/lock_dead" "$W3/user.ltx"
box_wine_kill "$FX/run/prefix"; echo "\$BOX_WHY"
after=\$(cd "$W3" && find . -type f | sort | xargs sha256sum)
[ "\$before" = "\$after" ] && echo "FILES UNTOUCHED; skipped: \$BOX_SKIPPED"
EOF
case_ lockstop_files_untouched 0 "FILES UNTOUCHED; skipped: collect; restore user.ltx" "" BOX_PLAY_LOCK="$FX/lock_live" -- "${SEQ[@]}" "$FX/seq_lockstop.sh"
case_ lockstop_wine_own_prefix 0 "WS WINEPREFIX=$FX/run/prefix -k" "prefix_play" -- cat "$FX/ws.log"
# ---- box_rm_name: the target cannot leave its folder ----
mkdir -p "$FX/rmdir"; echo x > "$FX/rmdir/a.log"; ln -s "$FX/rmdir" "$FX/rmlink"
case_ rm_name_ok           0 "" "" -- "${ONE[@]}" box_rm_name "$FX/rmdir" a.log
case_ rm_name_slash        2 "is not one file name" "" -- "${ONE[@]}" box_rm_name "$FX/rmdir" ../a.log
case_ rm_name_dotdot       2 "is not one file name" "" -- "${ONE[@]}" box_rm_name "$FX/rmdir" ..
case_ rm_name_glob         2 "is not one file name" "" -- "${ONE[@]}" box_rm_name "$FX/rmdir" 'alife_dump_*.txt'
case_ rm_dir_link          2 "is not a real folder" "" -- "${ONE[@]}" box_rm_name "$FX/rmlink" a.log
case_ rm_dir_relative      2 "is not absolute" "" -- "${ONE[@]}" box_rm_name rmdir a.log
# ---- N-7: only crash dumps newer than this boot's start marker ----
mkdir -p "$FX/dumps"; echo old > "$FX/dumps/old_caden.mdmp"; touch -d '-1 hour' "$FX/dumps/old_caden.mdmp"
touch -d '-1 minute' "$FX/dumps/boot.start"; echo new > "$FX/dumps/new_ours.mdmp"
case_ dumps_old_not_listed 0 "new_ours.mdmp" "old_caden.mdmp" -- "${ONE[@]}" box_dumps_newer "$FX/dumps" "$FX/dumps/boot.start"
case_ dumps_no_marker      2 "no start marker" "" -- "${ONE[@]}" box_dumps_newer "$FX/dumps" "$FX/dumps/no_marker"
# ---- box_wine_kill: never empty, never prefix_play, never a link ----
ln -s "$FX/run/prefix" "$FX/linkrun_prefix"; mkdir -p "$FX/lr"; ln -s "$FX/run" "$FX/lr/run"
case_ wine_kill_empty      2 "wineserver -k NOT run" "" -- "${ONE[@]}" box_wine_kill ""
case_ wine_kill_play       2 "wineserver -k NOT run" "" -- "${ONE[@]}" box_wine_kill "$FX/run/prefix_play"
case_ wine_kill_link       2 "is not a real folder that is its own realpath" "" -- "${ONE[@]}" box_wine_kill "$FX/lr/run/prefix"
case_ wine_kill_no_play_logged 1 "" "prefix_play" -- grep -c prefix_play "$FX/ws.log"
# ---- N-4: box_kill_own (the kill stand-in only logs) ----
case_ kill_own_child       0 "WHY: killed our Xvfb (pid 8080)" "" -- "${ONE[@]}" box_kill_own 8080 Xvfb 7777
case_ kill_wrong_parent    1 "NOT killed" "" -- "${ONE[@]}" box_kill_own 8080 Xvfb 1234
case_ kill_wrong_name      1 "NOT killed" "" -- "${ONE[@]}" box_kill_own 8080 openbox 7777
case_ kill_zombie          1 "NOT killed" "" -- "${ONE[@]}" box_kill_own 8081 Xvfb 7777
case_ kill_gone            1 "is gone" "" -- "${ONE[@]}" box_kill_own 9999 Xvfb 7777
case_ kill_log_only_8080   0 "KILL 8080" "8081" -- cat "$FX/kill.log"

# ---- structure: every write under run/ in both drivers goes through box_guarded ----
cat > "$FX/struct.awk" <<'EOF'
# flags a non-comment line that copies, moves, deletes, links or redirects into the run tree outside an allowed
# function body (A: names; det_run.sh's are only ever called guarded, checked in struct.sh; s4d2's log() writes its own
# state folder only) without box_guarded on it
function count(s, c,   n, i) { n = 0; for (i = 1; i <= length(s); i++) if (substr(s, i, 1) == c) n++; return n }
BEGIN { n = split(A, x, " "); for (i = 1; i <= n; i++) ALLOW[x[i]] = 1 }
/^[ \t]*#/ { next }
{ line = $0; bare = line; gsub(/"[^"]*"/, "\"\"", bare) }
fn == "" && match(line, /^[a-z_]+\(\) \{/) { fn = substr(line, 1, index(line, "(") - 1); depth = 0 }
{
  w = (bare ~ /(^|[;&|({ ])(cp|mv|rm|ln|tee|truncate|install) /) || (line ~ />>? *"\$(RUN|LOGDIR|SAVES|LOGS|AXR_DST|CFG)/) || (bare ~ />>? *\$(RUN|LOGDIR|SAVES|LOGS|AXR_DST|CFG)/) || (bare ~ />>? *run\//)
  if (w && line !~ /box_guarded/ && !(fn in ALLOW)) print FILENAME ":" FNR ": " line
}
fn != "" { depth += count(line, "{") - count(line, "}"); if (depth <= 0) fn = "" }
EOF
cat > "$FX/struct.sh" <<EOF
r=0
o=\$(awk -v A="ltx_swap clear_logs start_engine collect" -f "$FX/struct.awk" "$DETRUN")
[ -n "\$o" ] && { echo "UNGUARDED in det_run.sh:"; echo "\$o"; r=1; }
for f in ltx_swap clear_logs start_engine collect; do
  c=\$(grep -nw -- "\$f" "$DETRUN" | grep -v "^[0-9]*:\$f() {" | grep -v '^[0-9]*:[[:space:]]*#' | grep -Ev "(^[0-9]*:guard|box_guarded) \"[^\"]*\" \$f( |\\\$)")
  [ -n "\$c" ] && { echo "\$f called without guard:"; echo "\$c"; r=1; }
done
o=\$(awk -v A="log" -f "$FX/struct.awk" "$RUNNER" | grep -vF 'mv -- out/det_s4p_probe "\$MOVE_TO"')
[ -n "\$o" ] && { echo "UNGUARDED in s4d2_run.sh:"; echo "\$o"; r=1; }
[ \$r = 0 ] && echo "STRUCTURE OK: every write under run/ is guarded"
exit \$r
EOF
case_ struct_writes_guarded 0 "STRUCTURE OK" "UNGUARDED" -- bash "$FX/struct.sh"
{ cat "$DETRUN"; echo 'rm -f "$LOGDIR/xray_$ME.log"'; echo 'cp -- x "$RUN/appdata/user.ltx"'; echo 'echo 1 > "$RUN/appdata/x"'; } > "$FX/det_mutant.sh"
case_ struct_catches_mutant 0 "3" "" -- bash -c 'awk -v A="ltx_swap clear_logs start_engine collect" -f "$0" "$1" | grep -c det_mutant' "$FX/struct.awk" "$FX/det_mutant.sh"
{ cat "$DETRUN"; echo 'clear_logs'; } > "$FX/det_mutant2.sh"
case_ struct_catches_unguarded_call 1 "clear_logs called without guard" "" -- bash -c 'sed "s#$1#$0#g" "$2" | bash' "$FX/det_mutant2.sh" "$DETRUN" "$FX/struct.sh"

# ---- s4d2_run.sh: refuses to start without the real shared file; box-only mode answers by name ----
case_ driver_lib_missing   3 "REFUSED: box_preflight: missing file $FX/moved_aside.sh" "" S4D2_BOX_LIB="$FX/moved_aside.sh" -- bash "$RUNNER" --preflight-only
case_ driver_lib_unpinned  3 "is not the pinned file" "" S4D2_BOX_LIB="$FX/lib_nomarker.sh" -- bash "$RUNNER" --preflight-only
case_ driver_lib_nomarker  3 "did not set the marker" "" S4D2_BOX_LIB="$FX/lib_nomarker.sh" S4D2_BOX_LIB_SHA="$(sha256sum < "$FX/lib_nomarker.sh" | cut -d' ' -f1)" -- bash "$RUNNER" --preflight-only
case_ driver_lib_version   3 "is version '1'" "" S4D2_BOX_LIB="$FX/lib_oldversion.sh" S4D2_BOX_LIB_SHA="$(sha256sum < "$FX/lib_oldversion.sh" | cut -d' ' -f1)" -- bash "$RUNNER" --preflight-only
case_ driver_lib_nofunc    3 "does not define box_ok_claims" "" S4D2_BOX_LIB="$FX/lib_nofunc.sh" S4D2_BOX_LIB_SHA="$(sha256sum < "$FX/lib_nofunc.sh" | cut -d' ' -f1)" -- bash "$RUNNER" --preflight-only
case_ driver_lib_noguard   3 "does not define box_guarded" "" S4D2_BOX_LIB="$FX/lib_noguard.sh" S4D2_BOX_LIB_SHA="$(sha256sum < "$FX/lib_noguard.sh" | cut -d' ' -f1)" -- bash "$RUNNER" --preflight-only
case_ driver_lib_unreadable 3 "is not a readable file" "" S4D2_BOX_LIB="$FX/proc" -- bash "$RUNNER" --preflight-only
case_ driver_clean         0 "box preflight ok (box_preflight.sh v2)" "BOX REFUSED" S4D2_BOX_LIB="$LIB" -- bash "$RUNNER" --preflight-only
case_ driver_real_lib_pin  0 "box preflight ok" "" -- bash "$RUNNER" --preflight-only
case_ driver_lock          1 "BOX REFUSED: play_lock:" "" BOX_PLAY_LOCK="$FX/lock_live" -- bash "$RUNNER" --preflight-only
case_ driver_title_fails   2 "BOX REFUSED: protected_title:" "" BOX_TITLE_CHECK="$FX/bin/title_fail" -- bash "$RUNNER" --preflight-only
case_ driver_sober_cgroup  1 "BOX REFUSED: roblox:" "" BOX_PROC="$FX/proc_sober_cg" -- bash "$RUNNER" --preflight-only
case_ driver_other_claim   1 "BOX REFUSED: own_claim:" "" BOX_QUIET_DIR="$FX/q_other_job" -- bash "$RUNNER" --preflight-only
case_ driver_exclusive     1 "BOX REFUSED: exclusive:" "" BOX_QUIET_DIR="$FX/q_excl" -- bash "$RUNNER" --preflight-only
case_ driver_display0      2 "REFUSED: BENCH_DISPLAY ':0' is not :141" "" BENCH_DISPLAY=:0 -- bash "$RUNNER" --preflight-only
case_ driver_full_preflight_box 2 "REFUSED (box): BOX REFUSED: play_lock:" "" BOX_PLAY_LOCK="$FX/lock_live" -- bash "$RUNNER" probe --preflight-only

# ---- tools/det_run.sh --preflight-only (source + checks + exit: no out folder, claim, Xvfb, Wine or engine) ----
DR=(bash "$DETRUN" --preflight-only Fixture.exe st_fixture)
case_ detrun_lib_missing     3 "REFUSED: box_preflight: missing file $FX/moved_aside.sh" "" DET_BOX_LIB="$FX/moved_aside.sh" -- "${DR[@]}"
case_ detrun_lib_unpinned    3 "is not the pinned file" "" DET_BOX_LIB="$FX/lib_nomarker.sh" -- "${DR[@]}"
case_ detrun_lib_nomarker    3 "did not set the marker" "" DET_BOX_LIB="$FX/lib_nomarker.sh" DET_BOX_LIB_SHA="$(sha256sum < "$FX/lib_nomarker.sh" | cut -d' ' -f1)" -- "${DR[@]}"
case_ detrun_lib_version     3 "is version '1'" "" DET_BOX_LIB="$FX/lib_oldversion.sh" DET_BOX_LIB_SHA="$(sha256sum < "$FX/lib_oldversion.sh" | cut -d' ' -f1)" -- "${DR[@]}"
case_ detrun_lib_noguard     3 "does not define box_guarded" "" DET_BOX_LIB="$FX/lib_noguard.sh" DET_BOX_LIB_SHA="$(sha256sum < "$FX/lib_noguard.sh" | cut -d' ' -f1)" -- "${DR[@]}"
case_ detrun_lib_unreadable  3 "is not a readable file" "" DET_BOX_LIB="$FX/proc" -- "${DR[@]}"
case_ detrun_clean_selfclaim 0 "det_run preflight ok (shared, :141, engineperf)" "REFUSED" -- "${DR[@]}"
case_ detrun_no_display      2 "REFUSED: BENCH_DISPLAY '' is not :141" "" BENCH_DISPLAY= -- "${DR[@]}"
case_ detrun_display77       2 "REFUSED: BENCH_DISPLAY ':77' is not :141" "" BENCH_DISPLAY=:77 -- "${DR[@]}"
case_ detrun_display_busy    1 "REFUSED: display_free: display :141 exists" "" BOX_XROOT="$FX/xbusy" -- "${DR[@]}"
case_ detrun_tag_dotdot      2 "REFUSED: tag 'x/../../run' is not [a-z0-9_]+" "" -- bash "$DETRUN" --preflight-only Fixture.exe 'x/../../run'
case_ detrun_tag_glob        2 "REFUSED: tag" "" -- bash "$DETRUN" --preflight-only Fixture.exe 'a*'
case_ detrun_tag_empty       2 "REFUSED: tag" "" -- bash "$DETRUN" --preflight-only Fixture.exe ''
case_ detrun_tag_upper_ok    0 "det_run preflight ok" "" -- bash "$DETRUN" --preflight-only Fixture.exe St_Fixture2
case_ detrun_exe_path        2 "REFUSED: exe" "" -- bash "$DETRUN" --preflight-only '../x.exe' st_fixture
case_ detrun_save_bad        2 "REFUSED: BENCH_SAVE" "" BENCH_SAVE='bench1/../x' -- "${DR[@]}"
case_ detrun_noclaim_no_token 2 "DET_NOCLAIM=1 needs DET_BOX_TOKEN" "" DET_NOCLAIM=1 -- "${DR[@]}"
case_ detrun_noclaim_not_held 2 "no live claim under 'engineperf' carries 'TOKMINE'" "" DET_NOCLAIM=1 DET_BOX_TOKEN=TOKMINE -- "${DR[@]}"
case_ detrun_noclaim_mine    0 "det_run preflight ok" "REFUSED" DET_NOCLAIM=1 DET_BOX_TOKEN=TOKMINE BOX_QUIET_DIR="$FX/q_mine" -- "${DR[@]}"
case_ detrun_noclaim_other   1 "REFUSED: BOX REFUSED: own_claim:" "" DET_NOCLAIM=1 DET_BOX_TOKEN=TOKMINE BOX_QUIET_DIR="$FX/q_mine_and_other" -- "${DR[@]}"
case_ detrun_noclaim_nested  1 "REFUSED: BOX REFUSED: own_claim:" "" DET_NOCLAIM=1 DET_BOX_TOKEN=TOKMINE BOX_QUIET_DIR="$FX/q_nested" -- "${DR[@]}"
case_ detrun_selfclaim_other 1 "REFUSED: BOX REFUSED: own_claim:" "" BOX_QUIET_DIR="$FX/q_mine" -- "${DR[@]}"
case_ detrun_lock            1 "REFUSED: BOX REFUSED: play_lock:" "" BOX_PLAY_LOCK="$FX/lock_live" -- "${DR[@]}"
case_ detrun_title_fails     2 "REFUSED: BOX REFUSED: protected_title:" "" BOX_TITLE_CHECK="$FX/bin/title_fail" -- "${DR[@]}"
case_ detrun_title_linger    1 "REFUSED: BOX REFUSED: protected_title: a protected title was seen" "" BOX_PSEEN="$FX/pseen_recent" -- "${DR[@]}"
case_ detrun_ptc_env         2 "PTC_INTERVAL" "" PTC_INTERVAL=0 -- "${DR[@]}"
case_ detrun_roblox_cgroup   1 "REFUSED: BOX REFUSED: roblox:" "" BOX_PROC="$FX/proc_sober_cg" -- "${DR[@]}"
case_ detrun_engine_before   1 "REFUSED: BOX REFUSED: engine_running:" "" BOX_PGREP="$FX/bin/pgrep_engine" -- "${DR[@]}"
case_ detrun_exclusive       1 "REFUSED: BOX REFUSED: exclusive:" "" BOX_QUIET_DIR="$FX/q_excl" -- "${DR[@]}"
case_ detrun_exclusive_class 0 "det_run preflight ok (exclusive" "" DET_BOX_CLASS=exclusive BOX_QUIET_DIR="$FX/q_excl" -- "${DR[@]}"
case_ detrun_class_bogus     2 "REFUSED: DET_BOX_CLASS 'EXCL'" "" DET_BOX_CLASS=EXCL -- "${DR[@]}"
case_ detrun_wrote_nothing   0 "" "" -- bash -c '! ls -d "$0"/out/det_st_fixture "$0"/out/det_st_fixture2 2>/dev/null' "$(cd "$C/../../.." && pwd)"

echo "shared file sha256 $LIBSHA"
echo "BOX SELFTEST: $([ $ok = $n ] && echo PASS || echo FAIL) ($ok/$n)"
echo "output folder: $OUT ($(du -sh "$OUT" | cut -f1))"
