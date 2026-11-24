-- Offline test of the new on-mesh test: REAL zz_p1_puppet.script, REAL level.ai nodes (dumped by python to cells.txt),
-- REAL track sampled at 20 Hz. level.vertex_id is modelled like CLevelGraph::vertex_id (cell-centre heights).
local SHM, SCRIPT, CELLS, TRACK, T0, T1 = arg[1], arg[2], arg[3], arg[4], tonumber(arg[5]), tonumber(arg[6])
local real_open = io.open
io.open = function(p, m) p = p:gsub("^Z:\\dev\\shm\\itrz_b1\\", SHM .. "/"); return real_open(p, m) end
local hdr; local cells, ids, idy = {}, 0, {}
for l in io.lines(CELLS) do
  if not hdr then local a = {}; for v in l:gmatch("[^,]+") do a[#a+1] = tonumber(v) end; hdr = {mnx=a[1], mnz=a[2], cell=a[3], row=a[4], mxx=a[5], mxz=a[6]}
  else local k, y = l:match("^(%d+),(.+)$"); k = tonumber(k); cells[k] = cells[k] or {}; table.insert(cells[k], tonumber(y)) end
end
local NOW = 0
function time_global() return NOW end
function command_line() return "-p1 -t2_pose -t2_run 1" end
function printf(...) end
local function V(x,y,z) local v={x=x or 0,y=y or 0,z=z or 0}
  function v:set(a,b,c) self.x,self.y,self.z=a,b,c; return self end
  function v:distance_to(o) return math.sqrt((self.x-o.x)^2+(self.y-o.y)^2+(self.z-o.z)^2) end
  return v end
function vector() return V() end
local apos = V(0,0,0); local adir = 0
db = { storage = {}, actor = { health=1, id=function() return 0 end, position=function() return apos end,
  direction=function() return V(math.sin(adir),0,math.cos(adir)) end, set_health_ex=function() end,
  set_actor_position=function(_, v) apos = V(v.x,v.y,v.z) end, set_actor_direction=function(_, y) adir = y end } }
local vy = {}
level = { name=function() return "l01_escape" end,
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
function alife() return { switch_distance=function() return 450 end, object_count=function() return 0 end } end
function get_console() return { execute=function() end, get_bool=function() return true end } end
function profile_timer() return { start=function() end, stop=function() end, time=function() return 500 end } end
function getFS() return { update_path=function(_,a,b) return b end } end
local CB = {}
function RegisterScriptCallback(n, f) CB[n] = CB[n] or {}; table.insert(CB[n], f) end
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
for k = math.floor(T0 * 20), math.floor(T1 * 20) - 1 do
  n = n + 1; local x, y, z = at(k / 20)
  local f = real_open(SHM.."/pose.txt","wb"); f:write(string.format("seq,%d,%d,play,l01_escape,keep\r\np,%.3f,%.3f,%.3f,0.0000,1.0\r\nend,%d\r\n", n, 1795000000000 + n*50, x, y, z, n)); f:close(); tick(50)
end
for _, f in ipairs(CB.on_game_end or {}) do f() end
