# Arm C, run c2: runner and checker amended after c1 (2026-11-25). Draft until registered as section 69.

Overseer's ruling after c1 (section 68): check 3 must test the outcome, not a log line written only conditionally; add
an offline witness for a kill AFTER the new level's script has taken the marker; before registration check every
expected line or value against the paths that may not produce it.

## Files
- Runner tools/t2_xray_c2.sh d140e58300fd9b43999877338f085f69529fb96f28d380fd18453741e7481385 (from 40b87576; 7 lines).
  ONE change: at every restart boot it logs `boot k: crossing marker at restart: was <n|absent> bytes, now <0|absent>
  bytes (was: <first line|nothing>)`, the "now" read back after the emptying. 40b87576 stays on disk untouched.
  Not reviewed by CodeRabbit (a log line; the account is rate-limited): say if it must be.
- Checker tools/t3_armC2_xray_check.py c24ffd31e8b56a0627780c83396000777af2c2c91f663ebdabbc7d8b8bb673d7 (from 7ec98e73).
  Check 3 now: one planned-kill row with 4 to 6 s; the restart wait logged; exactly one marker line for boot 1 whose
  "now" is 0 or absent; and no CROSS_RESUME before boot 1's own request (no false arrival). The other eight unchanged.
- Unchanged: script 473dec25, Stance14.exe b4ce9c77, lib cb183b7b; env as c1.

## Offline witness, runner (fake engine sandbox, real runner code, planned kill, restart)
| case | runner | marker line logged | marker seen by boot 1 |
|---|---|---|---|
| kill BEFORE the script takes the marker (37 bytes left) | c2 | was 37 bytes, now 0 bytes | 0 bytes |
| kill AFTER the script took it (file empty): c1's real case | c2 | was 0 bytes, now 0 bytes | 0 bytes |
| no marker file at all | c2 | was absent bytes, now absent bytes | none |
| kill AFTER the script took it | old 40b87576 | NO line (c1's failure reproduced) | 0 bytes |

## Checker tests (copies of c1's REAL output, the runner log edited)
- c1 as it is: FAIL on 3 only (the old runner wrote no marker line): same verdict as section 68, nothing new.
- c1 + the line "was 0 bytes, now 0 bytes": PASS. "+ was absent, now absent": PASS. "+ was 37, now 0": PASS.
- Mutants, each FAIL on 3: "now 37 bytes" (marker not emptied); the line twice; no restart-wait line; a CROSS_RESUME
  before boot 1's request (a false arrival; also fails 4).

## Each expected line or value against the paths that may not produce it
| Check | Expects | A path that would not produce it | Verdict |
|---|---|---|---|
| 1 | runner log's last line ends `rc=0` | cancelled or refused runs end differently: that is a real failure | keep |
| 2 | boot 0 has a CROSS_REQ and no CROSS_RESUME | the kill landing before the request is read (pose seen by the runner first): possible in principle, the runner reads the pose at 1 Hz and the script every frame, so the script is first; a kill after the first update of the new level gives a RESUME: registered as FAIL of "kill inside the load" | keep, named |
| 3 | kill row; `restart wait:`; marker line | restart wait is logged only for a planned kill with a wait > 0: both are fixed by the env; the marker line is now unconditional (table above) | fixed |
| 4 | boot 1's own log has one request and one resume | the log is the new boot's own because the runner starts each boot with a new folder; unconditional | keep |
| 5 | first applied play pose at most 90 s after the cross pose | needs ITR2 to return to `play`; if it never does, a real failure | keep |
| 6 | pose counts | none conditional | keep |
| 7 | first pose after the resume near the changer | none conditional | keep |
| 8 | stance and height on every pose | none conditional | keep |
| 9 | VITALS: boot 0 none or one; boot 1 exactly two | a VITALS line is written when a Lua state ends cleanly. Boot 0 is killed: its state before the level change ended cleanly (one line, as in c1), or none if the kill came earlier: both allowed. Boot 1: one at the level change, one at the run's end: the second needs a clean end, which rc 0 also demands | keep |
| reported | level-mismatch count, game time to the request | reported only | - |
