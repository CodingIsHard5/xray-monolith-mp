# kj3p: X-ONLY emission preflight (DRAFT, 2026-11-26). Not registered. No boot before the supervisor's GO.

Drafted by an Opus sub-worker at the desk (no engine, no Wine, no display, no claim, no git). EnginePerf reads, fills the
`P = ?` lines and the PIN_ME pins, and registers. Everything below about the engine is from the scripts and earlier runs;
nothing of it has been seen in an emission yet.

## 0. Limits first
- One boot, X-Ray only, a stand-in pose writer, no ITR2. Nobody looks or listens: sky, sounds, pp effects, HUD, rockets and
  the hide task are not judged.
- **The actor's exposure cannot be shown.** Under g_god the engine stops hits before `actor_on_before_hit`: in
  `out/t3pfE3_prot` (g_god on) the bridge saw 0 hits in the same fields that gave 1230 with god off (`out/t3pfE3_god_off`).
  So the surge's psi hits, the wave-1 hit and the vortex shock never reach the bridge, in cover or not. F3 can show "unhurt",
  not "unhurt although exposed". Whether the track's start point (-220.99, -19.91, -144.99, near the rookie village) is
  inside one of the surge covers is NOT known; the run has no line that says so (`GetEvent("current_safe_cover")` and the
  surge's `1stwavehit` stage are not exported).
- psy_health is not read by the bridge (VITALS has health, radiation, bleeding). A telepatic hit that lowered psy health only
  would not show; under g_god none is expected to land.
- The stand-in writer is kw1's (`tools/t2_kill_pose.py`, unchanged): its stance field cycles 0/1/2 every 10 s of its own
  clock (`int(t // 10) % 3`). The actor crouches on and off in place. Not believed to matter for an emission; not tested.
- The X-only play clock starts ~15 s later in engine time than a joint run's: the writer waits for a snapshot with track
  time >= 0 (the 15 s warm-up), while ITR2 in kj2 was read 1 s after the first snapshot. The event timelines are relative
  to each event's own start, so they are unaffected; only the run length had to grow (section 2).
- The checker has read real engine bytes only from the kj2 folder (kill5, no e lines). The e/w/v/ns/E* lines have been
  produced only by the kill6 reader in the Lua stub, whose fake managers are my model of the scripts. The first real run is
  also the first time the checker reads these lines from the engine.
- The kj2 checker's Lua-error search (`LUA error` / `<name>.script (line`) misses the engine's runtime-error forms
  `<path>\<name>.script:16: attempt ...` and `<name>.script(325) : fn` (seen in real logs under out/). kj3p's checker
  searches all three forms. kj2's registered checker is not changed (report only).

## 1. Questions (for the partner itrzone and the later joint witness run)
- F1. Do `surge_manager` / `psi_storm_manager` resolve from the bridge in the real engine, and does a forced start through
  `emtest.txt` work on Cordon (`EFORCE,…,started`), or does the level / option / veto refuse it (`notstarted`)?
- F2. The real printed form of `<tf>` in the e line, and whether t runs as the desk table says: 222 s surge, 103 s psi;
  phases (kj3/emission-phases.md) as EPHASE lines in order; w lines for wave 1 and wave 2; v lines in the vortex window with s
  in the stated range at the strike.
- F3. Does the actor stay alive and unhurt through both events under the bridge's god mode, standing at the track's start?
- F4. eerr = 0 at the end, no EERR line; ENOTREADY absent on a save load (desk prediction).
- F5. Stalkers removed at the emission end (fate `explode`), psi stalkers turned to zombies under new ids: counts, reported.
- F6. Snapshot export cost with the new lines (SUMMARY / TICK export stats) against kj2: reported.

## 2. What is run
| item | value |
|---|---|
| command | `BENCH_DISPLAY=:141 wt-render-split/perf-harness/kj3/kj3p_run.sh out/kj3p` (from /home/caden/Projects/StalkerGameEngine, as a harness background task) |
| driver | `kj3/kj3p_run.sh`: hash preflight (section 12), display check (refuses :0, an existing X socket or lock), want + claim through `quiet-required.sh` by EXIT CODE (claim 30 min, WHAT `EXCLUSIVE: headless X-Ray-only B1-T2 kj3p …`, which the runner's own claim test needs), no start while `protected-title-check.sh` exits 0 or `pgrep -x sober`, a 10 s watchdog that stops the run if either starts, stand-in writer + runner, leftover-engine stop, withdraw then release |
| runner | `tools/t2_xray_kj3.sh <out>/run none-itr2` (kj2's runner + the emission triggers; installs kill6 as `zz_p1_puppet.script`, removes it and `emtest.txt` at cleanup) |
| runner env | `T2_EXE=Stance14.exe T2_FREEZE=sigstop T2_ABSORB=2000 T2_STALL_AT=99999 T2_KILL_AT=99999 T2_MAXRESTART=0 T2_LOST_MS=5000 T2_LOSTX_MS=120000 T2_OVER_S=120 T2_STALL_PLAY=0 T2_RUN_S=465 T2_WALL_S=900 T2_SURGE_PLAY=70 T2_PSI_PLAY=310` (kj2's flags; no stop; the two triggers; the run length below) |
| exe | `run/bin/Stance14.exe` sha256 b4ce9c77… (kj2's) |
| save | `cordon1` (`run/appdata/savedgames/cordon1.scop` 6dea752b…, kj2's) |
| display | `:141`, headless, its own; never :0 |
| pose feed | `python3 -I tools/t2_kill_pose.py /dev/shm/itrz_b1 <out>/pose_written.csv kj3/kj3p_track_start.csv aw=keep sess=<utc ms> pick=99999 snaplog=<out>/writer_snaps.csv`: pose v4 at 20 Hz, state play, away policy keep, **no k line ever** (pick at 99999 s), one point: the one-row track = the header and first data row of ITRZone's `cordon_full_track.csv` (the driver refuses if they differ). First pose at the first snapshot with track time >= 0 |
| reader | `harness/zz_p1_puppet_kill6.script` 7d7c173d… (read only); test mode on because the runner makes `emtest.txt` empty before the boot |
| kept | `boot0/watch/snaps_all.txt`, `poses_all.txt` (t1_watch_keep.py), `boot0/meta_t2.csv`, `hits_t2.csv`, `boot0/tb/xray.log`, `emforce.csv`, `t2_xray.log`, `restarts.csv` |
Plan on the runner's play clock (play0 = header utc of the first play pose the runner reads, the kj1-kj3 T2_STALL_PLAY clock):
| play s | what | expected in X-Ray (desk) |
|---|---|---|
| 0 | first pose; actor moved to the start point and held there | e,none,0.00,0,6 in every snapshot |
| 70 | runner appends `surge` | EFORCE,…,surge,started within 1 s; ESTART; factor 10; phases 1..13; waves at t 120 / 168 |
| ~293 | surge ends (t ~ 221-222) | EEND surge; factor 6 again |
| 310 | runner appends `psi` | EFORCE psi started; ESTART psi; vortices from t 33..36 every 3..6 s, last created <= 77 |
| ~414 | psi storm ends (t ~ 102-103) | EEND psi |
| ~449 | engine run ends (465 engine s from the level load = 15 s warm-up + 440 + 10) | SUMMARY, T2SUM (eerr last), VITALS |
X-only needs no other forcing numbers than the partner's 70 / 310 / 440. It needs a longer engine run (465 s, not 455)
because its play clock starts after the 15 s warm-up (section 0). Gap between the surge's end and the psi force: ~17 s
(PSM:313 would cancel a psi storm forced into a running surge; NV4 catches that).

## 3. Predictions (P = my probability; EnginePerf fills; suggested numbers in brackets)
| # | prediction | P |
|---|---|---|
| p1 | both managers readable; both forced starts `started` | P = 0.75 (EnginePerf) [0.80; the refusals I can name: Cordon not in `level_weathers.valid_levels`, a barrier-defense veto, both INFERRED absent] |
| p2 | tf printed `10` during events and `6` outside (LuaJIT `tostring` of an integral float) | P = 0.85 (EnginePerf) [0.90] |
| p3 | t tracks engine time within 0.5 s through each event; EEND t in (221, 222] / (102, 103] | P = 0.75 (EnginePerf) [0.85] |
| p4 | EPHASE surge 1..12 (+13), psi 1..5 (+6), each within 0.1 s after X-1 | P = 0.65 (EnginePerf) [0.80] |
| p5 | w,1 from t ~120 and w,2 from t ~168, 250 m north, -15 m/s | P = 0.50 (EnginePerf) [0.65; `effect:playing()` right after `play_at_pos`, and `stop_deffered` keeping it true after the stop, are not known] |
| p6 | 8-15 vortices at start times 33..77, gaps 3..6, each line gone within (-0.1, 1.1] s of s = 0 | P = 0.45 (EnginePerf) [0.60; the `==` test can end the chain early; PVF's 3..6 applies only if it wrapped initialize before the instance was made] |
| p7 | actor health 1.0000 in every snapshot; VITALS 1.000 / 0.0000 / 0.0000; 0 hits seen | P = 0.90 (EnginePerf) [0.90] |
| p8 | eerr 0, no EERR, no ENOTREADY | P = 0.85 (EnginePerf) [0.90] |
| p9 | F5: some stalkers removed at the surge's end (fate explode); zombies only if a stalker stood within 50 m of a strike | P = reported only (EnginePerf) [reported only] |
| p10 | F6: export mean within +10 % of kj2's 795.8 us | P = 0.60 (EnginePerf) [0.70; reported only] |
Outcome guess (EnginePerf): PASS 20 %, NO VERDICT 20 %, FAIL 60 % (the sub-worker suggested 35 / 20 / 45). The checks are many, new, and the bounds are derived from
scripts never run in an emission; a FAIL that shows a desk line wrong is a useful answer, not a broken run.

## 4. Checks (kj3/kj3p_check.py). PASS = all of them; F5, F6 are `reported:` lines.
| id | outcome judged | bound |
|---|---|---|
| 1 | runner rc 0, no restart row, one boot, no FATAL, no bridge Lua error (three log forms), every snapshot on l01_escape with one run id, no kept pose with a k line, last kept snapshot at play >= 440 | exact; 440 s |
| 2 | F1: emforce rows exactly [surge, psi]; one EFORCE per kind; result `started` or `notstarted` (an `error:…` or `unknown` FAILS); runner append at play 70 / 310 s + <= 2.5 s; EFORCE 250 ms early to 2.5 s late after the append (X mapped to W by the newest P2 line); no Lua error naming surge_manager / psi_storm_manager | see 6 |
| 3s / 3p | one ESTART per kind, 0..1000 ms after its EFORCE (X), t 0..1.0 | 1000 ms; 1.0 |
| 4s / 4p / 4n | F2: float(tf) == 10 in every surge / psi snapshot, == 6 in every none snapshot (at least one each); the printed forms are reported | exact |
| 5s / 5p | F2: max - min of (t - X/1000) over the event's snapshots | <= 0.5 s |
| 6s / 6p | F2: one EEND, its t in [220.5, 223.0] / [101.5, 104.0], no event snapshot after it | see 6 |
| 7s / 7p | F2: EPHASE phases exactly 1..12 (13 optional) / 1..5 (6 optional), in order, each with t in (X-1, X-1+1.0] (+-0.005 print rounding); every e line's phase = the table's phase of its t | 1.0 s |
| 8 | F2: w lines only in surge snapshots, n in {1,2}; per wave: first t in (X-1, X+1.0] (X = 120 / 168), first z - actor z in [244, 250.5], least-squares speed in [-16.5, -13.5] m/s | see 6 |
| 9 | F2: v lines only in psi snapshots, ascending n; per vortex: start = s + t - 20 within 0.02 of an integer in [33, 77]; gaps between starts in [3, 16]; first s in [19.0, 21]; last s in [-1.0, 1.5]; y = actor y + 20 +- 0.05; distance <= 150.5 m | see 6 |
| 10 | F3: actor health 1.0000 in every snapshot; VITALS min_health=1.000, max_radiation=0.0000, max_bleeding=0.0000, unread=0; SUMMARY min_health=1.000, refills=0, hits = cancelled | exact |
| 11 | F4: last T2SUM has the registered 20 keys in order (eerr last); eerr, torn, lost, epochs, every k* counter, ktabfail = 0; paused false; no EERR line; no snapshot in the degraded form (e none + ns,-1,-1) | exact |
| 12 | F4: no ENOTREADY and no EREADY line | exact |
| 13 | every snapshot has the form `a o* r? e (w\|v)* ns` with w only in a surge and v only in a psi storm, no k line | exact |
Always judged (in no NO VERDICT set): 1, 2, 4n, 10, 11, 12, 13. Reported: F1 ANSWER line (managers readable, each forced start
started / REFUSED BY THE GAME / CALL FAILED), tf forms, ESTART/EEND lines, wave z where it passes the actor and its last line,
each vortex, ns at the first snapshot, hits by draftsman/type, pose points and actor drift, F5 counts (online stalkers before
each event, not listed / not alive 2 s after it, their OFF lines, new zombied ids), F6 SUMMARY + per-minute export means from
the TICK lines marked by the event they overlap, against kj2's 795.8 / 1558.1 / 15920.6 us.

## 5. NO VERDICT rules (decided before; each names what it leaves untested)
| rule | fires when | leaves untested |
|---|---|---|
| NV1 | an event the run did not force: an e kind other than none before the first EFORCE, an ESTART with no `EFORCE <kind>,started` at or before it, or a kind started twice | 3s 3p 4s 4p 5s 5p 6s 6p 7s 7p 8 9 |
| NV2 | `EFORCE,…,surge,notstarted`: the game refused the surge (level, option, on_before_surge veto). F1 is answered "no" | 3s 4s 5s 6s 7s 8 |
| NV3 | `EFORCE,…,psi,notstarted` | 3p 4p 5p 6p 7p 9 |
| NV4 | the psi storm was forced before the surge's EEND (PSM:313 cancels it) | 3p 4p 5p 6p 7p 9 |
| NV5 | the psi storm started but no v line at all: PSM:361's `==` can end the chain before the first vortex; the run cannot tell that from a reader fault | 9 |
A NO VERDICT never hides a failure it does not explain: a failing check outside the union of the fired rules' sets makes
the run a FAIL, named. Missing / unreadable / empty required input: REFUSED (exit 2), by name. Unparsable input (a malformed
E* line, a snapshot that breaks the line grammar, emforce rows): FAIL, never excused. One retry after a NO VERDICT, crash or
stop, with a new out dir, only on the supervisor's GO; registered here before the first boot: same command, `out/kj3p-r1`.

## 6. Margin table (each bound derived for THIS run; expected = what the scripts and the stub say)
| check | bound | derivation | expected | margin | flag |
|---|---|---|---|---|---|
| 1 play end | >= 440 s | the partner's plan | ~449 s (465 engine s - 15 s warm-up - <= 1.2 s writer start and runner read) | 9 s slack vs ~1.2 s jitter: 7x | - |
| 2 runner append | plan + 0..2.5 s | runner loop = `sleep 1` + body | <= 1.3 s (kj2: 757 ms late at 105 s) | 3.3x | - |
| 2 EFORCE delay, late | <= 2.5 s | the reader reads emtest.txt once per 1000 ms of X time + one frame | <= ~1.05 s (stub 0.95 s) | 2.4x | - |
| 2 EFORCE delay, early | >= -250 ms | the P2 mapping is early by the pose's age at the read (<= 50 ms writer period + a frame + read) | -50..-90 ms | 2.8x | - |
| 3 ESTART after EFORCE | 0..1000 ms | `start(true)` sets started and inited_time in the call (SM:205-246); the next snapshot <= 100 ms + a frame | 0 (same frame in the stub) to ~130 ms | 7.5x | - |
| 3 t at ESTART | 0..1.0 | as above | 0.00..0.13 | 7.5x | - |
| 4 tf | exact | set and restored in the same Lua call as `started` (SM:245 / SM:388, PSM:117 / 244); the snapshot is built in the bridge's own callback | 10 / 6 | exact | - |
| 5 t - X spread | <= 0.5 s | game time = factor x time_global (alife_time_manager_inline.h:25); t = game s / factor; quantum 0.1 s if diffSec is whole game seconds, + 0.01 print | <= 0.11 | 4.5x | - |
| 6 surge EEND t | 220.5..223.0 | end_surge at the 1 s update with ceil(t) >= 222: last t seen in (220.9, 222] | 221.0..222.0 (stub 221.90) | low 0.4 s / 0.1 s spacing: 4x; high 1.0 s / a frame: >30x | - |
| 6 psi EEND t | 101.5..104.0 | as above with 103 | 102.0..103.0 (stub 102.90) | as above | - |
| 7 phase t | (X-1, X-1+1.0] | phase = ceil(t) >= X, first snapshot after t crosses X-1 | X-1 + 0..0.13 (stub +0.10) | 7.5x above; the lower edge is the rule itself (+-0.005 for the print) | lower edge by construction |
| 8 first w t | (X-1, X+1.0] | start_wave at the update with ceil(t) >= X: t in (X-1, X], first snapshot after it | (X-1, X+0.13] (stub X+0.00) | 7x above; lower edge by construction | lower edge by construction |
| 8 first z - actor z | 244..250.5 | inited_pos.z = actor z + 250 (SM:496); <= 1 step of 3 m (2 game s = 0.2 s) before the first snapshot | 247..250 (stub 250.0) | 3 m below / one step: **2.0x** | at 2x |
| 8 speed | -16.5..-13.5 m/s | 3 m per 2 game s at factor 10 (SM:688, 1065) = -15 | -15.0 +- 0.2 (3 m steps over ~33 s) | 7x | - |
| 9 integer start | 0.02 | two %.2f roundings | <= 0.01 | **2.0x** | at 2x |
| 9 start range / gaps | 33..77 / 3..16 | PSM:357-365; PVF 3..6 or stock 8..16 (which one applies is reported) | 33..77 / 3..6 | rule | copied from the scripts |
| 9 first s | 19.0..21 | created at the update with ceil(t) = start, t in (start-1, start]; s = start + 20 - t | 19.87..21 | 6.7x low; 21 is the rule | upper edge by construction |
| 9 last s | -1.0..1.5 | strike at the first update with ceil(t) - start >= 20 (s < 1), 1 s updates; last sighting <= one snapshot before | -0.04..1.13 (stub 0.10) | low 25x; high 0.37 s over a 0.13 s spacing: 2.8x | - |
| 9 y, distance | +-0.05; <= 150.5 m | particle_pos y = actor y + 20 (PSM:410); `math.random(0, 150)` (PSM:59, 407) | 0.00; <= 150 | 25x; distance is the rule | distance by construction |
| 10, 11, 12, 13 | exact | the standing rule (the actor takes no damage) / the format | as stated | exact | - |
Flagged: two bounds at exactly 2x (8 first z, 9 integer start); four edges set by the scripts' own rules rather than by a
margin (7 and 8 lower edges, 9 first-s upper edge, 9 distance). Copied, not derived for this run: kj2's runner flags (env
row of section 2), the Lua-log noise filter of the kj2 checker (a "doesn't exist" / "nil function" line within 8 lines
before), and the 3..16 gap range taken from the two script settings. No bound is copied from another arm's table.

## 7. Stop rules
- The driver refuses on any pin mismatch, a PIN_ME pin, a display that is :0 or exists, an engine already running, a left
  `zz_p1_puppet.script`, or an existing out dir. It waits (<= 240 min) while a protected title or Sober runs; claims only by exit 0.
- During the run: T2_MAXRESTART=0 (a crash, 120 s without a first snapshot, 30 s without a new seq, or the over-run limit
  ends the run, no restart); tb_run's wall limit 900 s from launch; the watchdog stops the run within 10 s if a protected title
  or Sober starts (exit 4, not a run of record).
- After: stop on a crash, timeout, refusal or stop; no second boot without the supervisor; at most the one retry of section 5.

## 8. Duration (against a 30 min claim)
Boot to level ~29 s (kj2) + 465 engine s + quit and Wine teardown ~10 s = ~8.5 min. Claim 30 min; the worst case the runner
allows is the 900 s wall limit (15 min) + cleanup. Scoring: < 1 s (selftest: 4521 stub snapshots in 0.2 s).
Disk: kj2 kept 14 MB for 240 s with ~70 o lines a snapshot; this run ~4650 snapshots -> ~16 MB snapshots + ~6 MB dumps +
~3 MB poses/meta/writer logs, plus tb_run's own out/tb_t2b0 copy: ~30 MB in all.

## 9. What it cannot show
Exposure (section 0); psy health; anything seen or heard; ITR2's side and the partner's reading of e/w/v/ns; a natural
(scheduled) event (forced starts only); an event over a level change, a save/load, a sleep, a stall or a freeze; the
attribution of a removed stalker to the emission (no callback names it; F5 counts only); more than one start point or one
save; story characters; whether the wave and vortex fields are right for a moving actor (the actor stands).

## 10. Score once
`cd /home/caden/Projects/StalkerGameEngine && bash -c 'set -C; python3 -I wt-render-split/perf-harness/kj3/kj3p_check.py out/kj3p/run > out/kj3p/kj3p_check.txt; echo $? > out/kj3p/kj3p_check.rc'`
`set -C` refuses to overwrite an existing report (tested in the selftest). The checker itself writes nothing. No re-score
after a checker fix: the registered checker's output stands, or the run is NO VERDICT if it gives none; a fixed checker's read
is POST-HOC DESCRIPTIVE; a verdict that counts needs a new run under a re-registered checker (the Overseer's rule since kw1).

## 11. Pins (sha256; PIN_ME = EnginePerf fills at registration from the bytes it registers)
| file | sha256 |
|---|---|
| wt-render-split/perf-harness/kj3/kj3p_run.sh (driver) | PIN_ME |
| wt-render-split/perf-harness/kj3/kj3p_check.py (checker; also pinned inside the driver as PIN_ME_CHECKER) | PIN_ME |
| wt-render-split/perf-harness/kj3/kj3p_selftest.sh (PIN_ME_SELFTEST in the driver) | PIN_ME |
| wt-render-split/perf-harness/kj3/kj3p_track_start.csv (PIN_ME_TRACK_START in the driver) | PIN_ME |
| wt-render-split/perf-harness/kj3/kill6h_stub.lua (the selftest's fake engine, unchanged) | PIN_ME (2026-11-26 20:17: f864764c06848c010324aa765a26a5d7943b530f4516ff5cd11c16ad55f6f4b5) |
| tools/t2_xray_kj3.sh, harness/zz_p1_puppet_kill6.script, tools/t2_kill_pose.py, tools/t1_watch_keep.py, tools/lib_engine_pids.sh, tools/tb_run.sh, tools/run_noguard.sh, harness/zz_eperf_dump.script, out/user.ltx.orig, run/bin/Stance14.exe, run/appdata/savedgames/cordon1.scop, ITRZone out/p1/cordon_full_track.csv, run/gamedata/scripts/surge_manager.script, psi_storm_manager.script, pulse_vortex_consistency_fix.script, run/gamedata/configs/axr_options.ltx | PIN_ME each; the values measured 2026-11-26 20:05 are written into the driver's pin lines (245c7dac…, 7d7c173d…, 9f3075cd…, 9df9c8e4…, cb183b7b…, 1b8d103f…, 92729abc…, 2b79a817…, 3efd38ee…, b4ce9c77…, 6dea752b…, 213ff8f1…, aad00a89…, 5e83cea9…, e795db4d…, 27d54b9f…); all but the last five equal kj2's pins |
Not pinned and read by the game: `appdata/axr_options_play.ltx` (the desk read says the same ten event values; which of the
two the engine reads is INFERRED).

## 12. Tests on these bytes
`nice -n 19 bash wt-render-split/perf-harness/kj3/kj3p_selftest.sh` (2026-11-26 20:18:21-20:18:28, checker 5c97e95d…,
run dir kj3/runs/kj3p-st-20261126-201821-1862432, 19 MB): `KJ3P SELFTEST: PASS (fixtures: 2 STUB-MADE runs; good PASS True;
mutants failing exactly their checks 26/26; made NO VERDICT 4/4; missing-file REFUSED 8/8; real kj2 bytes ok)`. Fixtures: two STUB-MADE runs of the real kill6 reader (forced surge +
psi; no force + two MADE `notstarted` lines), 26 MADE mutants each failing exactly its listed check(s) (at least one per check
1 to 13), 4 MADE NO VERDICT fixtures (NV1, NV2+NV3, NV4, NV5), one unparsable input, 8 missing-file and 3 empty-file folders
(REFUSED by name), a CRLF copy of the good run (PASS), the real kj2 X folder read in place (REFUSED: emforce.csv missing) and
through symlinks with a made emforce.csv (FAIL: no EFORCE, no e line; checks 2 and 13 named), "the checker writes nothing",
and the set -C score-once refusal. Not tested: the driver (`bash -n` only, never run); the runner's new lines (`bash -n` only,
kj3 Stage 2); any e/w/v/ns/E* byte the engine wrote; bounds exactly at their edges.
