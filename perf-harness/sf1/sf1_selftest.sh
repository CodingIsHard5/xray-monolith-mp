#!/usr/bin/env bash
# sf1 self-test (PREREG-sf1-slow-frame-classes-DRAFT.md section 8): scorer, probe gate, per-load check, the driver's
# logic offline (incl. the options-file restore on a scratch layout and the claim by exit code), checker mutants.
#   nice -n 19 bash sf1_selftest.sh [<parent folder>]     (default parent: sf1/selftest-runs) -> <parent>/<stamp>/
# Real fixtures: earlier run folders under <project>/out, referenced IN PLACE, read-only (never copied, never written).
# Made fixtures: real frame / site files of earlier runs (symlinked when unchanged, re-written when a case changes them),
# with slow-frame rows, conditions.txt and a small xray.log made to a plan. Nothing is ever deleted. The stamp folder must
# be new. Output also saved as <stamp>/selftest_output.txt. The real run/ is never written: the restore is tested on a
# copy of run/appdata/axr_options_play.ltx inside the stamp folder (the real file is only read).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
P="$(cd "$HERE/../../.." && pwd)"           # project root (wt-render-split/perf-harness/sf1 -> project)
REAL="$P/out"
SC="$HERE/sf1_classify.py"; DRV="$HERE/sf1_run.sh"
[ -s "$SC" ] || { echo "SF1 SELFTEST FAIL: scorer $SC missing"; exit 2; }
[ -s "$DRV" ] || { echo "SF1 SELFTEST FAIL: driver $DRV missing"; exit 2; }
[ -d "$REAL" ] || { echo "SF1 SELFTEST FAIL: real run root $REAL missing"; exit 2; }
PARENT="${1:-$HERE/selftest-runs}"; mkdir -p "$PARENT" && PARENT="$(cd "$PARENT" && pwd)" || { echo "SF1 SELFTEST FAIL: parent folder"; exit 2; }
ST="$PARENT/$(date +%Y%m%d-%H%M%S)-$$"
[ -e "$ST" ] && { echo "SF1 SELFTEST FAIL: $ST exists"; exit 2; }
mkdir -p "$ST/cases"
touch "$ST/.t0"; sleep 1
LOG="$ST/selftest_output.txt"
say() { echo "$*" | tee -a "$LOG"; }

python3 -I - "$ST" "$REAL" <<'PYEOF'
import os, sys, math
ST, REAL = sys.argv[1], sys.argv[2]
CASES = []

def rd(p):
    return [l.rstrip("\r") for l in open(p, "rb").read().decode("ascii", "replace").split("\n") if l.rstrip("\r")]

def pct(s, p):
    k = (len(s) - 1) * p; i = int(math.floor(k)); j = min(i + 1, len(s) - 1)
    return s[i] + (s[j] - s[i]) * (k - i)

def real_frames(tag):
    return [float(l.split(",")[1]) for l in rd("%s/%s/perf_frames_%s.csv" % (REAL, tag, tag))[1:]]

def real_sites(tag):
    L = rd("%s/%s/perf_sites_%s.csv" % (REAL, tag, tag))
    N = int(L[0].split(",")[1]); S = {}
    for l in L[3:]:
        a = l.split(",")
        S.setdefault(a[0], (a[1], a[2], float(a[5]) / N, float(a[8]) / N))
    return L, N, S

REP = {"alife": ("ALife/scheduled/update", "other"), "script": ("CScriptBinder::shedule_Update", "main"),
       "render": ("r_dsgraph_render_static", "main"), "gpu_wait": ("bench: wait for GPU (no present)", "main"),
       "online_sim": ("stalker/schedule_update/think/brain", "other"), "io_stream": ("Sound: Load ogg", "main"),
       "sound": ("CSoundRender_Core::update", "other"), "other_known": ("CDialogHolder::OnFrame", "main"),
       "gc": ("seqLuaGC", "other")}
WARM = 14000

def make_run(root, tag, src, T, plan, save, scale=1.0, crlf=False, verdict="VERDICT: clean", skip=(), empty=(),
             sites_add=(), sites_drop=(), seqframe_delta=0, header_T=None, drop_last=False, malformed=False,
             norows_from=None, train=(), freeze=False, fatal=False, fixed_dt=False, heavy_start="", heavy_end="",
             rows_off=False, no_rows=False, exe="Coarse14.exe"):
    d = os.path.join(root, tag); os.makedirs(d)
    fr0 = real_frames(src)
    fr = [round(v * scale, 4) for v in fr0]
    for i in train:
        fr[i] = 30.0
    N = len(fr); s = sorted(fr); p50 = pct(s, 0.5); p99 = pct(s, 0.99)
    SL, SN, S = real_sites(src)
    def site(name, thread):
        if name in S:
            f, l, sm, so = S[name]; return f, l, (sm if thread == "main" else so)
        return "synthetic", "0", 0.0
    bg = sorted([n for n in S if n != "mt_FreezeThread"], key=lambda n: -(S[n][2] + S[n][3]))[:8]
    def background():
        out = []
        for n in bg:
            th = "main" if S[n][2] >= S[n][3] else "other"
            f, l, b = site(n, th); out.append([th, b, 1, n, f, l])
        return out
    rows = []
    k = 0
    for i, v in enumerate(fr):
        if no_rows or not v > T or (norows_from is not None and i >= norows_from):
            continue
        R = background() if (v > p99 or i in train) else background()[:1]   # between T and p99 one row is enough (size)
        if i in train:
            f, l, b = site("ALife/scheduled/update", "other"); R.append(["other", 22.0, 20, "ALife/scheduled/update", f, l])
        elif v > p99:
            E = v - p50; spec = plan(k); k += 1
            if spec[0] == "unknown":
                for c, fr_ in (("render", 0.35), ("script", 0.35), ("gpu_wait", 0.30)):
                    n, th = REP[c]; f, l, b = site(n, th); R.append([th, b + fr_ * E, 1, n, f, l])
            elif spec[0] != "main_unzoned_or_wait":
                n, th = REP[spec[0]]; f, l, b = site(n, th); R.append([th, b + spec[1] * E, 1, n, f, l])
            elif freeze:
                R.append(["other", 500.0, 1, "mt_FreezeThread", "synthetic", "310"])
        merged = {}      # the profiler keeps the 14 largest self times; one row per (site, thread)
        for r in R:
            key = (r[3], r[0])
            if key in merged and merged[key][1] >= r[1]:
                continue
            merged[key] = r
        R = sorted(merged.values(), key=lambda r: -r[1])[:14]
        vv = v + 0.5 if (rows_off and not rows) else v      # rows_off: the first dumped frame's frame_ms disagrees
        rows += ["%d,%.4f,%s,%.4f,%d,%s,%s,%s" % (i, vv, r[0], r[1], r[2], r[3], r[4], r[5]) for r in R]
    if malformed:
        rows.append("%d,%.4f,main,1.0,1,broken_row_7_fields,x" % (0, fr[0]))
    nl = "\r\n" if crlf else "\n"
    def w(kind, name, lines, link=None):
        p = os.path.join(d, name)
        if kind in skip:
            return
        if link is not None and kind not in empty:
            os.symlink(link, p); return
        with open(p, "w", newline="") as f:
            if kind not in empty:
                f.write(nl.join(lines) + nl)
    fr_same = scale == 1.0 and not crlf and not train and not drop_last
    w("frames", "perf_frames_%s.csv" % tag, ["frame,ms"] + ["%d,%.4f" % (i, v) for i, v in enumerate(fr[:-1] if drop_last else fr)],
      link=("%s/%s/perf_frames_%s.csv" % (REAL, src, src)) if fr_same else None)
    SL2 = list(SL)
    if seqframe_delta:
        for j, l in enumerate(SL2):
            a = l.split(",")
            if a[0] == "Process seqFrame":
                a[3] = str(int(a[3]) + seqframe_delta); SL2[j] = ",".join(a)
    SL2 = [l for l in SL2 if l.split(",")[0] not in sites_drop] + list(sites_add)
    w("sites", "perf_sites_%s.csv" % tag, SL2,
      link=("%s/%s/perf_sites_%s.csv" % (REAL, src, src)) if (SL2 == SL and not crlf) else None)
    w("slow", "perf_slow_%s.csv" % tag, ["#threshold_ms,%.3f" % (header_T if header_T is not None else T),
                                         "frame,frame_ms,thread,self_ms,calls,name,file,line"] + rows)
    w("cond", "conditions.txt", ["start 2026-11-27 10:00:00 kind=EXCLUSIVE timing loadavg=0.30 0.40 0.50 heavy_others=[%s]" % heavy_start,
                                 "end   2026-11-27 10:02:00 loadavg=1.10 0.60 0.55 heavy_others=[%s]" % heavy_end, verdict])
    cmd = ("command line z:\\home\\caden\\projects\\stalkergameengine\\run\\bin\\%s -nointro -dbg -start server(%s/single/alife/load) "
           "client(localhost) -perf_capture %d,%d,%s%s -perf_quit -perf_noinput -perf_nopresent -perf_slow %.1f"
           % (exe.lower(), save, WARM, N, tag, " -perf_fixed_dt" if fixed_dt else "", T))
    w("xray", "xray.log", ["* selftest fixture made from real run %s" % src, cmd, "[xrprof] capture '%s' started" % tag]
      + (["FATAL ERROR", "[error]Expression    : fixture"] if fatal else []) + ["[xrprof] capture '%s' finished" % tag])

COR = ["r1_cor_k3", "r1a_cor_k0_3", "r1_cor_k2"]      # Coarse14, cordon1, no after-load train in the capture
ROS = ["s2_off_1", "s4c_noff_1", "s4a_noff_1"]        # stock loop (sim_tick 0), bench1, real time, 12000 frames
TC, TR = 6.0, 8.0
def good_cor(k): return ("alife", 0.8) if k % 10 < 7 else (("render", 0.8) if k % 10 < 9 else ("main_unzoned_or_wait",))
def good_ros(k):
    m = k % 20
    return ("render", 0.8) if m < 9 else (("gpu_wait", 0.8) if m < 15 else ("main_unzoned_or_wait",))
def line(tag, save, n, T, arm, exe="Coarse14.exe", warm=WARM):
    return "%s|%s|%s|%d|%d|%.1f|%s" % (tag, exe, save, warm, n, T, arm)
def add(name, mode, arg, order, out, want, pats):
    CASES.append("\t".join([name, mode, arg, order, out, str(want)] + pats))

