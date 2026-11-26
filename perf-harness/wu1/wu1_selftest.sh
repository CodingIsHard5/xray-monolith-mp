#!/usr/bin/env bash
# wu1 scorer self-test (PREREG-wu1-alife-warmup-2026-11-26.md section 8). DRAFT.
#   bash wu1_selftest.sh     -> builds wu1/selftest-runs/<stamp>/, runs every case and every mutant, prints one final line
# Real fixtures: earlier run folders and logs under <project>/out, read IN PLACE (never copied whole, never written);
#   real xray.log bytes (CRLF) are also spliced into made loads so the classifier is exercised on real engine output.
# Made fixtures: loads written to a plan into <stamp>/pool (one made load per fixture, shared by the cases).
# Mutants: copies of the scorer with ONE check switched off (its on("<name>") hook returns False); each must be caught.
# Nothing is ever deleted. The stamp folder must be new. Output also saved as <stamp>/selftest_output.txt.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
P="$(cd "$HERE/../../.." && pwd)"           # project root (wt-render-split/perf-harness/wu1 -> project)
REAL="$P/out"
SC="$HERE/wu1_score.py"
[ -s "$SC" ] || { echo "WU1 SELFTEST FAIL: scorer $SC missing"; exit 2; }
[ -d "$REAL" ] || { echo "WU1 SELFTEST FAIL: real run root $REAL missing"; exit 2; }
ST="$HERE/selftest-runs/$(date +%Y%m%d-%H%M%S)-$$"
[ -e "$ST" ] && { echo "WU1 SELFTEST FAIL: $ST exists"; exit 2; }
mkdir -p "$ST/cases" "$ST/pool" "$ST/mutants"
touch "$ST/.t0"; sleep 1
LOG="$ST/selftest_output.txt"

python3 -I - "$ST" "$REAL" "$SC" <<'PYEOF' 2>&1 | tee -a "$LOG"
import os, re, subprocess, sys
ST, REAL, SC = sys.argv[1], sys.argv[2], sys.argv[3]
POOL = os.path.join(ST, "pool")
FR = 7000                      # frames per made load (registered per line in the case order files); ~35 s at 5 ms, so
                               # the 30 s headline window is covered (Revision 2; was 4000 = 20 s for the 15 s window)
F0 = 64                        # engine frame of the first frame after the loading screen in made logs
LIMIT_FULL, LIMIT_WIDE = 4294967295, 65534

# ---------------- made loads ----------------
def frames_for(kind, o):
    base = o.get("base", 5.0)
    ms = [base + 0.3 * ((i * 7919) % 7) / 6.0 for i in range(FR)]
    ev = []                                            # (time ms, frame ms, zone)
    if o.get("save", True):
        ev.append((2000.0, o.get("save_ms", 900.0), "CALifeStorageManager::save" if o.get("save_row", True) else "Device: other work"))
    # stock in-play shape from the real fixed-step captures (prereg 6, Revision 2): hitch ~7 s after loading, then 27
    # passes 452 ms apart from ~9.8 s; the passes over 20 ms are pass 1 and the late passes 11.. (MEASURED: 16-19 frames
    # > 20 ms in 30 s, 2-6 in 15 s). Default: hitch + 16 slow passes = 17 in 30 s, 3 in 15 s; other passes 15 ms.
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
        while i < len(ms) - 1 and c < t:
            c += ms[i]; i += 1
        while i in zone and i < len(ms) - 1:
            i += 1
        ms[i] = v; zone[i] = z
    return [round(v, 4) for v in ms], zone

def pline(F, T, Pc, n, M, lim): return "* alife_pass frame %d time_global %d precache %d n %d of %d limit %d" % (F, T, Pc, n, M, lim)
def oline(F, T, Pc, v): return "* alife_opu frame %d time_global %d precache %d value %d" % (F, T, Pc, v)
def sline(F, i, name, v): return "* alife_slow frame %d id %d %s [%s] %.2f ms" % (F, i, name, name.rstrip("0123456789"), v)

