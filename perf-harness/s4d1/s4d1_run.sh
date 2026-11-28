#!/usr/bin/env bash
# REGISTERED 2026-11-28 after the GO re-probe. s4d1 series driver (PREREG-s4d1-leftovers-build1-2026-11-28.md sections 2.1, 3, 4, 5, 6;
# requirement R2 of s4d1/NOTES.md 4). Written 2026-11-28 at the desk from the template s4d2/s4d2_run.sh (sha256
# 2777c68f..., four reviews). Only `bash -n` and fixture `--preflight-only` calls have been run on it (s4d1/NOTES.md 9).
# Rule of the brief: where the draft and the template disagree, the DRAFT's test content wins (boots, tags, flags,
# order, probe, stop and retry rules, pins) and the TEMPLATE's safety rules win (box checks, claim, child handling,
# guarded writes, refusals). Every such point: s4d1/NOTES.md 9.
#
#   s4d1_run.sh --preflight-only            box checks ONLY: source common/box_preflight.sh, run box_preflight_all
#                                           (shared, :141, engineperf), print, exit 0 clear | 1 refused | 2 refused,
#                                           a check could not tell | 3 the shared file is missing, unreadable, not the
#                                           pinned file, or lacks its marker / version / functions. Touches nothing.
#   s4d1_run.sh probe   [--preflight-only]  the two unscored probe boots, s4d_probe (mask 7) then s4d_probe15 (mask 15)
#                                           (prereg 2.1). Read afterwards by hand, once, into s4d1/probe_readings.txt.
#   s4d1_run.sh series  [--preflight-only]  the scored boots (prereg 6.1): part A 6, part L 5, part S 7 or 8 (s4d_brk2
#                                           only if S3 is registered, i.e. PROBE_S3_FRAMES >= 300, prereg 2.3)
# "<mode> --preflight-only" runs every start check of that mode (box checks, environment, pins, placeholders, settings,
# folders, the probe reading) and exits 0 / 2 before any state folder, queue entry or claim.
# NO re-probe mode: the draft says a probe is re-run only after a fix a stop rule asks for (2.1) and after a stimulus
# change (1), but does not register where the first probe's folders and reading go (the scorer always reads
# out/det_s4d_probe and out/det_s4d_probe15). Coordinator 2026-11-28: a re-probe needs a new decision from the
# supervisor and a registered place for the old probe folders. `probe` refuses while any out/det_s4d_* folder or the
# probe reading exists (NOTES.md 9).
#
# BOX SAFETY (as s4d2_run.sh): every check of Caden's play lock, a protected title, Sober, an engine of the run tree,
# another claim under our name, EXCLUSIVE in the claim files and display :0 comes from common/box_preflight.sh (pinned
# below by full sha256; refused if missing, unreadable, unpinned, or without its marker, version or functions). It is run
# fail-closed: before the start, before every claim attempt, before every boot (before the claim renewal, before the
# options restore and last), and every ~5 s while a boot runs (all but engine_running). Each of this driver's own writes
# under run/ (the options copy, the witness moves) runs inside box_guarded; after a refusal nothing more under run/ is
# touched. It never deletes anything (no rm in this file); it releases only the claim it made (by its claim-id; by name
# only when the claim carrying this run's token is the only one under our name); every exit path goes through finish(),
# which kills and reaps only its own child (det_run.sh, KILL after CHILD_GRACE_S) and its own sleep. After det_run.sh
# ends or is KILLed, no engine of the run tree may still use run/prefix (engines_gone), else the claim is NOT released.
# Review 2026-11-28 (plan-facts/s4d1-driver-review-2026-11-28.md) applied: S-1 one claim, then `renew` by its id,
# finish() releases every id obtained; S-2 a lost claim (no live record with our token) stops, in box_gate and
# watch_boot; S-3 engines_gone right after det_run.sh returns; N-1 signals held over the launch until BP is set; N-5
# the wait deadline before every claim attempt; N-7 the series needs both probe folders; N-4 PROBE_BOOT_S checked
# against the probe logs and printed at the end of probe mode; N-3 the reader's hashes logged then.
# Re-check 2026-11-28 (same file, "Re-check after fixes"): R-1 the id every renewal prints must be CLAIM_ID0, else a
# box stop (renew_call rc 3); finish() releases ONLY CLAIM_ID0, never an id a renewal printed; CLAIM_MIN's sum
# corrected (+1425 -> +1536: the options copy's and the witness move's box_fresh, one watch pass; probe 58 min).
# Fourth-review leftovers (plan-facts/box-preflight-fourth-review-2026-11-28.md) applied here: N4-8 the five caller
# variables det_run.sh refuses are refused HERE, before any claim; N4-9 the token stands last in the claim text (the
# shared file's substring match is unchanged: residue named in NOTES.md 9); N4-11 a per-boot wall cap in watch_boot
# (WATCH_EXTRA_S); N4-12 run/appdata/user.ltx must be one of the two known files before the start and before every
# boot, refused by name here instead of det_run.sh's rc 2. S4-6 (det_run.sh's start race) is covered by engines_gone.
#
# Other rules (prereg 6, the template):
#   - display :141 only (BENCH_DISPLAY must be unset or :141); the display must be free before every boot;
#   - every pinned file is checked before the first boot and before every boot, by name; refused while any PIN_ME_
#     remains (all modes), and in series mode while PROBE_BOOT_S, PROBE_S3_FRAMES or any REG_ME_ remains;
#   - stops on the first crash dump, box condition, failed pre-check, failed in-run check or missing file;
#   - a crash dump already in run/appdata/logs before a boot refuses that boot;
#   - prereg 6.5 retry: in series mode a boot that ends rc 4 (early exit) or rc 5 (timeout) with no crash dump is run
#     once more at once as <tag>_r1, same switches; a second failure of the same slot stops the series. Probe mode: no
#     retry (a probe is re-run only after a fix, prereg 2.1);
#   - after every boot the witness run/appdata/logs/synth_<tag>.txt is moved into out/det_<tag>/ (det_run.sh does not:
#     det_run.sh:259-266 moves only this tag's dumps, counters, traces and saves); the folder must hold xray.log,
#     det_alife_counts and det_counters at cp1, cp2, cp3 (the scorer's C reads all six), and a lever boot's witness;
#   - in-run preflight (prereg 6.4) on s4d_l2a, checked right after its boot (NOTES.md 9: not during it);
#   - the plan is read on fd 3 and every child gets stdin from /dev/null; all times from `date`; no `rm` in this file.
# OPTIONS FILE (template, NOT in the draft: NOTES.md 9): before every boot, with the claim held and the box clear,
#   run/appdata/axr_options_play.ltx is hashed against AXR_SHA, copied over run/gamedata/configs/axr_options.ltx
#   (refused if that is a link or not a plain file inside run/) and the destination hashed. Why: each exe rewrites that
#   file's version line when it boots (wu3 RESULT; kj4p RESULT found Stance14's rewrite) and part A alternates Stance14
#   and SimTick18, so without it the arms of a pair would not start from the same bytes. Never while the play lock is live.
# NOTIFY: finish() sends one line to the Overseer (notify-overseer.sh engineperf -, timeout 60).
# Not scored here. Probe reading and scoring are run by hand, once each (prereg 2.1, 6.5):
#   python3 -I wt-render-split/perf-harness/s4d1/s4d1_score.py mode=probe root=out out=wt-render-split/perf-harness/s4d1/probe_readings.txt
#   python3 -I wt-render-split/perf-harness/s4d1/s4d1_score.py root=out out=wt-render-split/perf-harness/s4d1/s4d1_score_result.txt
set -u
# N4-8 (fourth review): det_run.sh refuses these five in both its modes (det_run.sh:73-74); refused here first, before
# any folder, queue entry or claim, so a set one cannot turn into a claimed series whose first boot ends rc 2.
for _v in NOGUARD_GAMES WINESERVER WINELOADER ENGINE_MIN_RSS_KB BASH_ENV; do
  [ -n "${!_v+x}" ] && { echo "REFUSED: $_v is set in the caller's environment (it changes the ~/Games guard, the Wine server, the early-exit test or the shell start-up; det_run.sh refuses it too): unset it"; exit 2; }
