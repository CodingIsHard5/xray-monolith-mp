-- Offline test of the new on-mesh test: REAL zz_p1_puppet.script, REAL level.ai nodes (dumped by python to cells.txt),
-- REAL track sampled at 20 Hz. level.vertex_id is modelled like CLevelGraph::vertex_id (cell-centre heights).
-- T3 variant: arg 7 = mode (v1 | v2 | v2:<yfloor offset m> | abs:<yfloor> | throw), arg 8 = stance export present (1/0)
local SHM, SCRIPT, CELLS, TRACK, T0, T1 = arg[1], arg[2], arg[3], arg[4], tonumber(arg[5]), tonumber(arg[6])
local MODE, HAS_STANCE = arg[7] or "v1", arg[8] == "1"
local OFF = tonumber(MODE:match("^v2:(.+)$") or "0"); local ABS = tonumber(MODE:match("^abs:(.+)$")); local V2 = MODE ~= "v1"; local THROW = MODE == "throw"; local NSET, STANCE, COLL = 0, nil, 0
local real_open = io.open
io.open = function(p, m) p = p:gsub("^Z:\\dev\\shm\\itrz_b1\\", SHM .. "/"); return real_open(p, m) end
local hdr; local cells, ids, idy = {}, 0, {}
for l in io.lines(CELLS) do
  if not hdr then local a = {}; for v in l:gmatch("[^,]+") do a[#a+1] = tonumber(v) end; hdr = {mnx=a[1], mnz=a[2], cell=a[3], row=a[4], mxx=a[5], mxz=a[6]}
  else local k, y = l:match("^(%d+),(.+)$"); k = tonumber(k); cells[k] = cells[k] or {}; table.insert(cells[k], tonumber(y)) end
end
local NOW = 0
function time_global() return NOW end
function command_line() return "-p1 -t2_pose -t2_run 1 " .. (os.getenv("STUB_CL") or "") end
function printf(...) end
local function V(x,y,z) local v={x=x or 0,y=y or 0,z=z or 0}
  function v:set(a,b,c) self.x,self.y,self.z=a,b,c; return self end
  function v:distance_to(o) return math.sqrt((self.x-o.x)^2+(self.y-o.y)^2+(self.z-o.z)^2) end
  return v end
function vector() return V() end
local apos = V(0,0,0); local adir = 0
db = { storage = {}, actor = { health=1, id=function() return 0 end, position=function() return apos end,
  direction=function() return V(math.sin(adir),0,math.cos(adir)) end, set_health_ex=function() end,
  set_actor_position=function(_, v, skip) NSET = NSET + 1; if not skip then COLL = COLL + 1 end; apos = V(v.x,v.y,v.z); if THROW and (NSET == 500 or NSET == 900) then apos = V(v.x + 90, v.y, v.z) end end, set_actor_direction=function(_, y) adir = y end } }
local vy = {}
level = { name=function() return LEVEL end,
  vertex_id=function(v)
    local h = hdr
    if v.x < h.mnx - h.cell*.5 or v.x > h.mxx + h.cell*.5 or v.z < h.mnz - h.cell*.5 or v.z > h.mxz + h.cell*.5 then return 4294967295 end
    local ys = cells[math.floor((v.x - h.mnx)/h.cell + .5) * h.row + math.floor((v.z - h.mnz)/h.cell + .5)]
    if not ys then return 4294967295 end
    local best, above
    for _, y in ipairs(ys) do if y <= v.y then if not best or y > best then best = y end elseif not above or y < above then above = y end end
    ids = ids + 1; vy[ids] = best or above; return ids end,
  vertex_position=function(id) return V(0, vy[id], 0) end,
  object_by_id=function() return nil end }
function device() return { pause=function() end, f_time_delta=0.05, frame=0 } end
game = { time=function() return NOW end }
LEVEL = os.getenv("STUB_LEVEL") or "l01_escape"; CHANGED = nil
clsid = { level_changer_s = 77 }
local GV = { [10]=1, [20]=2, [30]=3 }; local LN = { "l01_escape", "l02_garbage", "l03_agroprom" }
function game_graph() return { vertex=function(_, g) return { level_id=function() return GV[g] end } end } end
local OBJ = { [5]={c=77,n="esc_to_gar",g=10,dg=20,dp=V(1,2,3),dl=111}, [6]={c=77,n="gar_to_esc",g=20,dg=10,dp=V(4,5,6),dl=222}, [7]={c=77,n="gar_to_agr",g=20,dg=30,dp=V(7,8,9),dl=333}, [8]={c=5,n="npc",g=10}, [9]={c=77,n="esc_to_gar_2",g=10,dg=20,dp=V(11,12,13),dl=444},
  [11]={c=1,n="rad_far",s="zone_field_radioactive_weak",g=10,p=V(900,0,900)}, [12]={c=1,n="rad_near",s="zone_field_radioactive_strong",g=10,p=V(-200,-19,-140)}, [13]={c=1,n="rad_other_level",s="zone_field_radioactive_weak",g=20,p=V(-221,-19,-145)},
  [14]={c=1,n="anom_el",s="zone_mine_electric_weak",g=10,p=V(-210,-19,-150)}, [15]={c=1,n="anom_ghost",s="zone_mine_ghost",g=10,p=V(-221,-19,-145)} }
function alife() return { switch_distance=function() return 450 end, object_count=function() return 0 end, level_name=function(_, i) return LN[i] end,
  object=function(_, id) if id >= 500 then if id > 500 + (NCREATE or 0) or (RELEASED and RELEASED[id]) then return nil end; return { id = id, clsid = function() return 5 end, section_name = function() return "mut" end } end; local o = OBJ[id]; if not o then return nil end
    return { clsid=function() return o.c end, name=function() return o.n end, section_name=function() return o.s or "other" end, m_game_vertex_id=o.g, dest_game_vertex_id=o.dg, dest_position=o.dp, dest_level_vertex_id=o.dl, position=o.p or V(0,0,0) } end,
  release=function(_, se) RELEASED = RELEASED or {}; RELEASED[se.id] = true end,
  create=function(_, sec) NCREATE = (NCREATE or 0) + 1; if sec == os.getenv("STUB_BADSEC") then error("no section") end; return { id = 500 + NCREATE } end } end
function ChangeLevel(pos, lvid, gvid, ang) CHANGED = string.format("%g,%g,%g,%d,%d", pos.x, pos.y, pos.z, lvid, gvid); error("LEVEL_CHANGE") end
function get_console() return { execute=function(_, c) if tostring(c):find("g_freeze_absorb_armed") then print("CONSOLE " .. NOW .. " " .. c) end end, get_bool=function() return true end } end
function profile_timer() return { start=function() end, stop=function() end, time=function() return 500 end } end
function getFS() return { update_path=function(_,a,b) return b end } end
local CB = {}
function RegisterScriptCallback(n, f) CB[n] = CB[n] or {}; table.insert(CB[n], f) end
if HAS_STANCE then db.actor.set_actor_stance = function(_, k) STANCE = k end end
db.actor.radiation = tonumber(os.getenv("STUB_RAD") or "0"); BLEED = tonumber(os.getenv("STUB_BLEED") or "0"); setmetatable(db.actor, { __index = function(_, k) if k == "bleeding" then return BLEED end end, __newindex = function(t, k, v) if k == "bleeding" then BLEED = math.max(0, BLEED - v) else rawset(t, k, v) end end }); db.actor.alive = function() return true end; db.actor.change_radiation = function(self, d) self.radiation = self.radiation + d end; db.actor.level_vertex_id = function() return 1 end; db.actor.game_vertex_id = function() return 10 end
level.object_by_id = function(id) if id >= 500 then if RELEASED and RELEASED[id] then return nil end; return { position=function() return V(apos.x + 2, apos.y, apos.z) end, alive=function() return true end, best_enemy=function() return db.actor end } elseif id == 12 or id == 14 then return {} end end
dofile(SCRIPT); on_game_start()
local function tick(ms) NOW = NOW + ms; for _, f in ipairs(CB.actor_on_update) do f() end end
local T = {}
for l in io.lines(TRACK) do l = l:gsub("\r", ""); local t,x,y,z = l:match("^([%d%.%-]+),([%d%.%-]+),([%d%.%-]+),([%d%.%-]+)"); if t then T[#T+1] = {tonumber(t),tonumber(x),tonumber(y),tonumber(z)} end end
local j = 1
local function at(t) while j < #T - 1 and T[j+1][1] <= t do j = j + 1 end
  local a, b = T[j], T[j+1]; local f = math.min(1, math.max(0, (t - a[1]) / (b[1] - a[1])))
  return a[2] + f*(b[2]-a[2]), a[3] + f*(b[3]-a[3]), a[4] + f*(b[4]-a[4]) end
tick(1); tick(15000)
local n = 0
-- LOST tests: STUB_GAP="a,b" = the writer is dead for track seconds [a,b); STUB_RESEQ=1 = it comes back with seq 1;
-- STUB_SESS / STUB_SESS2 = session number before / after the gap (pose v3); STUB_AW = keep | freeze
local GA, GB = (os.getenv("STUB_GAP") or ""):match("^([%d%.]+),([%d%.]+)$"); GA, GB = tonumber(GA), tonumber(GB)
local PA2, PB2 = (os.getenv("STUB_PAUSE") or ""):match("^([%d%.]+),([%d%.]+)$"); PA2, PB2 = tonumber(PA2), tonumber(PB2)   -- a second, short gap (same seq and session after it)
local SESS, AWS, gapped = os.getenv("STUB_SESS"), os.getenv("STUB_AW") or "keep", false
for k = math.floor(T0 * 20), math.floor(T1 * 20) - 1 do
  if PA2 and k / 20 >= PA2 and k / 20 < PB2 then local ok, e = pcall(tick, 50); if not ok then print("stopped in pause: " .. tostring(e)); break end
  elseif GA and k / 20 >= GA and k / 20 < GB then gapped = true; local ok, e = pcall(tick, 50); if not ok then print("stopped in gap: " .. tostring(e)); break end
  else
  if gapped then gapped = false; if os.getenv("STUB_RESEQ") then n = 0 end; if os.getenv("STUB_SESS2") then SESS = os.getenv("STUB_SESS2") end end
  n = n + 1; local x, y, z = at(k / 20)
  local f = real_open(SHM.."/pose.txt","wb"); local body = V2 and string.format("p,%.3f,%.3f,%.3f,0.0000,1.0,%d", x, ABS or (y + 0.3 + OFF), z, (n % 400 < 200) and 0 or 1) or string.format("p,%.3f,%.3f,%.3f,0.0000,1.0", x, y, z)
  local st, zn = "play", LEVEL; if MODE == "cross" and n >= 100 then st, zn = "cross", (os.getenv("STUB_DEST") or "l02_garbage"); body = string.format("p,%.3f,%.3f,%.3f,0.0000,1.0,0", x, y + 0.3, z) .. (os.getenv("STUB_C") and ("\r\nc," .. os.getenv("STUB_C")) or "") end
  f:write(string.format("seq,%d,%d,%s,%s,%s%s\r\n%s\r\nend,%d\r\n", n, 1795000000000 + n*50, st, zn, AWS, SESS and ("," .. SESS) or "", body, n)); f:close(); local ok, e = pcall(tick, 50); if not ok then print("stopped: " .. tostring(e):sub(-12) .. " CHANGED=" .. tostring(CHANGED)); break end
  end
end
for _, f in ipairs(CB.actor_on_net_destroy or {}) do f() end
print("sets "..NSET.." collision-correcting sets "..COLL.." last stance "..tostring(STANCE))