def log_events(tag, kind, o, ms, zone):
    """Returns (loading_events [(frame, seq, text)], play_lines [text]) for a made load."""
    te = o.get("t_end", 33000)
    T = lambda F: te - (F0 - F) * 40
    Pc = lambda F: F0 - F
    L, lv = [], o.get("lever", "good")
    if kind == "on" and lv == "held":
        # Revision 3 (S4), the engine's order (device.cpp:131-170 on perf/alife-warmup): the countdown is held at 3 for
        # K = 12 frames (49..60 read precache 3) until the pass is claimed at frame 61, whose End() prints the held line
        # and counts 3 -> 2; frame 64 is the first with precache 0. Loading frames 200 ms of time_global apart, so the
        # 10 s pass-log window (from the pass at frame 6) has closed before the full pass: its alife_pass line is not
        # printed, and the last line with precache > 0 is the actor-reinit alife_opu at frame 50, inside the hold
        # (precache 3). The narrowing alife_opu (not windowed) is the first in-play line, capture frame 5.
        Th = lambda F: te - (F0 - F) * 200
        Ph = lambda F: F0 - F if F > F0 - 3 else max(3, F0 - 12 - F)
        L = [(6, 0, pline(6, Th(6), Ph(6), 3, 2400, 3)), (17, 0, pline(17, Th(17), Ph(17), 3, 2400, 3)),
             (28, 0, oline(28, Th(28), Ph(28), 20)), (42, 0, pline(42, Th(42), Ph(42), 3, 2400, 3)),
             (50, 0, oline(50, Th(50), Ph(50), LIMIT_WIDE))]
        L += [(61, 1, sline(61, 14200 + j, "zat_b106_smart_terrain" if j == 0 else "stalker_sim_squad_novice%d" % (19000 + j), 300.0 if j == 0 else 2.1)) for j in range(130)]
        L.append((61, 2, "* [alife_warmup] full pass: 2400 of 2400 objects, frame 61, precache_left 3, 612.3 ms"))
        L.append((61, 3, "* [alife_warmup] precache held 12 frames"))
        cum = [0.0]
        for v in ms[:6]:
            cum.append(cum[-1] + v)
        return L, [oline(F0 + 5, int(round(te + cum[5])), 0, 20)]
    L.append((6, 0, pline(6, T(6), Pc(6), 3, 2400, 3)))
    L.append((17, 0, pline(17, T(17), Pc(17), 3, 2400, 3)))
    L.append((28, 0, oline(28, T(28), Pc(28), LIMIT_WIDE)))
    wide_slows = lambda F, k: [(F, 1, sline(F, 14200 + j, "zat_b106_smart_terrain" if j == 0 else "stalker_sim_squad_novice%d" % (19000 + j), 300.0 if j == 0 else 2.1)) for j in range(k)]
    if kind == "on":
        Ff, Pf, seq = 31, Pc(31), 2
        if lv == "after_intro": Ff, seq = 62, 60
        if lv == "p2": Ff, Pf, seq = 62, 2, 0
        N = 1000 if lv == "partial" else 2400
        if lv != "none":
            L += wide_slows(Ff, 130)
            L.append((Ff, seq, "* [alife_warmup] full pass: %d of 2400 objects, frame %d, precache_left %d, %s ms" % (N, Ff, Pf, o.get("full_ms", "612.3"))))
            L.append((Ff, seq + 1, pline(Ff, T(Ff), Pf, N, 2400, LIMIT_FULL)))
        if lv == "two":
            L.append((33, 2, "* [alife_warmup] full pass: 2400 of 2400 objects, frame 33, precache_left 31, 20.1 ms"))
        if lv == "cap":
            L.append((62, 1, "! [alife_warmup] hold cap hit after 300 frames, pass not run"))
        L.append((42, 0, pline(42, T(42), Pc(42), 2400, 2400, LIMIT_WIDE)))
        L.append((50, 0, oline(50, T(50), Pc(50), 20)))
        L.append((53, 0, pline(53, T(53), Pc(53), 20, 2400, 20)))
    elif kind == "offL":
        L += wide_slows(31, o.get("wide_slows", 130))
        L.append((31, 2, pline(31, T(31), Pc(31), 2400, 2400, LIMIT_WIDE)))
        L.append((42, 0, pline(42, T(42), Pc(42), 2400, 2400, LIMIT_WIDE)))
        L.append((50, 0, oline(50, T(50), Pc(50), 20)))
        L.append((53, 0, pline(53, T(53), Pc(53), 20, 2400, 20)))
    else:  # offP: one long loading frame carried time_global past the 1 s reset before the next pass
        if not o.get("reset_late"):
            L.append((29, 0, oline(29, T(29), Pc(29), 20)))
        for F in (31, 42, 53):
            L.append((F, 0, pline(F, T(F), Pc(F), 20, 2400, 20)))
    if o.get("off_warm"):
        L.append((62, 1, "* [alife_warmup] precache held 3 frames"))
    if o.get("bad_pass"):
        L.append((40, 0, "* alife_pass frame 40 time_global garbage"))
    if o.get("bad_slow_ms"):
        L.append((40, 0, "* alife_slow frame 40 id 14200 zat_b106_smart_terrain [zat_b106_smart_terrain] %s ms" % o["bad_slow_ms"]))
    # in play: every 452 ms of time_global while within 10 s of the first pass (frame 6)
    play, cum = [], [0.0]
    for v in ms:
        cum.append(cum[-1] + v)
    t_first = T(6)
    if not o.get("no_play_lines"):
        k = 0
        while k < len(ms) and te + cum[k] - t_first <= 10000:
            n = 20
            lim = 20
            if o.get("wide_in_play") and k == 0:
                n, lim = 2400, LIMIT_WIDE
            play.append((k, pline(F0 + k, int(round(te + cum[k])), 0, n, 2400, lim)))
            k += 88
    if o.get("reset_late"):
        play.append((len(ms) - 1, oline(F0 + 2500, int(te + 12000), 0, 20)))
    if o.get("wide_in_play"):
        for j in range(130):
            play.append((0, sline(F0, 15000 + j, "stalker_sim_squad_novice%d" % (15000 + j), 2.0)))
    for i, z in sorted(zone.items()):
        if z == "ALife/scheduled/update":
            play.append((i, sline(F0 + i, 14200, "zat_b106_smart_terrain", ms[i] - 5.0)))
    play.sort(key=lambda x: x[0])
    # Revision 3 (S5): a build that prints no alife_pass / alife_opu line, or prints them under another prefix
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
    t += list(o.get("more_flags", ()))
    if (arm == "on") != bool(o.get("flip_flag")): t.append("-alife_warmup")
    return " ".join(t)

def write(path, lines, crlf, empty=False):
    with open(path, "w", newline="") as f:
        if not empty:
            nl = "\r\n" if crlf else "\n"
            f.write(nl.join(lines) + nl)

