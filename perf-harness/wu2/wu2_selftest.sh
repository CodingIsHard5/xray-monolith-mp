#!/usr/bin/env bash
# wu2 scorer self-test (PREREG-wu2-alife-warmup-DRAFT.md section 8). DRAFT.
#   nice -n 19 bash wu2_selftest.sh  -> builds wu2/selftest-runs/<stamp>/, runs every case and every mutant, prints one final line
# Real fixtures: earlier run folders and logs under <project>/out, read IN PLACE (never copied whole, never written).
#   out/wu1_prime (the one real Warm18 capture, with its loading frames) is probed in place; its mutants are folders in
#   <stamp>/rpm/<name>/wu1_prime/ that hold ONE changed file each, the other files are symlinks to the real ones (a
#   changed file is always created new with open(.., "xb"), never written through a link).
#   Real xray.log bytes (CRLF) of older runs are also spliced into made loads so the classifier sees real engine output.
# Made fixtures: loads written to a plan into <stamp>/pool. wu2: every made capture holds what the engine really writes
#   with -perf_capture 0,N (prereg 5.1): 13 loading frames first (their ms copied from wu1_prime), then 58 precache
#   frames, then play; the clock lines' frame numbers and times follow those frames (dwFrame = capture index + 18).
# Mutants: copies of the scorer with ONE check switched off (its on("<name>") hook returns False); each must be caught.
# The stamp folder must be new. Output also saved as <stamp>/selftest_output.txt. The self-test deletes nothing.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
P="$(cd "$HERE/../../.." && pwd)"           # project root (wt-render-split/perf-harness/wu2 -> project)
REAL="$P/out"
SC="$HERE/wu2_score.py"
[ -s "$SC" ] || { echo "WU2 SELFTEST FAIL: scorer $SC missing"; exit 2; }
[ -d "$REAL" ] || { echo "WU2 SELFTEST FAIL: real run root $REAL missing"; exit 2; }
for f in xray.log perf_frames_wu1_prime.csv perf_sites_wu1_prime.csv perf_slow_wu1_prime.csv conditions.txt; do
  [ -s "$REAL/wu1_prime/$f" ] || { echo "WU2 SELFTEST FAIL: real fixture $REAL/wu1_prime/$f missing"; exit 2; }
done
ST="$HERE/selftest-runs/$(date +%Y%m%d-%H%M%S)-$$"
[ -e "$ST" ] && { echo "WU2 SELFTEST FAIL: $ST exists"; exit 2; }
mkdir -p "$ST/cases" "$ST/pool" "$ST/mutants" "$ST/rpm"
touch "$ST/.t0"; sleep 1
LOG="$ST/selftest_output.txt"

python3 -I - "$ST" "$REAL" "$SC" <<'PYEOF' 2>&1 | tee -a "$LOG"
import hashlib, os, re, subprocess, sys
ST, REAL, SC = sys.argv[1], sys.argv[2], sys.argv[3]
POOL = os.path.join(ST, "pool")
FR = 7000                      # frames per made load: 71 loading + precache frames, then ~34.6 s of play at 5 ms
LIMIT_FULL, LIMIT_WIDE = 4294967295, 65534
# wu2 layout of a made capture (prereg 5.1), as in out/wu1_prime
LOAD_MS = [1.5257, 5761.1038, 0.0835, 0.0589, 1.7347, 0.0903, 0.3746, 0.1303, 0.7662, 1.4470, 259.2541, 0.2162, 213.0295]
NL = len(LOAD_MS)              # 13 loading frames (11 single events + 2 net_start_client6 calls)
PC = 58                        # precache frames (countdown 58 .. 1 at FrameMove)
E = NL + PC                    # 71: first play frame
DOFF = 18                      # dwFrame = capture index + 18
OLD = 7                        # the wu1 fixtures' frame numbers F_old (frame 64 = first play frame) -> capture F_old + 7
PRE_HEAD = [1234.3661, 2494.7303, 22.0252, 28.6732, 488.9313]   # wu1_prime capture 13-17
ZONE_FIX = {1: "CALifeSimulator::CALifeSimulator", 10: "CLevel::net_start_client6", 12: "CLevel::net_start6",
            13: "CLevel::cl_Process_Spawn", 14: "CScriptBinder::shedule_Update", 15: "CScriptBinder::shedule_Update",
            16: "CScriptBinder::shedule_Update", 17: "CLevel::cl_Process_Spawn"}
INTRO_ZONE = "CGamePersistent OnFrame"   # at capture E-2 (E = the made load's own first play frame)

def hold_k(kind, o):
    """Held frames of a made load. Engine (wt-simtick d77d70b5c, device.cpp:166-168): End() does not count the precache
    countdown down while it is 3 and the lever's pass is pending, so every held frame is one more FrameMove frame with the
    countdown > 0 ('Precache frame', device.cpp:771-773; dwFrame++ device.cpp:1048): the precache block is 58 + K frames
    and the first play frame is E = 71 + K."""
    return o.get("hold", 12) if (kind == "on" and o.get("lever") == "held") else 0

# ---------------- made loads ----------------
def frames_for(kind, o):
    base = o.get("base", 5.0)
    K = hold_k(kind, o)
    Eh = E + K
    jit = (lambda i: 0.0) if o.get("flat") else (lambda i: 0.3 * ((i * 7919) % 7) / 6.0)
    if o.get("flat"):
        pre_ms = [base] * PC
    elif kind == "on" and o.get("lever") == "held":
        pre_ms = [200.0] * (PC + K)   # long precache frames: the 10 s pass-log window closes before the full pass
    else:
        pre_ms = PRE_HEAD + [base + 1.0 + jit(i) for i in range(PC - len(PRE_HEAD))]
        pre_ms[E - 2 - NL] = 52.0  # intro frame (lua GC after intro_start, 52 ms in wu1_prime)
    play = [base + jit(i) for i in range(FR - Eh)]
    ev = []                                            # (time ms after the end of loading, frame ms, zone)
    if o.get("save", True):
        ev.append((2000.0, o.get("save_ms", 900.0), "CALifeStorageManager::save" if o.get("save_row", True) else "Device: other work"))
    # stock in-play shape from the real fixed-step captures (prereg 6): hitch ~7 s after loading, then 27 passes 452 ms
    # apart from ~9.8 s; default: hitch + 16 slow passes = 17 frames > 20 ms in 30 s, 3 in 15 s; other passes 15 ms
    if kind == "offP" and o.get("hitch", True):
        ev.append((7000.0, 320.0, "ALife/scheduled/update"))
    if kind == "offP" and o.get("train", True):
        slow = set(o.get("train_slow", [1] + list(range(11, 26))))
        for j in range(27):
            v = (40.0 if j % 2 else 30.0) if j in slow else 15.0
            ev.append((9800.0 + 452.0 * j, v, "ALife/scheduled/update"))
    for t, v in o.get("extra", ()):
        ev.append((t, v, "CDetailManager::cache_Update"))
    zone = {}
    for t, v, z in sorted(ev):
        c, i = 0.0, 0
        while i < len(play) - 1 and c < t:
            c += play[i]; i += 1
        while Eh + i in zone and i < len(play) - 1:
            i += 1
        play[i] = v; zone[Eh + i] = z
    ms = [round(v, 4) for v in (LOAD_MS + pre_ms + play)]
    for i, z in list(ZONE_FIX.items()) + [(Eh - 2, INTRO_ZONE)]:
        if ms[i] > 20.0:
            zone[i] = z
    if o.get("no_frame1"):
        zone[1] = "CDetailManager::cache_Update"
    return ms, zone

def pline(F, T, Pc, n, M, lim): return "* alife_pass frame %d time_global %d precache %d n %d of %d limit %d" % (F, T, Pc, n, M, lim)
def oline(F, T, Pc, v): return "* alife_opu frame %d time_global %d precache %d value %d" % (F, T, Pc, v)
def sline(F, i, name, v): return "* alife_slow frame %d id %d %s [%s] %.2f ms" % (F, i, name, name.rstrip("0123456789"), v)

def log_events(tag, kind, o, ms, zone):
    """Returns (loading_events [(F_old, seq, text)], play_lines [text]) for a made load. Lines are written in the wu1
    fixtures' frame numbers F_old (64 = first play frame) and placed in the capture at F_old + 7; the printed device
    frame is capture index + 18 and the printed time_global follows the frames file (te = time_global at the end)."""
    te = o.get("t_end", 33000)
    pre = [0.0]
    for v in ms:
        pre.append(pre[-1] + v)
    def T(F, extra=0):          # time_global of a line in F_old numbering
        c = F + OLD
        return int(round(te - (pre[E] - pre[c]) + extra)) + o.get("t_shift", 0)
    Fn = lambda F: F + OLD + DOFF + o.get("f_shift", {}).get(F, 0)
    Pc = lambda F: 64 - F
    L, lv = [], o.get("lever", "good")
    if kind == "on" and lv == "held":
        # the engine's order (device.cpp:131-170), in CAPTURE indices c: the countdown runs 58 .. 4 at capture 13 .. 67
        # and is 3 at capture 68 (E-3); while the pass is pending End() holds it at 3 (device.cpp:166-168), so capture
        # 69 .. 68+K also read 3 (K held frames, each one more 'Precache frame' call); the pass is claimed in capture
        # 68+K, whose End() prints the held line (only if K > 0) and counts 3 -> 2; capture 69+K reads 2, 70+K reads 1,
        # and Eh = 71+K is the first play frame (countdown 0). Precache frames are 200 ms apart, so the 10 s pass-log
        # window (from the pass at capture 13) has closed before the full pass: its alife_pass line is not printed,
        # and the last line with precache > 0 is the actor-reinit alife_opu in the claim frame (precache 3).
        # Log-order keys stay in the wu1 numbering (capture - 7), so intro_start / textures-handled sort at Eh-2-7 / Eh-1-7.
        K = hold_k(kind, o)
        Eh = E + K
        Pcap = lambda c: (E - c) if c <= E - 3 else (3 if c <= E - 3 + K else Eh - c)
        Tc = lambda c: int(round(te - (pre[Eh] - pre[c])))
        Fc = lambda c: c + DOFF
        cc = E - 3 + K                     # the claim frame
        L = [(13 - OLD, 0, pline(Fc(13), Tc(13), Pcap(13), 3, 2400, 3)), (24 - OLD, 0, pline(Fc(24), Tc(24), Pcap(24), 3, 2400, 3)),
             (35 - OLD, 0, oline(Fc(35), Tc(35), Pcap(35), 20)), (49 - OLD, 0, pline(Fc(49), Tc(49), Pcap(49), 3, 2400, 3)),
             (cc - OLD, 0, oline(Fc(cc), Tc(cc), Pcap(cc), LIMIT_WIDE))]
        L += [(cc - OLD, 1, sline(Fc(cc), 14200 + j, "zat_b106_smart_terrain" if j == 0 else "stalker_sim_squad_novice%d" % (19000 + j), 300.0 if j == 0 else 2.1)) for j in range(130)]
        L.append((cc - OLD, 2, "* [alife_warmup] full pass: 2400 of 2400 objects, frame %d, precache_left %d, 612.3 ms" % (Fc(cc), Pcap(cc))))
        if K:
            L.append((cc - OLD, 3, "* [alife_warmup] precache held %d frames" % K))
        return L, [oline(Fc(Eh + 5), int(round(te + pre[Eh + 5] - pre[Eh])), 0, 20)]
    L.append((6, 0, pline(Fn(6), T(6), Pc(6) - o.get("race", 0), 3, 2400, 3)))
    L.append((17, 0, pline(Fn(17), T(17, o.get("t_jit", 0)), Pc(17), 3, 2400, 3)))
    L.append((28, 0, oline(Fn(28), T(28), Pc(28), LIMIT_WIDE)))
    L.append((28, 1, "Time continual is:%d" % (T(28) - o.get("t_shift", 0))))
    wide_slows = lambda F, k: [(F, 1, sline(Fn(F), 14200 + j, "zat_b106_smart_terrain" if j == 0 else "stalker_sim_squad_novice%d" % (19000 + j), 300.0 if j == 0 else 2.1)) for j in range(k)]
    if kind == "on":
        Ff, Pf, seq = 31, Pc(31), 2
        if lv == "after_intro": Ff, seq = 62, 60
        if lv == "p2": Ff, Pf, seq = 62, 2, 0
        N = 1000 if lv == "partial" else 2400
        if lv != "none":
            L += wide_slows(Ff, 130)
            L.append((Ff, seq, "* [alife_warmup] full pass: %d of 2400 objects, frame %d, precache_left %d, %s ms" % (N, Fn(Ff), Pf, o.get("full_ms", "612.3"))))
            L.append((Ff, seq + 1, pline(Fn(Ff), T(Ff), Pc(Ff) if lv == "after_intro" else Pf, N, 2400, LIMIT_FULL)))
        if lv == "two":
            L.append((33, 2, "* [alife_warmup] full pass: 2400 of 2400 objects, frame %d, precache_left 31, 20.1 ms" % Fn(33)))
        if lv == "cap":
            L.append((62, 1, "! [alife_warmup] hold cap hit after 300 frames, pass not run"))
        L.append((42, 0, pline(Fn(42), T(42), Pc(42), 2400, 2400, LIMIT_WIDE)))
        L.append((50, 0, oline(Fn(50), T(50), Pc(50), 20)))
        L.append((53, 0, pline(Fn(53), T(53), Pc(53), 20, 2400, 20)))
    elif kind == "offL":
        L += wide_slows(31, o.get("wide_slows", 130))
        L.append((31, 2, pline(Fn(31), T(31), Pc(31), 2400, 2400, LIMIT_WIDE)))
        L.append((42, 0, pline(Fn(42), T(42), Pc(42), 2400, 2400, LIMIT_WIDE)))
        L.append((50, 0, oline(Fn(50), T(50), Pc(50), 20)))
        L.append((53, 0, pline(Fn(53), T(53), Pc(53), 20, 2400, 20)))
    else:  # offP: one long loading frame carried time_global past the 1 s reset before the next pass
        if not o.get("reset_late"):
            L.append((29, 0, oline(Fn(29), T(29), Pc(29), 20)))
        for F in (31, 42, 53):
            L.append((F, 0, pline(Fn(F), T(F), Pc(F), 20, 2400, 20)))
    if o.get("off_warm"):
        L.append((62, 1, "* [alife_warmup] precache held 3 frames"))
    if o.get("bad_pass"):
        L.append((40, 0, "* alife_pass frame 40 time_global garbage"))
    if o.get("bad_slow_ms"):
        L.append((40, 0, "* alife_slow frame 40 id 14200 zat_b106_smart_terrain [zat_b106_smart_terrain] %s ms" % o["bad_slow_ms"]))
    # in play: every 88 frames (~452 ms) while within 10 s of time_global after the first pass (old frame 6)
    play = []
    t_first = T(6)
    cum = lambda k: pre[E + k] - pre[E]
    if not o.get("no_play_lines"):
        k = 0
        while E + k < len(ms) and te + cum(k) - t_first <= 10000:
            n, lim = 20, 20
            if o.get("wide_in_play") and k == 0:
                n, lim = 2400, LIMIT_WIDE
            play.append((k, pline(Fn(64) + k, int(round(te + cum(k))) + o.get("t_shift", 0), 0, n, 2400, lim)))
            if k == 88:
                play.append((k, "Time continual is:%d" % int(round(te + cum(k)))))
                if o.get("tc_dup") is not None:    # one more stamp, tc_dup ms below the previous stamp
                    play.append((k, "Time continual is:%d" % (int(round(te + cum(k))) - o["tc_dup"])))
            k += 88
    if o.get("reset_late"):
        play.append((2500, oline(Fn(64) + 2500, int(round(te + cum(2500))) + o.get("t_shift", 0), 0, 20)))
    if o.get("wide_in_play"):
        for j in range(130):
            play.append((0, sline(Fn(64), 15000 + j, "stalker_sim_squad_novice%d" % (15000 + j), 2.0)))
    for i, z in sorted(zone.items()):
        if z == "ALife/scheduled/update":
            play.append((i - E, sline(Fn(64) + i - E, 14200, "zat_b106_smart_terrain", ms[i] - 5.0)))
    play.sort(key=lambda x: x[0])
    if o.get("swap_play"):                     # the first two play pass lines logged in the wrong order
        assert play[0][1].startswith("* alife_pass ") and play[1][1].startswith("* alife_pass "), tag
        play[0], play[1] = play[1], play[0]
    drop = o.get("drop_lines", ())
    keep = lambda t: not any(t.startswith(d) for d in drop)
    ren = lambda t: t.replace("* alife_opu ", "* alife_opu2 ") if o.get("rename_opu") else t
    return [(a, b, ren(t)) for a, b, t in L if keep(t)], [ren(t) for _, t in play if keep(t)]

