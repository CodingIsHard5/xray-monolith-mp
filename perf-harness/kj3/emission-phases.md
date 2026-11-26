# Emission / psi storm phases for the `e` line (Stage 2, 2026-11-26)

Implemented in harness/zz_p1_puppet_kill6.script (`SURGE_PH`, `PSI_PH`, `em_phase`). Desk work: thresholds read from the
scripts, never seen in the engine.

Paths: SM = run/gamedata/scripts/surge_manager.script, PSM = run/gamedata/scripts/psi_storm_manager.script (both
byte-identical to GAMMA's, per plan-facts/emission-clock-desk-read-2026-11-26.md).

## Rule
- t = `game.get_game_time():diffSec(M.inited_time) / level.get_time_factor()` (SM:676, PSM:317, without their math.ceil),
  printed with 2 decimals.
- d = `math.ceil(t)`. That is the scripts' own `diff_sec`, and every stage test in them is `diff_sec >= X`.
- phase = the highest i for which d >= X[i]; 0 = no event (kind none), and also d < 0 inside an event, which the scripts
  never produce.
- So phase i starts as soon as t > X[i] - 1, not at t >= X[i]. At t = X - 1 exactly you get the previous phase; at
  X - 0.99, X and X + 0.5 you get phase i (tested at every threshold in `em_surge_bounds` / `em_psi_bounds`).
  The scripts act only on their 1 s update (SM:51-57 `_tmr`), so a stage's action runs at the first update with
  d >= X, up to 1 s after the phase changes in the `e` line.

## Emission (kind surge), 222 s
| phase | X (d >=) | what the script does there | line |
|---|---|---|---|
| 0 | none | no event (SurgeManager.started false) | SM:606-608 |
| 1 | 0 | sky fx_blowout_*, begin sound | SM:708 |
| 2 | 30 | rockets, "hide" task, first message | SM:734 |
| 3 | 47 | impact sound | SM:765 |
| 4 | 50 | rumble starts (fades in to 75) | SM:779, SM:789 |
| 5 | 80 | first earthquake (quake20) | SM:796 |
| 6 | 108 | wave-1 sound; psi pp effect and actor hits begin | SM:854, SM:990 |
| 7 | 120 | wave 1 starts (start_wave(1)) | SM:870-872 |
| 8 | 156 | wave-2 sound | SM:877 |
| 9 | 168 | wave 2 starts (start_wave(2)) | SM:894-896 |
| 10 | 200 | quakes and rumble fade | SM:903, SM:939 |
| 11 | 212 | end message | SM:956 |
| 12 | 220 | after sound | SM:972 |
| 13 | 222 | end due: end_surge at the next 1 s update (surge_time = 222, SM:175) | SM:677 |

Stages I left out on purpose (they do not mark a change the partner can use): siren at 25 (SM:727, plays only when the
warning option is "siren"/"radio_siren"; this install has "siren_radio", so it never plays); quakes 40/60/80/100 at
100/102/104/106 (SM:822-844); second message 90 (SM:805); quake fade steps 202/206/209/214 (SM:910-931); hits end 210
(SM:1012).

## Psi storm (kind psi), 103 s
| phase | X (d >=) | what the script does there | line |
|---|---|---|---|
| 0 | none | no event (PsiStormManager.started false; or a surge is running, which wins) | PSM:297-299, PSM:313 |
| 1 | 0 | sky fx_psi_storm_* | PSM:335 |
| 2 | 10 | rockets and siren (warning "siren": plays here) | PSM:344 |
| 3 | 12 | hide task | PSM:350 |
| 4 | 30 | vortex window opens: next_vortex_at = d + 3..6 (PVF:137-138), vortices created while d <= 77 | PSM:357-364 |
| 5 | 78 | no more vortices (the window test is d <= 77); pending ones still strike, last at d <= 97 | PSM:357, PSM:373 |
| 6 | 103 | end due: finish() at the next 1 s update (psi_storm_duration = 103, PSM:64) | PSM:318 |

## Other lines (same clock)
- `w,<n>,<z>`, while `blowout_waves[n].effect:playing()` (SM:1064): z = `inited_pos.z - (ceil(game_s/2) -
  inited_time) * 3`, game_s = `diffSec(inited_time)` (SM:688, SM:1065; inited_pos.z = actor z + 250 at the start,
  SM:496; inited_time = diff_game_sec at the start, SM:872/896). The script keeps no field with the current z: it only
  moves the particle (SM:1066), so the reader works z out itself.
- `v,<n>,<x>,<y>,<z>,<s>` for each vortex with `hit == false` (PSM:412, PSM:373-382): n = its index in `vortexes`,
  position = `particle_pos` (actor y + 20, PSM:410), s = `start_time + 20 - t`. It strikes at the first update with
  ceil(t) - start_time >= 20, that is t > start_time + 19, so s is in (-1, 1] when it goes (plus up to 1 s of
  update lag).
- `ns,<surge_s>,<psi_s>` = `math.floor(_delta - now:diffSec(last_*_time))` (SM:266, PSM:132), clamped at 0 (overdue,
  start pending); -1 while that kind is running (the scripts roll the next date at the end, SM:337-341) or when the
  manager is not there.