def make(tag, kind, real=None, **o):
    """kind: on, offL, offP. real: path of a real xray.log to splice the made lines into (CRLF bytes kept)."""
    d = os.path.join(POOL, tag); os.makedirs(d)
    arm = "on" if kind == "on" else "off"
    ms, zone = frames_for(kind, o)
    skip, crlf = o.get("skip", ()), o.get("crlf", False)
    n_rows = FR - 1 if o.get("drop_last") else FR
    fl = ["frame,ms"] + ["%d,%.4f" % (i, v) for i, v in enumerate(ms[:n_rows])]
    if o.get("bad_frame_row"): fl[100] = "99,abc"
    if o.get("frame_text"): fl[101] = "100,%s" % o["frame_text"]
    if "frames" not in skip: write(os.path.join(d, "perf_frames_%s.csv" % tag), fl, crlf, "frames" in o.get("empty", ()))
    tot = sum(ms)
    sl = ["#frames,%d" % n_rows, "#total_ms,%.3f" % tot,
          "name,file,line,calls_main,incl_main_ms,self_main_ms,calls_other,incl_other_ms,self_other_ms",
          "Process seqFrame,z:\\src\\xrengine\\device.cpp,1121,%d,%.4f,1.0000,0,0.0000,0.0000" % (FR + o.get("seq_delta", 0), tot),
          "ALife/scheduled/update,z:\\src\\xrgame\\alife_schedule_registry.h,62,0,0.0000,0.0000,500,900.0000,900.0000"]
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
    ev.append((62, 50, "intro_start game_loaded"))
    ev.append((63, 50, "* [x-ray]: Handled Necessary Textures Destruction"))
    made = [t for _, _, t in sorted(ev, key=lambda x: (x[0], x[1]))]
    if real is None:
        head = ["* Detected CPU: AMD Ryzen 7 9800X3D 8-Core Processor", "Init FileSystem 5.5 sec",
                "Game started: 27.11.2026 10:00:09.000", "command line " + cmdline(tag, arm, o)]
        head += ["[LSS] Spawning object [m_crow%d][m_crow][%d]" % (j, j) for j in range(50)]
        if o.get("cap_fin_early"):      # S3: a finished marker inside loading, the started marker after it
            made = made[:-2] + [cap_f] + made[-2:]
        body = head + made + ([] if o.get("no_cap_lines") else [cap_s]) + play + ["- Autosaved new game"] + \
               ([] if o.get("no_cap_lines") or o.get("cap_fin_early") else [cap_f])
        nl = "\r\n" if crlf else "\n"
        data = (nl.join(body) + nl).encode("ascii")
    else:
        # real engine bytes: keep every real line (CRLF) except the old command line and old xrprof lines; insert the
        # made loading lines just before the real intro_start line and the in-play lines after the capture line
        raw = open(real, "rb").read().split(b"\r\n")
        outl = []
        intro_done = False
        made_pre = [t for t in made if t not in ("intro_start game_loaded", "* [x-ray]: Handled Necessary Textures Destruction")]
        for r in raw:
            s = r.decode("ascii", "replace")
            if s.startswith("command line "):
                outl.append(("command line " + cmdline(tag, arm, o)).encode()); continue
            if "[xrprof] capture '" in s:
                continue
            if s == "intro_start game_loaded" and not intro_done:
                outl += [t.encode() for t in made_pre]; intro_done = True
            outl.append(r)
            if s == "* [x-ray]: Handled Necessary Textures Destruction":
                outl.append(cap_s.encode()); outl += [t.encode() for t in play]
        outl.insert(len(outl) - 1, cap_f.encode())
        data = b"\r\n".join(outl)
    if "xray" not in skip:
        with open(os.path.join(d, "xray.log"), "wb") as f:
            f.write(data)

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
# slow-dump cap (Revision 3, T2): row counts the profiler can write (a frame is kept while rows + 14 <= 8400, so at most
# 8400): 599 frames x 14 = 8386 (the bound, VALID), 599 x 14 + 1 = 8387 (just above, VOID), 600 x 14 = 8400 (full, VOID)
SLOWX = lambda n: tuple((3000.0 + 30.0 * k, 21.0) for k in range(n))      # + the save frame = n + 1 slow frames
var("slow_cap", "on", extra=SLOWX(599), rows_per=14, count_rows=8400)
var("slow_8387", "on", extra=SLOWX(599), rows_per=14, last_rows=1, count_rows=8387)
var("slow_8386", "on", extra=SLOWX(598), rows_per=14, count_rows=8386)
# Revision 3 fixtures: S1 verdict form, S2 numbers, S3 capture order, S5 measurement lines, D3 command line
var("verdictx", "on", verdict="VERDICTX: clean"); var("verdict2", "offP", verdict="VERDICT: clean\nVERDICT: clean")
var("inf_frame", "on", frame_text="inf"); var("nan_frame", "offL", frame_text="nan"); var("nan_thr", "on", thr=float("nan"))
var("inf_site", "offP", site_text="inf"); var("full_ms", "on", full_ms="1..2"); var("slow_ms", "offP", bad_slow_ms="1..2")
var("inf_slow_ms", "offL", bad_slow_ms="inf")
var("cap_order", "on", cap_fin_early=True)
var("no_opu", "on", drop_lines=("* alife_opu ",)); var("no_pass", "offP", drop_lines=("* alife_pass ",))
var("opu_renamed", "offL", rename_opu=True)
var("cordon2", "offP", save_name="cordon2"); var("det_mode", "on", more_flags=("-det_mode",))
var("sim_tick_flag", "offL", more_flags=("-sim_tick", "60"))
var("on_noflag", "on", flip_flag=True); var("off_flag", "offP", flip_flag=True); var("fixed_dt", "on", fixed_dt=True)
var("warm600", "offL", warm=600); var("off_warm", "offP", off_warm=True)
for lv in ("none", "two", "after_intro", "p2", "cap", "partial", "held"):
    var("lever_" + lv, "on", lever=lv)
for nm, kind, o in (("ok", "offL", {}), ("no_opu", "offP", dict(drop_lines=("* alife_opu ",))),
                    ("no_pass", "offL", dict(drop_lines=("* alife_pass ",))), ("no_save", "offP", dict(save=False)),
                    ("no_cap", "offL", dict(no_cap_lines=True)), ("lever", "offP", dict(flip_flag=True)),
                    ("bad_row", "offL", dict(bad_frame_row=True))):
    make("p_prime_" + nm, kind, **o)      # D1 probe fixtures: the PRIME load (an OFF load, never scored)