def cmdline(tag, arm, o):
    t = ["z:\\home\\caden\\projects\\stalkergameengine\\run\\bin\\warm18.exe", "-nointro", "-dbg", "-start",
         "server(cordon1/single/alife/load)", "client(localhost)", "-perf_capture", "%d,%d,%s" % (o.get("warm", 0), FR, tag),
         "-perf_quit", "-perf_noinput", "-perf_nopresent", "-perf_slow", "20"]
    if o.get("fixed_dt"): t.insert(8, "-perf_fixed_dt")
    if o.get("save_name"): t[4] = "server(%s/single/alife/load)" % o["save_name"]
    t = [x for x in t if x not in o.get("drop_flags", ())]
    t += list(o.get("more_flags", ()))
    if (arm == "on") != bool(o.get("flip_flag")): t.append("-alife_warmup")
    return " ".join(t)

BLOBS = os.path.join(POOL, ".blobs")
os.makedirs(BLOBS)
def put(path, data):
    """Pool files are stored once per content and hard-linked to each load (disk: many made loads share their frames
    file); nothing ever writes to a pool file after this, so the links stay what they were made as."""
    b = os.path.join(BLOBS, hashlib.sha256(data).hexdigest())
    if not os.path.exists(b):
        with open(b, "xb") as f:
            f.write(data)
    os.link(b, path)
def write(path, lines, crlf, empty=False):
    nl = "\r\n" if crlf else "\n"
    put(path, b"" if empty else (nl.join(lines) + nl).encode("ascii"))

def site(name, line, calls, incl=1.0, self_=0.5):
    return "%s,z:\\src\\xrengine\\device.cpp,%d,%d,%.4f,%.4f,0,0.0000,0.0000" % (name, line, calls, incl, self_)

def make(tag, kind, real=None, **o):
    """kind: on, offL, offP. real: path of a real xray.log to splice the made lines into (CRLF bytes kept)."""
    d = os.path.join(POOL, tag); os.makedirs(d)
    arm = "on" if kind == "on" else "off"
    ms, zone = frames_for(kind, o)
    skip, crlf = o.get("skip", ()), o.get("crlf", False)
    n_rows = FR - 1 if o.get("drop_last") else FR
    fms = list(ms)
    if o.get("frames_shift"):                 # the frames file one frame early against the profiler's slow rows
        fms = fms[1:] + fms[-1:]
    if o.get("drop_loading"):                 # the loading frames left out of the frames file
        fms = fms[NL:]
        n_rows = len(fms)
    fl = ["frame,ms"] + ["%d,%.4f" % (i, v) for i, v in enumerate(fms[:n_rows])]
    if o.get("bad_frame_row"): fl[100] = "99,abc"
    if o.get("frame_text"): fl[101] = "100,%s" % o["frame_text"]
    if "frames" not in skip: write(os.path.join(d, "perf_frames_%s.csv" % tag), fl, crlf, "frames" in o.get("empty", ()))
    tot = sum(ms)
    c6 = o.get("c6", 2)
    sl = ["#frames,%d" % FR, "#total_ms,%.3f" % tot,
          "name,file,line,calls_main,incl_main_ms,self_main_ms,calls_other,incl_other_ms,self_other_ms",
          "Process seqFrame,z:\\src\\xrengine\\device.cpp,1121,%d,%.4f,1.0000,0,0.0000,0.0000" % (FR - NL + o.get("seq_delta", 0), tot),
          "ALife/scheduled/update,z:\\src\\xrgame\\alife_schedule_registry.h,62,0,0.0000,0.0000,500,900.0000,900.0000"]
    if not o.get("no_loading_site"):
        sl.append(site("Loading...", 698, NL + o.get("loading_delta", 0), 6236.1439, 0.0046))
    sl.append(site("LoadDraw", 702, NL, 3.4262, 0.0008))
    for n in ("CLevel::net_start1", "CLevel::net_start2", "CLevel::net_start3", "CLevel::net_start4", "CLevel::net_start5",
              "CLevel::net_start6", "CLevel::net_start_client1", "CLevel::net_start_client2", "CLevel::net_start_client3",
              "CLevel::net_start_client4", "CLevel::net_start_client5"):
        sl.append(site(n, 100, 1))
    sl.append(site("CLevel::net_start_client6", 240, c6, 260.2267, 197.4940))
    K = hold_k(kind, o)                       # held frames are precache frames (device.cpp:166-168, 771-773)
    sl.append(site("Precache frame", 773, PC + K, 0.0091, 0.0091))
    sl.append(site("Start xrSASH Benchmark", 709, FR - E - K, 0.1685, 0.1685))
    if o.get("save", True):
        sl.append("CALifeStorageManager::save,z:\\src\\xrgame\\alife_storage_manager.cpp,48,2,800.0000,700.0000,0,0.0000,0.0000")
    if o.get("site_text"): sl.append("CDetailManager::cache_Update,z:\\src\\x.cpp,1,10,1.0000,%s,0,0.0000,0.0000" % o["site_text"])
    if o.get("simtick"): sl.append("Sim tick,z:\\src\\xrengine\\device.cpp,1060,100,1.0000,1.0000,0,0.0000,0.0000")
    if o.get("eco"): sl.append("Eco Render,z:\\src\\xrengine\\device.cpp,758,4000,9000.0000,9000.0000,0,0.0000,0.0000")
    if "sites" not in skip: write(os.path.join(d, "perf_sites_%s.csv" % tag), sl, crlf)
    rows = ["#threshold_ms,%.3f" % o.get("thr", 20.0), "frame,frame_ms,thread,self_ms,calls,name,file,line"]
    per = []
    for i, v in enumerate(ms):
        if v > 20.0:
            z = zone.get(i, "CDetailManager::cache_Update")
            th = "other" if z.startswith("ALife") else "main"
            fr = ["%d,%.4f,%s,%.4f,1,%s,z:\\src\\x.cpp,1" % (i, v, th, v * 0.8, z),
                  "%d,%.4f,main,%.4f,1,bench: wait for GPU (no present),z:\\src\\y.cpp,2" % (i, v, 1.5)]
            for k in range(o.get("rows_per", 2) - 2):
                fr.append("%d,%.4f,main,%.4f,1,filler site %d,z:\\src\\f.cpp,%d" % (i, v, 0.1, k, k))
            per.append(fr)
    if o.get("last_rows") and per:          # the last slow frame keeps only this many rows (profiler: up to 14)
        per[-1] = per[-1][:o["last_rows"]]
    for fr in per:
        rows += fr
    if o.get("count_rows"): assert len(rows) - 2 == o["count_rows"], (tag, len(rows) - 2)
    if "slow" not in skip: write(os.path.join(d, "perf_slow_%s.csv" % tag), rows, crlf)
    cond = ["start 2026-11-27 10:00:00 kind=EXCLUSIVE timing loadavg=0.30 0.40 0.50 heavy_others=[]",
            "end   2026-11-27 10:02:00 loadavg=1.10 0.60 0.55 heavy_others=[]", o.get("verdict", "VERDICT: clean")]
    if "cond" not in skip: write(os.path.join(d, "conditions.txt"), cond, crlf)
    ev, play = log_events(tag, kind, o, ms, zone)
    cap_s = "* [xrprof] capture '%s' started: %d frames" % (tag, FR)
    cap_f = "* [xrprof] capture '%s' finished: %d frames, %.1f ms total, %.3f ms/frame avg" % (tag, FR, tot, tot / FR)
    ev.append((62 + K, 50, "intro_start game_loaded"))
    ev.append((o.get("handled_at", 63 + K), 50, "* [x-ray]: Handled Necessary Textures Destruction"))
    made = [t for _, _, t in sorted(ev, key=lambda x: (x[0], x[1]))]
    if real is None:
        head = ["* Detected CPU: AMD Ryzen 7 9800X3D 8-Core Processor", "Init FileSystem 5.5 sec",
                "Game started: 27.11.2026 10:00:09.000", "command line " + cmdline(tag, arm, o), "Time continual is:7749",
                "* [x-ray]: Prefetched Data"]
        cs = [] if o.get("no_cap_lines") else [cap_s]
        cf = [] if o.get("no_cap_lines") else [cap_f]
        if o.get("cap_fin_early"):      # the finished marker before the end of loading
            body = head + cs + made[:-3] + cf + made[-3:] + ["[LSS] Spawning object [m_crow%d][m_crow][%d]" % (j, j) for j in range(50)] + play + ["- Autosaved new game"]
        elif o.get("cap_late"):         # wu1's (wrong) order: the started marker after the end of loading
            body = head + made + ["[LSS] Spawning object [m_crow%d][m_crow][%d]" % (j, j) for j in range(50)] + cs + play + ["- Autosaved new game"] + cf
        else:
            body = head + cs + ["[LSS] Spawning object [m_crow%d][m_crow][%d]" % (j, j) for j in range(50)] + made + play + \
                   ["- Autosaved new game"] + cf
        nl = "\r\n" if crlf else "\n"
        data = (nl.join(body) + nl).encode("ascii")
    else:
        # real engine bytes: keep every real line (CRLF) except the old command line, the old xrprof lines and the old
        # run's own clock stamps ("Time continual is:"); the capture-started line and the made loading lines go just
        # before the real intro_start line, the in-play lines after the real textures-handled line
        raw = open(real, "rb").read().split(b"\r\n")
        outl = []
        intro_done = False
        made_pre = [t for t in made if t not in ("intro_start game_loaded", "* [x-ray]: Handled Necessary Textures Destruction")]
        for r in raw:
            s = r.decode("ascii", "replace")
            if s.startswith("command line "):
                outl.append(("command line " + cmdline(tag, arm, o)).encode()); continue
            if "[xrprof] capture '" in s or s.startswith("Time continual is:"):
                continue
            if s == "intro_start game_loaded" and not intro_done:
                outl.append(cap_s.encode()); outl += [t.encode() for t in made_pre]; intro_done = True
            outl.append(r)
            if s == "* [x-ray]: Handled Necessary Textures Destruction":
                outl += [t.encode() for t in play]
        outl.insert(len(outl) - 1, cap_f.encode())
        data = b"\r\n".join(outl)
    if "xray" not in skip:
        put(os.path.join(d, "xray.log"), data)