STOP_DONE = "class=done reason=list finished\n"
def case(name, expect_exit, pats, cor=None, ros=None, order_extra=None, pre_out=False, real=None, probe=None, root=None,
         probe_ros=None, stop=STOP_DONE):
    """cor/ros: list of 3 dicts of make_run overrides (None = scene absent). probe / probe_ros: dict of overrides for the
    made Cordon / Rostok probe load (default: good). real: list of order lines (root = REAL unless root= is given).
    stop: the bytes of the stop record next to the order file (None = no stop record)."""
    cd = os.path.join(ST, "cases", name); os.makedirs(cd, exist_ok=(root is not None))
    lines = []
    if real is not None:
        rt = root or REAL; lines = list(real)
    else:
        rt = os.path.join(cd, "runs"); os.makedirs(rt)
        for ptag, psrc, pT, pplan, psave, pre_, pov in (("sf1_probe_cor", COR[0], TC, good_cor, "cordon1", cor, probe),
                                                         ("sf1_probe_ros", ROS[0], TR, good_ros, "bench1", ros, probe_ros)):
            if pre_ is None:
                continue
            po = dict(pov or {})
            if not po.pop("absent", False):
                make_run(rt, ptag, psrc, pT, po.pop("plan", pplan), psave, **po)
            lines.append(line(ptag, psave, len(real_frames(psrc)), pT, "probe"))
        for scene, srcs, T, plan0, save, pre in (("cor", COR, TC, good_cor, "cordon1", cor), ("ros", ROS, TR, good_ros, "bench1", ros)):
            if pre is None:
                continue
            for j in range(3):
                o = dict(pre[j]); tag = o.pop("tag", "sf1_%s_%d" % (scene, j + 1))
                if o.pop("absent", False):
                    lines.append(line(tag, save, len(real_frames(srcs[j])), T, "scored")); continue
                plan = o.pop("plan", plan0); rr = o.pop("rr", None)
                make_run(rt, tag, srcs[j], T, plan, save, **o)
                if rr is not None:
                    make_run(rt, tag + "_r1", srcs[j], T, plan, save, verdict=rr)
                lines.append(line(tag, save, len(real_frames(srcs[j])), T, "scored"))
    if order_extra:
        lines = order_extra(lines)
    with open(os.path.join(cd, "sf1_order.txt"), "w") as f:
        f.write("# selftest case %s\n" % name + "\n".join(lines) + "\n")
    if stop is not None:
        with open(os.path.join(cd, "stop.txt"), "w", newline="") as f:
            f.write(stop)
    if pre_out:
        with open(os.path.join(cd, "score.txt"), "w") as f:
            f.write("an earlier score that must not be overwritten\n")
    add(name, "score", rt, os.path.join(cd, "sf1_order.txt"), os.path.join(cd, "score.txt"), expect_exit, pats)
    return rt

def gate(name, mode, want, pats, real_line=None, made=None, folder_tag=None):
    """probe= / check= cases. real_line: the order line of a REAL folder (read in place). made: (src, T, save, overrides,
    arm) for a made folder. The order also lists 3 dummy scored lines (the order shape needs a scene)."""
    cd = os.path.join(ST, "cases", name); os.makedirs(cd)
    if real_line is not None:
        tag = real_line.split("|")[0]; folder = os.path.join(REAL, tag); lines = [real_line]
        arm = real_line.split("|")[-1]
    else:
        src, T, save, o, arm = made; rt = os.path.join(cd, "runs"); os.makedirs(rt)
        o = dict(o); tag = o.pop("tag", ("sf1_probe_cor" if save == "cordon1" else "sf1_probe_ros") if arm == "probe"
                                  else ("sf1_cor_1" if save == "cordon1" else "sf1_ros_1"))
        made_tag = folder_tag or tag
        make_run(rt, made_tag, src, T, o.pop("plan", good_cor), save, **o)
        folder = os.path.join(rt, made_tag)
        lines = [line(tag, save, len(real_frames(src)), T, arm)]
    sv = lines[0].split("|")[2]; sT = TR if sv == "bench1" else TC
    if arm == "probe":
        lines += [line("dummy_%d" % i, sv, 3000, sT, "scored") for i in (1, 2, 3)]
    else:
        lines = [line("dummy_probe", sv, 3000, sT, "probe")] + lines + [line("dummy_%d" % i, sv, 3000, sT, "scored") for i in (1, 2)]
    with open(os.path.join(cd, "sf1_order.txt"), "w") as f:
        f.write("# selftest gate case %s\n" % name + "\n".join(lines) + "\n")
    add(name, mode, folder, os.path.join(cd, "sf1_order.txt"), "-", want, pats)

G3 = [{}, {}, {}]
RL = lambda tag, exe, save, warm, n, T, arm="scored": "%s|%s|%s|%d|%d|%s|%s" % (tag, exe, save, warm, n, T, arm)
# ======================= PROBE GATE (probe=), real folders read in place =======================
# every real folder was run with T = 20 and has p99 10-12 ms, so none can pass coverage_p99 at its own T: a real folder
# that fails ONLY coverage_p99 shows every other gate check passing on real bytes
gate("gate_real_s2_off_1_T20", "probe", 1, [r"^PROBE STOP coverage_p99: threshold 20\.000 ms above window p99 11\.8",
     r"^PROBE STOP slow_rows: perf_slow has no rows", r"^PROBE s2_off_1 FAIL \[slow_rows\] \[coverage_p99\]$"], real_line=RL("s2_off_1", "SimTick14.exe", "bench1", 3000, 12000, 20, "probe"))
gate("gate_real_s4c_noff_1_T20", "probe", 1, [r"^PROBE s4c_noff_1 FAIL \[slow_rows\] \[coverage_p99\]$"],
     real_line=RL("s4c_noff_1", "SimTick17.exe", "bench1", 3000, 12000, 20, "probe"))
gate("gate_real_fixed_step", "probe", 1, [r"^PROBE STOP cmdline: -perf_fixed_dt present", r"^PROBE r1a_cor_k0_3 FAIL \[cmdline\] \[coverage_p99\]$"],
     real_line=RL("r1a_cor_k0_3", "Coarse14.exe", "cordon1", 600, 3000, 20, "probe"))
gate("gate_real_warm0_seqframe", "probe", 1, [r"^PROBE STOP stock_loop: 'Process seqFrame' main calls 19987 != frames 20000",
     r"^PROBE wu3_09_on FAIL \[stock_loop\] \[coverage_p99\]$"], real_line=RL("wu3_09_on", "Warm18.exe", "cordon1", 0, 20000, 20, "probe"))
gate("gate_real_train", "probe", 1, [r"^PROBE STOP train: [0-9]+ A-Life passes", r"^PROBE wu3_02_off FAIL .*\[train\]$"],
     real_line=RL("wu3_02_off", "Warm18.exe", "cordon1", 0, 20000, 20, "probe"))
gate("gate_real_capped", "probe", 1, [r"^PROBE STOP frame_limiter: site 'Eco Render' has", r"^PROBE STOP frame_range: median frame 33\.[0-9]+ ms outside the plausible real-time range \[2\.0, 10\.0\] ms of bench1",
     r"^PROBE s2_c30 FAIL .*\[frame_limiter\].*\[frame_range\]"], real_line=RL("s2_c30", "SimTick14.exe", "bench1", 450, 1800, 20, "probe"))
gate("gate_real_simtick_on", "probe", 1, [r"^PROBE STOP stock_loop: site 'Sim tick' has", r"^PROBE s4c_n30_1 FAIL .*\[stock_loop\]"],
     real_line=RL("s4c_n30_1", "SimTick17.exe", "bench1", 3000, 12000, 20, "probe"))
gate("gate_real_missing_slow", "probe", 1, [r"^PROBE STOP missing: MISSING .*/out/hk2/perf_slow_hk2\.csv", r"^PROBE hk2 FAIL \[missing\]$"],
     real_line=RL("hk2", "RefDet13.exe", "bench1", 600, 3000, 20, "probe"))
gate("gate_real_threshold_and_cmdline", "probe", 1, [r"^PROBE STOP threshold_header: slow file threshold 20\.000, registered 9\.000",
     r"^PROBE STOP cmdline: -perf_slow '20', registered 9\.0"], real_line=RL("r1a_ros_k0_2", "Coarse14.exe", "bench1", 600, 3000, 9, "probe"))
gate("gate_real_framecount", "probe", 1, [r"^PROBE STOP framecount: frames file 3000 rows, sites header 3000, registered 12000",
     r"^PROBE STOP cmdline: -perf_capture '600,3000,r1a_ros_k0_1', registered '600,12000,r1a_ros_k0_1'"],
     real_line=RL("r1a_ros_k0_1", "Coarse14.exe", "bench1", 600, 12000, 20, "probe"))
gate("gate_real_wrong_exe", "probe", 1, [r"^PROBE STOP cmdline: exe is .*simtick14\.exe', registered Coarse14\.exe"],
     real_line=RL("s2_off_1", "Coarse14.exe", "bench1", 3000, 12000, 20, "probe"))
# ======================= PROBE GATE, made from real frame / site files =======================
PM = lambda o=None, src=None, T=TC, save="cordon1": (src or COR[0], T, save, o or {}, "probe")
gate("gate_made_pass", "probe", 0, [r"^PROBE sf1_probe_cor ok \(median frame 3\.[0-9]+ ms, window p99 [0-9.]+ ms, window 3000 frames, slow threshold 6\.0, cordon1\)$"], made=PM())
gate("gate_made_pass_crlf", "probe", 0, [r"^PROBE sf1_probe_cor ok"], made=PM({"crlf": True}))
gate("gate_made_pass_rostok", "probe", 0, [r"^PROBE sf1_probe_ros ok"], made=PM({"plan": good_ros}, src=ROS[0], T=TR, save="bench1"))
gate("gate_made_pass_not_clean", "probe", 0, [r"^PROBE sf1_probe_cor ok"], made=PM({"verdict": "VERDICT: CONFOUNDED (other heavy process present)"}))
gate("gate_made_fatal", "probe", 1, [r"^PROBE STOP fatal: xray.log line 4 contains FATAL: 'FATAL ERROR'", r"^PROBE sf1_probe_cor FAIL \[fatal\]$"], made=PM({"fatal": True}))
gate("gate_made_fixed_dt", "probe", 1, [r"^PROBE sf1_probe_cor FAIL \[cmdline\]$"], made=PM({"fixed_dt": True}))
gate("gate_made_zone_gpu_missing", "probe", 1, [r"^PROBE STOP zones: not in perf_sites with calls: bench: wait for GPU \(no present\), class gpu_wait$",
     r"^PROBE sf1_probe_cor FAIL \[zones\]$"], made=PM({"sites_drop": ("bench: wait for GPU (no present)",)}))
gate("gate_made_zone_seqframe_missing", "probe", 1, [r"^PROBE STOP zones: not in perf_sites with calls: Process seqFrame$",
     r"^PROBE STOP stock_loop: site 'Process seqFrame' missing"], made=PM({"sites_drop": ("Process seqFrame",)}))
gate("gate_made_no_slow_rows", "probe", 1, [r"^PROBE STOP slow_rows: perf_slow has no rows", r"^PROBE sf1_probe_cor FAIL \[slow_rows\] \[window_short\]$"],
     made=PM({"no_rows": True}))
gate("gate_made_row_ms_differs", "probe", 1, [r"^PROBE STOP slow_rows: frame [0-9]+ row frame_ms", r"^PROBE sf1_probe_cor FAIL \[slow_rows\]$"],
     made=PM({"rows_off": True}))
gate("gate_made_range_low", "probe", 1, [r"^PROBE STOP frame_range: median frame 1\.[0-4][0-9]* ms outside", r"\[frame_range\]"], made=PM({"scale": 0.35}))
gate("gate_made_range_high", "probe", 1, [r"^PROBE STOP frame_range: median frame (8\.[1-9]|9\.)[0-9]* ms outside", r"^PROBE sf1_probe_cor FAIL \[frame_range\]$"],
     made=PM({"scale": 2.4, "plan": lambda k: ("render", 0.8)}))
RM = lambda o=None: PM(dict({"plan": lambda k: ("render", 0.8)}, **(o or {})), src=ROS[0], T=TR, save="bench1")
gate("gate_made_ros_range_high", "probe", 1, [r"^PROBE STOP frame_range: median frame 1[01]\.[0-9]+ ms outside the plausible real-time range \[2\.0, 10\.0\] ms of bench1",
     r"^PROBE sf1_probe_ros FAIL \[frame_range\]$"], made=RM({"scale": 2.0}))
gate("gate_made_ros_range_low", "probe", 1, [r"^PROBE STOP frame_range: median frame 1\.[6-9][0-9]* ms outside the plausible real-time range \[2\.0, 10\.0\] ms of bench1"],
     made=RM({"scale": 0.33}))
