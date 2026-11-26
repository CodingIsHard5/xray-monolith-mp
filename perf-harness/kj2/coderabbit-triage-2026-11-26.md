# kj2: CodeRabbit pass 1 on 5b54a1d35 (20 findings, all "major"), my triage. Full output kept in my scratchpad (cr_kj2.txt).
FIX (before the final hashes):
1. reader: a pending kill must not be confirmed (KCONF) on a row already marked stale (id reused by another section).
2. reader: target released between the kill call and the next export: the pose path set dd, so no KCONF was logged and
   check 14 (exactly 2 KCONF) would fail a good run. Leave dd to the export when a kill is pending.
3. checker: a second k or o line for one id in one snapshot overwrote the first; check 15 could miss a dead/stale line.
   Duplicates now fail check 15 by name.
4. checker check 13: the first acknowledged pose read by the watcher just AFTER the snapshot that names it (the allowed
   50 ms inversion) failed for lack of an earlier pose. Handle the first pose like the later ones.
5. checker: a malformed KCONF/KNOCONF row is an input error (FAIL by name), not a check-14 failure an NV rule can excuse;
   "--10" must not raise.
6. checker check 9: the stop's measured length is judged against the registered 20 s (new bound, derived in the text).
7. fixture maker: refuse a destination inside a source run folder.
8. text: "an id with no object keeps k,<id>,0.000,dead" was loose. As in amendment 134: no object = `dead` with the row's
   applied total; 0.000 only when the id has no row. Told itrzone.
DECLINED, with reasons:
- 10 s hold refreshed by a repeated dead observation: registered behaviour (amendment 134, "refreshes the 10 s hold").
- pending kill not carried over a crossing; hit log truncated at a crossing; older ktab after a failed save; first-read
  guard with both LOST limits 0: no crossing in kj2; the last three are kill4's bytes, unchanged. Limits in the text.
- runner: non-atomic install, claim grep: kj1's runner bytes (one line changed), ran in kw1 and kj1; not touched before kj2.
- files under kj2/ versus tools/ and harness/: the kj2/ files are copies for the record; the pinned paths are tools/ and
  harness/ in the main project folder, hash-checked by itrzone's boot script. Said in the text.
- "can ITR2 take a plain k line as confirmation": itrzone's side; their unit case 20 covers it. Passed on.

## Pass 2 (ordered by the Overseer), on the fix round ae115dd3d, 7 files, 2026-11-26, ended before 21:49 UTC (by `date`)
Result: "No new findings" (the tool lists the 20 earlier findings as previous). No byte changed after it; the pins in
the registration text stand. Fixes 1-8 above were made in ae115dd3d, each with a test that fails without it (names in
kj2/NOTES.md, "Fix round 2").