# pool: 16 good ON, 14 good OFF-IN-LOADING, 11 good OFF-IN-PLAY
GON = ["g_on_%02d" % i for i in range(1, 17)]
GL = ["g_offl_%02d" % i for i in range(1, 15)]
GP = ["g_offp_%02d" % i for i in range(1, 12)]
for j, t in enumerate(GON): make(t, "on", t_end=33600 + (j * 37) % 300)
for j, t in enumerate(GL): make(t, "offL", t_end=33500 + (j * 53) % 300)
for j, t in enumerate(GP): make(t, "offP", t_end=32900 + (j * 41) % 300)
V = {}   # variant loads: name -> tag
def var(name, kind, **o):
    t = "v_" + name; make(t, kind, **o); V[name] = t; return t
var("crlf_on", "on", crlf=True); var("crlf_offl", "offL", crlf=True); var("crlf_offp", "offP", crlf=True)
var("miss_xray", "on", skip=("xray",)); var("miss_frames", "offP", skip=("frames",)); var("miss_sites", "on", skip=("sites",))
var("miss_slow", "offL", skip=("slow",)); var("miss_cond", "on", skip=("cond",)); var("empty_frames", "on", empty=("frames",))
var("bad_row", "on", bad_frame_row=True); var("bad_pass", "offP", bad_pass=True)
var("not_clean", "on", verdict="VERDICT: CONFOUNDED (other heavy process present)")
var("framecount", "offP", drop_last=True); var("seqframe", "on", seq_delta=-3); var("simtick", "on", simtick=True)
var("limiter", "offL", eco=True); var("threshold", "on", thr=9.0); var("window_short", "on", base=3.0)
var("save_unlocated", "on", save_row=False); var("no_cap_lines", "on", no_cap_lines=True)
# slow-dump cap: row counts the profiler can write (a frame is kept while rows + 14 <= 8400, so at most 8400). wu2: the
# capture's loading and precache frames over 20 ms (NSP of them in a made load, as in wu1_prime) take rows too
NSP = sum(1 for v in frames_for("on", {})[0][:E] if v > 20.0)
SLOWX = lambda n: tuple((3000.0 + 30.0 * k, 21.0) for k in range(n))      # + the save frame + NSP = n + 1 + NSP slow frames
var("slow_cap", "on", extra=SLOWX(600 - 1 - NSP), rows_per=14, count_rows=8400)
var("slow_8387", "on", extra=SLOWX(600 - 1 - NSP), rows_per=14, last_rows=1, count_rows=8387)
var("slow_8386", "on", extra=SLOWX(599 - 1 - NSP), rows_per=14, count_rows=8386)
var("verdictx", "on", verdict="VERDICTX: clean"); var("verdict2", "offP", verdict="VERDICT: clean\nVERDICT: clean")
var("inf_frame", "on", frame_text="inf"); var("nan_frame", "offL", frame_text="nan"); var("nan_thr", "on", thr=float("nan"))
var("inf_site", "offP", site_text="inf"); var("full_ms", "on", full_ms="1..2"); var("slow_ms", "offP", bad_slow_ms="1..2")
var("inf_slow_ms", "offL", bad_slow_ms="inf")
var("cap_order", "on", cap_fin_early=True); var("cap_late", "offP", cap_late=True)
var("no_opu", "on", drop_lines=("* alife_opu ",)); var("no_pass", "offP", drop_lines=("* alife_pass ",))
var("opu_renamed", "offL", rename_opu=True)
var("cordon2", "offP", save_name="cordon2"); var("det_mode", "on", more_flags=("-det_mode",))
var("sim_tick_flag", "offL", more_flags=("-sim_tick", "60"))
var("on_noflag", "on", flip_flag=True); var("off_flag", "offP", flip_flag=True); var("fixed_dt", "on", fixed_dt=True)
var("warm600", "offL", warm=600); var("off_warm", "offP", off_warm=True)
for lv in ("none", "two", "after_intro", "p2", "cap", "partial", "held"):
    var("lever_" + lv, "on", lever=lv)
var("lever_held0", "on", lever="held", hold=0)            # the same path with no held frame: control, E = 71
var("no_nopresent", "on", drop_flags=("-perf_nopresent",)); var("no_noinput", "offP", drop_flags=("-perf_noinput",))
var("g_back", "offP", swap_play=True)                     # two play pass lines logged in the wrong order: time_global goes back
var("tc_back1", "on", tc_dup=1); var("tc_back2", "on", tc_dup=2)   # a stamp 1 / 2 ms below the previous stamp
# wu2: the end of loading inside the capture (prereg 5.1, 5.4); each must be caught by name
var("no_frame1", "on", no_frame1=True)                    # no CALifeSimulator slow row in capture frame 1
var("no_loading_site", "offL", no_loading_site=True)      # sites file without 'Loading...'
var("c6_three", "on", c6=3)                               # 3 net_start_client6 calls but 13 loading frames
var("move_pre_fit", "offP", f_shift={17: -10})            # a countdown line moved 10 frames back, onto the 1234 ms frame: no fit
var("move_pre_end", "on", f_shift={17: 1})                # a countdown line moved one frame in quiet frames: its countdown says E+1
var("move_pre_back", "offL", f_shift={42: -2})            # a countdown line moved two frames back: E-2
var("race", "on", race=1)                                 # first countdown line read one precache lower (A-Life thread): ok
var("race2", "on", race=2)                                # two lower: not a race
var("clk_14", "on", t_jit=14); var("clk_16", "on", t_jit=16)   # one clock line 14 / 16 ms off the frames
var("t_shift", "offP", t_shift=1000)                      # every time_global +1000 against the Time continual stamps
var("handled_early", "offL", handled_at=40)               # the loading-end line logged before a countdown line
var("frames_shift", "on", frames_shift=True)              # frames file one frame early against the slow rows
var("drop_loading", "offP", drop_loading=True)            # loading frames left out of the frames file
var("flat", "on", flat=True, save=False)                  # every frame 5.000 ms: the clock lines fit any offset
NOPLAY1 = var("noplay", "on", no_play_lines=True)         # no clock line in play: load time from the last countdown line
for nm, kind, o in (("ok", "offL", {}), ("no_opu", "offP", dict(drop_lines=("* alife_opu ",))),
                    ("no_pass", "offL", dict(drop_lines=("* alife_pass ",))), ("no_save", "offP", dict(save=False)),
                    ("no_cap", "offL", dict(no_cap_lines=True)), ("lever", "offP", dict(flip_flag=True)),
                    ("bad_row", "offL", dict(bad_frame_row=True)), ("move", "offP", dict(f_shift={17: -10})),
                    ("short", "offP", dict(base=3.0)),
                    ("conf", "offL", dict(verdict="VERDICT: CONFOUNDED (other heavy process present)"))):
    make("p_prime_" + nm, kind, **o)      # probe fixtures: the PRIME load (an OFF load, never scored)
var("reset_late", "offP", reset_late=True); var("legacy_disagree", "offL", wide_slows=10); var("wide_in_play", "offP", wide_in_play=True)
CONF = [make("v_conf_%d" % i, "on", verdict="VERDICT: CONFOUNDED (other heavy process present)") or "v_conf_%d" % i for i in range(3)]
WSHORT = [make("v_wshort_%d" % i, "on", base=3.0) or "v_wshort_%d" % i for i in range(9)]   # VOID window_short, not a stop
QUIET = [make("v_quiet_%d" % i, "offP", hitch=False) or "v_quiet_%d" % i for i in range(3)]
NOTRAIN = [make("v_notrain_%d" % i, "offP", train=False) or "v_notrain_%d" % i for i in range(3)]
WORSE = [make("v_worse_%d" % i, "on", extra=((6000.0, 200.0),)) or "v_worse_%d" % i for i in range(9)]
TRAIN = [make("v_train_%d" % i, "on", extra=tuple((3000.0 + 500.0 * k, 25.0) for k in range(11))) or "v_train_%d" % i for i in range(9)]
TRAIN10 = [make("v_train10_%d" % i, "on", extra=tuple((3000.0 + 500.0 * k, 25.0) for k in range(10))) or "v_train10_%d" % i for i in range(9)]
P7 = [make("v_p7_%d" % i, "offP", train_slow=[1, 11, 12, 13, 14, 15]) or "v_p7_%d" % i for i in range(11)]
P6 = [make("v_p6_%d" % i, "offP", train_slow=[1, 11, 12, 13, 14]) or "v_p6_%d" % i for i in range(11)]
WIN_IN = [make("v_winin_%d" % i, "on", extra=((20000.0, 200.0),)) or "v_winin_%d" % i for i in range(9)]
WIN_OUT = [make("v_winout_%d" % i, "on", extra=((31000.0, 200.0),)) or "v_winout_%d" % i for i in range(9)]
STRAY = [make("v_stray_%d" % i, "on", extra=((6000.0, 400.0),)) or "v_stray_%d" % i for i in range(2)]
LHITCH = [make("v_lhitch_%d" % i, "offL", extra=((6000.0, 200.0),)) or "v_lhitch_%d" % i for i in range(5)]
LTRAIN = [make("v_ltrain_%d" % i, "offL", extra=tuple((3000.0 + 500.0 * k, 25.0) for k in range(11))) or "v_ltrain_%d" % i for i in range(5)]
LTRAIN10 = [make("v_ltrain10_%d" % i, "offL", extra=tuple((3000.0 + 500.0 * k, 25.0) for k in range(10))) or "v_ltrain10_%d" % i for i in range(5)]
PRICE = [make("v_price_%d" % i, "on", t_end=36700 + i * 11) or "v_price_%d" % i for i in range(9)]
REALSRC = {"r_cp2": ("offL", REAL + "/cp2/xray.log"), "r_cp1": ("offP", REAL + "/cp1/xray.log"),
           "r_cp4": ("offL", REAL + "/cp4/xray.log"), "r_uc1": ("offP", REAL + "/uc1/xray.log")}
for t, (k, src) in REALSRC.items():
    make(t, k, real=src, t_end=33100)

# ---------------- real wu1_prime mutants: ONE changed file each, the rest symlinked to the real files ----------------
RP = os.path.join(REAL, "wu1_prime")
RPF = {"log": "xray.log", "frames": "perf_frames_wu1_prime.csv", "sites": "perf_sites_wu1_prime.csv",
       "slow": "perf_slow_wu1_prime.csv", "cond": "conditions.txt"}
def rpm(name, which, fn):
    """<ST>/rpm/<name>/wu1_prime/: the file `which` rewritten by fn(bytes) -> bytes, every other file a symlink."""
    d = os.path.join(ST, "rpm", name, "wu1_prime"); os.makedirs(d)
    src = open(os.path.join(RP, RPF[which]), "rb").read()
    new = fn(src)
    assert new != src, name
    with open(os.path.join(d, RPF[which]), "xb") as f:
        f.write(new)
    for k, f in RPF.items():
        if k != which:
            os.symlink(os.path.join(RP, f), os.path.join(d, f))
    return os.path.dirname(d)
def sub1(a, b):
    def f(x):
        assert x.count(a) == 1, a
        return x.replace(a, b)
    return f
def log_lines(fn):
    def f(x):
        L = x.split(b"\r\n")
        return b"\r\n".join(fn(L))
    return f
def t_plus(L):
    out = []
    for l in L:
        m = re.match(rb"^(\* alife_(?:pass|opu) frame \d+ time_global )(\d+)( .*)$", l)
        out.append(m.group(1) + str(int(m.group(2)) + 1000).encode() + m.group(3) if m else l)
    return out
