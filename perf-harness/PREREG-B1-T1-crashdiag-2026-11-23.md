# B1-T1 crash diagnosis: X-Ray-only series, 3 with the stall and 3 without (PREREG, 2026-11-23; nothing run)
Overseer ruling (after the T1 retry): T1 FAILED as registered, and the X-Ray crash is now the blocker. Desk trace first.
Then the box is approved for X-Ray-only runs (no ITR2) on the same track and save: 3 with the 20 s SIGSTOP and 3
without, interleaved, with spawn-section logging on. It hit 1 in 4 so far, so 6 runs give at least a rough rate.
If none of the 6 crashes, the crash is recorded as rare and unexplained, and T2 goes ahead with a crash-restart path for
X-Ray in the bridge. Unscored as a gate; registered first.

## 1. Desk trace (done; an Opus sub-agent read the code, and I checked the key claims myself)
- The section name was NOT empty. The log line's argument is the single byte 0x01 (`od -c` of xray.log:9214 shows
  `' 001 '`). A null or empty name would have failed earlier, at R_ASSERT2(S && strlen(S)) (Xr_ini.cpp:1604), with
  another message. A control byte as a section name is what a read past the end of an array of names gives.
- No Lua traceback was printed: the fatal handler prints the main Lua stack (xrDebugNew.cpp, CScriptStorage::print_stack)
  and the line after "stack trace:" is empty. So the fatal most likely came from C++, not a Lua ini call (sub-agent's
  reading; not checked by me).
- TOP CANDIDATE (code checked by me): CWeapon::net_Spawn (Weapon.cpp:1120-1130) takes m_ammoType = E->ammo_type from the
  spawn packet and calls m_DefaultCartridge.Load(m_ammoTypes[m_ammoType]). There is NO bounds check; the guard catches
  only a null c_str(). An index past the end of ammo_class reads a garbage name, and CCartridge::Load -> r_float ->
  r_section then gives the fatal.
  SECOND (checked): CWeaponMagazinedWGrenade::net_Spawn (WeaponMagazinedWGrenade.cpp:87-89) does the same with
  grenades_type, with no check at all.
  The timing fits: the fatal came right after a stalker switched online (alife_switch_manager.cpp:65 prints that
  [LSS] line), and the stalker's weapons go online with it.
  Why per run (inference): GAMMA randomises NPC loadouts and loot. A respawned squad member reuses an object id, so the
  same name can carry a new random loadout in each run.
- Less likely: a Lua ini read (no Lua stack), or the SIGSTOP (no evidence either way).
- LATENT BUG for BUGFIXES.md, not fixed: the missing bounds checks. A fix (clamp to 0 with a log line) changes what a
  player sees in a rare case, so it goes to Caden via the Overseer under Rule Two.

## 2. Logging for the series
- Diagnostic exe B1Diag14.exe = RefDet14's own commit (54a338e15, perf/equivalence-harness) plus two log lines only
  (perf/diag-ammo d6991f73e): before the index is used, an out-of-range ammo_type or grenades_type logs
  "! [B1DIAG] ... index >= size: name, section, id, parent", then flushes the log. Behaviour is unchanged: the
  out-of-range read still happens, exactly as in RefDet14.
- The exe's matching .pdb (from its CI run) sits beside it, so a crash prints a C++ stack. T1's crash had none: the log
  said SymType '-exported-' because the symbols were missing.

## 3. Runs
- perf-harness/diag_series.sh: one EXCLUSIVE slice via WANT. 6 runs in the order S N N S S N (S = 20 s SIGSTOP at
  track 600 s, N = none). Each run is tools/t1_xray.sh exactly as in T1 (same track, hash 213ff8f1; save cordon1;
  -b1_snap; watcher), with T1_EXE=B1Diag14.exe and, for N runs, T1_STALL_AT=99999. No ITR2.
- A crash is the event being counted, so it does NOT stop the series. A refusal, a protected title, Roblox or an
  unsettled box does stop it. About 20 min per run, about 2 h in all (one slice, under 3 h).
- t1_xray.sh gains only T1_EXE (default RefDet14.exe), so its hash changes. ITRZone is told, for its later re-freeze.

## 4. What is recorded (per run, then per arm)
- crashed (FATAL ERROR in the run's xray.log) yes/no; if yes: the fatal text, the track time of the last snapshot, the
  C++ stack if printed, and the [LSS] lines just before it;
- every [B1DIAG] line (weapon name, section, index, size, parent id), crash or not;
- whether a B1DIAG line came just before the fatal (that names the cause).
Reading (not a gate):
- B1DIAG before a fatal: cause found (that weapon's index).
- B1DIAG lines but no crash: the bad index exists, but reading past the end does not always hit a garbage name.
- A fatal without B1DIAG: the weapon candidate is wrong, and the C++ stack is the next lead.
- Crashes only in S runs: points at the stall. In both arms: the stall is not needed.
- 0 crashes and 0 B1DIAG in 6: rare and unexplained (Overseer), and T2 adds a crash-restart path.
PREDICTIONS:
- at least one crash in 6: 60 % (1 in 4 so far, but small numbers);
- at least one B1DIAG line in 6: 55 %;
- if there is a crash, a B1DIAG line just before it: 70 %;
- crashes in S runs only, given at least 2 crashes: 20 %.