done
cd "$(dirname "$0")/../../.." || { echo "REFUSED: cannot enter the project root"; exit 2; }   # project root
# S3-2 (third review): the engine checks and PFX below use the physical tree; it must be the one tree they look at
[ "$(pwd -P)" = "$HOME/Projects/StalkerGameEngine" ] || { echo "REFUSED: the project root $(pwd -P) (physical) is not $HOME/Projects/StalkerGameEngine"; exit 2; }
T="$HOME/.claude/overseer/tools"; W=engineperf
S4=wt-render-split/perf-harness/s4d1; ST=out/s4d1_state; LOGS=run/appdata/logs; CFG=run/appdata/user.ltx

# ---- the shared box checks (pinned; S4D1_BOX_LIB / S4D1_BOX_LIB_SHA are honoured ONLY by the preflight-only forms,
# ---- for fixture calls; a real run refuses while either, or any BOX_* input, is set)
BOXLIB_DEF=wt-render-split/perf-harness/common/box_preflight.sh
BOXLIB_SHA=f8026040f2f9b3f6cd5c7c33b11062f7efc1ed726af03651ce16bbb0fccd53ef   # common/box_preflight.sh v2, third-review revision 2026-11-28
BOXLIB_MARKER=engineperf-box-preflight-v2; BOXLIB_VERSION=2

# ---- pins (prereg 1, 6.4; full sha256, read 2026-11-28 with sha256sum) ----
EXE=SimTick18.exe                # build 1, CI perf-build run 46114135445 of wt-simtick 6acb77044 (7b1232ffc + comments)
EXE_SHA=40d2d37d810bdd9fdf0a19a094dea621a16a2f164fdb1e1b9d1b209d5a6b09d4   # prereg 1 PIN_ME_EXE (filled 2026-11-28)
STOCK=Stance14.exe; STOCK_SHA=b4ce9c772ae5fa4bf5ddb955f2dfcb943627d6c2b2e4ce66e21eac1168646cbe   # prereg 1 / 6.4 "b4ce9c77"
STIM=run/gamedata/scripts/zz_eperf_synth.script   # the installed stimulus (prereg 1, R3): checked here, NEVER installed here
STIM_SHA=1777fb6865cc5b56c80f197f3f95a250104d6c90ce0b8b2a35422e3bed39ff95   # = harness/zz_eperf_synth.script
                                 # (2026-11-27 22:15), re-pinned 2026-11-28 by the coordinator's decision (was prefix
                                 # d7dbc6c5...; those bytes are gone). The same installed file and hash as s4d2_run.sh.
                                 # Its one addition, the -det_synth_item path, is off without that switch (script:20).
                                 # Prereg Amendment 1 (2026-11-28): every stimulus boot (both probes, every part-S boot,
                                 # the breaks included) passes it, so the actor holds an item (script:68-79); the boots
                                 # without the stimulus (parts A and L) do not (ITEM check below).
