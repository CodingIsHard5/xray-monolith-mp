# Joint pose v4 / kill channel, draft 1 (ITRZone master 21d554d): EnginePerf's answers to the [X] lines
Desk only. Read against harness/zz_p1_puppet_lost3.script 5bf69e5c (the v3 reader in use). Nothing built or run.

## Section 1, the version mark: agreed (8th header field, the digit 4), with one correction
My v3 reader does NOT ignore an 8th header field. Its header patterns are anchored to exactly 6 or 7 fields
(lines 272, 279, 280); a pose with 8 fields matches neither, so it is counted TORN, never applied, and the pose goes
LOST after 5 s. So the true sentence is: "a v3 reader refuses a v4 pose whole". That fails closed and I think it is
the right behaviour, but a v4 ITR2 cannot drive a v3 X-Ray. Unknown line types are ignored (the 5-field `k` line
does not match anything in the v3 reader).
The v4 reader: 7 fields = version 3, `k` lines in it are not applied and are counted; 8 fields with the 8th equal
to 4 = version 4; any other 8th field, or more than 8 fields: refused whole and counted (`kver`).
The old form: today's v3 reader still ACTS on a bare `k,<id>` line: it kills that id with the actor as killer
(lines 288 and 326). The v4 reader removes this in every version. Until v4 is installed, a v3 pose must never
carry a `k` line; please confirm your v3 writer cannot write one.

## Section 1, the section name on the k line: agreed
I compare it with the object's section name now (online: the game object's; offline: the alife object's). This
closes same-id reuse except by another creature of the same section, where applying the damage is still wrong but
cannot be told apart by anyone; limit to state.

## Section 2, the snapshot version
The snapshot header today has 6 fields: `seq,<n>,<t_xray_ms>,<t_track_s>,<level>,<run id>`. Proposal: a 7th
field, the digit 4, the same number as the pose version, so there is one bridge version and not two series.
A snapshot with 6 fields has no `k` lines. Your reader takes the first six, so an older mod is not disturbed.

## Section 4, restated (replaces the draft's bullets; the first, third and fourth stand as written)
- Part hit, creature online and alive, `total - applied` > 0: a near-zero hit with the actor as source, then health
  lowered directly by the difference, stopping at 0.01 unless `dead` is 1; then `applied = total`. A difference
  above 1.0 is cut to 1.0 and counted. A total below `applied` within one session is ignored and counted.
- Offline part hit: owed. `applied` stays, so the snapshot shows less than the total; applied when the creature is
  online again. An owed part is lost when the ITR2 session changes (the new totals start at 0).
- The table is written to a file at the crossing request and read back at the resume, only when the run id matches.
- The actor takes no damage from any of this (standing rule); checked in the witness run.

## Section 5: agreed as written. One addition to the "ITR2 restarts" row, X-Ray column: "owed parts dropped".

## Section 7 [X]: agreed; these are my open checks. Added: the near-zero hit may play a pain reaction on every
report, or may be filtered before GAMMA's callbacks; only the X-only witness tells.

## Section 8: agreed, same four questions as my draft's section 11.
