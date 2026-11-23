# Walk stall: one reproduction and classification (registered 2026-11-22, before any run)

Order (Overseer, 2026-11-22): this is the hang after the dogs hit the actor, at about 442 s of engine time, seen twice
(INFORMATION.md 2026-11-22 07:39). Reproduce it once on RefDet14 with the walk route, using the fixed sampler and a
screenshot of the X display at stall time. Classify it as dialog, spin or deadlock. If it is a deadlock, take a wine
thread dump. Not gated on Caden: it is a diagnosis, nothing is changed.

## Run
One run: `TB_NOCLAIM=1 TB_EXE=RefDet14.exe TB_TIMEOUT=1500 tools/tb_run.sh walkst1 bench1 -tb_walk 60`. It is the
same exe, save and route as the two stalls (tag "walk"). It runs headless on :77 and is driven by
tools/stall_run.sh. The machine is taken through WANT, EXCLUSIVE, and fair share.
At most one retry, and only if the run is void (a crash before 400 s of engine time, a protected title, or a
disturbed start).

## Stall detection
The engine log (run/appdata/logs/xray_caden.log) must be over 100 KB (the load is finished), the engine process must
be alive, and the log size must be unchanged for 90 s. The engine process is the process whose command line matches
the exe and has the largest resident memory. Its per-thread CPU comes from /proc/<pid>/task/*/stat, using the fields
after the last ')'.

## Captured at the moment of a stall (all read-only)
1. A screenshot of :77 (xwd, converted to png), plus `xwininfo -root -tree`.
2. Per-thread CPU ticks taken twice, 30 s apart, with each thread's state and wchan.
3. Resident memory, and the last 40 log lines.
4. Only in the deadlock and unclassified cases: `winedbg` attached to the engine for `bt all`, with output saved.
   There is no PDB for RefDet14 on disk, so frames show module+offset; symbols are a later desk step.
After the captures the engine is killed (wineserver -k), and tb_run.sh records rc=4. This is expected, not a failure.

## Classification rule (applied in this order, first match wins)
- NOT REPRODUCED: no stall is detected before the run ends or times out. I report it, and the candidate stays open.
- DIALOG: the screenshot shows a message box or dialog, or the last 40 log lines contain "FATAL", "assert" or
  "Expression". The dialog's text is recorded.
- SPIN: the engine process used >= 0.5 core on average over the 30 s window (the sum of all threads' ticks). The
  hottest thread is named.
- DEADLOCK (idle hang): it used < 0.05 core and there is no dialog. The winedbg dump shows where the main thread waits.
- UNCLASSIFIED: anything in between (0.05-0.5 core). Numbers and the dump are reported as they are.

## Guess (registered, may be wrong)
DEADLOCK/idle hang, at about 60 %. The old 07:20 run's samples, though from unreliable pids, showed almost no CPU,
and the log stops right after hit and pain-sound lines. My second guess is a hidden dialog.

## RESULT (scored once, 2026-11-22 23:50; run 23:32:48-23:42:44, start load 1.13, no retry)
REPRODUCED at the same spot. The log stopped at 427,697 bytes after the same pattern: dog_normal_red hits, a pain sound,
"Damage to actor". Stall detected 23:42:09.
Classification, first match in the registered order: **DIALOG**. The screenshot (walkstall_screen.png) shows the
"GAME OVER / PRESS SPACE TO CONTINUE" screen: THE ACTOR DIED to the dogs. The game sits on its death screen waiting for
a key, which a headless run never sends. No FATAL or assert lines in the log. No winedbg dump (it is only for the
deadlock and unclassified cases).
CPU in the 30 s window: 2.68 cores, so the SPIN bar would also have matched. But the main thread (X-RAY_Primary)
was sleeping (state S, 0.15 core). The CPU was the OpenAL mixer (1.0 core) and dxvk-submit (1.0 core) on the
death screen. That is reported only and not chased.
Meaning: this is NOT an engine hang. My -tb_walk harness teleported a stationary, unarmed actor into
bar_zastava_dogs_lair_2, and the dogs killed it. The defect candidate "hang after mutant damage while walking" is
CLOSED as a harness artefact. Both earlier stalls fit this (same lair, same hit pattern before the log stopped).
My guess was wrong: I said deadlock at about 60 %.
Consequence for later work: any headless run that walks the actor near mutants (P1) must keep the actor alive
(god mode or a health refill each tick) or treat death as an end state. Otherwise a run "hangs" on the death screen.
