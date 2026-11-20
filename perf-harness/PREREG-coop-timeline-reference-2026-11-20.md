# Pre-registration 2026-11-20 09:51: no-client reference boot for the co-op series (the time confound)
Approved by the Overseer 2026-11-20: 1 boot. Committed before the boot.
Why: in series A, B and I the player count was tied to the time since boot. M (server main-thread processor ms per second)
dipped by about 35 ms at the 2-player window, about 640 s into the run, in all three boots.
Run Z: tools/coop_series.sh with COOP_NOCLIENTS=1: the same scratch server, flags, claim rules and windows as series A/B; in
place of every client join the driver sleeps 16 s (the join time seen in A/B), so the six windows fall at the same times
since boot (window ends at about 160, 310, 476, 642, 827, 1040 s; the server's own clock is logged per window to check
this: each window end must be within 15 s of series A's, otherwise the comparison is reported as misaligned).
Quantities: Z1..Z6 = M in the six windows (n = 120 one-second samples each). Z0 = mean of Z1 and Z2 (the two windows that
were "0 players" in A/B).
DECISION RULE (fixed now):
- SAME DIP: Z4 (the window that held 2 players) is at least 25 ms below Z0. Then the dip is the world's timeline. The
  full-arm reading becomes "no rise above the timeline" IF the residual at 8 players, R8 = mean of A and B at 8 minus Z6,
  is at most 24.4 ms (twice the largest difference seen between A and B in any window, 12.2). The bound on the engine-side
  cost is then max(24.4, R8) / 8 ms per second per player. If R8 is above 24.4 a rise IS resolved and R8 / 8 is reported.
  Residuals for every window are reported for A, B and I.
- NO DIP: Z4 is within 10 ms of Z0. Then the dip came with the players, and the full-arm slope stays UNRESOLVED.
- Between (10 to 25 ms below): AMBIGUOUS, reported as such; a reversed-order boot would be asked for, not run.
Also reported, no rule: the busiest non-main thread per window (it rose from 7 to 20 ms per second with players in A/B/I;
if it stays near 7 here, that rise belongs to the players), server memory, frames per second.
Identity of the sampled process (as ruled): command line with the scratch path and port 5461, the pid recorded by the
launcher, resident size; thread = the one whose id equals the pid; thread time <= process time in every sample.
One retry at most on a crash; then report.
