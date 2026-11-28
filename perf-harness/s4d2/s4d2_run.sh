#!/usr/bin/env bash
# DRAFT - NOT RUN, NOT REGISTERED. s4d2 series driver (PREREG-s4d2-npc-pose-DRAFT.md sections 2.1, 3, 4, 6; requirement
# R2 of 1.4). Written 2026-11-27 at the desk, revised the same day after the review
# plan-facts/stage4d2-w1-and-runscript-review-2026-11-27.md (B2-1, S2-1..S2-8, N2-1..N2-8) and the supervisor's order
# to take the box checks from ONE shared file. Only `bash -n` has been run on it, plus its `--preflight-only` box mode
# inside common/box_preflight_selftest.sh (fixtures only).
#
#   s4d2_run.sh --preflight-only            box checks ONLY: source common/box_preflight.sh, run box_preflight_all
#                                           (shared, :141, engineperf), print, exit 0 clear | 1 refused | 2 refused,
#                                           a check could not tell | 3 the shared file is missing, unreadable, not the
#                                           pinned file, or lacks its marker / version / functions. Touches nothing.
#   s4d2_run.sh probe   [--preflight-only]  one boot: s4p_probe (NPC = the Duty walker)
#   s4d2_run.sh probe_b [--preflight-only]  the one allowed re-probe (prereg 2.1), only after a probe whose reading
#                                           STOPped on `npc` and/or `attach` alone: s4p_probe again, fallback NPC
#   s4d2_run.sh probe_fix <n> [--preflight-only]   re-probe n (1-9) after a code or stimulus fix (prereg 2.1)
#   s4d2_run.sh series  [--preflight-only]  the 16 scored boots (part A, part S)
# "<mode> --preflight-only" runs every start check of that mode (box checks, pins, placeholders, folders, readings
# chain) and exits 0 / 2 before any state folder, queue entry or claim.
#
# Probe attempts and their reading files (the scorer never overwrites an out file, and always reads det_s4p_probe):
#   probe          -> boots s4p_probe; read by hand into s4d2/probe_readings.txt
#   probe_b        -> needs probe_readings.txt ending in "S4D2 PROBE: STOP (<n> rules)" whose PROBE STOP codes are only
#                     npc / attach, and no probe_readings_b.txt; renames out/det_s4p_probe -> out/det_s4p_probe_a
#                     (AFTER the gates of the boot passed, S2-5), boots s4p_probe with esc_2_12_stalker_fanat;
#                     read into s4d2/probe_readings_b.txt
#   probe_fix <n>  -> the documented manual step first: write s4d2/probe_fix<n>.txt (which stop, what was changed, the
#                     new pins) and edit the pin lines below by hand for any changed file (exe, stimulus). Needs the
#                     previous attempt's reading ending in STOP and no probe_readings_f<n>.txt; renames
#                     out/det_s4p_probe -> out/det_s4p_probe_before_f<n> (after the gates), boots s4p_probe with the
#                     previous attempt's NPC; read into s4d2/probe_readings_f<n>.txt
#   series         -> reads the LATEST reading of the chain (probe_readings_f<highest n>.txt, else _b, else the first);
#                     its LAST line must be exactly "S4D2 PROBE: GO" (CR stripped), every earlier one must end in STOP
# Made from tools/s4c_partA.sh (flag preflight, cfg0) and sf1/sf1_run.sh (pins, display :141, stop record, one way
# out, child kill, signal hold around the claim, options restore, notify), kj4p_run.sh (own-claim token, release by
# claim-id), with det_run.sh as the per-boot runner (DET_NOCLAIM=1: this driver holds the claim; DET_BOX_TOKEN = this
# run's claim token, so det_run.sh's own shared box checks accept this claim and refuse any other one).
#
# BOX SAFETY (review B2-1, S2-1; supervisor's order): every check of Caden's play lock, a protected title, Sober,
# an engine of the run tree, another claim under our name, EXCLUSIVE in the claim files and display :0 comes from
# common/box_preflight.sh (pinned below by full sha256; refused if missing, unreadable, unpinned, or without its marker,
# version or functions). It is run fail-closed: before the start, before every claim attempt, before every boot
# (twice: before the claim renewal and again last, after the options restore), and every ~5 s while a boot runs
# (all but engine_running). The driver never starts a boot, moves a log or dump, or writes the options file while any
# refuses; it never deletes anything (no rm in this file); it releases only the claim it made (by its claim-id; by
# name only when the claim carrying this run's token is the only one under our name); every exit path (normal, stop,
# error, TERM, INT, HUP) goes through finish(), which kills and reaps only its own child (det_run.sh, then KILL after
# 180 s, CHILD_GRACE_S) and its own sleep. Exit-path table: s4d2/NOTES.md section 9.
# 2026-11-28 (plan-facts/box-preflight-second-review-2026-11-27.md): box_preflight.sh v2 (claims read from the claim
# tool's files, no `check` call; Sober by its flatpak; the title's 180-s linger; PTC_* refused). Each of this driver's
# own writes under run/ (the options copy, the witness moves) runs inside box_guarded: the box is checked again on the
# line before (B-1), and after a refusal nothing more under run/ is touched (B-2; det_run.sh does the same on a stop or
# a signal and lists what it left). After det_run.sh ends or is KILLed, no engine of the run tree may still use
# run/prefix: wineserver -k there (box_wine_kill), a 60-s wait from date, and if one is still left the claim is NOT
# released and the pids are logged (S-2). The queue entry: see want_call (S-3; a known limit, common/README.md).
# done.txt lists completed boots only; boots.txt lists every boot with its rc (N-13).
# 2026-11-28 (plan-facts/s4d1-driver-review-2026-11-28.md, the faults it found here too): S-1 one claim, then `renew`
# by its id, finish() releases every id obtained; S-2 a lost claim stops (box_gate, watch_boot); S-3 engines_gone right
# after det_run.sh returns; N-1 signals held over the launch until BP is set; N-5 the wait deadline before every claim.
# Re-check 2026-11-28 (same file, "Re-check after fixes", R-1): the id every renewal prints must be CLAIM_ID0, else a
# box stop; finish() releases ONLY CLAIM_ID0; CLAIM_MIN = s4d1's renewal-to-renewal sum with s4d2's own terms
# (BENCH_TIMEOUT + 1586 s, probe 60 min, refused above 180 min; was timeout + 10 min); watch_boot's wall cap
# BENCH_TIMEOUT + WATCH_EXTRA_S 1020 s (class fault). CHILD_GRACE_S 180 unchanged.
# 2026-11-28 (plan-facts/box-preflight-third-review-2026-11-28.md): U-1 cfg0 reads out/user_det.ltx.orig (the bytes the
# engine runs with; a missing sim_tick = the engine default 0); S3-2 the physical project root must be
# $HOME/Projects/StalkerGameEngine; S3-7 CHILD_GRACE_S 180; N3-4 axr_restore returns a code (no subshell); N3-6
# BENCH_SAVE=bench1 passed to det_run.sh; box_preflight_all is run in this shell (BOX_OUT), so a title sighting is kept
# for 180 s (S3-5).
#
# Other rules (the brief of 2026-11-27 and prereg 6):
#   - display :141 only (BENCH_DISPLAY must be unset or :141); the display must be free before every boot;
#   - every pinned file is checked before the first boot and before every boot, by name; refused while any PIN_ME_
#     remains, and in series mode while any REG_ME_ remains;
#   - stops on the first crash dump, timeout, box condition, failed pre-check, failed in-run check, missing file, or a
#     boot whose stimulus took no item (or a different item than the first boot of this run);
#   - a crash dump already in run/appdata/logs before a boot refuses that boot (N2-4: it would be blamed on it);
#   - at most ONE retry in the whole series, `<tag>_r1`, only for det_run.sh rc 4 with no crash dump (prereg 6.5);
#   - after every boot: the witness run/appdata/logs/synth_<tag>.txt is moved into out/det_<tag>/ (det_run.sh does
#     not, its lines 44-47), only while no play lock is live; the folder must hold xray.log, det_alife_counts and
#     det_counters at cp1, cp2 and cp3 (the scorer's C reads all six, s4d2_score.py C3), and a lever boot's witness;
#   - the claim wait is capped at WAIT_MAX_S (6 h, then class=box), and `want` is re-sent on every poll (S2-7);
#   - the plan is read on fd 3 and every child gets stdin from /dev/null (S2-4);
#   - all times from `date`; no `rm` anywhere in this file.
# OPTIONS FILE (N2-7): before every boot, with the claim held and the box checks clear, run/appdata/axr_options_play.ltx
#   is hashed against AXR_SHA (refused: fault stop, destination untouched), copied over
#   run/gamedata/configs/axr_options.ltx (refused if that is a link or not a plain file inside run/), and the
#   destination hashed (sf1_run.sh, em1/t2_xray_em1.sh:57-70). Why: each exe rewrites that file's version line when it
#   boots (wu3 RESULT; kj4p RESULT found Stance14's rewrite), and s4d2 alternates Stance14, build 2 and build 1, so
#   without it the arms of a pair would not start from the same bytes. Never done while the play lock is live
#   (Caden's launcher copies his own options over that file while he plays).
# NOTIFY (N2-7): finish() sends one line to the Overseer (notify-overseer.sh engineperf -, timeout 60), as sf1: the
#   series runs for 1.5 h unattended, and a stop must reach the supervisor without anyone polling.
# Not scored here. Probe reading and scoring are run by hand, once each:
#   python3 -I wt-render-split/perf-harness/s4d2/s4d2_score.py mode=probe root=out out=<the attempt's reading file above>
#   python3 -I wt-render-split/perf-harness/s4d2/s4d2_score.py root=out out=wt-render-split/perf-harness/s4d2/s4d2_score_result.txt
set -u
cd "$(dirname "$0")/../../.." || { echo "REFUSED: cannot enter the project root"; exit 2; }   # project root
# S3-2 (third review): the engine checks and PFX below use the physical tree; it must be the one tree they look at
[ "$(pwd -P)" = "$HOME/Projects/StalkerGameEngine" ] || { echo "REFUSED: the project root $(pwd -P) (physical) is not $HOME/Projects/StalkerGameEngine"; exit 2; }
T="$HOME/.claude/overseer/tools"; W=engineperf
S4=wt-render-split/perf-harness/s4d2; ST=out/s4d2_state; LOGS=run/appdata/logs; CFG=run/appdata/user.ltx

