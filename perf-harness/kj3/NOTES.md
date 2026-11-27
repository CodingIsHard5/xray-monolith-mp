# kj3 desk work: kill6 (the three review fixes found after kj2 was registered), 2026-11-26

## Limits first
- Desk only. No game, engine, Wine, display or runner ran; no git command; no install; nothing deleted. No existing file was
  edited (kill5 still c01f0f20...3328; kj2/ unchanged). The heavy checker selftest (t3_kj2_selftest.sh) was NOT run (ordered).
- kill6 has run only in the Lua 5.1 stub. None of the new paths (stale pending row, identity mismatch, pending kill over a
  crossing) has ever happened in the engine; kj2's run did not even show a pending kill (prereg section 11).
- Identity = the SECTION name only. A replacement of the SAME section under the same id cannot be told apart from the target
  (pinned by `ident_reuse_same_section`): while it lives, the kill stays pending, the plain line is written, KNOCONF comes at
  2.0 s and kdead_fail counts 1; if it then dies, the reader logs KCONF and writes `dead` as if the target had died. GAMMA's
  generic sections (kj1/kj2's S was `sim_default_stalker_0`) make this the likely case for stalkers, not a rare one.
- An id with NO object still confirms (amendment 138, as kill5). So a target removed by an emission, or swapped by a psi
  storm (the stalker's id freed, the zombie under a new id), is answered `dead` and logged KCONF although the kill call
  did not cause it. The format cannot say "gone"; this is amendment 138's rule, unchanged.
- The clock over a crossing: kt is carried as raw time_global, like kill5's dd. From the engine source (not a run):
  `TimerGlobal.Start()` only in `xrEngine/Device_Initialize.cpp:40` (once per process), `dwTimeGlobal =
  TimerGlobal.GetElapsed_ms()` (`device.cpp:808`), so it does not restart at a level change. `CRenderDevice::Pause`
  (`device.cpp:828-854`) can pause TimerGlobal; whether it is paused during the level load (so whether the load time counts
  in the pending kill's age) I did not verify. The stub models the clock as continuous, the 15 s boot counting.
- No CodeRabbit review: this folder is not a git repository and the brief forbids git-changing commands.

## The reader: harness/zz_p1_puppet_kill6.script = kill5 + these hunks (`diff` gives exactly these)
- Header lines 35-41: a kill6 paragraph. K-table comment lines 83-84: fields `ks`, `kx`.
- Lines 132-136: new `k_sec_now(id)`: `o:section()` of the game object, else `se:section_name()` of the server object,
  nil if neither (the same two reads kill5's stale test in k_line uses).

### Fix 1: a stale row with a pending kill gives only the stale answer
- Lines 161-162, 165: `local stl = r.st or r.kx`; the pending block is `if r.kt and not r.dd and not stl then` (kill5: `if
  r.kt and not r.dd then` with `if dn and not r.st` inside, so a stale row fell through to KNOCONF + kdead_fail); the
  confirm is now `if dn then` (the guard moved out, covering KCONF and KNOCONF); the stale word is written `if stl`.
- Tests: `pend_stale_nofail` (the review's case: id reused by a DEAD boar, poses keep naming the id with the old section,
  3 s: every snapshot `k,101,0.250,stale`, no KCONF, no KNOCONF, kdead_fail 0, one KSTALE section line, kstale 1);
  `pend_st_only` (the pose's section differs, the object is still the target, 3 s: stale only, no KNOCONF; once the pose
  drops the line the kill is judged again: plain line, KNOCONF once, kdead_fail 1).
- Mutants: `m_fail_on_stale` (kill5's guard back) fails pend_stale_nofail; `m_st_guard` (guard on kx only) fails
  pend_st_only. kill5 itself fails both (KNOCONF,17401,101,2050 / kdead_fail 1).

### Fix 2: identity recorded at the kill call, checked in the export, sticky
- Line 369: the online kill call stores `r.ks = cur` (the section just read from the object for the stale test).
- Lines 158-160: in the export, for a pending row (`kt`, no `dd`, no `kx`): if the object now holding the id (online or
  offline) has another section, `r.kx = true` and, unless the pose already marked the row (`st`), `kc.kstale + 1` and
  `KSTALE,<now>,<seq of the r line>,<id>,ident,<ks>,<section now>`. No object: no check (confirms as before). `kx` is
  never cleared (k_pose resets only `st`), so it gives `stale` in every later snapshot.
- Line 347: k_line's KSTALE line and count are skipped when `kx` is already set (one event, one count).
- Tests: `ident_omitted_pose` (the review's case: dead boar, one pose marks st, then poses omit the id for 3 s: stale every
  snapshot, no KCONF/KNOCONF, kstale 1); `ident_no_pose` (no pose names the id after the call: stale, the KSTALE ident line
  once, kstale 1); `ident_reuse_alive_other` (brief: a LIVING boar takes the id, named then omitted: stale, no
  KCONF/KNOCONF, kdead_fail 0, the boar never hit or killed); `ident_sticky` (the boar is released too: still stale, no
  KCONF); `ident_removed_dead` (brief: the target removed, no object: `k,101,0.250,dead` from the first snapshot after,
  KCONF once with ms since the call, no KSTALE); `ident_reuse_same_section` (brief, the limit above, pinned).
- Mutants: `m_ident_never` / `m_ident_never_np` / `m_ident_never_alive` (no identity check) fail ident_omitted_pose /
  ident_no_pose / ident_reuse_alive_other; `m_ks_none` (identity not recorded) fails ident_no_pose; `m_ident_cleared` (a
  pose clears kx like st) fails ident_sticky; `m_ident_absent_stale` (no object counted as a mismatch) fails
  ident_removed_dead; `m_ident_any` (any object counted as a mismatch) fails ident_reuse_same_section.

### Fix 3: the pending kill and the identity over a crossing
- Lines 397-398 (k_save): each row is `r,<id>,<ap>,<dd|->,<ow>,<st>,<kt|->,<kn>,<ks|->,<kx>` (kill5's five fields first,
  four appended).
- Lines 419-423 (k_load): the kill5 pattern plus the rest of the line; the four fields are read only if the rest is exactly
  `,<kt|->,<0|1>,<ks|->,<0|1>`. A kill5 row (nothing after st) loads exactly as before. kill5's own loader would read a
  kill6 file's first five fields and ignore the rest.
- After the resume k_line returns at kill5's `if r.kt then return end` (no second kill, offline or online), and the export
  confirms, logs KNOCONF, or marks stale as on one level.
- Tests: `cross_pending` (the ktab row carries `<T>,0,dog_weak,0`; after the resume no second kill, one KDEAD; KNOCONF once,
  ms = its time - the call's time and >= 15000 (the carried clock); the offline target then reads dead: dead line, KCONF
  once, ms since the call); `cross_pending_released` (target released during the crossing: dead line, KCONF once, one KDEAD,
  no KNOCONF); `cross_kn_kept` (KNOCONF before the crossing is not logged again after it); `cross_ident` (the id reused by a
  living boar during the crossing: stale, KSTALE ident once, no KCONF/KNOCONF); `cross_kx` (kx set before the crossing,
  the id empty after it: still stale, no KCONF); `ktab_old_format` (a hand-written kill5-format file with two rows: KTAB
  read 1,2; 101 hit 0.200 from the loaded 0.300; 102, dead in its row, not killed again; no KDEAD).
- Mutants: `m_cross_nopend` / `m_cross_nopend_rel` (kill5's row format) fail cross_pending / cross_pending_released;
  `m_cross_kt_late` (age restarted at the resume) fails cross_pending; `m_cross_kn`, `m_cross_ks`, `m_cross_kx` (one field
  not loaded) fail cross_kn_kept, cross_ident, cross_kx; `m_old_format` (loader requires the new fields) fails ktab_old_format.

T2SUM: the same 19 keys in the same order. Snapshot format, the `stale` word and the r line: unchanged.

## Decisions I made that the brief did not specify
- Identity is the section. Stronger and cheap would be the stalker's generated name (`character_name()`), which differs
  between most same-section stalkers, but it does not exist for monsters, the stub does not model it, and I could not check
  its server-object form; not used. The object `name()` is section + id in X-Ray, so it adds nothing.
- kx is sticky for the row's life (saved over a crossing too). The stale line for that id is written in every later snapshot,
  even after ITR2 drops its line, and the row never takes hits or kills for a later creature under that id (kill5's stale
  rows did the same while the id was named).
- The identity check runs only while the kill is pending (kt, no dd). After a confirmed death kill5's rules apply unchanged.
- A mismatch found by the export is logged as KSTALE with reason `ident` and counted in kstale (the existing line and key;
  the line has the same 7 fields). Not logged or counted again if the pose already marked the row stale, and k_line does not
  count again once kx is set.
- A stale mark from the pose alone (identity intact) holds the pending kill only while the pose marks it; when the pose drops
  the line, the kill is judged again (KNOCONF possible). The identity check is what protects against reuse.
- kt carried as raw time_global (reason in Limits), the same as kill5 already does for dd; the age then includes the
  crossing. `m_cross_kt_late` shows that the tests pin this choice. No clamp for a clock that went backwards: the table is
  read only for the same run id, which means the same process.
- A row with the four new fields malformed loads as a kill5 row (no pending kill), silently.
- kj2 mutants re-anchored on kill6's changed lines, the same mutations: `m_conf_never` and `m_hold_from_call` now anchor on
  `if dn then r.dd = now; m(`; `m_conf_on_stale` puts kill5's guard back (`if r.kt and not r.dd then`), because the old
  inner `not r.st` no longer exists. No check or expectation of kj2 changed: the kj2 stub UNCHANGED run on kill6 passes all
  44 checks.
- The kj3 tests run on kill5 fail exactly where the defects are (pend_stale_nofail, pend_st_only, ident_omitted_pose,
  ident_no_pose, ident_reuse_alive_other, ident_sticky, cross_pending, cross_pending_released, cross_ident, cross_kx) and
  pass where kill5 already behaved right (ident_removed_dead, ident_reuse_same_section, ktab_old_format; cross_kn_kept
  passes on kill5 only because kill5 forgets the kill entirely).

## The run checker kj2/t3_kj2_check.py (not edited)
No change needed for kill6's log lines: KCONF/KNOCONF unchanged; the new KSTALE reason `ident` goes to the generic KOTHER list
(line 535, only the first field is read); kstale is judged `eq 0`, which is right for a run with no reuse; the checker does
not read ktab.txt (only the ktabfail key). Not a checker point, but a kj3 run needs a runner: kj2's `t2_xray_kj2.sh`
installs kill5 by a fixed path, so a run of kill6 needs a copy with that line changed (not in my create list).

## What I could not verify
- Anything in the engine: kill6 has never run there; no engine bytes with KSTALE ident, a pending kill over a crossing, or a
  reused id.
- That a reused id in this install really shows the new section at once, through `level.object_by_id` or `alife():object`,
  and that both return nil for a removed id (kj2's open point too).
- Whether time_global is paused during a level load (above).
- How ITR2 treats a stale line that keeps coming after it dropped its line (sticky kx); itrzone's side.
- The heavy checker selftest was not run (ordered); the checker itself was not changed.

## Results (2026-11-26, desk)
`bash kj3/kill6h_test.sh`: `== checks: 58 pass, 0 fail; mutants: 73 caught, 0 not caught or missing` / `RESULT: PASS`
(run dir kj3/runs/r-20261126-175502-3981627). The first run (kj3/runs/r-20261126-175415-3970229: 13 fail, 4 not caught:
setup timing in the new tests and three mutant anchors with a wrong escape) is kept; nothing deleted.

# Stage 2: emission lines (2026-11-26, desk)

## Limits first
- Never run in the engine. Nothing here touched the game, Wine, a display or a runner. The reader's new code has run only
  under the Lua 5.1 stub, against FAKE managers I wrote from the script text. Whether `surge_manager` /
  `psi_storm_manager` resolve as globals from the bridge's namespace, whether `CTime:diffSec` returns a fractional number,
  and how `tostring(level.get_time_factor())` prints the engine's float (I expect `10` and `6`) are all INFERRED.
- The fake models only the fields the reader reads. It does not model save/load, a level change during an event, the
  `on_before_surge` veto, invalid levels, sleeping, or the order of the scripts' 1 s update against the bridge's update in
  one frame (in the stub the script update runs first).
- tools/t2_xray_kj3.sh: `bash -n` only, never run. The witness path (runner appends -> reader calls start_surge() ->
  emission starts) has never run end to end.
- No CodeRabbit review: this is not a git repository, and git commands that change anything are forbidden.
- **Desk read: things I found wrong or imprecise** (plan-facts/emission-clock-desk-read-2026-11-26.md):
  1. It says the timeline actions happen "at the first 1 s update with t >= the value". The scripts test `diff_sec >= X`
     with `diff_sec = math.ceil(diffSec/factor)` (SM:676, SM:708-977; PSM:317, PSM:335-392). So an action fires at the
     first update with **t > X - 1**, up to 1 s earlier than the read says. My phase table follows the scripts (ceil).
  2. "Psi strike time of vortex v: v.start_time + 20 in t units" (PSM:373): the test is `diff_sec - start_time >= 20`,
     so the strike comes at t > start_time + 19. In the `v` line, s therefore reaches (-1, 1] when the vortex goes, not 0.
  3. "Next start ... (SM:662)": SM:662 is the comparison. The game-seconds value with its `math.floor` is SM:266
     (PSM:132).
  4. Not wrong, but missing: `vortex_index` is reset only in initialize (PSM:56), not at each start (PSM:105-118). Over
     several storms it grows, and old indices are gone from `vortexes` (emptied at start, PSM:109). So a vortex's n is
     unique for the Lua state, not 1.. per storm (the example at t = 60 shows n = 3..6 of the first storm).
  The rest of what I used checked out against the scripts: SM:24/PSM:19 globals; SM:160-167 / PSM:41-46 initial fields;
  SM:497 wave fields; SM:688 + SM:1065 wave z; SM:1072-1073 wave stop; PSM:412 vortex fields; PSM:357-364 window 30..77
  (`==` creation); PVF:137-138 period 3..6; start functions SM:1673-1675, PSM:713-715 (no arguments used); SM:227-231 /
  PSM:101 skip when t + 6 > duration (a forced start sets inited_time = now, so t = 0: not skipped).

## What the reader writes (harness/zz_p1_puppet_kill6.script)
Every version-4 snapshot (`if t2`, the same condition as the k answers), after the r line (if there is one) and before the
k lines: one `e`, then `w` lines, then `v` lines, then one `ns`. Phases: kj3/emission-phases.md. Fixtures:
kj3/emission-line-examples.txt (real stub bytes).
- `e,<kind>,<t>,<phase>,<tf>`: kind surge if `SurgeManager.started`, else psi if `PsiStormManager.started`, else none.
  t = `game.get_game_time():diffSec(M.inited_time) / level.get_time_factor()`, `%.2f`. tf = `tostring(level.get_time_factor())`.
- `w,<n>,<z>` (`%.3f`): only during a surge, for n = 1, 2 with `blowout_waves[n].effect:playing()`.
- `v,<n>,<x>,<y>,<z>,<s>` (`%.3f` x3, `%.2f`): only during a psi storm, for each `vortexes[i]` with `hit == false`,
  ascending i. s = `start_time + 20 - t`.
- `ns,<surge_s>,<psi_s>`: `max(0, floor(_delta - now:diffSec(last_*_time)))`; -1 while that kind is running; both -1 when
  degraded.
- Meta (once each): `ESTART,<now>,<kind>,<t>`, `EPHASE,<now>,<kind>,<phase>,<t>`, `EEND,<now>,<kind>,<t at last sight>`,
  `EERR,<now>,<message>` (first degraded snapshot of the Lua state), `EFORCE,<now>,<line>,<result>`.

## Decisions you did not specify
- **Phase boundaries** come from `ceil(t)`, as the scripts do (see desk read point 1). 13 surge phases and 6 psi phases.
  I left out stages that mark no change useful to the partner (siren 25, which never plays in this install; quakes
  100-106; the fade steps): listed in emission-phases.md. Phase 13 / 6 = "end due": the script ends the event at its next
  1 s update.
- **Degrading is all or nothing.** Any error in the group (a nil module or instance, `started` not a boolean, a missing
  `inited_time`, `_delta` or `last_*_time`, a throwing accessor, a factor that is unreadable or <= 0) removes every e, w,
  v, ns line of that snapshot already built and writes `e,none,0.00,0,<tf>` + `ns,-1,-1` instead (tf = `-1` if the
  factor itself is unreadable). Each degraded snapshot adds 1 to eerr. Only the first is logged (EERR). A degraded
  snapshot leaves the ESTART/EPHASE/EEND tracking alone: a short read error inside an event produces no EEND/ESTART pair.
- A nil manager counts as an error (eerr). The bridge exports only 15 s after the actor's first update, and the scripts
  build both instances at their first 1 s update after that update (SM:59-64), so in a normal run eerr stays 0. A run
  with eerr > 0 means that something was not readable.
- **ns while an event of that kind runs = -1.** During the event `last_*_time` and `_delta` still describe the old date;
  the next date is rolled at the end (SM:337-341). An overdue date whose start is still pending prints 0, not a negative.
- **Restart rule:** the same kind with t lower by more than 0.5 s than at the last snapshot = a new event: `EEND` (old t)
  then `ESTART`, and the phases are logged again from 1. That covers start_surge() during an emission (inited_time = now,
  SM:205-206) and the Lua-state case. A new Lua state (level change, load) starts with no tracked event, so ESTART is
  logged again with the current t. It also opens its meta file with "w" unless it is a crossing, as before.
- **Time jump:** every skipped phase gets its own EPHASE line, in order, all with the same script time and the same t
  (the t after the jump). A phase going DOWN without a restart (not produced by the scripts) is logged as one EPHASE with
  the lower phase.
- **Kind change** (surge starts during a psi storm; PSM:313 cancels the psi storm): EEND psi, then ESTART surge.
- **Forced starts (Part B).** The test mode is decided ONCE, in on_game_start (t2 only): if `DIR .. "emtest.txt"` opens
  (`Z:\dev\shm\itrz_b1\emtest.txt`, the same folder and the same way as dmgtest.txt), `em_test = true`, and the complete
  lines already in the file are taken as done (`em_done`), so a level change or a load does not repeat them. Without
  the file there is one failed open at game start and nothing else: per frame the cost is one upvalue test
  (`if em_test then`). No read, no call (check em_force_absent counts the opens: exactly 1, at game start, even if the
  file appears later). With the file: at most one open + read per second; only lines ending in LF count (an
  incomplete line waits); each new non-empty line is acted on once: `surge` -> `surge_manager.start_surge()`, `psi` ->
  `psi_storm_manager.start_psi_storm()`, both inside pcall; result `started` / `notstarted` (the manager's `started`
  after the call, so a skip by the level, the option or the veto reads `notstarted`) / `error:<text>`; any other text
  -> `unknown`, no call. Note PSM:313: a psi storm forced while an emission runs reads `started` and is cancelled by the
  script one second later.
- **Runner:** the file must exist when the game starts, so the runner creates it EMPTY when T2_SURGE_PLAY or T2_PSI_PLAY
  is not 99999 (after removing any stale one), and appends the lines later. Each trigger fires once per runner run (not
  per boot; a boot after a restart treats lines already in the file as done). T2_STALL_PLAY=99999 did NOT mean "never"
  in kj2: it armed a stop 99999 s (27.8 h) after the first play pose, so in practice never in a 1100 s run, but not by
  rule. kj3 maps 99999 to 0 (off). kj2 removes dmgtest.txt only at start (when T2_DMG_OK is not 1), never at cleanup;
  emtest.txt is removed at start always and at cleanup (in `cleanup()`, the EXIT trap).
- The SRC line ("line 54" in kj2) is line 62 in kj3 because of the new header lines; it is the same statement.

## T2SUM change: LOUD
**T2SUM has a new key `eerr` (snapshots whose emission lines were degraded), appended as the LAST key, after
`ktabfail`.** The run checker judges every T2SUM key: it must learn `eerr` (expected 0). A checker that requires
`ktabfail` to be last will break. Two existing stub checks were updated for this (v3_unchanged now strips
`,eerr=0` as well; ktab_savefail expects `,ktabfail=1,eerr=0$`). Two more were updated for the new layout
(snapshot_lines and r_line_presence: `e,none,0.00,0,6` and `ns,...` now sit between r and k). Their intent is unchanged.

## Per-snapshot cost
Per v4 snapshot: one `pcall(level.get_time_factor)`, one `pcall(em_body, ...)` with its arguments (no closure, so no
allocation for it), two global lookups and a few field reads, ONE `game.get_game_time()` (one CTime userdata, shared by
t, the wave z and both ns values), one or two `diffSec`, 2 + (<= 2 waves | pending vortices) `string.format` strings
that go into the L table that the snapshot builds anyway. The vortex walk goes down from `vortex_index` until a nil
entry: <= one storm's vortices (8-15) plus one lookup. No table is allocated. On an error: one `string.gsub` (first time
only) and two constant strings. The test mode (absent): one boolean test per frame. INFERRED cheap (same order as one o
line); not measured.

## Tests (kj3/kill6h_stub.lua, kj3/kill6h_test.sh)
19 new checks (em_*) with 31 mutants (m_em_*). Each mutant fails its own check with an outcome (no ERROR):
em_v4_every, em_absent_v3 (a -b1_snap run without -t2_pose writes T1 snapshots without e/ns), em_none_exact,
em_surge_timeline / em_psi_timeline (every 10 Hz snapshot of a whole 222 s / 103 s event: exact e line; a snapshot at
every whole second), em_surge_bounds / em_psi_bounds (every threshold at X-1, X-0.99, X, X+0.5, with the clock frozen),
em_waves, em_vortex, em_ns, em_degraded_nil, em_degraded_field (missing _delta, a throwing vortexes accessor, a throwing
effect:playing, missing inited_time: degraded, exactly one EERR, eerr = degraded snapshots, no Lua error), em_order (r e
w ns k / r e v ns k), em_force_absent, em_force_lines (once, never twice, at most one read per second, unknown line,
incomplete line, psi), em_force_throw, em_meta_once (ESTART 1, EPHASE 1..13 once each in order, EEND 1 with the last
t), em_jump (+20 s at t = 100: EPHASE 6 and 7 at one time, after 5), em_backwards (start_surge again at t = 150: EEND
150.x, ESTART 0.x, EPHASE 1).
Fake fidelity: each fake field cites its script line in the stub ("kj3 Stage 2: fake surge / psi managers"). The fake's
vortex positions and period cycle (4,3,6,5) are deterministic stand-ins for math.random.

## Results (2026-11-26, desk)
`bash kj3/kill6h_test.sh`: `== checks: 77 pass, 0 fail; mutants: 104 caught, 0 not caught or missing` / `RESULT: PASS`
(58 + 19 checks, 73 + 31 mutants). The kept run dir is under kj3/runs/. Two earlier runs from this stage (one with 2
layout failures, one before the cost fix) were removed by literal path to keep the disk use under the limit.

## Runner diff (tools/t2_xray_kj2.sh -> tools/t2_xray_kj3.sh), bash -n only
```diff
--- t2_xray_kj2.sh	2026-11-26 16:04:09.179919672 -0500
+++ t2_xray_kj3.sh	2026-11-26 18:08:41.083344846 -0500
@@ -32,6 +32,13 @@
 # complete pose with state play that this runner read in this boot, counted from that pose's own utc_ms (header field 3).
 # Rows go to stall.csv as for T2_STALL_AT; the two triggers share the "done" latch, so use one of them.
 SPLAY=${T2_STALL_PLAY:-0}
+# kj3 Stage 2: T2_SURGE_PLAY / T2_PSI_PLAY = <s> after the first play pose's utc_ms (the T2_STALL_PLAY clock; default 99999 =
+# never): the runner appends `surge` / `psi` to $SHM/emtest.txt once (the reader then calls the manager's own start function)
+# and logs it in emforce.csv. emtest.txt is created EMPTY before the engine starts when either is set (the reader enables its
+# test mode only if the file is there at game start) and removed at start and at cleanup. T2_STALL_PLAY=99999 = no stop at all.
+SURGE_PLAY=${T2_SURGE_PLAY:-99999}; PSI_PLAY=${T2_PSI_PLAY:-99999}
+[ "$SPLAY" = 99999 ] && SPLAY=0
+EMPLAY=""; { [ "$SURGE_PLAY" != 99999 ] || [ "$PSI_PLAY" != 99999 ]; } && EMPLAY=1
 PFLAG=""; [ "${T2_PAUSE:-0}" = 1 ] && PFLAG="-t2_pause"
 # T2_ABSORB=<ms> (with T2_FREEZE=sigstop, engine perf/freeze-absorb): the world clock skips the frozen time.
 # T2_ABSORB_SKIP=<k>: the first k freezes stay unarmed (pre-flight 3 control).
@@ -51,10 +58,11 @@
 # pre-flight E test mode (section 46): its control file may exist only when the pre-flight runner says so (T2_DMG_OK=1)
 [ "${T2_DMG_OK:-0}" = 1 ] || { [ -e "$SHM/dmgtest.txt" ] && { rm -f "$SHM/dmgtest.txt"; log "REMOVED a stale test-mode control file (dmgtest.txt)"; }; }
 rm -f "$SHM"/snap.txt "$SHM"/meta_t2.csv "$SHM"/hits_t2.csv; mkdir -p "$SHM"      # pose.txt is ITR2's: never touched here
-SRC="$P/harness/zz_p1_puppet_kill5.script"   # kj2 (am138), was kill4; kw1: the kill channel script (pose v4) is installed beside the frozen one, under its own name
+rm -f "$SHM/emtest.txt"; if [ -n "$EMPLAY" ]; then : > "$SHM/emtest.txt"; echo "utc_ms,wall_utc,kind,ms_after_play0" > "$O/emforce.csv"; log "emtest.txt created (surge at ${SURGE_PLAY} s, psi at ${PSI_PLAY} s after the first play pose; 99999 = never)"; fi
+SRC="$P/harness/zz_p1_puppet_kill6.script"   # kj3 (kill6 + Stage 2 emission lines), was kill5; kj2 (am138), was kill4; kw1: the kill channel script (pose v4) is installed beside the frozen one, under its own name
 cp "$SRC" "$RUN/gamedata/scripts/zz_p1_puppet.script" && cmp -s "$SRC" "$RUN/gamedata/scripts/zz_p1_puppet.script" || { log "REFUSED: could not install $SRC"; exit 2; }
 # a runner that is cancelled must not leave a stopped engine behind: CONT whatever this runner stopped
-cleanup() { [ -n "${FZP:-}" ] && kill -CONT "$FZP" 2>/dev/null; [ -n "${STP:-}" ] && kill -CONT "$STP" 2>/dev/null; rm -f "$RUN/gamedata/scripts/zz_p1_puppet.script"; }
+cleanup() { [ -n "${FZP:-}" ] && kill -CONT "$FZP" 2>/dev/null; [ -n "${STP:-}" ] && kill -CONT "$STP" 2>/dev/null; rm -f "$RUN/gamedata/scripts/zz_p1_puppet.script"; rm -f "$SHM/emtest.txt"; }
 trap cleanup EXIT; trap 'exit 143' TERM INT
 # first line of pose.txt, but only of a COMPLETE record (first line seq,<n>,... and last line end,<n>); else empty
 posehdr() { awk '{ sub(/\r$/, "") } NR == 1 { h = $0; split($0, a, ","); if (a[1] != "seq") exit } NF { l = $0 } END { if (h != "" && l == "end," a[2]) print h }' "$SHM/pose.txt" 2>/dev/null; }
@@ -114,8 +122,14 @@
     # LOST: wall time since a new complete pose last appeared. Counted only after one appeared in this boot.
     if [ -n "$pl" ] && [ "$pl" != "$ppl" ]; then ppl=$pl; pchg=$(utc); pseen=1
       [ -n "$lost" ] && { log "lost end: new pose (state ${pst:-?}) after $(( pchg - lostu )) ms"; lost=""; }; fi
-    if [ "$SPLAY" -gt 0 ] && [ -n "$pseen" ] && [ -z "${play0:-}" ] && [ "${pst:-}" = play ]; then
-      IFS=, read -r _ _ play0 _ <<<"$pl"; case "$play0" in ''|*[!0-9]*) play0="" ;; *) log "first play pose read: $pl (stop planned ${SPLAY} s after its utc_ms)" ;; esac; fi
+    if { [ "$SPLAY" -gt 0 ] || [ -n "$EMPLAY" ]; } && [ -n "$pseen" ] && [ -z "${play0:-}" ] && [ "${pst:-}" = play ]; then
+      IFS=, read -r _ _ play0 _ <<<"$pl"; case "$play0" in ''|*[!0-9]*) play0="" ;; *) log "first play pose read: $pl (stop planned ${SPLAY} s after its utc_ms; 0 = none)" ;; esac; fi
+    if [ -n "$EMPLAY" ] && [ -n "${play0:-}" ]; then el=$(( $(utc) - play0 ))   # kj3: forced starts, once each per run (every boot)
+      if [ "$SURGE_PLAY" != 99999 ] && [ -z "${em_s_done:-}" ] && [ $el -ge $(( SURGE_PLAY * 1000 )) ]; then em_s_done=1
+        echo surge >> "$SHM/emtest.txt"; echo "$(utc),$(date -u '+%FT%T.%3NZ'),surge,$el" >> "$O/emforce.csv"; log "EMFORCE surge appended to emtest.txt, $el ms after the first play pose"; fi
+      if [ "$PSI_PLAY" != 99999 ] && [ -z "${em_p_done:-}" ] && [ $el -ge $(( PSI_PLAY * 1000 )) ]; then em_p_done=1
+        echo psi >> "$SHM/emtest.txt"; echo "$(utc),$(date -u '+%FT%T.%3NZ'),psi,$el" >> "$O/emforce.csv"; log "EMFORCE psi appended to emtest.txt, $el ms after the first play pose"; fi
+    fi
     if [ $k = 0 ] && [ "$SPLAY" -gt 0 ] && [ -z "$done_stall" ] && [ -n "${play0:-}" ] && [ $(( $(utc) - play0 )) -ge $(( SPLAY * 1000 )) ]; then
       EP=$(engine); [ -n "$EP" ] || { why="no engine pid found (stall)"; break; }; log "STALL start (play clock): $(( $(utc) - play0 )) ms after the first play pose; seq $s track ${tt}s engine $EP"
       echo "utc_ms,event,seq,track_s" > "$O/stall.csv"; echo "$(utc),STOP,$s,$tt" >> "$O/stall.csv"
```

# Stage 3: stale rows under the agreed rule (a)-(c), and two Stage 2 review fixes (2026-11-26, desk)

## Limits first
- Desk only, Lua 5.1 stub only. Nothing ran in the engine; no git command; no CodeRabbit.
- Strict (a) ("stale only while the pose's line has ANOTHER section") would remove the alive-again kind, whose line has the
  SAME section. I kept alive-again as a stale kind that behaves like the section kind (stale while named, dropped by (c)),
  because without it a line the mod never dropped for a dead creature would be applied to the next same-section creature.
  Itrzone should confirm this reading.
- Unchanged limit: a same-section replacement under the same id cannot be told apart (ident_reuse_same_section still pins it).
- A pending kill on a row dropped by (c) is never judged: no KCONF, no KNOCONF, no kdead_fail; its kdead_online stays
  counted. A kill that was made and then forgotten this way shows only as `KDEAD,...,online` followed by `KSTALE,...,drop,...,pending`.
- Two k lines for one id in one pose: not tested (processed in order, as before).

## The three stale kinds now
| kind | how found | stale line written | when the pose no longer names the id | a line with the object's own section |
|---|---|---|---|---|
| section (am138) | at the pose: line's section differs from the object's | while each newest pose carries such a line (st, reset per pose) | row dropped at that pose (c) | if the row's recorded creature (ks) differs: fresh row (b); if ks equals it (the pose's section was just wrong): the row goes on, pending kill judged again |
| alive_again (am138) | at the pose: death confirmed > 10 s ago, id alive again, same section | while named (st) | row dropped at that pose (c); the next line is a new row | same section: stays stale (cannot be told apart from the old line) |
| ident (kill6) | in the export, pending rows only: the object holding the id has another section than ks | only while the newest processed pose named the id (kx and nm); unnamed: no line at all | row dropped at the next pose without its line (c); kx is never cleared by a pose | fresh row (b) |
Stale rows of every kind: no KCONF, no KNOCONF, no kdead_fail while stale (stage 1 fix 1, unchanged).
Live rows (applied > 0 or a pending kill, not stale) are never dropped: a retired line keeps its plain `k,<id>,<applied>`
line exactly as before, and a pending kill without a line still gets KNOCONF / KCONF (st3_live_kept).

## Reader changes (harness/zz_p1_puppet_kill6.script 096380d8...)
- k_line: every row named by a line of the pose gets `nm`; `ks` is recorded at every line taken (stale test passed), not
  only at the kill call; (b) `if sec == cur and r.ks and r.ks ~= cur` replaces the row by `{ ap = 0 }` (applied, dd, ow, kb,
  ke, kt, kn, kx, st all gone), clears kwho[id], logs `KSTALE,<now>,<seq>,<id>,fresh,<old ks>,<section>`, then the line is
  processed as any first line (damage and kill applied normally).
- k_pose: marks are reset and lines processed only for a pose with <= 64 lines; then (c) every row not named whose last
  pose marked it stale (was) or with kx is removed, kwho[id] cleared, logged `KSTALE,<now>,<seq>,<id>,drop,<applied>,<pending|->`.
  **Changed:** an over-64 pose now leaves every row and mark as it was (stage 2 cleared the st marks). Refused lines (form,
  total, id, kind) do not name a row, so a stale row whose only line is refused is dropped.
- Export: stale line `if r.st or (r.kx and r.nm)`; a kx row not named writes no line.
- **KSTALE reasons added: `fresh` and `drop`. Neither is counted in kstale** (kstale still counts stale events: section,
  alive_again, ident, once per episode). The kj2 checker files KSTALE lines under KOTHER (reported only).
- ktab.txt: rows get **nm as a 10th field**; the loader takes 10, then 9 (stage 1/2 kill6), then 5 (kill5) fields. Stale rows
  are carried with st, kx, nm, so the boot snapshot after the resume answers stale exactly as before the crossing, the
  first pose processed after the resume re-marks them, and a pose that does not name them drops them. ktab also carries
  `e,<em_done>` (below).
- The r line: untouched (k_r is set at the end of k_pose for every processed pose, drops included; over64_r passes).
- T2SUM keys: unchanged (19 + eerr). Snapshot format: unchanged.

## Stage 2 review fixes
1. **Not ready.** In the scripts (gamma_flat read-only; stock bind_stalker.script / _g.script from
   plan-facts/stock_scripts/_unpacked, gamma_flat has no bind_stalker.script): `SurgeManager` / `PsiStormManager` start as
   nil (SM:24, PSM:19) and are created by `get_*_manager()`. On a save load, `actor_binder:load` (bind_stalker.script:356-365,
   USE_MARSHAL = `marshal ~= nil`, _g.script:46) calls `load_state` and then `bind_stalker_ext.actor_on_load` ->
   `surge_manager.actor_on_load` (SM:1571) / `psi_storm_manager.actor_on_load`, which call `get_*_manager()` first: the
   instances exist before the actor's first update. On a NEW game nothing creates them until `main_loop` (SM:51-63,
   AddUniqueCall), which waits for `db.actor.afterFirstUpdate` (set in the actor's first update, bind_stalker.script:230),
   while the bridge exports on its very first actor_on_update (warm-up included). So **yes, it can happen**: a new game,
   and I found no call of actor_on_load in the non-marshal load path (bind_stalker.script:366-409). For the T2 runs (a
   save loaded, marshal present) I infer it does not; not verified in the engine, nor what a level change does.
   Rule built: while no good read has happened in this Lua state, a failure with `SurgeManager` or `PsiStormManager` nil
   writes the same degraded lines, does not count eerr, logs no EERR, and logs `ENOTREADY,<now>` once; the first good read
   logs `EREADY,<now>` (only if ENOTREADY was logged, so a normal run's meta is unchanged). Any other failure, and any
   failure after the first good read, counts as before. Per Lua state (a new state after a crossing starts not ready again).
   A module itself missing, or an instance with missing fields, still counts. Check em_notready; mutants m_em_nr_count,
   m_em_nr_always.
2. **Forced starts over a crossing.** k_save writes `e,<em_done>` into ktab.txt when the test mode is on; k_load reads it
   only after the run id test; on_game_start sets em_done from it when the file is there, else counts the file's lines
   (fresh process, or another run id). Check em_force_cross (same run: a `psi` appended during the level load is acted on
   after it; another run id: skipped; fresh process: old lines skipped); mutants m_em_carry_none, m_em_carry_nosave,
   m_em_carry_anyrun. Limit: if ktab.txt cannot be written at a crossing and an older ktab.txt of the same run is still
   there, its older cursor is used (lines acted on twice) - the same exposure the kill table already had.

## Tests
New checks (each with its mutants, every catch an outcome failure): st3_a_named (m_kx_st_only, m_ident_never),
st3_b_fresh (m_fresh_none, m_fresh_keep_ap, m_fresh_keep_kt), st3_same_back (m_fresh_any), st3_c_drop (m_drop_never_c,
m_drop_over64), st3_live_kept (m_drop_live), st3_partner (m_drop_kwho, m_partner_perm; the partner's case end to end), em_notready,
em_force_cross; also m_kx_unnamed on ident_no_pose, m_drop_never on ident_omitted_pose, m_cross_nm on cross_ident.
**Stage 1 checks whose "permanent" expectation was changed:** pend_st_only (after the line is dropped: no line, no KNOCONF,
drop line; was plain line + KNOCONF), ident_omitted_pose (dropped; the old line again is a new row `k,101,0.000,stale`),
ident_no_pose (no line at all; was stale every snapshot), ident_reuse_alive_other (stale while named or while the kill pose
is newest, none after), ident_sticky (now: never cleared, dropped, no KCONF when the id empties), cross_kx (stale while named
after the resume, dropped after), cross_pending (ktab row has the nm field). Re-anchored: m_ks_none, m_ident_cleared,
m_cross_nopend, m_cross_nopend_rel; m_ident_never moved from ident_omitted_pose to st3_a_named. The new st3/em checks run
on the stage 2 reader fail except st3_same_back (which pins behaviour stage 2 already had).
emission-line-examples.txt: regenerated, snapshot bodies byte-identical; only the header hash line changed.

## Results (2026-11-26 19:03, desk)
`cd wt-render-split/perf-harness && nice -n 19 bash kj3/kill6h_test.sh`: `== checks: 85 pass, 0 fail; mutants: 122 caught, 0 not caught or missing` / `RESULT: PASS` (77 + 8 checks, 104 + 18 mutants; run dir kj3/runs/r-20261126-190334-2450941). Two earlier runs of this stage (7 and 3 expected failures while the checks were rewritten) were removed by literal path.

# Review pass 2 (CodeRabbit 2026-11-26, plan-facts/cr_wu2_kj3_pass2_2026-11-26.txt), desk

## Limits first
- Lua 5.1 stub only; nothing ran in the engine; no git, no CodeRabbit. Same limits as Stage 2/3 otherwise.
- The legacy (nine-field) ktab row can only come from a stage 1/2 kill6 reader writing ktab.txt and this reader loading it
  in the same run (same run id = same process). Neither stage ran in the engine; the fix is for completeness.

## Fix 1 (finding :200): not ready only for the failure that IS a nil manager
- Found: em_snap suppressed eerr whenever `em_absent()` was true before the first good read, whatever em_body had failed
  on. em_body checks the time factor FIRST (`time factor unreadable`), so a factor <= 0 or unreadable while a manager was
  still nil counted nothing and logged no EERR (only ENOTREADY). Confirmed.
- Now: `local nr = not em_ready and (err == "SurgeManager nil" or err == "PsiStormManager nil")`, the two errors em_body
  raises itself (level 0, exact strings) for a nil instance. em_absent is removed. Every other failure counts as before:
  a bad factor, a missing module (`surge_manager` nil raises a Lua index error, not these strings), missing fields, a
  throwing accessor, and any failure after the first good read.
- Check em_notready_tf: both managers nil (a new game), boot, 200 ms not ready (degraded, not counted), then factor 0 for
  1 s (degraded and counted: eerr = those snapshots; EERR `time factor unreadable` once), then factor good with the
  managers still nil (not ready again, not counted); ENOTREADY once, no EREADY. Mutant m_em_nr_anyerr (pass 1's rule:
  "any failure while a manager is nil") caught: eerr 0 want 10. m_em_nr_always re-anchored on the new line (same
  mutation: nil after a good read not counted), still caught by em_notready.

## Fix 2 (finding :539): a nine-field row with kx answers stale until the first processed pose
- Found: k_load's nine-field branch reads kt, kn, ks, kx but no nm, so `r.nm` is nil and the export's (a) rule
  (`r.st or (r.kx and r.nm)`) writes no line for that row until a pose names it; a stage 1/2 reader wrote `stale` for a
  kx row in every snapshot. Confirmed.
- Chosen (smallest, inside the (a)-(c) rule): in the nine-field branch `nm = kx`, so a legacy identity-stale row counts as
  named until the first processed pose. k_pose then resets nm for every row (as for any row) and (c) drops it if that
  pose does not name it; a pose that names it keeps it stale. Ten-field rows keep their own nm; kill5 rows are untouched.
- Check ktab_legacy_kx: a hand-written ktab.txt with `r,101,0.250000000,-,0,0,9000,0,dog_weak,1` (same run id, the id now
  a boar): `k,101,0.250,stale` in every snapshot before any pose; the first processed pose without 101: no k line, one
  `KSTALE,...,101,drop,` line; KTAB read 1,1; no KCONF, no KNOCONF, kdead_fail 0. Mutant m_legacy_nm (the assignment
  removed) caught: k line nil in the boot snapshots.

## Files
- harness/zz_p1_puppet_kill6.script = kj3/zz_p1_puppet_kill6.script (cp, cmp equal), sha256
  7d7c173dae815a0d7673acffb9d911413a18708f9beb7914afc1dc4685d4c8e9 (was 096380d8...). Header paragraph updated.
- kj3/kill6h_stub.lua f864764c06848c010324aa765a26a5d7943b530f4516ff5cd11c16ad55f6f4b5: 2 checks, 2 mutants, 1 re-anchor.
- kj3/emission-line-examples.txt 0fab2ee26bda681c658da6aeb6ae0d0566aacace6420da7946eb4e6af380b28c: regenerated with
  mode `examples`; snapshot bodies byte-identical (diff of everything from the first `## ` line: none); only the header
  hash line changed.
- T2SUM keys, snapshot format, meta lines: unchanged.

## Results (2026-11-26 19:37:59, desk)
`cd wt-render-split/perf-harness && nice -n 19 bash kj3/kill6h_test.sh`: `== checks: 87 pass, 0 fail; mutants: 124 caught,
0 not caught or missing` / `RESULT: PASS` (85 + 2 checks, 122 + 2 mutants; run dir kj3/runs/r-20261126-193759-4021895).