USERDET=out/user_det.ltx.orig; USERDET_SHA=3e0e0a6f1fe8010cd727a8f4e718b0486711fbcf8e4f7bfc8eb769d047411b81   # prereg 6.4 "3e0e0a6f1fe8010c"
USERORIG=out/user.ltx.orig       # what det_run.sh restores after a boot (not pinned by the draft; must exist)
DETRUN=tools/det_run.sh; DETRUN_SHA=e29800949b503a7f61a5f9b3a9a2289cdd01424fa87f3a55dacac464fa578f48   # prereg 6.2 / change log 2026-11-28
PIDLIB=tools/lib_engine_pids.sh; PIDLIB_SHA=cb183b7b894eab3edb200e2c03a3a96f2e935cd2d1e1f402f48be7aad238ed6e   # template (det_run.sh sources it)
NOGUARD=tools/run_noguard.sh; NOGUARD_SHA=92729abc420784053cee4b1b1832f5d112f74979a4760e2173d45578358455d5      # template (det_run.sh runs it)
SAVE_P=run/appdata/savedgames/bench1.scop; SAVE_P_SHA=891693c5e318ad1e97e8437476654200eb39d3bab89de79a778fa9b90ff89ffa  # template (sf1:192)
SAVE_C=run/appdata/savedgames/bench1.scoc; SAVE_C_SHA=7c2ddf33ccdaa75c341ba8a7c3cd84cb454972e81a47ce844d6919cb2a2f167e  # template (sf1:193)
AXR_SRC=run/appdata/axr_options_play.ltx; AXR_DST=run/gamedata/configs/axr_options.ltx
AXR_SHA=27d54b9f59404d224258649598ca604ebfc174dd066be4a96ff392323244fb29   # template (sf1's value; matches 2026-11-28)
# ---- values the probe supplies (prereg 2.3; series mode only; filled by hand from the GO reading) ----
PROBE_BOOT_S=402                 # the longer probe's det_run.sh `rc=0 elapsed=` seconds
PROBE_S3_FRAMES=2746             # = the scorer's CONST value (cross-checked below); decides whether s4d_brk2 boots
# ---- registration values (series mode only; prereg 2.5, 8: "record the hashes of scorer and self-test") ----
SCORER_SHA=28af1a58ccbaf15c0085ef869e67e4621181efb6c659af9c096cf831aa71b3ad # sha256 of s4d1_score.py as registered (CONST filled)
SELFTEST_SHA=c2216766d8e216be412397502312bf61bd3a089134d8fa96ac01723a98ee2615 # sha256 of s4d1_selftest.sh as registered
# ---- fixed by the prereg ----
BASE="-det_passes 100,200,320 -det_lua_streams -det_counters"   # prereg 6.2 <base> (tools/s4c_partA.sh:11)
REF="-det_passes 50,600,1650 -det_save -det_lua_streams -det_counters"   # the 11-19 counter method (s4c_partA.sh:10-16)
ALLOWED="-:50,600,1650 -:-det_save +:100,200,320"
STIMSW="-det_synth_tw -det_synth_walk -det_synth_tracer -det_synth_item"   # prereg 2.1 / 5, Amendment 1: tripwires +
                                 # stimulus + the held item (probes, part S)
DROP_MIN=300                     # prereg 2.3: PROBE_S3_FRAMES < 300 -> S3, TWB2 dropped, s4d_brk2 not booted
DISP=:141
# Timeouts (det_run.sh BENCH_TIMEOUT: from the engine start to the last checkpoint, det_run.sh:234, :256), per arm:
#   probe (both boots K=3 + tripwires + stimulus): 1900 s, prereg 6.2/6.3. Guess for a K=3 boot 375 s (211 s stock
#   maximum [M] + 2 x 5440 render-only frames x ~15 ms [G]) -> 1900 / 375 = 5.1x (shared class needs >= 5x, prereg 7).
#   series: prereg 2.3, BENCH_TIMEOUT = max(1900, ceil(5 x PROBE_BOOT_S / 60) x 60): >= 5x the MEASURED longer probe,
#   which is a K=3 boot with tripwires and the stimulus. Per arm that is: K=3 S runs (on3, off3: what the probe does)
#   >= 5x; K=2 S runs and part L (fewer render-only frames; L also without tripwire hashing) and K=1 / part A (no
#   render-only frames) are shorter boots, so their margin is larger than 5x. One value for all boots, as registered.
TIMEOUT_PROBE=1900
WAIT_MAX_S=21600                 # claim wait cap 6 h, then class=box. COPIED from s4d2_run.sh (its review S2-7): no
                                 # s4d1 quantity sets it; flagged in NOTES.md 9 (margin table)
POLL_S=60                        # claim poll: one box_preflight_all (<= ~40 s worst: title 10 s + flatpak 10 s +
                                 # systemctl 10 s bounds, box_preflight.sh) plus a `want` per poll, so the start lags a
                                 # cleared box by <= ~1.7 min; 6 h / 60 s = 360 polls
CHILD_GRACE_S=250                # TERM -> KILL for det_run.sh (caller rule: >= 180 s, prereg change log 2026-11-28).
                                 # Its cleanup after a TERM (det_run.sh:173-186): the command in flight finishes first
                                 # (longest: wineserver -w, 30 s); stop_wine = -k 30 s + -w 30 s (box_wine_kill bounds)
                                 # + the 60-s wait, whose repeated `-k nowait` (10 s) starts only while <= 50 s are gone
                                 # (det_run.sh:157), so <= 60 + 10; the user.ltx restore is skipped (on_sig sets
                                 # BOX_STOPPED); own Xvfb / openbox kill and report_left: < 5 s; no release (DET_NOCLAIM).
                                 # Worst 30 + 60 + 70 + 5 = 165 s; 250 = 1.5x
WATCH_EXTRA_S=900                # N4-11: watch_boot's cap = BENCH_TIMEOUT + this. det_run.sh's time outside its LIMIT,
                                 # worst case by its own bounds: before the engine start two box passes (preflight and
                                 # box_fresh, <= 40 s each) + 3 guarded steps (box_fresh <= 40 s each) + Xvfb 11 s +
                                 # openbox wait 30 x (5 + 0.5) = 165 s, i.e. <= 376 s + run_noguard.sh's find (seconds
                                 # [G]); after LIMIT: the loop notices it <= 5 s + one inboot (<= 40 s) late; after cp3
                                 # waitchk 8 s (+ <= 40 s); stop_wine <= 130 s; collect and restore guarded <= 80 s.
                                 # Sum ~ 376 + 45 + 48 + 130 + 80 = 679 s; 900 = 1.3x of that worst case, and ~22x the
                                 # ~40 s measured between boots (prereg 6.3 [M])
DISP_WAIT_N=10; DISP_WAIT_S=2    # display free before a boot: det_run.sh kills its own Xvfb (TERM) before it exits and
                                 # Xvfb drops its socket and lock within ~1 s [G]; 10 x 2 s = 20 s = ~20x
ENG_GONE_S=60                    # engines_gone: after box_wine_kill (-k, then -w up to 30 s) the engine processes of
                                 # run/prefix must be gone within the same 60 s det_run.sh's stop_wine allows (det_run.sh:156)
WATCH_NAP_S=5                    # box checks during a boot: every 5 s, the rate of det_run.sh's own inboot (det_run.sh:244)

usage() { echo "usage: $0 --preflight-only | probe|series [--preflight-only]"; exit 2; }
MODE=${1:-}; PRE=""
case "$MODE" in
  --preflight-only) [ $# = 1 ] || usage ;;
  probe|series) PRE=${2:-}; [ $# -le 2 ] || usage ;;
  *) usage ;;