# ---- the shared box checks (pinned; S4D2_BOX_LIB / S4D2_BOX_LIB_SHA are honoured ONLY by the two preflight-only
# ---- forms, for common/box_preflight_selftest.sh; a real run refuses while either, or any BOX_* input, is set)
BOXLIB_DEF=wt-render-split/perf-harness/common/box_preflight.sh
BOXLIB_SHA=f8026040f2f9b3f6cd5c7c33b11062f7efc1ed726af03651ce16bbb0fccd53ef   # common/box_preflight.sh v2, third-review revision 2026-11-28 (was f3ad9351..., v1 420c5f2a...)
BOXLIB_MARKER=engineperf-box-preflight-v2; BOXLIB_VERSION=2

# ---- pins (PIN_ME_: before ANY boot, the probe included; prereg 1.2, 1.4 items 3-5) ----
EXE2=SimTick19.exe            # build 2 exe in run/bin CI run 46116095777 of perf/tick-4d 555771502 (pinned 2026-11-28)
EXE2_SHA=aa54104afb5734a27f42bce90264b09c2d3b15e9f215edeff90c72c13eba96a8              # its sha256 (64 hex)
EXE1=SimTick18.exe            # build 1 exe in run/bin (s4d1's SimTick18.exe), 7b1232ffc or 6acb77044
EXE1_SHA=40d2d37d810bdd9fdf0a19a094dea621a16a2f164fdb1e1b9d1b209d5a6b09d4           # its sha256
STOCK=Stance14.exe; STOCK_SHA=b4ce9c772ae5fa4bf5ddb955f2dfcb943627d6c2b2e4ce66e21eac1168646cbe      # full sha256 (N2-5). Read 2026-11-27 22:40:
                                                    # b4ce9c772ae5fa4bf5ddb955f2dfcb943627d6c2b2e4ce66e21eac1168646cbe
STIM=run/gamedata/scripts/zz_eperf_synth.script     # installed stimulus = harness/zz_eperf_synth.script (with -det_synth_item)
STIM_SHA=1777fb6865cc5b56c80f197f3f95a250104d6c90ce0b8b2a35422e3bed39ff95
USERDET=out/user_det.ltx.orig; USERDET_SHA=3e0e0a6f1fe8010cd727a8f4e718b0486711fbcf8e4f7bfc8eb769d047411b81   # full sha256 (N2-5). Read 2026-11-27 22:40:
                                                    # 3e0e0a6f1fe8010cd727a8f4e718b0486711fbcf8e4f7bfc8eb769d047411b81
DETRUN=tools/det_run.sh; DETRUN_SHA=e29800949b503a7f61a5f9b3a9a2289cdd01424fa87f3a55dacac464fa578f48   # 2026-11-28 third review (was 0b7a6edb..., da66e107..., 01fb40dc...)
PIDLIB=tools/lib_engine_pids.sh; PIDLIB_SHA=cb183b7b894eab3edb200e2c03a3a96f2e935cd2d1e1f402f48be7aad238ed6e
NOGUARD=tools/run_noguard.sh; NOGUARD_SHA=92729abc420784053cee4b1b1832f5d112f74979a4760e2173d45578358455d5
SAVE_P=run/appdata/savedgames/bench1.scop; SAVE_P_SHA=891693c5e318ad1e97e8437476654200eb39d3bab89de79a778fa9b90ff89ffa  # S2-8 (sf1:192)
SAVE_C=run/appdata/savedgames/bench1.scoc; SAVE_C_SHA=7c2ddf33ccdaa75c341ba8a7c3cd84cb454972e81a47ce844d6919cb2a2f167e  # S2-8 (sf1:193)
AXR_SRC=run/appdata/axr_options_play.ltx; AXR_DST=run/gamedata/configs/axr_options.ltx
AXR_SHA=27d54b9f59404d224258649598ca604ebfc174dd066be4a96ff392323244fb29   # sf1's registered value; matches 2026-11-27 22:40
# ---- registration values (REG_ME_: series mode only; prereg 2.3, 8) ----
NPC=REG_ME_PROBE_NPC             # the NPC the GO probe named (its pose line)
TIMEOUT=REG_ME_BENCH_TIMEOUT     # max(2000, ceil(5 x PROBE_BOOT_S / 60) x 60)
SCORER_SHA=REG_ME_SCORER_SHA     # sha256 of s4d2_score.py as registered (CONST filled)
SELFTEST_SHA=REG_ME_SELFTEST_SHA # sha256 of s4d2_selftest.sh as registered
# ---- fixed by the prereg ----
NPC_PROBE=sim_default_duty_428175; NPC_PROBE_B=esc_2_12_stalker_fanat; TIMEOUT_PROBE=2000
BASE="-det_passes 100,200,320 -det_lua_streams -det_counters"
ITEM="-det_synth_item"           # on EVERY boot, both arms of every pair, part A and the probe included (prereg 2.1, 3, 4)
REF="-det_passes 50,600,1650 -det_save -det_lua_streams -det_counters"   # the 11-19 counter method (s4c_partA.sh:10-16)
ALLOWED="-:50,600,1650 -:-det_save +:100,200,320"
NTOK_B2=138; NTOK_B1=58          # witness tokens per line: build 2 with W1+W2, build 1 (prereg 1.1)
DISP=:141
WAIT_MAX_S=21600; POLL_S=60      # claim wait cap 6 h (S2-7), poll as sf1
CHILD_GRACE_S=180                # det_run.sh gets this long after TERM before KILL (S3-7). Its cleanup, worst case: wineserver
                                 # -k 30 s + -w 30 s (box_wine_kill's timeouts), then the 60-s wait for no engine in
                                 # run/prefix (its repeated -k is bounded inside the 60 s), its own Xvfb/openbox, the
                                 # claim-tool release (not bounded; normally < 1 s): 30 + 30 + 60 + release + margin. After
                                 # a TERM the user.ltx restore is skipped (a signal is a stop), so no box_fresh is in it.
