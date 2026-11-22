# Tier (b) blind spot - proposal (desk only, nothing run; for the Overseer's ruling)

Blind spot: tier (b) sees LESS A-Life work (U, the pass count) but not the SAME amount of work done WRONGLY. A threading
bug typically does exactly that: a race or lost write corrupts state while every update still runs.

## Options considered

1. Per-squad trajectory diff against stock at fixed seeds (normal mode). NOT cheap: normal-mode runs already diverge
   (stock noise V = 1-6 squads on a different vertex at 900 s), and the engine has no deterministic replay in normal
   threaded mode. A trajectory diff would mostly measure that noise.
2. Deterministic replay. It already exists: tier (a), serial -det_mode, exact counters (refs p0a/p0b/p0c). It catches
   wrong DECISIONS exactly, but only for code that can run serially, so it cannot see a race that needs real threads.
3. RECOMMENDED: consistency invariants (statistic C), computed from the dumps tier (b) already writes. No engine change
   is needed for the statistic itself. Cross-checks between independent records of the same A-Life state:
   - I1 creature names a squad that does not exist
   - I2 creature level differs from its squad's level
   - I3 creature game vertex differs from its squad's
   - I4 squad npc count differs from the number of creatures naming it
   - I5 creature smart differs from its squad's smart
   - I6 smart terrain population differs from the sum of its squads' npcs
   Races and lost writes break exactly these cross-records.

## Stock baseline (computed now from the 50 existing tier (b) dumps at 900 s; tools: perf-harness/tb_invariants.awk)

- 46 of 50 runs give IDENTICAL counts: I1 31, I2 0, I3 15, I4 2, I5 1, I6 12. The other 4 differ only in I1 (33-36)
  and I3 (13-15), each in runs where squads disappeared (world events, e.g. h05).
- The counts are not zero in stock (stock bookkeeping has its own standing mismatches), so C is scored AGAINST STOCK,
  like the other statistics, not against zero. I2 and I4-I6 never moved in 50 runs, so they have essentially no noise.
- The dose-1000 runs (4 % of updates lost) show the stock counts: C is indeed blind to LESS work, and U to WRONG
  work. The two complement each other.

## Dose-style control (must fail for C, must pass for U)

New overlay flag `-tb_wrong N [-tb_wrong_at 60]`: after 60 s, for N offline squad updates, run the update normally,
then apply one "lost write" of the kind a race produces: put the squad's assigned target back to its value from before
the update, without telling the smart terrain. Same amount of work (U unchanged), wrong state.
- Doses N = 1, 10, 100; 5 runs each.
- Required: N = 100 FAILS on C; U PASSES for every dose (proves "same work"). The detection limit is quoted in
  corrupted updates and as a share of offline updates.
- Overlay inertness: the flag changes the overlay hash, so 5 new reference runs WITHOUT the flag are first scored as a
  candidate against r01-r10 (must pass on all seven statistics). After that, the existing 15 refs and the band stay
  valid; the false-fail rate is recomputed with C added (band not re-chosen unless it goes above 5 %).
- Unverified until a smoke run: whether a Lua-side restore of assigned_target_id is enough to break I5/I6, or whether
  the game repairs it within seconds. If the smoke shows C unchanged at N = 100, I report that and propose a stronger
  write (also the squad's smart registration), not a quiet change.

## Box time

1 smoke + 5 inertness refs + 15 dose runs = 21 runs x about 16.5 min = about 6 h, under the same slice and fair-share
rules. Desk: about 1 h (overlay flag, C in tier_b.py, self-test with a fixture that has one I4 break).