esac
case "$PRE" in ''|--preflight-only) ;; *) usage ;; esac
[ "${BENCH_DISPLAY:-:141}" = ":141" ] || { echo "REFUSED: BENCH_DISPLAY '${BENCH_DISPLAY}' is not :141"; exit 2; }

# ---- load the shared file (refuse by name; 3 in the box-only mode, 2 otherwise) ----
LIBRC=2; [ "$MODE" = --preflight-only ] && LIBRC=3
libfail() { echo "REFUSED: box_preflight: $*"; exit $LIBRC; }
if [ "$MODE" != --preflight-only ] && [ "$PRE" != --preflight-only ]; then
  [ -n "${S4D1_BOX_LIB+x}${S4D1_BOX_LIB_SHA+x}" ] && libfail "S4D1_BOX_LIB / S4D1_BOX_LIB_SHA are set: only a preflight-only call may override the shared file"
fi
BOXLIB=${S4D1_BOX_LIB:-$BOXLIB_DEF}; BOXSHA=${S4D1_BOX_LIB_SHA:-$BOXLIB_SHA}
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
for v in EXE_SHA STOCK_SHA STIM_SHA USERDET_SHA; do
  case "${!v}" in PIN_ME_*) refuse "$v is still the placeholder ${!v}" ;; esac
done
nodigits() { case "$1" in ''|*[!0-9]*|0?*) return 0 ;; esac; return 1; }   # not a number of digits without a leading zero
# the scorer's own value of a CONST key: digits (optionally negative), the placeholder name, or "" if absent
scorer_const() { tr -d '\r' < "$S4/s4d1_score.py" 2>/dev/null | sed -n 's/^ *"'"$1"'": *\(-\{0,1\}[0-9][0-9]*\|"[A-Z0-9_]*"\), *#.*$/\1/p' | head -1 | tr -d '"'; }
TIMEOUT=""
if [ "$MODE" = series ]; then
  for v in PROBE_BOOT_S PROBE_S3_FRAMES; do
    case "${!v}" in PROBE_*) refuse "$v is still the placeholder ${!v} (prereg 2.3: filled from the GO probe)" ;; esac
  done
  for v in SCORER_SHA SELFTEST_SHA; do
    case "${!v}" in REG_ME_*) refuse "$v is still the placeholder ${!v} (set at registration, prereg 2.5)" ;; esac
  done
  case "$PROBE_BOOT_S" in PROBE_*) ;; *)
    if nodigits "$PROBE_BOOT_S" || [ "$PROBE_BOOT_S" -lt 1 ]; then refuse "PROBE_BOOT_S '$PROBE_BOOT_S' is not a positive number without a leading zero"
    else TIMEOUT=$(( (5 * PROBE_BOOT_S + 59) / 60 * 60 )); [ "$TIMEOUT" -lt 1900 ] && TIMEOUT=1900; fi ;;   # prereg 2.3
  esac
  case "$PROBE_S3_FRAMES" in PROBE_*) ;; *)
    if nodigits "$PROBE_S3_FRAMES"; then refuse "PROBE_S3_FRAMES '$PROBE_S3_FRAMES' is not a number without a leading zero"
    else _sc=$(scorer_const PROBE_S3_FRAMES)   # the scorer decides S3 / TWB2 from ITS value: the boot list must agree
      [ "$_sc" = "$PROBE_S3_FRAMES" ] || refuse "PROBE_S3_FRAMES here is $PROBE_S3_FRAMES, the scorer's CONST says '${_sc:-absent}' ($S4/s4d1_score.py)"; fi ;;
  esac
fi
[ "$bad" = 0 ] || exit 2
if [ "$MODE" = series ]; then TIMEOUT_RUN=$TIMEOUT; else TIMEOUT_RUN=$TIMEOUT_PROBE; fi
# Claim length, extended by its id before every boot (gates, renew_call). It must outlast, from one renewal to the next
# (re-check 2026-11-28: the first sum, +1425, missed the options copy's box_fresh, the watch pass itself and the witness
# move's box_fresh). Box pass bounds (common/box_preflight.sh): box_preflight_all and box_fresh <= 40 s each (title
# checker 10 s + Sober flatpak 10 s + systemctl 10 s timeouts, the rest file reads and one ps scan).
#   GATE_AFTER_S 150   the gates after renew_call: display wait 20 s + cfg0, ltx_known, pins (~1 s hashing measured;
#                      10 s allowed) + box_gate 40 + the options copy's box_guarded (box_fresh) 40 + box_gate 40
#   boot               TIMEOUT_RUN + WATCH_EXTRA_S (the watch cap) + one watch pass that sees it: WATCH_NAP_S + its
#                      checks WATCH_CHK_S 30 (title 10 + flatpak 10 + systemctl 10; lock, claims, EXCLUSIVE read files)
#   stop there         CHILD_GRACE_S + 1 (stop_child's 1-s poll) + box_wine_kill 60 (-k 30 + -w 30) + ENG_GONE_S
#   AFTER_BOOT_S 80    after a normal return: the witness move's box_guarded 40 + the next gates' box_gate 40 (engines_gone
#                      after a normal return is the same 120 s as in the stop line, which is counted instead)
# Sum (both the stop and the after-boot steps counted, an upper bound): TIMEOUT_RUN + 900 + 5 + 30 + 150 + 250 + 1 + 60
# + 60 + 80 = TIMEOUT_RUN + 1536 s, whole minutes (probe: 3436 s -> 58 min). Refused above 180 min (the fleet's slice
# limit; the claim tool sets no maximum of its own).
WATCH_CHK_S=30; GATE_AFTER_S=150; AFTER_BOOT_S=80
CLAIM_MIN=$(( (TIMEOUT_RUN + WATCH_EXTRA_S + WATCH_NAP_S + WATCH_CHK_S + GATE_AFTER_S + CHILD_GRACE_S + 1 + 60 + ENG_GONE_S + AFTER_BOOT_S + 59) / 60 ))
[ "$CLAIM_MIN" -le 180 ] || { echo "REFUSED: one claim period would be $CLAIM_MIN min (> 180, the fleet's slice limit): BENCH_TIMEOUT ${TIMEOUT_RUN}s is too long"; exit 2; }
BRK2=0; [ "$MODE" = series ] && [ "$PROBE_S3_FRAMES" -ge "$DROP_MIN" ] && BRK2=1   # prereg 2.3, 6.1

