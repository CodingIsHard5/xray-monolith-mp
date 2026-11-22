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

## Note (2026-11-22 07:46:15 EST): built and checked at the desk, before any run

- Section 5 done: tier_b.py null on the 15 existing references with C added, band 97.5: overall false-fail 4.5 %
  (unchanged), C's own rate 0.000. Honest caveat: C is 0 here only because all 15 references give identical counts;
  the 4-in-50 world-event rate seen across all dumps (e.g. h05) is not inside this reference set. Output:
  perf-harness/tierB_null_withC_2026-11-22.txt.
- tier_b.py: statistic C added (invariants() + stat_C; I4 counts an unavailable npc_count, the 2 stock helicopters, as
  a mismatch, exactly like tb_invariants.awk). Python and awk give identical I1..I6 on all 50 existing runs.
- Overlay: -tb_lostreg N [-tb_lostreg_at s] added (purely additive, 34 lines; syntax-checked with luac 5.1); tb_extra
  gains four "info lostreg_*" lines. Overlay hash now cd7a6ad68b84d994 (run tree = harness copy); the ri runs test
  exactly this overlay.
- Self-test tools/selftest/tb_statC_selftest.sh: a CRLF fixture with one lost registration gives C = 1 on I5 only;
  witness W4 (I5 check removed) gives C = 0. Passes. The CRLF and settle self-tests still pass.
- Scheduler: TB_ORDER / TB_STATE select tools/tb_order_c.txt and out/tbc_state; smoke check smokec_ok as in section 6,
  tested: a stock run fails it (I5 = 1, no done line), a doctored copy with 60 lost registrations passes.

## Note (2026-11-22 07:47:18 EST): limit and stock-side rule (Overseer; written before any run of this series is read or scored)

- LIMIT: the control exercises I5 only. C's sensitivity through I1, I2, I3, I4 and I6 stays UNPROVEN; any claim says
  "C validated on lost registrations (I5) only".
- STOCK-SIDE CHANGE: the reference noise for C is exactly 0, so p is fragile. Stock C does move in rare world events
  (4 of 50 dumps so far, on I1 and I3; e.g. h05 has C = 6 against every reference). Rule, fixed now:
  1. The five ri runs are read first. For each, its C distance to every R1 run is reported, per component.
  2. If no ri run differs from R1 on C: the doses are scored as registered (threshold P97.5 of R1 x R2 = 0).
  3. If any ri run differs (on any component, I5 included): the change is REPORTED with the run and component, and
     every dose group is scored on C against a WIDENED stock range: threshold = the largest C distance between any ri
     run and any R1 run, in place of 0; Rule B is unchanged. The verdict under the registered threshold is reported
     beside it, and the widened one governs. The widened threshold is then also used for every later candidate.
  4. The inertness check (ri as a candidate on all seven statistics) is scored as registered either way; its
     medians already absorb one world-event run in five.

## RESULTS (2026-11-22 13:26:20 EST) - scored once, as registered (21 runs 07:47-13:25, 2026-11-22; none DISTURBED; smoke I5 = 101)

Outputs: perf-harness/tierB_statC_step1_2026-11-22.txt, tierB_statC_score_2026-11-22.txt, tierB_statC_components_2026-11-22.txt.

1. Stock side (rule 1): ri01, ri02, ri04, ri05 equal R1 on every check. ri03 DIFFERS: I1 = 33 (R1: 31), C = 2 against
   every R1 run (a world event: two creatures name a squad that is gone). Reported here. Rule 3 applies: WIDENED
   threshold for C = 2 (governs, also for later candidates); the registered threshold 0 is shown beside.
2. Inertness (ri as candidate, seven statistics): PASS on all seven. The new overlay is inert; the old references and
   band stay valid.
3. Doses (components I1..I6 per run in the components file; lostreg_done = N in every run):
   - I5 = 2 / 11 / 101 in all 15 runs: EXACTLY as predicted.
   - I6 = 13 / 20 / 60 (stock 12): predicted 0 change - WRONG. npc_info is filled for off-level smarts too; my reading
     of update_jobs was wrong. So C distance = 2 / 18 / 148, not the predicted 1 / 10 / 100.
   - C, registered threshold 0: FAIL at 1, 10, 100. C, WIDENED threshold 2 (governs): dose 1 PASS (median 2 <= 2),
     dose 10 FAIL (18), dose 100 FAIL (148). DETECTION LIMIT of C = 10 lost registrations (governing), 1 under the
     registered threshold. Monotonic.
   - P (existing statistic, smart npc counts) ALSO FAILS at every dose (median 1 / 10 / 99). Predicted "every other
     statistic passes" - WRONG. Consequence: this control does not show C seeing anything P could not already see.
     C's added value rests on I1-I4, which stay UNPROVEN.
   - U: predicted PASS at all doses - WRONG at dose 1: FAIL (median 0.0026 vs 0.0025, p = 0.008; the lr1 runs did
     1,926-1,930 passes against about 1,922). U PASSES at 10 and 100. Non-monotonic, so I read it as a U false fail or
     an unexplained effect, not as "dose 1 changes the amount of work". The must-hold "U passes at all doses" is NOT
     met as registered; reported, not explained away.
   - Must-hold "dose 100 fails on C": MET.
4. Finding about the decision rule (applies to all of tier (b)): Rule B (permutation of median distances) cannot fail
   a gross effect on near-constant data. For C and P at dose 100, p = 1.000, because any labelling with a majority of
   candidate runs in G1 gives the same median gap. Every C and P failure above comes from Rule A alone. So for C and P,
   Rule B adds nothing, and the "fragile p" concern does not arise; their verdict is Rule A against the threshold.
5. Claims language from now on: "passes tier (b) incl. C; limits: U about 4 % of A-Life work; C 10 lost registrations
   (P sees them too); I1-I4 unproven; stationary player".
