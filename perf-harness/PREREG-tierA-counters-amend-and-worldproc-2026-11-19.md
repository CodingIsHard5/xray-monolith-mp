# Pre-registration 2026-11-19 (two parts), written before the runs

## Part 1. Amendment to PREREG-tierA-counters: checkpoints at fixed A-Life pass counts
This amendment is made AFTER seeing data, and is labelled so. What the data showed (runs c0a c0b c0c cs1 on RefDet14):
random streams exact everywhere; per-object update counts exact except that one run at one checkpoint (c0a at 300 s, cs1 at
900 s) was exactly ONE scheduler pass (20 consecutive objects) ahead; totals equal again later. The snapshot is taken by time
since the start point, the A-Life pass runs on the object scheduler's clock, and the two are a few frames out of phase
between runs. As registered, adoption condition 1 (references exact at all checkpoints) FAILED, and condition 4 failed too
(state diff 7/18/19 against a floor of 7/16/14). That result stands and is reported.
Change: flag `-det_passes 50,600,1650` fires checkpoint k when that many A-Life passes have run since the start point
(a pass = a frame in which a scheduled offline squad or smart-terrain script update ran), instead of at 25/300/900 s.
No build. Runs: p0a p0b p0c (references), ps1 (skip 1), pad (extra A-Life draw), all with
`-det_passes 50,600,1650 -det_save -det_lua_streams -det_counters` on RefDet14.
Adoption, same four conditions as before, with "25 s / 300 s / 900 s" read as checkpoint 1 / 2 / 3:
1. references C = 0, all pairs, all checkpoints; 2. ps1 caught at checkpoints 2 and 3 by the skipped squad's `sq` update
count, C = 0 at checkpoint 1; 3. pad caught at checkpoints 2 and 3 by the `stream alife` line, C = 0 at checkpoint 1;
4. state diff of the reference pairs no worse than 7/16/14.
If the perturbation (at 60 s) lands before checkpoint 1 in a control, that control is void for checkpoint 1 and said so.
If condition 1 fails again, no further amendment: report the size and stop (Overseer: no more harness rounds without asking).
Condition 4 is expected to stay noisy (it depends on online play); if only 4 fails, that is reported as such, not adjusted.

## Part 2. Does stock A-Life run with NOTHING online? (plan section 6, unknown 2)
Run w1: `-det_times 25,300,900 -det_save -det_lua_streams -det_counters -det_noonline` (switch distance forced to 0.5 m from
the start point and re-applied every 100 updates). Reference: c0b (same build, same flags without -det_noonline).
Negative control wn: same as w1 plus `-det_noalife` (scheduled objects per pass forced to 0 every update).
"Runs correctly" for w1 means ALL of:
 a. all three checkpoints written, no crash dump, and no more "LUA error"/"stack traceback"/"FATAL" log lines than c0b has;
 b. nothing online: at 300 s and 900 s the dump shows no online object other than the actor and objects parented to it;
 c. counters advance: total scheduled update() calls between 300 s and 900 s is at least 90 % of c0b's (21,340), and every
    scheduled object offline at both checkpoints has a higher count at 900 s than at 300 s;
 d. squads move: the number of squads whose game-graph point differs between 25 s and 900 s is at least c0b's number for the
    same squads. Both numbers are reported. If c0b's number is 0 this criterion says nothing and that is stated.
Control: wn must FAIL criterion c (counts do not advance). If wn passes c, the test cannot tell a dead A-Life from a live one
and proves nothing.
Limits, stated now: the level and the actor are still loaded and the client half still runs; this is not a separate process
and not a level-less server. 15 minutes only. It shows the stock A-Life tolerates having nothing online, no more.
