# Lost pose, fix L2 (2026-11-25)

Pre-flight L's cross boot called the pose lost 135.0 s after the resume (rule 120 to 122.5 s): section 63.
- `zz_p1_puppet.script` (installed as harness/zz_p1_puppet_lost2.script): the first pose READ after a level change
  does not restart the lost timer and keeps the cross limit; a pose taken on a later read does. Three lines of logic
  (`p_xwait`), active only in a Lua state that began with a crossing marker and with a lost limit on.
- `t2_xray.sh` (tools/t2_xray_lost2.sh): the lost runner with only the script name changed.
- `t3_pfL.sh` (tools/t3_pfL2.sh): the driver with only the runner name changed.
- `lost_stub.lua`: unchanged copy of the offline stub.

Offline (stub, limits 5,000 / 120,000 ms; phase 1 crosses at seq 100, phase 2 is the new level with no writer):
| script | pose file at the resume | LOST after CROSS_RESUME |
|---|---|---|
| old 0347bdee | as at the request (seq 100) | 120,000 ms |
| old 0347bdee | writer wrote 70 more poses during the load (seq 170), then died | **135,000 ms** (the engine's 135,010) |
| new | seq 100 | 120,000 ms |
| new | seq 170 | **120,000 ms** |
A live writer after the level change: no LOST, 2,830 poses applied, the same with old and new.
No crossing (keep, freeze, same session, limits off; 30 s gap, 3 s pause): old and new write the same log, byte for
byte except the wall-clock field of the G lines.