var("reset_late", "offP", reset_late=True); var("legacy_disagree", "offL", wide_slows=10); var("wide_in_play", "offP", wide_in_play=True)
CONF = [make("v_conf_%d" % i, "on", verdict="VERDICT: CONFOUNDED (other heavy process present)") or "v_conf_%d" % i for i in range(9)]
QUIET = [make("v_quiet_%d" % i, "offP", hitch=False) or "v_quiet_%d" % i for i in range(3)]
NOTRAIN = [make("v_notrain_%d" % i, "offP", train=False) or "v_notrain_%d" % i for i in range(3)]
NOPLAY = [make("v_noplay_%d" % i, "on", no_play_lines=True) or "v_noplay_%d" % i for i in range(9)]
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

# ---------------- cases ----------------
CASES = []
def order_lines(on_tags, off_tags, frames=FR):
    on_tags, off_tags = list(on_tags), list(off_tags)
    out = ["wu1_prime|Warm18.exe|cordon1|0|%d|20|prime" % frames]
    for a in ["on", "off", "off", "on"] * 8:
        t = (on_tags if a == "on" else off_tags).pop(0)
        out.append("%s|Warm18.exe|cordon1|0|%d|20|%s" % (t, frames, a))
    return out
OFF_GOOD = [GP[0], GP[1], GL[0], GP[2], GP[3], GL[1], GP[4], GP[5], GL[2], GP[6], GP[7], GL[3], GP[8], GP[9], GL[4], GP[10]]
def sub(lst, old, new):
    l = list(lst); l[l.index(old)] = new; return l
def case(name, want, pats, on_tags=GON, off_tags=OFF_GOOD, lines=None, root=None, pre_out=False, parse=None, targets=(),
         one=None):
    """one = ("probe" | "check", tag): the driver's one-load modes on POOL/<tag> with this case's order file (lines)."""
    cd = os.path.join(ST, "cases", name); os.makedirs(cd)
    if one is not None:
        parse = ["%s=%s" % (one[0], os.path.join(root or POOL, one[1])), "order=" + os.path.join(cd, "wu1_order.txt")]
    if parse is None or one is not None:
        L = lines if lines is not None else order_lines(on_tags, off_tags)
        with open(os.path.join(cd, "wu1_order.txt"), "w") as f:
            f.write("# selftest case %s\n" % name + "\n".join(L) + "\n")
    CASES.append(dict(name=name, want=want, pats=pats, root=root or POOL, order=os.path.join(cd, "wu1_order.txt"),
                      dir=cd, pre_out=pre_out, parse=parse))

# real logs read in place by the parser alone
for d, leg in (("cp1", "P"), ("cp2", "L"), ("cp3", "P"), ("cp4", "L"), ("uc1", "P"), ("lc1", "P"), ("lc2", "P"), ("lc3", "P"), ("cpl1", "P")):
    case("real_parse_" + d, 0, [r"legacy=%s " % leg, r"alife_pass=0 alife_opu=0 warmup_full=0"], parse=REAL + "/%s/xray.log" % d)
# real run folders as loads: wrong exe, fixed step, other capture, missing slow file -> FAIL by name
case("real_folders", 1, [r"LOAD cp1 OFF FAIL \[arm_mismatch\]", r"exe is .*refdet13\.exe", r"-perf_fixed_dt present",
                         r"LOAD cp2 OFF FAIL \[arm_mismatch\]", r"MISSING .*/out/uc1/perf_slow_uc1\.csv",
                         r"LOAD r1a_cor_1 ON FAIL .*\[lever_count\]", r"^WU1 RESULT: FAIL"],
     lines=["cp1|Warm18.exe|cordon1|0|3000|20|off", "cp2|Warm18.exe|cordon1|0|3000|20|off",
            "uc1|Warm18.exe|cordon1|0|3000|20|off", "r1a_cor_1|Warm18.exe|cordon1|0|3000|20|on"], root=REAL)
# made loads spliced into real engine logs (real CRLF bytes, real alife_slow lines, real intro_start)
case("real_spliced", 0, [r"LOAD r_cp2 OFF VALID group=OFF-IN-LOADING", r"LOAD r_cp1 OFF VALID group=OFF-IN-PLAY",
                         r"LOAD r_cp4 OFF VALID group=OFF-IN-LOADING", r"LOAD r_uc1 OFF VALID group=OFF-IN-PLAY",
                         r"legacy L \(max 133 alife_slow", r"^WU1 RESULT: PASS"],
     off_tags=sub(sub(sub(sub(OFF_GOOD, GL[0], "r_cp2"), GP[0], "r_cp1"), GL[1], "r_cp4"), GP[1], "r_uc1"))
# made
case("good", 0, [r"GROUPS .*ON 16, OFF-IN-PLAY 11, OFF-IN-LOADING 5, OFF-OTHER 0", r"H1 .* -> ok", r"H2 .* -> ok",
                 r"H3 .* -> ok", r"SAME AS GOOD ORDER .* -> SAME", r"save=#\d+ 900 ms", r"^WU1 RESULT: PASS$"])
case("good_crlf", 0, [r"LOAD v_crlf_on ON VALID", r"LOAD v_crlf_offl OFF VALID group=OFF-IN-LOADING",
                      r"LOAD v_crlf_offp OFF VALID group=OFF-IN-PLAY", r"^WU1 RESULT: PASS$"],
     on_tags=sub(GON, GON[0], V["crlf_on"]), off_tags=sub(sub(OFF_GOOD, GL[0], V["crlf_offl"]), GP[0], V["crlf_offp"]))