gate("gate_made_ros_between_ranges", "probe", 0, [r"^PROBE sf1_probe_ros ok \(median frame 9\.[0-9]+ ms, .* slow threshold 8\.0, bench1\)$"], made=RM({"scale": 1.6}))
gate("gate_made_ros_train", "probe", 1, [r"^PROBE sf1_probe_ros FAIL \[train\]$"], made=RM({"train": (4000, 4150, 4300)}))
gate("gate_made_ros_coverage", "probe", 1, [r"^PROBE sf1_probe_ros FAIL \[coverage_p99\]$"], made=RM({"scale": 0.6}))
gate("gate_made_ros_fixed_dt", "probe", 1, [r"^PROBE sf1_probe_ros FAIL \[cmdline\]$"], made=RM({"fixed_dt": True}))
gate("gate_made_ros_zone_alife_missing", "probe", 1, [r"^PROBE STOP zones: not in perf_sites with calls: class alife$"],
     made=RM({"sites_drop": tuple(l.split(",")[0] for l in real_sites(ROS[0])[0][3:] if l.startswith("ALife/"))}))
gate("gate_made_coverage", "probe", 1, [r"^PROBE sf1_probe_cor FAIL \[coverage_p99\]$"], made=PM({"scale": 0.55}))
gate("gate_made_window_short", "probe", 1, [r"^PROBE sf1_probe_cor FAIL \[window_short\]$"], made=PM({"norows_from": 1000}))
gate("gate_made_train", "probe", 1, [r"^PROBE sf1_probe_cor FAIL \[train\]$"], made=PM({"train": (500, 560, 620)}))
gate("gate_made_threshold_header", "probe", 1, [r"^PROBE STOP threshold_header: slow file threshold 7\.000, registered 6\.000", r"^PROBE sf1_probe_cor FAIL \[threshold_header\] \[slow_rows\]$"], made=PM({"header_T": 7.0}))
gate("gate_made_framecount", "probe", 1, [r"^PROBE STOP framecount: frames file 2999 rows, sites header 3000, registered 3000",
     r"^PROBE sf1_probe_cor FAIL \[framecount\]"], made=PM({"drop_last": True}))
gate("gate_made_limiter", "probe", 1, [r"^PROBE sf1_probe_cor FAIL \[frame_limiter\]$"],
     made=PM({"sites_add": ("Eco Render,synthetic,758,3000,9000.0,9000.0,0,0.0000,0.0000",)}))
gate("gate_made_simtick", "probe", 1, [r"^PROBE sf1_probe_cor FAIL \[stock_loop\]$"],
     made=PM({"sites_add": ("Sim tick,synthetic,1014,100,0.1000,0.1000,0,0.0000,0.0000",)}))
gate("gate_made_no_xray_log", "probe", 1, [r"^PROBE STOP missing: MISSING .*/sf1_probe_cor/xray\.log", r"^PROBE sf1_probe_cor FAIL \[missing\]$"], made=PM({"skip": ("xray",)}))
gate("gate_made_no_conditions", "probe", 1, [r"^PROBE STOP missing: MISSING .*/sf1_probe_cor/conditions\.txt"], made=PM({"skip": ("cond",)}))
gate("gate_made_empty_sites", "probe", 1, [r"^PROBE STOP empty: EMPTY .*/perf_sites_sf1_probe_cor\.csv"], made=PM({"empty": ("sites",)}))
gate("gate_refused_scored_folder", "probe", 2, [r"^REFUSED: sf1_cor_1 is not the probe load"], made=(COR[0], TC, "cordon1", {}, "scored"))
gate("gate_refused_probe_rerun", "probe", 2, [r"^REFUSED: sf1_probe_cor_r1 is not the probe load"], made=PM(), folder_tag="sf1_probe_cor_r1")
# ======================= per-load check (check=) =======================
gate("check_real_s2_off_1_ok", "check", 0, [r"^CHECK s2_off_1 ok$"], real_line=RL("s2_off_1", "SimTick14.exe", "bench1", 3000, 12000, 20))
gate("check_real_fixed_step", "check", 1, [r"^CHECK STOP cmdline: -perf_fixed_dt present", r"^CHECK r1a_cor_k0_1 FAIL \[cmdline\]$"],
     real_line=RL("r1a_cor_k0_1", "Coarse14.exe", "cordon1", 600, 3000, 20))
gate("check_made_ok", "check", 0, [r"^CHECK sf1_cor_1 ok$"], made=(COR[1], TC, "cordon1", {}, "scored"))
gate("check_made_r1_ok", "check", 0, [r"^CHECK sf1_cor_1_r1 ok$"], made=(COR[1], TC, "cordon1", {}, "scored"), folder_tag="sf1_cor_1_r1")
gate("check_made_fatal", "check", 1, [r"^CHECK sf1_cor_1 FAIL \[fatal\]$"], made=(COR[1], TC, "cordon1", {"fatal": True}, "scored"))
gate("check_refused_probe", "check", 2, [r"^REFUSED: sf1_probe_cor is not a scored slot or its _r1"], made=PM())

# ======================= scorer: real run folders, read in place =======================
case("real_k0_coarse14", 0, [r"^PROBE cord2 STOP \[cmdline\] \[coverage_p99\] \[train\]$",
                             r"RUN r1a_cor_k0_1 used=r1a_cor_k0_1 VOID \[cmdline\] \[coverage_p99\] \[train\]$",
                             r"RUN r1a_cor_k0_2 used=r1a_cor_k0_2 VOID \[cmdline\] \[coverage_p99\]$",
                             r"SCENE cordon NO VERDICT \[probe\]", r"^SF1 RESULT: cordon=NO VERDICT \(probe\)$"],
     real=[RL("cord2", "RefDet12.exe", "cordon1", 600, 3000, 20, "probe")] +
          [RL("r1a_cor_k0_%d" % i, "Coarse14.exe", "cordon1", 600, 3000, 20) for i in (1, 2, 3)])
case("real_trains_and_not_clean", 0, [r"RUN cord1 used=cord1 VOID \[not_clean\] \[cmdline\] \[coverage_p99\] \[train\]$",
                                      r"RUN cp1 used=cp1 VOID \[not_clean\] \[cmdline\] \[coverage_p99\] \[train\]$",
                                      r"RUN s4c_poff_1 used=s4c_poff_1 VOID \[not_clean\] \[coverage_p99\]$",
                                      r"RUN s2_off_1 used=s2_off_1 VOID \[coverage_p99\]$",
                                      r"conditions cord1: start heavy_others=\[[^]]*\]; end heavy_others=\[",
                                      r"SF1 RESULT: cordon=NO VERDICT \(probe\); rostok=NO VERDICT \(probe\)"],
     real=[RL("r1a_cor_k0_3", "Coarse14.exe", "cordon1", 600, 3000, 20, "probe"), RL("s2_off_2", "SimTick14.exe", "bench1", 3000, 12000, 20, "probe"),
           RL("cord1", "RefDet12.exe", "cordon1", 600, 3000, 20), RL("cord2", "RefDet12.exe", "cordon1", 600, 3000, 20),
           RL("cp1", "RefDet13.exe", "cordon1", 600, 3000, 20), RL("s2_off_1", "SimTick14.exe", "bench1", 3000, 12000, 20),
           RL("s4c_noff_1", "SimTick17.exe", "bench1", 3000, 12000, 20), RL("s4c_poff_1", "SimTick16.exe", "bench1", 3000, 12000, 20)])
case("real_capped", 0, [r"^PROBE hk3 STOP \[missing\]$", r"RUN s2_c30 used=s2_c30 VOID .*\[frame_limiter\]",
                        r"RUN s2_c60 used=s2_c60 VOID .*\[frame_limiter\]", r"frame_limiter: site 'Eco Render' has"],
     real=[RL("hk3", "RefDet13.exe", "bench1", 600, 3000, 20, "probe"), RL("s2_c30", "SimTick14.exe", "bench1", 450, 1800, 20),
           RL("s2_c60", "SimTick14.exe", "bench1", 900, 3600, 20), RL("s2_c50", "SimTick14.exe", "bench1", 750, 3000, 20)])
case("real_simtick_on", 0, [r"RUN s4c_n30_1 used=s4c_n30_1 VOID .*\[stock_loop\]", r"stock_loop: site 'Sim tick' has",
                            r"RUN s4c_p30_1 used=s4c_p30_1 VOID .*\[stock_loop\]"],
     real=[RL("base1", "RefDet12.exe", "bench1", 600, 3000, 20, "probe"), RL("s4c_n30_1", "SimTick17.exe", "bench1", 3000, 12000, 20),
           RL("s4c_n60_1", "SimTick17.exe", "bench1", 3000, 12000, 20), RL("s4c_p30_1", "SimTick16.exe", "bench1", 3000, 12000, 20)])
case("real_registration_mismatch", 0, [r"RUN r1a_ros_k0_1 used=r1a_ros_k0_1 VOID \[framecount\]",
                                       r"RUN r1a_ros_k0_2 used=r1a_ros_k0_2 VOID \[threshold_header\]",
                                       r"threshold_header: slow file threshold 20.000, registered 9.000"],
     real=[RL("hk2", "RefDet13.exe", "bench1", 600, 3000, 20, "probe"), RL("r1a_ros_k0_1", "Coarse14.exe", "bench1", 600, 12000, 20),
           RL("r1a_ros_k0_2", "Coarse14.exe", "bench1", 600, 3000, 9), RL("r1a_ros_k0_3", "Coarse14.exe", "bench1", 600, 3000, 20)])
case("real_missing_slow_file", 0, [r"RUN hk2 used=hk2 VOID \[missing\]$", r"MISSING .*/out/hk2/perf_slow_hk2\.csv",
                                   r"RUN hk3 used=hk3 VOID \[missing\]$", r"RUN base1 used=base1 VOID \[cmdline\] \[coverage_p99\]$"],
     real=[RL("r1a_ros_k0_3", "Coarse14.exe", "bench1", 600, 3000, 20, "probe"), RL("hk2", "RefDet13.exe", "bench1", 600, 3000, 20),
           RL("hk3", "RefDet13.exe", "bench1", 600, 3000, 20), RL("base1", "RefDet12.exe", "bench1", 600, 3000, 20)])
# the re-run substitution on real bytes: s4c_poff_1 (CONFOUNDED) and its real re-run s4c_poff_1_rr shown under the sf1 name
# s4c_poff_1_r1 (folder symlinks to the real folders; the _r1 folder holds symlinks to the real files under the new name
# and an xray.log with the real command line re-tagged, the only made bytes)
rt = os.path.join(ST, "cases", "real_r1_substitution", "runs"); os.makedirs(rt)
for t in ("s2_off_2", "s2_off_1", "s4c_noff_1", "s4c_poff_1"):
    os.symlink(os.path.join(REAL, t), os.path.join(rt, t))
r1 = os.path.join(rt, "s4c_poff_1_r1"); os.makedirs(r1)
for k in ("frames", "sites", "slow"):
    os.symlink("%s/s4c_poff_1_rr/perf_%s_s4c_poff_1_rr.csv" % (REAL, k), "%s/perf_%s_s4c_poff_1_r1.csv" % (r1, k))