WATCH_EXTRA_S=1020               # re-check R-1 (2026-11-28), s4d1's N4-11 cap derived for s4d2's boots: watch_boot stops
                                 # det_run.sh (class fault) once it has run BENCH_TIMEOUT + this. det_run.sh's time outside
                                 # its LIMIT (which starts at the engine start, det_run.sh START), worst case by its own
                                 # bounds, for s4d2's call (DET_NOCLAIM=1, so no claim; no -det_save in BASE, ITEM or any
                                 # lever, so no save wait). Before the engine start: box_preflight_all <= 40 s, box_fresh
                                 # <= 40, Xvfb socket 10 + 0.5 + settle 1, wmctrl 5 + openbox 30 x (5 + 0.5) = 165, three
                                 # guarded steps (box_fresh 40 each) 120: <= 382 s + run_noguard.sh's find (seconds [G]).
                                 # After LIMIT: one loop pass late (sleep 5 + inboot <= 30) and, if the cp3 dump landed in
                                 # that pass, waitchk 8 (+ one inboot 30 + 1) = 74; stop_wine -k 30 + -w 30 + the 60-s wait
                                 # with its 10-s `-k nowait` + 1 = 131; collect guarded 40 + moves ~5; the user.ltx restore
                                 # guarded 40 + 1; own openbox / Xvfb kill < 5: <= 296 s. Sum ~678 s; 1020 = 1.5x
WATCH_NAP_S=5                    # box checks during a boot: every 5 s, the rate of det_run.sh's own inboot (det_run.sh:244)
WATCH_CHK_S=30                   # one watch pass's checks: title 10 + Sober flatpak 10 + systemctl 10 (box_preflight.sh
                                 # timeouts); the play lock, claim and EXCLUSIVE tests read files
ENG_GONE_S=60                    # engines_gone: after box_wine_kill (-k 30 + -w 30) the engines of run/prefix must be gone
                                 # within the same 60 s det_run.sh's stop_wine allows (det_run.sh:156)

usage() { echo "usage: $0 --preflight-only | probe|probe_b|series [--preflight-only] | probe_fix <1-9> [--preflight-only]"; exit 2; }
MODE=${1:-}; FIXN=""; PRE=""
case "$MODE" in
  --preflight-only) [ $# = 1 ] || usage ;;
  probe|probe_b|series) PRE=${2:-}; [ $# -le 2 ] || usage ;;
  probe_fix) FIXN=${2:-}; PRE=${3:-}; [ $# -ge 2 ] && [ $# -le 3 ] || usage
             case "$FIXN" in [1-9]) ;; *) usage ;; esac ;;
  *) usage ;;
esac
case "$PRE" in ''|--preflight-only) ;; *) usage ;; esac
[ "${BENCH_DISPLAY:-:141}" = ":141" ] || { echo "REFUSED: BENCH_DISPLAY '${BENCH_DISPLAY}' is not :141"; exit 2; }

# ---- load the shared file (refuse by name; 3 in the box-only mode, 2 otherwise) ----
LIBRC=2; [ "$MODE" = --preflight-only ] && LIBRC=3
libfail() { echo "REFUSED: box_preflight: $*"; exit $LIBRC; }
if [ "$MODE" != --preflight-only ] && [ "$PRE" != --preflight-only ]; then
  [ -n "${S4D2_BOX_LIB+x}${S4D2_BOX_LIB_SHA+x}" ] && libfail "S4D2_BOX_LIB / S4D2_BOX_LIB_SHA are set: only a preflight-only call may override the shared file"
fi
BOXLIB=${S4D2_BOX_LIB:-$BOXLIB_DEF}; BOXSHA=${S4D2_BOX_LIB_SHA:-$BOXLIB_SHA}
{ [ -e "$BOXLIB" ] || [ -L "$BOXLIB" ]; } || libfail "missing file $BOXLIB"
{ [ -f "$BOXLIB" ] && [ -r "$BOXLIB" ]; } || libfail "$BOXLIB is not a readable file"
_got=$(sha256sum < "$BOXLIB" 2>/dev/null | cut -d' ' -f1)
[ "$_got" = "$BOXSHA" ] || libfail "$BOXLIB is not the pinned file (${BOXSHA:0:16}; it is ${_got:0:16})"
unset BOX_PREFLIGHT_MARKER BOX_PREFLIGHT_VERSION
# shellcheck source=../common/box_preflight.sh
. "$BOXLIB" || libfail "sourcing $BOXLIB failed"
[ "${BOX_PREFLIGHT_MARKER:-}" = "$BOXLIB_MARKER" ] || libfail "$BOXLIB did not set the marker $BOXLIB_MARKER"
[ "${BOX_PREFLIGHT_VERSION:-}" = "$BOXLIB_VERSION" ] || libfail "$BOXLIB is version '${BOX_PREFLIGHT_VERSION:-none}', not $BOXLIB_VERSION"
for _f in box_preflight_all box_ok_play_lock box_ok_title box_ok_roblox box_ok_engine box_ok_claims box_ok_exclusive \
          box_ok_display box_ok_display_free box_claim_has_token box_queue_count box_engines_in_prefix box_wine_kill \
          box_guarded box_fresh; do
  declare -F "$_f" >/dev/null || libfail "$BOXLIB does not define $_f"
done
declare -p BOX_QR_UNSET >/dev/null 2>&1 || libfail "$BOXLIB does not define BOX_QR_UNSET"
if [ "$MODE" = --preflight-only ]; then
  box_preflight_all shared "$DISP" "$W" >/dev/null; _r=$?; _o=$BOX_OUT
  [ -n "$_o" ] && echo "$_o"
  [ $_r = 0 ] && echo "box preflight ok (box_preflight.sh v$BOX_PREFLIGHT_VERSION)"
  exit $_r
fi
if [ "$PRE" != --preflight-only ] && [ -n "$BOX_OVERRIDES" ]; then
  echo "REFUSED: box_preflight inputs overridden ($BOX_OVERRIDES): only a preflight-only call may run with fixtures"; exit 2
fi

bad=0
refuse() { echo "REFUSED: $*"; bad=1; }
# the box checks first, by name (they are also re-run before the claim and before every boot)
box_preflight_all shared "$DISP" "$W" >/dev/null; rr=$?; r=$BOX_OUT   # not in $(...): a title sighting stays (S3-5)
[ $rr = 0 ] || { echo "$r" | grep -v '^BOX OVERRIDES' | sed 's/^/REFUSED (box): /'; bad=1; }
[ -n "$BOX_OVERRIDES" ] && echo "note: box_preflight inputs overridden ($BOX_OVERRIDES): preflight-only"
for v in EXE2 EXE2_SHA EXE1 EXE1_SHA STIM_SHA STOCK_SHA USERDET_SHA; do
  case "${!v}" in PIN_ME_*) refuse "$v is still the placeholder ${!v}" ;; esac
done
if [ "$MODE" = series ]; then
  for v in NPC TIMEOUT SCORER_SHA SELFTEST_SHA; do
    case "${!v}" in REG_ME_*) refuse "$v is still the placeholder ${!v} (set at registration)" ;; esac
  done
  case "$TIMEOUT" in ''|*[!0-9]*) refuse "TIMEOUT '$TIMEOUT' is not a number" ;; *) [ "$TIMEOUT" -ge 2000 ] || refuse "TIMEOUT $TIMEOUT < 2000 (prereg 6.3)" ;; esac
  case "$NPC" in "$NPC_PROBE"|"$NPC_PROBE_B") ;; REG_ME_*) ;; *) refuse "NPC '$NPC' is neither probe NPC ($NPC_PROBE, $NPC_PROBE_B)" ;; esac
fi
[ "$bad" = 0 ] || exit 2

