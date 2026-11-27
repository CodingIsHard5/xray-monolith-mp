-- kj3 review pass 2 (2026-11-26): checks em_notready_tf (a bad time factor while the managers are nil counts eerr) and
-- ktab_legacy_kx (a 9-field row with kx answers stale until the first processed pose); mutants m_em_nr_anyerr, m_legacy_nm;
-- m_em_nr_always re-anchored on the new not-ready line (same mutation).
-- kj3 Stage 3 (stale rows, 2026-11-26; kj3/NOTES.md "Stage 3"): 6 new checks st3_* plus seven kj3 checks rewritten for the
-- agreed rules (a)-(c) (pend_st_only, ident_omitted_pose, ident_no_pose, ident_reuse_alive_other, ident_sticky, cross_kx; and
-- cross_pending's ktab row, which gains the nm field); 13 new mutants; m_ks_none, m_ident_cleared, m_cross_nopend(_rel)
-- re-anchored, m_ident_never now guards st3_a_named. Review of Stage 2: checks em_notready, em_force_cross with 5 mutants.
-- kj3 Stage 2 (emission lines, 2026-11-26; kj3/NOTES.md "Stage 2"): fake surge / psi managers, game clock and time factor
-- (section "kj3 Stage 2: fake ..."), 19 new checks em_* with 31 mutants m_em_*, mode `examples` (writes the fixtures of
-- kj3/emission-line-examples.txt), and two kj3 checks updated for the new layout and T2SUM key: snapshot_lines and
-- r_line_presence (e and ns now sit between r and k), v3_unchanged and ktab_savefail (eerr=0 is the new last T2SUM key).
-- kj3 copy (kill6, review fixes 1-3 after kj2; see kj3/NOTES.md): the kj2 stub pointed at harness/zz_p1_puppet_kill7.script,
-- with 14 new checks and 16 new mutants (section "kj3 checks"), and three kj2 mutants re-anchored on kill6's changed lines
-- (m_conf_never, m_hold_from_call, m_conf_on_stale: same mutations). Everything else as kj2.
-- kj2 copy (kill5, joint amendment 138): the kj1 stub (kj1/kill4h_stub.lua) pointed at harness/zz_p1_puppet_kill5.script, with
-- (1) a fake kill that can take effect LATER (KILL_LAG: 0 = at the call, as the kj1 stub; > 0 = at the start of the first
-- frame at least that many ms after the call, health -1 as kj1's real engine showed, snap 964; false = never), (2) eight new
-- checks with their mutants, (3) a mutant may be a list of patches (all must apply once), (4) stale_section expects the
-- word `stale` (amendment 138 point 2) and m_death_online / m_snap_hold anchor on kill5's text. Everything else as kj1.
-- Fix round 2 (2026-11-26, CodeRabbit pass 1): checks pending_stale_noconf and pending_released with their mutants
-- m_conf_on_stale and m_released_pending; m_conf_never and m_hold_from_call anchor on the changed KCONF line (same mutation).
-- Offline test of the pose v4 / kill channel bridge script (harness/zz_p1_puppet_kill5.script).
-- Direct runs need the workspace kill5h_test.sh creates (a workdir with empty subdirectories a, b, c, each with a .keep file).
-- Runs the REAL script under plain Lua 5.1 with faked engine objects (modelled on armD-draft/armD_stub.lua) and checks
-- em1 COPY (em1/em1_stub.lua): kj4/kill7h_stub.lua (de1b6f8b...) plus ONE fixture command `lag <ms>` (sets KILL_LAG so a
-- kill call stays pending, as kj1's real engine showed). Nothing else changed.
-- OUTCOMES: creature health and alive flags, kill / kill_entity calls, the snapshot file, meta log lines, T2SUM counters.
--   lua5.1 kill4_stub.lua run <script> <check> <workdir> [<lost3 script>]   prints "PASS <check>", "FAIL <check>: ..." (an
--                         outcome was wrong) or "ERROR <check>: ..." (a Lua exception or a setup failure: never a catch)
--   lua5.1 kill4_stub.lua fixture <script> <dir> [good|nomonster]          kw1 checker selftest (tools/t3_kw1_selftest.sh):
--                         one world, <dir> as its tmpfs, driven from stdin: tick | hurt <id> <h> | wkill <id> <killer> |
--                         finish; answers "ok <time_global>" per command ("ERROR fixture: ..." and exit 1 on an error)
--   lua5.1 kill4_stub.lua checks                                            check names, one per line
--   lua5.1 kill4_stub.lua mutants                                           "<mutant> <check>" per line
--   lua5.1 kill4_stub.lua mkmutant <mutant> <script in> <script out>        patched copy; exits 1 if the patch did not apply once
-- <workdir> must hold the empty subdirectories a, b, c (made by kill4_test.sh); each world gets one as its tmpfs.
-- Fake engine semantics, from the engine source (wt-simtick/src) where it was read:
--   game object alive() = health > 0 (Entity.h g_Alive); set_health_ex clamps to -0.01..1 (script_game_object4.cpp:451);
--   kill(who) on an object already dead does nothing but log an error (script_game_object_use.cpp:118), modelled as a
--   counted error; alife():kill_entity does NOT change the server object's health (alife_combat_manager.cpp:459), so the
--   fake leaves it alive and only records the call. Hit delivery, GAMMA callbacks and time_global over a level change are
--   NOT modelled from source (see the report).

local real_open = io.open
local MODE = arg[1]

------------------------------------------------------------------------------------------------ mutants
-- Each: name, the check that must catch it, exact text in the script, replacement. Applied with plain find, exactly once.
local MUTANTS = {
	{ "m_total_capped", "total_above_one", 'if not tot then return kref(now, n, id, "total") end', 'if not tot then return kref(now, n, id, "total") end; if tot > 1 then tot = 1 end' },
	{ "m_total_refused", "total_above_one", 'if not tot then return kref(now, n, id, "total") end', 'if not tot or tot > 1 then return kref(now, n, id, "total") end' },
	{ "m_v3_unchanged", "v3_unchanged", 'if nc >= 7 then if nc == 7', 'if nc >= 6 then if nc == 7' },
	{ "m_v3_klines", "v3_klines_not_applied", 'if not v4 then for _, l in ipairs(lines)', 'if false then for _, l in ipairs(lines)' },
	{ "m_bare_k", "bare_k_refused", 'if not id then return kref(now, n, string.match(line, "^k,([^,]*)") or "?", "form") end',
		'if not id then local o0 = level.object_by_id(tonumber(string.match(line, "^k,(%d+)$") or "-1")); if o0 and o0:alive() then o0:kill(a) end; return kref(now, n, string.match(line, "^k,([^,]*)") or "?", "form") end' },
	{ "m_bad_version", "bad_version_refused", 'string.sub(first, -2) == ",4"', 'true' },
	{ "m_repeat", "repeated_reads", 'r.ap = tot; kc.khits', 'kc.khits' },
	{ "m_part_hit", "part_hit", 'local hn = hb - d;', 'local hn = hb - tot;' },
	{ "m_stop001", "stop_at_001", 'math.min(hb, 0.01)', 'math.min(hb, 0)' },
	{ "m_death_online", "death_online", 'if dead == "1" then o:kill(a); r.kt = now;', 'if dead == "1" then r.kt = now;' },
	{ "m_already_dead", "death_already_dead", 'if not alive then r.dd = now; m(string.format("KDEAD,%d,%d,%d,seen", now, n, id)); return end', '' },
	{ "m_offline_death", "offline_death", 'pcall(function() se:kill() end)', 'pcall(function() alife():kill_entity(se) end)' },
	{ "m_offline_failed", "offline_death_failed", 'if ok and ok2 and not al then', 'if ok then' },
	{ "m_owed", "owed_hit", 'r.ow = true; kc.kowed', 'r.ow = true; r.ap = tot; kc.kowed' },
	{ "m_back", "total_backwards", 'diff = 0 end', 'r.ap = tot; diff = 0 end' },
	{ "m_id0", "id_zero", 'if id == 0 then return kref(now, n, id, "actor") end', '' },
	{ "m_kind", "non_creature", 'if not (o:is_monster() or o:is_stalker()) then return kref(now, n, id, "kind") end', '' },
	{ "m_over64", "over64", 'local K_HOLD, K_MAX = 10000, 64', 'local K_HOLD, K_MAX = 10000, 99' },
	{ "m_session", "new_session", 'for _, r in pairs(K) do r.ap, r.ow, r.kb = 0, nil, nil end', '' },
	{ "m_stale", "stale_section", 'if sec ~= cur or (r.dd and alive', 'if (r.dd and alive' },
	{ "m_lost", "lost_middle", 'p_lost = true; p_lost_n = p_lost_n + 1', 'p_lost = true; p_lost_n = p_lost_n + 1; K = {}' },
	{ "m_gates", "cross_state_gate", 'and state ~= "cross" and not p_xwait then k_pose', 'then k_pose' },
	{ "m_cross_run", "crossing", 'if run ~= t2_run then', 'if false then' },
	{ "m_cross_nosave", "crossing", '				k_save()\n', '' },
	{ "m_snap_hold", "snapshot_lines", 'elseif r.dd and now - r.dd <= K_HOLD and dn then', 'elseif r.dd and now - r.dd <= 3 * K_HOLD and dn then' },
	{ "m_actor", "actor_health", 'o:hit(h) end)', 'a:hit(h) end)' },
	{ "m_clamp", "clamp", 'if d > 1 then d = 1;', 'if false then d = 1;' },
	{ "m_r_absent", "r_line_presence", 'if t2 and k_r then L[#L + 1] = "r," .. k_r end', '' },
	{ "m_r_once", "r_advances", 'k_r = string.format("%s,%d", tostring(sess or 0), n)', 'k_r = k_r or string.format("%s,%d", tostring(sess or 0), n)' },
	{ "m_r_zone", "r_stands_still", 'if zone ~= level.name() then m(', 'if zone ~= level.name() then k_r = tostring(p_sess or 0) .. "," .. n; m(' },
	{ "m_r_kver", "r_stands_still", 'if kbad then f:close()', 'if kbad then f:close(); k_r = "0," .. n' },
	{ "m_lethal_kill", "death_lethal_total", 'o:kill(a)', 'if o.health > 0 then o:kill(a) end' },
	{ "m_kwho_killer", "kill_who", 'who and who:id() or -1', '-1' },
	{ "m_kwho_gate", "kill_who", 'if not ok or not K[id] or kwho[id] then return end', 'if not ok or kwho[id] then return end' },
	{ "m_kwho_once", "kill_who", 'kwho[id] = true;', '' },
	{ "m_over64_r", "over64_r", 'then kref(now, n, "-", "over64")', 'then return kref(now, n, "-", "over64")' },
	{ "m_ktab_decimals", "ktab_decimals", 'r,%d,%.9f,', 'r,%d,%.3f,' },
	{ "m_ktab_openfail", "ktab_savefail", 'if not f then return k_savefail() end', 'if not f then return end' },
	{ "m_ktab_writefail", "ktab_savefail", 'if not ok then return k_savefail() end', '' },
	{ "m_r_session", "r_new_session", 'k_r = string.format("%s,%d", tostring(sess or 0), n)', 'k_r = string.format("%s,%d", k_r and string.match(k_r, "^(%d+)") or tostring(sess or 0), n)' },
	-- kj2 (kill5, amendment 138). A mutant whose 3rd field is a table applies every {old, new} pair in it (each exactly once).
	-- the kj1 reader's behaviour, rebuilt: death taken at the call AND the dead word without the "reads dead" test
	{ "m_kj1_dead_at_call", "dead_waits_for_effect", {
		{ 'o:kill(a); r.kt = now;', 'o:kill(a); r.kt = now; r.dd = now;' },
		{ 'elseif r.dd and now - r.dd <= K_HOLD and dn then', 'elseif r.dd and now - r.dd <= K_HOLD then' } } },
	{ "m_pending_noplain", "dead_waits_for_effect", '(r.ap > 0 or r.kt) and', '(r.ap > 0 and not r.kt) and' },
	{ "m_conf_never", "dead_next_frame", 'if dn then r.dd = now; m(', 'if false then r.dd = now; m(' },   -- kj3: re-anchored
	{ "m_kconf_ms", "dead_next_frame", 'm(string.format("KCONF,%d,%d,%d", now, id, now - r.kt))', 'm(string.format("KCONF,%d,%d,%d", now, id, 0))' },
	{ "m_dead_word_nodn", "no_dead_beside_alive", 'elseif r.dd and now - r.dd <= K_HOLD and dn then', 'elseif r.dd and now - r.dd <= K_HOLD then' },
	{ "m_noconf_never", "kill_never_effect", 'kc.kdead_fail = kc.kdead_fail + 1; m(string.format("KNOCONF', 'm(string.format("KNOCONF' },
	{ "m_noconf_early", "kill_never_effect", 'local K_CONF_MS = 2000', 'local K_CONF_MS = 500' },
	{ "m_retry", "kill_never_effect", 'if r.kt then return end', 'if r.kt and false then return end' },
	{ "m_stale_as_dead", "stale_word", '"k,%d,%.3f,stale"', '"k,%d,%.3f,dead"' },
	{ "m_stale_from_call", "stale_word", '(r.dd and alive and now - r.dd > K_HOLD)', '(r.dd and alive and now - (r.kt or r.dd) > K_HOLD)' },
	{ "m_noobj_alive", "no_object_dead", 'local se = alife():object(id); if not se then return true end', 'local se = alife():object(id); if not se then return false end' },
	{ "m_noobj_stale", "no_object_dead", 'if not r.dd then m(string.format("KDEAD,%d,%d,%d,released", now, n, id)) end; r.dd = now; return end',
		'if not r.dd then m(string.format("KDEAD,%d,%d,%d,released", now, n, id)) end; r.dd = now; r.st = true; return end' },
	{ "m_hold_from_call", "hold_from_confirmation", 'if dn then r.dd = now; m(', 'if dn then r.dd = r.kt; m(' },   -- kj3: re-anchored
	{ "m_dd_at_call", "hold_from_confirmation", 'o:kill(a); r.kt = now;', 'o:kill(a); r.kt = now; r.dd = now;' },
	{ "m_r_waits", "r_at_processing", 'k_r = string.format("%s,%d", tostring(sess or 0), n)',
		'local pend = false; for _, x in pairs(K) do if x.kt and not x.dd then pend = true end end; if not pend then k_r = string.format("%s,%d", tostring(sess or 0), n) end' },
	-- kj2 fix round 2 (CodeRabbit pass 1, kj2/coderabbit-triage-2026-11-26.md items 1 and 2): each undoes one fix
	{ "m_conf_on_stale", "pending_stale_noconf", 'if r.kt and not r.dd and not stl then', 'if r.kt and not r.dd then' },   -- kj3: re-anchored (kill5's guard back)
	{ "m_released_pending", "pending_released", '		if r.kt and not r.dd then return end   -- kill5: a pending kill call', '		-- (mutant) a pending kill call' },
	-- kj3 (kill6, review fixes; kj3/NOTES.md). Each undoes one fix, or a part of one.
	{ "m_fail_on_stale", "pend_stale_nofail", 'if r.kt and not r.dd and not stl then', 'if r.kt and not r.dd then' },          -- fix 1 and 2 guard gone (kill5)
	{ "m_st_guard", "pend_st_only", 'if r.kt and not r.dd and not stl then', 'if r.kt and not r.dd and not r.kx then' },     -- fix 1 only
	{ "m_ident_never", "st3_a_named", 'if r.ks and cs and cs ~= r.ks then', 'if false then' },                        -- fix 2: no identity check (stage 3: was ident_omitted_pose, which the pose's own mark now covers)
	{ "m_ident_never_np", "ident_no_pose", 'if r.ks and cs and cs ~= r.ks then', 'if false then' },
	{ "m_ks_none", "ident_no_pose", '	r.ks = cur   -- stage 3: the section', '	-- (mutant) the section' },   -- stage 3: re-anchored (ks now recorded at every line taken)                                              -- fix 2: identity not recorded
	{ "m_ident_never_alive", "ident_reuse_alive_other", 'if r.ks and cs and cs ~= r.ks then', 'if false then' },
	{ "m_ident_cleared", "ident_sticky", 'for _, r in pairs(K) do r.was, r.st, r.nm = r.st, false, nil end', 'for _, r in pairs(K) do r.was, r.st, r.nm, r.kx = r.st, false, nil, nil end' },   -- stage 3: re-anchored
	{ "m_ident_absent_stale", "ident_removed_dead", { { 'r.ks and cs and cs ~= r.ks', 'r.ks and cs ~= r.ks' }, { 'id, r.ks, cs))', 'id, r.ks, tostring(cs)))' } } },                  -- no object taken as a mismatch
	{ "m_ident_any", "ident_reuse_same_section", 'r.ks and cs and cs ~= r.ks', 'r.ks and cs' },                             -- any object taken as a mismatch
	{ "m_cross_nopend", "cross_pending", '"r,%d,%.9f,%s,%d,%d,%s,%d,%s,%d,%d\\n"', '"r,%d,%.9f,%s,%d,%d\\n"' },   -- stage 3: re-anchored (nm field)                  -- fix 3: kill5's row
	{ "m_cross_kt_late", "cross_pending", 'r.kt = tonumber(kt);', 'r.kt = time_global();' },                                -- fix 3: age restarted at the resume
	{ "m_cross_nopend_rel", "cross_pending_released", '"r,%d,%.9f,%s,%d,%d,%s,%d,%s,%d,%d\\n"', '"r,%d,%.9f,%s,%d,%d\\n"' },
	{ "m_cross_kn", "cross_kn_kept", 'r.kn = kn == "1" or nil;', '' },
	{ "m_cross_ks", "cross_ident", 'r.ks = ks ~= "-" and ks or nil;', '' },
	{ "m_cross_kx", "cross_kx", 'r.kx = kx == "1" or nil', 'r.kx = nil' },
	{ "m_old_format", "ktab_old_format", '([01]),([01])([^\\n]*)")', '([01]),([01])(,[^\\n]+)")' },
	-- kj3 Stage 3 (stale rows (a)-(c); kj3/NOTES.md "Stage 3"). Each undoes one rule, or a part of one.
	{ "m_kx_st_only", "st3_a_named", 'if r.st or (r.kx and r.nm) then', 'if r.st then' },                                     -- (a): identity stale not written while named
	{ "m_kx_unnamed", "ident_no_pose", 'if r.st or (r.kx and r.nm) then', 'if r.st or r.kx then' },                           -- (a): identity stale written although no line names it
	{ "m_fresh_none", "st3_b_fresh", 'if sec == cur and r.ks and r.ks ~= cur then', 'if false then' },                       -- (b) gone
	{ "m_fresh_keep_ap", "st3_b_fresh", 'r = { ap = 0, nm = true }', 'r = { ap = r.ap, nm = true }' },                      -- (b): applied not restarted
	{ "m_fresh_keep_kt", "st3_b_fresh", 'r = { ap = 0, nm = true }', 'r = { ap = 0, nm = true, kt = r.kt }' },              -- (b): pending kill kept
	{ "m_fresh_any", "st3_same_back", 'if sec == cur and r.ks and r.ks ~= cur then', 'if sec == cur and r.ks and r.was then' },   -- (b) on any row stale last pose
	{ "m_drop_never", "ident_omitted_pose", 'if not r.nm and (r.was or r.kx) then', 'if false then' },                     -- (c) gone (stage 1)
	{ "m_drop_never_c", "st3_c_drop", 'if not r.nm and (r.was or r.kx) then', 'if false then' },
	{ "m_drop_over64", "st3_c_drop", { { 'if #lines > K_MAX then kref(now, n, "-", "over64")   -- every', 'if #lines > K_MAX then kref(now, n, "-", "over64"); lines = {} end; do   -- every' }, { '\telse   -- (stage 3: an over-64', '\t   -- (stage 3: an over-64' } } },   -- an over-64 pose drops like an empty one
	{ "m_drop_live", "st3_live_kept", 'if not r.nm and (r.was or r.kx) then', 'if not r.nm then' },                        -- the edge: live rows dropped too
	{ "m_drop_kwho", "st3_partner", 'K[id] = nil; kwho[id] = nil', 'K[id] = nil' },                                         -- KWHO not reopened for the next creature
	{ "m_partner_perm", "st3_partner", 'if not r.nm and (r.was or r.kx) then', 'if false then' },                         -- the partner's case under no drop
	{ "m_cross_nm", "cross_ident", 'r.nm = nm == "1" or nil end', 'end' },                                                   -- nm not carried over the crossing
	-- kj3 review of Stage 2 (2026-11-26): not ready, and the forced-start cursor over a crossing
	{ "m_em_nr_count", "em_notready", 'if nr then if not em_nr then', 'if nr then eerr = eerr + 1; if not em_nr then' },   -- not ready counted as an error
	{ "m_em_nr_always", "em_notready", 'local nr = not em_ready and (', 'local nr = (' },                                  -- nil after a good read not counted (pass 2: re-anchored)
	-- kj3 review pass 2 (2026-11-26)
	{ "m_em_nr_anyerr", "em_notready_tf", '(err == "SurgeManager nil" or err == "PsiStormManager nil")', '(surge_manager.SurgeManager == nil or psi_storm_manager.PsiStormManager == nil)' },   -- pass 1's rule: any failure while a manager is nil
	{ "m_legacy_nm", "ktab_legacy_kx", '; nm = kx end   -- kill6 stage 1/2 row', ' end   -- kill6 stage 1/2 row' },                -- a 9-field row with kx loads unnamed
	{ "m_em_carry_none", "em_force_cross", 'if em_carry then em_done = em_carry', 'if false then em_done = em_carry' },
	{ "m_em_carry_nosave", "em_force_cross", 'if em_test then ok = f:write(string.format("e,%d\\n", em_done)) and ok end', '' },
	{ "m_em_carry_anyrun", "em_force_cross", { { '	em_carry = tonumber(string.match(s, "\\ne,(%d+)\\n"))   -- review: only here, after the run id test\n', '' },
		{ 'if run ~= t2_run then', 'em_carry = tonumber(string.match(s, "\\ne,(%d+)\\n")); if run ~= t2_run then' } } },
	-- kj3 Stage 2 (emission lines; kj3/NOTES.md "Stage 2")
	{ "m_em_rk", "em_v4_every", 'if t2 then em_snap(now, L) end', 'if t2 and k_r then em_snap(now, L) end' },
	{ "m_em_v3", "em_absent_v3", 'if t2 then em_snap(now, L) end', 'em_snap(now, L)' },
	{ "m_em_fmt", "em_none_exact", 'string.format("e,%s,%.2f,%d,%s", kind, t, ph, tostring(tf))', 'string.format("e,%s,%.1f,%d,%s", kind, t, ph, tostring(tf))' },
	{ "m_em_tf", "em_none_exact", 'kind, t, ph, tostring(tf))', 'kind, t, ph, "6")' },
	{ "m_em_div", "em_surge_timeline", 't = gt:diffSec(M.inited_time) / tf;', 't = gt:diffSec(M.inited_time) / 6;' },
	{ "m_em_sph_t", "em_surge_timeline", '200, 212, 220', '200, 210, 220' },
	{ "m_em_ceil", "em_surge_bounds", 'PSI_PH, math.ceil(t)) end', 'PSI_PH, math.floor(t)) end' },
	{ "m_em_sph", "em_surge_bounds", '108, 120, 156', '108, 121, 156' },
	{ "m_em_pph", "em_psi_bounds", '{ 0, 10, 12, 30, 78, 103 }', '{ 0, 10, 12, 30, 77, 103 }' },
	{ "m_em_pph_t", "em_psi_timeline", '{ 0, 10, 12, 30, 78, 103 }', '{ 0, 10, 12, 31, 78, 103 }' },
	{ "m_em_wz", "em_waves", '(dgs - wv.inited_time) * 3)', '(dgs - wv.inited_time) * 2)' },
	{ "m_em_wplay", "em_waves", 'if wv and wv.effect:playing() then', 'if wv then' },
	{ "m_em_vhit", "em_vortex", 'if v.hit == false then local p', 'if true then local p' },
	{ "m_em_vs", "em_vortex", 'v.start_time + 20 - t)', 'v.start_time + 18 - t)' },
	{ "m_em_vn", "em_vortex", '"v,%d,%.3f,%.3f,%.3f,%.2f", i,', '"v,%d,%.3f,%.3f,%.3f,%.2f", #L,' },
	{ "m_em_nsrun", "em_ns", 'if M.started then return -1 end', '' },
	{ "m_em_nsfloor", "em_ns", 'local v = math.floor(M._delta', 'local v = math.ceil(M._delta' },
	{ "m_em_nopcall", "em_degraded_nil", 'local ok, err = pcall(em_body, L, tf, now)', 'local ok, err = true, em_body(L, tf, now)' },
	{ "m_em_logmany", "em_degraded_nil", 'if not em_logged then em_logged = true;', 'if true then em_logged = true;' },
	{ "m_em_nocount", "em_degraded_field", 'eerr = eerr + 1', '' },
	{ "m_em_notrunc", "em_degraded_field", 'for i = #L, n0 + 1, -1 do L[i] = nil end', '' },
	{ "m_em_order", "em_order", { { 'if t2 then em_snap(now, L) end   -- Stage 2: e, w, v, ns (after r, before k)', '' }, { 'L[#L + 1] = string.format("end,%d", seq)', 'if t2 then em_snap(now, L) end; L[#L + 1] = string.format("end,%d", seq)' } } },
	{ "m_em_force_always", "em_force_absent", 'if em_test then em_step(now) end', 'em_step(now)' },
	{ "m_em_force_again", "em_force_lines", 'if i > em_done then em_done = i', 'if true then em_done = i' },
	{ "m_em_force_rate", "em_force_lines", 'if now - em_last < 1000 then return end; em_last = now', 'em_last = now' },
	{ "m_em_force_torn", "em_force_lines", '"([^\\n]*)\\n") do l = string.gsub', '"([^\\n]+)") do l = string.gsub' },
	{ "m_em_force_nopcall", "em_force_throw", 'local ok, st = pcall(em_start, l)', 'local ok, st = true, em_start(l)' },
	{ "m_em_phase_rep", "em_meta_once", 'em_ph, em_t = ph, t', 'em_ph, em_t = 0, t' },
	{ "m_em_eend_t", "em_meta_once", 'm(string.format("EEND,%d,%s,%.2f", now, em_kind, em_t))', 'm(string.format("EEND,%d,%s,%.2f", now, em_kind, 0))' },
	{ "m_em_jump", "em_jump", 'for p = em_ph + 1, ph do', 'for p = ph, ph do' },
	{ "m_em_restart", "em_backwards", 'if kind ~= em_kind or (M and t < em_t - 0.5) then', 'if kind ~= em_kind then' },
}

if MODE == "mutants" then for _, mu in ipairs(MUTANTS) do print(mu[1] .. " " .. mu[2]) end; os.exit(0) end
if MODE == "mkmutant" then
	local name, fin, fout = arg[2], arg[3], arg[4]
	local mu; for _, x in ipairs(MUTANTS) do if x[1] == name then mu = x end end
	if not mu then print("NO MUTANT " .. tostring(name)); os.exit(1) end
	local f = real_open(fin, "rb"); if not f then print("NO SCRIPT " .. tostring(fin)); os.exit(1) end
	local s = f:read("*a"); f:close()
	local pairs_ = type(mu[3]) == "table" and mu[3] or { { mu[3], mu[4] } }
	local o = s
	for i, pr in ipairs(pairs_) do
		local a = string.find(o, pr[1], 1, true)
		if not a then print("PATCH NOT FOUND " .. name .. " #" .. i); os.exit(1) end
		if string.find(o, pr[1], a + 1, true) then print("PATCH NOT UNIQUE " .. name .. " #" .. i); os.exit(1) end
		local o2 = o:sub(1, a - 1) .. pr[2] .. o:sub(a + #pr[1])
		if o2 == o then print("PATCH CHANGED NOTHING " .. name .. " #" .. i); os.exit(1) end
		o = o2
	end
	local g = real_open(fout, "wb"); if not g then print("CANNOT WRITE " .. tostring(fout)); os.exit(1) end
	local okw = g:write(o); local okc = g:close()
	if not (okw and okc) then print("CANNOT WRITE " .. tostring(fout)); os.exit(1) end
	print("MUTANT " .. name .. " written"); os.exit(0)
end

------------------------------------------------------------------------------------------------ fake engine
local SHM, NOW, LEVEL, RUN, CL_EXTRA = nil, 0, "l01_escape", "1", ""
local CB, OBJ, ACT, CHANGED
local DEATHQ = {}   -- deaths whose death callback fires at the start of the next frame (the GE_DIE event is handled later)
KILL_LAG = 0        -- kj2: when a kill call takes effect (see o:kill); reset to 0 by new_world
local function V(x, y, z) local v = { x = x or 0, y = y or 0, z = z or 0 }
	function v:set(a, b, c) self.x, self.y, self.z = a, b, c; return self end
	function v:distance_to(o) return math.sqrt((self.x - o.x) ^ 2 + (self.y - o.y) ^ 2 + (self.z - o.z) ^ 2) end
	function v:normalize() local l = math.sqrt(self.x ^ 2 + self.y ^ 2 + self.z ^ 2); if l > 1e-9 then self.x, self.y, self.z = self.x / l, self.y / l, self.z / l end; return self end
	return v end
local KTAB_WFAIL = false   -- ktab_savefail: the ktab.txt handle opens, but every write fails (as a full disk)
EM_OPENS = 0   -- kj3: opens of emtest.txt
io.open = function(p, m) p = p:gsub("^Z:\\dev\\shm\\itrz_b1\\", SHM .. "/"); if p == "p1_track.csv" then p = SHM .. "/p1_track.csv" end
	if p:find("/emtest%.txt$") then EM_OPENS = EM_OPENS + 1 end
	if KTAB_WFAIL and p:find("/ktab%.txt$") and m == "wb" then return { write = function() return nil, "disk full" end, close = function() return true end } end
	return real_open(p, m) end
function time_global() return NOW end
os.time = function() return 1795000000 + math.floor(NOW / 1000) end   -- wall clock follows the fake time: logs are reproducible
function command_line() return (CL_BASE or ("-p1 -t2_pose -t2_run " .. RUN .. " -t2_lost 5000 -t2_lostx 120000 ")) .. CL_EXTRA end   -- kj3: CL_BASE = a non-t2 run
function printf() end
function vector() return V() end
function device() return { pause = function() end, f_time_delta = 0.05, frame = 0 } end
game = { time = function() return NOW end }
clsid = { level_changer_s = 77 }
hit = setmetatable({ fire_wound = 8, wound = 6, strike = 2 }, { __call = function() return {} end })
local GV = { [10] = 1, [20] = 2 }; local LN = { "l01_escape", "l02_garbage" }
function game_graph() return { vertex = function(_, g) return { level_id = function() return GV[g] end } end } end
local CHG = { [5] = { n = "esc_to_gar", g = 10, dg = 20, dp = V(1, 2, 3), dl = 111 }, [6] = { n = "gar_to_esc", g = 20, dg = 10, dp = V(4, 5, 6), dl = 222 } }
function ChangeLevel(pos, lvid, gvid) CHANGED = gvid; error("LEVEL_CHANGE") end
function get_console() return { execute = function() end, get_bool = function() return true end } end
function profile_timer() return { start = function() end, stop = function() end, time = function() return 500 end } end
function getFS() return { update_path = function(_, a, b) return b end } end
function RegisterScriptCallback(n, f) CB[n] = CB[n] or {}; table.insert(CB[n], f) end
local lastprobe = 0
level = { name = function() return LEVEL end,
	vertex_id = function(v) lastprobe = v.y; return 1 end,
	vertex_position = function() return V(0, lastprobe - 1.0, 0) end }

-- kj3 Stage 2: fake surge / psi managers, game clock and time factor. Only the fields the reader uses, set as the real
-- scripts set them (SM = run/gamedata/scripts/surge_manager.script, PSM = psi_storm_manager.script, PVF =
-- pulse_vortex_consistency_fix.script). The 1 s script update (SM:51-57 main_loop `_tmr`) is modelled by em_sim below.
-- Game time: integer game ms, advanced by frame ms * TF (alife_time_manager_inline.h:25); CTime:diffSec in seconds.
GAME_MS, TF, GS_FROZEN, SIM_OFF = 0, 6, false, false
local function CT(ms) local c = { ms = ms }
	function c:diffSec(o) if type(o) ~= "table" or not o.ms then error("diffSec: bad CTime", 0) end; return (self.ms - o.ms) / 1000 end
	return c end
game.get_game_time = function() return CT(GAME_MS) end
level.get_time_factor = function() return TF end
local function fx() local e = { on = true }; function e:playing() return self.on end; return e end
local VPER = { 4, 3, 6, 5 }   -- PVF:137-138: vortex period 3..6 (math.random); deterministic cycle here
local SMF, PMF, EMC, EM_SIM_NEXT, VPI
local function sm_start()   -- SM:191-249 start(true): inited_time = now, started, waves/stages emptied, factor 10
	SMF.inited_time = CT(GAME_MS); SMF.started = true; SMF.finished = false; SMF.blowout_waves = {}; SMF.stages = {}; TF = 10 end
local function pm_start()   -- PSM:72-121 start(true)
	PMF.inited_time = CT(GAME_MS); PMF.started = true; PMF.finished = false; PMF.stages = {}; PMF.vortexes = {}; PMF.next_vortex_at = nil; TF = 10 end
local function new_managers()
	EMC = { surge = 0, psi = 0 }; EM_SIM_NEXT = 0; VPI = 0
	-- SM:160-167 initialize; SM:171 _delta = random(12 h, 24 h) (emission_frequency 24)
	SMF = { started = false, finished = true, inited_time = CT(0), last_surge_time = CT(0), _delta = 50000, surge_time = 222, blowout_waves = {}, stages = {} }
	-- PSM:41-46, PSM:53 (psi_storm_frequency 48), PSM:56 vortex_index = 0
	PMF = { started = false, finished = true, inited_time = CT(0), last_psi_storm_time = CT(0), _delta = 90000, psi_storm_duration = 103, vortexes = {}, vortex_index = 0, stages = {} }
	surge_manager = { SurgeManager = SMF, start_surge = function() EMC.surge = EMC.surge + 1; if EM_START_ERR then error(EM_START_ERR, 0) end; sm_start() end }   -- SM:1673-1675
	-- kill7 (kj3p r1, 2026-11-27): in the engine the two managers are luabind script-class objects. `==` between two
	-- DIFFERENT such objects looks for an __eq operator and raises; `==` on the same object, or against nil, does not
	-- (Lua 5.1 calls __eq only for two different objects that share the metamethod). Plain tables hid this at the desk.
	local function no_eq(x) error("! No such operator [__eq] defined in class [" .. tostring(getmetatable(x).cls) .. "]", 0) end
	setmetatable(SMF, { __eq = no_eq, cls = "CSurgeManager" }); setmetatable(PMF, { __eq = no_eq, cls = "CPsiStormManager" })
	psi_storm_manager = { PsiStormManager = PMF, start_psi_storm = function() EMC.psi = EMC.psi + 1; if EM_START_ERR then error(EM_START_ERR, 0) end; pm_start() end }   -- PSM:713-715
end
local function em_sim()   -- one 1 s script update (SM:571-1100 / PSM:276-393), only the parts behind the read fields
	local ap = db.actor:position()
	if SMF.started then
		local d = math.ceil((GAME_MS - SMF.inited_time.ms) / 1000 / TF)   -- SM:676
		if d >= SMF.surge_time then   -- SM:677 -> end_surge: SM:337 last_surge_time, SM:259 new _delta, SM:388 factor back
			SMF.started = false; SMF.finished = true; SMF.last_surge_time = CT(GAME_MS); SMF._delta = 60000; TF = 6
			for _, wv in pairs(SMF.blowout_waves) do wv.effect.on = false end; return end
		local dgs = math.ceil((GAME_MS - SMF.inited_time.ms) / 1000 / 2)   -- SM:688
		for _, n in ipairs({ 1, 2 }) do local key = n == 1 and "1stwave" or "2ndwave"
			if d >= (n == 1 and 120 or 168) and SMF.stages[key] == nil then   -- SM:870-873 / SM:894-897 -> start_wave SM:490-498
				SMF.blowout_waves[n] = { effect = fx(), inited_time = dgs, inited_pos = V(ap.x, ap.y / 2, ap.z + 250) }; SMF.stages[key] = true end end
		for _, wv in pairs(SMF.blowout_waves) do if wv.effect.on and wv.inited_pos.z - (dgs - wv.inited_time) * 3 < ap.z - 250 then wv.effect.on = false end end   -- SM:1065,1072-1073
	end
	if PMF.started then
		if SMF.started then PMF.started = false; PMF.finished = true; PMF.last_psi_storm_time = CT(GAME_MS); return end   -- PSM:313-314 skip
		local d = math.ceil((GAME_MS - PMF.inited_time.ms) / 1000 / TF)   -- PSM:317
		if d >= PMF.psi_storm_duration then PMF.started = false; PMF.finished = true; PMF.last_psi_storm_time = CT(GAME_MS); PMF._delta = 100000; TF = 6; return end   -- PSM:318-319
		if d >= 30 and d <= 77 then   -- PSM:357-365
			if PMF.next_vortex_at == nil then VPI = VPI % 4 + 1; PMF.next_vortex_at = d + VPER[VPI] end
			if PMF.next_vortex_at == d then   -- PSM:400-412 vortex(): index + 1, particle at actor + offset, y + 20
				PMF.vortex_index = PMF.vortex_index + 1; local i = PMF.vortex_index
				PMF.vortexes[i] = { particle_pos = V(ap.x + 10 * i, ap.y + 20, ap.z - 7 * i), sound_pos = V(ap.x + 10 * i, ap.y + 60, ap.z - 7 * i), start_time = d, hit = false }
				VPI = VPI % 4 + 1; PMF.next_vortex_at = d + VPER[VPI] end
		end
		for _, v in pairs(PMF.vortexes) do if d - v.start_time >= 20 and v.hit == false then v.hit = true end end   -- PSM:367-383
	end
end

-- creatures: OBJ[id] = { sec, kind (m|s|x), lvl, online, health, dtime (death time), killer, hits = {}, kill_err, offkill }
local function add(id, sec, kind, o) o = o or {}
	OBJ[id] = { sec = sec, kind = kind, lvl = o.lvl or LEVEL, online = o.online ~= false, health = o.health or 1, hits = {}, kill_err = 0, offkill = 0, sekill = 0, sets = 0, pos = o.pos }
	return OBJ[id] end
local GO_MT = { __index = function(t, k) if k == "health" then return t._r.health end end }
local function go(id, R)
	local o = setmetatable({ _r = R }, GO_MT)
	function o:id() return id end
	function o:section() return R.sec end
	function o:is_monster() return R.kind == "m" end
	function o:is_stalker() return R.kind == "s" end
	function o:alive() return R.health > 0 end
	function o:position() local p = R.pos or V(10, 0, 10); return V(p.x, p.y, p.z) end
	function o:direction() return V(0, 0, 1) end
	function o:best_enemy() return nil end
	function o:set_health_ex(h) R.sets = R.sets + 1; R.health = math.max(-0.01, math.min(1, h)) end
	function o:hit(h) R.hits[#R.hits + 1] = { who = h.draftsman and h.draftsman:id() or -1, power = h.power, type = h.type } end
	-- Kill refuses only an object already killed (AlreadyDie = death time set, Entity.h:104), not one at health 0
	-- kj2: KILL_LAG 0 = in effect at the call (kj1 stub); > 0 = in effect at the start of the first frame >= call + KILL_LAG
	-- (health -1, as kj1's real engine read one snapshot later); false = never in effect (dtime set, health untouched)
	function o:kill(who) if R.dtime then R.kill_err = R.kill_err + 1 else R.dtime = NOW; R.killer = who and who:id() or -1; R.kills = (R.kills or 0) + 1
		if KILL_LAG == 0 then R.health = 0; DEATHQ[#DEATHQ + 1] = { id = id, who = who }
		elseif KILL_LAG then R.kill_at = NOW + KILL_LAG; R.kill_who = who end end end
	return o
end
local function online(R) return R.online and R.lvl == LEVEL and not R.released end
local function new_actor()
	local apos = V(0, 0, 0)
	ACT = { health = 1, hits = 0, sets = 0, minh = 1, killed = 0, nset = 0 }
	db = { storage = {}, actor = { health = 1, radiation = 0, bleeding = 0 } }
	local a = db.actor
	function a:id() return 0 end
	function a:position() return apos end
	function a:direction() return V(0, 0, 1) end
	function a:alive() return true end
	function a:is_monster() return false end
	function a:is_stalker() return false end
	function a:section() return "actor" end
	function a:set_health_ex(h) ACT.sets = ACT.sets + 1; self.health = math.max(-0.01, math.min(1, h)) end
	function a:hit(h) ACT.hits = ACT.hits + 1; self.health = self.health - (h.power or 0) end
	function a:kill() ACT.killed = ACT.killed + 1 end
	function a:set_actor_position(v) ACT.nset = ACT.nset + 1; apos = V(v.x, v.y, v.z) end
	function a:set_actor_direction() end
	function a:change_radiation() end
	function a:level_vertex_id() return 1 end
	function a:game_vertex_id() return 10 end
end
level.object_by_id = function(id)
	if id == 0 then return db.actor end
	local R = OBJ[id]; if R and online(R) then return go(id, R) end
end
function alife() return {
	switch_distance = function() return 450 end, object_count = function() return 0 end,
	level_name = function(_, i) return LN[i] end,
	object = function(_, id)
		local R = OBJ[id]
		if R then if R.released then return nil end
			return { id = id, online = online(R), section_name = function() return R.sec end, alive = function() return R.health > 0 end,
				clsid = function() return 1 end, _r = R,
				kill = function() R.sekill = R.sekill + 1; if not R.killfail then R.health = 0 end end } end   -- CSE_ALifeMonsterAbstract::kill: health 0
		local c = CHG[id]; if not c then return nil end
		return { clsid = function() return 77 end, name = function() return c.n end, section_name = function() return "level_changer" end,
			m_game_vertex_id = c.g, dest_game_vertex_id = c.dg, dest_position = c.dp, dest_level_vertex_id = c.dl, position = V(0, 0, 0) }
	end,
	kill_entity = function(_, se) se._r.offkill = se._r.offkill + 1 end,   -- health untouched, as the engine source (must not be used)
	release = function() end, create = function() end } end

local function fire(name, ...) for _, f in ipairs(CB[name] or {}) do f(...) end end
-- a death in X-Ray's own world (not by the script): killer by id, nil = none named
local function world_kill(id, kid) local R = OBJ[id]; R.dtime = NOW; R.killer = kid or -1; R.health = 0
	DEATHQ[#DEATHQ + 1] = { id = id, who = kid and { id = function() return kid end } or nil } end
local function tick(ms)
	NOW = NOW + ms
	if not GS_FROZEN then GAME_MS = GAME_MS + ms * TF end   -- kj3
	if not SIM_OFF and NOW >= EM_SIM_NEXT then EM_SIM_NEXT = NOW + 1000; em_sim() end   -- kj3: the scripts' 1 s update, before the bridge's (callback order not modelled)
	for id, R in pairs(OBJ) do if R.kill_at and NOW >= R.kill_at then   -- kj2: a delayed kill takes effect at the frame start
		R.kill_at = nil; R.health = -1; DEATHQ[#DEATHQ + 1] = { id = id, who = R.kill_who } end end
	local q = DEATHQ; DEATHQ = {}
	for _, d in ipairs(q) do local R = OBJ[d.id]   -- GAMMA: stalkers -> npc_on_death_callback, monsters -> monster_on_death_callback
		fire(R.kind == "s" and "npc_on_death_callback" or "monster_on_death_callback", go(d.id, R), d.who) end
	db.storage = {}
	for id, R in pairs(OBJ) do if online(R) then db.storage[id] = { object = go(id, R) } end end
	fire("actor_on_update")
	if db.actor.health < ACT.minh then ACT.minh = db.actor.health end
end
local SCRIPT
local function boot()
	CB = {}
	dofile(SCRIPT); on_game_start(); tick(1); tick(15000)
end
local function new_world(dir, script, run)
	SHM, SCRIPT, RUN, LEVEL, OBJ, CHANGED, NOW, DEATHQ, KTAB_WFAIL = dir, script, run or "1", "l01_escape", {}, nil, 0, {}, false
	KILL_LAG, LAST_SQ = 0, nil
	GAME_MS, TF, GS_FROZEN, SIM_OFF, CL_BASE, EM_START_ERR = 0, 6, false, false, nil, nil; new_managers()   -- kj3
	new_actor()
end
local function ticks(ms)   -- a level change inside: on_before_level_changing, then the new level boots
	for _ = 1, math.floor(ms / 50) do
		local ok, e = pcall(tick, 50)
		if not ok then
			if not tostring(e):find("LEVEL_CHANGE") then error(e, 0) end
			fire("on_before_level_changing"); LEVEL = LN[GV[CHANGED]]; boot(); return "changed"
		end
	end
end

-- pose writer: o = { k = {lines}, ver = "4" | nil | other, sess = string | false, state, zone, aw, c }
local SEQ, SESS = 0, "1795000000000"
local function pose(o)
	SEQ = o.seq or (SEQ + 1)
	local h = string.format("seq,%d,%d,%s,%s,%s", SEQ, 1795000000000 + SEQ * 50, o.state or "play", o.zone or LEVEL, o.aw or "keep")
	if o.sess ~= false then h = h .. "," .. (o.sess or SESS) end
	if o.ver then h = h .. "," .. o.ver end
	local L = { h, "p,1.000,2.300,3.000,0.0000,1.0,0" }
	if o.c then L[#L + 1] = "c," .. o.c end
	for _, k in ipairs(o.k or {}) do L[#L + 1] = k end
	L[#L + 1] = "end," .. SEQ
	local f = real_open(SHM .. "/pose.txt", "wb"); f:write(table.concat(L, "\r\n"), "\r\n"); f:close()   -- CRLF as from Wine
end
local function run(o, ms) local r; for _ = 1, math.floor((ms or 50) / 50) do pose(o); r = ticks(50) or r end; return r end
local function v4(k, x) x = x or {}; x.k, x.ver = k, x.ver or "4"; return x end
local function finish() fire("actor_on_net_destroy") end

local function readf(p) local f = real_open(p, "rb"); if not f then return "" end; local s = f:read("*a"); f:close(); return s end
local function meta() return readf(SHM .. "/meta_p1.csv") end
local function snap() return readf(SHM .. "/snap.txt") end
local function count(s, pat) local c = 0; for _ in string.gmatch(s, pat) do c = c + 1 end; return c end
local function sum(key) local v; for x in string.gmatch(meta(), "T2SUM,[^\n]*") do v = tonumber(string.match(x, "," .. key .. "=(%d+)")) end; return v end
local function has_line(s, l) return string.find("\n" .. s, "\n" .. l .. "\n", 1, true) ~= nil end
local function rline() return string.match(snap(), "\nr,([^\n]*)\n") end
local function near(a, b) return math.abs(a - b) < 1e-6 end
-- kj2 helpers. rec(o, ms): as run(o, ms) (o = nil: no new pose, the file on disk stays), but returns every snapshot written
-- meanwhile (the file is read after each 50 ms tick; a snapshot is written at most every 100 ms, so none is missed)
local function sseq(s) return string.match(s or "", "^seq,(%d+),") end
local function stx(s) return tonumber(string.match(s or "", "^seq,%d+,(%d+),")) end
local function rec(o, ms)
	local out = {}
	for _ = 1, math.floor((ms or 50) / 50) do
		if o then pose(o) end; ticks(50)
		local s = snap(); local q = sseq(s)
		if q and q ~= LAST_SQ then LAST_SQ = q; out[#out + 1] = s end
	end
	return out
end
local function ofield(s, id, k)   -- field k of id's o line (o,id,sec,kind,x,y,z,yaw,health,alive,enemy,dist: alive = 10)
	for l in string.gmatch(s, "[^\n]+") do
		if string.sub(l, 1, #tostring(id) + 3) == "o," .. id .. "," then local f = {}; for x in string.gmatch(l, "[^,]+") do f[#f + 1] = x end; return f[k] end
	end
end
local function kl(s, id) return string.match("\n" .. s, "\n(k," .. id .. ",[^\n]*)") end   -- id's k line, nil if none
local function mnum(pat) return tonumber(string.match(meta(), pat)) end
local function untilt(t, o) while NOW < t do rec(o, 50) end end   -- tick (with pose o, or none) until NOW >= t

------------------------------------------------------------------------------------------------ checks
local C, ORDER = {}, {}
local function check(name, f) C[name] = f; ORDER[#ORDER + 1] = name end
local F
local function expect(ok, what) if not ok then F[#F + 1] = what end end
local DIRS

-- v3 sequence for the old-against-new comparison: LOST, EPOCH, crossing; then a v2 writer with RESET
local function v3_sequence(script, dir, v2)
	new_world(dir, script); CL_EXTRA = v2 and "-t2_v2reset" or ""
	add(101, "dog_weak", "m"); add(102, "stalker_bandit", "s")
	SEQ = 0; boot()
	if v2 then
		run({ sess = false }, 3000); ticks(6000)              -- LOST
		SEQ = 0; run({ sess = false }, 2000)                  -- restarted v2 writer: RESET
	else
		run({ sess = "111" }, 3000); ticks(6000)              -- LOST
		SEQ = 0; run({ sess = "222" }, 2000)                  -- new session: EPOCH + FOUND
		run({ sess = "222", zone = "l02_garbage" }, 500)      -- another level: mismatch
		local r; for _ = 1, 20 do r = run({ sess = "222", state = "cross", zone = "l02_garbage", c = "esc_to_gar" }, 50); if r then break end end
		run({ sess = "222" }, 1000)                           -- after the change: the writer still says l01 -> mismatch
		run({ sess = "222", zone = "l02_garbage" }, 2000)
	end
	finish()
	return meta()
end
check("v3_unchanged", function(ARGS)
	for _, v2 in ipairs({ false, true }) do
		local m3 = v3_sequence(ARGS.lost3, DIRS.a .. (v2 and "/v2" or "/v3"), v2)
		local m4 = v3_sequence(SCRIPT_UNDER_TEST, DIRS.b .. (v2 and "/v2" or "/v3"), v2)
		expect(#m3 > 0, "lost3 wrote no log")
		local stripped = string.gsub(m4, "(T2SUM,[^\n]-),khits=%d+,kdead_online=%d+,kdead_offline=%d+,kowed=%d+,kstale=%d+,kback=%d+,krefused=%d+,kclamped=%d+,kver=0,kdead_fail=0,ktabfail=0,eerr=0", "%1")   -- kj3: eerr is the new last key (must be 0 here)
		expect(stripped == m3, "meta differs from lost3 (" .. (v2 and "v2" or "v3") .. " sequence)")
		expect(string.find(m3, v2 and "RESET," or "EPOCH,", 1, true) and string.find(m3, "LOST,", 1, true), "sequence did not reach its LOST/EPOCH/RESET")
		if not v2 then expect(string.find(m3, "CROSS_RESUME,", 1, true), "sequence did not cross") end
	end
end)

check("v3_klines_not_applied", function()
	add(101, "dog_weak", "m"); add(102, "stalker_bandit", "s"); boot()
	run({ k = { "k,101,dog_weak,0.300,0", "k,102,stalker_bandit,0.500,1" } }, 1000)
	expect(near(OBJ[101].health, 1) and #OBJ[101].hits == 0, "101 changed by a v3 pose")
	expect(OBJ[102].health == 1 and not OBJ[102].dtime, "102 killed by a v3 pose")
	finish()
	expect(count(meta(), "KREFUSED,%d+,%d+,10[12],v3") == 2, "v3 refusals not logged")
	expect((sum("krefused") or 0) >= 2, "krefused not counted")
end)

check("bare_k_refused", function()
	add(101, "dog_weak", "m"); add(102, "stalker_bandit", "s"); boot()
	run(v4({ "k,101" }), 500)
	run({ k = { "k,102" } }, 500)                 -- v3 pose with the old bare form
	expect(not OBJ[101].dtime and OBJ[101].health == 1, "bare k,101 in a v4 pose killed or hurt 101")
	expect(not OBJ[102].dtime and OBJ[102].health == 1, "bare k,102 in a v3 pose killed or hurt 102")
	finish()
	expect(string.find(meta(), "KREFUSED,%d+,%d+,101,form"), "bare k in v4 not logged as form")
	expect((sum("krefused") or 0) >= 2, "krefused not counted")
end)

check("bad_version_refused", function()
	add(101, "dog_weak", "m"); boot()
	local n0 = ACT.nset
	run(v4({ "k,101,dog_weak,0.300,0" }, { ver = "5" }), 500)
	run(v4({ "k,101,dog_weak,0.300,0" }, { ver = "4,9" }), 500)
	expect(OBJ[101].health == 1, "a pose with a bad version field was applied (101 hurt)")
	expect(ACT.nset == n0, "a pose with a bad version field moved the actor")
	run(v4({ "k,101,dog_weak,0.300,0" }), 200)
	expect(near(OBJ[101].health, 0.7), "the good v4 pose after them was not applied")
	finish()
	expect((sum("kver") or 0) >= 2, "kver not counted")
	expect(not string.find(meta(), "TORN,", 1, true), "refused versions logged as torn")
end)

check("repeated_reads", function()
	add(101, "dog_weak", "m"); boot()
	pose(v4({ "k,101,dog_weak,0.250,0" })); ticks(2000)   -- the same file read 40 times
	expect(near(OBJ[101].health, 0.75), "repeated read of one pose: health " .. OBJ[101].health)
	SEQ = SEQ + 10; run(v4({ "k,101,dog_weak,0.250,0" }), 1000)   -- missed writes, then the same total 20 times
	expect(near(OBJ[101].health, 0.75), "same total in new poses: health " .. OBJ[101].health)
	run(v4({ "k,101,dog_weak,0.500,0" }), 1000)
	expect(near(OBJ[101].health, 0.5), "0.500 over 20 poses: health " .. OBJ[101].health)
	finish()
	expect(count(meta(), "KAPPLY,") == 2 and sum("khits") == 2, "KAPPLY/khits not exactly 2")
end)

check("part_hit", function()
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.250,0" }), 300)
	expect(near(OBJ[101].health, 0.75), "first part: health " .. OBJ[101].health)
	local h = OBJ[101].hits[1]
	expect(#OBJ[101].hits == 1 and h.who == 0 and h.power > 0 and h.power <= 0.001, "near-zero hit from the actor not sent exactly once")
	expect(has_line(snap(), "k,101,0.250"), "snapshot lacks k,101,0.250")
	run(v4({ "k,101,dog_weak,0.600,0" }), 300)
	expect(near(OBJ[101].health, 0.4), "second part: health " .. OBJ[101].health)
	expect(OBJ[101].health > 0 and not OBJ[101].dtime, "101 died from part hits")
	finish()
	expect(string.find(meta(), "KAPPLY,%d+,%d+,101,0%.350,0%.600,0%.7500,0%.4000,hit"), "KAPPLY line for the second part wrong")
end)

check("stop_at_001", function()
	add(101, "dog_weak", "m", { health = 0.3 }); boot()
	run(v4({ "k,101,dog_weak,0.500,0" }), 500)
	expect(near(OBJ[101].health, 0.01), "health " .. OBJ[101].health .. ", expected 0.01")
	expect(OBJ[101].health > 0 and not OBJ[101].dtime, "101 died without dead=1")
end)

check("death_online", function()
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.250,0" }), 200)
	run(v4({ "k,101,dog_weak,0.250,1" }), 200)
	expect(OBJ[101].dtime and OBJ[101].killer == 0, "101 not killed by the actor")
	expect(OBJ[101].kill_err == 0, "kill called on a dead object")
	expect(has_line(snap(), "k,101,0.250,dead"), "snapshot lacks k,101,0.250,dead")
	finish()
	expect(sum("kdead_online") == 1 and count(meta(), "KDEAD,%d+,%d+,101,online") == 1, "KDEAD online / kdead_online not 1")
end)

check("death_already_dead", function()
	add(102, "stalker_bandit", "s", { health = 0 }); OBJ[102].dtime = 5; OBJ[102].killer = 77   -- died in X-Ray first
	add(105, "dog_weak", "m"); boot()
	OBJ[105].released = true                                                                   -- released after its snapshot
	run(v4({ "k,102,stalker_bandit,0.300,1", "k,105,dog_weak,0.200,1" }), 500)
	expect(OBJ[102].kill_err == 0 and OBJ[102].killer == 77 and #OBJ[102].hits == 0, "the dead 102 was hit or killed again")
	local s = snap()
	expect(has_line(s, "k,102,0.000,dead") and has_line(s, "k,105,0.000,dead"), "dead / released not confirmed in the snapshot")
	finish()
	expect(string.find(meta(), "KDEAD,%d+,%d+,102,seen") and string.find(meta(), "KDEAD,%d+,%d+,105,released"), "KDEAD seen/released missing")
end)

check("offline_death", function()
	add(103, "boar_strong", "m"); boot()
	OBJ[103].online = false
	run(v4({ "k,103,boar_strong,0.400,1" }), 500)
	expect(OBJ[103].sekill == 1 and OBJ[103].offkill == 0, "se:kill not called exactly once, or kill_entity used")
	expect(OBJ[103].health == 0, "offline 103 still alive (health " .. OBJ[103].health .. ")")
	expect(has_line(snap(), "k,103,0.000,dead"), "snapshot lacks k,103,0.000,dead")
	finish()
	expect(sum("kdead_offline") == 1 and string.find(meta(), "KDEAD,%d+,%d+,103,offline"), "KDEAD offline / kdead_offline")
end)

check("offline_death_failed", function()
	add(103, "boar_strong", "m"); boot()
	OBJ[103].online = false; OBJ[103].killfail = true          -- the server kill has no effect
	run(v4({ "k,103,boar_strong,0.400,1" }), 500)
	expect(not string.find(snap(), "\nk,103", 1, true), "failed offline kill confirmed in the snapshot")
	finish()
	expect(count(meta(), "KDEAD,%d+,%d+,103,failed,alive") == 1, "KDEAD failed not logged once")
	expect(sum("kdead_fail") == 1 and sum("kdead_offline") == 0, "kdead_fail / kdead_offline counters")
end)

check("owed_hit", function()
	add(103, "boar_strong", "m"); boot()
	OBJ[103].online = false
	run(v4({ "k,103,boar_strong,0.300,0" }), 2000)
	expect(OBJ[103].health == 1, "offline part applied while offline")
	expect(not string.find(snap(), "\nk,103", 1, true), "snapshot confirms an owed part")
	OBJ[103].online = true
	run(v4({ "k,103,boar_strong,0.300,0" }), 300)
	expect(near(OBJ[103].health, 0.7), "owed part not applied on return: health " .. OBJ[103].health)
	expect(has_line(snap(), "k,103,0.300"), "snapshot lacks k,103,0.300 after the return")
	finish()
	expect(count(meta(), "KOWED,") == 1 and sum("kowed") == 1, "KOWED not exactly once")
end)

check("total_backwards", function()
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.400,0" }), 200)
	run(v4({ "k,101,dog_weak,0.200,0" }), 1000)
	expect(near(OBJ[101].health, 0.6), "falling total changed health: " .. OBJ[101].health)
	run(v4({ "k,101,dog_weak,0.500,0" }), 200)
	expect(near(OBJ[101].health, 0.5), "0.500 after the fall: health " .. OBJ[101].health)
	finish()
	expect(sum("kback") == 1 and count(meta(), "KBACK,") == 1, "KBACK not exactly once")
end)

check("id_zero", function()
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,0,actor,0.500,1" }), 500)
	expect(ACT.killed == 0 and ACT.hits == 0 and ACT.sets == 0 and ACT.minh == 1, "the actor was touched")
	finish()
	expect(string.find(meta(), "KREFUSED,%d+,%d+,0,actor"), "id 0 not refused as actor")
end)

check("non_creature", function()
	add(200, "explosive_barrel", "x"); add(106, "dog_weak", "m", { online = false }); boot()
	run(v4({ "k,200,explosive_barrel,0.500,1", "k,106,dog_weak,0.500,1" }), 500)
	expect(OBJ[200].health == 1 and OBJ[200].sets == 0 and #OBJ[200].hits == 0 and not OBJ[200].dtime, "non-creature 200 touched")
	expect(OBJ[106].offkill == 0, "offline id never seen as a creature was killed")
	finish()
	expect(string.find(meta(), "KREFUSED,%d+,%d+,200,kind") and string.find(meta(), "KREFUSED,%d+,%d+,106,kind"), "kind refusals missing")
end)

check("over64", function()
	add(101, "dog_weak", "m"); boot()
	local L = { "k,101,dog_weak,0.250,0" }; for i = 1, 64 do L[#L + 1] = string.format("k,%d,dog_weak,0.100,0", 3000 + i) end
	run(v4(L), 300)
	expect(OBJ[101].health == 1, "65 lines: 101 hurt")
	L[65] = nil; run(v4(L), 300)
	expect(near(OBJ[101].health, 0.75), "64 lines: 101 not hurt (" .. OBJ[101].health .. ")")
	finish()
	expect(string.find(meta(), "KREFUSED,%d+,%d+,%-,over64"), "over64 not refused")
end)

check("new_session", function()
	add(101, "dog_weak", "m"); add(103, "boar_strong", "m"); boot()
	OBJ[103].online = false
	run(v4({ "k,101,dog_weak,0.300,0", "k,103,boar_strong,0.300,0" }, { sess = "111" }), 300)
	expect(near(OBJ[101].health, 0.7), "session A part")
	SEQ = 0
	run(v4({ "k,101,dog_weak,0.200,0" }, { sess = "222" }), 300)
	expect(near(OBJ[101].health, 0.5), "session B 0.200 on 101: health " .. OBJ[101].health)
	expect(has_line(snap(), "k,101,0.200"), "snapshot lacks k,101,0.200")
	OBJ[103].online = true
	run(v4({ "k,101,dog_weak,0.200,0", "k,103,boar_strong,0.100,0" }, { sess = "222" }), 300)
	expect(near(OBJ[103].health, 0.9), "owed session-A part not dropped: 103 health " .. OBJ[103].health)
	finish()
	expect(sum("kback") == 0 and sum("epochs") == 1, "KBACK on a new session, or no EPOCH")
end)

check("stale_section", function()
	add(101, "dog_weak", "m"); add(102, "stalker_bandit", "s"); boot()
	run(v4({ "k,101,dog_strong,0.300,0" }), 1000)
	expect(OBJ[101].health == 1 and #OBJ[101].hits == 0, "line with another section applied")
	expect(has_line(snap(), "k,101,0.000,stale"), "stale 101 not answered stale")   -- kj2: amendment 138 point 2 (kj1: dead)
	run(v4({}), 300)
	expect(not string.find(snap(), "\nk,101", 1, true), "dead answer kept after the line was dropped")
	-- death confirmed more than 10 s ago, id alive again (same section)
	run(v4({ "k,102,stalker_bandit,0.300,1" }), 200)
	expect(OBJ[102].dtime, "102 not killed")
	run(v4({}), 11000)
	OBJ[102].health, OBJ[102].dtime, OBJ[102].hits = 1, nil, {}
	run(v4({ "k,102,stalker_bandit,0.500,0" }), 500)
	expect(OBJ[102].health == 1 and #OBJ[102].hits == 0, "reused id 102 hurt")
	expect(has_line(snap(), "k,102,0.300,stale"), "stale 102 not answered stale")   -- kj2: amendment 138 point 2 (kj1: dead)
	finish()
	expect(count(meta(), "KSTALE,%d+,%d+,101,section") == 1 and count(meta(), "KSTALE,%d+,%d+,102,alive_again") == 1, "KSTALE lines")
	expect(sum("kstale") == 2, "kstale ~= 2")
end)

check("lost_middle", function()
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.200,0" }), 500)
	ticks(6000)                                     -- writer gone: LOST at 5 s; the old file re-read meanwhile
	expect(near(OBJ[101].health, 0.8), "changed during LOST")
	run(v4({ "k,101,dog_weak,0.500,0" }), 300)       -- same session, back
	expect(near(OBJ[101].health, 0.5), "after FOUND: health " .. OBJ[101].health .. " (table not kept over LOST?)")
	finish()
	local s = meta(); expect(string.find(s, "LOST,", 1, true) and string.find(s, "FOUND,", 1, true), "no LOST/FOUND")
end)

check("cross_state_gate", function()
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.300,0" }, { zone = "l02_garbage" }), 300)          -- another level: nothing
	run(v4({ "k,101,dog_weak,0.300,0" }, { state = "cross" }), 300)               -- crossing state on this level
	expect(OBJ[101].health == 1, "applied for another level or in a crossing")
	run(v4({ "k,101,dog_weak,0.300,0" }), 300)
	expect(near(OBJ[101].health, 0.7), "not applied on play")
end)

local function cross_world(dir, run2)
	new_world(dir, SCRIPT_UNDER_TEST, "1")
	add(101, "dog_weak", "m"); add(102, "stalker_bandit", "s")
	boot()
	run(v4({ "k,101,dog_weak,0.300,0" }), 300)
	local r_pre = SEQ   -- the last pose whose k lines were processed before the crossing
	local r; for _ = 1, 20 do r = run(v4({ "k,101,dog_weak,0.300,0" }, { state = "cross", zone = "l02_garbage", c = "esc_to_gar" }), 50); if r then break end end
	expect(r == "changed" and LEVEL == "l02_garbage", "no level change")
	if run2 then RUN = run2; end
	return r, r_pre
end
check("crossing", function()
	-- same run id: table carried over
	local _, r_pre = cross_world(DIRS.a)
	local kt = readf(DIRS.a .. "/ktab.txt")
	expect(string.find(kt, "^run,1\n") and string.find(kt, "\nr,101,0.300000000,", 1, true), "ktab.txt not written at the crossing request")
	expect(rline() == SESS .. "," .. r_pre, "r line before the first destination pose not the pre-crossing " .. SESS .. "," .. r_pre .. ": " .. tostring(rline()))
	pose(v4({ "k,101,dog_weak,0.300,0" }, { zone = "l02_garbage" })); ticks(100)
	expect(rline() == SESS .. "," .. SEQ, "r did not advance to the first destination pose " .. SEQ .. ": " .. tostring(rline()))
	run(v4({ "k,101,dog_weak,0.300,0" }, { zone = "l02_garbage" }), 500)
	expect(has_line(snap(), "k,101,0.300"), "table not read back at the resume (no k,101,0.300)")
	run(v4({ "k,101,dog_weak,0.500,0" }, { zone = "l02_garbage" }), 300)
	finish()
	expect(string.find(meta(), "KOWED,%d+,%d+,101,0%.500,0%.300"), "after the resume 101 not owed from 0.300")
	expect(string.find(meta(), "KTAB,read,1,1", 1, true), "KTAB read line missing")
	-- another run id (X-Ray restarted between): not read. The boot after the change uses run id 2.
	new_world(DIRS.c, SCRIPT_UNDER_TEST, "1")
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.300,0" }), 300)
	RUN = "2"   -- the next boot (after the level change) sees another run id
	local r; for _ = 1, 20 do r = run(v4({ "k,101,dog_weak,0.300,0" }, { state = "cross", zone = "l02_garbage", c = "esc_to_gar" }), 50); if r then break end end
	expect(r == "changed", "no level change (run 2)")
	run(v4({}, { zone = "l02_garbage" }), 500)
	expect(not string.find(snap(), "\nk,101", 1, true), "table of another run id read back")
	finish()
	expect(string.find(meta(), "KTAB,skip,1,1", 1, true), "KTAB skip line missing")
end)

check("snapshot_lines", function()
	add(101, "dog_weak", "m"); add(102, "stalker_bandit", "s"); boot()
	run(v4({ "k,101,dog_weak,0.250,0" }), 300)
	local s = snap()
	expect(string.match(s, "^seq,%d+,%d+,[^,]+,l01_escape,1,4\n"), "snapshot header 7th field not 4")
	expect(string.find(s, "\no,[^\n]*\nr,[^\n]*\ne,none,0%.00,0,6\nns,[^\n]*\nk,101,0%.250\nend,%d+\n$"), "k line not after the o and r lines and before end")
	expect(not string.find(s, "\nk,102", 1, true), "line for an id never hit")
	run(v4({ "k,101,dog_weak,0.250,1" }), 50)
	run(v4({}), 9000)
	expect(has_line(snap(), "k,101,0.250,dead"), "dead line gone before 10 s")
	run(v4({}), 2000)
	expect(not string.find(snap(), "\nk,101", 1, true), "dead line still there after 11 s")
end)

check("actor_health", function()
	add(101, "dog_weak", "m"); add(102, "stalker_bandit", "s"); boot()
	run(v4({ "k,101,dog_weak,0.300,0", "k,102,stalker_bandit,0.400,0", "k,0,actor,0.900,1" }), 500)
	run(v4({ "k,101,dog_weak,0.900,1", "k,102,stalker_bandit,0.700,0" }), 500)
	expect(OBJ[101].dtime and near(OBJ[102].health, 0.3), "scenario did not apply")
	expect(ACT.hits == 0 and ACT.sets == 0 and ACT.minh == 1 and ACT.killed == 0 and db.actor.health == 1, "actor health touched")
end)

check("total_above_one", function()   -- kj1 (healing): a running total may pass 1.000 on a creature that healed between hits
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.600,0" }), 300)
	expect(near(OBJ[101].health, 0.4), "first hit: health " .. OBJ[101].health)
	OBJ[101].health = 0.8   -- X-Ray's own regeneration between the hits
	run(v4({ "k,101,dog_weak,1.100,0" }), 300)
	expect(near(OBJ[101].health, 0.3), "second hit: health " .. OBJ[101].health .. ", expected 0.3 (0.8 - 0.5)")
	expect(OBJ[101].health > 0 and not OBJ[101].dtime, "101 died with dead=0")
	expect(has_line(snap(), "k,101,1.100"), "snapshot lacks k,101,1.100")
	finish()
	expect(sum("kclamped") == 0, "a total above 1.000 was counted as cut")
	expect(sum("krefused") == 0, "a total above 1.000 was refused")
	expect(string.find(meta(), "KAPPLY,%d+,%d+,101,0%.500,1%.100,0%.8000,0%.3000,hit"), "KAPPLY line for the total above 1.000 wrong")
end)

check("clamp", function()
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,1.500,0" }), 300)
	expect(near(OBJ[101].health, 0.01), "health " .. OBJ[101].health)
	finish()
	expect(sum("kclamped") == 1 and string.find(meta(), "KAPPLY,%d+,%d+,101,1%.000,1%.500"), "difference not cut to 1.0 / not counted")
end)

check("r_line_presence", function()
	add(101, "dog_weak", "m"); boot()
	run({ k = {} }, 500)                                        -- v3 poses only: no r line
	expect(rline() == nil, "r line before any v4 pose")
	run(v4({}), 200)                                            -- v4 pose without k lines counts
	expect(rline() == SESS .. "," .. SEQ, "r line after a v4 pose without k lines: " .. tostring(rline()))
	run(v4({ "k,101,dog_weak,0.250,0" }), 200)
	expect(string.find(snap(), "\no,[^\n]*\nr," .. SESS .. "," .. SEQ .. "\ne,none,0%.00,0,6\nns,[^\n]*\nk,101,0%.250\nend,%d+\n$"), "r line not between the o lines and the k lines")
end)

check("r_advances", function()
	add(101, "dog_weak", "m"); add(103, "boar_strong", "m"); boot()
	OBJ[103].online = false
	for i = 1, 5 do pose(v4({ "k,101,dog_weak,0.100,0" })); ticks(100); expect(rline() == SESS .. "," .. SEQ, "r not at pose " .. SEQ .. ": " .. tostring(rline())) end
	run(v4({ "k,103,boar_strong,0.300,0" }), 200)              -- owed part: processed, applied stays
	expect(rline() == SESS .. "," .. SEQ, "r did not advance on an owed part: " .. tostring(rline()))
	expect(not string.find(snap(), "\nk,103", 1, true) and OBJ[103].health == 1, "owed part applied or confirmed")
end)

check("r_stands_still", function()
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.100,0" }), 200)
	local r0 = rline(); expect(r0 == SESS .. "," .. SEQ, "r not set: " .. tostring(r0))
	ticks(6000)                                                 -- LOST
	expect(rline() == r0, "r moved while LOST")
	run(v4({ "k,101,dog_weak,0.200,0" }, { zone = "l02_garbage" }), 300)   -- another level (this also ends LOST)
	expect(rline() == r0, "r moved on a wrong-level pose: " .. tostring(rline()))
	run(v4({ "k,101,dog_weak,0.200,0" }, { state = "cross" }), 300)
	expect(rline() == r0, "r moved in a crossing state")
	run(v4({ "k,101,dog_weak,0.200,0" }, { ver = "5" }), 300)
	expect(rline() == r0, "r moved on a kver-refused pose")
	SEQ = SEQ + 1; local f = io.open(SHM .. "/pose.txt", "wb"); f:write("seq,", SEQ, ",1,play,l01_escape,keep,", SESS, ",4\r\nk,101,dog_weak,0.200,0\r\n"); f:close(); ticks(300)   -- torn
	expect(rline() == r0, "r moved on a torn pose")
	finish(); local m_ = meta()
	expect(string.find(m_, "LOST,", 1, true) and string.find(m_, "ZONE_MISMATCH,", 1, true) and string.find(m_, "TORN,", 1, true), "scenario did not reach LOST / mismatch / torn")
end)

check("r_new_session", function()
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.300,0" }, { sess = "111" }), 300)
	expect(rline() == "111," .. SEQ, "r in session 111: " .. tostring(rline()))
	SEQ = 0; pose(v4({ "k,101,dog_weak,0.100,0" }, { sess = "222" })); ticks(100)
	expect(rline() == "222,1", "r after the new session: " .. tostring(rline()))
end)

check("death_lethal_total", function()   -- the part hit alone takes health to 0 (dead = 1): the kill call still kills
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,1.000,1" }), 300)
	expect(OBJ[101].dtime and OBJ[101].killer == 0, "101 not killed by the actor (killer " .. tostring(OBJ[101].killer) .. ")")
	expect(OBJ[101].kill_err == 0 and OBJ[101].health == 0, "kill error or health not 0")
	finish()
	expect(sum("kdead_online") == 1 and count(meta(), "KDEAD,%d+,%d+,101,online") == 1, "KDEAD online / kdead_online not 1")
end)

check("kill_who", function()
	add(101, "dog_weak", "m"); add(102, "stalker_bandit", "s"); add(103, "boar_strong", "m"); boot()
	run(v4({ "k,101,dog_weak,0.250,1", "k,102,stalker_bandit,0.300,1" }), 500)
	world_kill(103, 102); ticks(200)                                   -- a death with no row: no KWHO
	DEATHQ[#DEATHQ + 1] = { id = 102, who = { id = function() return 9000 end } }; ticks(200)   -- a second callback (another killer) for 102
	finish()
	local s = meta()
	expect(count(s, "\nKWHO,%d+,101,0\n") == 1 and count(s, "\nKWHO,%d+,102,0\n") == 1, "KWHO with killer 0 not once each for 101 and 102")
	expect(count(s, "\nKWHO,") == 2, "KWHO lines: " .. count(s, "\nKWHO,") .. ", expected 2 (none for 103, none repeated)")
	-- a v3 run: no rows, no line, whatever dies
	new_world(DIRS.b, SCRIPT_UNDER_TEST); add(101, "dog_weak", "m"); add(102, "stalker_bandit", "s"); boot()
	run({ k = { "k,101,dog_weak,0.300,1" } }, 300); world_kill(101, 0); world_kill(102, 0); run({}, 300)
	finish()
	expect(OBJ[101].dtime and not string.find(meta(), "KWHO,", 1, true), "KWHO in a v3 run")
end)

check("over64_r", function()   -- a pose with more than 64 k lines is refused but processed: r advances
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.100,0" }), 200)
	local L = {}; for i = 1, 65 do L[#L + 1] = string.format("k,%d,dog_weak,0.100,0", 3000 + i) end
	pose(v4(L)); ticks(100)
	expect(rline() == SESS .. "," .. SEQ, "r not at the over-64 pose " .. SEQ .. ": " .. tostring(rline()))
end)

check("ktab_decimals", function()   -- a total with more than 3 decimals survives the crossing exactly
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.1234,0" }), 300)
	expect(near(OBJ[101].health, 1 - 0.1234), "0.1234 not applied: " .. OBJ[101].health)
	local r; for _ = 1, 20 do r = run(v4({ "k,101,dog_weak,0.1234,0" }, { state = "cross", zone = "l02_garbage", c = "esc_to_gar" }), 50); if r then break end end
	expect(r == "changed", "no level change")
	OBJ[101].lvl = "l02_garbage"                                         -- the creature is online on the new level
	run(v4({ "k,101,dog_weak,0.1234,0" }, { zone = "l02_garbage" }), 500)
	expect(near(OBJ[101].health, 1 - 0.1234) and #OBJ[101].hits == 1, "same total after the crossing changed health: " .. OBJ[101].health)
	finish()
	expect(count(meta(), "KAPPLY,") == 1, "KAPPLY lines " .. count(meta(), "KAPPLY,") .. ", expected 1")
end)

check("ktab_savefail", function()
	for _, how in ipairs({ "open", "write" }) do
		new_world(how == "open" and DIRS.b or DIRS.c, SCRIPT_UNDER_TEST, "1")
		if how == "open" then os.execute("mkdir -p '" .. SHM .. "/ktab.txt'") else KTAB_WFAIL = true end   -- a directory cannot be opened as a file
		add(101, "dog_weak", "m"); boot()
		run(v4({ "k,101,dog_weak,0.300,0" }), 300)
		local r; for _ = 1, 20 do r = run(v4({ "k,101,dog_weak,0.300,0" }, { state = "cross", zone = "l02_garbage", c = "esc_to_gar" }), 50); if r then break end end
		expect(r == "changed", how .. ": the crossing did not proceed")
		run(v4({}, { zone = "l02_garbage" }), 300)
		finish()
		local s = meta()
		expect(count(s, "KTAB,savefail,1") == 1, how .. ": KTAB,savefail not logged once")
		local t; for x in string.gmatch(s, "T2SUM,[^\n]*") do t = t or x end   -- the first T2SUM: the Lua state that saved
		expect(t and string.find(t, ",ktabfail=1,eerr=0$"), how .. ": ktabfail not 1 at the end of the first T2SUM: " .. tostring(t))
	end
end)

------------------------------------------------------------------------------------------------ kj2 checks (kill5, amendment 138)
check("dead_waits_for_effect", function()   -- point 1: a kill call not yet in effect writes the plain line, never dead
	KILL_LAG = 1000
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.250,0" }), 300)
	local S = rec(v4({ "k,101,dog_weak,0.250,1" }), 900)   -- ITR2 keeps sending dead = 1 while no dead line comes back
	local T = mnum("\nKDEAD,(%d+),%d+,101,online")
	expect(T and OBJ[101].dtime and OBJ[101].health > 0, "kill not called, or already in effect (scenario)")
	local n = 0
	for _, s in ipairs(S) do if T and stx(s) >= T then n = n + 1
		expect(ofield(s, 101, 10) == "1", "snapshot " .. sseq(s) .. ": 101 not listed alive 1 (scenario)")
		expect(kl(s, 101) == "k,101,0.250", "snapshot " .. sseq(s) .. " after the call, 101 still alive: k line " .. tostring(kl(s, 101)) .. ", expected the plain k,101,0.250") end end
	expect(n >= 5, "only " .. n .. " snapshots after the call while 101 was alive")
	expect(not string.find(meta(), "KCONF,", 1, true), "KCONF while 101 still alive")
end)

check("dead_next_frame", function()   -- as kj1's real engine: the kill takes effect one frame later; dead in the next snapshot
	KILL_LAG = 1
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.250,0" }), 300)
	local q0 = sseq(snap()); local g = 0
	repeat ticks(50); g = g + 1 until sseq(snap()) ~= q0 or g > 10   -- a snapshot was just written: the next is 2 frames later
	ticks(50); LAST_SQ = sseq(snap())
	local S1 = rec(v4({ "k,101,dog_weak,0.250,1" }), 50)   -- the kill pose is taken in a frame that writes a snapshot (kj1 snap 963)
	local T = mnum("\nKDEAD,(%d+),%d+,101,online")
	local s1 = S1[1]
	expect(T and s1 and stx(s1) == T, "scenario: no snapshot in the kill frame (T " .. tostring(T) .. ", snap " .. tostring(s1 and stx(s1)) .. ")")
	if s1 then expect(ofield(s1, 101, 10) == "1" and kl(s1, 101) == "k,101,0.250", "kill frame snapshot: alive " .. tostring(ofield(s1, 101, 10)) .. ", k line " .. tostring(kl(s1, 101))) end
	local S2 = rec(v4({ "k,101,dog_weak,0.250,1" }), 100)
	local s2 = S2[1]
	expect(s2 and ofield(s2, 101, 10) == "0", "next snapshot: 101 not alive 0 (scenario)")
	expect(s2 and kl(s2, 101) == "k,101,0.250,dead", "next snapshot (101 reads dead): k line " .. tostring(s2 and kl(s2, 101)) .. ", expected k,101,0.250,dead")
	local c = mnum("\nKCONF,%d+,101,(%d+)\n")
	expect(c and s2 and T and c == stx(s2) - T, "KCONF ms " .. tostring(c) .. ", expected " .. tostring(s2 and T and stx(s2) - T))
	finish()
	expect(count(meta(), "\nKCONF,") == 1 and sum("kdead_fail") == 0 and sum("kdead_online") == 1, "KCONF lines / kdead_fail / kdead_online")
end)

check("no_dead_beside_alive", function()   -- point 1, the hold: a confirmed death that reads alive again inside 10 s
	add(102, "stalker_bandit", "s"); boot()
	run(v4({ "k,102,stalker_bandit,0.300,0" }), 300)
	run(v4({ "k,102,stalker_bandit,0.300,1" }), 300)
	expect(has_line(snap(), "k,102,0.300,dead"), "scenario: 102 not confirmed dead")
	run(v4({}), 1000)
	OBJ[102].health, OBJ[102].dtime = 1, nil                    -- alive again 1.3 s after its death (inside the 10 s hold)
	local S = rec(v4({}), 1000); local bad = 0
	for _, s in ipairs(S) do
		if ofield(s, 102, 10) == "1" and string.find(s, "\nk,102,[^\n]*,dead\n") then bad = bad + 1 end end
	expect(#S >= 5 and bad == 0, bad .. " of " .. #S .. " snapshots hold k,102,...,dead beside o alive 1")
	expect(has_line(snap(), "k,102,0.300"), "reader's choice (NOTES): plain line inside the hold while alive; got " .. tostring(kl(snap(), 102)))
end)

check("kill_never_effect", function()   -- point 4: still alive 2.0 s after the call: KNOCONF, kdead_fail, no dead line, no retry
	KILL_LAG = false
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.250,0" }), 300)
	local S = rec(v4({ "k,101,dog_weak,0.250,1" }), 100)   -- two frames: one of them writes a snapshot and flushes the meta file
	local T = mnum("\nKDEAD,(%d+),%d+,101,online")
	expect(T ~= nil, "scenario: no kill call")
	if not T then return end
	while NOW < T + 1900 do for _, s in ipairs(rec(v4({ "k,101,dog_weak,0.250,1" }), 50)) do S[#S + 1] = s end end
	expect(not string.find(meta(), "KNOCONF,", 1, true), "KNOCONF before 2.0 s (at " .. NOW - T .. " ms)")
	while NOW < T + 3000 do for _, s in ipairs(rec(v4({ "k,101,dog_weak,0.250,1" }), 50)) do S[#S + 1] = s end end
	local d = 0; for _, s in ipairs(S) do if string.find(s, "\nk,101,[^\n]*,dead\n") then d = d + 1 end end
	expect(d == 0, d .. " snapshots with a dead line for 101 (never in effect)")
	expect(has_line(snap(), "k,101,0.250"), "plain line not kept: " .. tostring(kl(snap(), 101)))
	expect(OBJ[101].kills == 1 and OBJ[101].kill_err == 0, "kill calls " .. tostring(OBJ[101].kills) .. ", errors " .. OBJ[101].kill_err .. " (no retry)")
	finish()
	local s = meta(); local ms = tonumber(string.match(s, "\nKNOCONF,%d+,101,(%d+)\n"))
	expect(count(s, "\nKNOCONF,") == 1 and ms and ms >= 2000 and ms < 2100, "KNOCONF lines " .. count(s, "\nKNOCONF,") .. ", ms " .. tostring(ms) .. " (expected one, 2000..2099)")
	expect(sum("kdead_fail") == 1 and sum("kdead_online") == 1 and count(s, "\nKDEAD,") == 1, "kdead_fail / kdead_online / KDEAD lines")
	expect(not string.find(s, "KCONF,", 1, true) and not string.find(s, "KDEAD,%d+,%d+,101,seen"), "KCONF or KDEAD seen for a kill never in effect")
end)

check("stale_word", function()   -- point 2: both stale cases answer stale, never dead; alive-again counted from the confirmation
	KILL_LAG = 1500
	add(101, "dog_weak", "m"); add(102, "stalker_bandit", "s"); boot()
	run(v4({ "k,101,dog_strong,0.300,0" }), 300)                       -- another section
	expect(has_line(snap(), "k,101,0.000,stale") and not string.find(snap(), "\nk,101,[^\n]*,dead"), "section case: " .. tostring(kl(snap(), 101)))
	run(v4({ "k,102,stalker_bandit,0.300,1" }), 50)                    -- killed; in effect 1.5 s later
	run(v4({}), 2000)
	local Tc = mnum("\nKCONF,(%d+),102,")
	expect(Tc ~= nil, "scenario: 102 never confirmed")
	if not Tc then return end
	untilt(Tc + 9400, v4({}))
	OBJ[102].health, OBJ[102].dtime, OBJ[102].hits = 1, nil, {}      -- alive again (hit list emptied: hits after this are the reader's)
	run(v4({ "k,102,stalker_bandit,0.500,0" }), 100)                   -- 9.4-9.5 s after the confirmation (> 10 s after the call)
	expect(not string.find(snap(), ",stale\n", 1, true) and not string.find(meta(), "KSTALE,%d+,%d+,102"), "102 stale less than 10 s after its CONFIRMED death: " .. tostring(kl(snap(), 102)))
	untilt(Tc + 10400, v4({}))
	run(v4({ "k,102,stalker_bandit,0.500,0" }), 200)
	expect(has_line(snap(), "k,102,0.300,stale") and not string.find(snap(), "\nk,102,[^\n]*,dead"), "alive-again case: " .. tostring(kl(snap(), 102)))
	expect(OBJ[102].health == 1 and #OBJ[102].hits == 0, "reused id 102 hurt")
	finish()
	expect(count(meta(), "KSTALE,%d+,%d+,101,section") == 1 and count(meta(), "KSTALE,%d+,%d+,102,alive_again") == 1 and sum("kstale") == 2, "KSTALE lines / kstale")
end)

check("no_object_dead", function()   -- point 2 (as amendment 134): an id with no object gets a dead line with its applied total (106: 0.300; 105: 0.000, nothing applied), never stale
	add(105, "dog_weak", "m"); add(106, "boar_strong", "m"); boot()
	run(v4({ "k,106,boar_strong,0.300,0" }), 300)
	OBJ[105].released = true; OBJ[106].released = true                -- no game object, no server object
	run(v4({ "k,105,dog_weak,0.200,1", "k,106,boar_strong,0.300,0" }), 300)
	local s = snap()
	expect(has_line(s, "k,105,0.000,dead"), "no-object 105: " .. tostring(kl(s, 105)) .. ", expected k,105,0.000,dead")
	expect(has_line(s, "k,106,0.300,dead"), "no-object 106 (hit before): " .. tostring(kl(s, 106)) .. ", expected k,106,0.300,dead")
	finish()
	expect(count(meta(), "KDEAD,%d+,%d+,10[56],released") == 2 and sum("kstale") == 0, "KDEAD released lines / kstale")
end)

check("pending_stale_noconf", function()   -- fix round 2 (1): a pending kill is never confirmed on a stale row
	KILL_LAG = false                                                   -- the call never takes effect on the original
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.250,0" }), 300)
	run(v4({ "k,101,dog_weak,0.250,1" }), 50)                          -- the kill call (101 still alive)
	expect(OBJ[101].dtime and OBJ[101].health > 0, "scenario: no kill call, or already in effect")
	add(101, "boar_strong", "m", { health = 0 })                       -- id reused by a DEAD creature of another section, before the next export
	local S = rec(v4({ "k,101,dog_weak,0.250,1" }), 500)
	expect(#S >= 3, "scenario: snapshots " .. #S)
	for _, s in ipairs(S) do
		expect(ofield(s, 101, 10) == "0" and kl(s, 101) == "k,101,0.250,stale", "snapshot " .. sseq(s) .. ": alive " .. tostring(ofield(s, 101, 10)) ..
			", k line " .. tostring(kl(s, 101)) .. " (expected k,101,0.250,stale beside the dead replacement)") end
	finish()
	local s = meta()
	expect(not string.find(s, "\nKCONF,", 1, true), "KCONF logged for a row marked stale: " .. tostring(string.match(s, "\n(KCONF,[^\n]*)")))
	expect(count(s, "KSTALE,%d+,%d+,101,section") == 1 and sum("kstale") == 1 and sum("kdead_online") == 1, "KSTALE / kstale / kdead_online")
end)

check("pending_released", function()   -- fix round 2 (2): released between the kill call and the next export; and the old case
	KILL_LAG = false
	add(101, "dog_weak", "m"); add(105, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.250,0", "k,105,dog_weak,0.200,0" }), 300)
	run(v4({ "k,101,dog_weak,0.250,1", "k,105,dog_weak,0.200,0" }), 50)   -- the kill call on 101; 105 hit only
	OBJ[101].released = true; OBJ[105].released = true                -- no game object, no server object for either
	local S = rec(v4({ "k,101,dog_weak,0.250,1", "k,105,dog_weak,0.200,0" }), 500)
	expect(#S >= 3, "scenario: snapshots " .. #S)
	for _, s in ipairs(S) do
		expect(kl(s, 101) == "k,101,0.250,dead" and kl(s, 105) == "k,105,0.200,dead", "snapshot " .. sseq(s) .. ": " .. tostring(kl(s, 101)) .. " / " .. tostring(kl(s, 105))) end
	finish()
	local s = meta(); local T = mnum("\nKDEAD,(%d+),%d+,101,online")
	expect(count(s, "\nKDEAD,%d+,%d+,101,") == 1 and count(s, "\nKDEAD,%d+,%d+,101,online") == 1, "101: KDEAD lines " .. count(s, "\nKDEAD,%d+,%d+,101,") .. " (expected exactly one, online)")
	local c = tonumber(string.match(s, "\nKCONF,%d+,101,(%d+)\n")); local Tc = tonumber(string.match(s, "\nKCONF,(%d+),101,"))
	expect(count(s, "\nKCONF,%d+,101,") == 1 and T and Tc and c == Tc - T and stx(S[1]) == Tc, "101: KCONF lines " .. count(s, "\nKCONF,%d+,101,") ..
		", ms " .. tostring(c) .. " (call " .. tostring(T) .. ", confirmation " .. tostring(Tc) .. ", first snapshot after the release " .. tostring(S[1] and stx(S[1])) .. ")")
	expect(count(s, "\nKDEAD,%d+,%d+,105,released") == 1 and not string.find(s, "\nKCONF,%d+,105,"), "105 (no pending kill): KDEAD released once, no KCONF, as before")
	expect(sum("kdead_online") == 1 and sum("kdead_fail") == 0, "kdead_online / kdead_fail")
end)

check("hold_from_confirmation", function()   -- the 10 s dead hold runs from the confirmation, not from the call
	KILL_LAG = 1500
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.250,0" }), 300)
	run(v4({ "k,101,dog_weak,0.250,1" }), 50)
	run(v4({}), 2000)                                                  -- no line after the kill pose: nothing refreshes the hold
	local T, Tc = mnum("\nKDEAD,(%d+),%d+,101,online"), mnum("\nKCONF,(%d+),101,")
	expect(T and Tc and Tc - T >= 1500 and Tc - T < 1600, "KCONF: call " .. tostring(T) .. ", confirmation " .. tostring(Tc) .. " (expected 1500..1599 ms after)")
	if not T then return end
	Tc = Tc or (T + 1550)                                              -- no KCONF: judge the hold from where it should have been
	untilt(Tc + 9800, v4({}))
	expect(has_line(snap(), "k,101,0.250,dead"), "dead line gone 9.8 s after the confirmation (" .. (NOW - T) .. " ms after the call): " .. tostring(kl(snap(), 101)))
	untilt(Tc + 10300, v4({}))
	expect(not string.find(snap(), "\nk,101", 1, true), "dead line still there 10.3 s after the confirmation")
end)

check("r_at_processing", function()   -- point 3: r advances when the pose is processed, before the kill is in effect
	KILL_LAG = 1000
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.250,0" }), 300)
	LAST_SQ = sseq(snap())
	pose(v4({ "k,101,dog_weak,0.250,1" })); local Q = SEQ
	local S = rec(nil, 400)                                            -- the same pose file stays on disk
	expect(#S >= 3, "scenario: snapshots " .. #S)
	for _, s in ipairs(S) do
		expect(string.find(s, "\nr," .. SESS .. "," .. Q .. "\n", 1, true) and kl(s, 101) == "k,101,0.250" and ofield(s, 101, 10) == "1",
			"snapshot " .. sseq(s) .. ": r " .. tostring(string.match(s, "\nr,([^\n]*)")) .. " (expected " .. SESS .. "," .. Q .. "), k " .. tostring(kl(s, 101))) end
	S = rec(v4({ "k,101,dog_weak,0.250,1" }), 100)
	expect(S[#S] and string.find(S[#S], "\nr," .. SESS .. "," .. SEQ .. "\n", 1, true) and OBJ[101].health > 0, "r not at the next pose " .. SEQ .. " while 101 still alive")
end)

------------------------------------------------------------------------------------------------ kj3 checks (kill6: review fixes 1-3)
-- helpers: a kill call on 101 that never takes effect (the original target stays alive at health 0.75)
local function kj3_pending()
	KILL_LAG = false
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.250,0" }), 300)
	run(v4({ "k,101,dog_weak,0.250,1" }), 100)                         -- two frames: one writes a snapshot and flushes the meta file
	expect(OBJ[101].dtime and OBJ[101].health > 0 and mnum("\nKDEAD,(%d+),%d+,101,online"), "scenario: no kill call on 101, or already in effect")
	LAST_SQ = sseq(snap())                                             -- rec() returns only snapshots written from here on
end
local function all_k(S, want, what)   -- every snapshot of S has id 101's k line == want
	expect(#S >= 3, what .. ": scenario: snapshots " .. #S)
	for _, s in ipairs(S) do expect(kl(s, 101) == want, what .. ": snapshot " .. sseq(s) .. ": k line " .. tostring(kl(s, 101)) .. ", expected " .. want) end
end
local function no_conf(what, kon)   -- no KCONF, no KNOCONF, kdead_fail 0 (finish() already called); kon: kdead_online of the last T2SUM (1)
	local s = meta()
	expect(not string.find(s, "\nKCONF,", 1, true), what .. ": KCONF logged: " .. tostring(string.match(s, "\n(KCONF,[^\n]*)")))
	expect(not string.find(s, "\nKNOCONF,", 1, true), what .. ": KNOCONF logged: " .. tostring(string.match(s, "\n(KNOCONF,[^\n]*)")))
	expect(sum("kdead_fail") == 0 and sum("kdead_online") == (kon or 1), what .. ": kdead_fail " .. tostring(sum("kdead_fail")) .. " (expected 0), kdead_online " .. tostring(sum("kdead_online")))
end

check("pend_stale_nofail", function()   -- fix 1, the review's case: pending kill, id reused by a DEAD boar, poses name the id; 3 s
	kj3_pending()
	add(101, "boar_strong", "m", { health = 0 })
	all_k(rec(v4({ "k,101,dog_weak,0.250,1" }), 3000), "k,101,0.250,stale", "dead replacement")
	finish(); no_conf("dead replacement")
	expect(count(meta(), "KSTALE,%d+,%d+,101,") == 1 and count(meta(), "KSTALE,%d+,%d+,101,section") == 1 and sum("kstale") == 1, "KSTALE: one line (section), kstale 1")
end)

-- stage 3 helpers
local function no_k(S, what)   -- no snapshot of S has a k line for id 101
	expect(#S >= 3, what .. ": scenario: snapshots " .. #S)
	for _, s in ipairs(S) do expect(kl(s, 101) == nil, what .. ": snapshot " .. sseq(s) .. ": k line " .. tostring(kl(s, 101)) .. ", expected none") end
end
local function nmeta(pat) return count(meta(), pat) end

check("pend_st_only", function()   -- fix 1; stage 3 (c): the pose's stale mark alone (identity unchanged): stale only while the pose marks it, then dropped
	kj3_pending()
	all_k(rec(v4({ "k,101,dog_strong,0.250,1" }), 3000), "k,101,0.250,stale", "pose says another section")
	expect(not string.find(meta(), "\nKNOCONF,", 1, true), "KNOCONF while the row was stale")
	no_k(rec(v4({}), 1000), "after the line was dropped")               -- stage 3 (c); stage 1 expected the plain line and KNOCONF here
	finish(); no_conf("dropped pending row")
	expect(nmeta("\nKSTALE,%d+,%d+,101,drop,0%.250,pending\n") == 1 and sum("kstale") == 1, "KSTALE drop line once (pending), kstale 1")
end)

check("ident_omitted_pose", function()   -- stage 3 (c), the review's case: stale mark from a pose, then poses omit the id: dropped; the old line again = a new row
	kj3_pending()
	add(101, "boar_strong", "m", { health = 0 })
	run(v4({ "k,101,dog_weak,0.250,1" }), 100)                         -- the pose marks it stale (section); the export sets kx too
	expect(has_line(snap(), "k,101,0.250,stale"), "scenario: not stale while named: " .. tostring(kl(snap(), 101))); LAST_SQ = sseq(snap())
	no_k(rec(v4({}), 1000), "poses omit the id")
	all_k(rec(v4({ "k,101,dog_weak,0.250,1" }), 500), "k,101,0.000,stale", "the old line again")   -- a new row (applied 0), stale while named
	finish(); no_conf("poses omit the id")
	expect(nmeta("\nKSTALE,%d+,%d+,101,drop,0%.250,pending\n") == 1 and nmeta("\nKSTALE,%d+,%d+,101,section,") == 2 and sum("kstale") == 2,
		"KSTALE: drop once, section twice (two rows), kstale 2 (got " .. tostring(sum("kstale")) .. ")")
end)

check("ident_no_pose", function()   -- fix 2 + stage 3 (a): no pose names the id after the call, a dead replacement: no stale line, dropped at the next pose
	kj3_pending()
	add(101, "boar_strong", "m", { health = 0 })
	no_k(rec(v4({}), 3000), "no pose names the id")
	finish(); no_conf("no pose names the id")
	expect(nmeta("\nKSTALE,%d+,%d+,101,ident,dog_weak,boar_strong\n") == 1 and nmeta("\nKSTALE,%d+,%d+,101,drop,0%.250,pending\n") == 1 and nmeta("\nKSTALE,") == 2 and sum("kstale") == 1,
		"KSTALE ident once + drop once, kstale 1: " .. tostring(string.match(meta(), "\n(KSTALE,[^\n]*)")))
end)

check("ident_reuse_alive_other", function()   -- brief + stage 3: a LIVING boar takes the id while the kill is pending
	kj3_pending()
	add(101, "boar_strong", "m")
	all_k(rec(nil, 1000), "k,101,0.250,stale", "living replacement, the kill pose still the newest")   -- only the identity check sees it
	all_k(rec(v4({ "k,101,dog_weak,0.250,1" }), 1000), "k,101,0.250,stale", "living replacement, named")
	no_k(rec(v4({}), 1000), "living replacement, omitted")
	expect(OBJ[101].health == 1 and #OBJ[101].hits == 0 and not OBJ[101].dtime, "the living replacement was hurt or killed")
	finish(); no_conf("living replacement")
end)

check("ident_sticky", function()   -- fix 2 + stage 3: a pose never clears the mismatch; the row is dropped and an emptied id does not confirm the kill
	kj3_pending()
	add(101, "boar_strong", "m")
	no_k(rec(v4({}), 300), "replacement present")
	OBJ[101].released = true
	no_k(rec(v4({}), 1000), "replacement released")
	finish(); no_conf("replacement released")
	expect(nmeta("\nKSTALE,%d+,%d+,101,ident,") == 1 and nmeta("\nKSTALE,%d+,%d+,101,drop,") == 1, "KSTALE ident / drop not once each")
end)

check("ident_removed_dead", function()   -- brief: the target removed (no object) while the kill is pending: confirmed dead, as kill5
	kj3_pending()
	OBJ[101].released = true
	local S = rec(v4({}), 500)
	all_k(S, "k,101,0.250,dead", "removed target")
	finish(); local s = meta()
	local T, Tc, c = mnum("\nKDEAD,(%d+),%d+,101,online"), mnum("\nKCONF,(%d+),101,"), mnum("\nKCONF,%d+,101,(%d+)\n")
	expect(count(s, "\nKCONF,") == 1 and T and Tc and c == Tc - T and S[1] and stx(S[1]) == Tc, "KCONF once in the first snapshot after the removal, ms since the call")
	expect(not string.find(s, "\nKSTALE,", 1, true) and sum("kstale") == 0 and sum("kdead_fail") == 0, "KSTALE / kstale / kdead_fail")
end)

check("ident_reuse_same_section", function()   -- LIMIT pinned: a replacement of the SAME section is taken for the target
	kj3_pending()
	add(101, "dog_weak", "m")                                          -- a new dog_weak under id 101, alive, never touched
	local S = rec(v4({ "k,101,dog_weak,0.250,1" }), 2500)
	all_k(S, "k,101,0.250", "same-section replacement alive")
	expect(not OBJ[101].kills and #OBJ[101].hits == 0, "the replacement was killed or hit by the reader")
	expect(count(meta(), "\nKNOCONF,%d+,101,") == 1, "KNOCONF not logged once for the alive same-section replacement")
	world_kill(101, 7)                                                 -- the replacement dies in X-Ray's own world
	S = rec(v4({ "k,101,dog_weak,0.250,1" }), 200)
	expect(S[1] and kl(S[1], 101) == "k,101,0.250,dead", "replacement dead: " .. tostring(S[1] and kl(S[1], 101)) .. " (the limit: confirmed as the target)")
	finish(); local s = meta()
	expect(count(s, "\nKCONF,%d+,101,") == 1 and (mnum("\nKCONF,%d+,101,(%d+)\n") or 0) > 2000 and sum("kdead_fail") == 1 and not string.find(s, "\nKSTALE,", 1, true),
		"KCONF once (> 2000 ms), kdead_fail 1, no KSTALE")
end)

-- crossing helpers: the kill is pending at the crossing request; 101 stays on l01 (offline on l02)
local function kj3_cross(before)
	kj3_pending()
	if before then before() end
	local r; for _ = 1, 20 do r = run(v4({ "k,101,dog_weak,0.250,1" }, { state = "cross", zone = "l02_garbage", c = "esc_to_gar" }), 50); if r then break end end
	expect(r == "changed" and LEVEL == "l02_garbage", "scenario: no level change")
	return mnum("\nKDEAD,(%d+),%d+,101,online")
end
local function kj3_after(ms) return rec(v4({ "k,101,dog_weak,0.250,1" }, { zone = "l02_garbage" }), ms) end

check("cross_pending", function()   -- fix 3: pending kill over a crossing: no second kill, KNOCONF on the carried clock, later KCONF
	local T = kj3_cross()
	expect(string.find(readf(DIRS.a .. "/ktab.txt"), "\nr,101,0.250000000,-,0,0," .. tostring(T) .. ",0,dog_weak,0,1\n", 1, true), "ktab.txt row lacks the pending fields")   -- stage 3: nm appended (was ,0\n)
	kj3_after(500)
	expect(OBJ[101].kills == 1 and OBJ[101].sekill == 0 and OBJ[101].kill_err == 0, "a second kill after the crossing (kill " .. tostring(OBJ[101].kills) .. ", se:kill " .. OBJ[101].sekill .. ")")
	OBJ[101].health = 0                                                -- the offline target now reads dead
	local S = kj3_after(300)
	expect(S[1] and kl(S[1], 101) == "k,101,0.250,dead", "dead not confirmed after the crossing: " .. tostring(S[1] and kl(S[1], 101)))
	finish(); local s = meta()
	expect(count(s, "\nKDEAD,%d+,%d+,101,") == 1, "KDEAD lines for 101: " .. count(s, "\nKDEAD,%d+,%d+,101,") .. " (expected 1, online)")
	local kn = mnum("\nKNOCONF,%d+,101,(%d+)\n"); local Tn = mnum("\nKNOCONF,(%d+),101,")
	expect(count(s, "\nKNOCONF,") == 1 and kn and Tn and T and kn == Tn - T and kn >= 15000, "KNOCONF once after the resume, ms on the carried clock: " .. tostring(kn))
	local c, Tc = mnum("\nKCONF,%d+,101,(%d+)\n"), mnum("\nKCONF,(%d+),101,")
	expect(count(s, "\nKCONF,") == 1 and c and Tc and T and c == Tc - T, "KCONF once, ms since the call: " .. tostring(c))
end)

check("cross_pending_released", function()   -- fix 3: the target released during the crossing: KCONF after the resume
	local T = kj3_cross(function() OBJ[101].released = true end)
	local S = kj3_after(500)
	expect(S[#S] and kl(S[#S], 101) == "k,101,0.250,dead", "released target after the resume: " .. tostring(S[#S] and kl(S[#S], 101)))
	finish(); local s = meta()
	expect(count(s, "\nKCONF,%d+,101,") == 1 and count(s, "\nKDEAD,%d+,%d+,101,") == 1 and not string.find(s, "\nKNOCONF,", 1, true),
		"KCONF once, one KDEAD (online), no KNOCONF: KCONF " .. count(s, "\nKCONF,") .. ", KDEAD " .. count(s, "\nKDEAD,%d+,%d+,101,"))
end)

check("cross_kn_kept", function()   -- fix 3: a KNOCONF logged before the crossing is not logged again after it
	local T = kj3_cross(function() untilt(NOW + 2500, v4({ "k,101,dog_weak,0.250,1" })) end)
	kj3_after(1000)
	finish(); local s = meta()
	expect(count(s, "\nKNOCONF,%d+,101,") == 1, "KNOCONF lines " .. count(s, "\nKNOCONF,%d+,101,") .. ", expected 1 (before the crossing only)")
end)

check("cross_ident", function()   -- fix 3 + 2: the identity carried over: id reused by a living boar during the crossing
	kj3_cross(function() add(101, "boar_strong", "m", { lvl = "l01_escape" }) end)
	all_k(kj3_after(3000), "k,101,0.250,stale", "reused during the crossing")
	finish(); local s = meta()
	expect(not string.find(s, "\nKCONF,", 1, true) and not string.find(s, "\nKNOCONF,", 1, true) and sum("kdead_fail") == 0, "KCONF / KNOCONF / kdead_fail after the resume")
	expect(count(s, "\nKSTALE,%d+,%d+,101,ident,dog_weak,boar_strong\n") == 1, "KSTALE ident line not once")
end)

check("cross_kx", function()   -- fix 3 + 2, stage 3: a stale identity carried over: stale while named (the id now empty), dropped when not named
	kj3_cross(function() add(101, "boar_strong", "m"); rec(nil, 200); OBJ[101].released = true end)
	all_k(kj3_after(1000), "k,101,0.250,stale", "stale identity after the resume")
	no_k(rec(v4({}, { zone = "l02_garbage" }), 1000), "after the resume, the line dropped")
	finish(); no_conf("stale identity after the resume", 0)   -- the last T2SUM is the new Lua state's: its counters start at 0
end)

check("ktab_legacy_kx", function()   -- review pass 2: a 9-field (stage 1/2) row with kx: stale from the boot snapshot until the first processed pose, then dropped
	local f = real_open(SHM .. "/ktab.txt", "wb"); f:write("run,1\nr,101,0.250000000,-,0,0,9000,0,dog_weak,1\nc,101\ns,", SESS, ",7\nend\n"); f:close()
	f = real_open(SHM .. "/cross.txt", "wb"); f:write("l01_escape,l02_garbage,1795000000350\nx,keep,", SESS, ",7\n"); f:close()
	LEVEL = "l02_garbage"; add(101, "boar_strong", "m"); boot()
	all_k(rec(nil, 600), "k,101,0.250,stale", "legacy identity-stale row, no pose processed yet")
	SEQ = 7; no_k(rec(v4({}, { zone = "l02_garbage" }), 600), "the first processed pose does not name 101")
	finish(); local s = meta()
	expect(string.find(s, "KTAB,read,1,1", 1, true) and nmeta("\nKSTALE,%d+,%d+,101,drop,") == 1, "KTAB read / one drop line")
	no_conf("legacy identity-stale row", 0)
end)
check("ktab_old_format", function()   -- fix 3: a table file written by kill5 (five fields a row) still loads
	local f = real_open(SHM .. "/ktab.txt", "wb"); f:write("run,1\nr,101,0.300000000,-,0,0\nr,102,0.200000000,9000,0,0\nc,101\nc,102\ns,", SESS, ",7\nend\n"); f:close()
	f = real_open(SHM .. "/cross.txt", "wb"); f:write("l01_escape,l02_garbage,1795000000350\nx,keep,", SESS, ",7\n"); f:close()
	LEVEL = "l02_garbage"; add(101, "dog_weak", "m"); add(102, "stalker_bandit", "s", { health = 0 }); boot()
	SEQ = 7; run(v4({ "k,101,dog_weak,0.500,0", "k,102,stalker_bandit,0.200,1" }), 500)
	expect(near(OBJ[101].health, 0.8), "101 not hit from the loaded 0.300 (health " .. OBJ[101].health .. ")")
	expect(not OBJ[102].kills and OBJ[102].kill_err == 0, "102 (dead in the loaded row) killed again")
	finish(); local s = meta()
	expect(string.find(s, "KTAB,read,1,2", 1, true) and string.find(s, "KAPPLY,%d+,%d+,101,0%.200,0%.500,"), "KTAB read / KAPPLY from 0.300")
	expect(not string.find(s, "\nKDEAD,", 1, true), "a KDEAD for a loaded row")
end)

------------------------------------------------------------------------------------------------ kj3 Stage 3 checks (stale rows (a)-(c))
check("st3_a_named", function()   -- (a): the identity stale is written exactly while the newest pose names the id with the old section
	kj3_pending()
	add(101, "boar_strong", "m", { health = 0 })                       -- after the kill pose: only the export's identity check sees it
	all_k(rec(nil, 600), "k,101,0.250,stale", "the newest pose (the kill pose) names 101")
	no_k(rec(v4({}), 600), "the line dropped")
	finish(); no_conf("identity stale")
	expect(nmeta("\nKSTALE,%d+,%d+,101,ident,") == 1 and sum("kstale") == 1, "KSTALE ident once, kstale 1")
end)

check("st3_b_fresh", function()   -- (b): the new creature's section on a row stale by identity starts a fresh row; then its kill works
	kj3_pending()                                                      -- 101 dog_weak: 0.250 applied, kill pending (never in effect)
	add(101, "boar_strong", "m")
	all_k(rec(v4({ "k,101,dog_weak,0.250,1" }), 500), "k,101,0.250,stale", "old line")
	local S = rec(v4({ "k,101,boar_strong,0.200,0" }), 3000)           -- the mod's new line for the boar, at once (no pose without a line between)
	expect(near(OBJ[101].health, 0.8), "boar health " .. OBJ[101].health .. ", expected 0.8 (0.200 applied from 0)")
	all_k(S, "k,101,0.200", "fresh row")
	KILL_LAG = 0
	S = rec(v4({ "k,101,boar_strong,0.500,1" }), 300)
	expect(OBJ[101].kills == 1 and OBJ[101].health <= 0, "the boar not killed")
	expect(S[#S] and kl(S[#S], 101) == "k,101,0.500,dead", "the boar's death: " .. tostring(S[#S] and kl(S[#S], 101)))
	finish(); local s = meta()
	expect(count(s, "\nKSTALE,%d+,%d+,101,fresh,dog_weak,boar_strong\n") == 1, "KSTALE fresh line not once")
	expect(not string.find(s, "\nKNOCONF,", 1, true) and sum("kdead_fail") == 0, "the old pending kill judged after the fresh row (KNOCONF / kdead_fail)")
	expect(count(s, "\nKCONF,%d+,101,") == 1 and sum("kdead_online") == 2 and sum("khits") == 3, "KCONF " .. count(s, "\nKCONF,%d+,101,") ..
		", kdead_online " .. tostring(sum("kdead_online")) .. ", khits " .. tostring(sum("khits")) .. " (want 1, 2, 3)")
end)

check("st3_same_back", function()   -- (b) bound: the pose's section was wrong but the creature is the row's own (ks): no fresh row, the row goes on
	kj3_pending()
	all_k(rec(v4({ "k,101,dog_strong,0.250,1" }), 500), "k,101,0.250,stale", "wrong section")
	all_k(rec(v4({ "k,101,dog_weak,0.250,1" }), 2500), "k,101,0.250", "the right section again")
	finish(); local s = meta()
	expect(OBJ[101].kills == 1 and #OBJ[101].hits == 1 and OBJ[101].kill_err == 0, "a second hit or kill call (hits " .. #OBJ[101].hits .. ", kill errors " .. OBJ[101].kill_err .. ")")
	expect(count(s, "\nKNOCONF,%d+,101,") == 1 and sum("kdead_fail") == 1 and not string.find(s, "\nKSTALE,%d+,%d+,101,fresh,"), "KNOCONF once, kdead_fail 1, no fresh row")
end)

check("st3_c_drop", function()   -- (c): stale rows of both pose kinds are dropped at the first pose without their line; an over-64 pose drops nothing
	add(101, "dog_weak", "m"); add(102, "stalker_bandit", "s"); boot()
	run(v4({ "k,101,dog_weak,0.300,0", "k,102,stalker_bandit,0.300,1" }), 300)   -- 101 hit; 102 killed (in effect at once)
	add(101, "boar_strong", "m")                                       -- 101: the id reused by another section
	run(v4({}), 11000)                                                 -- 102's dead hold runs out
	OBJ[102].health, OBJ[102].dtime, OBJ[102].hits = 1, nil, {}        -- 102 alive again (same section)
	local both = { "k,101,dog_weak,0.300,0", "k,102,stalker_bandit,0.300,1" }
	local S = rec(v4(both), 500)
	expect(#S >= 3 and kl(S[#S], 101) == "k,101,0.300,stale" and kl(S[#S], 102) == "k,102,0.300,stale", "scenario: not both stale: " .. tostring(S[#S] and kl(S[#S], 101)) .. " / " .. tostring(S[#S] and kl(S[#S], 102)))
	local L = { both[1], both[2] }; for i = 1, 63 do L[#L + 1] = string.format("k,%d,dog_weak,0.100,0", 3000 + i) end
	S = rec(v4(L), 300)                                                -- 65 lines: refused whole; rows and marks as they were
	expect(#S >= 2 and kl(S[#S], 101) == "k,101,0.300,stale" and kl(S[#S], 102) == "k,102,0.300,stale", "after an over-64 pose: " .. tostring(S[#S] and kl(S[#S], 101)) .. " / " .. tostring(S[#S] and kl(S[#S], 102)))
	S = rec(v4({}), 1000)
	no_k(S, "101 not named")
	for _, s in ipairs(S) do expect(kl(s, 102) == nil, "102 not named: snapshot " .. sseq(s) .. ": " .. tostring(kl(s, 102))) end
	run(v4({ "k,102,stalker_bandit,0.100,0" }), 300)                   -- a line after the drop is a new creature's: applied from 0
	expect(near(OBJ[102].health, 0.9) and OBJ[101].health == 1 and #OBJ[101].hits == 0, "102 health " .. OBJ[102].health .. " (want 0.9), boar " .. OBJ[101].health)
	finish(); local s = meta()
	expect(count(s, "\nKSTALE,%d+,%d+,101,drop,0%.300,%-\n") == 1 and count(s, "\nKSTALE,%d+,%d+,102,drop,0%.300,%-\n") == 1, "KSTALE drop lines for 101 and 102")
	expect(count(s, "\nKSTALE,%d+,%d+,101,section,") == 1 and count(s, "\nKSTALE,%d+,%d+,102,alive_again,") == 1 and sum("kstale") == 2, "KSTALE section / alive_again once each, kstale 2")
end)

check("st3_live_kept", function()   -- the agreed edge: live rows (applied > 0, alive, the line retired) stay as before; a pending row too
	KILL_LAG = false
	add(101, "dog_weak", "m"); add(103, "dog_weak", "m"); add(104, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.300,0", "k,103,dog_weak,0.200,1", "k,104,dog_strong,0.100,0" }), 300)   -- 101 live; 103 kill pending; 104 stale
	LAST_SQ = sseq(snap())
	local S = rec(v4({}), 3000)
	expect(#S >= 3, "scenario: snapshots " .. #S)
	for _, s in ipairs(S) do expect(kl(s, 101) == "k,101,0.300" and kl(s, 103) == "k,103,0.200" and kl(s, 104) == nil,
		"snapshot " .. sseq(s) .. ": " .. tostring(kl(s, 101)) .. " / " .. tostring(kl(s, 103)) .. " / " .. tostring(kl(s, 104))) end
	run(v4({ "k,101,dog_weak,0.500,0" }), 300)                         -- the mod restarts its total from the applied count
	expect(near(OBJ[101].health, 0.5), "101 health " .. OBJ[101].health .. ", expected 0.5 (0.200 more)")
	finish(); local s = meta()
	expect(count(s, "\nKNOCONF,%d+,103,") == 1 and sum("kdead_fail") == 1, "103: KNOCONF once, kdead_fail 1")
	expect(count(s, "\nKSTALE,%d+,%d+,%d+,drop,") == 1 and count(s, "\nKSTALE,%d+,%d+,104,drop,") == 1, "drop lines: only 104")
end)

check("st3_partner", function()   -- itrzone's case end to end: kill X (A); X becomes B; old line stale; line dropped; B's line hurts B; B's kill works
	add(101, "dog_weak", "m"); boot()
	run(v4({ "k,101,dog_weak,0.300,0" }), 300)
	run(v4({ "k,101,dog_weak,0.300,1" }), 300)                         -- the kill, in effect at once: confirmed
	expect(has_line(snap(), "k,101,0.300,dead"), "scenario: A not answered dead: " .. tostring(kl(snap(), 101))); LAST_SQ = sseq(snap())
	add(101, "boar_strong", "m")                                       -- X's object is now section B
	all_k(rec(v4({ "k,101,dog_weak,0.300,1" }), 1000), "k,101,0.300,stale", "the mod still sends A's line")
	expect(OBJ[101].health == 1 and #OBJ[101].hits == 0, "B hurt by A's line")
	no_k(rec(v4({}), 1000), "the mod dropped its line")
	local S = rec(v4({ "k,101,boar_strong,0.250,0" }), 1000)
	expect(near(OBJ[101].health, 0.75), "B health " .. OBJ[101].health .. ", expected 0.75")
	all_k(S, "k,101,0.250", "B's line")
	S = rec(v4({ "k,101,boar_strong,0.400,1" }), 300)
	expect(OBJ[101].kills == 1 and OBJ[101].health <= 0 and S[#S] and kl(S[#S], 101) == "k,101,0.400,dead", "B not killed / not answered dead: " .. tostring(S[#S] and kl(S[#S], 101)))
	finish(); local s = meta()
	expect(count(s, "\nKDEAD,%d+,%d+,101,online") == 2 and count(s, "\nKCONF,%d+,101,") == 2 and sum("kdead_fail") == 0, "two kills, two confirmations, kdead_fail 0")
	expect(count(s, "\nKWHO,%d+,101,0\n") == 2, "KWHO lines " .. count(s, "\nKWHO,%d+,101,0\n") .. " (want one per creature)")
	expect(count(s, "\nKSTALE,%d+,%d+,101,section,dog_weak,boar_strong\n") == 1 and count(s, "\nKSTALE,%d+,%d+,101,drop,0%.300,%-\n") == 1 and sum("kstale") == 1,
		"KSTALE section once, drop once, kstale 1")
end)

if MODE == "fixture" then   -- STUB-MADE fixtures for the kw1 checker: the real script in this fake world
	local script, dir, scen = arg[2], arg[3], arg[4] or "good"
	if not (script and real_open(script, "rb") and dir) then print("ERROR fixture: usage or script missing"); os.exit(1) end
	SCRIPT_UNDER_TEST = script; new_world(dir, script); CL_EXTRA = "-p1_tag t2"
	add(201, "stalker_bandit", "s", { pos = V(5, 0, 0) }); add(202, "stalker_duty", "s", { pos = V(40, 0, 0) })
	add(301, "dog_weak", "m", { pos = V(0, 0, 8), health = scen == "nomonster" and 0.5 or 1 }); add(302, "boar_strong", "m", { pos = V(0, 0, 3), health = 0.5 })
	local ok, e = pcall(boot); if not ok then print("ERROR fixture: " .. tostring(e)); os.exit(1) end
	print("ready " .. NOW); io.stdout:flush()
	for line in io.lines() do
		local c, a1, a2 = line:match("^(%S+)%s*(%S*)%s*(%S*)$")
		local ok2, e2 = pcall(function()
			if c == "tick" then ticks(50)
			elseif c == "hurt" then OBJ[tonumber(a1)].health = tonumber(a2)
			elseif c == "wkill" then world_kill(tonumber(a1), tonumber(a2))
			elseif c == "lag" then KILL_LAG = tonumber(a1)   -- em1 selftest: a kill call takes effect a1 ms later (pending)
			elseif c == "finish" then finish()
			else error("unknown command " .. tostring(line)) end end)
		if not ok2 then print("ERROR fixture: " .. tostring(e2)); os.exit(1) end
		print("ok " .. NOW); io.stdout:flush()
		if c == "finish" then os.exit(0) end
	end
	print("ERROR fixture: stdin ended without finish"); os.exit(1)
end

------------------------------------------------------------------------------------------------ kj3 Stage 2 checks (emission lines)
-- independent copies of the phase table of kj3/emission-phases.md (phase i when ceil(t) >= X[i])
local SPH = { 0, 30, 47, 50, 80, 108, 120, 156, 168, 200, 212, 220, 222 }
local PPH = { 0, 10, 12, 30, 78, 103 }
local function phx(tab, t) local d, p = math.ceil(t), 0; for i, x in ipairs(tab) do if d >= x then p = i end end; return p end
local function erec(ms)   -- every new snapshot meanwhile, with the game clock at that moment
	local out = {}
	for _ = 1, math.floor(ms / 50) do ticks(50); local s = snap(); local q = sseq(s)
		if q and q ~= LAST_SQ then LAST_SQ = q; local vh = {}; for n, v in pairs(PMF.vortexes) do vh[n] = v.hit end
			out[#out + 1] = { s = s, gs = GAME_MS, now = NOW, vh = vh } end end
	return out end
local function lines(s, pre) local r = {}; for l in string.gmatch(s, "[^\n]+") do if string.sub(l, 1, #pre) == pre then r[#r + 1] = l end end; return r end
local function eline(s) return lines(s, "e,")[1] end
local function et(s) return tonumber(string.match(eline(s) or "", "^e,[^,]+,([^,]+),")) end
local function one_snap() local S = erec(100); return S[#S] and S[#S].s or "" end
local function append(l) local f = real_open(SHM .. "/emtest.txt", "ab"); f:write(l); f:close() end
local function safe(f, ...) local ok, e = pcall(f, ...); expect(ok, "Lua error in the bridge: " .. tostring(e)); return ok end
local function timeline(kind, tab, secs)   -- a whole event at the scripts' own pace; every snapshot judged
	boot(); erec(1000)
	if kind == "surge" then surge_manager.start_surge() else psi_storm_manager.start_psi_storm() end
	local g0 = GAME_MS; local S = erec((secs + 5) * 1000)
	local seen_end, bad, n = false, 0, 0
	local M = kind == "surge" and surge_manager.SurgeManager or psi_storm_manager.PsiStormManager
	for _, x in ipairs(S) do local e = eline(x.s)
		if string.find(e, "^e,none,") then seen_end = true; if e ~= "e,none,0.00,0,6" then bad = bad + 1 end
		else n = n + 1; local t = (x.gs - g0) / 1000 / 10
			local want = string.format("e,%s,%.2f,%d,10", kind, t, phx(tab, t))
			if e ~= want then bad = bad + 1; if bad == 1 then expect(false, "snapshot e line " .. e .. " want " .. want) end end
			if seen_end then expect(false, "event line after the end") end end end
	expect(seen_end, kind .. " never ended")
	expect(n >= secs * 10 - 5, kind .. ": only " .. n .. " event snapshots")
	expect(bad == 0, kind .. ": " .. bad .. " wrong e lines")
	-- one-second steps: at every whole second 0..secs-1 one snapshot exists with that t
	local have = {}; for _, x in ipairs(S) do local t = et(x.s); if t then have[string.format("%.2f", t)] = true end end
	for k = 0, secs - 1 do if not have[string.format("%.2f", k)] then expect(false, kind .. ": no snapshot at t = " .. k); break end end
	return S
end
check("em_v4_every", function()
	boot(); local S = rec({}, 1000)   -- no kill: no r line (the e lines do not depend on it)
	surge_manager.start_surge(); for _, x in ipairs(rec(nil, 3000)) do S[#S + 1] = x end
	expect(#S >= 30, "too few snapshots")
	for _, s in ipairs(S) do
		if #lines(s, "e,") ~= 1 or #lines(s, "ns,") ~= 1 then expect(false, "snapshot without exactly one e and one ns line: " .. s); break end end
end)
check("em_absent_v3", function()   -- a T1 snapshot (-b1_snap, no -t2_pose; header without the version field) gets no e line
	CL_BASE = "-p1 -b1_snap "
	local f = real_open(SHM .. "/p1_track.csv", "wb"); f:write("t,x,y,z,nd\n0,0,0,0,1\n1000,0,0,0,1\n"); f:close()
	boot(); surge_manager.start_surge(); local S = rec(nil, 2000)
	expect(#S >= 10, "no T1 snapshots (" .. #S .. ")")
	for _, s in ipairs(S) do
		expect(string.find(s, "^seq,%d+,%d+,[%-%d%.]+\n"), "not a T1 header: " .. string.sub(s, 1, 40))
		if string.find(s, "\ne,") or string.find(s, "\nns,") then expect(false, "e/ns line in a T1 snapshot"); break end end
end)
check("em_none_exact", function()
	boot(); local S = erec(2000)
	for _, x in ipairs(S) do local e = eline(x.s)
		if e ~= "e,none,0.00,0,6" then expect(false, "outside an event: " .. tostring(e)); break end end
	TF = 1; local s = one_snap(); expect(eline(s) == "e,none,0.00,0,1", "factor 1 not printed: " .. tostring(eline(s)))
end)
check("em_surge_timeline", function() local S = timeline("surge", SPH, 222)
	local last; for _, x in ipairs(S) do if string.find(x.s, "\ne,surge,") then last = x.s end end
	expect(et(last or "") and et(last) > 221 and phx(SPH, et(last)) == 13, "last surge snapshot not in phase 13 past 221 s")
end)
check("em_psi_timeline", function() timeline("psi", PPH, 103) end)
local function bounds(kind, tab)
	boot(); SIM_OFF, GS_FROZEN, GAME_MS, TF = true, true, 100000000, 10
	local M = kind == "surge" and surge_manager.SurgeManager or psi_storm_manager.PsiStormManager
	M.started = true
	for i, x in ipairs(tab) do
		for _, c in ipairs({ { x - 1, i - 1 }, { x - 0.99, i }, { x, i }, { x + 0.5, (tab[i + 1] and tab[i + 1] <= x + 1) and i + 1 or i } }) do
			M.inited_time = CT(GAME_MS - math.floor(c[1] * 10000 + 0.5))
			local want = string.format("e,%s,%.2f,%d,10", kind, c[1], c[2]); local e = eline(one_snap())
			expect(e == want, "threshold " .. x .. ": " .. tostring(e) .. " want " .. want) end end
end
check("em_surge_bounds", function() bounds("surge", SPH) end)
check("em_psi_bounds", function() bounds("psi", PPH) end)
check("em_waves", function()
	boot(); surge_manager.start_surge(); local g0 = GAME_MS; local S = erec(225000)
	local first, last, both, bad = {}, {}, 0, 0
	for _, x in ipairs(S) do local W = lines(x.s, "w,"); local t = et(x.s)
		if #W == 2 then both = both + 1 end
		for _, l in ipairs(W) do local n, z = string.match(l, "^w,(%d),([%-%d%.]+)$"); n = tonumber(n)
			local wv = surge_manager.SurgeManager.blowout_waves[n]
			local want = wv and string.format("w,%d,%.3f", n, wv.inited_pos.z - (math.ceil((x.gs - g0) / 1000 / 2) - wv.inited_time) * 3)
			if l ~= want then bad = bad + 1; if bad == 1 then expect(false, "wave line " .. l .. " want " .. tostring(want)) end end
			first[n] = first[n] or t; last[n] = t end end
	expect(first[1] and first[1] > 119 and first[1] <= 121, "wave 1 first at " .. tostring(first[1]))
	expect(first[2] and first[2] > 167 and first[2] <= 169, "wave 2 first at " .. tostring(first[2]))
	expect(last[1] and last[1] > 150 and last[1] < 156, "wave 1 last at " .. tostring(last[1]))
	expect(last[2] and last[2] > 198 and last[2] < 204, "wave 2 last at " .. tostring(last[2]))
	expect(both == 0, "both waves listed together " .. both .. " times (they do not overlap)")
	-- a wave is listed exactly while its effect plays: count the snapshots without the line while it plays
	expect(bad == 0, bad .. " wrong wave lines")
end)
check("em_vortex", function()
	boot(); psi_storm_manager.start_psi_storm(); local S = erec(105000)
	local pos, firstsnap, lastS, gone, bad, maxp = {}, {}, {}, {}, 0, 0
	for k, x in ipairs(S) do local Vl = lines(x.s, "v,"); local t = et(x.s); if #Vl > maxp then maxp = #Vl end
		local newn = 0
		for _, l in ipairs(Vl) do local n, xyz, sv = string.match(l, "^v,(%d+),([^,]+,[^,]+,[^,]+),([%-%d%.]+)$"); n = tonumber(n)
			local v = psi_storm_manager.PsiStormManager.vortexes[n]
			if not v then bad = bad + 1 else
				local want = string.format("%.2f", v.start_time + 20 - t)
				if sv ~= want then bad = bad + 1; if bad == 1 then expect(false, "vortex " .. n .. " s " .. sv .. " want " .. want) end end
				if pos[n] and pos[n] ~= xyz then expect(false, "vortex " .. n .. " moved") end; pos[n] = xyz
				if gone[n] then expect(false, "vortex " .. n .. " listed again") end
				if lastS[n] and tonumber(sv) > lastS[n] then expect(false, "vortex " .. n .. " s rose") end; lastS[n] = tonumber(sv)
				if x.vh[n] ~= false then expect(false, "vortex " .. n .. " listed after striking") end
				if not firstsnap[n] then firstsnap[n] = k; newn = newn + 1 end end end
		if newn > 1 then expect(false, newn .. " vortices appeared in one snapshot") end
		for n in pairs(pos) do local hit = true; for _, l in ipairs(Vl) do if string.match(l, "^v,(%d+),") == tostring(n) then hit = false end end; if hit then gone[n] = true end end
	end
	local N = psi_storm_manager.PsiStormManager.vortex_index; local listed = 0; for _ in pairs(pos) do listed = listed + 1 end
	expect(N >= 8 and listed == N, "vortices created " .. N .. ", listed " .. listed)
	expect(maxp >= 3, "never 3 pending at once (" .. maxp .. ")")
	expect(bad == 0, bad .. " wrong vortex lines")
end)
check("em_ns", function()
	boot(); local S = erec(1500)
	for _, x in ipairs(S) do local gs = x.gs / 1000
		local want = string.format("ns,%d,%d", math.max(0, math.floor(50000 - gs)), math.max(0, math.floor(90000 - gs)))
		local l = lines(x.s, "ns,")[1]; if l ~= want then expect(false, "idle " .. tostring(l) .. " want " .. want); break end end
	surge_manager.start_surge(); local x = erec(200); x = x[#x]
	expect(lines(x.s, "ns,")[1] == string.format("ns,-1,%d", math.floor(90000 - x.gs / 1000)), "surge running: " .. tostring(lines(x.s, "ns,")[1]))
	SMF.started = false; psi_storm_manager.start_psi_storm(); x = erec(200); x = x[#x]
	expect(lines(x.s, "ns,")[1] == string.format("ns,%d,-1", math.max(0, math.floor(50000 - x.gs / 1000))), "psi running: " .. tostring(lines(x.s, "ns,")[1]))
	PMF.started = false; SMF._delta = 1; x = erec(200); x = x[#x]
	expect(string.find(lines(x.s, "ns,")[1] or "", "^ns,0,"), "a start overdue is not 0: " .. tostring(lines(x.s, "ns,")[1]))
end)
local function degraded_ok(S, what)
	expect(#S > 0, what .. ": no snapshot")
	for _, x in ipairs(S) do local s = x.s or x
		if #lines(s, "e,") ~= 1 or eline(s) ~= "e,none,0.00,0," .. tostring(TF) or #lines(s, "ns,") ~= 1 or lines(s, "ns,")[1] ~= "ns,-1,-1"
			or #lines(s, "w,") > 0 or #lines(s, "v,") > 0 then expect(false, what .. ": not degraded: " .. s); return end end end
check("em_degraded_nil", function()
	boot(); erec(200); local nd = 0
	safe(function()
		local keep = surge_manager; surge_manager = nil; local S = erec(1000); degraded_ok(S, "surge_manager nil"); nd = nd + #S; surge_manager = keep
		keep = psi_storm_manager.PsiStormManager; psi_storm_manager.PsiStormManager = nil; S = erec(1000); degraded_ok(S, "PsiStormManager nil"); nd = nd + #S
		psi_storm_manager.PsiStormManager = keep; S = erec(500); expect(eline(S[#S].s) == "e,none,0.00,0,6", "not back after the manager came back")
	end)
	finish(); expect(count(meta(), "\nEERR,") == 1, "EERR lines: " .. count(meta(), "\nEERR,")); expect(sum("eerr") == nd, "eerr " .. tostring(sum("eerr")) .. " want " .. nd)
end)
check("em_degraded_field", function()
	boot(); erec(200); local nd = 0
	safe(function()
		SMF._delta = nil; local S = erec(1000); degraded_ok(S, "_delta missing"); nd = nd + #S; SMF._delta = 50000
		pm_start(); PMF.vortex_index = 1; PMF.vortexes = setmetatable({}, { __index = function() error("vortex accessor boom") end })
		S = erec(1000); degraded_ok(S, "vortex accessor throws"); nd = nd + #S; PMF.started = false; PMF.vortexes = {}; PMF.vortex_index = 0
		sm_start(); SMF.blowout_waves[1] = { effect = { playing = function() error("playing boom") end }, inited_time = 0, inited_pos = V(0, 0, 250) }
		SIM_OFF = true; S = erec(1000); degraded_ok(S, "effect:playing throws"); nd = nd + #S
		SMF.inited_time = nil; S = erec(500); degraded_ok(S, "inited_time missing"); nd = nd + #S
	end)
	finish(); expect(count(meta(), "\nEERR,") == 1, "EERR lines: " .. count(meta(), "\nEERR,")); expect(sum("eerr") == nd, "eerr " .. tostring(sum("eerr")) .. " want " .. nd)
end)
check("em_order", function()
	add(101, "dog_weak", "m"); boot(); run(v4({ "k,101,dog_weak,0.100,0" }), 300)
	local function order(s, want) local seq = {}
		for l in string.gmatch(s, "[^\n]+") do local c = string.match(l, "^(%a+),"); if c and c ~= "o" and c ~= "a" and c ~= "seq" and c ~= "end" and seq[#seq] ~= c then seq[#seq + 1] = c end end
		expect(table.concat(seq, " ") == want, "order " .. table.concat(seq, " ") .. " want " .. want) end
	surge_manager.start_surge(); SMF.inited_time = CT(GAME_MS - 1240000); local S = rec(nil, 1500); order(S[#S], "r e w ns k")
	SMF.started = false; TF = 6; psi_storm_manager.start_psi_storm(); PMF.inited_time = CT(GAME_MS - 400000); S = rec(nil, 8000); order(S[#S], "r e v ns k")
end)
check("em_force_absent", function()
	boot(); local o0 = EM_OPENS; rec(nil, 3000)
	append("surge\n"); rec(nil, 3000)   -- appears after game start: the test mode stays off
	expect(o0 == 1, "game start probes emtest.txt " .. o0 .. " times (want 1)")
	expect(EM_OPENS == o0, "emtest.txt opened " .. (EM_OPENS - o0) .. " times after game start without the file")
	expect(EMC.surge == 0 and EMC.psi == 0, "a start function was called")
	finish(); expect(not string.find(meta(), "EFORCE", 1, true), "EFORCE logged")
end)
check("em_force_lines", function()
	append(""); boot(); local o0 = EM_OPENS
	append("surge\n"); rec(nil, 1500)
	expect(EMC.surge == 1, "surge start calls " .. EMC.surge)
	expect(count(meta(), "\nEFORCE,%d+,surge,started\n") == 1, "EFORCE surge line")
	local o1 = EM_OPENS; rec(nil, 3000)
	expect(EMC.surge == 1, "the same surge line acted on again (" .. EMC.surge .. ")")
	expect(EM_OPENS - o1 <= 4, "emtest.txt read " .. (EM_OPENS - o1) .. " times in 3 s")
	append("bogus\npsi\n"); rec(nil, 1500)
	expect(EMC.psi == 1 and EMC.surge == 1, "psi line: calls surge " .. EMC.surge .. " psi " .. EMC.psi)
	expect(count(meta(), "\nEFORCE,%d+,psi,started\n") == 1 and count(meta(), "\nEFORCE,%d+,bogus,unknown\n") == 1, "EFORCE psi / unknown lines")
	append("sur"); rec(nil, 2000); expect(EMC.surge == 1, "an incomplete line acted on")
	append("ge\n"); rec(nil, 1500); expect(EMC.surge == 2, "the completed second surge line not acted on")
end)
check("em_force_throw", function()
	append(""); boot(); EM_START_ERR = "start boom"
	safe(function() append("surge\n"); rec(nil, 1500); append("psi\n"); rec(nil, 1500) end)
	local S = rec(nil, 500); expect(#S > 0, "no snapshot after the throw")
	expect(EMC.surge == 1 and EMC.psi == 1, "calls " .. EMC.surge .. "/" .. EMC.psi)
	expect(count(meta(), "\nEFORCE,%d+,surge,error:start boom\n") == 1 and count(meta(), "\nEFORCE,%d+,psi,error:start boom\n") == 1, "EFORCE error lines")
end)
check("em_meta_once", function()
	local S = timeline("surge", SPH, 222); finish(); local s = meta()
	expect(count(s, "\nESTART,%d+,surge,") == 1 and count(s, "\nEEND,%d+,surge,") == 1, "ESTART/EEND not once")
	local ph = {}; for p in string.gmatch(s, "\nEPHASE,%d+,surge,(%d+),") do ph[#ph + 1] = p end
	expect(table.concat(ph, " ") == "1 2 3 4 5 6 7 8 9 10 11 12 13", "EPHASE sequence " .. table.concat(ph, " "))
	local lt; for _, x in ipairs(S) do if string.find(x.s, "\ne,surge,") then lt = et(x.s) end end
	expect(string.find(s, string.format("\nEEND,%%d+,surge,%.2f\n", lt or -1)), "EEND t not the last t seen (" .. tostring(lt) .. ")")
	local st = string.match(s, "\nESTART,%d+,surge,([%d%.]+)\n"); expect(st and tonumber(st) < 0.2, "ESTART t " .. tostring(st))
end)
check("em_jump", function()   -- +20 s (a stop without the absorb): skipped phases are logged one by one, in order, at one time
	boot(); surge_manager.start_surge(); SMF.inited_time = CT(GAME_MS - 1000000); erec(500)
	NOW = NOW + 19950; GAME_MS = GAME_MS + 199500; erec(50); erec(500); finish()
	local s = meta(); local L = {}; for a, p, t in string.gmatch(s, "\nEPHASE,(%d+),surge,(%d+),([%d%.]+)") do L[#L + 1] = { a, p, t } end
	local n = #L; expect(n >= 3, "EPHASE lines " .. n)
	if n >= 3 then expect(L[n - 1][2] == "6" and L[n][2] == "7" and L[n - 2][2] == "5", "phases around the jump " .. L[n - 2][2] .. " " .. L[n - 1][2] .. " " .. L[n][2])
		expect(L[n - 1][1] == L[n][1] and L[n - 1][3] == L[n][3] and tonumber(L[n][3]) >= 120, "skipped phases not logged together at the jump") end
end)
check("em_backwards", function()   -- a restart (start_surge during an emission, or a new Lua state) puts t back
	boot(); surge_manager.start_surge(); SMF.inited_time = CT(GAME_MS - 1500000); erec(500)
	safe(function() surge_manager.start_surge(); erec(1000) end)   -- (a new Lua state starts a new meta file: nothing to carry)
	finish(); local s = meta()
	local e1 = string.match(s, "\nEEND,%d+,surge,([%d%.]+)\n"); expect(e1 and tonumber(e1) >= 150, "no EEND at the restart (" .. tostring(e1) .. ")")
	expect(count(s, "\nESTART,%d+,surge,") == 2, "ESTART count " .. count(s, "\nESTART,%d+,surge,"))
	expect(string.find(s, "\nEEND,%d+,surge,[%d%.]+\nESTART,%d+,surge,0%.%d%d\nEPHASE,%d+,surge,1,0%.%d%d\n"), "restart not EEND, ESTART, EPHASE 1")
	expect(not string.find(s, "\nEPHASE,%d+,surge,1[^\n]*\nEPHASE,%d+,surge,0,"), "phase 0 logged")
end)
check("em_notready", function()   -- review 1: managers nil before the first good read: degraded lines, not counted; after it, counted
	surge_manager.SurgeManager = nil; psi_storm_manager.PsiStormManager = nil   -- a new game: the scripts create them later
	local nd = 0
	safe(function()
		boot(); local S = erec(1000); degraded_ok(S, "not ready")
		surge_manager.SurgeManager = SMF; S = erec(500); degraded_ok(S, "half ready (psi nil)")
		psi_storm_manager.PsiStormManager = PMF; S = erec(500); expect(S[#S] and eline(S[#S].s) == "e,none,0.00,0,6", "not normal once ready")
		surge_manager.SurgeManager = nil; S = erec(500); degraded_ok(S, "nil after the first good read"); nd = #S; surge_manager.SurgeManager = SMF
	end)
	finish(); local s = meta()
	expect(nd > 0 and sum("eerr") == nd, "eerr " .. tostring(sum("eerr")) .. " want " .. nd .. " (only the snapshots after the first good read)")
	expect(count(s, "\nENOTREADY,") == 1 and count(s, "\nEREADY,") == 1 and count(s, "\nEERR,") == 1, "ENOTREADY / EREADY / EERR not once each")
	expect(string.find(s, "\nENOTREADY,%d+\n.-\nEREADY,%d+\n.-\nEERR,"), "order not ENOTREADY, EREADY, EERR")
end)
check("em_notready_tf", function()   -- review pass 2: not ready only when the failure IS a nil manager; a bad factor at the same moment counts
	surge_manager.SurgeManager = nil; psi_storm_manager.PsiStormManager = nil   -- a new game: the scripts create them later
	local nd = 0
	safe(function()
		boot(); degraded_ok(erec(200), "managers nil (not ready)"); TF = 0; local S = erec(1000); degraded_ok(S, "factor 0, managers nil"); nd = #S
		TF = 6; S = erec(500); degraded_ok(S, "factor good, managers nil (not ready)")
	end)
	finish(); local s = meta()
	expect(nd > 0 and sum("eerr") == nd, "eerr " .. tostring(sum("eerr")) .. " want " .. nd .. " (the factor-0 snapshots only)")
	expect(count(s, "\nEERR,%d+,time factor unreadable\n") == 1, "EERR for the factor not logged once")
	expect(count(s, "\nENOTREADY,") == 1 and count(s, "\nEREADY,") == 0, "ENOTREADY not once (or EREADY logged)")
end)
check("em_force_cross", function()   -- review 2: a line appended during the level load is acted on after it; another run id or a fresh process skips old lines
	local function crossing()   -- l01 -> l02; `psi` is appended while the level loads
		local cl = ChangeLevel; ChangeLevel = function(...) append("psi\n"); return cl(...) end
		local r; for _ = 1, 20 do r = run(v4({}, { state = "cross", zone = "l02_garbage", c = "esc_to_gar" }), 50); if r then break end end
		ChangeLevel = cl; expect(r == "changed", "scenario: no level change")
	end
	append(""); boot(); append("surge\n"); rec(nil, 1500)
	expect(EMC.surge == 1, "scenario: surge not started before the crossing")
	crossing(); rec(v4({}, { zone = "l02_garbage" }), 2000)
	expect(EMC.psi == 1 and EMC.surge == 1, "same run: calls surge " .. EMC.surge .. " psi " .. EMC.psi .. " (want 1, 1)")
	expect(string.find(readf(DIRS.a .. "/ktab.txt"), "\ne,1\n", 1, true), "ktab.txt lacks the cursor e,1")
	finish(); expect(count(meta(), "\nEFORCE,%d+,psi,started\n") == 1, "EFORCE psi after the crossing not once")
	new_world(DIRS.b, SCRIPT_UNDER_TEST, "1"); append(""); boot(); append("surge\n"); rec(nil, 1500)
	RUN = "2"; crossing(); rec(v4({}, { zone = "l02_garbage" }), 2000)   -- the boot after the change sees another run id
	expect(EMC.psi == 0 and EMC.surge == 1, "other run id: calls surge " .. EMC.surge .. " psi " .. EMC.psi .. " (want 1, 0)")
	new_world(DIRS.c, SCRIPT_UNDER_TEST, "1"); append("surge\npsi\n"); boot(); rec(nil, 2000)
	expect(EMC.surge == 0 and EMC.psi == 0, "a fresh process acted on lines already in the file")
end)
if MODE == "examples" then   -- lua5.1 kill6h_stub.lua examples <script> <dir>: the fixture text for kj3/emission-line-examples.txt
	SCRIPT_UNDER_TEST = arg[2]; new_world(arg[3], arg[2]); F = {}
	add(101, "dog_weak", "m"); boot(); run(v4({ "k,101,dog_weak,0.100,0" }), 300)
	local function show(cap, s) io.write("## ", cap, "\n", s, "\n") end
	show("(1) no event (factor 6)", one_snap())
	surge_manager.start_surge()
	local function walk(targets, kind)
		local i = 1
		while targets[i] do local x = erec(50)[1]
			if x and et(x.s) and string.find(x.s, "\ne," .. kind .. ",") and et(x.s) >= targets[i][1] - 1e-9 then show(targets[i][2] .. " (t = " .. string.format("%.2f", et(x.s)) .. ")", x.s); i = i + 1 end end end
	walk({ { 10, "(2a) emission, t = 10" }, { 125, "(2b) emission, wave 1 active" }, { 170, "(2c) emission, wave 2 only (the waves do not overlap: wave 1 stops near t = 153)" }, { 221, "(2d) emission, last second" } }, "surge")
	erec(3000); psi_storm_manager.start_psi_storm()
	walk({ { 40, "(3a) psi storm with pending vortices" }, { 60, "(3b) psi storm with pending vortices" } }, "psi")
	erec(50000); surge_manager = nil; show("(4) degraded (surge_manager missing; factor 6)", one_snap())
	os.exit(0)
end

if MODE == "checks" then for _, n in ipairs(ORDER) do print(n) end; os.exit(0) end
if MODE ~= "run" then print("usage: see the header"); os.exit(2) end
SCRIPT_UNDER_TEST = arg[2]
local name, wd = arg[3], arg[4]
local ARGS = { lost3 = arg[5] }
if not C[name] then print("ERROR " .. tostring(name) .. ": no such check"); os.exit(1) end
if not real_open(SCRIPT_UNDER_TEST, "rb") then print("ERROR " .. name .. ": script missing " .. tostring(SCRIPT_UNDER_TEST)); os.exit(1) end
if name == "v3_unchanged" and not (ARGS.lost3 and real_open(ARGS.lost3, "rb")) then print("ERROR " .. name .. ": lost3 script missing"); os.exit(1) end
DIRS = { a = wd .. "/a", b = wd .. "/b", c = wd .. "/c" }
for _, d in pairs(DIRS) do if not real_open(d .. "/.keep", "rb") then print("ERROR " .. name .. ": workdir " .. d .. " not prepared"); os.exit(1) end end
F = {}
new_world(DIRS.a, SCRIPT_UNDER_TEST)
local ok, e = pcall(C[name], ARGS)
if not ok then print("ERROR " .. name .. ": exception: " .. tostring(e) .. (#F > 0 and (" (after: " .. table.concat(F, "; ") .. ")") or "")); os.exit(1) end
if #F == 0 then print("PASS " .. name) else print("FAIL " .. name .. ": " .. table.concat(F, "; ")); os.exit(1) end
