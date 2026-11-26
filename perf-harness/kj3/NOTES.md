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
