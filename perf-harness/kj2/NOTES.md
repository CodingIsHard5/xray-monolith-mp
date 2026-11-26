# kj2 desk work: notes (2026-11-26)

## Limits first
- Desk only. Nothing ran in the engine, Wine or a display. No runner script ran. No git command. No existing file was edited;
  the real kj1 run folder was only read (the selftest hashes all 24 of its files before and after and compares: unchanged).
- kill5 has run only in the Lua 5.1 stub. The stub's delayed kill (`KILL_LAG`) is modelled on ONE observation in kj1 (M:
  alive 1 at 0.0008 in snap 963, alive 0 at -1.0000 in 964, KWHO logged in the call frame after the snapshot's W line). I did
  not read the engine source for when `kill()` takes effect.
- The new checker has read real kept bytes only from kj1 (kill4). KCONF, KNOCONF and the `stale` word have never been read
  from engine bytes; in the `real_kj1_kill5like` fixture the two KCONF lines are typed in by me.
- The runner that kj2 needs does not exist (see "Brief points that turned out wrong or incomplete").
- No CodeRabbit review was run: the brief forbids git-changing commands and these are untracked new files; I did not
  want to send code to an outside service from a desk-only brief. Worth doing before registration.

## The reader, line by line (harness/zz_p1_puppet_kill5.script vs kill4; `diff` gives exactly these hunks)
1. Header comment: a 7-line kill5 paragraph (what amendment 138 changes).
2. K-table comment: two new fields, `kt` (time of the online kill call) and `kn` (KNOCONF logged).
3. New constant `K_CONF_MS = 2000` (the 2.0 s of point 4).
4. New function `k_dead_now(id, alv)` before `export_b1`: the id reads dead if its o line in this snapshot read
   `alive()` false; if not listed, the game object's `alive()`; if no game object, the server object's `alive()` (an
   unreadable one counts as NOT dead); no object at all = dead.
5. `export_b1`: a table `alv`; each o line's `o:alive()` is read ONCE into `alv[id]` and that value is written in the o line
   (kill4 called `o:alive()` inline). The k answers use the same value, so a dead line beside alive 1 cannot be built.
6. `export_b1`, the k answers (replaces kill4's two lines):
   - a pending online kill (`kt` set, `dd` not): if it reads dead now, `dd = now` and `KCONF,<now>,<id>,<now - kt>`; else if
     2.0 s have passed and `kn` is not set: `kn = true`, `kdead_fail + 1`, `KNOCONF,<now>,<id>,<now - kt>`;
   - `st` (stale) -> `k,<id>,<ap>,stale` (kill4: dead);
   - confirmed within the 10 s hold AND reads dead now -> `k,<id>,<ap>,dead`;
   - otherwise, if something was applied or a kill call is pending, and no hold has run out -> the plain `k,<id>,<ap>`.
7. `k_line`: after the "dead here already: keep confirming" line, `if r.kt then return end`: a pending kill makes no second
   call and applies no part hit (and is never logged `KDEAD,…,seen` when it then reads dead).
8. `k_line`, online death: `o:kill(a); r.kt = now` (kill4: `r.dd = now`). KDEAD online and kdead_online at the call, as kill4.
9. `k_line`, offline death that reads dead at once: adds `KCONF,<now>,<id>,0` after the KDEAD offline line.
T2SUM: unchanged, the same 19 keys in the same order. New meta lines: KCONF, KNOCONF.

## Decisions I made that the brief did not specify
- Log name `KNOCONF,<t>,<id>,<ms since call>` (pairs with KCONF). Not a second `KDEAD,…,failed` line: that would put two
  KDEAD lines on one id, and checks 7/10 and the ITR2 side count KDEAD lines per id.
- After KNOCONF the kill stays pending: if the creature reads dead later, it IS confirmed (KCONF with ms > 2000, dead line).
  "Not confirmed" read as "not confirmed at that time". Reason: the dead line now means "reads dead", so writing it when true
  is right, and ITR2 otherwise waits forever. kdead_fail stays counted. No retry (kill4 never retried either).
- Confirmation happens only while a snapshot is built (every 100 ms), never in `k_line`: one place, the same alive() read.
- Offline kills: KCONF with ms 0 at the call (the existing code already reads `se:alive()` false there).
- KCONF only for kills of mine (online, offline). `seen` and `released` deaths log no KCONF.
- A pending kill with applied total 0 writes the plain line `k,<id>,0.000` (point 1 says the plain line; kill4 wrote no
  line for applied 0).
- A confirmed death that reads alive again INSIDE the 10 s hold: plain line (not dead, not stale). The format does not say;
  stale is defined only for "more than 10 s". Pinned by the test `no_dead_beside_alive`.
- The "keep confirming" refresh of kill4 (each new pose that still carries the id, while it reads dead, moves the hold's
  start to that pose) is kept. So the hold runs from the confirmation, or from the last pose that still carried the line,
  whichever is later. ITR2 drops the line when it reads the dead line, so in the run it is the confirmation.
- `ktab.txt` (crossing table) unchanged: `kt` is not saved. A kill pending at a level crossing is forgotten; on the new
  level the next dead pose would call kill again. Not in kj2 (no crossing).
- Checker: KCONF/KNOCONF judged as two rows `14.log.KCONF`, `14.log.KNOCONF` of check 14's one table (not T2SUM keys; a T2SUM
  key without an entry still FAILS, as kj1). KCONF row: exactly 2, one per KDEAD online id, after it, ms = time - KDEAD time,
  0 <= ms <= 2000. Check 9 does not look at KCONF/KNOCONF (unchanged from kj1).