for nm, slot, pat in (("miss_xray", "on", r"MISSING .*/v_miss_xray/xray\.log"), ("miss_frames", "offp", r"MISSING .*/perf_frames_v_miss_frames\.csv"),
                      ("miss_sites", "on", r"MISSING .*/perf_sites_v_miss_sites\.csv"), ("miss_slow", "offl", r"MISSING .*/perf_slow_v_miss_slow\.csv"),
                      ("miss_cond", "on", r"MISSING .*/v_miss_cond/conditions\.txt"), ("empty_frames", "on", r"EMPTY .*/perf_frames_v_empty_frames\.csv"),
                      ("bad_row", "on", r"UNPARSABLE .*perf_frames_v_bad_row\.csv row"), ("bad_pass", "offp", r"UNPARSABLE .*v_bad_pass/xray\.log line")):
    ot = sub(GON, GON[2], V[nm]) if slot == "on" else GON
    ft = OFF_GOOD if slot == "on" else sub(OFF_GOOD, GP[2] if slot == "offp" else GL[2], V[nm])
    case("f_" + nm, 1, [pat, r"^WU1 RESULT: FAIL"], on_tags=ot, off_tags=ft)
case("f_missing_folder", 1, [r"MISSING folder .*/v_absent$", r"^WU1 RESULT: FAIL"], on_tags=sub(GON, GON[5], "v_absent"))
for nm, slot, code in (("not_clean", "on", "not_clean"), ("framecount", "offp", "framecount"), ("seqframe", "on", "stock_loop"),
                       ("simtick", "on", "stock_loop"), ("limiter", "offl", "frame_limiter"), ("threshold", "on", "threshold_header"),
                       ("window_short", "on", "window_short"), ("save_unlocated", "on", "save_unlocated"), ("no_cap_lines", "on", "capture_lines"),
                       ("slow_cap", "on", "slow_cap")):
    ot = sub(GON, GON[3], V[nm]) if slot == "on" else GON
    ft = OFF_GOOD if slot == "on" else sub(OFF_GOOD, GP[3] if slot == "offp" else GL[3], V[nm])
    case("v_" + nm, 0, [r"LOAD %s \S+ VOID \[%s\]" % (V[nm], code), r"^WU1 RESULT: PASS$"], on_tags=ot, off_tags=ft)
for nm, slot, pat in (("on_noflag", "on", r"-alife_warmup absent on an ON load"), ("off_flag", "offp", r"-alife_warmup present on an OFF load"),
                      ("fixed_dt", "on", r"-perf_fixed_dt present"), ("warm600", "offl", r"-perf_capture is not 0,%d,v_warm600" % FR)):
    ot = sub(GON, GON[4], V[nm]) if slot == "on" else GON
    ft = OFF_GOOD if slot == "on" else sub(OFF_GOOD, GP[4] if slot == "offp" else GL[4], V[nm])
    case("a_" + nm, 1, [r"LOAD %s \S+ FAIL .*\[arm_mismatch\]" % V[nm], pat, r"^WU1 RESULT: FAIL"], on_tags=ot, off_tags=ft)
case("a_off_not_stock", 1, [r"LOAD v_off_warm OFF FAIL \[off_not_stock\]", r"^WU1 RESULT: FAIL"], off_tags=sub(OFF_GOOD, GP[5], V["off_warm"]))
for lv, code in (("none", "lever_count"), ("two", "lever_count"), ("after_intro", "lever_after_intro"), ("p2", "lever_precache"),
                 ("cap", "lever_cap"), ("partial", "lever_partial")):
    case("l_" + lv, 1, [r"LOAD v_lever_%s ON FAIL \[%s\] group=" % (lv, code), r"^WU1 RESULT: FAIL \(1 load"], on_tags=sub(GON, GON[6], V["lever_" + lv]))
case("l_held_ok", 0, [r"LOAD v_lever_held ON VALID .*\| load=33\.00 s$", r"precache held 12 frames \(cap 300\)",
                      r"load time: frame0=64 from line at frame 42 precache 10 \+ 12 held frames; first in-play line frame 69 \(\+5 frames\)",
                      r"^WU1 RESULT: PASS$"],
     on_tags=sub(GON, GON[6], V["lever_held"]))
for nm, pat in (("reset_late", r"pass lines end before the narrowing"), ("legacy_disagree", r"pass lines say in-loading, alife_slow lines say P"),
                ("wide_in_play", r"wide pass only after loading ended")):
    ft = sub(OFF_GOOD, GL[1] if nm == "legacy_disagree" else GP[6], V[nm])
    case("c_" + nm, 0, [r"LOAD %s OFF VALID group=OFF-OTHER" % V[nm], pat, r"^WU1 RESULT: PASS$"], off_tags=ft)
GROUP_OFF = [GP[0], GL[0], GL[1], GP[1], GL[2], GL[3], GP[2], GL[4], GL[5], GP[3], GL[6], GL[7], GP[4], GL[8], GL[9], GL[10]]
case("grouping", 0, [r"GROUPS .*OFF-IN-PLAY 5, OFF-IN-LOADING 11", r"^WU1 RESULT: PASS$"], off_tags=GROUP_OFF)
case("nv_few_on", 3, [r"NO VERDICT \[few_on: 7 valid ON loads < 8\]", r"^WU1 RESULT: NO VERDICT \(few_on, loadtime\)$"], on_tags=CONF + GON[:7])
case("nv_few_inplay", 3, [r"NO VERDICT \[few_inplay: 2 valid", r"^WU1 RESULT: NO VERDICT \(few_inplay, loadtime\)$"], off_tags=GP[:2] + GL[:14])
case("nv_no_hitch", 3, [r"NO VERDICT \[no_stock_hitch", r"^WU1 RESULT: NO VERDICT \(no_stock_hitch\)$"], off_tags=QUIET + GL[:13])
case("nv_no_train", 3, [r"NO VERDICT \[no_stock_train", r"^WU1 RESULT: NO VERDICT \(no_stock_train, stock_orders_alike\)$"], off_tags=NOTRAIN + GL[:13])
case("nv_loadtime", 3, [r"NO VERDICT \[loadtime: load time measurable in 7 ON", r"^WU1 RESULT: NO VERDICT \(loadtime\)$"], on_tags=NOPLAY + GON[:7])
case("h_worst", 1, [r"H1 .*\(FAIL\), and .*\(ok\) -> FAIL", r"^WU1 RESULT: FAIL \(H1\)$"], on_tags=WORSE + GON[:7])
case("h_stray_two", 1, [r"H1 .*\(ok\), and .*: 2 <= 1 \(FAIL\) -> FAIL", r"^WU1 RESULT: FAIL \(H1\)$"], on_tags=STRAY[:2] + GON[:14])
case("h_stray_one", 0, [r"H1 .*: 1 <= 1 \(ok\) -> ok", r"^WU1 RESULT: PASS$"], on_tags=STRAY[:1] + GON[:15])
case("nv_alike_worst", 3, [r"NO VERDICT \[stock_orders_alike", r"^WU1 RESULT: NO VERDICT \(stock_orders_alike\)$"],
     off_tags=GP[:11] + LHITCH)