os.symlink("%s/s4c_poff_1_rr/conditions.txt" % REAL, "%s/conditions.txt" % r1)
cl = [l for l in rd("%s/s4c_poff_1_rr/xray.log" % REAL) if l.startswith("command line ")][0]
open("%s/xray.log" % r1, "w").write(cl.replace(",s4c_poff_1_rr ", ",s4c_poff_1_r1 ") + "\n")
case("real_r1_substitution", 0, [r"RUN s4c_poff_1 used=s4c_poff_1_r1 VOID \(substituted: s4c_poff_1 is 'VERDICT: CONFOUNDED \(other heavy process present\)'\) \[coverage_p99\]$",
                                 r"RUN s2_off_1 used=s2_off_1 VOID \[coverage_p99\]$", r"conditions s4c_poff_1_r1: start heavy_others=\[\]"],
     real=[RL("s2_off_2", "SimTick14.exe", "bench1", 3000, 12000, 20, "probe"), RL("s2_off_1", "SimTick14.exe", "bench1", 3000, 12000, 20),
           RL("s4c_noff_1", "SimTick17.exe", "bench1", 3000, 12000, 20), RL("s4c_poff_1", "SimTick16.exe", "bench1", 3000, 12000, 20)], root=rt)

# ======================= scorer: made from real frame and site files =======================
case("good", 0, [r"^PROBE sf1_probe_cor ok", r"^PROBE sf1_probe_ros ok", r"^STOP RECORD .*/stop\.txt: class=done reason=list finished$", r"RUN sf1_cor_1 used=sf1_cor_1 VALID", r"RUN sf1_ros_3 used=sf1_ros_3 VALID",
                 r"SCENE cordon RESULT: DOMINANT alife", r"SCENE rostok RESULT: NO SINGLE DOMINANT CLASS; largest render",
                 r"^SF1 RESULT: cordon=DOMINANT alife 70%; rostok=MIXED largest render 4[45]%"], cor=G3, ros=G3)
case("good_crlf", 0, [r"SCENE cordon RESULT: DOMINANT alife", r"SCENE rostok RESULT: NO SINGLE DOMINANT CLASS; largest render"],
     cor=[{"crlf": True}] * 3, ros=[{"crlf": True}] * 3, probe={"crlf": True}, probe_ros={"crlf": True})
case("freeze_thread_excluded", 0, [r"SCENE cordon RESULT: DOMINANT alife", r"^  main_unzoned_or_wait +9 "],
     cor=[{"freeze": True}] * 3, ros=G3)
for c in ["alife", "script", "render", "gpu_wait", "online_sim", "io_stream", "sound", "other_known"]:
    case("class_" + c, 0, [r"^  %s +90 +100\.0%%" % c, r"SCENE cordon RESULT: DOMINANT %s " % c],
         cor=[{"plan": (lambda k, c=c: (c, 0.8))}] * 3, ros=G3)
for c in ["gc", "main_unzoned_or_wait", "unknown"]:
    case("class_" + c, 0, [r"^  %s +90 +100\.0%%" % c, r"SCENE cordon NO VERDICT \[coarse\]: 100\.0%"],
         cor=[{"plan": (lambda k, c=c: (c,) if c != "gc" else ("gc", 0.8))}] * 3, ros=G3)
case("theta_above", 0, [r"^  alife +90 +100\.0%"], cor=[{"plan": lambda k: ("alife", 0.55)}] * 3, ros=G3)
case("theta_below", 0, [r"^  main_unzoned_or_wait +90 +100\.0%"], cor=[{"plan": lambda k: ("alife", 0.45)}] * 3, ros=G3)
case("missing_frames", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VOID \[missing\]$", r"MISSING .*/sf1_cor_1/perf_frames_sf1_cor_1\.csv",
                           r"SCENE cordon NO VERDICT \[runs\]: 2 of 3", r"SCENE rostok RESULT"],
     cor=[{"skip": ("frames",)}, {}, {}], ros=G3)
case("missing_sites", 0, [r"MISSING .*/sf1_cor_2/perf_sites_sf1_cor_2\.csv"], cor=[{}, {"skip": ("sites",)}, {}], ros=G3)
case("missing_conditions", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VOID \[missing\]", r"MISSING .*/sf1_cor_1/conditions\.txt"],
     cor=[{"skip": ("cond",)}, {}, {}], ros=G3)
case("missing_xray_log", 0, [r"RUN sf1_cor_2 used=sf1_cor_2 VOID \[missing\]$", r"MISSING .*/sf1_cor_2/xray\.log"],
     cor=[{}, {"skip": ("xray",)}, {}], ros=G3)
case("missing_folder", 0, [r"RUN sf1_cor_3 used=sf1_cor_3 VOID \[missing\]$", r"MISSING folder .*/sf1_cor_3$"],
     cor=[{}, {}, {"absent": True}], ros=G3)
case("empty_slow", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VOID \[empty\]$", r"EMPTY .*/perf_slow_sf1_cor_1\.csv"],
     cor=[{"empty": ("slow",)}, {}, {}], ros=G3)