def f_plus(L):
    out = []
    for l in L:
        m = re.match(rb"^(\* alife_(?:pass|opu) frame )(\d+)( .*)$", l)
        out.append(m.group(1) + str(int(m.group(2)) + 1).encode() + m.group(3) if m else l)
    return out
def drop_clock(pred):
    return lambda L: [l for l in L if not (re.match(rb"^\* alife_(pass|opu) ", l) and pred(l))]
def move_line(pat, before_pat):
    def f(L):
        i = [k for k, l in enumerate(L) if l.startswith(pat)]; assert len(i) == 1, pat
        x = L.pop(i[0])
        j = [k for k, l in enumerate(L) if l.startswith(before_pat)]; assert len(j) >= 1, before_pat
        L.insert(j[0], x)
        return L
    return f
def frames_rows(fn):
    def f(x):
        L = x.split(b"\r\n")
        head, rows = L[0], [r for r in L[1:] if r]
        vals = [r.split(b",")[1] for r in rows]
        vals = fn(vals)
        return b"\r\n".join([head] + [b"%d,%s" % (i, v) for i, v in enumerate(vals)]) + b"\r\n"
    return f
RPM = {}
RPM["opu31_to_32"] = rpm("opu31_to_32", "log", sub1(b"* alife_opu frame 31 ", b"* alife_opu frame 32 "))
RPM["pass40_to_41"] = rpm("pass40_to_41", "log", sub1(b"* alife_pass frame 40 ", b"* alife_pass frame 41 "))
RPM["no_countdown"] = rpm("no_countdown", "log", log_lines(drop_clock(lambda l: not l.split(b" precache ")[1].startswith(b"0 "))))
RPM["countdown_zeroed"] = rpm("countdown_zeroed", "log", log_lines(lambda L: [re.sub(rb" precache [1-9]\d* ", b" precache 0 ", l) if re.match(rb"^\* alife_(pass|opu) ", l) else l for l in L]))
RPM["no_clock"] = rpm("no_clock", "log", log_lines(drop_clock(lambda l: True)))
RPM["t_plus_1000"] = rpm("t_plus_1000", "log", log_lines(t_plus))
RPM["pass102_early"] = rpm("pass102_early", "log", log_lines(move_line(b"* alife_pass frame 102 ", b"* [x-ray]: Handled Necessary")))
RPM["handled_early"] = rpm("handled_early", "log", log_lines(move_line(b"* [x-ray]: Handled Necessary", b"* alife_pass frame 40 ")))
RPM["frames_shift"] = rpm("frames_shift", "frames", frames_rows(lambda v: v[1:] + v[-1:]))
RPM["frames_no_loading"] = rpm("frames_no_loading", "frames", frames_rows(lambda v: v[13:]))
RPM["sites_no_loading"] = rpm("sites_no_loading", "sites", lambda x: b"".join(l for l in x.splitlines(True) if not l.startswith(b"Loading...,")))
RPM["f_plus_1_all"] = rpm("f_plus_1_all", "log", log_lines(f_plus))
RPM["play_line_moved"] = rpm("play_line_moved", "log", sub1(b"* alife_pass frame 1015 ", b"* alife_pass frame 1016 "))
RPM["pass_swap"] = rpm("pass_swap", "log", log_lines(move_line(b"* alife_pass frame 420 ", b"* alife_pass frame 322 ")))
RPM["tcont_back"] = rpm("tcont_back", "log", sub1(b"Time continual is:20415", b"Time continual is:18000"))

# ---------------- cases ----------------
CASES = []
def order_lines(on_tags, off_tags, frames=FR, prime="p_prime_ok"):
    """The prime is a real pool load (clean) since the scorer counts its VERDICT as the driver does (MAX_CONFOUNDED)."""
    on_tags, off_tags = list(on_tags), list(off_tags)
    out = ["%s|Warm18.exe|cordon1|0|%d|20|prime" % (prime, frames)]
    for a in ["on", "off", "off", "on"] * 8:
        t = (on_tags if a == "on" else off_tags).pop(0)
        out.append("%s|Warm18.exe|cordon1|0|%d|20|%s" % (t, frames, a))
    return out
OFF_GOOD = [GP[0], GP[1], GL[0], GP[2], GP[3], GL[1], GP[4], GP[5], GL[2], GP[6], GP[7], GL[3], GP[8], GP[9], GL[4], GP[10]]
def sub(lst, old, new):
    l = list(lst); l[l.index(old)] = new; return l
def case(name, want, pats, on_tags=GON, off_tags=OFF_GOOD, lines=None, root=None, pre_out=False, parse=None, targets=(),
         one=None, prime="p_prime_ok"):
    """one = ("probe" | "check", tag): the driver's one-load modes on <root>/<tag> with this case's order file (lines)."""
    cd = os.path.join(ST, "cases", name); os.makedirs(cd)
    if one is not None:
        parse = ["%s=%s" % (one[0], os.path.join(root or POOL, one[1])), "order=" + os.path.join(cd, "wu2_order.txt")]
    if parse is None or one is not None:
        L = lines if lines is not None else order_lines(on_tags, off_tags, prime=prime)
        with open(os.path.join(cd, "wu2_order.txt"), "w") as f:
            f.write("# selftest case %s\n" % name + "\n".join(L) + "\n")
    CASES.append(dict(name=name, want=want, pats=pats, root=root or POOL, order=os.path.join(cd, "wu2_order.txt"),
                      dir=cd, pre_out=pre_out, parse=parse))

# real logs read in place by the parser alone
for d, leg in (("cp1", "P"), ("cp2", "L"), ("cp3", "P"), ("cp4", "L"), ("uc1", "P"), ("lc1", "P"), ("lc2", "P"), ("lc3", "P"), ("cpl1", "P")):
    case("real_parse_" + d, 0, [r"legacy=%s " % leg, r"alife_pass=0 alife_opu=0 warmup_full=0"], parse=REAL + "/%s/xray.log" % d)
case("real_parse_wu1_prime", 0, [r"intro=8489 handled=8492 legacy=P ", r"alife_pass=22 alife_opu=2 warmup_full=0 held=0 cap=0 time_continual=10$"],
     parse=REAL + "/wu1_prime/xray.log")
# real run folders as loads: wrong exe, fixed step, other capture, missing slow file -> FAIL by name
case("real_folders", 1, [r"LOAD cp1 OFF FAIL \[arm_mismatch\]", r"exe is .*refdet13\.exe", r"-perf_fixed_dt present",
                         r"LOAD cp2 OFF FAIL \[arm_mismatch\]", r"MISSING .*/out/uc1/perf_slow_uc1\.csv",
                         r"LOAD r1a_cor_1 ON FAIL .*\[lever_count\]", r"^WU2 RESULT: FAIL"],
     lines=["cp1|Warm18.exe|cordon1|0|3000|20|off", "cp2|Warm18.exe|cordon1|0|3000|20|off",
            "uc1|Warm18.exe|cordon1|0|3000|20|off", "r1a_cor_1|Warm18.exe|cordon1|0|3000|20|on"], root=REAL)
# ---- the real Warm18 capture out/wu1_prime (read in place): the probe passes, and prints every number (conditions 1, 3) ----
RPO = ["wu1_prime|Warm18.exe|cordon1|0|20000|20|prime", "wu2_01_on|Warm18.exe|cordon1|0|20000|20|on"]
case("real_prime_probe", 0, [
    r"^PROBE layout wu1_prime loading frames 0\.\.12 \(13 = 11 single events \+ 2 net_start_client6 calls; CALifeSimulator::CALifeSimulator in frame 1\), precache frames 13\.\.70 \(58\), first play frame E=71$",
    r"^PROBE clock wu1_prime D=18 .* from 24 clock lines: max .* 4\.27 ms \(tolerance 15\); next best D=17 leaves 1213\.2 ms$",
    r"^PROBE loading_end wu1_prime located: first play frame E=71 by the engine's counters, 71 by the countdown lines \(frame 31 precache 58 -> 71, frame 33 precache 56 -> 71, frame 34 precache 54 -> 70, frame 40 precache 49 -> 71\); 10940\.6 ms after capture start; engine time at E 19876 ms$",
    r"\(log line 8492\) is printed in the End\(\) of the last precache frame: capture 70 \(engine counters\) / 70 \(countdown lines\); log order puts it after the clock line of capture 22 and before that of capture 84; intro marker .* at capture 69 \(expected E-2 = 69\)$",
    r"^PROBE window wu1_prime 30 s from capture 71: worst 339\.8 ms, n20 20, n33 6, .*before the window: 71 frames \(13 loading 0\.\.12, 58 precache 13\.\.70\), 10940\.6 ms, 10 of them > 20 ms, worst 5761\.1 ms$",
    r"^PROBE info wu1_prime group=OFF-IN-PLAY save=#155 80 ms load=19\.88 s",
    r"load=19\.88 s \(time_global 19876 ms at capture 71 \(from frame 102 T 19948 minus 71\.6 ms; from the last countdown line 19876 ms; estimate: -\d+\.\d / \+\d+\.\d ms = frame E \d+\.\d ms / frame 84 \d+\.\d ms, \+ 1 ms\)\)$",
    r"^PROBE wu1_prime ok$"],
     lines=RPO, root=REAL, one=("probe", "wu1_prime"))
for nm, want, pats in (
        ("opu31_to_32", 1, [r"^PROBE STOP wu1_prime clock_fit: no device-frame offset .* best D=\d+ leaves \d+\.\d ms > 15 ms"]),
        ("pass40_to_41", 1, [r"^PROBE STOP wu1_prime clock_fit: no device-frame offset .* best D=18 leaves 17\.0 ms > 15 ms"]),
        # the 4 countdown lines removed: the 20 play lines alone do not pin the offset (and both alife_opu lines go)
        ("no_countdown", 1, [r"^PROBE STOP wu1_prime no_measure_lines: 20 alife_pass and 0 alife_opu",
                             r"^PROBE STOP wu1_prime clock_fit: ambiguous: D=18 \(0\.8 ms\) and D=19 \(7\.1 ms\)"]),
        ("countdown_zeroed", 1, [r"^PROBE STOP wu1_prime loading_end: no clock line during the precache countdown"]),
        ("no_clock", 1, [r"^PROBE STOP wu1_prime no_measure_lines: 0 alife_pass and 0 alife_opu", r"^PROBE STOP wu1_prime clock_fit: 0 clock line"]),
        # every time_global + 1000 against the stamps: REPORTED only since review pass 1 (finding 8; stated limit)
        ("t_plus_1000", 0, [r"^PROBE note wu1_prime clock_cross \(reported only\): log line 7046 Time continual 15175 after log line 7045 time_global 16175: 1000 ms back",
                            r"^PROBE wu1_prime ok$"]),
        ("pass_swap", 1, [r"^PROBE STOP wu1_prime clock_order: log line 8512 time_global 21312 after log line 8511 time_global 21765: time_global goes back"]),
        ("tcont_back", 1, [r"^PROBE STOP wu1_prime clock_order: log line 8497 Time continual 18000 after log line 8130 Time continual 18905: Time continual goes back"]),
        ("pass102_early", 1, [r"^PROBE STOP wu1_prime clock_order: a clock line of capture frame 84 is logged before the loading-end line \(printed in frame 70\)"]),
        ("handled_early", 1, [r"^PROBE STOP wu1_prime clock_order: a clock line of capture frame 22 is logged after the loading-end line"]),
        ("frames_shift", 1, [r"^PROBE STOP wu1_prime frames_slow: \d+ perf_slow row\(s\) disagree with perf_frames \(first: frame 1 frame_ms 5761\.1038"]),
        ("frames_no_loading", 1, [r"^PROBE STOP wu1_prime framecount: frames file 19987 rows, sites header 20000"]),
        ("sites_no_loading", 1, [r"^PROBE STOP wu1_prime stock_loop: 'Process seqFrame' main calls 19987 != frames 20000 - loading frames 0",
                                 r"^PROBE STOP wu1_prime loading_frames: 'Loading\.\.\.' 0 calls != 'LoadDraw' 13"]),
        # limits, stated: a renumbering of every device frame is invisible and harmless (same E); one play line moved
        # one frame is under the tolerance and does not touch E or the window
        ("f_plus_1_all", 0, [r"^PROBE clock wu1_prime D=19 ", r"E=71 by the engine's counters, 71 by the countdown", r"^PROBE wu1_prime ok$"]),
        ("play_line_moved", 0, [r"^PROBE clock wu1_prime D=18 .* max .* \d+\.\d\d ms \(tolerance 15\)", r"^PROBE wu1_prime ok$"])):
    case("real_prime_" + nm, want, pats + ([r"^PROBE wu1_prime FAIL$"] if want else []), lines=RPO, root=RPM[nm],
         one=("probe", "wu1_prime"))
