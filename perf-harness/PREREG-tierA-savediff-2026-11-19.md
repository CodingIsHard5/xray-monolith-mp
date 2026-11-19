# Pre-registration: tier (a) by save-file comparison, and the same two controls (2026-11-19)

Written and committed BEFORE any run that writes a save. Ruling by the Overseer 2026-11-19 after the dump-based
tier (a) check failed to catch both controls (see PREREG-tierA-control-2026-11-19.md; that check is NOT adopted).

## Why saves
The game's own save serialises all A-Life state, so it is complete by construction. The text dump was our guess
at what matters, and it was blind.

## What is run
- Build RefDet12 (CI 44510900390), serial det_mode, save bench1, headless, `-det_times 25,300,900 -det_save`.
- At each of the three time points the test script issues the console command `save det_<tag>_<k>` at the same
  update in every run (after the text dump). Saves go to the project's own run dir, never Caden's.
- Reference runs: `s1a`, `s1b`, `s1c`. Control runs: `sc1` (`-det_perturb skip`), `sc2` (`-det_perturb draw`),
  the SAME two perturbations as before, applied once at 60 s.

## The check
- Tool: `perf-harness/save_diff.py A.scop B.scop --online dumpA dumpB --tier-a`, as committed here.
- An object record = its saved state packet + its saved update packet, compared byte for byte.
- OFFLINE = not online in either run, by the text dump taken at the same checkpoint.
- PASS at a time point = no offline object record differs, no offline object exists in one run only, and the
  game-time chunk and the registries chunk (info portions, relations, news, tasks, statistics) are byte-equal.
- Online objects are counted and listed but are not part of tier (a) (judged statistically, as ruled).
- Script-side save file (`.scoc`, the Lua state store): compared as a whole file and reported as a second view.
  It is NOT part of the verdict in this round (no per-key reader yet); if it differs between reference runs I
  report that as an open gap in the check.

## Masking rule
- Nothing is masked at the start.
- A field may be masked only if (1) it differs between `s1a` and `s1b`, and (2) I can show from the engine source
  that it is not simulation state (e.g. a wall-clock timestamp). Every masked field is listed with its reason.
- Masks are derived from `s1a` vs `s1b` ONLY, then frozen. `s1a` vs `s1c` must pass with the frozen masks, without
  further change. The control comparisons are made after the masks are frozen.
- If reference runs differ in offline records for a reason that IS simulation state, there is no mask: the check
  fails for reference pairs and is not adopted in this form; I report what differs.

## Decision rules
1. A control is CAUGHT if `sc<n>` vs `s1a` gives PASS at 25 s (before the perturbation) and FAIL at 300 s or 900 s,
   and the engine log shows the `* det_perturb` line. A FAIL at 25 s voids that control run.
2. Tier (a) is adopted only if BOTH controls are caught AND `s1a`/`s1b` and `s1a`/`s1c` both pass at all three
   time points.
3. Reported in every case: per run, how many offline object records change between the 25 s save and the 900 s
   save (how much the offline world moves in the window), and what the caught difference consists of.
4. If a control is missed, it is reported as a miss. A longer window or a raised A-Life time factor (identical in
   all runs) is then the next proposal; it is not tried silently inside this round.
