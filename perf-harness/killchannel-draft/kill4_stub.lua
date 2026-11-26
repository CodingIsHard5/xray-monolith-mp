-- Offline test of the pose v4 / kill channel bridge script (harness/zz_p1_puppet_kill4.script).
-- Direct runs need the workspace kill4_test.sh creates (a workdir with empty subdirectories a, b, c, each with a .keep file).
-- Runs the REAL script under plain Lua 5.1 with faked engine objects (modelled on armD-draft/armD_stub.lua) and checks
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
	{ "m_v3_unchanged", "v3_unchanged", 'if nc >= 7 then if nc == 7', 'if nc >= 6 then if nc == 7' },
	{ "m_v3_klines", "v3_klines_not_applied", 'if not v4 then for _, l in ipairs(lines)', 'if false then for _, l in ipairs(lines)' },
	{ "m_bare_k", "bare_k_refused", 'if not id then return kref(now, n, string.match(line, "^k,([^,]*)") or "?", "form") end',
		'if not id then local o0 = level.object_by_id(tonumber(string.match(line, "^k,(%d+)$") or "-1")); if o0 and o0:alive() then o0:kill(a) end; return kref(now, n, string.match(line, "^k,([^,]*)") or "?", "form") end' },
	{ "m_bad_version", "bad_version_refused", 'string.sub(first, -2) == ",4"', 'true' },
	{ "m_repeat", "repeated_reads", 'r.ap = tot; kc.khits', 'kc.khits' },
	{ "m_part_hit", "part_hit", 'local hn = hb - d;', 'local hn = hb - tot;' },
	{ "m_stop001", "stop_at_001", 'math.min(hb, 0.01)', 'math.min(hb, 0)' },
	{ "m_death_online", "death_online", 'if dead == "1" then o:kill(a); r.dd = now;', 'if dead == "1" then r.dd = now;' },
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
	{ "m_snap_hold", "snapshot_lines", '(r.dd and now - r.dd <= K_HOLD)', '(r.dd and now - r.dd <= 3 * K_HOLD)' },
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
}

if MODE == "mutants" then for _, mu in ipairs(MUTANTS) do print(mu[1] .. " " .. mu[2]) end; os.exit(0) end
if MODE == "mkmutant" then
	local name, fin, fout = arg[2], arg[3], arg[4]
	local mu; for _, x in ipairs(MUTANTS) do if x[1] == name then mu = x end end
	if not mu then print("NO MUTANT " .. tostring(name)); os.exit(1) end
	local f = real_open(fin, "rb"); if not f then print("NO SCRIPT " .. tostring(fin)); os.exit(1) end
	local s = f:read("*a"); f:close()
	local a = string.find(s, mu[3], 1, true)
	if not a then print("PATCH NOT FOUND " .. name); os.exit(1) end
	if string.find(s, mu[3], a + 1, true) then print("PATCH NOT UNIQUE " .. name); os.exit(1) end
	local o = s:sub(1, a - 1) .. mu[4] .. s:sub(a + #mu[3])
	if o == s then print("PATCH CHANGED NOTHING " .. name); os.exit(1) end
	local g = real_open(fout, "wb"); if not g then print("CANNOT WRITE " .. tostring(fout)); os.exit(1) end
	local okw = g:write(o); local okc = g:close()
	if not (okw and okc) then print("CANNOT WRITE " .. tostring(fout)); os.exit(1) end
	print("MUTANT " .. name .. " written"); os.exit(0)
end

------------------------------------------------------------------------------------------------ fake engine
local SHM, NOW, LEVEL, RUN, CL_EXTRA = nil, 0, "l01_escape", "1", ""
local CB, OBJ, ACT, CHANGED
local DEATHQ = {}   -- deaths whose death callback fires at the start of the next frame (the GE_DIE event is handled later)
local function V(x, y, z) local v = { x = x or 0, y = y or 0, z = z or 0 }
	function v:set(a, b, c) self.x, self.y, self.z = a, b, c; return self end
	function v:distance_to(o) return math.sqrt((self.x - o.x) ^ 2 + (self.y - o.y) ^ 2 + (self.z - o.z) ^ 2) end
	function v:normalize() local l = math.sqrt(self.x ^ 2 + self.y ^ 2 + self.z ^ 2); if l > 1e-9 then self.x, self.y, self.z = self.x / l, self.y / l, self.z / l end; return self end
	return v end
local KTAB_WFAIL = false   -- ktab_savefail: the ktab.txt handle opens, but every write fails (as a full disk)
io.open = function(p, m) p = p:gsub("^Z:\\dev\\shm\\itrz_b1\\", SHM .. "/")
	if KTAB_WFAIL and p:find("/ktab%.txt$") and m == "wb" then return { write = function() return nil, "disk full" end, close = function() return true end } end
	return real_open(p, m) end
function time_global() return NOW end
os.time = function() return 1795000000 + math.floor(NOW / 1000) end   -- wall clock follows the fake time: logs are reproducible
function command_line() return "-p1 -t2_pose -t2_run " .. RUN .. " -t2_lost 5000 -t2_lostx 120000 " .. CL_EXTRA end
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
	function o:kill(who) if R.dtime then R.kill_err = R.kill_err + 1 else R.dtime = NOW; R.killer = who and who:id() or -1; R.health = 0
		DEATHQ[#DEATHQ + 1] = { id = id, who = who } end end
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
		local stripped = string.gsub(m4, "(T2SUM,[^\n]-),khits=%d+,kdead_online=%d+,kdead_offline=%d+,kowed=%d+,kstale=%d+,kback=%d+,krefused=%d+,kclamped=%d+,kver=0,kdead_fail=0,ktabfail=0", "%1")
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
	expect(has_line(snap(), "k,101,0.000,dead"), "stale 101 not answered dead")
	run(v4({}), 300)
	expect(not string.find(snap(), "\nk,101", 1, true), "dead answer kept after the line was dropped")
	-- death confirmed more than 10 s ago, id alive again (same section)
	run(v4({ "k,102,stalker_bandit,0.300,1" }), 200)
	expect(OBJ[102].dtime, "102 not killed")
	run(v4({}), 11000)
	OBJ[102].health, OBJ[102].dtime, OBJ[102].hits = 1, nil, {}
	run(v4({ "k,102,stalker_bandit,0.500,0" }), 500)
	expect(OBJ[102].health == 1 and #OBJ[102].hits == 0, "reused id 102 hurt")
	expect(has_line(snap(), "k,102,0.300,dead"), "stale 102 not answered dead")
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
	expect(string.find(s, "\no,[^\n]*\nr,[^\n]*\nk,101,0%.250\nend,%d+\n$"), "k line not after the o and r lines and before end")
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
	expect(string.find(snap(), "\no,[^\n]*\nr," .. SESS .. "," .. SEQ .. "\nk,101,0%.250\nend,%d+\n$"), "r line not between the o lines and the k lines")
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
		expect(t and string.find(t, ",ktabfail=1$"), how .. ": ktabfail not 1 at the end of the first T2SUM: " .. tostring(t))
	end
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
			elseif c == "finish" then finish()
			else error("unknown command " .. tostring(line)) end end)
		if not ok2 then print("ERROR fixture: " .. tostring(e2)); os.exit(1) end
		print("ok " .. NOW); io.stdout:flush()
		if c == "finish" then os.exit(0) end
	end
	print("ERROR fixture: stdin ended without finish"); os.exit(1)
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