pin() {   # <file> <full sha256>: by name
  local f=$1 want=$2 got
  [ -e "$f" ] || { echo "REFUSED: missing file $f"; return 1; }
  got=$(sha256sum < "$f" 2>/dev/null | cut -d' ' -f1)
  [ "$got" = "$want" ] || { echo "REFUSED: $f is not ${want:0:16} (it is ${got:0:16})"; return 1; }
}
cfgval() { awk -v k="$1" '{gsub("\r","")} $1==k{v=$2; f=1} END{if (f) print v; else print "<absent>"}' "$2" 2>/dev/null; }   # <key> <file>
cfg0() {   # tools/s4c_partA.sh:23, review U-1: the settings the engine RUNS with have both switches at 0. That is
  # out/user_det.ltx.orig (pinned: USERDET_SHA), which det_run.sh copies over run/appdata/user.ltx right before the
  # engine start; run/appdata/user.ltx itself is not what the engine reads during the boot. A missing sim_tick line
  # reads as 0: the engine's default is 0, wt-simtick/src/xrEngine/device.cpp:465 (`int g_sim_tick_hz = 0;`), console
  # command registered at wt-simtick/src/xrEngine/xr_ioc_cmd.cpp:1362 (`CMD4(CCC_Integer, "sim_tick", &g_sim_tick_hz,
  # 0, 240)`); the only other setter is the `-sim_tick <hz>` start switch (device.cpp:618-622), which no boot of this
  # driver passes (BASE, ITEM, lever). r__framelimit must be present and 0 (it is, line 553).
  local v
  [ -s "$USERDET" ] || { echo "REFUSED: missing file $USERDET"; return 1; }
  v=$(cfgval sim_tick "$USERDET"); [ "$v" = "<absent>" ] && v=0
  [ "$v" = 0 ] || { echo "REFUSED: sim_tick is '$v' in $USERDET, not 0"; return 1; }
  v=$(cfgval r__framelimit "$USERDET")
  [ "$v" = 0 ] || { echo "REFUSED: r__framelimit is '$v' in $USERDET, not 0"; return 1; }
}
flags_ok() {   # s4c_partA.sh:10-16: BASE differs from the 11-19 method exactly by ALLOWED
  local d
  d=$( { for w in $REF; do case " $BASE " in *" $w "*) ;; *) echo "-:$w";; esac; done
         for w in $BASE; do case " $REF " in *" $w "*) ;; *) echo "+:$w";; esac; done; } | tr '\n' ' ' )
  [ "$(echo $d)" = "$ALLOWED" ] || { echo "REFUSED: base flags differ from the 11-19 method in an unlisted way: [$d] allowed [$ALLOWED]"; return 1; }
}
pins_all() {   # every pin, by name; 0 = all good
  local f=0
  pin "$BOXLIB" "$BOXSHA" || f=1
  pin "$DETRUN" "$DETRUN_SHA" || f=1
  pin "$PIDLIB" "$PIDLIB_SHA" || f=1
  pin "$NOGUARD" "$NOGUARD_SHA" || f=1
  pin "run/bin/$EXE2" "$EXE2_SHA" || f=1
  pin "$STIM" "$STIM_SHA" || f=1
  pin "$USERDET" "$USERDET_SHA" || f=1
  pin "$SAVE_P" "$SAVE_P_SHA" || f=1
  pin "$SAVE_C" "$SAVE_C_SHA" || f=1
  pin "$AXR_SRC" "$AXR_SHA" || f=1
  [ -e out/user.ltx.orig ] || { echo "REFUSED: missing file out/user.ltx.orig (det_run.sh restores it)"; f=1; }
  if [ "$MODE" = series ]; then
    pin "run/bin/$EXE1" "$EXE1_SHA" || f=1
    pin "run/bin/$STOCK" "$STOCK_SHA" || f=1
    pin "$S4/s4d2_score.py" "$SCORER_SHA" || f=1
    pin "$S4/s4d2_selftest.sh" "$SELFTEST_SHA" || f=1
  fi
  return $f
}
dumps_left() { ls "$LOGS"/*.mdmp >/dev/null 2>&1; }   # N2-4: a dump already in the logs folder

# ---- probe readings chain (S2-6, mismatches 1-3) ----
last_line() { tr -d '\r' < "$1" 2>/dev/null | awk 'NF{l=$0} END{print l}'; }   # last non-empty line, CR stripped
ends_stop() { last_line "$1" | grep -Eqx 'S4D2 PROBE: STOP \([0-9]+ rules\)'; }
ends_go() { [ "$(last_line "$1")" = "S4D2 PROBE: GO" ]; }
stop_codes() { tr -d '\r' < "$1" 2>/dev/null | sed -n 's/^PROBE STOP \([a-z0-9_]*\): .*$/\1/p' | sort -u | tr '\n' ' '; }
RD0=$S4/probe_readings.txt; RDB=$S4/probe_readings_b.txt
rdf() { echo "$S4/probe_readings_f$1.txt"; }
chain() {   # the reading files that exist, in attempt order
  [ -e "$RD0" ] && echo "$RD0"; [ -e "$RDB" ] && echo "$RDB"
  local n; for n in 1 2 3 4 5 6 7 8 9; do [ -e "$(rdf $n)" ] && echo "$(rdf $n)"; done; }
prev_npc() {   # the NPC of the latest attempt so far (probe_b switched to the fallback)
  [ -e "$RDB" ] && echo "$NPC_PROBE_B" || echo "$NPC_PROBE"; }

# ---- the boots: tag|exe|lever (prereg 3 and 4; order 6.1) ----
lever() {   # <K> <mask> <npc> <extra...>
  local k=$1 m=$2 npc=$3; shift 3
  echo "-det_synth_k $k -sim_4d_mask $m -det_synth_tw -det_synth_walk -det_synth_tracer -det_synth_pose -det_synth_pose_npc $npc${*:+ $*}"
}
plan() {   # prints the boot list of this mode, one "tag|exe|lever-or-empty" per line
  case "$MODE" in
    probe)     echo "s4p_probe|$EXE2|$(lever 3 23 $NPC_PROBE)" ;;
    probe_b)   echo "s4p_probe|$EXE2|$(lever 3 23 $NPC_PROBE_B)" ;;
    probe_fix) echo "s4p_probe|$EXE2|$(lever 3 23 "$(prev_npc)")" ;;
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
case "$MODE" in   # where a re-probe moves the previous probe folder (inside the boot loop, after the gates)
  probe_b) MOVE_TO=out/det_s4p_probe_a ;; probe_fix) MOVE_TO=out/det_s4p_probe_before_f$FIXN ;; *) MOVE_TO="" ;;
esac
case "$MODE" in   # the reading file the next by-hand read must write
  probe) RD_NEXT=$RD0 ;; probe_b) RD_NEXT=$RDB ;; probe_fix) RD_NEXT=$(rdf "$FIXN") ;; *) RD_NEXT="" ;;
esac

# ---- preflight (nothing below this block boots anything until it passed) ----
flags_ok || bad=1
cfg0 || bad=1
pins_all || bad=1
box_ok_display_free "$DISP" || refuse "$BOX_WHY"
dumps_left && refuse "a crash dump is already in $LOGS ($(ls "$LOGS"/*.mdmp 2>/dev/null | head -3 | tr '\n' ' ')): not this run's; not moved"
SD=$ST/$MODE$FIXN                # this attempt's state folder: out/s4d2_state/<probe|probe_b|probe_fix<n>|series>, new
[ -e "$SD" ] && refuse "$SD exists (the state folder of $MODE$FIXN must be new)"
if [ "$MODE" = series ]; then TIMEOUT_RUN=$TIMEOUT; else TIMEOUT_RUN=$TIMEOUT_PROBE; fi
case "$MODE" in
  probe)
    [ -e out/det_s4p_probe ] && refuse "out/det_s4p_probe exists"
    for f in out/det_s4p_probe_a out/det_s4p_probe_before_f*; do [ -e "$f" ] && refuse "$f exists"; done
    [ -n "$(chain)" ] && refuse "a probe reading already exists ($(chain | tr '\n' ' ')): this is not the first probe" ;;
  probe_b)
    [ -d out/det_s4p_probe ] || refuse "missing folder out/det_s4p_probe (the first probe)"
    [ -e out/det_s4p_probe_a ] && refuse "out/det_s4p_probe_a exists: the one re-probe was used"
    [ -e "$RD0" ] || refuse "missing file $RD0 (the first probe's reading)"
    [ -e "$RDB" ] && refuse "$RDB exists: the one re-probe was used"
    [ "$(chain)" = "$RD0" ] || refuse "the re-probe comes right after the first probe only (readings: $(chain | tr '\n' ' '))"
    ends_stop "$RD0" || refuse "$RD0 does not end in 'S4D2 PROBE: STOP (<n> rules)' (last line: '$(last_line "$RD0")')"
    for c in $(stop_codes "$RD0"); do case "$c" in npc|attach) ;; *) refuse "$RD0 stops on '$c': the re-probe is allowed only for npc / attach (prereg 2.1)" ;; esac; done
    [ -n "$(stop_codes "$RD0")" ] || refuse "$RD0 names no PROBE STOP code" ;;
  probe_fix)
    [ -d out/det_s4p_probe ] || refuse "missing folder out/det_s4p_probe (the previous attempt)"
    [ -e "$MOVE_TO" ] && refuse "$MOVE_TO exists"
    [ -s "$S4/probe_fix$FIXN.txt" ] || refuse "missing file $S4/probe_fix$FIXN.txt (the written fix: which stop, what changed, the new pins)"
    [ -e "$(rdf "$FIXN")" ] && refuse "$(rdf "$FIXN") exists: re-probe $FIXN was read already"
    if [ "$FIXN" -gt 1 ]; then
      [ -e "$(rdf $((FIXN - 1)))" ] || refuse "missing file $(rdf $((FIXN - 1))) (re-probe $((FIXN - 1)) must be read first)"
    fi
    _prev=$(chain | tail -1)
    [ -n "$_prev" ] || refuse "no probe reading exists yet"
    [ -n "$_prev" ] && { ends_stop "$_prev" || refuse "$_prev does not end in STOP: there is nothing to fix"; } ;;
  series)
    _last=$(chain | tail -1)
    if [ -z "$_last" ]; then refuse "missing file $RD0 (no probe reading, prereg 2.1)"; else
      ends_go "$_last" || refuse "$_last (the latest probe reading) does not end in 'S4D2 PROBE: GO' (last line: '$(last_line "$_last")')"
      for f in $(chain | sed '$d'); do ends_stop "$f" || refuse "$f is an earlier attempt but does not end in STOP"; done
    fi
    for t in $(plan | cut -d'|' -f1); do
      for f in out/det_$t out/det_${t}_r1 $LOGS/synth_$t.txt $LOGS/synth_${t}_r1.txt; do [ -e "$f" ] && refuse "$f exists"; done
    done ;;
esac
[ -e "$LOGS/synth_s4p_probe.txt" ] && refuse "$LOGS/synth_s4p_probe.txt exists (a probe witness was not moved)"
[ "$bad" = 0 ] || exit 2
# Claim length (re-check R-1, 2026-11-28: was one boot's timeout + 10 min, which a slow boot could outlast), extended by
# its id before every boot (gates, renew_call). s4d1's formula, each term for s4d2's own steps. It must outlast, from one
# renewal to the next (box_preflight_all and box_fresh <= 40 s each, common/box_preflight.sh timeouts):
#   GATE_AFTER_S 150   the gates after renew_call: display wait 10 x 2 s = 20 + cfg0 and pins (~1 s hashing measured
#                      2026-11-28 for the exes and saves; 10 s allowed) + box_gate 40 + the options copy's box_guarded
#                      (box_fresh) 40 + the last box_gate 40
#   boot               TIMEOUT_RUN + WATCH_EXTRA_S (the watch cap) + one watch pass that sees it, WATCH_NAP_S + WATCH_CHK_S
#   stop there         CHILD_GRACE_S + 1 (stop_child's 1-s poll) + box_wine_kill 60 + ENG_GONE_S (engines_gone)
#   AFTER_BOOT_S 80    after a normal return: the witness move's box_guarded 40 + the next gates' box_gate 40 (engines_gone
#                      after a normal return is the same 120 s as in the stop line, which is counted instead)
# Sum (stop and after-boot both counted, an upper bound): TIMEOUT_RUN + 1020 + 5 + 30 + 150 + 180 + 1 + 60 + 60 + 80 =
# TIMEOUT_RUN + 1586 s, whole minutes (probe and a 2000-s series: 3586 s -> 60 min). Refused above 180 min (the fleet's
# slice limit, as s4d1): that allows BENCH_TIMEOUT up to 9180 s, i.e. PROBE_BOOT_S up to ~1836 s.
GATE_AFTER_S=150; AFTER_BOOT_S=80
CLAIM_MIN=$(( (TIMEOUT_RUN + WATCH_EXTRA_S + WATCH_NAP_S + WATCH_CHK_S + GATE_AFTER_S + CHILD_GRACE_S + 1 + 60 + ENG_GONE_S + AFTER_BOOT_S + 59) / 60 ))
[ "$CLAIM_MIN" -le 180 ] || { echo "REFUSED: one claim period would be $CLAIM_MIN min (> 180, the fleet's slice limit): BENCH_TIMEOUT ${TIMEOUT_RUN}s is too long"; exit 2; }
[ "$PRE" = "--preflight-only" ] && { echo "preflight ok ($MODE$FIXN): $(plan | wc -l) boot(s)"; plan | cut -d'|' -f1,2 | tr '\n' ' '; echo; exit 0; }

# ---- state, log, one way out ----
mkdir -p "$ST" 2>/dev/null && mkdir "$SD" 2>/dev/null && : > "$SD/series.log" && : > "$SD/done.txt" && : > "$SD/boots.txt" && : > "$SD/claim_calls.txt" || {
  echo "REFUSED: cannot create the state folder $SD"; exit 2; }
log() { echo "$(date '+%F %T') $*" | tee -a "$SD/series.log"; }
WATCHWHY=""; sc=""; stop=""; rc=0; CLAIMTRY=0; CLAIMED=0; WANTED=0; OURQ=0; BOOTED=0; NORELEASE=0; NORELEASE_WHY=""; RETRIED=0; LIVE=""; BP=""; SP=""; SIGPEND=0; ITEM_FIRST=""
TOKEN="s4d2-$MODE$FIXN-$(date +%Y%m%d%H%M%S)-$$"   # in the claim's <what>: only THIS run's claim is ever released
WHAT="headless Xvfb GAMMA A-Life equivalence series s4d2 ($MODE$FIXN, $(plan | wc -l) boots, display $DISP) $TOKEN"
case "$WHAT" in *EXCLUSIVE*) echo "REFUSED: the claim text names EXCLUSIVE"; exit 2 ;; esac
qr() { env "${BOX_QR_UNSET[@]}" "$T/quiet-required.sh" "$@" >/dev/null 2>&1 </dev/null; }   # the tool's tuning variables dropped
halt() { sc=$1; stop=$2; rc=1; }
nap() { sleep "$1" </dev/null & SP=$!; wait $SP; SP=""; }   # a sleep a trapped signal interrupts at once
own_pid() {   # <pid>: 0 only if it is a live (not zombie) process whose parent is THIS driver: a reused pid is never signalled
  local s; [ -n "${1:-}" ] || return 1
  s=$(cat "/proc/$1/stat" 2>/dev/null) || return 1
  s=${s##*) }; set -- $s
  [ "$1" != Z ] && [ "$2" = "$$" ]; }
child_alive() { own_pid "$BP"; }   # BP is only ever this driver's own det_run.sh
PFX="$(pwd -P)/run/prefix"   # this driver's Wine prefix (det_run.sh's run/prefix); never prefix_play (box_wine_kill)
engines_gone() {   # S-2: after our det_run.sh ended, no engine of the run tree may still use run/prefix; else
                   # wineserver -k there and wait (from date, 60 s). Still one left: the claim is NOT released (NORELEASE).
  local t0
  box_engines_in_prefix "$PFX" && return 0
  log "after det_run.sh: $BOX_WHY; wineserver -k in $PFX"
  box_wine_kill "$PFX" || { NORELEASE=1; NORELEASE_WHY="$BOX_WHY"; log "$BOX_WHY"; return 1; }
  t0=$(date +%s)
  until box_engines_in_prefix "$PFX"; do
    [ $(( $(date +%s) - t0 )) -ge "$ENG_GONE_S" ] && { NORELEASE=1; NORELEASE_WHY="still running ${ENG_GONE_S} s after wineserver -k: $BOX_WHY"; log "$NORELEASE_WHY"; return 1; }
    sleep 1
  done
  log "no engine left in $PFX"; return 0; }
stop_child() {   # TERM our det_run.sh (its EXIT trap runs its cleanup), wait up to CHILD_GRACE_S (from date), then KILL; reap
  local t0
  child_alive || { BP=""; [ "$BOOTED" = 1 ] && engines_gone; return 0; }
  kill -TERM "$BP" 2>/dev/null
  t0=$(date +%s)
  while child_alive && [ $(( $(date +%s) - t0 )) -lt "$CHILD_GRACE_S" ]; do sleep 1; done
  if child_alive; then
    child_alive && kill -KILL "$BP" 2>/dev/null
    log "det_run.sh (pid $BP) did not end ${CHILD_GRACE_S}s after TERM: KILLed; its cleanup did not run (its Xvfb on $DISP may be left; run/appdata/user.ltx not restored, by design after a stop)"
  fi
  wait "$BP" 2>/dev/null; BP=""
  engines_gone
}
release_mine() {   # release ONLY this call's own claim, by its id (re-check R-1, 2026-11-28): CLAIM_ID0, the id the one
                   # acquire claim printed (claim_calls.txt holds claim output only; renewals go to renew_calls.txt).
                   # Never an id a renewal printed: if our record had vanished, `renew` extends every engineperf record
                   # and prints a foreign id, and releasing that would release another job's claim. By name only when
                   # no id was ever printed (kj4p release_mine).
  local id
  [ "$CLAIMTRY" = 1 ] || return 0
  id=$CLAIM_ID0
  [ -n "$id" ] || id=$(tr -d '\r' < "$SD/claim_calls.txt" 2>/dev/null | sed -n 's/^claim-id: \([A-Za-z0-9._-][A-Za-z0-9._-]*\)$/\1/p' | tail -1)
  if [ -n "$id" ]; then qr release $W "$id"; log "released claim $id (rc $?)"; return 0; fi   # [A-Za-z0-9._-]: no glob
  box_claim_has_token $W "$TOKEN"; case $? in
    0) if box_ok_claims $W "$TOKEN"; then
         qr release $W; log "released the claim carrying $TOKEN by name (no claim-id; it was the only claim under $W) rc $?"
       else log "NOT released: the claim carrying $TOKEN has no claim-id and another claim under $W is live ($BOX_WHY); ours expires by itself (<= $CLAIM_MIN min)"; fi ;;
    1) log "no claim carrying $TOKEN found: nothing released" ;;
    *) log "NOT released: cannot tell which claim is this run's ($BOX_WHY); it expires by itself (<= $CLAIM_MIN min)" ;;
  esac
}
finish() {   # the ONE way out: stop record, own child, own queue entry, own claim, log, notify
  trap '' TERM INT HUP; trap - EXIT
  [ -n "$sc" ] || halt fault "driver exited without a recorded stop"
  printf 'class=%s%s reason=%s\n' "$sc" "${LIVE:+ interrupted=$LIVE}" "$(printf '%s' "${stop:-list finished}" | tr '\n\r' '  ')" > "$SD/stop.txt" \
    || { echo "stop record NOT written ($SD/stop.txt)" >&2; [ "$rc" = 0 ] && rc=1; }
  own_pid "$SP" && { kill "$SP" 2>/dev/null; wait "$SP" 2>/dev/null; }; SP=""
  stop_child
  withdraw_mine
  if [ "$NORELEASE" = 1 ]; then log "claim NOT released: $NORELEASE_WHY; it expires by itself (<= $CLAIM_MIN min)"; else release_mine; fi
  log "end ($MODE$FIXN): $(grep -c . "$SD/done.txt") boot(s) done, retries $RETRIED of 1. $(cat "$SD/stop.txt" 2>/dev/null)"
  timeout 60 "$T/notify-overseer.sh" engineperf - >/dev/null 2>&1 <<<"EnginePerf s4d2 $MODE$FIXN: ended $(date +%T), $(grep -c . "$SD/done.txt") boot(s) done, retries $RETRIED of 1. $(cat "$SD/stop.txt" 2>/dev/null). $([ "$sc" = done ] && echo "Not read or scored yet (by hand, once${RD_NEXT:+: out=$RD_NEXT})." || echo "Stopped: nothing is resumed without the supervisor's word (prereg 6.4).")"
  exit $rc; }
on_sig() { halt box "cancelled by a signal${LIVE:+ during $LIVE}"; rc=143; finish; }
held() {   # run "$@" with TERM / INT / HUP held; act on a held signal right after (sf1:116-120, kj4p held_call)
  local r; SIGPEND=0; trap 'SIGPEND=1' TERM INT HUP
  "$@"; r=$?
  trap on_sig TERM INT HUP; [ "$SIGPEND" = 1 ] && on_sig
  return $r; }
claim_call() { CLAIMTRY=1; env "${BOX_QR_UNSET[@]}" "$T/quiet-required.sh" claim $W "$CLAIM_MIN" "$WHAT" >> "$SD/claim_calls.txt" 2>&1 </dev/null; }
# S-1 (review 2026-11-28): the claim is taken ONCE (acquire) and then only EXTENDED by its id, never claimed again: a
# second `claim` by the same worker on its live slot can NEST a new record (quiet-required.sh nest_claim, header of the
# claim records), and each nested record would outlive this run unless released. `renew <w> <min>` with QR_CLAIM_ID
# set extends exactly that record when several are nested (renew_own: `QR_CLAIM_ID's ... extend only`), and with one
# record rewrites the slot keeping the id (recs_fresh <id>). It prints `claim-id: <id>` (kept in renew_calls.txt; the
# last call's output in renew_last.txt). Before every renewal box_gate has checked that a live record carries our token
# and that no other engineperf record is live. If our record vanished in the moment between that check and the renewal,
# renew_own extends EVERY engineperf record and prints the first (foreign) id (quiet-required.sh renew_own fallback;
# review re-check R-1), so the printed id is compared with CLAIM_ID0 after every renewal and a mismatch stops the run
# (rc 3 here, box stop in gates; nothing under run/ touched). QR_CLAIM_ID is set AFTER BOX_QR_UNSET drops any
# inherited value.
CLAIM_ID0=""
RENEW_WHY=""
renew_call() {   # 0 = our claim extended; 1 = refused or no id; 3 = it printed another id (or none)
  local r rid
  RENEW_WHY=""; [ -n "$CLAIM_ID0" ] || { RENEW_WHY="no claim id"; return 1; }
  env "${BOX_QR_UNSET[@]}" QR_CLAIM_ID="$CLAIM_ID0" "$T/quiet-required.sh" renew $W "$CLAIM_MIN" > "$SD/renew_last.txt" 2>&1 </dev/null; r=$?
  cat "$SD/renew_last.txt" >> "$SD/renew_calls.txt" 2>/dev/null
  [ "$r" = 0 ] || { RENEW_WHY="quiet-required.sh renew rc $r"; return 1; }
  rid=$(tr -d '\r' < "$SD/renew_last.txt" 2>/dev/null | sed -n 's/^claim-id: \([A-Za-z0-9._-][A-Za-z0-9._-]*\)$/\1/p' | tail -1)
  [ "$rid" = "$CLAIM_ID0" ] || { RENEW_WHY="renew printed claim-id '${rid:-none}', not this run's $CLAIM_ID0: our record was gone and the renewal may have extended another job's claim (not released by this run)"; return 3; }
  return 0; }
# S-3: the queue is keyed by the worker NAME only (quiet-required.sh `want` dedupes by name, `withdraw <w>` drops every
# entry of <w>, and the name must be a tmux session overseer-<name>, so a per-run queue name is not possible). Known
# without `check`, by reading QUIET-QUEUE: whether an entry under our name exists BEFORE a want. Rule: want only when no
# entry exists (then the entry is this run's: OURQ=1); withdraw only if OURQ=1 and exactly one entry exists then. NOT
# knowable: another engineperf job's want that landed on our entry (deduped into it); withdraw then removes it too.
want_call() {
  box_queue_count $W || { log "queue unreadable ($BOX_WHY): want not sent"; return 0; }
  [ "$BOX_QN" -gt 0 ] && return 0   # an entry under our name exists (ours, or another job's: either way not re-sent)
  qr want $W && { WANTED=1; OURQ=1; }; return 0; }   # SHARED: no EXCLUSIVE word
withdraw_mine() {
  [ "$WANTED" = 1 ] && [ "$OURQ" = 1 ] || return 0
  WANTED=0
  box_queue_count $W || { log "queue unreadable ($BOX_WHY): not withdrawn (the entry is dropped by the tool's QUEUE_TTL or a later claim)"; return 0; }
  case "$BOX_QN" in
    0) log "no queue entry under $W left: nothing to withdraw" ;;
    1) qr withdraw $W; log "withdrew the queue entry under $W (rc $?)" ;;
    *) log "NOT withdrawn: $BOX_QN entries under $W (not only this run's)" ;;
  esac
  OURQ=0; }
trap finish EXIT; trap on_sig TERM INT HUP

# ---- one SHARED claim for the whole run (prereg 7: SHARED; exit codes only; capped wait) ----
acquire() {   # review :456 / N-5 (2026-11-28): the 6-h deadline is checked before every claim attempt (also right
              # after the box checks, which take up to ~40 s), and each sleep is cut to the time left
  local t0 out="" r said="" left
  t0=$(date +%s)
  while :; do
    left=$(( WAIT_MAX_S - ($(date +%s) - t0) ))
    [ "$left" -le 0 ] && { halt box "no claim within $((WAIT_MAX_S / 3600)) h (last: $(echo "$out" | tr '\n' ' '))"; return 1; }
    box_preflight_all shared "$DISP" "$W" "$TOKEN" >/dev/null; r=$?; out=$BOX_OUT   # this shell: sightings kept (S3-5)
    if [ $r = 0 ]; then
      [ $(( $(date +%s) - t0 )) -ge "$WAIT_MAX_S" ] && continue   # past the deadline after the checks: no claim
      held claim_call; r=$?
      [ $r = 0 ] && { CLAIMED=1; break; }
      [ "$said" = "claim rc $r" ] || { log "waiting: claim refused (rc $r)"; said="claim rc $r"; }
    else
      [ "$said" = "$out" ] || { log "waiting: $(echo "$out" | tr '\n' ' ')"; said=$out; }
    fi
    held want_call
    left=$(( WAIT_MAX_S - ($(date +%s) - t0) )); [ "$left" -le 0 ] && continue
    nap $(( left < POLL_S ? left : POLL_S ))
  done
  withdraw_mine
  return 0
}
acquire || finish
CLAIM_ID0=$(tr -d '\r' < "$SD/claim_calls.txt" 2>/dev/null | sed -n 's/^claim-id: \([A-Za-z0-9._-][A-Za-z0-9._-]*\)$/\1/p' | tail -1)
[ -n "$CLAIM_ID0" ] || { halt box "the claim was granted but printed no claim-id: it cannot be renewed by id (S-1)"; finish; }
log "claim granted ($MODE$FIXN, $TOKEN), load $(cut -d' ' -f1-3 /proc/loadavg), timeout per boot ${TIMEOUT_RUN}s"

# ---- per boot ----
box_gate() {   # <when>: box_preflight_all; 1 = stop (halt set)
  local out r
  box_preflight_all shared "$DISP" "$W" "$TOKEN" >/dev/null; r=$?; out=$BOX_OUT   # this shell (S3-5)
  [ $r = 0 ] || { halt box "$1: $(echo "$out" | tr '\n' ' ')"; return 1; }
  # S-2 (review 2026-11-28): own_claim passes with NO record under our name, so a lost claim needs its own test
  box_claim_has_token $W "$TOKEN" || { halt box "$1: lost claim: no live claim record under $W carries $TOKEN ($BOX_WHY)"; return 1; }
  return 0
}
axr_restore() {   # the options file before THIS boot (header OPTIONS FILE); only with the claim held and the box clear.
  # Called in THIS shell (N3-4: never inside `$(...)`, where the sticky BOX_STOPPED of box_guarded would be lost):
  # 0 copied and checked | 1 fault | 2 box refusal (BOX_STOPPED set); the reason is left in AXR_MSG.
  local r
  AXR_MSG=""
  r=$(pin "$AXR_SRC" "$AXR_SHA") || { AXR_MSG=$r; return 1; }
  { [ -L "$AXR_DST" ] || ! [ -f "$AXR_DST" ] || [ "$(realpath -- "$AXR_DST")" != "$(realpath -- .)/$AXR_DST" ]; } && {
    AXR_MSG="REFUSED: $AXR_DST is a link or not a plain file inside run/ (not written)"; return 1; }
  box_guarded "copy $AXR_SRC over $AXR_DST" cp -f -- "$AXR_SRC" "$AXR_DST" || {
    [ -n "$BOX_STOPPED" ] && { AXR_MSG="REFUSED (box): $BOX_STOPPED: $AXR_DST not written"; return 2; }
    AXR_MSG="REFUSED: cannot copy $AXR_SRC over $AXR_DST"; return 1; }
  r=$(pin "$AXR_DST" "$AXR_SHA") || { AXR_MSG="$r (after the copy)"; return 1; }
  return 0
}
gates() {   # <tag> <exe>: every pre-boot check; 1 = stop (halt set)
  local t=$1 e=$2 r
  box_gate "before $t" || return 1
  held renew_call || { halt box "claim renewal (renew by id $CLAIM_ID0) failed before $t: $RENEW_WHY"; return 1; }
  for _ in 1 2 3 4 5 6 7 8 9 10; do box_ok_display_free "$DISP" && break; nap 2; done
  box_ok_display_free "$DISP" || { halt fault "$BOX_WHY (before $t)"; return 1; }
  r=$(cfg0) || { halt fault "$r (before $t)"; return 1; }
  r=$(pins_all) || { halt fault "pins before $t: $(echo "$r" | tr '\n' ' ')"; return 1; }
  [ -e "run/bin/$e" ] || { halt fault "missing file run/bin/$e (before $t)"; return 1; }
  dumps_left && { halt fault "a crash dump is already in $LOGS before $t ($(ls "$LOGS"/*.mdmp 2>/dev/null | head -3 | tr '\n' ' ')): not moved"; return 1; }
  box_gate "before the options restore of $t" || return 1
  axr_restore; case $? in
    0) ;;
    2) halt box "options restore before $t: $AXR_MSG"; return 1 ;;
    *) halt fault "options restore before $t: $AXR_MSG"; return 1 ;;
  esac
  box_gate "last check before $t" || return 1
  if [ -n "$MOVE_TO" ] && [ -d out/det_s4p_probe ]; then   # S2-5: the re-probe's rename, only once the gates passed
    [ -e "$MOVE_TO" ] && { halt fault "$MOVE_TO exists before the re-probe"; return 1; }
    mv -- out/det_s4p_probe "$MOVE_TO" || { halt fault "cannot rename out/det_s4p_probe to $MOVE_TO"; return 1; }
    log "renamed out/det_s4p_probe -> $MOVE_TO (prereg 2.1: the re-probe is read as det_s4p_probe)"
  fi
  [ -e "out/det_$t" ] && { halt fault "out/det_$t exists before its boot"; return 1; }
  [ -e "$LOGS/synth_$t.txt" ] && { halt fault "$LOGS/synth_$t.txt exists before its boot"; return 1; }
  return 0
}
item_of() {   # <xray.log>: the one item line's "slot N section S", "NONE", or "" (absent / more than one)
  tr -d '\r' < "$1" 2>/dev/null | sed -n 's/^.*\* det_synth stimulus: item \(slot [0-9][0-9]* section [^ ]*\)$/\1/p; s/^.*\* det_synth stimulus: item NONE.*$/NONE/p' \
    | awk '{l=$0; n++} END{if (n == 1) print l}'; }
after() {   # <tag> <lever>: move the witness, check what every boot must leave; 1 = stop (halt set)
  local t=$1 lv=$2 d=out/det_$1 w f it
  [ -d "$d" ] || { halt fault "missing folder $d after the boot"; return 1; }
  if [ -e "$LOGS/synth_$t.txt" ]; then
    box_guarded "move $LOGS/synth_$t.txt" mv -- "$LOGS/synth_$t.txt" "$d/" || {
      [ -n "$BOX_STOPPED" ] && { halt box "$BOX_STOPPED: $LOGS/synth_$t.txt not moved"; return 1; }
      halt fault "cannot move $LOGS/synth_$t.txt into $d/"; return 1; }
    log "moved $LOGS/synth_$t.txt -> $d/"
  fi
  for f in "$d/xray.log" "$d"/det_alife_counts_"$t"_{1,2,3}.txt "$d"/det_counters_"$t"_{1,2,3}.txt; do
    [ -s "$f" ] || { halt fault "missing file $f after $t"; return 1; }
  done
  it=$(item_of "$d/xray.log")
  case "$it" in
    '') halt fault "$t: not exactly one 'det_synth stimulus: item' line in $d/xray.log"; return 1 ;;
    NONE) halt fault "$t: the stimulus took no item (item NONE): prereg 2.2 item"; return 1 ;;
  esac
  [ -z "$ITEM_FIRST" ] && ITEM_FIRST=$it
  [ "$it" = "$ITEM_FIRST" ] || { halt fault "$t took '$it', the first boot took '$ITEM_FIRST'"; return 1; }
  [ -z "$lv" ] && return 0
  w=$d/synth_$t.txt
  [ -s "$w" ] || { halt fault "missing file $w (the witness of lever boot $t)"; return 1; }
  return 0
}
has_seq() { case " $1 " in *" $2 "*) return 0 ;; esac; return 1; }   # whole-token match (N2-2)
inrun_check() {   # <tag> <exe> <lever>: prereg 6.2 in-run preflight on s4p_off2a (and on the probe): the registered shape
  local t=$1 e=$2 lv=$3 d=out/det_$1 n want cl k m wc brk npc
  want=$NTOK_B2; [ "$e" = "$EXE1" ] && want=$NTOK_B1
  n=$(tr -d '\r' < "$d/synth_$t.txt" | awk -v w="$want" 'NF && NF != w {b++} END {print b+0}')
  [ "$n" = 0 ] || { echo "witness $d/synth_$t.txt: $n lines without $want tokens"; return 1; }
  cl=$(tr -d '\r' < "$d/xray.log" | grep -a "command line" | head -1 | tr 'A-Z' 'a-z' | tr -s ' ')
  for s in "$BASE" "$ITEM" "$lv"; do has_seq "$cl" "$s" || { echo "engine command line lacks [$s]"; return 1; }; done
  k=$(echo "$lv" | sed -n 's/^-det_synth_k \([0-9]*\) .*/\1/p'); m=$(echo "$lv" | sed -n 's/.* -sim_4d_mask \([0-9]*\) .*/\1/p')
  wc=0; has_seq "$lv" -det_synth_wallclock && wc=1
  brk=$(echo "$lv" | sed -n 's/.* -sim_4d_break \([0-9]*\)$/\1/p'); brk=${brk:-0}
  [ "$(tr -d '\r' < "$d/xray.log" | grep -acx ".*\* det_synth: k $k wallclock $wc tw 1 break $brk mask $m *")" = 1 ] \
    || { echo "not exactly one det_synth line 'k $k wallclock $wc tw 1 break $brk mask $m' in $d/xray.log"; return 1; }
  if [ "$e" != "$EXE1" ]; then
    npc=$(echo "$lv" | sed -n 's/.* -det_synth_pose_npc \([A-Za-z0-9_]*\)\( .*\)*$/\1/p')
    [ -n "$npc" ] || { echo "no NPC in the lever [$lv]"; return 1; }
    [ "$(tr -d '\r' < "$d/xray.log" | grep -acx ".*\* det_synth: pose witness on, npc $npc *")" = 1 ] \
      || { echo "not exactly one pose witness line naming $npc in $d/xray.log"; return 1; }
  fi
  return 0
}
watch_boot() {   # in THIS shell (no subshell: traps act at once): while our det_run.sh runs, the box conditions
                 # every WATCH_NAP_S (engine_running excluded: ours runs), and the wall cap (re-check R-1, s4d1 N4-11);
                 # sets WATCHWHY and returns 1 to stop (WATCHCLS box or fault)
  local t0; t0=$(date +%s)
  WATCHWHY=""; WATCHCLS=box
  while child_alive; do
    nap "$WATCH_NAP_S"
    child_alive || return 0
    box_ok_play_lock || { WATCHWHY="play_lock: $BOX_WHY"; return 1; }
    box_ok_title     || { WATCHWHY="protected_title: $BOX_WHY"; return 1; }
    box_ok_roblox    || { WATCHWHY="roblox: $BOX_WHY"; return 1; }
    box_ok_claims $W "$TOKEN" || { WATCHWHY="own_claim: $BOX_WHY"; return 1; }
    box_claim_has_token $W "$TOKEN" || { WATCHWHY="lost_claim: no live claim record under $W carries this run's token ($BOX_WHY)"; return 1; }   # S-2
    box_ok_exclusive || { WATCHWHY="exclusive: $BOX_WHY"; return 1; }
    [ $(( $(date +%s) - t0 )) -ge $(( TIMEOUT_RUN + WATCH_EXTRA_S )) ] && {
      WATCHCLS=fault; WATCHWHY="det_run.sh still running $(( $(date +%s) - t0 ))s after its start (cap ${TIMEOUT_RUN}+${WATCH_EXTRA_S}s)"; return 1; }
  done
  return 0
}
boot() {   # <tag> <exe> <lever>: one det_run.sh boot in the background (S2-3); returns its rc, 9 crash dump, 8 box stop
  local t=$1 e=$2 lv=$3 r t0
  t0=$(date +%s)
  log "boot $t exe=$e extra=[$BASE $ITEM${lv:+ $lv}] display=$DISP timeout=${TIMEOUT_RUN}s load $(cut -d' ' -f1-3 /proc/loadavg)"
  LIVE=$t; BOOTED=1; SIGPEND=0; trap 'SIGPEND=1' TERM INT HUP   # review N-1 / (e): held until BP is recorded
  DET_NOCLAIM=1 DET_BOX_TOKEN=$TOKEN DET_BOX_CLASS=shared DET_BOX_WORKER=$W BENCH_SAVE=bench1 \
  BENCH_DISPLAY=$DISP BENCH_TIMEOUT=$TIMEOUT_RUN DET_EXTRA="$BASE $ITEM${lv:+ $lv}" \
    "$DETRUN" "$e" "$t" 60 3 > "$SD/run_$t.log" 2>&1 </dev/null & BP=$!
  trap on_sig TERM INT HUP; [ "$SIGPEND" = 1 ] && on_sig   # finish() now sees BP and stops this child
  if ! watch_boot; then
    log "$WATCHCLS condition during $t: $WATCHWHY; stopping this run's det_run.sh (pid $BP)"
    stop_child; BOXWHY=$WATCHWHY; BOXCLS=$WATCHCLS; LIVE=""; return 8
  fi
  wait "$BP"; r=$?; BP=""
  LIVE=""
  # S-3 / (c) (review 2026-11-28): det_run.sh keeps its rc when its stop_wine left an engine (WINE_LEFT); check at once,
  # before any retry or after-boot step, that no engine of the run tree uses run/prefix (engines_gone: wineserver -k
  # there and a 60-s wait). Still one left: fault stop, claim not released (NORELEASE).
  engines_gone || { DETRC=$r; return 10; }
  log "boot $t rc=$r wall=$(( $(date +%s) - t0 ))s: $(grep -aE '^rc=|TIMEOUT|ABORT|CRASH|REFUSED|exited early' "$SD/run_$t.log" | tr '\n' ' ')"
  { grep -aq "CRASH DUMP present" "$SD/run_$t.log" || ls out/det_"$t"/*.mdmp >/dev/null 2>&1; } && return 9
  return $r
}

plan > "$SD/plan.txt" || { halt fault "cannot write $SD/plan.txt"; finish; }
BOXWHY=""; BOXCLS=box
while IFS='|' read -r -u 3 tag exe lv; do
  t=$tag
  while :; do
    gates "$t" "$exe" || finish
    boot "$t" "$exe" "$lv"; brc=$?
    echo "$t rc=$brc" >> "$SD/boots.txt" || { halt fault "cannot append to $SD/boots.txt"; finish; }
    [ "$brc" = 0 ] && { echo "$t" >> "$SD/done.txt" || { halt fault "cannot append to $SD/done.txt"; finish; }; }   # N-13: completed boots only
    case $brc in
      0) ;;
      8) halt "$BOXCLS" "during $t: $BOXWHY (this run's boot stopped)"; finish ;;
      9) halt fault "crash dump after $t"; finish ;;
      5) halt fault "timeout of $t (${TIMEOUT_RUN}s)"; finish ;;
      10) halt fault "an engine of the run tree still used run/prefix after $t (det_run.sh rc ${DETRC:-?}): ${NORELEASE_WHY:-see the log}"; finish ;;
      3) halt box "det_run.sh rc 3 during $t: its box preflight refused, or it aborted for the play lock, a title or Sober ($(grep -aE '^(REFUSED|ABORT)' "$SD/run_$t.log" | head -2 | tr '\n' ' '))"; finish ;;
      4) if [ "$MODE" = series ] && [ "$RETRIED" = 0 ] && [ "$t" = "$tag" ]; then
           RETRIED=1; t=${tag}_r1; log "boot $tag exited early (rc 4, no crash dump): the one retry of the series, as $t"
           # the failed boot may have left a witness in the logs folder: keep it with its own folder (N2-3: checked)
           if [ -e "$LOGS/synth_$tag.txt" ]; then
             if [ -d "out/det_$tag" ] && box_guarded "move $LOGS/synth_$tag.txt" mv -- "$LOGS/synth_$tag.txt" "out/det_$tag/"; then log "moved $LOGS/synth_$tag.txt -> out/det_$tag/"
             elif [ -n "$BOX_STOPPED" ]; then halt box "$BOX_STOPPED: $LOGS/synth_$tag.txt not moved"; finish
             else log "NOT moved: $LOGS/synth_$tag.txt (out/det_$tag missing or mv failed); it stays in $LOGS and does not clash with $t"; fi
           fi
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
done 3< "$SD/plan.txt"
sc=done; stop=""
log "all boots of $MODE$FIXN done. Next (by hand, once): $([ "$MODE" = series ] && echo "python3 -I $S4/s4d2_score.py root=out out=$S4/s4d2_score_result.txt" || echo "python3 -I $S4/s4d2_score.py mode=probe root=out out=$RD_NEXT")"
finish