case("malformed_row", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VOID \[malformed\]$"], cor=[{"malformed": True}, {}, {}], ros=G3)
case("framecount", 0, [r"RUN sf1_ros_1 used=sf1_ros_1 VOID \[framecount\]"], cor=G3, ros=[{"drop_last": True}, {}, {}])
case("threshold_header", 0, [r"RUN sf1_cor_2 used=sf1_cor_2 VOID \[threshold_header\] \[slow_rows\]$"], cor=[{}, {"header_T": 7.0}, {}], ros=G3)
case("simtick_on", 0, [r"RUN sf1_ros_2 used=sf1_ros_2 VOID \[stock_loop\]$"], cor=G3,
     ros=[{}, {"sites_add": ("Sim tick,synthetic,1014,100,0.1000,0.1000,0,0.0000,0.0000",)}, {}])
case("seqframe_count", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VOID \[stock_loop\]$", r"'Process seqFrame' main calls 2995 != frames 3000"],
     cor=[{"seqframe_delta": -5}, {}, {}], ros=G3)
case("frame_limiter", 0, [r"RUN sf1_cor_3 used=sf1_cor_3 VOID \[frame_limiter\]$"],
     cor=[{}, {}, {"sites_add": ("Eco Render,synthetic,758,3000,9000.0,9000.0,0,0.0000,0.0000",)}], ros=G3)
case("cmdline_fixed_dt", 0, [r"RUN sf1_ros_3 used=sf1_ros_3 VOID \[cmdline\]$", r"-perf_fixed_dt present"], cor=G3, ros=[{}, {}, {"fixed_dt": True}])
case("fatal_in_run", 0, [r"RUN sf1_cor_2 used=sf1_cor_2 VOID \[fatal\]$"], cor=[{}, {"fatal": True}, {}], ros=G3)
case("row_ms_differs", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VOID \[slow_rows\]$"], cor=[{"rows_off": True}, {}, {}], ros=G3)
case("not_clean_no_rerun", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VOID \[not_clean\]$", r"there is no sf1_cor_1_r1"],
     cor=[{"verdict": "VERDICT: CONFOUNDED (other heavy process present)", "heavy_start": "150.0%:rustc"}, {}, {}], ros=G3)
case("not_clean_r1_clean", 0, [r"RUN sf1_cor_1 used=sf1_cor_1_r1 VALID \(substituted: sf1_cor_1 is 'VERDICT: CONFOUNDED",
                               r"SCENE cordon RESULT: DOMINANT alife"],
     cor=[{"verdict": "VERDICT: CONFOUNDED (other heavy process present)", "rr": "VERDICT: clean"}, {}, {}], ros=G3)
case("not_clean_r1_not_clean", 0, [r"RUN sf1_cor_1 used=sf1_cor_1_r1 VOID .*\[not_clean\]"],
     cor=[{"verdict": "VERDICT: CONFOUNDED (other heavy process present)", "rr": "VERDICT: CONFOUNDED (other heavy process present)"}, {}, {}], ros=G3)
case("unexpected_rerun", 0, [r"RUN sf1_ros_2 used=sf1_ros_2 VOID \[unexpected_rerun\]$"], cor=G3, ros=[{}, {"rr": "VERDICT: clean"}, {}])
case("heavy_others_reported", 0, [r"conditions sf1_cor_2: start heavy_others=\[\]; end heavy_others=\[212\.0%:cc1plus \]$"],
     cor=[{}, {"heavy_end": "212.0%:cc1plus "}, {}], ros=G3)
case("coverage_p99", 0, [r"RUN sf1_cor_2 used=sf1_cor_2 VOID \[coverage_p99\]$"], cor=[{}, {"scale": 0.55}, {}], ros=G3)
case("window_short", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VOID \[window_short\]$"], cor=[{"norows_from": 1000}, {}, {}], ros=G3)
case("window_cut_valid", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VALID .*window cut at frame 2[45][0-9][0-9] of 3000",
                             r"SCENE cordon RESULT: DOMINANT alife"], cor=[{"norows_from": 2400}, {}, {}], ros=G3)
case("train_in_window", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VOID \[train\]$"], cor=[{"train": (500, 560, 620)}, {}, {}], ros=G3)
case("train_two_only", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VALID"], cor=[{"train": (500, 560)}, {}, {}], ros=G3)
case("train_far_apart", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VALID"], cor=[{"train": (500, 1100, 1700)}, {}, {}], ros=G3)
case("p99_spread", 0, [r"SCENE cordon NO VERDICT \[p99_spread\]"], cor=[{"scale": 1.3}, {}, {}], ros=G3)
case("coarse_majority", 0, [r"SCENE cordon NO VERDICT \[coarse\]: 60\.0%"],
     cor=[{"plan": lambda k: ("alife", 0.8) if k % 10 < 4 else ("unknown",)}] * 3, ros=G3)
case("share_repeat", 0, [r"SCENE cordon NO VERDICT \[share_repeat\]", r"repeat alife +sf1_cor_1 share 0\.000"],
     cor=[{"plan": lambda k: ("render", 0.8)}, {}, {}], ros=G3)
# V-1: a failed or missing probe voids ITS OWN scene only (the driver stops the whole series on either)
case("probe_failed_in_scorer", 0, [r"^PROBE sf1_probe_cor STOP \[fatal\]$", r"RUN sf1_cor_1 used=sf1_cor_1 VALID",
                                   r"SCENE cordon NO VERDICT \[probe\]", r"^SF1 RESULT: cordon=NO VERDICT \(probe\); rostok=MIXED largest render"],
     cor=G3, ros=G3, probe={"fatal": True})
case("probe_missing_in_scorer", 0, [r"^PROBE sf1_probe_cor STOP \[missing\]$", r"^SF1 RESULT: cordon=NO VERDICT \(probe\); rostok=MIXED"],
     cor=G3, ros=G3, probe={"absent": True})
case("probe_ros_failed_only_rostok", 0, [r"^PROBE sf1_probe_ros STOP \[cmdline\]$", r"SCENE rostok NO VERDICT \[probe\]: the probe load sf1_probe_ros",
                                         r"^SF1 RESULT: cordon=DOMINANT alife 70%; rostok=NO VERDICT \(probe\)$"], cor=G3, ros=G3, probe_ros={"fixed_dt": True})
# the stop record (prereg 6.4): read from the order file's folder; exactly one physical line, class done
SN = r"^SF1 RESULT: cordon=NO VERDICT \(stop\); rostok=NO VERDICT \(stop\)$"
case("stop_missing", 0, [r"STOP RECORD NOT DONE \[missing\]: no stop record .*/stop\.txt", r"SCENE cordon NO VERDICT \[stop\]", SN], cor=G3, ros=G3, stop=None)
case("stop_blank_second_line", 0, [r"STOP RECORD NOT DONE \[lines\]: .* is not exactly one physical line ended by LF \(2 line ends", SN], cor=G3, ros=G3,
     stop="class=done reason=list finished\n\n")
case("stop_two_lines", 0, [r"STOP RECORD NOT DONE \[lines\]", SN], cor=G3, ros=G3, stop="class=done reason=list finished\nclass=done reason=list finished\n")
case("stop_no_line_end", 0, [r"STOP RECORD NOT DONE \[lines\]: .*\(0 line ends", SN], cor=G3, ros=G3, stop="class=done reason=list finished")
case("stop_box", 0, [r"STOP RECORD NOT DONE \[class\]: the series did not finish \(class=box reason=not quiet before sf1_ros_2", SN], cor=G3, ros=G3,
     stop="class=box reason=not quiet before sf1_ros_2 (settle cap)\n")
case("stop_fault", 0, [r"STOP RECORD NOT DONE \[class\]", SN], cor=G3, ros=G3, stop="class=fault reason=load sf1_ros_2 failed a check\n")
case("stop_box_interrupted", 0, [r"STOP RECORD NOT DONE \[class\]: .*interrupted=sf1_ros_2", SN], cor=G3, ros=G3,
     stop="class=box interrupted=sf1_ros_2 reason=cancelled during sf1_ros_2\n")
case("stop_done_interrupted", 0, [r"STOP RECORD NOT DONE \[malformed\]", SN], cor=G3, ros=G3, stop="class=done interrupted=sf1_ros_2 reason=x\n")
case("stop_malformed", 0, [r"STOP RECORD NOT DONE \[malformed\]", SN], cor=G3, ros=G3, stop="done\n")
case("stop_crlf_done", 0, [r"^SF1 RESULT: cordon=DOMINANT alife 70%; rostok=MIXED largest render"], cor=G3, ros=G3, stop="class=done reason=list finished\r\n")
case("out_exists", 2, [r"REFUSED: .*score\.txt exists, not overwriting"], cor=G3, ros=G3, pre_out=True)
case("uppercase_tag", 2, [r"REFUSED: tag SF1_cor_1 is not lowercase"], cor=G3, ros=G3,
     order_extra=lambda L: L[:2] + [L[2].replace("sf1_cor_1", "SF1_cor_1", 1)] + L[3:])
case("two_slots", 2, [r"REFUSED: scene cordon has not exactly 3 slots"], cor=G3, ros=G3, order_extra=lambda L: L[:2] + L[3:])
case("six_fields", 2, [r"REFUSED: order line .* is not tag\|exe\|save\|warm\|frames\|slow_ms\|arm"], cor=G3, ros=G3,
     order_extra=lambda L: L[:2] + [L[2].rsplit("|", 1)[0]] + L[3:])
case("probe_after_scored", 2, [r"REFUSED: every probe line must come before the first scored line"], cor=G3, ros=G3,
     order_extra=lambda L: [L[0]] + L[2:3] + L[1:2] + L[3:])
case("two_probes_one_scene", 2, [r"REFUSED: scene cordon has not exactly one probe line"], cor=G3, ros=G3,
     order_extra=lambda L: [L[0], L[0].replace("sf1_probe_cor", "sf1_probe_cor2")] + L[1:])
case("scene_without_probe", 2, [r"REFUSED: scene rostok has not exactly one probe line"], cor=G3, ros=G3, order_extra=lambda L: [L[0]] + L[2:])
case("rerun_tag_in_order", 2, [r"REFUSED: tag sf1_cor_1_r1 looks like a re-run folder name"], cor=G3, ros=G3,
     order_extra=lambda L: L[:2] + [L[2].replace("sf1_cor_1|", "sf1_cor_1_r1|", 1)] + L[3:])
case("duplicate_tag", 2, [r"REFUSED: a tag is listed twice"], cor=G3, ros=G3, order_extra=lambda L: L[:3] + [L[2]] + L[4:])
with open(os.path.join(ST, "cases.tsv"), "w") as f:
    f.write("\n".join(CASES) + "\n")
PYEOF
[ -s "$ST/cases.tsv" ] || { say "SF1 SELFTEST FAIL: fixture builder failed"; exit 1; }

# ---- one case with a given scorer: prints nothing, exit 0 if every expectation holds, else 1 and the reason on fd 3 ----
run_case() {   # <scorer> <outdir> <name> <mode> <arg> <order> <out> <want> <patterns...>
  local sc=$1 od=$2 name=$3 mode=$4 arg=$5 order=$6 out=$7 want=$8 rc before so pt why=""; shift 8
  mkdir -p "$od/$name"; so="$od/$name/stdout.txt"
  if [ "$mode" = score ]; then
    [ "$od" = "$ST/cases" ] || { [ -e "$out" ] && cp -- "$out" "$od/$name/score.txt"; out="$od/$name/score.txt"; }
    before=""; [ -e "$out" ] && before=$(sha256sum "$out" | cut -d' ' -f1)
    python3 -I "$sc" root="$arg" order="$order" out="$out" > "$so" 2>&1; rc=$?
    [ -n "$before" ] && [ "$(sha256sum "$out" | cut -d' ' -f1)" != "$before" ] && why="$why; existing out file changed"
  else
    out=/dev/null; python3 -I "$sc" "$mode=$arg" order="$order" > "$so" 2>&1; rc=$?
  fi
  [ "$rc" = "$want" ] || why="$why; exit $rc, wanted $want"
  for pt in "$@"; do cat "$so" "$out" 2>/dev/null | grep -Eq -- "$pt" || why="$why; no match: $pt"; done
  [ -z "$why" ] && return 0
  echo "$why" >&3; return 1
}
pass=0; fail=0; nreal=0; nmade=0; ngate=0
while IFS=$'\t' read -r name mode arg order out want rest; do
  [ -n "$name" ] || continue
  case "$name" in gate_*|check_*) ngate=$((ngate+1));; real_*) nreal=$((nreal+1));; *) nmade=$((nmade+1));; esac
  IFS=$'\t' read -r -a pats <<<"$rest"
  if why=$(run_case "$SC" "$ST/cases" "$name" "$mode" "$arg" "$order" "$out" "$want" "${pats[@]}" 3>&1); then
    pass=$((pass+1)); say "ok   $name"; else fail=$((fail+1)); say "FAIL $name:$why"; fi
done < "$ST/cases.tsv"

# ---- checker mutants: a scorer copy with one rule removed or one constant moved; at least one listed case must fail ----
MD="$ST/mutants"; mkdir -p "$MD"; nmut=0
while IFS='|' read -r mname expr mcases; do
  [ -n "$mname" ] || continue
  sed "$expr" "$SC" > "$MD/sf1_classify_$mname.py"
  if cmp -s "$SC" "$MD/sf1_classify_$mname.py"; then fail=$((fail+1)); say "FAIL mutant $mname: the sed did not change the scorer"; continue; fi
  caught=""
  for cn in ${mcases//,/ }; do
    l=$(awk -F'\t' -v n="$cn" '$1==n' "$ST/cases.tsv"); [ -n "$l" ] || { caught="$caught MISSING-CASE:$cn"; continue; }
    IFS=$'\t' read -r name mode arg order out want rest <<<"$l"; IFS=$'\t' read -r -a pats <<<"$rest"
    run_case "$MD/sf1_classify_$mname.py" "$MD/$mname" "$name" "$mode" "$arg" "$order" "$out" "$want" "${pats[@]}" 3>/dev/null || caught="$caught $name"
  done
  case "$caught" in *MISSING-CASE*|"") fail=$((fail+1)); say "FAIL mutant $mname survived (cases: $mcases)${caught:+ $caught}";;
    *) pass=$((pass+1)); nmut=$((nmut+1)); say "ok   mutant $mname caught by:$caught";; esac
done <<'MUTEOF'
train|s/if worst >= TRAIN_MIN:/if False:/|train_in_window,gate_made_train,gate_real_train
coverage|s/if p99 < thr \* (1 + COVER_TOL):/if False:/|coverage_p99,gate_made_coverage,gate_real_s2_off_1_T20
sim_tick|s/    if st:/    if False:/|simtick_on,gate_made_simtick,gate_real_simtick_on
seqframe|s/elif seq\[0\]\["cm"\] != N:/elif False:/|seqframe_count,gate_real_warm0_seqframe
limiter|s/    if eco:/    if False:/|frame_limiter,gate_made_limiter,gate_real_capped
exclusion|s/if n in EXCLUDED:/if False:/|freeze_thread_excluded
theta|s/^THETA = 0\.5 /THETA = 0.4 /|theta_below
theta_high|s/^THETA = 0\.5 /THETA = 0.6 /|theta_above
substitution|s/used = rr$/used = s["tag"]/|not_clean_r1_clean,real_r1_substitution
share_repeat|s/rep_ok &= ok/rep_ok \&= True/|share_repeat
window_cut|s/if v > thr \* (1 + COVER_TOL) and i not in rows:/if False:/|window_short,window_cut_valid,gate_made_window_short
fixed_dt|s/if "-perf_fixed_dt" in toks:/if False:/|cmdline_fixed_dt,gate_made_fixed_dt,gate_real_fixed_step,check_real_fixed_step
cmd_exe|s/if not toks or not toks\[0\].lower().endswith/if False and toks[0].lower().endswith/|gate_real_wrong_exe
cmd_capture|s/if after("-perf_capture") != cap:/if False:/|gate_real_framecount
fatal|s/    if fat:/    if False:/|fatal_in_run,gate_made_fatal,check_made_fatal
zones|s/    if miss:/    if False:/|gate_made_zone_gpu_missing
zones_class|s/+ \["class " + c for c in REQUIRED_CLASSES if not have.get(c)\]/+ []/|gate_made_zone_gpu_missing
frame_range|s/    if not (lo <= p50_all <= hi):/    if False:/|gate_made_range_low,gate_made_range_high,gate_made_ros_range_high,gate_real_capped
range_hi|s/"cordon1": (1\.5, 8\.0)/"cordon1": (1.5, 9.9)/|gate_made_range_high
range_lo|s/"cordon1": (1\.5, 8\.0)/"cordon1": (1.0, 8.0)/|gate_made_range_low
ros_range_hi|s/"bench1": (2\.0, 10\.0)/"bench1": (2.0, 12.0)/|gate_made_ros_range_high
ros_range_lo|s/"bench1": (2\.0, 10\.0)/"bench1": (1.5, 10.0)/|gate_made_ros_range_low
ros_range_is_cordons|s/lo, hi = PROBE_P50\[slot\["save"\]\]/lo, hi = PROBE_P50["cordon1"]/|gate_made_ros_between_ranges,gate_made_ros_range_high
probe_voids_both|s/if not probe_ok\[sc\]\[0\]:/if not all(v[0] for v in probe_ok.values()):/|probe_failed_in_scorer,probe_ros_failed_only_rostok
probe_order|s/if "probe" in arms\[first_scored:\]:/if False:/|probe_after_scored
probe_per_scene|s/if s\["arm"\] == "probe" and s\["scene"\] == sc) != 1:/if s["arm"] == "probe") < 1:/|two_probes_one_scene
stop_not_read|s/        if stop_bad:/        if False:/|stop_missing,stop_box,stop_blank_second_line
stop_missing_ok|s/        stop_bad = "missing: no stop record %s" % sp/        stop_bad = None/|stop_missing
stop_lines|s/        if body is None or "\\n" in body or "\\r" in body:/        if body is None:/|stop_blank_second_line,stop_two_lines
stop_rstrip|s/body = raw\[:-1\] if raw.endswith("\\n") else None/body = raw.rstrip("\\r\\n")/|stop_blank_second_line,stop_no_line_end
stop_class|s/        elif not body.startswith("class=done "):/        elif False:/|stop_box,stop_fault,stop_box_interrupted
stop_form|s/        elif not re.fullmatch(STOP_RE, body):/        elif False:/|stop_done_interrupted,stop_malformed
rows_match|s/        if off:/        if False:/|row_ms_differs,gate_made_row_ms_differs
rows_present|s/    if not D\["rows"\]:/    if False:/|gate_made_no_slow_rows
unexpected_rerun|s/elif os.path.isdir(os.path.join(args\["root"\], rr)):/elif False:/|unexpected_rerun
probe_in_scorer|s/probe_ok\[pr\["scene"\]\] = (not PR\["reasons"\], pr\["tag"\])/probe_ok[pr["scene"]] = (True, pr["tag"])/|probe_failed_in_scorer,probe_missing_in_scorer,probe_ros_failed_only_rostok
framecount|s/if N != slot\["frames"\] or nfr != slot\["frames"\]:/if False:/|framecount,gate_made_framecount,gate_real_framecount
threshold_hdr|s/if abs(thr - slot\["slow"\]) > 0\.0005:/if False:/|threshold_header,gate_made_threshold_header,gate_real_threshold_and_cmdline
unexplained|s/^UNEXPLAINED_MAX = 0\.5 /UNEXPLAINED_MAX = 0.65 /|coarse_majority
p99_spread|s/^P99_SPREAD = 0\.12 /P99_SPREAD = 0.5 /|p99_spread
no_cr_strip|s/return \[l.rstrip("\\r") for l in raw/return [l for l in raw/|good_crlf,gate_made_pass_crlf
probe_wrong_arm|s/(s\[0\]\["arm"\] == "probe") != probe/False/|gate_refused_scored_folder,check_refused_probe
MUTEOF

# ---- the driver's logic, offline: sf1_run.sh is never run; its marked function block is extracted and run with the gate
# helpers, the bench, the check and quiet-required.sh as stubs; the options restore runs for real on a SCRATCH layout ----
DT="$ST/driver"; mkdir -p "$DT"
sed -n '/^# ---- BEGIN driver functions/,/^# ---- END driver functions/p' "$DRV" > "$DT/funcs.sh"
RMAX_D=$(sed -n 's/^NSCORED=6; NPROBE=2; R_MAX=\([0-9][0-9]*\) .*/\1/p' "$DRV")
AXR_SHA_D=$(sed -n 's/^AXR_SHA=\([0-9a-f]\{64\}\)$/\1/p' "$DRV")
mkdir -p "$DT/axr_src"; cp -- "$P/run/appdata/axr_options_play.ltx" "$DT/axr_src/axr_options_play.ltx"   # read the real file once
drv_tests() {   # <function file> <work dir> <R_MAX>: prints one line per failed test; exit status = number failed
  ( set -u; EXE=Coarse14.exe; NSCORED=6; NPROBE=2; D=$2; f=0; W=engineperf; WHAT="EXCLUSIVE: test"
    . "$1"
    mkdir -p "$D"; src="$D/order_src.txt"; cp -- "$HERE/sf1_order.txt" "$src"; want=$(sha256sum "$src" | cut -d' ' -f1)
    # 1. the registered order shape: the real sf1_order.txt passes; each wrong shape is refused by name
    order_ok "$HERE/sf1_order.txt" > "$D/t0.txt" 2>&1 || { echo "order_ok refused the registered order: $(tr '\n' ' ' < "$D/t0.txt")"; f=$((f+1)); }
    oc() { grep -v '^#' "$HERE/sf1_order.txt" | sed "$1" > "$D/o_$2.txt"; order_ok "$D/o_$2.txt" > "$D/o_$2.out" 2>&1 && { echo "order_ok accepted $2"; f=$((f+1)); }; }
    oc '1{h;d};2G' probes_swapped; oc '2{h;d};3G' ros_probe_after_scored; oc 's/^sf1_cor_1|\(.*\)|scored$/sf1_cor_1|\1|probe/' three_probes
    oc 's/^sf1_ros_3|/sf1_ros_2_r1|/' rerun_tag; oc '2d' no_ros_probe; oc '1d' no_cor_probe
    oc 's/^sf1_cor_2|\(.*\)|6\.0|scored$/sf1_cor_2|\1|8.0|scored/' wrong_slow; oc 's/^sf1_ros_2|\(.*\)|8\.0|scored$/sf1_ros_2|\1|9.0|scored/' old_ros_slow
    oc '$d' seven_loads; oc 's/^sf1_ros_3|/sf1_ros_2|/' dup_tag
    oc '4s/|14000|/|11500|/' old_warm; oc '1s/|14000|/|11500|/' old_warm_probe; oc 's/^sf1_cor_1|/SF1_cor_1|/' upper
    oc 's/^sf1_probe_cor|/sf1_probe|/' old_probe_tag; oc '1s/|6\.0|probe$/|8.0|probe/' probe_slow; oc '2s/|8\.0|probe$/|9.0|probe/' ros_probe_slow
    oc '2s/|bench1|/|cordon1|/' ros_probe_wrong_scene
    # 2. order copy: hash, read-only, refusals
    mkdir "$D/st1"; ST="$D/st1"; : > "$ST/done.txt"
    order_copy "$src" "$want" "$ST" > "$D/t1.txt" 2>&1 || { echo "order_copy refused the registered order ($(tr '\n' ' ' < "$D/t1.txt"))"; f=$((f+1)); }
    if [ "$(id -u)" != 0 ] && [ -w "$ST/sf1_order.txt" ]; then echo "the copy is writable"; f=$((f+1)); fi
    printf 'sf1_evil|Coarse14.exe|cordon1|14000|10000|6.0|scored\n' >> "$src"
    mkdir "$D/st2"; order_copy "$src" "$want" "$D/st2" > "$D/t2.txt" 2>&1 && { echo "a changed order was accepted"; f=$((f+1)); }
    grep -q "failed its hash check" "$D/t2.txt" || { echo "the hash refusal is not named"; f=$((f+1)); }
    mkdir "$D/st3"; order_copy "$HERE/sf1_order.txt" PIN_ME_ORDER "$D/st3" > "$D/t3.txt" 2>&1 && { echo "PIN_ME_ORDER accepted"; f=$((f+1)); }
    order_copy "$HERE/sf1_order.txt" "$want" "$D/st1" > "$D/t4.txt" 2>&1 && { echo "an existing copy was overwritten"; f=$((f+1)); }
    # 3. OPTIONS RESTORE on a scratch layout (run/appdata, run/gamedata/configs under $D; the real run/ is never named)
    A="$D/axr"; mkdir -p "$A/run/appdata" "$A/run/gamedata/configs"
    printf 'version = 20261126\nselftest options source\n' > "$A/run/appdata/axr_options_play.ltx"   # small stand-in (size); the real bytes: below
    S="$A/run/appdata/axr_options_play.ltx"; Dd="$A/run/gamedata/configs/axr_options.ltx"; K=$(sha256sum < "$S" | cut -c1-64)
    printf 'version = 20261122 (rewritten by a boot)\n' > "$Dd"
    r=$(axr_restore "$S" "$Dd" "$K"); rr=$?
    [ $rr = 0 ] && cmp -s "$S" "$Dd" && [ "$(sha256sum < "$Dd" | cut -c1-64)" = "$K" ] || { echo "restore: a good source did not give the registered destination (rc $rr: $r)"; f=$((f+1)); }
    printf 'version = stale\n' > "$Dd"; d0=$(sha256sum < "$Dd"); cp -- "$S" "$A/good.ltx"; printf ' ' >> "$S"
    r=$(axr_restore "$S" "$Dd" "$K"); rr=$?
    [ $rr = 1 ] && [ "$d0" = "$(sha256sum < "$Dd")" ] && case "$r" in "REFUSED: options restore: sha256 of source $S is "*"(destination untouched)") true;; *) false;; esac || \
      { echo "restore: a source with another hash was not refused by name with the destination untouched (rc $rr: $r)"; f=$((f+1)); }
    mv -- "$S" "$A/moved.ltx"; r=$(axr_restore "$S" "$Dd" "$K"); rr=$?
    [ $rr = 1 ] && [ "$d0" = "$(sha256sum < "$Dd")" ] && [ "$r" = "REFUSED: options restore: source $S missing" ] || { echo "restore: a missing source (rc $rr: $r)"; f=$((f+1)); }
    cp -- "$A/good.ltx" "$S"; r=$(axr_restore "$S" "$A/no/such/dir/axr_options.ltx" "$K"); rr=$?
    [ $rr = 1 ] && [ "$r" = "REFUSED: options restore: copy to $A/no/such/dir/axr_options.ltx failed" ] || { echo "restore: a failed copy (rc $rr: $r)"; f=$((f+1)); }
    r=$( cp() { command cp "$@" && printf 'x' >> "${@: -1}"; }; axr_restore "$S" "$Dd" "$K" ); rr=$?   # the copy lands altered
    [ $rr = 1 ] && case "$r" in "REFUSED: options restore: sha256 of $Dd after the copy is "*) true;; *) false;; esac || { echo "restore: an altered destination was not refused (rc $rr: $r)"; f=$((f+1)); }
    # 4. slice deadline
    now=$(date +%s)
    slice_ok $((now + 1100)) 1020 || { echo "slice_ok refused 1100 s left for 1020 s"; f=$((f+1)); }
    slice_ok $((now + 1000)) 1020 && { echo "slice_ok accepted 1000 s left for 1020 s"; f=$((f+1)); }
    # 5. verdict_of and cond_of on REAL engine-side conditions.txt files, read in place
    OUT=$REAL
    [ "$(verdict_of wu3_02_off)" = "VERDICT: clean" ] && [ "$(verdict_of wu3_01_on)" = "VERDICT: CONFOUNDED (other heavy process present)" ] || \
      { echo "verdict_of on the real wu3_02_off / wu3_01_on: '$(verdict_of wu3_02_off)' / '$(verdict_of wu3_01_on)'"; f=$((f+1)); }
    case "$(cond_of wu3_01_on)" in "conditions wu3_01_on: start "*"heavy_others=["*"|end "*"heavy_others=["*"|VERDICT: CONFOUNDED (other heavy process present)|") ;;
      *) echo "cond_of on the real wu3_01_on: '$(cond_of wu3_01_on)'"; f=$((f+1)) ;; esac
    # 6. series_loop offline. Gate helpers, the bench and the check are stubs that log "<name> <tag>"; the restore is the REAL
    # function on the scratch layout; the bench stub records the options file's hash at its boot, then rewrites it (as an
    # exe does), and writes conditions.txt (heavy_others, VERDICT) in CRLF bytes
    cp -- "$A/good.ltx" "$S"; AXR_SRC=$S; AXR_DST=$Dd; AXR_SHA=$K
    SL_END=$(( $(date +%s) + 100000 )); BENCH_TO=900; END_RESERVE=120; DISP=:141; R_MAX=$3
    rec() { echo "$1 $2" >> "$ST/calls"; }
    field() { awk -v t="$2" -v k="$1" '$1==t{print (k=="v" ? $2 : (k=="rc" ? $3 : $4)); x=1} END{if (!x) print (k=="v" ? "clean" : 0)}' "$ST/table"; }
    log() { echo "$*" >> "$ST/series.log"; echo "log $*" >> "$ST/calls"; }
    settle() { rec settle "$1"; [ "${CAP_AT:-}" = "$1" ] && { echo "settle wait=600s load=3.0 heavy=[100%:x] cap"; return 1; }; echo "settle wait=0s load=0.5 heavy=[] ok"; }
    title_running() { rec title "$1"; return 1; }
    roblox_running() { rec roblox "$1"; return 1; }
    caden_lock() { rec caden "$1"; return 1; }
    disp_wait() { rec disp "$1"; return 0; }
    cfg_ok() { rec cfg "$1"; [ "${CFG_AT:-}" != "$1" ]; }
    exe_ok() { rec exe "$1"; return 0; }
    claim_renew() { rec claim "$1"; [ "${CLAIM_AT:-}" != "$1" ]; }
    slice_ok() { rec slice "$2"; return 0; }
    bench() { rec bench "$1"; mkdir "$OUT/$1" || return 9
      echo "$1 $(sha256sum < "$AXR_DST" 2>/dev/null | cut -c1-64)" >> "$ST/boots"; printf 'version = 20261122\n' > "$AXR_DST"
      [ "${CANCEL_AT:-}" = "$1" ] && { cancel; stop_write; exit 143; }
      if [ "$(field v "$1")" = conf ]; then printf 'start x heavy_others=[150.0%%:rustc ]\r\nend   y heavy_others=[]\r\nVERDICT: CONFOUNDED (other heavy process present)\r\n'
      else printf 'start x heavy_others=[]\r\nend   y heavy_others=[]\r\nVERDICT: clean\r\n'; fi > "$OUT/$1/conditions.txt"
      return "$(field rc "$1")"; }
    check_run() { rec check "$1"; local r w=CHECK; r=$(field crc "$1"); [ "$2" = probe ] && w=PROBE
      if [ "$r" = 0 ]; then echo "$w $1 ok"; else echo "$w STOP zones: not in perf_sites with calls: class gpu_wait"; echo "$w $1 FAIL [zones]"; fi
      return "$r"; }
    scen() {   # <name> [<tag> <conf|clean> <bench rc> <check rc>]...: one offline series; sets B (bench calls), S (stop record)
      local n=$1; shift
      ST="$D/s_$n"; OUT="$ST/out"; mkdir -p "$OUT"; : > "$ST/done.txt"; : > "$ST/calls"; : > "$ST/table"; : > "$ST/boots"; : > "$ST/conditions.log"
      while [ $# -ge 4 ]; do echo "$1 $2 $3 $4" >> "$ST/table"; shift 4; done
      cp -- "$HERE/sf1_order.txt" "$ST/sf1_order.txt"; ORDER="$ST/sf1_order.txt"
      stop=""; sc=""; rc=0; nnc=0; nrr=0; PROBED=0; LIVE=""; INTR=""
      ( series_loop; stop_write )
      B=$(grep '^bench ' "$ST/calls" | cut -d' ' -f2 | tr '\n' ' '); SR=$(cat "$ST/stop.txt" 2>/dev/null)
      CAP_AT=""; CFG_AT=""; CLAIM_AT=""; CANCEL_AT=""; }
    nb() { echo "$B" | wc -w; }
    DONE="class=done reason=list finished"
    scen clean
    [ "$B" = "sf1_probe_cor sf1_probe_ros sf1_cor_1 sf1_ros_1 sf1_ros_2 sf1_cor_2 sf1_cor_3 sf1_ros_3 " ] && [ "$SR" = "$DONE" ] || { echo "clean: bench '$B' stop '$SR'"; f=$((f+1)); }
    # every boot saw the registered options file although each boot rewrote it (the restore runs before EVERY boot)
    [ "$(grep -c " $K$" "$ST/boots")" = 8 ] && [ "$(grep -c . "$ST/boots")" = 8 ] || { echo "restore: $(grep -vc " $K$" "$ST/boots") of $(grep -c . "$ST/boots") boots started from other options bytes"; f=$((f+1)); }
    # gate order before every bench: claim renewal, then the restore, then the slice recheck (1020 s), then the bench
    awk '$1=="claim"{c=$2; a=0; s=0} $1=="log" && $2=="options" && $3=="restored:" && c!=""{a=1} $1=="slice" && $2=="1020" && a{s=1} $1=="bench"{if (c!=$2 || !a || !s) bad=bad" "$2; c=""; a=0; s=0} END{if (bad) {print bad; exit 1}}' "$ST/calls" > "$D/gates.txt" || \
      { echo "gates: no claim -> restore -> slice recheck right before the bench of:$(head -c 70 "$D/gates.txt")"; f=$((f+1)); }
    [ "$(grep -c . "$ST/conditions.log")" = 8 ] && [ "$(grep -c 'heavy_others=\[' "$ST/conditions.log")" = 8 ] && ! grep -q $'\r' "$ST/conditions.log" || \
      { echo "conditions.log: $(grep -c . "$ST/conditions.log") lines (want 8, each with heavy_others, CR stripped)"; f=$((f+1)); }
    scen probe_fail sf1_probe_cor clean 0 1
    [ "$B" = "sf1_probe_cor " ] && case "$SR" in "class=fault reason=PROBE FAILED on sf1_probe_cor, series stopped before any scored run: FAIL [zones] PROBE STOP zones: "*) true;; *) false;; esac || \
      { echo "probe_fail: a failed probe did not stop before any scored run with the failed check named (bench '$B' stop '$SR')"; f=$((f+1)); }
    scen probe_ros_fail sf1_probe_ros clean 0 1
    [ "$B" = "sf1_probe_cor sf1_probe_ros " ] && case "$SR" in "class=fault reason=PROBE FAILED on sf1_probe_ros, series stopped before any scored run: FAIL [zones] PROBE STOP zones: "*) true;; *) false;; esac || \
      { echo "probe_ros_fail: a failed Rostok probe did not stop before any scored run (bench '$B' stop '$SR')"; f=$((f+1)); }
    scen probe_not_clean sf1_probe_cor conf 0 0 sf1_probe_ros conf 0 0
    [ "$(nb)" = 8 ] && ! echo "$B" | grep -q _r1 && [ "$SR" = "$DONE" ] || { echo "probe_not_clean: re-run or stop ($(nb) loads, stop '$SR')"; f=$((f+1)); }
    # the scored gate itself (not only the probe-gate stop): with one probe "passed", a scored load must not boot
    ST="$D/s_gate1"; OUT="$ST/out"; mkdir -p "$OUT"; : > "$ST/calls"; : > "$ST/table"; PROBED=1; stop=""; sc=""; rc=0
    ( gates sf1_cor_1 scored; echo "gates=$?"; stop_write ) > "$D/gate1.txt" 2>&1
    [ "$(grep -c '^bench ' "$ST/calls")" = 0 ] && grep -qx 'gates=1' "$D/gate1.txt" && [ "$(cat "$ST/stop.txt" 2>/dev/null)" = "class=fault reason=scored load sf1_cor_1 reached without both probes passed (1 of 2)" ] || \
      { echo "probed_gate: one passed probe let a scored load through ($(tr '\n' ' ' < "$D/gate1.txt") stop '$(cat "$ST/stop.txt" 2>/dev/null)')"; f=$((f+1)); }
    scen rerun sf1_cor_1 conf 0 0
    [ "$B" = "sf1_probe_cor sf1_probe_ros sf1_cor_1 sf1_cor_1_r1 sf1_ros_1 sf1_ros_2 sf1_cor_2 sf1_cor_3 sf1_ros_3 " ] && [ "$SR" = "$DONE" ] || { echo "rerun: bench '$B' stop '$SR'"; f=$((f+1)); }
    grep -q 'conditions sf1_cor_1: start x heavy_others=\[150.0%:rustc \]|' "$ST/conditions.log" || { echo "rerun: the not-clean load's heavy_others is not in conditions.log"; f=$((f+1)); }
    scen twice sf1_cor_1 conf 0 0 sf1_cor_1_r1 conf 0 0
    [ "$(nb)" = 9 ] && [ "$SR" = "$DONE" ] && ! echo "$B" | grep -q sf1_cor_1_r2 || { echo "twice: $(nb) loads, stop '$SR' (want 9, done)"; f=$((f+1)); }
    scen all_six sf1_cor_1 conf 0 0 sf1_ros_1 conf 0 0 sf1_ros_2 conf 0 0 sf1_cor_2 conf 0 0 sf1_cor_3 conf 0 0 sf1_ros_3 conf 0 0
    [ "$(nb)" = 14 ] && [ "$SR" = "$DONE" ] || { echo "all_six: 6 not-clean first loads: $(nb) loads, stop '$SR' (want 14, $DONE)"; f=$((f+1)); }
    scen check sf1_cor_1 conf 0 1
    [ "$B" = "sf1_probe_cor sf1_probe_ros sf1_cor_1 " ] && [ "${SR%%: *}" = "class=fault reason=load sf1_cor_1 failed a check" ] || { echo "check: bench '$B' stop '$SR'"; f=$((f+1)); }
    scen crash sf1_cor_1 conf 5 0
    [ "$B" = "sf1_probe_cor sf1_probe_ros sf1_cor_1 " ] && [ "${SR%% (*}" = "class=fault reason=load sf1_cor_1 ended rc=5" ] || { echo "crash: bench '$B' stop '$SR'"; f=$((f+1)); }
    CAP_AT=sf1_ros_1; scen settle
    [ "$B" = "sf1_probe_cor sf1_probe_ros sf1_cor_1 " ] && [ "${SR%% (*}" = "class=box reason=not quiet before sf1_ros_1" ] || { echo "settle: bench '$B' stop '$SR'"; f=$((f+1)); }
    CAP_AT=sf1_cor_1_r1; scen settle_rerun sf1_cor_1 conf 0 0
    [ "$B" = "sf1_probe_cor sf1_probe_ros sf1_cor_1 " ] && [ "${SR%% (*}" = "class=box reason=not quiet before sf1_cor_1_r1" ] || { echo "settle_rerun: bench '$B' stop '$SR'"; f=$((f+1)); }
    CFG_AT=sf1_ros_1; scen cfg
    [ "$SR" = "class=fault reason=settings not stock/uncapped before sf1_ros_1" ] || { echo "cfg: stop '$SR'"; f=$((f+1)); }
    CLAIM_AT=sf1_ros_1; scen claim
    [ "$B" = "sf1_probe_cor sf1_probe_ros sf1_cor_1 " ] && [ "$SR" = "class=box reason=claim renewal refused before sf1_ros_1" ] || { echo "claim: stop '$SR'"; f=$((f+1)); }
    CANCEL_AT=sf1_ros_1; scen cancel_live
    [ "$SR" = "class=box interrupted=sf1_ros_1 reason=cancelled during sf1_ros_1" ] || { echo "cancel_live: stop '$SR'"; f=$((f+1)); }
    printf 'tampered\n' >> "$AXR_SRC"; scen restore_refused
    [ "$B" = "" ] && case "$SR" in "class=fault reason=REFUSED: options restore: sha256 of source $AXR_SRC is "*"(destination untouched) (before sf1_probe_cor)") true;; *) false;; esac || \
      { echo "restore_refused: a source with another hash did not stop before the first boot with a named fault (bench '$B' stop '$SR')"; f=$((f+1)); }
    cp -- "$A/good.ltx" "$AXR_SRC"
    # the stop record cannot be written: rc 1 (or the rc it had), SNOTE set
    mkdir -p "$D/norec/stop.txt"; ST="$D/norec"; sc=box; stop=x; INTR=""; SNOTE=""; rc=0
    record_stop; r=$?
    [ "$r" != 0 ] && [ "$rc" = 1 ] && [ "$SNOTE" = "stop record NOT written ($D/norec/stop.txt)" ] || { echo "record_stop: write failure gave return $r rc $rc note '$SNOTE'"; f=$((f+1)); }
    # 7. acquire: claim by EXIT CODE only. A stub quiet-required.sh prints DECOY text (claimed on a refusal, REFUSED on a
    # grant); kj4p handoff: a live claim under our name makes acquire wait, the queue's handoff pre-claim does not
    T="$D/qt"; mkdir -p "$T"; POLL_S=0; CANCEL=0; CLAIMED=0
    cat > "$T/quiet-required.sh" <<'QEOF'
