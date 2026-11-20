# Pre-registration 2026-11-20 08:50: what one extra REAL player costs today's co-op server (measurement only)
Approved by the Overseer 2026-11-20 (staged: smoke; then one series to 4 players, checks, then to 8; full twice, inert once).
Desk plan: DESKPLAN-coop-second-actors.md in the project folder. Committed BEFORE the smoke boot.

## Setup
- Binaries: the co-op worker's deployed dedicated build (OurDedicatedDX8.exe, CI run 44112131745, branch renderless-r0 at
  089c3c42), COPIED to ~/Projects/StalkerGameEngine/coopload. No build, no push, their folders only read.
- Runner: the same as their harness: system wine 9.0, no display, `taskset` + `nice -n 19 ionice -c3`, same engine flags
  (`-mp_host -xrnet_udp -mp_noauth`, clients `-noinput -coop_noimgui -coop_test_input`), save n1-autosave (Cordon).
  DIFFERENCES from their harness, stated: the Wine prefix is a COPY of theirs (same contents, not the same folder); port
  5461 instead of 5445; server memory cap 6 GB instead of 12 GB; every client pinned to its own pair of cores from 4-15
  instead of all on 4,5; cores 0-1 left free; server gamedata is a folder of links to gamma_flat plus ONE added test script
  (zz_eperf_svbinder.script); extra server flags `-fp_svtime -coop_clframetrace` (and `-fp_inert` in the inert series).
- EXCLUSIVE quiet claim held for as long as any of these processes is alive. Headless, never on :0. Yields to protected titles.
- Tools: tools/coop_load.sh (launcher), tools/coop_sample.py (processor time once a second from /proc).

## Quantities, per step of N joined players, over a 120 s window (n = 120 one-second samples) after a 30 s settle
- M = server main-thread processor ms per second of wall time. Also: all server threads, server resident memory, each
  client's processor ms per second, idle share of cores 0-1.
- S = per actor on the server, from the script's own timer: calls and ms per call of the player script update
  (sv_binder.csv), read at the start and end of the window.
- Count of "binder RETIRED" lines in the server log; server frames per second (its own trace).

## Smoke (1 server + 2 clients; NOT a result). It answers: does it start from the scratch copy; do the joined players'
binders tick or retire; does the overlay load (log line "sv_binder: installed"); does a joined actor carry items.

## Predictions, both branches (numbers fixed now; I do not know which branch is true; the co-op notes suggest RETIRED)
Shared guesses: server frames per second 60-100 (its frame sleep gives a 10 ms floor); M at N=0 between 100 and 600 ms per
second; the host actor (id 0) binder ticks once per server frame at 0.4-1.0 ms per call.
Branch TICK (joined players' binders keep ticking):
- S for a joined player: 0.63-1.04 ms per call (0.834 +/- 25 %), calls per second within 20 % of the host actor's.
- M grows per joined player by S x frames per second plus the engine part: 50-120 ms per second per player.
- At 8 players M is 400-960 ms per second above N=0: the main thread is at or near one full core.
Branch RETIRED (each joined player's binder raises on its first update and gets no more ticks):
- S for a joined player: at most 2 calls in total; one "binder RETIRED" line per join (or the silent budget after 32).
- M grows per joined player by the engine part only: 2-20 ms per second per player (a guess from the 0.19 ms a calm NPC
  costs per 30 Hz tick; a player has no AI). At 8 players M is 16-160 ms per second above N=0.
Either branch: the relay of every player's state to every other grows with N squared; predicted too small to see at 8.

## Controls
CONTROL 1 (must agree with a known quantity): a joined player's S within 25 % of 0.834 ms per call (0.63-1.04) and its
calls per second within 20 % of the host actor's, in both full series. If the binders are retired, control 1 FAILS and the
record reads: "today's co-op server does not run the player script per player; there is no per-player script cost to
measure"; M against N is then reported labelled ENGINE-SIDE SLOPE, NO PLAYER-SCRIPT VERDICT.
Stated in advance: 0.834 was measured in the single-player build with a screen; the renderless server runs no HUD code, so
a miss can also mean the server's call is truly cheaper. The host actor's S in the same boot is recorded to tell these apart.
CONTROL 2 (must show about zero): inert series (`-fp_inert`: joined players' script update replaced by an empty function):
S for joined players under 0.01 ms per call. If not, the overlay is not in force and the inert series is void.
NOISE: N=0 is measured twice in every boot (two 120 s windows). A step in M is "resolved" only if it exceeds twice the
difference between those two windows; otherwise it is reported as not resolved.
LINEARITY: slope of M from 0-4 against 4-8 reported; no rule.

## Order and stop rules
1. Smoke. 2. Full series A to N=4; CHECK: cores 0-1 idle share above 70 %, server under 6 GB (the launcher's cap enforces
it), every client under 500 ms of processor per second. Only if all hold: continue the same boot to N=8. 3. Full series B
(0,1,2,4,8). 4. Inert series (0,1,2,4,8); run even if control 1 failed.
Stop the series on: any crash or a client that never joins within 4 minutes, the server killed by the cap, or a failed
check at N=4. One retry at most, then report. 16 players are not part of this.
What this cannot show: more than 8 players, players spread over a level (all join at the host's place), bandwidth, real
rendering clients, internet conditions.