# made loads spliced into real engine logs (real CRLF bytes, real alife_slow lines, real intro_start)
case("real_spliced", 0, [r"LOAD r_cp2 OFF VALID group=OFF-IN-LOADING", r"LOAD r_cp1 OFF VALID group=OFF-IN-PLAY",
                         r"LOAD r_cp4 OFF VALID group=OFF-IN-LOADING", r"LOAD r_uc1 OFF VALID group=OFF-IN-PLAY",
                         r"legacy L \(max 133 alife_slow", r"^WU2 RESULT: PASS"],
     off_tags=sub(sub(sub(sub(OFF_GOOD, GL[0], "r_cp2"), GP[0], "r_cp1"), GL[1], "r_cp4"), GP[1], "r_uc1"))
# made
case("good", 0, [r"GROUPS .*ON 16, OFF-IN-PLAY 11, OFF-IN-LOADING 5, OFF-OTHER 0", r"H1 .* -> ok", r"H2 .* -> ok",
                 r"H3 .* -> ok", r"SAME AS GOOD ORDER .* -> SAME", r"save=#\d+ 900 ms", r"^WU2 RESULT: PASS$"])
# condition 3: the 13 loading frames (one 5761 ms) and the 58 precache frames (1234, 2494, 489 ms ...) are NOT in the window
case("loading_outside_window", 0, [r"LOAD g_on_01 ON VALID group=ON worst=5\.\d n20=0 n33=0 ",
                                   r"window from capture 71; before the window: 71 frames \(13 loading 0\.\.12, 58 precache 13\.\.70\), \d+\.\d ms, 9 of them > 20 ms, worst 5761\.1 ms",
                                   r"LOAD g_offp_01 OFF VALID group=OFF-IN-PLAY worst=320\.0 n20=17 ", r"^WU2 RESULT: PASS$"])
case("good_crlf", 0, [r"LOAD v_crlf_on ON VALID", r"LOAD v_crlf_offl OFF VALID group=OFF-IN-LOADING",
                      r"LOAD v_crlf_offp OFF VALID group=OFF-IN-PLAY", r"^WU2 RESULT: PASS$"],
     on_tags=sub(GON, GON[0], V["crlf_on"]), off_tags=sub(sub(OFF_GOOD, GL[0], V["crlf_offl"]), GP[0], V["crlf_offp"]))
for nm, slot, pat in (("miss_xray", "on", r"MISSING .*/v_miss_xray/xray\.log"), ("miss_frames", "offp", r"MISSING .*/perf_frames_v_miss_frames\.csv"),
                      ("miss_sites", "on", r"MISSING .*/perf_sites_v_miss_sites\.csv"), ("miss_slow", "offl", r"MISSING .*/perf_slow_v_miss_slow\.csv"),
                      ("miss_cond", "on", r"MISSING .*/v_miss_cond/conditions\.txt"), ("empty_frames", "on", r"EMPTY .*/perf_frames_v_empty_frames\.csv"),
                      ("bad_row", "on", r"UNPARSABLE .*perf_frames_v_bad_row\.csv row"), ("bad_pass", "offp", r"UNPARSABLE .*v_bad_pass/xray\.log line")):
    ot = sub(GON, GON[2], V[nm]) if slot == "on" else GON
    ft = OFF_GOOD if slot == "on" else sub(OFF_GOOD, GP[2] if slot == "offp" else GL[2], V[nm])
    case("f_" + nm, 1, [pat, r"^WU2 RESULT: FAIL"], on_tags=ot, off_tags=ft)
case("f_missing_folder", 1, [r"MISSING folder .*/v_absent$", r"^WU2 RESULT: FAIL"], on_tags=sub(GON, GON[5], "v_absent"))
for nm, slot, code in (("not_clean", "on", "not_clean"), ("framecount", "offp", "framecount"), ("seqframe", "on", "stock_loop"),
                       ("simtick", "on", "stock_loop"), ("limiter", "offl", "frame_limiter"), ("threshold", "on", "threshold_header"),
                       ("window_short", "on", "window_short"), ("save_unlocated", "on", "save_unlocated"), ("no_cap_lines", "on", "capture_lines"),
                       ("slow_cap", "on", "slow_cap"),
                       # wu2 layout checks
                       ("no_frame1", "on", "loading_frames"), ("no_loading_site", "offl", "stock_loop"), ("c6_three", "on", "loading_frames"),
                       ("move_pre_fit", "offp", "clock_fit"), ("move_pre_end", "on", "loading_end"), ("move_pre_back", "offl", "loading_end"),
                       ("race2", "on", "loading_end"), ("clk_16", "on", "clock_fit"), ("g_back", "offp", "clock_order"), ("tc_back2", "on", "clock_order"),
                       ("handled_early", "offl", "clock_order"), ("frames_shift", "on", "frames_slow"), ("drop_loading", "offp", "framecount"),
                       ("flat", "on", "clock_fit"), ("cap_late", "offp", "capture_lines")):
    ot = sub(GON, GON[3], V[nm]) if slot == "on" else GON
    ft = OFF_GOOD if slot == "on" else sub(OFF_GOOD, GP[3] if slot == "offp" else GL[3], V[nm])
    case("v_" + nm, 0, [r"LOAD %s \S+ VOID \[%s\]" % (V[nm], code), r"^WU2 RESULT: PASS$"], on_tags=ot, off_tags=ft)
case("v_flat_msg", 0, [r"clock_fit: ambiguous: D=-?\d+ \(0\.0 ms\) and D=-?\d+ \(0\.0 ms\) both fit"], on_tags=sub(GON, GON[3], V["flat"]))
case("v_no_loading_site_msg", 0, [r"loading_frames: 'Loading\.\.\.' 0 calls != 'LoadDraw' 13"], off_tags=sub(OFF_GOOD, GL[3], V["no_loading_site"]))
case("t_shift_cross", 0, [r"LOAD v_t_shift OFF VALID group=OFF-IN-PLAY", r"^    clock_cross \(reported only\): .*: 1000 ms back across the two clocks",
                          r"^WU2 RESULT: PASS$"], off_tags=sub(OFF_GOOD, GP[3], V["t_shift"]))
case("tc_back1_ok", 0, [r"LOAD v_tc_back1 ON VALID group=ON", r"^WU2 RESULT: PASS$"], on_tags=sub(GON, GON[3], V["tc_back1"]))
case("v_g_back_msg", 0, [r"clock_order: log line \d+ time_global \d+ after log line \d+ time_global \d+: time_global goes back"], off_tags=sub(OFF_GOOD, GP[3], V["g_back"]))
case("v_tc_back2_msg", 0, [r"clock_order: log line \d+ Time continual (\d+) after log line \d+ Time continual \d+: Time continual goes back"], on_tags=sub(GON, GON[3], V["tc_back2"]))
case("race_ok", 0, [r"LOAD v_race ON VALID group=ON", r"^WU2 RESULT: PASS$"], on_tags=sub(GON, GON[3], V["race"]))
case("clk_14_ok", 0, [r"LOAD v_clk_14 ON VALID group=ON", r"^WU2 RESULT: PASS$"], on_tags=sub(GON, GON[3], V["clk_14"]))
case("noplay_loadtime", 0, [r"LOAD v_noplay ON VALID group=ON .*\| load=33\.00 s$",
                            r"load time: time_global 33000 ms at capture 71 \(from frame \d+ T \d+ plus \d+\.\d ms; estimate: -\d+\.\d / \+\d+\.\d ms = frame E \d+\.\d ms / frame \d+ \d+\.\d ms, \+ 1 ms\)$", r"^WU2 RESULT: PASS$"],
     on_tags=sub(GON, GON[3], V["noplay"]))
for nm, slot, pat in (("on_noflag", "on", r"-alife_warmup absent on an ON load"), ("off_flag", "offp", r"-alife_warmup present on an OFF load"),
                      ("fixed_dt", "on", r"-perf_fixed_dt present"), ("warm600", "offl", r"-perf_capture is not 0,%d,v_warm600" % FR),
                      ("no_nopresent", "on", r"arm_mismatch: -perf_nopresent absent \(registered measurement flag\)$"),
                      ("no_noinput", "offp", r"arm_mismatch: -perf_noinput absent \(registered measurement flag\)$")):
    ot = sub(GON, GON[4], V[nm]) if slot == "on" else GON
    ft = OFF_GOOD if slot == "on" else sub(OFF_GOOD, GP[4] if slot == "offp" else GL[4], V[nm])
    case("a_" + nm, 1, [r"LOAD %s \S+ FAIL .*\[arm_mismatch\]" % V[nm], pat, r"^WU2 RESULT: FAIL"], on_tags=ot, off_tags=ft)
case("a_off_not_stock", 1, [r"LOAD v_off_warm OFF FAIL \[off_not_stock\]", r"^WU2 RESULT: FAIL"], off_tags=sub(OFF_GOOD, GP[5], V["off_warm"]))
for lv, code in (("none", "lever_count"), ("two", "lever_count"), ("after_intro", "lever_after_intro"), ("p2", "lever_precache"),
                 ("partial", "lever_partial")):
    case("l_" + lv, 1, [r"LOAD v_lever_%s ON FAIL \[%s\] group=" % (lv, code), r"^WU2 RESULT: FAIL \(1 load"], on_tags=sub(GON, GON[6], V["lever_" + lv]))
# a cap hit holds 300 frames: the made fixture does not hold them, so its countdown also disagrees (VOID loading_end beside the FAIL)
case("l_cap", 1, [r"LOAD v_lever_cap ON FAIL \[lever_cap\]", r"^WU2 RESULT: FAIL \(1 load"], on_tags=sub(GON, GON[6], V["lever_cap"]))
# a hold of K = 12 frames ADDS 12 precache frames (device.cpp:166-168, 771-773): E = 71 + 12 = 83; the no-hold control 71
case("l_held_ok", 0, [r"LOAD v_lever_held ON VALID .*\| load=33\.00 s$", r"precache held 12 frames \(cap 300\)",
                      r"window from capture 83; before the window: 83 frames \(13 loading 0\.\.12, 70 precache 13\.\.82\)",
                      r"load time: time_global 33000 ms at capture 83 \(from frame 106 T \d+ minus \d+\.\d ms",
                      r"^WU2 RESULT: PASS$"],
     on_tags=sub(GON, GON[6], V["lever_held"]))
case("l_held0_ok", 0, [r"LOAD v_lever_held0 ON VALID .*\| load=33\.00 s$",
                       r"window from capture 71; before the window: 71 frames \(13 loading 0\.\.12, 58 precache 13\.\.70\)",
                       r"load time: time_global 33000 ms at capture 71 \(from frame 94 T \d+ minus \d+\.\d ms",
                       r"^WU2 RESULT: PASS$"],
     on_tags=sub(GON, GON[6], V["lever_held0"]))
for nm, pat in (("reset_late", r"pass lines end before the narrowing"), ("legacy_disagree", r"pass lines say in-loading, alife_slow lines say P"),
                ("wide_in_play", r"wide pass only after loading ended")):
    ft = sub(OFF_GOOD, GL[1] if nm == "legacy_disagree" else GP[6], V[nm])
    case("c_" + nm, 0, [r"LOAD %s OFF VALID group=OFF-OTHER" % V[nm], pat, r"^WU2 RESULT: PASS$"], off_tags=ft)
GROUP_OFF = [GP[0], GL[0], GL[1], GP[1], GL[2], GL[3], GP[2], GL[4], GL[5], GP[3], GL[6], GL[7], GP[4], GL[8], GL[9], GL[10]]
case("grouping", 0, [r"GROUPS .*OFF-IN-PLAY 5, OFF-IN-LOADING 11", r"^WU2 RESULT: PASS$"], off_tags=GROUP_OFF)
case("nv_few_on", 3, [r"NO VERDICT \[few_on: 7 valid ON loads < 8\]", r"^WU2 RESULT: NO VERDICT \(few_on\)$"], on_tags=WSHORT + GON[:7])
# the driver stops at the MAX_CONFOUNDED-th non-clean load (prime included); such a series is FAIL even with no folder missing
case("s_stopped_last", 1, [r"^FAIL series stopped_series: 3 non-clean load\(s\) \(v_conf_0, v_conf_1, v_conf_2\) >= MAX_CONFOUNDED 3",
                           r"^WU2 RESULT: FAIL \(0 load\(s\) failed a registered check, series stopped: stopped_series\)$"],
     on_tags=sub(sub(sub(GON, GON[0], CONF[0]), GON[5], CONF[1]), GON[15], CONF[2]))