#!/usr/bin/env bash
d="$(dirname "$0")"; echo "$*" >> "$d/qcalls"
case "$1" in
  check) n=$(grep -c '^check' "$d/qcalls"); l=$(sed -n "${n}p" "$d/checks" 2>/dev/null); [ -n "$l" ] && echo "$l"; exit 0 ;;
  claim) n=$(grep -c '^claim' "$d/qcalls"); r=$(sed -n "${n}p" "$d/claims"); [ "$r" = 0 ] && echo "REFUSED: decoy" || echo "claimed (decoy)"; exit "${r:-5}" ;;
  *) exit 0 ;;
esac
QEOF
    chmod +x "$T/quiet-required.sh"
    title_running() { return 1; }; roblox_running() { return 1; }; caden_lock() { return 1; }
    finish() { echo "finish called"; exit 99; }
    : > "$T/qcalls"; printf '5\n4\n0\n' > "$T/claims"; : > "$T/checks"
    ( acquire; echo "CLAIMED=$CLAIMED" ) > "$D/acq1.txt" 2>&1
    [ "$(tail -1 "$D/acq1.txt")" = "CLAIMED=1" ] && [ "$(grep -c '^claim ' "$T/qcalls")" = 3 ] && [ "$(grep -c '^want ' "$T/qcalls")" = 2 ] && [ "$(tail -1 "$T/qcalls")" = "withdraw engineperf" ] || \
      { echo "acquire: exit codes 5, 4, 0 did not give 3 claims, 2 wants, a withdraw and CLAIMED=1 ($(tr '\n' ';' < "$T/qcalls") / $(tail -1 "$D/acq1.txt"))"; f=$((f+1)); }
    : > "$T/qcalls"; printf '0\n' > "$T/claims"
    printf '%s\n' "HELD: 2026-11-27 12:00:00 engineperf 30 EXCLUSIVE: another EnginePerf job" "HELD: 2026-11-27 12:00:00 engineperf 30 EXCLUSIVE: another EnginePerf job" \
      "HELD: 2026-11-27 12:01:00 engineperf 5 EXCLUSIVE handoff pre-claim (claim within 5 min)" > "$T/checks"
    ( acquire; echo "CLAIMED=$CLAIMED" ) > "$D/acq2.txt" 2>&1
    [ "$(tail -1 "$D/acq2.txt")" = "CLAIMED=1" ] && [ "$(grep -c '^claim ' "$T/qcalls")" = 1 ] && [ "$(grep -c '^check' "$T/qcalls")" = 3 ] || \
      { echo "acquire: a live claim under our name did not make it wait, or the handoff pre-claim was not claimed over ($(tr '\n' ';' < "$T/qcalls"))"; f=$((f+1)); }
    exit $f )
}
ndrv=0
[ "$RMAX_D" = 6 ] || { fail=$((fail+1)); say "FAIL driver R_MAX is '$RMAX_D', registered 6"; }
[ "$AXR_SHA_D" = 27d54b9f59404d224258649598ca604ebfc174dd066be4a96ff392323244fb29 ] && \
  [ "$(sha256sum < "$DT/axr_src/axr_options_play.ltx" | cut -c1-64)" = "$AXR_SHA_D" ] || { fail=$((fail+1)); say "FAIL the driver's AXR_SHA ('$AXR_SHA_D') is not the ordered hash of run/appdata/axr_options_play.ltx"; }
