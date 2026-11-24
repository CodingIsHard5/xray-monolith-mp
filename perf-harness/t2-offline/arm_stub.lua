-- Fake test of the absorb arm/disarm paths (prereg section 28). Runs the REAL zz_p1_puppet.script with engine stubs.
--   lua5.1 arm_stub.lua <shm dir> <script> <case>     prints: the ABSORB lines, then "ENGINE_ARMED_AT_END=<0|1>"
local SHM, SCRIPT, CASE = arg[1], arg[2], arg[3]
local real_open = io.open
io.open = function(p, m) p = p:gsub("^Z:\\dev\\shm\\itrz_b1\\", SHM .. "/"); return real_open(p, m) end
local NOW, ARMED = 0, 0
function time_global() return NOW end
function command_line() return "-p1 -t2_pose -t2_run 1 -t2_absorb 2000" end
function printf(...) end
local function V(x,y,z) local v={x=x or 0,y=y or 0,z=z or 0}
  function v:set(a,b,c) self.x,self.y,self.z=a,b,c; return self end
  function v:distance_to(o) return math.sqrt((self.x-o.x)^2+(self.y-o.y)^2+(self.z-o.z)^2) end
  return v end
function vector() return V() end
local apos = V(0,0,0)
db = { storage = {}, actor = { health=1, id=function() return 0 end, position=function() return apos end,
  direction=function() return V(0,0,1) end, set_health_ex=function() end,
  set_actor_position=function(_, v) apos = V(v.x,v.y,v.z) end, set_actor_direction=function() end } }
level = { name=function() return "l01_escape" end, vertex_id=function() return 7 end, vertex_position=function() return V(0,0.5,0) end,
  object_by_id=function() return nil end }
function device() return { pause=function() end, f_time_delta=0.05, frame=0 } end
game = { time=function() return NOW end }
function alife() return { switch_distance=function() return 450 end, object_count=function() return 0 end } end
function get_console() return { execute=function(_, c) local v = c:match("^g_freeze_absorb_armed (%d)"); if v then ARMED = tonumber(v) end end, get_bool=function() return true end } end
function profile_timer() return { start=function() end, stop=function() end, time=function() return 500 end } end
function getFS() return { update_path=function(_,a,b) return b end } end
local CB = {}
function RegisterScriptCallback(n, f) CB[n] = CB[n] or {}; table.insert(CB[n], f) end
dofile(SCRIPT); on_game_start()
local function tick(ms) NOW = NOW + ms; for _, f in ipairs(CB.actor_on_update) do f() end end
local N = 0
local function pose(state, aw) N = N + 1
  local f = real_open(SHM.."/pose.txt","wb"); f:write(string.format("seq,%d,%d,%s,l01_escape,%s\r\np,10.000,0.500,20.000,0.0000,1.0\r\nend,%d\r\n", N, 1795000000000+N, state, aw, N)); f:close(); tick(50) end
local function run(state, aw, secs) for _ = 1, secs * 20 do pose(state, aw) end end
local function idle(secs) for _ = 1, secs * 20 do tick(50) end end
tick(1); tick(15000); run("play", "freeze", 2)
if CASE == "end_freeze" then pose("end", "freeze"); idle(20)                       -- r2b: ITR2's one final pose, then nothing
elseif CASE == "dead_then_stale" then pose("dead", "freeze"); idle(20)             -- writer dies right after the dead pose; no STOP comes
elseif CASE == "dead_no_stop" then run("dead", "freeze", 20)                       -- poses keep coming, bridge never stops X-Ray
elseif CASE == "dead_then_play" then run("dead", "freeze", 1); run("play", "freeze", 2)   -- the normal CONT path (short, unabsorbed)
elseif CASE == "dead_then_end" then run("dead", "freeze", 1); pose("end", "freeze"); idle(2)
elseif CASE == "armed_at_game_end" then run("dead", "freeze", 1)
elseif CASE == "real_freeze" then pose("dead", "freeze"); NOW = NOW + 33; run("play", "freeze", 1)  -- STOP 77 s, absorbed: time_global +33 ms
end
local mid = ARMED
for _, f in ipairs(CB.on_game_end or {}) do f() end
local MF = io.popen("ls "..SHM.."/meta*.csv"):read("*l")
for l in io.lines(MF) do l = l:gsub("\r", ""); if l:match("^ABSORB_[AD]") or l:match("^STALE") then print("  "..l) end end
print("  ENGINE_ARMED_BEFORE_GAME_END="..mid.."  AFTER="..ARMED)