case("nv_alike_train", 3, [r"NO VERDICT \[stock_orders_alike", r"^WU1 RESULT: NO VERDICT \(stock_orders_alike\)$"],
     off_tags=GP[:11] + LTRAIN)
case("h_train", 1, [r"H2 .* = 6\.0 >= 7 -> FAIL", r"^WU1 RESULT: FAIL \(H2\)$"], on_tags=TRAIN + GON[:7])
# Revision 2 edges, each constant on both sides of its bound (median OFF-IN-PLAY n20 in 30 s is 17 in the good pool)
case("h_train_edge_ok", 0, [r"H2 .* = 7\.0 >= 7 -> ok", r"^WU1 RESULT: PASS$"], on_tags=TRAIN10 + GON[:7])
case("nv_alike_edge_ok", 0, [r"GROUPS .*OFF-IN-LOADING 5", r"^WU1 RESULT: PASS$"], off_tags=GP[:11] + LTRAIN10)
case("train_edge_ok", 0, [r"OFF-IN-PLAY +n20 +median 7\.00", r"H2 .* = 7\.0 >= 7 -> ok", r"^WU1 RESULT: PASS$"], off_tags=P7 + GL[:5])
case("nv_train_edge", 3, [r"NO VERDICT \[no_stock_train: median OFF-IN-PLAY frames > 20 ms 6\.0 < 7",
                          r"^WU1 RESULT: NO VERDICT \(no_stock_train, stock_orders_alike\)$"], off_tags=P6 + GL[:5])
case("win_in", 1, [r"H1 .*\(FAIL\), and .*\(ok\) -> FAIL", r"^WU1 RESULT: FAIL \(H1\)$"], on_tags=WIN_IN + GON[:7])
case("win_out", 0, [r"LOAD v_winout_0 ON VALID group=ON worst=5\.\d n20=0 ", r"^WU1 RESULT: PASS$"], on_tags=WIN_OUT + GON[:7])
case("good_15s_reported", 0, [r"LOAD g_offp_01 OFF VALID group=OFF-IN-PLAY worst=320\.0 n20=17 n33=10 .*\| 15s: worst=320\.0 n20=3 n33=3 ",
                              r"OFF-IN-PLAY +n20_15 +median 3\.00", r"^WU1 RESULT: PASS$"])
case("h_price", 1, [r"H3 .* -> FAIL", r"SAME AS GOOD ORDER .* -> DIFFERENT", r"^WU1 RESULT: FAIL \(H3\)$"], on_tags=PRICE + GON[:7])
G = order_lines(GON, OFF_GOOD)
# ---------------- Revision 3 (review fixes) ----------------
# S1: a VERDICT line the bench never writes is a named FAIL (was: not_clean VOID, load dropped, PASS)
case("f_verdictx", 1, [r"LOAD v_verdictx ON FAIL \[unparsable\]", r"VERDICT line\(s\) \['VERDICTX: clean'\] are not one",
                       r"^WU1 RESULT: FAIL"], on_tags=sub(GON, GON[8], V["verdictx"]))
case("f_verdict2", 1, [r"LOAD v_verdict2 OFF FAIL \[unparsable\]", r"^WU1 RESULT: FAIL"], off_tags=sub(OFF_GOOD, GP[8], V["verdict2"]))
# S2: inf / nan in a timing field, a pattern-matching but unconvertible number in the log: named UNPARSABLE, report written
for nm, slot, pat in (("inf_frame", "on", r"UNPARSABLE .*perf_frames_v_inf_frame\.csv row 101 '100,inf'"),
                      ("nan_frame", "offl", r"UNPARSABLE .*perf_frames_v_nan_frame\.csv row 101 '100,nan'"),
                      ("nan_thr", "on", r"UNPARSABLE .*perf_slow_v_nan_thr\.csv header"),
                      ("inf_site", "offp", r"UNPARSABLE .*perf_sites_v_inf_site\.csv row"),
                      ("full_ms", "on", r"UNPARSABLE .*v_full_ms/xray\.log line \d+ .*1\.\.2 ms.* \(not a finite number\)"),
                      ("slow_ms", "offp", r"UNPARSABLE .*v_slow_ms/xray\.log line \d+ .*1\.\.2 ms.* \(not a finite number\)"),
                      ("inf_slow_ms", "offl", r"UNPARSABLE .*v_inf_slow_ms/xray\.log line \d+ .*inf ms")):
    ot = sub(GON, GON[9], V[nm]) if slot == "on" else GON
    ft = OFF_GOOD if slot == "on" else sub(OFF_GOOD, GP[9] if slot == "offp" else GL[2], V[nm])
    case("f_" + nm, 1, [r"LOAD %s \S+ FAIL \[unparsable\]" % V[nm], pat, r"^WU1 RESULT: FAIL"], on_tags=ot, off_tags=ft)
