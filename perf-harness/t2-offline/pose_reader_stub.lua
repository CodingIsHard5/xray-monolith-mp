local SHM = arg[1]
local real_open = io.open
io.open = function(p, m) p = p:gsub("^Z:\\dev\\shm\\itrz_b1\\", SHM .. "/"); return real_open(p, m) end
local NOW = 0
function time_global() return NOW end
function command_line() return "-p1 -t2_pose -t2_run 1795452855036 -t2_pause" end
function printf(...) print(string.format(...)) end
local function V(x,y,z) local v={x=x or 0,y=y or 0,z=z or 0}
  function v:set(a,b,c) self.x,self.y,self.z=a,b,c; return self end
  function v:distance_to(o) return math.sqrt((self.x-o.x)^2+(self.y-o.y)^2+(self.z-o.z)^2) end
  return v end
function vector() return V() end
local apos = V(0,0,0); local adir = 0; PAUSED=false; KILLED={}
db = { storage = {}, actor = { health=1, id=function() return 0 end, position=function() return apos end,
  direction=function() return V(math.sin(adir),0,math.cos(adir)) end, set_health_ex=function() end,
  set_actor_position=function(_, v) apos = V(v.x,v.y,v.z) end, set_actor_direction=function(_, y) adir = y end } }
level = { name=function() return "l01_escape" end,
  vertex_id=function(v) if v.x > 1000 then return 4294967295 end return 7 end,
  vertex_position=function(id) return V(0,0,0) end,
  object_by_id=function(id) return { alive=function() return not KILLED[id] end, kill=function() KILLED[id]=true end } end }
function device() return { pause=function(_, b) PAUSED=b end } end
function alife() return { switch_distance=function() return 450 end } end
function get_console() return { execute=function() end, get_bool=function() return true end } end
function profile_timer() return { start=function() end, stop=function() end, time=function() return 500 end } end
function getFS() return { update_path=function(_,a,b) return b end } end
local CB = {}
function RegisterScriptCallback(n, f) CB[n] = CB[n] or {}; table.insert(CB[n], f) end
dofile(arg[2]); on_game_start()
local function tick(ms) NOW = NOW + ms; for _, f in ipairs(CB.actor_on_update) do f() end end
local function pose(n, body, crlf, state, aw)
  local nl = crlf and "\r\n" or "\n"
  local f = real_open(SHM.."/pose.txt","wb")
  f:write(table.concat({string.format("seq,%d,%d,%s,l01_escape,%s", n, 1795452900000+n, state or "play", aw or "keep"), body or "p,10.000,0.500,20.000,1.5708,1.0", "end,"..(n)}, nl) .. nl); f:close() end
tick(1)                      -- first update: t0 = 1 + 15000
tick(15000)                  -- warm-up over
pose(1); tick(30)            -- LF
pose(2, nil, true); tick(30) -- CRLF
local f = real_open(SHM.."/pose.txt","wb"); f:write("seq,3,1795452900003,play,l01_escape,keep\np,1,2,3,0,1\n"); f:close(); tick(30) -- torn
tick(1100)                   -- stale
pose(4, "p,2000,0,0,0,1"); tick(30) -- off-mesh -> hold, resume
pose(5, "p,10,0.5,20,0,1\nk,4242"); tick(30); pose(6, "p,10,0.5,20,0,1\nk,4242"); tick(30) -- kill once
pose(7, nil, false, "dead", "freeze"); tick(30); print("paused", PAUSED)
pose(8); tick(30); print("paused", PAUSED, "actor", apos.x, apos.z, "yaw", adir)
for _, f in ipairs(CB.on_game_end) do f() end
