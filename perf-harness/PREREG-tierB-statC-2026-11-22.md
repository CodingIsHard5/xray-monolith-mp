# Tier (b) statistic C (consistency) and its lost-write control - pre-registration (2026-11-22 07:43 EST)

Approved in principle by the Overseer (proposal bce2a4014). Nothing below has been run.

## 1. Desk trace: which of the six checks a lost write reaches (code: run/gamedata/scripts, as the game runs it)

- I1..I6 as in the proposal; counts from perf-harness/tb_invariants.awk on tb_extra_<tag>_3.txt.
- Reverting the squad's assigned target (the proposal's -tb_wrong) reaches NONE of the six. sim_squad_scripted:
  generic_update (l.333-) reads assigned_target_id on the next update; if it names a smart other than smart_id, it
  calls SIMBOARD:assign_squad_to_smart -> assign_smart (l.517-), which writes smart_id and re-registers every member in
  one go, so the records agree again after one update. On top of that, in stock 15-min runs, offline squads almost
  never get a new target after the first 5 min: smart_id changed for 10 of 575 squads between 0 and 300 s and for 0
  of 575 between 300 and 900 s (r01, r05, r10), so a revert is mostly a no-op. Predicted change in C: 0 at every dose.
  Dropped as a control.
- Chosen lost write instead: a member's registration is lost, as when a race clobbers the smart's register_npc write.
  After a normal offline squad update, for one member whose m_smart_terrain_id equals the squad's smart_id, call the
  smart's own unregister_npc(member) (smart_terrain.script l.395: clears arriving_npc, clear_smart_terrain() sets the
  member's smart to 65535, clear_job removes its npc_info entry). The squad keeps smart_id. Re-registration happens
  only in assign_smart (l.536), add_new_member_forced (l.594), the spawn path (l.745) and register_delayed_npc (smart
  load), none of which runs for an existing squad that keeps its smart. So the break persists to the 900 s dump.
- Reach per check: I5 (creature smart vs squad smart) +1 per lost write. I1, I2, I3, I4: unchanged (group_id, level,
  vertex untouched). I6 (smart population vs squad npc sum): npc_info is filled only on the actor's level
  (update_jobs returns when not is_on_actor_level), and only offline squads are touched; 24 of 575 squads are on
  l05_bar, nearly all online. Predicted I6 change: 0 at every dose (0-1 at dose 100).

## 2. Statistic C (pair statistic, added to P, L, V, M, D, U)

C(x, y) = sum over I1..I6 of |I_j(x) - I_j(y)| at the 900 s dump. Same decision rule as the others (band percentile of
R1 x R2, permutation p > 0.05). Stock noise: all 15 reference runs give identical counts (31, 0, 15, 2, 1, 12), so the
R1 x R2 noise is 0 and the threshold is 0. In 50 stock dumps, 4 differ (world events: squads vanish), so a candidate
fails only if most of its runs differ.

## 3. Control: -tb_lostreg N [-tb_lostreg_at 60]

Overlay flag (harness/zz_eperf_dump.script), counted only when the unregister happened, logged
"* tb_lostreg: done n=..". Each lost write hits a different member (one per update, members already at 65535 skipped).
Same amount of work: the update itself runs unchanged, so U must not move.
- Doses N = 1, 10, 100; 5 runs each (lr1_01..05, lr10_.., lr100_..).
- PREDICTED I5 at 900 s: 2, 11, 101 (stock 1). PREDICTED C distance to stock per pair: 1, 10, 100. I1-I4 and I6:
  unchanged. U: PASS at every dose.
- PREDICTED verdicts with the frozen band 97.5: dose 1 FAILS on C (median 1 > threshold 0, all 25 pairs differ,
  p = 0.008), doses 10 and 100 FAIL on C. Every other statistic PASSES at every dose (a lost registration changes
  where nobody walks in 15 min). If dose 1 passes, my trace or the persistence claim is wrong, and I say so.
- Must-hold for the control to count: dose 100 FAILS on C AND U PASSES at all doses. Detection limit = smallest
  failing dose, quoted as lost writes and as a share of offline squad updates in the run (about 25,000).

## 4. Overlay inertness (the text cut off in the notice)

The new flag changes the overlay's hash. So 5 reference runs WITHOUT any flag (ri01..ri05), on the new overlay, are
scored once as a candidate against R1 = r01, r02_rr, r03_rr, r04_rr, r05 and R2 = r06..r10 with the frozen band, on
all seven statistics. They must PASS. If they do, the old 15 references and the band stay valid for the new overlay.
If they fail, the dose results are not interpretable and I stop.

## 5. False-fail with C added

Before any new run: tier_b.py null on the existing 15 references with C added, band 97.5 kept. If the overall rate
goes above 5 %, I report it and do not run the doses.

## 6. Runs and machine rules

Order (tools/tb_order_c.txt, fixed with this file): smoke (N = 100, 180 s, must show I5 >= 50 at its last dump and the
"tb_lostreg: done n=100" line), then ri01, lr1_01, lr10_01, lr100_01, ri02, ... (5 cycles). 21 runs, about 6 h.
Same scheduler rules: any hour, EXCLUSIVE through WANT, slices <= 3 h, fair-share gap, settle wait, 21-min cap,
protected-title and Roblox checks, DISTURBED runs re-run once (re-runs first).
Stop rules as before: a crash, timeout or failed smoke check stops the series and is reported.

## 7. Claims language

Any later claim: "passes tier (b) with C; limits: U about 4 % of A-Life work; C <detection limit> lost registrations;
stationary player". C covers lost registrations only, not every kind of wrong work.