# S3: finished marker before the end of loading, started after it: VOID capture_lines (was VALID)
case("v_cap_order", 0, [r"LOAD v_cap_order ON VOID \[capture_lines\]", r"^WU1 RESULT: PASS$"], on_tags=sub(GON, GON[10], V["cap_order"]))
# S5: no alife_opu / no alife_pass / alife_opu under another prefix: FAIL no_measure_lines (was OFF-OTHER or VALID)
case("f_no_opu", 1, [r"LOAD v_no_opu ON FAIL \[no_measure_lines\]", r"no_measure_lines: \d+ alife_pass and 0 alife_opu",
                     r"^WU1 RESULT: FAIL \(1 load\(s\) failed a registered check: no_measure_lines\)"], on_tags=sub(GON, GON[11], V["no_opu"]))
case("f_no_pass", 1, [r"LOAD v_no_pass OFF FAIL \[no_measure_lines\]", r"no_measure_lines: 0 alife_pass and \d+ alife_opu",
                      r"^WU1 RESULT: FAIL"], off_tags=sub(OFF_GOOD, GP[10], V["no_pass"]))
case("f_opu_renamed", 1, [r"LOAD v_opu_renamed OFF FAIL \[no_measure_lines\]", r"^WU1 RESULT: FAIL"], off_tags=sub(OFF_GOOD, GL[3], V["opu_renamed"]))
# T2: the slow-dump cutoff at exactly 8386 rows (VALID) and just above (8387, VOID); 8400 (full) is v_slow_cap
case("v_slow_8387", 0, [r"LOAD v_slow_8387 ON VOID \[slow_cap\]", r"perf_slow has 8387 rows > 8386", r"^WU1 RESULT: PASS$"],
     on_tags=sub(GON, GON[12], V["slow_8387"]))
case("slow_8386_ok", 0, [r"LOAD v_slow_8386 ON VALID group=ON", r"^WU1 RESULT: PASS$"], on_tags=sub(GON, GON[12], V["slow_8386"]))
# D1: the probe on the PRIME load (exit 0 = go on, 1 = stop the series before any scored load, 2 = refused)
def one_order(tag, arm):
    return ["%s|Warm18.exe|cordon1|0|%d|20|%s" % (tag, FR, arm), "g_on_01|Warm18.exe|cordon1|0|%d|20|on" % FR]
case("probe_ok", 0, [r"^PROBE info p_prime_ok group=OFF-IN-LOADING save=#\d+ 900 ms load=\d", r"^PROBE p_prime_ok ok$"],
     lines=one_order("p_prime_ok", "prime"), one=("probe", "p_prime_ok"))
for nm, pat in (("no_opu", r"no_measure_lines"), ("no_pass", r"no_measure_lines"), ("no_save", r"no_save_frame"),
                ("no_cap", r"capture_lines"), ("lever", r"arm_mismatch: .*-alife_warmup present on an PRIME load"),
                ("bad_row", r"unparsable")):
    case("probe_" + nm, 1, [r"^PROBE STOP p_prime_%s %s" % (nm, pat), r"^PROBE p_prime_%s FAIL$" % nm],
         lines=one_order("p_prime_" + nm, "prime"), one=("probe", "p_prime_" + nm))
case("probe_refused", 2, [r"REFUSED: g_on_01 is not the prime load"], lines=one_order("p_prime_ok", "prime"), one=("probe", "g_on_01"))
# D3: the per-load check after every scored load (same rules as scoring; stop on a FAIL or a stop-VOID, not on not_clean)
def chk(name, want, tag, arm, pats):
    case(name, want, pats + [r"^CHECK %s %s$" % (tag, "ok" if want == 0 else "FAIL")],
         lines=["wu1_prime|Warm18.exe|cordon1|0|%d|20|prime" % FR, "%s|Warm18.exe|cordon1|0|%d|20|%s" % (tag, FR, arm)],
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
chk("check_framecount", 1, V["framecount"], "off", [r"^CHECK STOP v_framecount framecount"])
chk("check_lever_cap", 1, V["lever_cap"], "on", [r"^CHECK STOP v_lever_cap lever_cap"])
chk("check_missing", 1, V["miss_slow"], "off", [r"^CHECK STOP v_miss_slow missing: MISSING"])
chk("check_window_short", 0, V["window_short"], "on", [r"^CHECK note v_window_short window_short"])
case("check_refused", 2, [r"REFUSED: wu1_prime is not a scored load"], lines=one_order("wu1_prime", "prime"), one=("check", "wu1_prime"))
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
       "nv_no_hitch": ["nv_no_hitch"], "nv_no_train": ["nv_no_train"], "nv_loadtime": ["nv_loadtime"], "nv_alike": ["nv_alike_worst", "nv_alike_train"],
       "h1_median": ["h_worst"], "h1_max": ["h_stray_two"],
       "h2": ["h_train"], "h3": ["h_price"], "refuse_out": ["r_out_exists"], "lowercase": ["r_uppercase"], "dup_tag": ["r_dup"],
       # Revision 3 hooks
       "finite": ["f_inf_frame", "f_inf_site", "f_nan_thr"], "verdict_form": ["f_verdictx", "f_verdict2"],
       "log_num": ["f_full_ms", "f_slow_ms"], "held_k": ["l_held_ok"], "capture_order": ["v_cap_order", "check_cap_order"],
       "measure_lines": ["f_no_opu", "f_no_pass", "f_opu_renamed", "probe_no_opu"], "probe_save": ["probe_no_save"]}
src = open(SC).read()
hook = 'def on(name):\n    """Self-test mutation hook: every check below is guarded by on("<name>"). Always True in this file."""\n    return True\n'
if src.count(hook) != 1:
    print("FAIL mutants: the on() hook is not found exactly once in the scorer"); nfail += 1