# the restore on the REAL source bytes (a copy in the stamp folder) with the driver's registered hash, into a scratch file
( . "$DT/funcs.sh"; printf 'version = 20261122\n' > "$DT/axr_src/dst.ltx"; axr_restore "$DT/axr_src/axr_options_play.ltx" "$DT/axr_src/dst.ltx" "$AXR_SHA_D" ) > "$DT/axr_real.txt" 2>&1
if [ $? = 0 ] && cmp -s "$DT/axr_src/axr_options_play.ltx" "$DT/axr_src/dst.ltx"; then pass=$((pass+1)); ndrv=$((ndrv+1)); say "ok   options restore on the real source bytes with the registered hash: $(cut -c1-60 "$DT/axr_real.txt")..."
else fail=$((fail+1)); say "FAIL options restore on the real source bytes: $(cat "$DT/axr_real.txt")"; fi
if r=$(drv_tests "$DT/funcs.sh" "$DT/real" "${RMAX_D:-6}"); then pass=$((pass+1)); ndrv=$((ndrv+1)); say "ok   driver functions offline (order shape + copy, options restore on a scratch layout: pass / source hash / missing / copy / altered, slice, verdict_of + cond_of on real files; series_loop: clean (restore before every boot), probe gate, probe not clean, re-run, twice, six re-runs, check, crash, settle, settle before re-run, cfg, claim renewal, cancel, restore refused; stop record; acquire by exit code with decoy text, handoff pre-claim)"
else fail=$((fail+1)); say "FAIL driver functions: $(echo "$r" | tr '\n' ';')"; fi
while IFS='|' read -r name expr; do
  [ -n "$name" ] || continue
  sed "$expr" "$DT/funcs.sh" > "$DT/funcs_$name.sh"
  if cmp -s "$DT/funcs.sh" "$DT/funcs_$name.sh"; then fail=$((fail+1)); say "FAIL driver mutant $name: the sed did not change the functions"; continue; fi
  if r=$(drv_tests "$DT/funcs_$name.sh" "$DT/m_$name" 6 2>"$DT/stderr_$name.txt"); then fail=$((fail+1)); say "FAIL driver mutant $name survived"
  else pass=$((pass+1)); ndrv=$((ndrv+1)); say "ok   driver mutant $name caught by: $(echo "$r" | tr '\n' ';' | cut -c1-160)"; fi
