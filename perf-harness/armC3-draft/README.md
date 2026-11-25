# Arm C, run c3: a LIVE early kill (2026-11-25). Draft, NOT registered, NOT installed.

Why: c1 and c2 both killed X-Ray about 4 s after the crossing request, when the new level's script had already taken
the crossing marker. The other case (the marker still full at the restart) passed only offline. c3 runs it live.
Agreed with itrzone: ITR2 side as c2 plus its amendment 121; X side as c2 except the runner and the checker below.

## Files (installed in tools/ 2026-11-25; registered as section 71)
- Runner tools/t2_xray_c3.sh 67687701c850f7bee3fb35c3e7c07951380bf9d89cf4d524098213931abe1492 (from c2's d140e583).
  New lever T2_KILL_MARK=1: once boot 0 has its first snapshot, a watcher polls cross.txt every 20 ms and SIGKILLs the
  engine as soon as the file is non-empty; it hands a receipt to the main loop, which writes the restart row. With it
  the marker is also emptied and logged BEFORE boot 0, and the old track-time kill is off. T2_KILL_XS stays in the file
  and is 0 for c3. With T2_KILL_MARK unset the runner behaves as d140e583 (sandbox regression run: same lines).
- Checker tools/t3_armC3_xray_check.py 651cbce2a2bbf6f64afebd4b55abb28a8d91b72592f22705f350d1d9ed088cbb (from c2's
  c24ffd31). Check 3 rewritten, check 10 new, 1, 2, 4 to 9 unchanged.
- Unchanged: script 473dec25, Stance14.exe b4ce9c77, lib cb183b7b, save cordon1 (scoc a9f9a8a9, scop 6dea752b).
- Env: as c2, but T2_KILL_XS=0 and T2_KILL_MARK=1 (T2_RESTART_WAIT_S=150 stays).

## CodeRabbit (one run on the draft, three findings, all fixed, then re-witnessed)
1. Major: the wrapper (tb_run) may end between the watcher's kill and the main loop's next look; the kill would then
   be counted as an unplanned exit. Fixed: after the loop, if the watched engine is gone, wait up to 2 s for the
   receipt and book it. Witness "fastexit" (a wrapper that ends 30 ms after the engine dies): booked as planned.
2. Minor: a receipt left in a reused output folder would count. Fixed: both hand-over files are removed at the
   start. Witness "stalercpt" (old receipt present, marker taken at once): no kill booked, one boot.
3. Minor: the old track-time kill stayed armed in marker mode (c3's env sets it to 99999 anyway). Fixed: off when
   T2_KILL_MARK=1.

## What is known about the window (from c2's logs; boot 1's own, unkilled crossing)
- The marker is written in the script update that reads the cross pose, one statement before ChangeLevel.
- It is taken when the new level's Lua state loads zz_p1_puppet.script.
- ORDER, read from the engine's own log (out/t3armC_c2/boot1/tb/xray.log, line numbers): autosave saved (8579) ->
  "- Disconnect" (8581) -> "- Destroying level" (8584) -> first script of the new Lua state (12727) -> "loading script
  zz_p1_puppet.script" (14738) -> end of that loading phase, "phase time: 3650 ms" (14919). So the take comes after
  the autosave, the disconnect, the destruction of the old level and the loading of about 1,290 scripts before it.
- TIME. Hard bound: the autosave file was on disk 0.15 s after the request, and the take is later than that (order
  above). Upper bound: 4.46 s (c2's kill found the marker taken). Estimate: the puppet script is at 92 % of the lines
  of a loading phase the engine timed at 3.65 s, so the take is about 3 to 4.4 s after the write. The estimate uses
  log-line position as a stand-in for time; the engine log has no time stamps, so the take itself is NOT timed.
- c1's boot 1 shows the same order and a loading phase of 3,577 ms (c2: 3,650 ms): two crossings, one picture.
- The kill is expected 20 to 60 ms after the write (sandbox: 2, 4 and 20 ms).
- Side finding: c2's kill at 4.46 s landed at most about 1.5 s after the take. c1 and c2 were nearer to the early case
  than their 4 s figure suggests; a slightly faster kill or slower load would have flipped them.

## Predictions, X side (to be registered before the run)
| # | Check | Guess | Why |
|---|---|---|---|
| 3 | early kill by outcome: marker full at the restart, with boot 0's request, emptied | 95 % | the take is later than 0.15 s by the engine log's order, and about 3 s or more by estimate; the kill is expected within 60 ms |
| 1, 2 | runner rc 0; boot 0 has the request and no arrival | 95 % | the request line is flushed before the marker is written |
| 4 to 8 | boot 1 as in c2 | 90 % | c2 passed these; boot 1 does not depend on where boot 0 died |
| 9 | VITALS: boot 0 NONE (c2 had one), boot 1 two | 90 % | boot 0's Lua state never ends cleanly now; "none or one" is allowed |
| 10 | test save untouched | 97 % | the engine's level-change autosave is another file |
| all | **X side PASS** | **70 %** | unchanged by the better bound on check 3: the rest is the first real engine killed this early (Wine teardown, a cut autosave, a cut snapshot) |
Also expected, not scored: boot 0's engine log may be cut short (SIGKILL); the autosave file may be half-written (it
is not loaded: boot 1 loads cordon1 by name, and overwrites the autosave at its own crossing).

## Margins
| Value | Limit | Expected | Margin |
|---|---|---|---|
| kill after the marker's write | before the take: later than 0.15 s (hard, by log order); about 3 to 4.4 s (estimate) | 20 to 60 ms | 2.5 times on the hard bound; about 50 times on the estimate |
| first play pose after boot 1's request (check 5) | 90 s | about 22 s (c1 22.3, c2 22.4) | 4 times |
| poses boot 0 before the request (check 6) | 400 or more | about 2,160 | 5 times |
| poses boot 1 after the arrival (check 6) | 600 or more | about 2,400 | 4 times |
| first pose to the changer's destination (check 7) | 5 m | 0.004 m | very large |
| boot 0 wall limit | 1,000 s | kill at about 152 s | 6 times |
| new X-Ray run seen by ITR2 after its cross start (itrzone's number) | their range | 181 to 190 s: kill 0.1, pick-up by my 1 Hz loop up to 1, teardown 4.9, wait 150, boot 28 to 32 | told to itrzone |

## Offline witness, runner (fake engine sandbox, the real runner code)
| case | what the fake engine does | result |
|---|---|---|
| early | writes the marker, would take it 2 s later | killed 20 ms after the write; "was 37 bytes, now 0 bytes (was: its line)" |
| late | would take it 2 ms later | killed 4 ms after the write; marker still full (the watcher won by luck) |
| stale | a marker left from an older run before boot 0 | "boot 0: crossing marker at start: was 14 bytes, now 0 bytes"; then as early |
| miss | writes and takes in the same statement | NO kill, one boot, run to the end, rc 0: the checker fails this as "not an early kill" |

## Scorer self-test (copies of c2's REAL output, the runner log and restart row edited)
- c2 as it is: FAIL on 3 only (not an early kill). Edited to the early-kill lines: PASS.
- Mutants, each FAIL on 3 only: marker empty at the restart; marker not emptied; marker content of another request;
  restart row says 0 bytes; c2's old timer row; no kill row; restart line twice; no restart-wait line; no boot 0 start
  line; boot 0 marker not emptied.
- A false arrival in boot 1: FAIL on 3, 4, 5, 6, 7. A changed test save: FAIL on 10 only.
- The sandbox's real runner lines match the checker's three patterns (early, late, stale: 1 row, 1 start line, 1
  restart line; miss: 0 rows).
- Limit: the self-test's "good" case is c2's data with edited lines. No real engine has been killed this early yet.

## Each expected line or value against the paths that may not produce it
| Check | Expects | A path that would not produce it | Verdict |
|---|---|---|---|
| 2 | boot 0 CROSS_REQ | written and flushed before the marker, so it exists whenever the marker does | keep |
| 3 | kill row, start line, restart line | the kill row exists only if the watcher saw the marker: its absence IS the failure tested; start and restart lines are unconditional under T2_KILL_MARK=1 | keep |
| 3 | marker content = home,dest,utc of boot 0's request | the script writes level.name(), zone, utc: the same three values as fields 3, 4, 11 of the request | keep |
| 9 | boot 0 VITALS none or one | none expected now; allowed | keep |
| 1 | no FATAL / Lua error in boot 0's engine log | a SIGKILLed engine may leave a short or missing log: read as no lines, which passes | keep, named |
| 10 | save hashes | none conditional | keep |