- Checker: NV3's exemption is limited to KDEAD **online** (not offline/failed), to the FIRST unexplained fall at or after
  the KDEAD, and needs the later snapshot to read alive 0 at health <= 0. It skips falls already explained by a K line
  (found by the fixture `m10_kdead_before`, where the first fall after a moved KDEAD was the KAPPLY's own fall).
- Checker: check 15 judges every id, not only S and M, and is always judged (in no NV rule's untested set), like 1, 12, 13.
- Check 11 kept at 2.0 s, not tightened.
- Fixture `m11_alive` now fails 11 AND 15 (the snapshot 5 s after the dead line still holds it); I kept it and added
  `m11_alive_late` (12 s, past the hold) which fails 11 alone.
- Selftest checker mutants are run on listed fixtures only (not all 123), to keep the run short.
- The fixture simulation: a kill takes effect at the END of the call frame (after the snapshot), KWHO logged then, as in
  kj1's meta order (KWHO after W). Knob `kill_short` (M 1 thousandth by default) keeps M alive at about 0.001 in the kill
  frame, like kj1's M at 0.0008.

## Brief points that turned out wrong or incomplete
- The runner: `tools/t2_xray_kj.sh` line 54 installs `harness/zz_p1_puppet_kill4.script` by fixed path. kj2 cannot run
  kill5 with it. Needs a new runner (a copy with that one line changed) or an edit; neither was in my create list. The draft
  says so in its limits and pins.
- "first frame after CONT 1 ms": the snapshot read 1 ms after CONT (seq 962) has t_xray 129233, which is RESUMED_GAP's
  "last frame before the gap" (field 6). It is the frame the SIGSTOP cut, finishing its write after CONT, not a new frame.
  The first new frame's snapshot (963, t_xray 149258) was read 26 ms after CONT. So "dead line 25 ms after it" is 25 ms
  after that cut frame's snapshot. Nothing in kj1's scoring depends on it (10 counts from the first kept snapshot), but the
  margin table now says 0.026 s for the first new frame.
- kj1's S did NOT read alive at the call: its last part hit took it to 0.0000, and the engine's alive() is health > 0, so S
  read alive 0 in the call frame's snapshot (804). Under kill5 S's dead line stays in 804 (11 ms). Only M shows the
  one-snapshot delay. The S NV3 fall (0.0000 -> -1.0000) is still a death step the exemption covers.
- The kj1 mutant "writes dead straight after the kill call" is not one patch in kill5: kill5 gates the dead word on "reads
  dead" AND sets the death time only on reading dead. The faithful kj1 mutant needs both undone (`m_kj1_dead_at_call`, two
  patches; the stub's mutant format now takes a list of patches). Undoing only the first (`m_dd_at_call`) gives no dead line
  beside alive 1 but loses KCONF and starts the hold at the call; it is caught by `hold_from_confirmation`.
- kj1's real meta file is LF, not CRLF (the kj1 fixture maker's comment says the real meta file is CRLF; that was kw1's).
  Every reader still strips CR.
- 9's lower edge at CONT (RESUMED_GAP whole second vs CONT - 1.1 s) had 303 ms to spare in kj1, as thin as the STOP side;
  kj1's margin table flagged only the STOP side.

## Old tests changed (only where amendment 138 requires; each listed)
- `stale_section` (kj2/kill5h_stub.lua): `k,101,0.000,dead` -> `k,101,0.000,stale`; `k,102,0.300,dead` -> `k,102,0.300,stale`
  (point 2). Its other expectations unchanged. Running kj1's own stub UNMODIFIED against kill5 fails exactly this check.
- Mutant anchors (the patch text, not what is tested): `m_death_online` now patches `o:kill(a); r.kt = now;` (kill4 had
  `r.dd = now`); `m_snap_hold` patches kill5's dead-word line `elseif r.dd and now - r.dd <= K_HOLD and dn then`. Same
  check, same mutation (no kill call; a 3x hold).
- Checker fixtures: `m10_kdead_before` now expects failing {10, 14} (14.log.KCONF: the moved KDEAD breaks KCONF's ms);
  `m11_alive` expects {11, 15}. `m_kill_never` (new) expects {8, 10, 11, 14}.
- The fixture simulation (kj1's stated design) now follows kill5: the kill takes effect at the end of the call frame,
  KCONF/KNOCONF are written, dead lines only beside alive 0. kj1's fixture expectations were kept everywhere else and pass.

## What I could not verify
- When a real `kill()` takes effect (only kj1's one frame); whether it can stay pending for longer, or never (the KNOCONF path).
- That `level.object_by_id` / `alife():object` return nil for a released id in the real game (the no-object branch).
- Whether `se:alive()` reads correctly for an offline creature right after `se:kill()` (as kill4 assumed).
- Whether itrzone's mod treats a plain line after a dead pose correctly (their half of amendment 138).
- The checker on engine-written KCONF/KNOCONF/stale lines.
- Runner for kj2 (not written).

## Results (2026-11-26)
- `bash kj2/kill5h_test.sh`: `== checks: 42 pass, 0 fail; mutants: 55 caught, 0 not caught or missing` / `RESULT: PASS`.
- `bash kj2/t3_kj2_selftest.sh`: `SELFTEST: PASS (123 folders: good 5/5; mutant 45/45; nv 15/15; counter 24/24; nvfail 2/2;
  missing 7/7; empty 7/7; unreadable 7/7; unparsed 8/8; real 3/3; checker-mutant 4/4; counters-only 4/4; refuse-overwrite ok;
  old checker on the real copy NO VERDICT via NV3 (ok); real folder unchanged ok; reports in .../selftest-runs/...)`.
- Earlier failed selftest and test run folders are kept under kj2/selftest-runs and kj2/runs (nothing deleted).