case("s_stopped_prime", 1, [r"LOAD p_prime_conf prime: not scored \(VERDICT: CONFOUNDED",
                            r"^FAIL series stopped_series: 3 non-clean load\(s\) \(p_prime_conf, v_conf_0, v_conf_1\)",
                            r"^WU2 RESULT: FAIL \(0 load\(s\) failed a registered check, series stopped"],
     on_tags=sub(sub(GON, GON[0], CONF[0]), GON[15], CONF[1]), prime="p_prime_conf")
case("s_two_ok", 0, [r"LOAD v_conf_0 ON VOID \[not_clean\]", r"LOAD v_conf_1 ON VOID \[not_clean\]", r"^WU2 RESULT: PASS$"],
     on_tags=sub(sub(GON, GON[0], CONF[0]), GON[15], CONF[1]))
case("nv_few_inplay", 3, [r"NO VERDICT \[few_inplay: 2 valid", r"^WU2 RESULT: NO VERDICT \(few_inplay\)$"], off_tags=GP[:2] + GL[:14])
case("nv_no_hitch", 3, [r"NO VERDICT \[no_stock_hitch", r"^WU2 RESULT: NO VERDICT \(no_stock_hitch\)$"], off_tags=QUIET + GL[:13])
case("nv_no_train", 3, [r"NO VERDICT \[no_stock_train", r"^WU2 RESULT: NO VERDICT \(no_stock_train, stock_orders_alike\)$"], off_tags=NOTRAIN + GL[:13])
case("h_worst", 1, [r"H1 .*\(FAIL\), and .*\(ok\) -> FAIL", r"^WU2 RESULT: FAIL \(H1\)$"], on_tags=WORSE + GON[:7])
case("h_stray_two", 1, [r"H1 .*\(ok\), and .*: 2 <= 1 \(FAIL\) -> FAIL", r"^WU2 RESULT: FAIL \(H1\)$"], on_tags=STRAY[:2] + GON[:14])
case("h_stray_one", 0, [r"H1 .*: 1 <= 1 \(ok\) -> ok", r"^WU2 RESULT: PASS$"], on_tags=STRAY[:1] + GON[:15])
case("nv_alike_worst", 3, [r"NO VERDICT \[stock_orders_alike", r"^WU2 RESULT: NO VERDICT \(stock_orders_alike\)$"],
     off_tags=GP[:11] + LHITCH)
case("nv_alike_train", 3, [r"NO VERDICT \[stock_orders_alike", r"^WU2 RESULT: NO VERDICT \(stock_orders_alike\)$"],
     off_tags=GP[:11] + LTRAIN)
case("h_train", 1, [r"H2 .* = 6\.0 >= 7 -> FAIL", r"^WU2 RESULT: FAIL \(H2\)$"], on_tags=TRAIN + GON[:7])
case("h_train_edge_ok", 0, [r"H2 .* = 7\.0 >= 7 -> ok", r"^WU2 RESULT: PASS$"], on_tags=TRAIN10 + GON[:7])
case("nv_alike_edge_ok", 0, [r"GROUPS .*OFF-IN-LOADING 5", r"^WU2 RESULT: PASS$"], off_tags=GP[:11] + LTRAIN10)
case("train_edge_ok", 0, [r"OFF-IN-PLAY +n20 +median 7\.00", r"H2 .* = 7\.0 >= 7 -> ok", r"^WU2 RESULT: PASS$"], off_tags=P7 + GL[:5])
case("nv_train_edge", 3, [r"NO VERDICT \[no_stock_train: median OFF-IN-PLAY frames > 20 ms 6\.0 < 7",
                          r"^WU2 RESULT: NO VERDICT \(no_stock_train, stock_orders_alike\)$"], off_tags=P6 + GL[:5])
case("win_in", 1, [r"H1 .*\(FAIL\), and .*\(ok\) -> FAIL", r"^WU2 RESULT: FAIL \(H1\)$"], on_tags=WIN_IN + GON[:7])
case("win_out", 0, [r"LOAD v_winout_0 ON VALID group=ON worst=5\.\d n20=0 ", r"^WU2 RESULT: PASS$"], on_tags=WIN_OUT + GON[:7])
case("good_15s_reported", 0, [r"LOAD g_offp_01 OFF VALID group=OFF-IN-PLAY worst=320\.0 n20=17 n33=10 .*\| 15s: worst=320\.0 n20=3 n33=3 ",
                              r"OFF-IN-PLAY +n20_15 +median 3\.00", r"^WU2 RESULT: PASS$"])
case("h_price", 1, [r"H3 .* -> FAIL", r"SAME AS GOOD ORDER .* -> DIFFERENT", r"^WU2 RESULT: FAIL \(H3\)$"], on_tags=PRICE + GON[:7])
G = order_lines(GON, OFF_GOOD)
case("f_verdictx", 1, [r"LOAD v_verdictx ON FAIL \[unparsable\]", r"VERDICT line\(s\) \['VERDICTX: clean'\] are not one",
                       r"^WU2 RESULT: FAIL"], on_tags=sub(GON, GON[8], V["verdictx"]))
case("f_verdict2", 1, [r"LOAD v_verdict2 OFF FAIL \[unparsable\]", r"^WU2 RESULT: FAIL"], off_tags=sub(OFF_GOOD, GP[8], V["verdict2"]))
for nm, slot, pat in (("inf_frame", "on", r"UNPARSABLE .*perf_frames_v_inf_frame\.csv row 101 '100,inf'"),
                      ("nan_frame", "offl", r"UNPARSABLE .*perf_frames_v_nan_frame\.csv row 101 '100,nan'"),
                      ("nan_thr", "on", r"UNPARSABLE .*perf_slow_v_nan_thr\.csv header"),
                      ("inf_site", "offp", r"UNPARSABLE .*perf_sites_v_inf_site\.csv row"),
                      ("full_ms", "on", r"UNPARSABLE .*v_full_ms/xray\.log line \d+ .*1\.\.2 ms.* \(not a finite number\)"),
                      ("slow_ms", "offp", r"UNPARSABLE .*v_slow_ms/xray\.log line \d+ .*1\.\.2 ms.* \(not a finite number\)"),
                      ("inf_slow_ms", "offl", r"UNPARSABLE .*v_inf_slow_ms/xray\.log line \d+ .*inf ms")):
    ot = sub(GON, GON[9], V[nm]) if slot == "on" else GON
    ft = OFF_GOOD if slot == "on" else sub(OFF_GOOD, GP[9] if slot == "offp" else GL[2], V[nm])
    case("f_" + nm, 1, [r"LOAD %s \S+ FAIL \[unparsable\]" % V[nm], pat, r"^WU2 RESULT: FAIL"], on_tags=ot, off_tags=ft)
case("v_cap_order", 0, [r"LOAD v_cap_order ON VOID \[capture_lines\]", r"^WU2 RESULT: PASS$"], on_tags=sub(GON, GON[10], V["cap_order"]))
case("f_no_opu", 1, [r"LOAD v_no_opu ON FAIL \[no_measure_lines\]", r"no_measure_lines: \d+ alife_pass and 0 alife_opu",
                     r"^WU2 RESULT: FAIL \(1 load\(s\) failed a registered check: no_measure_lines\)"], on_tags=sub(GON, GON[11], V["no_opu"]))
case("f_no_pass", 1, [r"LOAD v_no_pass OFF FAIL \[no_measure_lines\]", r"no_measure_lines: 0 alife_pass and \d+ alife_opu",
                      r"^WU2 RESULT: FAIL"], off_tags=sub(OFF_GOOD, GP[10], V["no_pass"]))
case("f_opu_renamed", 1, [r"LOAD v_opu_renamed OFF FAIL \[no_measure_lines\]", r"^WU2 RESULT: FAIL"], off_tags=sub(OFF_GOOD, GL[3], V["opu_renamed"]))
case("v_slow_8387", 0, [r"LOAD v_slow_8387 ON VOID \[slow_cap\]", r"perf_slow has 8387 rows > 8386", r"^WU2 RESULT: PASS$"],
     on_tags=sub(GON, GON[12], V["slow_8387"]))
case("slow_8386_ok", 0, [r"LOAD v_slow_8386 ON VALID group=ON", r"^WU2 RESULT: PASS$"], on_tags=sub(GON, GON[12], V["slow_8386"]))
# the probe on the PRIME load (exit 0 = go on, 1 = stop the series before any scored load, 2 = refused)
def one_order(tag, arm):
    return ["%s|Warm18.exe|cordon1|0|%d|20|%s" % (tag, FR, arm), "g_on_01|Warm18.exe|cordon1|0|%d|20|on" % FR]
case("probe_ok", 0, [r"^PROBE info p_prime_ok group=OFF-IN-LOADING save=#\d+ 900 ms load=\d",
                     r"^PROBE loading_end p_prime_ok located: first play frame E=71 by the engine's counters, 71 by the countdown lines",
                     r"^PROBE handled p_prime_ok .*: capture 70 \(engine counters\) / 70 \(countdown lines\)", r"^PROBE p_prime_ok ok$"],
     lines=one_order("p_prime_ok", "prime"), one=("probe", "p_prime_ok"))
for nm, pat in (("no_opu", r"no_measure_lines"), ("no_pass", r"no_measure_lines"), ("no_save", r"no_save_frame"),
                ("no_cap", r"capture_lines"), ("lever", r"arm_mismatch: .*-alife_warmup present on an PRIME load"),
                ("bad_row", r"unparsable"), ("move", r"clock_fit"),
                ("short", r"window_short: capture covers \d+\.\d s after the end of loading < 30 s")):
    case("probe_" + nm, 1, [r"^PROBE STOP p_prime_%s %s" % (nm, pat), r"^PROBE p_prime_%s FAIL$" % nm],
         lines=one_order("p_prime_" + nm, "prime"), one=("probe", "p_prime_" + nm))
case("probe_refused", 2, [r"REFUSED: g_on_01 is not the prime load"], lines=one_order("p_prime_ok", "prime"), one=("probe", "g_on_01"))
# the per-load check after every scored load (same rules as scoring; stop on a FAIL or a stop-VOID, not on not_clean)
def chk(name, want, tag, arm, pats):
    case(name, want, pats + [r"^CHECK %s %s$" % (tag, "ok" if want == 0 else "FAIL")],
         lines=["wu2_prime|Warm18.exe|cordon1|0|%d|20|prime" % FR, "%s|Warm18.exe|cordon1|0|%d|20|%s" % (tag, FR, arm)],
         one=("check", tag))
chk("check_ok", 0, "g_on_01", "on", [r"^CHECK info g_on_01 group=ON"])
chk("check_not_clean", 0, V["not_clean"], "on", [r"^CHECK note v_not_clean not_clean"])
chk("check_cordon2", 1, V["cordon2"], "off", [r"^CHECK STOP v_cordon2 arm_mismatch: save is not cordon1"])
chk("check_det_mode", 1, V["det_mode"], "on", [r"^CHECK STOP v_det_mode arm_mismatch: -det_mode present"])
chk("check_sim_tick", 1, V["sim_tick_flag"], "off", [r"^CHECK STOP v_sim_tick_flag arm_mismatch: -sim_tick present"])
chk("check_lever_absent", 1, V["on_noflag"], "on", [r"^CHECK STOP v_on_noflag arm_mismatch: -alife_warmup absent on an ON"])
chk("check_lever_present", 1, V["off_flag"], "off", [r"^CHECK STOP v_off_flag arm_mismatch: -alife_warmup present on an OFF"])
chk("check_no_cap", 1, V["no_cap_lines"], "on", [r"^CHECK STOP v_no_cap_lines capture_lines"])
chk("check_cap_order", 1, V["cap_order"], "on", [r"^CHECK STOP v_cap_order capture_lines"])
chk("check_cap_late", 1, V["cap_late"], "off", [r"^CHECK STOP v_cap_late capture_lines"])
chk("check_framecount", 1, V["framecount"], "off", [r"^CHECK STOP v_framecount framecount"])
chk("check_lever_cap", 1, V["lever_cap"], "on", [r"^CHECK STOP v_lever_cap lever_cap"])
chk("check_missing", 1, V["miss_slow"], "off", [r"^CHECK STOP v_miss_slow missing: MISSING"])
chk("check_window_short", 0, V["window_short"], "on", [r"^CHECK note v_window_short window_short"])
chk("check_loading_frames", 1, V["no_frame1"], "on", [r"^CHECK STOP v_no_frame1 loading_frames: perf_slow names CALifeSimulator::CALifeSimulator in capture frame\(s\) none"])
chk("check_clock_fit", 1, V["move_pre_fit"], "off", [r"^CHECK STOP v_move_pre_fit clock_fit"])
chk("check_loading_end", 1, V["move_pre_end"], "on", [r"^CHECK STOP v_move_pre_end loading_end: .* -> 72"])
chk("check_clock_order", 1, V["g_back"], "off", [r"^CHECK STOP v_g_back clock_order: .*time_global goes back"])
chk("check_clock_cross", 0, V["t_shift"], "off", [r"^CHECK note v_t_shift clock_cross \(reported only\)"])
chk("check_no_nopresent", 1, V["no_nopresent"], "on", [r"^CHECK STOP v_no_nopresent arm_mismatch: -perf_nopresent absent"])
chk("check_held", 0, V["lever_held"], "on", [r"^CHECK loading_end v_lever_held located: first play frame E=83 by the engine's counters, 83 by the countdown lines .* \+ 12 held;"])
chk("check_held0", 0, V["lever_held0"], "on", [r"^CHECK loading_end v_lever_held0 located: first play frame E=71 by the engine's counters, 71 by the countdown lines \(.*frame 86 precache 3 -> 71\);"])
chk("check_frames_slow", 1, V["frames_shift"], "on", [r"^CHECK STOP v_frames_shift frames_slow"])
case("check_refused", 2, [r"REFUSED: wu2_prime is not a scored load"], lines=one_order("wu2_prime", "prime"), one=("check", "wu2_prime"))
case("r_out_exists", 2, [r"REFUSED: .*score\.txt exists, not overwriting"], pre_out=True)
case("r_uppercase", 2, [r"REFUSED: tag G_on_01 is not lowercase"], lines=[G[0], G[1].replace("g_on_01", "G_on_01", 1)] + G[2:])
case("r_dup", 2, [r"REFUSED: tag g_on_01 listed twice"], lines=G + [G[1]])
case("r_bad_arm", 2, [r"REFUSED: arm 'maybe'"], lines=G[:1] + [G[1].replace("|on", "|maybe")] + G[2:])
case("r_fields", 2, [r"REFUSED: order line .* is not tag\|exe"], lines=G[:1] + [G[1].rsplit("|", 1)[0]] + G[2:])

