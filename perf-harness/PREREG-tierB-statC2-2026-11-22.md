# Tier (b) statistic C, second control: lost member link (engine-side test hook) - pre-registration
# (2026-11-22 13:39 EST; Overseer rulings 1-3 of 2026-11-22. Nothing run.)

## 1. The hook (test-only)

- Branch test/tb-hooks, commit 2c09c6773 (on top of the RefDet14 source, 54a338e15; engine src unchanged since).
  Everything sits behind the build define XRAY_TB_TESTHOOKS, set ONLY by the workflow TB-TestHook-Build
  (.github/workflows/tb-testhook-build.yml, triggers only on test/tb-hooks). Perf-Build and every other workflow never
  define it. The exe is stored locally as run/bin/TestHook14.exe and is never a candidate or a shipped build.
- CSE_ALifeOnlineOfflineGroup::tb_lose_member_link(member): the member leaves the squad's member list (m_members),
  keeps its group_id, and is NOT re-added to graph or scheduler, i.e. unregister_member with its other writes lost.
  Refuses squads with one member. Lua binding under the same define. Test-build-only guard in unregister_member
  (return if the member is not in the list): needed because sim_offline_combat can kill a cut member offline and the
  script then unregisters it (stock VERIFY is compiled out in release, so stock would read end()).
- Review: CodeRabbit on the three source files, 0 findings.
- Absence check (Overseer condition): "tb_lose_member_link" occurs 0 times (ASCII and UTF-16) in RefDet14.exe and
  PerfDX11.exe today; the same check is run on every candidate exe before it is scored, and on TestHook14.exe it must
  occur (positive control of the search).
- Overlay: -tb_lostlink N [-tb_lostlink_at 60]. After a normal offline squad update of a squad not hit before, with
  >= 2 members, cut the link of its first member; N squads in all. tb_extra gains "info lostlink_*" lines, including
  lostlink_hook ok|missing.

## 2. Desk prediction, per statistic and dose (doses N = 1, 10, 100 cut links, each on a different squad)

Consistency counts (stock: I1 31, I2 0, I3 15, I4 2, I5 1, I6 12):
- I4 (squad npc_count vs creatures naming it): npc_count falls by one, the creature still names the squad -> +1 per
  cut. PREDICTED I4 = 3 / 12 / 102.
- I6 (smart population vs sum of its squads' npc_count): the squad's npc_count falls but the smart's npc_info keeps the
  member -> +1 per smart touched for the first time. PREDICTED I6 = 13 / 20 / 60 (same as the lost-registration control,
  which touched smarts in the same way); range 12-13 / 16-22 / 45-65.
- I1, I2, I3, I5: UNCHANGED (the squad still exists; level, vertex and smart of the member are not touched).
- C distance to stock = 2 / 18 / 148 (range 1-2 / 14-20 / 135-165).
Other statistics:
- P (smart npc_info counts): UNCHANGED at every dose -> PASS. This is the point of the control.
- L, V (squad level / vertex), M (object sections), D (deaths): PASS at every dose.
- U (pass count): PASS at every dose. Caveat on record: the unexplained +5 rise at lost-registration doses 1 and 10.
Verdicts (band 97.5; C governing threshold 2 from the stock-side rule; Rule B' for P, D, C):
- Dose 1: C median 2 <= 2 -> PASS. Dose 10 and 100: C FAIL (Rule A and B', 5/5 deviating). P PASS at all doses.
- Under the registered threshold 0 (shown beside): C FAILS at all three doses.

## 3. Must-hold for the control to count

- Dose 100 FAILS on C, I4 at dose 100 >= 50, AND P PASSES at every dose. If P fails, the control does not separate C
  from P, and I say so.
- Inertness: the 5 ti runs (TestHook14.exe, new overlay, NO flag) scored once as a candidate against R1 = r01, r02_rr,
  r03_rr, r04_rr, r05 and R2 = r06..r10 (RefDet14) on all seven statistics, Rule B' for P, D, C, C threshold 2. Must
  PASS (shows the test exe without the flag behaves like RefDet14). If it fails, the doses are not interpretable.
- Stock-side rule as before: ti runs read first; any C difference is reported, and the widened threshold becomes the
  largest ti x R1 C distance if that is larger than 2.

## 4. Runs (tools/tb_order_c2.txt, fixed here)

smokel (60/120/180 s, -tb_lostlink 100; must show the hook present, I4 >= 50, the done line), then 5 cycles of
{ti, ll1, ll10, ll100}, order within each cycle shuffled with random.Random(20261122) (Overseer ruling 3):
smokel ti01 ll100_01 ll10_01 ll1_01 ll10_02 ll1_02 ll100_02 ti02 ll10_03 ll1_03 ll100_03 ti03 ti04 ll1_04 ll10_04 ll100_04 ti05 ll100_05 ll10_05 ll1_05 
21 runs, about 6 h, all on TB_EXE=TestHook14.exe. Same scheduler rules (any hour, WANT, fair-share gap, settle wait,
21-min cap, title and Roblox checks, DISTURBED re-run once, re-runs first, stop on any failure).

## 5. Rule B' adopted (Overseer ruling 2), with its limits

As proposed in PREREG-tierB-statC notes (4); implemented as tier_b.py score --ruleb2 P,D,C (--thr C=2 carries the
governing C threshold). Limits on the record: its null comes from only 34 stock runs with 2 deviants; the false-fail
rate is somewhere between 0.9 % and about 11 %. Earlier verdicts keep their registered scoring.