else:
    used = set(re.findall(r'on\("([a-z0-9_]+)"\)', src)) - {"<name>"}
    missing = sorted(used - set(MUT))
    if missing:
        print("FAIL mutants: hooks without a mutant: %s" % ", ".join(missing)); nfail += 1
    else:
        npass += 1; print("ok   every on() hook in the scorer has a mutant (%d)" % len(used))
    byname = {c["name"]: c for c in CASES}
    for m, targets in sorted(MUT.items()):
        md = os.path.join(ST, "mutants", m); os.makedirs(md)
        ms = os.path.join(md, "wu1_score.py")
        with open(ms, "w") as f:
            f.write(src.replace(hook, 'def on(name):\n    return name != "%s"   # MUTANT\n' % m))
        caught = None
        for cn in targets + [c["name"] for c in CASES if c["name"] not in targets]:
            cd = os.path.join(md, cn); os.makedirs(cd)
            if run(ms, byname[cn], cd):
                caught = cn; break
        if caught:
            npass += 1; print("ok   mutant %-18s caught by %s" % (m, caught))
        else:
            nfail += 1; print("FAIL mutant %s survived every case" % m)
# ---------------- constant mutants (Revision 2): every changed or new constant moved off its registered value ----------------
# Each must be caught by some case (the edge cases above sit on both sides of each bound).
CMUT = {"window_15_headline": ("WINDOW_MS = 30000.0 ", "WINDOW_MS = 15000.0 ", ["good", "win_in", "v_window_short"]),
        "windows_swapped": ("WINDOW_MS = 30000.0 ", "WINDOW_MS = 15000.0 ", ["good"], ("WINDOW2_MS = 15000.0 ", "WINDOW2_MS = 30000.0 ")),
        "window_45": ("WINDOW_MS = 30000.0 ", "WINDOW_MS = 45000.0 ", ["good", "win_out"]),
        "train_min_6": ("STOCK_TRAIN_MIN = 7 ", "STOCK_TRAIN_MIN = 6 ", ["nv_train_edge"]),
        "train_min_8": ("STOCK_TRAIN_MIN = 7 ", "STOCK_TRAIN_MIN = 8 ", ["train_edge_ok"]),
        "n20_drop_6": ("N20_DROP = 7 ", "N20_DROP = 6 ", ["h_train", "nv_alike_train"]),
        "n20_drop_8": ("N20_DROP = 7 ", "N20_DROP = 8 ", ["h_train_edge_ok", "nv_alike_edge_ok", "train_edge_ok"]),
        # Revision 3 (T2): the cutoff moved to what the profiler can write: 8400 (cap) misses 8387 and 8400; 8385 voids 8386
        "slow_cap_rows_up": ("SLOW_CAP_ROWS = 600 * 14 - 14 ", "SLOW_CAP_ROWS = 600 * 14 - 0 ", ["v_slow_8387", "v_slow_cap"]),
        "slow_cap_rows_down": ("SLOW_CAP_ROWS = 600 * 14 - 14 ", "SLOW_CAP_ROWS = 600 * 14 - 15 ", ["slow_8386_ok"])}
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
    ms = os.path.join(md, "wu1_score.py")
    with open(ms, "w") as f:
        f.write(msrc)
    caught = None
    for cn in targets + [c["name"] for c in CASES if c["name"] not in targets]:
        cd = os.path.join(md, cn); os.makedirs(cd)
        if run(ms, byname[cn], cd):
            caught = cn; break
    if caught:
        npass += 1; ncm += 1; print("ok   constant mutant %-18s caught by %s" % (m, caught))
    else:
        nfail += 1; print("FAIL constant mutant %s survived every case" % m)
with open(os.path.join(ST, "counts.txt"), "w") as f:
    f.write("%d %d %d %d %d %d\n" % (npass, nfail, nreal, nmade, len(MUT), ncm))
PYEOF
[ -s "$ST/counts.txt" ] || { echo "WU1 SELFTEST FAIL: case runner did not finish" | tee -a "$LOG"; exit 1; }
read -r pass fail nreal nmade nmut ncm < "$ST/counts.txt"
# the real fixture folders must be untouched: nothing in them newer than the start of this self-test
# Revision 3 (T1): a folder that is missing or that find cannot read counts as touched, never as "unchanged"
newer_count() {   # <dir> <stamp file>: prints the number of paths newer than the stamp; returns 1 if find failed
  local out
  [ -d "$1" ] || { echo "missing $1" >&2; return 1; }
  out=$(find "$1" -newer "$2" -print) || return 1
  printf '%s' "$out" | grep -c . || true
}
# the check itself must fail on a find error (a path that does not exist; nothing is created for it)
if newer_count "$ST/t1_no_such_dir" "$ST/.t0" >/dev/null 2>&1; then fail=$((fail+1)); echo "FAIL untouched check: a find error was not caught" | tee -a "$LOG"
else pass=$((pass+1)); echo "ok   untouched check fails on a find error" | tee -a "$LOG"; fi
touched=0
for t in cp1 cp2 cp3 cp4 uc1 lc1 lc2 lc3 cpl1 r1a_cor_1; do
  if n=$(newer_count "$REAL/$t" "$ST/.t0"); then touched=$((touched + n))
  else touched=$((touched + 1)); echo "FAIL fixture folder $REAL/$t could not be checked" | tee -a "$LOG"; fi
done
if [ "$touched" != 0 ]; then fail=$((fail+1)); echo "FAIL real fixture folders changed during the self-test ($touched paths)" | tee -a "$LOG"
else pass=$((pass+1)); echo "ok   real fixture folders unchanged" | tee -a "$LOG"; fi
tot=$((pass+fail))
if [ "$fail" = 0 ]; then echo "WU1 SELFTEST PASS $pass/$tot ($nreal real cases, $nmade made cases, $nmut hook mutants caught, $ncm constant mutants caught, 1 hook-coverage check, 1 find-error check, 1 untouched check) in $ST" | tee -a "$LOG"; exit 0
else echo "WU1 SELFTEST FAIL $fail of $tot in $ST" | tee -a "$LOG"; exit 1; fi