pin() {   # <file> <full sha256>: by name
  local f=$1 want=$2 got
  [ -e "$f" ] || { echo "REFUSED: missing file $f"; return 1; }
  got=$(sha256sum < "$f" 2>/dev/null | cut -d' ' -f1)
  [ "$got" = "$want" ] || { echo "REFUSED: $f is not ${want:0:16} (it is ${got:0:16})"; return 1; }
}
cfgval() { awk -v k="$1" '{gsub("\r","")} $1==k{v=$2; f=1} END{if (f) print v; else print "<absent>"}' "$2" 2>/dev/null; }   # <key> <file>
cfg0() {   # prereg 6.4 cfg0 (tools/s4c_partA.sh:23), as fixed 2026-11-28 (s4d2's U-1): the settings the engine RUNS with
  # have both switches at 0. That is out/user_det.ltx.orig (pinned: USERDET_SHA), which det_run.sh copies over
  # run/appdata/user.ltx right before the engine start (det_run.sh:213-216, :231). A missing sim_tick line reads as 0:
  # the engine default, wt-simtick/src/xrEngine/device.cpp:465 (`int g_sim_tick_hz = 0;`; line 464 at 6acb77044), the
  # console command `sim_tick` (xr_ioc_cmd.cpp CMD4 CCC_Integer 0..240); the only other setter is the `-sim_tick <hz>`
  # start switch (device.cpp:618-622), which no boot of this driver passes (BASE, lever). r__framelimit must be present
  # and 0 (line 553). Also prereg 6.4: that file "has g_legs 1 and r__actor_shadow on" (lines 261, 537).
  local v
  [ -s "$USERDET" ] || { echo "REFUSED: missing file $USERDET"; return 1; }
  v=$(cfgval sim_tick "$USERDET"); [ "$v" = "<absent>" ] && v=0
  [ "$v" = 0 ] || { echo "REFUSED: sim_tick is '$v' in $USERDET, not 0"; return 1; }
  v=$(cfgval r__framelimit "$USERDET")
  [ "$v" = 0 ] || { echo "REFUSED: r__framelimit is '$v' in $USERDET, not 0"; return 1; }
  v=$(cfgval g_legs "$USERDET")
  [ "$v" = 1 ] || { echo "REFUSED: g_legs is '$v' in $USERDET, not 1 (prereg 0, 6.4)"; return 1; }
  v=$(cfgval r__actor_shadow "$USERDET")
  [ "$v" = on ] || { echo "REFUSED: r__actor_shadow is '$v' in $USERDET, not on (prereg 0, 6.4)"; return 1; }
}
ltx_known() {   # N4-12 / prereg change log 2026-11-28: det_run.sh swaps user.ltx only while it is one of these two files
  [ -f "$CFG" ] || { echo "REFUSED: missing file $CFG"; return 1; }
  cmp -s -- "$CFG" "$USERORIG" || cmp -s -- "$CFG" "$USERDET" || {
    echo "REFUSED: $CFG is neither $USERORIG nor $USERDET (a stopped boot left it, or Caden's settings): det_run.sh would refuse its swap; restore by hand"; return 1; }
}
flags_ok() {   # prereg 6.4: s4c_partA.sh:10-16: BASE differs from the 11-19 method exactly by ALLOWED
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
  pin "run/bin/$EXE" "$EXE_SHA" || f=1
  pin "$STIM" "$STIM_SHA" || f=1          # prereg 1: installed before the probes, stays for the whole series
  pin "$USERDET" "$USERDET_SHA" || f=1
  pin "$SAVE_P" "$SAVE_P_SHA" || f=1
  pin "$SAVE_C" "$SAVE_C_SHA" || f=1
  pin "$AXR_SRC" "$AXR_SHA" || f=1
  [ -e "$USERORIG" ] || { echo "REFUSED: missing file $USERORIG (det_run.sh restores it)"; f=1; }
  if [ "$MODE" = series ]; then
    pin "run/bin/$STOCK" "$STOCK_SHA" || f=1
    pin "$S4/s4d1_score.py" "$SCORER_SHA" || f=1
    pin "$S4/s4d1_selftest.sh" "$SELFTEST_SHA" || f=1
  fi
  return $f
}
dumps_left() { ls "$LOGS"/*.mdmp >/dev/null 2>&1; }   # a dump already in the logs folder

# ---- the probe reading (prereg 2.1: written once by the scorer; its last line is "S4D1 PROBE: GO" or "... STOP (n rules)")
last_line() { tr -d '\r' < "$1" 2>/dev/null | awk 'NF{l=$0} END{print l}'; }   # last non-empty line, CR stripped
RD0=$S4/probe_readings.txt

# ---- the boots: tag|exe|lever (prereg 2.1, 6.1, 6.2 table; the mask always explicit) ----
plan() {   # prints the boot list of this mode, one "tag|exe|lever-or-empty" per line, in the registered order
  case "$MODE" in
    probe)
      echo "s4d_probe|$EXE|-det_synth_k 3 -sim_4d_mask 7 $STIMSW"
      echo "s4d_probe15|$EXE|-det_synth_k 3 -sim_4d_mask 15 $STIMSW" ;;
    series)
      echo "s4d_s1|$STOCK|"; echo "s4d_o1|$EXE|"; echo "s4d_s2|$STOCK|"; echo "s4d_o2|$EXE|"; echo "s4d_s3|$STOCK|"; echo "s4d_o3|$EXE|"
      echo "s4d_l2a|$EXE|-det_synth_k 2 -sim_4d_mask 0"
      echo "s4d_l3|$EXE|-det_synth_k 3 -sim_4d_mask 0"
      echo "s4d_l1|$EXE|-det_synth_k 1 -sim_4d_mask 0"
      echo "s4d_l2w|$EXE|-det_synth_k 2 -sim_4d_mask 0 -det_synth_wallclock"
      echo "s4d_l2b|$EXE|-det_synth_k 2 -sim_4d_mask 0"
      echo "s4d_off2a|$EXE|-det_synth_k 2 -sim_4d_mask 0 $STIMSW"
      echo "s4d_on2a|$EXE|-det_synth_k 2 -sim_4d_mask 7 $STIMSW"
      echo "s4d_off3|$EXE|-det_synth_k 3 -sim_4d_mask 0 $STIMSW"
      echo "s4d_on3|$EXE|-det_synth_k 3 -sim_4d_mask 7 $STIMSW"
      echo "s4d_off2b|$EXE|-det_synth_k 2 -sim_4d_mask 0 $STIMSW"
      echo "s4d_on2b|$EXE|-det_synth_k 2 -sim_4d_mask 7 $STIMSW"
      echo "s4d_brk1|$EXE|-det_synth_k 2 -sim_4d_mask 7 -det_synth_tw -sim_4d_break 1 -det_synth_walk -det_synth_tracer -det_synth_item"
      [ "$BRK2" = 1 ] && echo "s4d_brk2|$EXE|-det_synth_k 2 -sim_4d_mask 7 -det_synth_tw -sim_4d_break 2 -det_synth_walk -det_synth_tracer -det_synth_item"
      return 0 ;;
  esac
}
PROBE_DIRS="out/det_s4d_probe out/det_s4d_probe15"
probe_boot_s() {   # prereg 2.3 PROBE_BOOT_S: the larger det_run.sh `rc=0 elapsed=<n>s` of the two probe boots' logs, or rc 1
  local f v m=""
  for f in "$ST/probe/run_s4d_probe.log" "$ST/probe/run_s4d_probe15.log"; do
    v=$(tr -d '\r' < "$f" 2>/dev/null | sed -n 's/^rc=0 elapsed=\([0-9][0-9]*\)s .*$/\1/p' | tail -1)
    [ -n "$v" ] || return 1
    { [ -z "$m" ] || [ "$v" -gt "$m" ]; } && m=$v
  done
  echo "$m"; }
# ITEM (prereg Amendment 1, 2026-11-28; it reverses the coordinator's NOITEM rule of the same day): the shared stimulus
# takes an item into the actor's hands only with -det_synth_item (zz_eperf_synth.script:20, :68). Every boot that
# carries the stimulus (-det_synth_walk) must carry it, and no other boot (parts A, L; BASE) may. Refused by name.
case " $BASE " in *" -det_synth_item "*) refuse "-det_synth_item is in BASE (only stimulus boots carry it, prereg Amendment 1)" ;; esac
while IFS='|' read -r _t _e _l; do
  case " $_l " in
    *" -det_synth_walk "*) case " $_l " in *" -det_synth_item "*) ;; *) refuse "$_t carries the stimulus (-det_synth_walk) without -det_synth_item (prereg Amendment 1)" ;; esac ;;
    *" -det_synth_item "*) refuse "$_t carries -det_synth_item without the stimulus (-det_synth_walk): only stimulus boots carry it (prereg Amendment 1)" ;;
  esac
done < <(plan)

# ---- preflight (nothing below this block boots anything until it passed) ----
flags_ok || bad=1
cfg0 || bad=1
ltx_known || bad=1
pins_all || bad=1
box_ok_display_free "$DISP" || refuse "$BOX_WHY"
dumps_left && refuse "a crash dump is already in $LOGS ($(ls "$LOGS"/*.mdmp 2>/dev/null | head -3 | tr '\n' ' ')): not this run's; not moved"
SD=$ST/$MODE                     # this attempt's state folder: out/s4d1_state/<probe|series>, new
[ -e "$SD" ] && refuse "$SD exists (the state folder of $MODE must be new)"
# prereg 6.4: no run/appdata/logs/synth_s4d_*.txt and no out/det_s4d_* (series: except the two probe folders)
for f in "$LOGS"/synth_s4d_*.txt; do [ -e "$f" ] && refuse "$f exists (a witness was not moved)"; done
for f in out/det_s4d_*; do
  [ -e "$f" ] || continue
  if [ "$MODE" = series ]; then case " $PROBE_DIRS " in *" $f "*) continue ;; esac; fi
  refuse "$f exists"
done
case "$MODE" in
  probe)
    [ -e "$RD0" ] && refuse "$RD0 exists: the probes were read already (no re-probe mode: NOTES.md 9)" ;;
  series)
    if [ ! -e "$RD0" ]; then refuse "missing file $RD0 (no probe reading, prereg 2.1)"
    else [ "$(last_line "$RD0")" = "S4D1 PROBE: GO" ] || refuse "$RD0 does not end in 'S4D1 PROBE: GO' (last line: '$(last_line "$RD0")')"; fi
    for f in $PROBE_DIRS; do [ -d "$f" ] || refuse "missing folder $f (the probe the GO reading was made from; review :337)"; done
    if _pb=$(probe_boot_s); then   # N-4: PROBE_BOOT_S must be the probe logs' own number (prereg 2.3)
      case "$PROBE_BOOT_S" in PROBE_*) ;; *) [ "$_pb" = "$PROBE_BOOT_S" ] || refuse "PROBE_BOOT_S is $PROBE_BOOT_S, the probe logs say $_pb ($ST/probe/run_s4d_probe*.log)" ;; esac
    else refuse "missing file or line: $ST/probe/run_s4d_probe.log and run_s4d_probe15.log must each hold 'rc=0 elapsed=<n>s' (PROBE_BOOT_S's source)"; fi ;;
esac
[ "$bad" = 0 ] || exit 2
[ "$PRE" = "--preflight-only" ] && { echo "preflight ok ($MODE): $(plan | wc -l) boot(s), timeout ${TIMEOUT_RUN}s"; plan | cut -d'|' -f1,2 | tr '\n' ' '; echo; exit 0; }

# ---- state, log, one way out ----
mkdir -p "$ST" 2>/dev/null && mkdir "$SD" 2>/dev/null && : > "$SD/series.log" && : > "$SD/done.txt" && : > "$SD/boots.txt" && : > "$SD/claim_calls.txt" || {
  echo "REFUSED: cannot create the state folder $SD"; exit 2; }
log() { echo "$(date '+%F %T') $*" | tee -a "$SD/series.log"; }
WATCHWHY=""; sc=""; stop=""; rc=0; CLAIMTRY=0; CLAIMED=0; WANTED=0; OURQ=0; BOOTED=0; NORELEASE=0; NORELEASE_WHY=""; RETRIED=0; LIVE=""; BP=""; SP=""; SIGPEND=0
TOKEN="s4d1-$MODE-$(date +%Y%m%d%H%M%S)-$$"   # in the claim's <what>, LAST (N4-9): only THIS run's claim is ever released
WHAT="headless Xvfb GAMMA A-Life equivalence series s4d1 ($MODE, $(plan | wc -l) boots, display $DISP) $TOKEN"
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
engines_gone() {   # after our det_run.sh ended, no engine of the run tree may still use run/prefix; else wineserver -k
                   # there and wait (from date, ENG_GONE_S). Still one left: the claim is NOT released (NORELEASE).
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
  log "end ($MODE): $(grep -c . "$SD/done.txt") boot(s) done, retries $RETRIED. $(cat "$SD/stop.txt" 2>/dev/null)"
  timeout 60 "$T/notify-overseer.sh" engineperf - >/dev/null 2>&1 <<<"EnginePerf s4d1 $MODE: ended $(date +%T), $(grep -c . "$SD/done.txt") boot(s) done, retries $RETRIED. $(cat "$SD/stop.txt" 2>/dev/null). $([ "$sc" = done ] && { [ "$MODE" = series ] && echo "Not scored yet (by hand, once: out=$S4/s4d1_score_result.txt)." || echo "Not read yet (by hand, once: out=$RD0)."; } || echo "Stopped: nothing is resumed without the supervisor's word.")"
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
# The queue is keyed by the worker NAME only (common/README.md, S-3 known limit): want only when no entry exists (then the
# entry is this run's: OURQ=1); withdraw only if OURQ=1 and exactly one entry exists then.
want_call() {
  box_queue_count $W || { log "queue unreadable ($BOX_WHY): want not sent"; return 0; }
  [ "$BOX_QN" -gt 0 ] && return 0   # an entry under our name exists (ours, or another job's: either way not re-sent)
  qr want $W && { WANTED=1; OURQ=1; }; return 0; }   # SHARED: no EXCLUSIVE word (prereg 7: shared, every run)
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

# ---- one SHARED claim for the whole run (prereg 6.1 "one shared claim", 7 "shared, every run"; capped wait) ----
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
log "claim granted ($MODE, $TOKEN, $CLAIM_MIN min), load $(cut -d' ' -f1-3 /proc/loadavg), timeout per boot ${TIMEOUT_RUN}s, s4d_brk2 $([ "$BRK2" = 1 ] && echo booted || echo 'not booted')"

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
  # Called in THIS shell (never inside `$(...)`, where the sticky BOX_STOPPED of box_guarded would be lost):
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
  for _ in $(seq 1 "$DISP_WAIT_N"); do box_ok_display_free "$DISP" && break; nap "$DISP_WAIT_S"; done
  box_ok_display_free "$DISP" || { halt fault "$BOX_WHY (before $t)"; return 1; }
  r=$(cfg0) || { halt fault "$r (before $t)"; return 1; }
  r=$(ltx_known) || { halt fault "$r (before $t)"; return 1; }
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
  [ -e "out/det_$t" ] && { halt fault "out/det_$t exists before its boot"; return 1; }
  [ -e "$LOGS/synth_$t.txt" ] && { halt fault "$LOGS/synth_$t.txt exists before its boot"; return 1; }
  return 0
}
after() {   # <tag> <lever>: move the witness, check what every boot must leave; 1 = stop (halt set)
  local t=$1 lv=$2 d=out/det_$1 w f
  [ -d "$d" ] || { halt fault "missing folder $d after the boot"; return 1; }
  if [ -e "$LOGS/synth_$t.txt" ]; then   # a part-A boot that wrote one is moved too: the scorer's A5 then FAILs by name
    box_guarded "move $LOGS/synth_$t.txt" mv -- "$LOGS/synth_$t.txt" "$d/" || {
      [ -n "$BOX_STOPPED" ] && { halt box "$BOX_STOPPED: $LOGS/synth_$t.txt not moved"; return 1; }
      halt fault "cannot move $LOGS/synth_$t.txt into $d/"; return 1; }
    log "moved $LOGS/synth_$t.txt -> $d/"
  fi
  for f in "$d/xray.log" "$d"/det_alife_counts_"$t"_{1,2,3}.txt "$d"/det_counters_"$t"_{1,2,3}.txt; do
    [ -s "$f" ] || { halt fault "missing file $f after $t"; return 1; }
  done
  [ -z "$lv" ] && return 0
  w=$d/synth_$t.txt
  [ -s "$w" ] || { halt fault "missing file $w (the witness of lever boot $t)"; return 1; }
  return 0
}
has_seq() { case " $1 " in *" $2 "*) return 0 ;; esac; return 1; }   # whole-token match
inrun_check() {   # <tag> <lever>: prereg 6.4 in-run preflight on s4d_l2a: the counter file of cp1, the witness present and
                  # non-empty, the engine command line and det_synth line as registered (k, wallclock, tw, break, mask)
  local t=$1 lv=$2 d=out/det_$1 cl k m wc tw brk
  [ -s "$d/det_counters_${t}_1.txt" ] || { echo "missing file $d/det_counters_${t}_1.txt"; return 1; }
  [ -s "$d/det_alife_counts_${t}_1.txt" ] || { echo "missing file $d/det_alife_counts_${t}_1.txt"; return 1; }
  [ -s "$d/synth_$t.txt" ] || { echo "missing or empty file $d/synth_$t.txt"; return 1; }
  cl=$(tr -d '\r' < "$d/xray.log" | grep -a "command line" | head -1 | tr 'A-Z' 'a-z' | tr -s ' ')
  [ -n "$cl" ] || { echo "no 'command line' line in $d/xray.log"; return 1; }
  for s in "$BASE" "$lv" "-det_tag $t"; do has_seq "$cl" "$s" || { echo "engine command line lacks [$s]"; return 1; }; done
  k=$(echo "$lv" | sed -n 's/^-det_synth_k \([0-9]*\) .*/\1/p'); m=$(echo "$lv" | sed -n 's/.*-sim_4d_mask \([0-9]*\)\( .*\)*$/\1/p')
  wc=0; has_seq "$lv" -det_synth_wallclock && wc=1
  tw=0; has_seq "$lv" -det_synth_tw && tw=1
  brk=$(echo "$lv" | sed -n 's/.*-sim_4d_break \([0-9]*\)\( .*\)*$/\1/p'); brk=${brk:-0}
  { [ -n "$k" ] && [ -n "$m" ]; } || { echo "no K or mask in the lever [$lv]"; return 1; }
  [ "$(tr -d '\r' < "$d/xray.log" | grep -acx ".*\* det_synth: k $k wallclock $wc tw $tw break $brk mask $m *")" = 1 ] \
    || { echo "not exactly one det_synth line 'k $k wallclock $wc tw $tw break $brk mask $m' in $d/xray.log"; return 1; }
  return 0
}
watch_boot() {   # in THIS shell (no subshell: traps act at once): while our det_run.sh runs, the box conditions
                 # every WATCH_NAP_S (engine_running excluded: ours runs), and the wall cap (N4-11); sets WATCHWHY,
                 # returns 1 to stop (WATCHCLS box or fault)
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
boot() {   # <tag> <exe> <lever>: one det_run.sh boot in the background; returns its rc, 9 crash dump, 8 watch stop
  local t=$1 e=$2 lv=$3 r t0
  t0=$(date +%s)
  log "boot $t exe=$e extra=[$BASE${lv:+ $lv}] display=$DISP timeout=${TIMEOUT_RUN}s load $(cut -d' ' -f1-3 /proc/loadavg)"
  LIVE=$t; BOOTED=1; SIGPEND=0; trap 'SIGPEND=1' TERM INT HUP   # review N-1 / (e): held until BP is recorded
  DET_NOCLAIM=1 DET_BOX_TOKEN=$TOKEN DET_BOX_CLASS=shared DET_BOX_WORKER=$W BENCH_SAVE=bench1 \
  BENCH_DISPLAY=$DISP BENCH_TIMEOUT=$TIMEOUT_RUN DET_EXTRA="$BASE${lv:+ $lv}" \
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
    [ "$brc" = 0 ] && { echo "$t" >> "$SD/done.txt" || { halt fault "cannot append to $SD/done.txt"; finish; }; }   # completed boots only
    case $brc in
      0) ;;
      8) halt "$BOXCLS" "during $t: $BOXWHY (this run's boot stopped)"; finish ;;
      9) halt fault "crash dump after $t"; finish ;;
      10) halt fault "an engine of the run tree still used run/prefix after $t (det_run.sh rc ${DETRC:-?}): ${NORELEASE_WHY:-see the log}"; finish ;;
      3) halt box "det_run.sh rc 3 during $t: its box preflight refused, or it aborted for the play lock, a title or Sober ($(grep -aE '^(REFUSED|ABORT)' "$SD/run_$t.log" | head -2 | tr '\n' ' '))"; finish ;;
      4|5) # prereg 6.5: rc != 0 "(timeout, early exit)" = det_run.sh rc 5 / rc 4, no crash dump (rc 9 above): one retry
           # of THIS slot, at once, as <tag>_r1 with the same switches; a second failure of the same slot stops. Series only.
         if [ "$MODE" = series ] && [ "$t" = "$tag" ]; then
           RETRIED=$((RETRIED + 1)); t=${tag}_r1; log "boot $tag ended rc $brc ($([ "$brc" = 5 ] && echo timeout || echo 'early exit'), no crash dump): its one retry, as $t"
           # the failed boot may have left a witness in the logs folder: keep it with its own folder (checked)
           if [ -e "$LOGS/synth_$tag.txt" ]; then
             if [ -d "out/det_$tag" ] && box_guarded "move $LOGS/synth_$tag.txt" mv -- "$LOGS/synth_$tag.txt" "out/det_$tag/"; then log "moved $LOGS/synth_$tag.txt -> out/det_$tag/"
             elif [ -n "$BOX_STOPPED" ]; then halt box "$BOX_STOPPED: $LOGS/synth_$tag.txt not moved"; finish
             else log "NOT moved: $LOGS/synth_$tag.txt (out/det_$tag missing or mv failed); it stays in $LOGS and does not clash with $t"; fi
           fi
           continue
         fi
         halt fault "boot $t ended rc $brc ($([ "$brc" = 5 ] && echo "timeout ${TIMEOUT_RUN}s" || echo 'early exit'))$([ "$MODE" = series ] && echo ', the second failure of this slot (prereg 6.5)' || echo ' (probe: no retry, prereg 2.1)')"; finish ;;
      *) halt fault "boot $t ended rc=$brc (pre-check inside det_run.sh)"; finish ;;
    esac
    after "$t" "$lv" || finish
    if [ "$tag" = s4d_l2a ]; then
      r=$(inrun_check "$t" "$lv") || { halt fault "in-run preflight on $t failed: $r"; finish; }
      log "in-run preflight on $t ok"
    fi
    break
  done
done 3< "$SD/plan.txt"
sc=done; stop=""
if [ "$MODE" = probe ]; then   # no judgement in the copy step: the number and the reader's bytes, from this run's own files
  if _pb=$(probe_boot_s); then echo "PROBE_BOOT_S=$_pb" > "$SD/probe_boot_s.txt"; log "copy into s4d1_run.sh: PROBE_BOOT_S=$_pb (the larger det_run.sh elapsed of the two probes, prereg 2.3)"
  else log "PROBE_BOOT_S: no 'rc=0 elapsed=' line in both run logs"; fi
  log "read with: s4d1_score.py sha256 $(sha256sum < "$S4/s4d1_score.py" | cut -d' ' -f1), s4d1_selftest.sh sha256 $(sha256sum < "$S4/s4d1_selftest.sh" | cut -d' ' -f1) (review N-3)"
fi
log "all boots of $MODE done. Next (by hand, once): $([ "$MODE" = series ] && echo "python3 -I $S4/s4d1_score.py root=out out=$S4/s4d1_score_result.txt" || echo "python3 -I $S4/s4d1_score.py mode=probe root=out out=$RD0")"
finish