# ---------------- runner ----------------
def run(scorer, c, outdir):
    out = os.path.join(outdir, "score.txt")
    if c["pre_out"] and not os.path.exists(out):
        with open(out, "w") as f:
            f.write("an earlier score that must not be overwritten\n")
    before = open(out, "rb").read() if os.path.exists(out) else None
    args = [sys.executable, "-I", scorer] + ((c["parse"] if isinstance(c["parse"], list) else ["parse=" + c["parse"]])
                                             if c["parse"] else
                                             ["root=" + c["root"], "order=" + c["order"], "out=" + out])
    p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    txt = p.stdout.decode("ascii", "replace")
    if os.path.exists(out) and not c["parse"]:
        txt += "\n" + open(out, "rb").read().decode("ascii", "replace")
    why = []
    if p.returncode != c["want"]:
        why.append("exit %d, wanted %d" % (p.returncode, c["want"]))
    if before is not None and open(out, "rb").read() != before:
        why.append("existing out file changed")
    for pt in c["pats"]:
        if not re.search(pt, txt, re.M):
            why.append("no match: " + pt)
    with open(os.path.join(outdir, "stdout.txt"), "w") as f:
        f.write(txt)
    return why

npass = nfail = nreal = nmade = 0
for c in CASES:
    why = run(SC, c, c["dir"])
    if c["name"].startswith("real_"): nreal += 1
    else: nmade += 1
    if why:
        nfail += 1; print("FAIL %s: %s" % (c["name"], "; ".join(why)))
    else:
        npass += 1; print("ok   %s" % c["name"])

# ---------------- mutants: one check switched off each; every one must be caught ----------------
MUT = {"missing_fail": ["f_miss_xray"], "cr_strip": ["good_crlf"], "not_clean": ["v_not_clean"], "framecount": ["v_framecount"],
       "stock_loop": ["v_seqframe", "v_simtick"], "frame_limiter": ["v_limiter"], "threshold": ["v_threshold"],
       "capture_lines": ["v_no_cap_lines"], "slow_cap": ["v_slow_cap"], "window_short": ["v_window_short"], "save_exclude": ["good"],
       "save_unlocated": ["v_save_unlocated"], "arm_cmdline": ["a_on_noflag"], "off_not_stock": ["a_off_not_stock"],
       "lever_count": ["l_none"], "lever_before_intro": ["l_after_intro"], "lever_precache": ["l_p2"], "lever_cap": ["l_cap"],
       "lever_partial": ["l_partial"], "grouping": ["grouping"], "legacy_agree": ["c_legacy_disagree"], "cover": ["c_reset_late"],
       "l_in_loading": ["c_wide_in_play"], "nv_few_on": ["nv_few_on"], "nv_few_inplay": ["nv_few_inplay"],
       "nv_no_hitch": ["nv_no_hitch"], "nv_no_train": ["nv_no_train"], "nv_alike": ["nv_alike_worst", "nv_alike_train"],
       "h1_median": ["h_worst"], "h1_max": ["h_stray_two"],
       "h2": ["h_train"], "h3": ["h_price"], "refuse_out": ["r_out_exists"], "lowercase": ["r_uppercase"], "dup_tag": ["r_dup"],
       "finite": ["f_inf_frame", "f_inf_site", "f_nan_thr"], "verdict_form": ["f_verdictx", "f_verdict2"],
       "log_num": ["f_full_ms", "f_slow_ms"], "held_k": ["l_held_ok", "check_held"], "capture_order": ["v_cap_order", "v_cap_late", "check_cap_order"],
       "measure_lines": ["f_no_opu", "f_no_pass", "f_opu_renamed", "probe_no_opu"], "probe_save": ["probe_no_save"],
       # wu2 hooks: the end of loading inside the capture
       "loading_frames": ["v_no_frame1", "v_no_loading_site_msg", "v_c6_three", "real_prime_sites_no_loading"],
       "clock_fit": ["v_move_pre_fit", "v_clk_16", "real_prime_opu31_to_32", "real_prime_pass40_to_41"], "clock_unique": ["v_flat"],
       "loading_end": ["v_move_pre_end", "v_move_pre_back", "v_race2", "real_prime_countdown_zeroed"],
       "handled_bracket": ["v_handled_early", "real_prime_pass102_early", "real_prime_handled_early"],
       "clock_monotone": ["v_g_back", "v_tc_back2", "check_clock_order", "real_prime_pass_swap", "real_prime_tcont_back"],
       "clock_cross": ["t_shift_cross", "check_clock_cross", "real_prime_t_plus_1000"],
       "frames_slow": ["v_frames_shift", "real_prime_frames_shift"],
       # review pass 1 (2026-11-26)
       "probe_window": ["probe_short"], "measure_flags": ["a_no_nopresent", "a_no_noinput", "check_no_nopresent"],
       "series_stopped": ["s_stopped_last", "s_stopped_prime"],
       "window_from_e": ["loading_outside_window", "real_prime_probe"], "stock_loop_l": ["good", "real_prime_probe"]}
src = open(SC).read()
hook = 'def on(name):\n    """Self-test mutation hook: every check below is guarded by on("<name>"). Always True in this file."""\n    return True\n'
byname = {c["name"]: c for c in CASES}
def caught_by(ms_path, md, targets, all_targets):
    """Every listed target must catch the mutant (wu2: not just the first case that differs)."""
    first = None
    for cn in targets:
        cd = os.path.join(md, cn); os.makedirs(cd)
        if not run(ms_path, byname[cn], cd):
            return None, "target %s did not catch it" % cn
        first = first or cn
    if first is None:
        for cn in all_targets:
            cd = os.path.join(md, cn); os.makedirs(cd)
            if run(ms_path, byname[cn], cd):
                return cn, ""
        return None, "survived every case"
    return first, ""
if src.count(hook) != 1:
    print("FAIL mutants: the on() hook is not found exactly once in the scorer"); nfail += 1
else:
    used = set(re.findall(r'on\("([a-z0-9_]+)"\)', src)) - {"<name>"}
    missing = sorted(used - set(MUT))
    extra = sorted(set(MUT) - used)
    if missing or extra:
        print("FAIL mutants: hooks without a mutant: %s; mutants without a hook: %s" % (", ".join(missing) or "-", ", ".join(extra) or "-")); nfail += 1
    else:
        npass += 1; print("ok   every on() hook in the scorer has a mutant (%d)" % len(used))
    for m, targets in sorted(MUT.items()):
        if m not in used:
            continue
        md = os.path.join(ST, "mutants", m); os.makedirs(md)
        ms = os.path.join(md, "wu2_score.py")
        with open(ms, "w") as f:
            f.write(src.replace(hook, 'def on(name):\n    return name != "%s"   # MUTANT\n' % m))
        cn, why = caught_by(ms, md, targets, [c["name"] for c in CASES])
        if cn:
            npass += 1; print("ok   mutant %-18s caught by %s" % (m, ", ".join(targets) or cn))
        else:
            nfail += 1; print("FAIL mutant %s: %s" % (m, why))
# ---------------- constant mutants: every changed or new constant moved off its registered value ----------------
CMUT = {"window_15_headline": ("WINDOW_MS = 30000.0 ", "WINDOW_MS = 15000.0 ", ["good", "win_in", "v_window_short"]),
        "windows_swapped": ("WINDOW_MS = 30000.0 ", "WINDOW_MS = 15000.0 ", ["good"], ("WINDOW2_MS = 15000.0 ", "WINDOW2_MS = 30000.0 ")),
        "window_45": ("WINDOW_MS = 30000.0 ", "WINDOW_MS = 45000.0 ", ["good", "win_out"]),
        "train_min_6": ("STOCK_TRAIN_MIN = 7 ", "STOCK_TRAIN_MIN = 6 ", ["nv_train_edge"]),
        "train_min_8": ("STOCK_TRAIN_MIN = 7 ", "STOCK_TRAIN_MIN = 8 ", ["train_edge_ok"]),
        "n20_drop_6": ("N20_DROP = 7 ", "N20_DROP = 6 ", ["h_train", "nv_alike_train"]),
        "n20_drop_8": ("N20_DROP = 7 ", "N20_DROP = 8 ", ["h_train_edge_ok", "nv_alike_edge_ok", "train_edge_ok"]),
        "slow_cap_rows_up": ("SLOW_CAP_ROWS = 600 * 14 - 14 ", "SLOW_CAP_ROWS = 600 * 14 - 0 ", ["v_slow_8387", "v_slow_cap"]),
        "slow_cap_rows_down": ("SLOW_CAP_ROWS = 600 * 14 - 14 ", "SLOW_CAP_ROWS = 600 * 14 - 15 ", ["slow_8386_ok"]),
        # wu2 constants, each on both sides
        "clock_tol_13": ("CLOCK_TOL_MS = 15.0 ", "CLOCK_TOL_MS = 13.0 ", ["clk_14_ok"]),
        "clock_tol_17": ("CLOCK_TOL_MS = 15.0 ", "CLOCK_TOL_MS = 17.0 ", ["v_clk_16"]),
        "clock_tol_3": ("CLOCK_TOL_MS = 15.0 ", "CLOCK_TOL_MS = 3.0 ", ["real_prime_probe"]),
        "race_0": ("RACE_FRAMES = 1 ", "RACE_FRAMES = 0 ", ["race_ok", "real_prime_probe"]),
        "race_2": ("RACE_FRAMES = 1 ", "RACE_FRAMES = 2 ", ["v_race2", "v_move_pre_back"]),
        # review pass 1
        "clock_back_0": ("CLOCK_BACK_MS = 1 ", "CLOCK_BACK_MS = 0 ", ["tc_back1_ok"]),
        "clock_back_2": ("CLOCK_BACK_MS = 1 ", "CLOCK_BACK_MS = 2 ", ["v_tc_back2"]),
        "max_conf_2": ("MAX_CONFOUNDED = 3 ", "MAX_CONFOUNDED = 2 ", ["s_two_ok"]),
        "max_conf_4": ("MAX_CONFOUNDED = 3 ", "MAX_CONFOUNDED = 4 ", ["s_stopped_last", "s_stopped_prime"])}
ncm = 0
for m, spec in sorted(CMUT.items()):
    old, new, targets = spec[0], spec[1], spec[2]
    msrc = src
    for a, b in [(old, new)] + list(spec[3:]):
        if msrc.count(a) != 1:
            print("FAIL constant mutant %s: %r not found exactly once in the scorer" % (m, a)); nfail += 1; msrc = None; break
        msrc = msrc.replace(a, b)
    if msrc is None:
        continue
    md = os.path.join(ST, "mutants", "const_" + m); os.makedirs(md)
    ms = os.path.join(md, "wu2_score.py")
    with open(ms, "w") as f:
        f.write(msrc)
    cn, why = caught_by(ms, md, targets, [c["name"] for c in CASES])
    if cn:
        npass += 1; ncm += 1; print("ok   constant mutant %-18s caught by %s" % (m, ", ".join(targets)))
    else:
        nfail += 1; print("FAIL constant mutant %s: %s" % (m, why))
