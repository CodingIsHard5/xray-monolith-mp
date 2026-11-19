# Pre-registration: growth of the reference-vs-reference difference (2026-11-19)

Written and committed BEFORE the first run. Rules set by the Overseer (ruling of 2026-11-19, option 3).

## What is run
- Build: `perf/equivalence-harness` at CI run 44510900390 (`RefDet12.exe`), the same exe for every run.
- Mode: serial det_mode (`-det_mode`, worker tasks inline, every mt_* off, fixes 0-6 of the causes list),
  save `bench1` (Rostok), headless.
- Two reference-vs-reference pairs: `g1a`/`g1b` and `g2a`/`g2b`.
- State dump at 25 s, 300 s and 900 s of engine time after the start point (`-det_times 25,300,900`).
- Run after the timing baseline has finished, never at the same time as another run of mine.
- Tool: `perf-harness/det_growth.py` (in this commit), unchanged after this commit for these runs.

## Definitions (fixed here)
- Position-derived fields: `pos`, `lvid`, `gvid`. Discrete fields: every other field of an object line
  (section, name, parent, online flag, alive, hp, community, rank, money, squad, squad action/target/smart/
  npc count/player, smart npc count) and every header line (faction relations, actor money/items, object count).
- Offline object: online flag not "true" in both runs. For offline objects ALL fields must match.
- "Exact" at a time point = no object present in only one run, no differing header line, no differing offline
  object, no differing discrete field of an online object. Online position-derived differences are counted
  and their spread reported, not failed.

## What the result will mean
1. If BOTH pairs are exact at 25 s, 300 s and 900 s: adopt as the tier (a) check - discrete fields and offline
   objects exact; online positions within the measured reference-vs-reference spread at the same time point;
   plus a control that must fail (one shifted RNG draw or one skipped squad update) and must be caught by the
   DISCRETE check, not by positions alone.
2. If either pair is not exact at 300 s or 900 s: the exact check covers only the window in which both pairs are
   still exact (25 s, or 300 s). Report the first discrete field / offline object to diverge and at which time
   point. The Overseer then decides whether one more bounded round on cause 7 is worth it.
3. If either pair is not exact already at 25 s: report that; it contradicts the three earlier pairs.
- A run that crashes or times out is repeated once with the same tag suffix `r`; it is reported, not hidden.
- No other statistic will be used to argue the outcome.