done <<'MUTEOF'
no_restore|/# AXR-GATE$/d
restore_no_src_hash|/want \$want (destination untouched)"; return 1; }$/d
restore_no_dst_hash|/after the copy is/d
restore_no_src_check|/source \$src missing"; return 1; }$/d
claim_parses_text|s|"\$T/quiet-required.sh" claim \$W 30 "\$WHAT" >/dev/null 2>&1; then|"$T/quiet-required.sh" claim $W 30 "$WHAT" 2>\&1 \| grep -q '^claimed'; then|
no_w_holds|s/! caden_lock && ! w_holds &&/! caden_lock \&\&/
handoff_not_excluded|s/!index(\$0, "handoff pre-claim")/1/
no_probe_gate|/# PROBE-GATE$/d
rerun_probe|/# RERUN-RULE probe: never re-run$/d
no_rerun|/# RERUN-RULE rerun$/d
third_attempt|/# RERUN-RULE one$/d
rerun_after_failed_check|/halt fault "load \$t failed a check/d
rerun_after_crash|/halt fault "load \$t ended rc=/d
settle_as_fault|s/halt box "not quiet/halt fault "not quiet/
claim_as_fault|s/halt box "claim renewal/halt fault "claim renewal/
no_cr_strip|s/ | tr -d '\\r'//
no_recheck|/slice_ok \$SL_END \$((BENCH_TO + END_RESERVE))/d
no_stop_write|s|> "\$ST/stop.txt"|> /dev/null|
no_live|/# LIVE-BENCH set$/d
no_cond_log|/# COND-LOG$/d
order_no_probe_line|/the first load of \$o is not the registered Cordon probe line/d
order_no_ros_probe_line|/the second load of \$o is not the registered Rostok probe line/d
order_old_warm|/scored\$")" = 3/s/bench1|14000|/bench1|[0-9]*|/
one_probe_enough|s/\[ "\$PROBED" = "\$NPROBE" \] || { halt fault "scored load/[ "$PROBED" -ge 1 ] || { halt fault "scored load/
MUTEOF
if r=$(drv_tests "$DT/funcs.sh" "$DT/rmax_5" 5); then fail=$((fail+1)); say "FAIL driver constant mutant R_MAX=5 survived"
else pass=$((pass+1)); ndrv=$((ndrv+1)); say "ok   driver constant mutant R_MAX=5 caught by: $(echo "$r" | tr '\n' ';' | cut -c1-160)"; fi
# the main body (not in the function block): static checks
if awk '/^order_copy "\$ORDER" "\$ORDER_SHA" "\$ST"/{c=NR} /^ORDER=\$ST\/sf1_order.txt$/{if (c && NR<=c+2) ok=1} END{exit !ok}' "$DRV" && \
   awk '/^trap finish EXIT; trap .cancel; finish. TERM INT$/{t=NR} /^acquire$/{a=NR} END{exit !(t && a && t < a)}' "$DRV" && \
   awk '/^finish\(\) \{/{f=NR} f && NR>f && /record_stop/ && !s{s=NR} f && NR>f && /quiet-required.sh" withdraw/ && !w{w=NR} END{exit !(s && w && s < w)}' "$DRV" && \
   [ "$(tail -2 "$DRV" | tr '\n' ' ')" = "series_loop finish " ] && grep -q 'for f in \$OUT/\$t \$OUT/\${t}_r1; do' "$DRV" && \
   grep -q 'tools/s2_bench.sh "\$2" "\$1" "\$4" "\$5" \$FLAGS -perf_slow "\$6" >' "$DRV" && grep -q 'BENCH_FIXED=0 BENCH_TIMEOUT=\$BENCH_TO' "$DRV" && \
   grep -q '^pin \$AXR_SRC  *\$AXR_SHA || bad=1$' "$DRV" && ! grep -q "grep -q '^claimed'" "$DRV" && \
   [ "$(grep -c 'quiet-required.sh" claim [^|]*>/dev/null 2>&1' "$DRV")" = 2 ] && ! grep -q 'quiet-required.sh" claim.*2>&1 *|' "$DRV" && \
   grep -q '^AXR_SRC=run/appdata/axr_options_play.ltx; AXR_DST=run/gamedata/configs/axr_options.ltx$' "$DRV"; then
  pass=$((pass+1)); ndrv=$((ndrv+1)); say "ok   driver body: order copy used, trap before acquire, stop record first in finish, series_loop + finish, _r1 preflight, real-time bench call, options source pinned, both claim calls exit-code only, real restore paths"
else fail=$((fail+1)); say "FAIL driver body: one of the static checks failed"; fi

# ---- the real fixture folders must be untouched: nothing in them newer than the start of this self-test ----
touched=0
for t in r1a_cor_k0_1 r1a_cor_k0_2 r1a_cor_k0_3 r1a_ros_k0_1 r1a_ros_k0_2 r1a_ros_k0_3 cord1 cord2 cp1 s2_off_1 s2_off_2 s4c_noff_1 s4c_poff_1 \
         s4c_poff_1_rr s2_c30 s2_c60 s2_c50 s4c_n30_1 s4c_n60_1 s4c_p30_1 hk2 hk3 base1 r1_cor_k3 r1_cor_k2 s4a_noff_1 wu3_09_on wu3_02_off wu3_01_on; do
  if [ -d "$REAL/$t" ] && n=$(find "$REAL/$t" -newer "$ST/.t0" -print 2>/dev/null | wc -l); then touched=$((touched + n))
  else touched=$((touched + 1)); say "FAIL fixture folder $REAL/$t could not be checked"; fi
done
[ "$(find "$P/run/appdata/axr_options_play.ltx" "$P/run/gamedata/configs/axr_options.ltx" -newer "$ST/.t0" 2>/dev/null | wc -l)" = 0 ] || touched=$((touched + 1))
if [ "$touched" != 0 ]; then fail=$((fail+1)); say "FAIL real fixture folders / options files changed during the self-test ($touched paths)"
else pass=$((pass+1)); say "ok   real fixture folders and the real options files unchanged"; fi
tot=$((pass+fail))
if [ $fail = 0 ]; then say "SF1 SELFTEST PASS $pass/$tot ($nreal real-folder scorer cases, $nmade made scorer cases, $ngate gate/check cases, $nmut checker mutants caught, $ndrv driver checks incl. mutants, 1 untouched check) in $ST"; exit 0
else say "SF1 SELFTEST FAIL $fail of $tot in $ST"; exit 1; fi