# ---------------- one value in two files: the scorer's MAX_CONFOUNDED must be the driver's (wu2_run.sh) ----------------
drv = open(os.path.join(os.path.dirname(SC), "wu2_run.sh")).read()
mc_d, mc_s = re.findall(r"\bMAX_CONFOUNDED=(\d+)", drv), re.findall(r"^MAX_CONFOUNDED = (\d+) ", src, re.M)
if len(mc_d) == 1 and len(mc_s) == 1 and mc_d == mc_s:
    npass += 1; print("ok   MAX_CONFOUNDED: scorer %s = driver %s" % (mc_s[0], mc_d[0]))
else:
    nfail += 1; print("FAIL MAX_CONFOUNDED: scorer %s, driver %s (each must be set exactly once, equal)" % (mc_s, mc_d))
nmut = sum(1 for m in MUT if m in used) if src.count(hook) == 1 else 0
with open(os.path.join(ST, "counts.txt"), "w") as f:
    f.write("%d %d %d %d %d %d\n" % (npass, nfail, nreal, nmade, nmut, ncm))
PYEOF
[ -s "$ST/counts.txt" ] || { echo "WU2 SELFTEST FAIL: case runner did not finish" | tee -a "$LOG"; exit 1; }
read -r pass fail nreal nmade nmut ncm < "$ST/counts.txt"
# the real fixture folders must be untouched: nothing in them newer than the start of this self-test
# a folder that is missing or that find cannot read counts as touched, never as "unchanged"
newer_count() {   # <dir> <stamp file>: prints the number of paths newer than the stamp; returns 1 if find failed
  local out
  [ -d "$1" ] || { echo "missing $1" >&2; return 1; }
  out=$(find "$1" -newer "$2" -print) || return 1
  printf '%s' "$out" | grep -c . || true
}
newer_count_mut() {   # MUTANT of newer_count: find's exit status ignored (what review pass 1, finding 6, asked to catch)
  local out
  [ -d "$1" ] || { echo "missing $1" >&2; return 1; }
  out=$(find "$1" -newer "$2" -print)
  printf '%s' "$out" | grep -c . || true
}
nc_tests() {   # <function>: the untouched check's own tests; prints one line per failed test, returns how many failed
  local fn=$1 f=0 n
  "$fn" "$ST/t1_no_such_dir" "$ST/.t0" >/dev/null 2>&1 && { echo "missing directory counted as unchanged"; f=$((f+1)); }
  mkdir -p "$ST/t1_dir" && : > "$ST/t1_dir/a"
  # existing directory, find itself fails: the -newer reference does not exist
  "$fn" "$ST/t1_dir" "$ST/t1_no_such_stamp" >/dev/null 2>&1 && { echo "find error (missing -newer file) on an existing directory counted as unchanged"; f=$((f+1)); }
  # existing directory, find fails part-way (an unreadable subfolder; skipped as root, who reads everything)
  if [ "$(id -u)" != 0 ]; then
    mkdir -p "$ST/t1_dir/locked" && chmod 000 "$ST/t1_dir/locked"
    "$fn" "$ST/t1_dir" "$ST/.t0" >/dev/null 2>&1 && { echo "find error (unreadable subfolder) counted as unchanged"; f=$((f+1)); }
    chmod 700 "$ST/t1_dir/locked"
  fi
  # control: a readable directory works and counts its new paths
  n=$("$fn" "$ST/t1_dir" "$ST/.t0" 2>/dev/null) || { echo "a readable directory reported as a find error"; f=$((f+1)); }
  [ "${n:-0}" -ge 2 ] 2>/dev/null || { echo "a readable directory with new paths counted ${n:-nothing}"; f=$((f+1)); }
  return $f
}
nfind=0
if r=$(nc_tests newer_count); then pass=$((pass+1)); nfind=$((nfind+1)); echo "ok   untouched check fails on a missing folder and on find errors in an existing folder" | tee -a "$LOG"
else fail=$((fail+1)); echo "FAIL untouched check: $r" | tee -a "$LOG"; fi
if r=$(nc_tests newer_count_mut); then fail=$((fail+1)); echo "FAIL untouched-check mutant (find status ignored) survived" | tee -a "$LOG"
else pass=$((pass+1)); nfind=$((nfind+1)); echo "ok   untouched-check mutant (find status ignored) caught by: $(echo "$r" | tr '\n' ';')" | tee -a "$LOG"; fi

# ---- the driver's logic, offline (review pass 1, findings 3 and 9): wu2_run.sh is never run; its marked function
# block is extracted and run on a copy of wu2_order.txt in this folder; no load, no queue, no display, no claim ----
DRV="$HERE/wu2_run.sh"
DT="$ST/driver"; mkdir -p "$DT"
sed -n '/^# ---- BEGIN driver functions/,/^# ---- END driver functions/p' "$DRV" > "$DT/funcs.sh"
drv_tests() {   # <function file> <work dir>: prints one line per failed test; exit status = number failed
  ( set -u; EXE=Warm18.exe; NLOADS=33; NON=16; NOFF=16; D=$2; f=0
    . "$1"
    mkdir -p "$D"; src="$D/order_src.txt"; cp -- "$HERE/wu2_order.txt" "$src"; want=$(sha256sum "$src" | cut -d' ' -f1)
    # 1. the registered order is copied, the copy has the hash, it is read-only, and dispatch reads the copy only
    mkdir "$D/st1"; ST="$D/st1"; : > "$ST/done.txt"
    order_copy "$src" "$want" "$ST" > "$D/t1.txt" 2>&1 || { echo "order_copy refused the registered order ($(tr '\n' ' ' < "$D/t1.txt"))"; f=$((f+1)); }
    [ "$(sha256sum "$ST/wu2_order.txt" 2>/dev/null | cut -d' ' -f1)" = "$want" ] || { echo "the copy's hash differs"; f=$((f+1)); }
    if [ "$(id -u)" != 0 ] && [ -w "$ST/wu2_order.txt" ]; then echo "the copy is writable"; f=$((f+1)); fi
    p1=$(pending "$ST/wu2_order.txt")
    printf 'wu2_99_evil|Warm18.exe|cordon1|0|20000|20|on\n' >> "$src"          # the original edited after the copy
    p2=$(pending "$ST/wu2_order.txt")
    [ "$p1" = "$p2" ] && [ "$(echo "$p2" | grep -c .)" = 33 ] && ! echo "$p2" | grep -q evil || { echo "dispatch from the copy changed after the original was edited"; f=$((f+1)); }
    echo wu2_prime >> "$ST/done.txt"
    [ "$(pending "$ST/wu2_order.txt" | head -1 | cut -d'|' -f1)" = wu2_01_on ] || { echo "pending does not skip a done load"; f=$((f+1)); }
    # 2. the original changed between the preflight and the copy: the copy fails the registered hash
    mkdir "$D/st2"; ST="$D/st2"
    order_copy "$src" "$want" "$ST" > "$D/t2.txt" 2>&1 && { echo "a copy that is not the registered order was accepted"; f=$((f+1)); }
    grep -q "failed its hash check" "$D/t2.txt" || { echo "the hash refusal is not named"; f=$((f+1)); }
    # 3. no registered hash yet: refused
    mkdir "$D/st3"; ST="$D/st3"; cp -- "$HERE/wu2_order.txt" "$D/order3.txt"
    order_copy "$D/order3.txt" PIN_ME_ORDER "$ST" > "$D/t3.txt" 2>&1 && { echo "PIN_ME_ORDER accepted"; f=$((f+1)); }
    # 4. the copy exists already: refused
    order_copy "$D/order3.txt" "$want" "$D/st1" > "$D/t4.txt" 2>&1 && { echo "an existing copy was overwritten"; f=$((f+1)); }
    # 5. the hash matches but the shape is not the registered one: refused by order_ok
    mkdir "$D/st5"; ST="$D/st5"; head -3 "$HERE/wu2_order.txt" > "$D/order5.txt"
    order_copy "$D/order5.txt" "$(sha256sum "$D/order5.txt" | cut -d' ' -f1)" "$ST" > "$D/t5.txt" 2>&1 && { echo "a 2-load order accepted"; f=$((f+1)); }
    # 6. slice deadline: a load starts only with >= BENCH_TO + END_RESERVE s left
    now=$(date +%s)
    slice_ok $((now + 1100)) 1020 || { echo "slice_ok refused 1100 s left for 1020 s"; f=$((f+1)); }
    slice_ok $((now + 1000)) 1020 && { echo "slice_ok accepted 1000 s left for 1020 s"; f=$((f+1)); }
    slice_ok $((now - 5)) 0 && { echo "slice_ok accepted a passed deadline"; f=$((f+1)); }
    exit $f )
}
ndrv=0
if r=$(drv_tests "$DT/funcs.sh" "$DT/real"); then pass=$((pass+1)); ndrv=$((ndrv+1)); echo "ok   driver functions offline (order copy + hash, dispatch from the copy, slice deadline)" | tee -a "$LOG"
else fail=$((fail+1)); echo "FAIL driver functions: $(echo "$r" | tr '\n' ';')" | tee -a "$LOG"; fi
# driver mutants: each must be caught by the offline tests
for m in no_copy_hash:'/pin "\$dst" "\$want"/d' copy_writable:'/chmod a-w "\$dst"/d' slice_always:'s/\[ \$(( \$1 - \$(date +%s) )) -ge "\$2" \]/true/'; do
  name=${m%%:*}; expr=${m#*:}
  sed "$expr" "$DT/funcs.sh" > "$DT/funcs_$name.sh"
  if cmp -s "$DT/funcs.sh" "$DT/funcs_$name.sh"; then fail=$((fail+1)); echo "FAIL driver mutant $name: the sed did not change the functions" | tee -a "$LOG"; continue; fi
  if [ "$name" = copy_writable ] && [ "$(id -u)" = 0 ]; then echo "skip driver mutant $name (root writes read-only files)" | tee -a "$LOG"; continue; fi
  if r=$(drv_tests "$DT/funcs_$name.sh" "$DT/m_$name"); then fail=$((fail+1)); echo "FAIL driver mutant $name survived" | tee -a "$LOG"
  else pass=$((pass+1)); ndrv=$((ndrv+1)); echo "ok   driver mutant $name caught by: $(echo "$r" | tr '\n' ';')" | tee -a "$LOG"; fi
done
# the main body (not in the function block) must use the copy and recheck the deadline before the bench: static checks
if awk '/^order_copy "\$ORDER" "\$ORDER_SHA" "\$ST"/{c=NR} /^ORDER=\$ST\/wu2_order.txt$/{if (c && NR==c+1) ok=1} END{exit !ok}' "$DRV" && \
   [ "$(grep -c 'pending "\$ORDER"' "$DRV")" = 3 ] && ! grep -q '\bpending |' "$DRV" && grep -q 'order=\$ORDER 2>&1' "$DRV" && \
   awk '/quiet-required.sh" claim \$W 30 "\$WHAT" >\/dev\/null 2>&1 \|\| \{ stop="claim renewal/{r=NR} /slice_ok \$SL_END \$\(\(BENCH_TO \+ END_RESERVE\)\)/{s=NR} /tools\/s2_bench.sh "\$exe"/{b=NR} END{exit !(r && s > r && b > s)}' "$DRV"; then
  pass=$((pass+1)); ndrv=$((ndrv+1)); echo "ok   driver body: ORDER = the state-dir copy right after order_copy; pending/check use it; deadline rechecked after the renewal, before the bench" | tee -a "$LOG"
else fail=$((fail+1)); echo "FAIL driver body: copy not used everywhere, or the deadline recheck is not between the claim renewal and the bench" | tee -a "$LOG"; fi
touched=0
for t in cp1 cp2 cp3 cp4 uc1 lc1 lc2 lc3 cpl1 r1a_cor_1 wu1_prime; do
  if n=$(newer_count "$REAL/$t" "$ST/.t0"); then touched=$((touched + n))
  else touched=$((touched + 1)); echo "FAIL fixture folder $REAL/$t could not be checked" | tee -a "$LOG"; fi
done
if [ "$touched" != 0 ]; then fail=$((fail+1)); echo "FAIL real fixture folders changed during the self-test ($touched paths)" | tee -a "$LOG"
else pass=$((pass+1)); echo "ok   real fixture folders unchanged" | tee -a "$LOG"; fi
tot=$((pass+fail))
if [ "$fail" = 0 ]; then echo "WU2 SELFTEST PASS $pass/$tot ($nreal real cases, $nmade made cases, $nmut hook mutants caught, $ncm constant mutants caught, 1 hook-coverage check, 1 MAX_CONFOUNDED agreement check, $nfind find-error checks, $ndrv driver checks, 1 untouched check) in $ST" | tee -a "$LOG"; exit 0
else echo "WU2 SELFTEST FAIL $fail of $tot in $ST" | tee -a "$LOG"; exit 1; fi
