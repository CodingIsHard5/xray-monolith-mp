#!/usr/bin/env bash
# sf1 scorer self-test (PREREG-sf1-slow-frame-classes-DRAFT.md section 8).
#   bash sf1_selftest.sh            -> builds sf1/selftest-runs/<stamp>/, runs every case, prints one final line
# Real fixtures: earlier run folders under <project>/out, referenced IN PLACE, read-only (never copied, never written).
# Made fixtures: real frame files and real perf_sites files of earlier runs, re-written into the new stamp folder, with
# slow-frame rows made to a plan (one mutant per class rule and per check / no-verdict rule).
# Nothing is ever deleted. The stamp folder must be new. Output also saved as <stamp>/selftest_output.txt.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
P="$(cd "$HERE/../../.." && pwd)"           # project root (wt-render-split/perf-harness/sf1 -> project)
REAL="$P/out"
SC="$HERE/sf1_classify.py"
[ -s "$SC" ] || { echo "SF1 SELFTEST FAIL: scorer $SC missing"; exit 2; }
[ -d "$REAL" ] || { echo "SF1 SELFTEST FAIL: real run root $REAL missing"; exit 2; }
ST="$HERE/selftest-runs/$(date +%Y%m%d-%H%M%S)-$$"
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

def make_run(root, tag, src, T, plan, scale=1.0, crlf=False, verdict="VERDICT: clean", skip=(), empty=(),
             sites_add=(), seqframe_delta=0, header_T=None, drop_last=False, malformed=False, norows_from=None,
             train=(), freeze=False):
    d = os.path.join(root, tag); os.makedirs(d)
    fr = [round(v * scale, 4) for v in real_frames(src)]
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
        if not v > T or (norows_from is not None and i >= norows_from):
            continue
        R = background()
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
        # the profiler keeps the 14 largest self times; one row per (site, thread)
        merged = {}
        for r in R:
            key = (r[3], r[0])
            if key in merged and merged[key][1] >= r[1]:
                continue
            merged[key] = r
        R = sorted(merged.values(), key=lambda r: -r[1])[:14]
        rows += ["%d,%.4f,%s,%.4f,%d,%s,%s,%s" % (i, v, r[0], r[1], r[2], r[3], r[4], r[5]) for r in R]
    if malformed:
        rows.append("%d,%.4f,main,1.0,1,broken_row_7_fields,x" % (0, fr[0]))
    nl = "\r\n" if crlf else "\n"
    def w(kind, name, lines):
        p = os.path.join(d, name)
        if kind in skip:
            return
        with open(p, "w", newline="") as f:
            if kind not in empty:
                f.write(nl.join(lines) + nl)
    w("frames", "perf_frames_%s.csv" % tag, ["frame,ms"] + ["%d,%.4f" % (i, v) for i, v in enumerate(fr[:-1] if drop_last else fr)])
    SL2 = list(SL)
    if seqframe_delta:
        for j, l in enumerate(SL2):
            a = l.split(",")
            if a[0] == "Process seqFrame":
                a[3] = str(int(a[3]) + seqframe_delta); SL2[j] = ",".join(a)
    SL2 += list(sites_add)
    w("sites", "perf_sites_%s.csv" % tag, SL2)
    w("slow", "perf_slow_%s.csv" % tag, ["#threshold_ms,%.3f" % (header_T if header_T is not None else T),
                                         "frame,frame_ms,thread,self_ms,calls,name,file,line"] + rows)
    w("cond", "conditions.txt", ["start (selftest fixture made from real run %s)" % src, verdict])

COR = ["r1_cor_k3", "r1a_cor_k0_3", "r1_cor_k2"]      # Coarse14, cordon1, no after-load train in the capture
ROS = ["s2_off_1", "s4c_noff_1", "s4a_noff_1"]        # stock loop (sim_tick 0), bench1, real time, 12000 frames
TC, TR = 6.0, 9.0
def good_cor(k): return ("alife", 0.8) if k % 10 < 7 else (("render", 0.8) if k % 10 < 9 else ("main_unzoned_or_wait",))
def good_ros(k):
    m = k % 20
    return ("render", 0.8) if m < 9 else (("gpu_wait", 0.8) if m < 15 else ("main_unzoned_or_wait",))

def case(name, expect_exit, pats, cor=None, ros=None, order_extra=None, pre_out=False, real=None):
    """cor/ros: list of 3 dicts of make_run overrides (None = scene absent). real: list of order lines (root = REAL)."""
    cd = os.path.join(ST, "cases", name); os.makedirs(cd)
    lines = []
    if real is not None:
        root = REAL; lines = real
    else:
        root = os.path.join(cd, "runs"); os.makedirs(root)
        for scene, srcs, T, plan0, save, pre in (("cor", COR, TC, good_cor, "cordon1", cor), ("ros", ROS, TR, good_ros, "bench1", ros)):
            if pre is None:
                continue
            for j in range(3):
                o = dict(pre[j]); tag = o.pop("tag", "sf1_%s_%d" % (scene, j + 1))
                if o.pop("absent", False):
                    lines.append("%s|Coarse14.exe|%s|10500|%d|%.1f" % (tag, save, len(real_frames(srcs[j])), T)); continue
                plan = o.pop("plan", plan0); rr = o.pop("rr", None)
                make_run(root, tag, srcs[j], T, plan, **o)
                if rr is not None:
                    make_run(root, tag + "_rr", srcs[j], T, plan, verdict=rr)
                lines.append("%s|Coarse14.exe|%s|10500|%d|%.1f" % (tag, save, len(real_frames(srcs[j])), T))
    if order_extra:
        lines = order_extra(lines)
    with open(os.path.join(cd, "sf1_order.txt"), "w") as f:
        f.write("# selftest case %s\n" % name + "\n".join(lines) + "\n")
    if pre_out:
        with open(os.path.join(cd, "score.txt"), "w") as f:
            f.write("an earlier score that must not be overwritten\n")
    CASES.append("\t".join([name, root, os.path.join(cd, "sf1_order.txt"), os.path.join(cd, "score.txt"), str(expect_exit)] + pats))

G3 = [{}, {}, {}]
# ---------- real run folders, read in place ----------
case("real_k0_coarse14", 0, [r"RUN r1a_cor_k0_1 used=r1a_cor_k0_1 VOID \[coverage_p99\] \[train\]$",
                             r"RUN r1a_cor_k0_2 used=r1a_cor_k0_2 VOID \[coverage_p99\]$",
                             r"RUN r1a_ros_k0_3 used=r1a_ros_k0_3 VOID \[coverage_p99\]$",
                             r"SCENE cordon NO VERDICT \[runs\]: 0 of 3", r"SCENE rostok NO VERDICT \[runs\]: 0 of 3"],
     real=["r1a_cor_k0_%d|Coarse14.exe|cordon1|600|3000|20" % i for i in (1, 2, 3)] +
          ["r1a_ros_k0_%d|Coarse14.exe|bench1|600|3000|20" % i for i in (1, 2, 3)])
case("real_trains_and_rr", 0, [r"RUN cord1 used=cord1 VOID \[not_clean\] \[coverage_p99\] \[train\]$",
                               r"RUN cp1 used=cp1 VOID \[not_clean\] \[coverage_p99\] \[train\]$",
                               r"RUN s4c_poff_1 used=s4c_poff_1_rr VOID \(substituted: s4c_poff_1 is 'VERDICT: CONFOUNDED",
                               r"RUN s2_off_1 used=s2_off_1 VOID \[coverage_p99\]$", r"SF1 RESULT: cordon=NO VERDICT \(runs 0/3 valid\)"],
     real=["cord1|RefDet12.exe|cordon1|600|3000|20", "cord2|RefDet12.exe|cordon1|600|3000|20", "cp1|RefDet13.exe|cordon1|600|3000|20",
           "s2_off_1|SimTick14.exe|bench1|3000|12000|20", "s4c_noff_1|SimTick17.exe|bench1|3000|12000|20",
           "s4c_poff_1|SimTick16.exe|bench1|3000|12000|20"])
case("real_capped", 0, [r"RUN s2_c30 used=s2_c30 VOID .*\[frame_limiter\]", r"RUN s2_c60 used=s2_c60 VOID .*\[frame_limiter\]",
                        r"frame_limiter: site 'Eco Render' has"],
     real=["s2_c30|SimTick14.exe|bench1|450|1800|20", "s2_c60|SimTick14.exe|bench1|900|3600|20", "s2_c50|SimTick14.exe|bench1|750|3000|20"])
case("real_simtick_on", 0, [r"RUN s4c_n30_1 used=s4c_n30_1 VOID .*\[stock_loop\]", r"stock_loop: site 'Sim tick' has",
                            r"RUN s4c_p30_1 used=s4c_p30_1 VOID .*\[stock_loop\]"],
     real=["s4c_n30_1|SimTick17.exe|bench1|3000|12000|20", "s4c_n60_1|SimTick17.exe|bench1|3000|12000|20",
           "s4c_p30_1|SimTick16.exe|bench1|3000|12000|20"])
case("real_registration_mismatch", 0, [r"RUN r1a_ros_k0_1 used=r1a_ros_k0_1 VOID \[framecount\]",
                                       r"RUN r1a_ros_k0_2 used=r1a_ros_k0_2 VOID \[threshold_header\]",
                                       r"threshold_header: slow file threshold 20.000, registered 9.000"],
     real=["r1a_ros_k0_1|Coarse14.exe|bench1|600|12000|20", "r1a_ros_k0_2|Coarse14.exe|bench1|600|3000|9",
           "r1a_ros_k0_3|Coarse14.exe|bench1|600|3000|20"])
case("real_missing_slow_file", 0, [r"RUN hk2 used=hk2 VOID \[missing\]$", r"MISSING .*/out/hk2/perf_slow_hk2\.csv",
                                   r"RUN hk3 used=hk3 VOID \[missing\]$", r"RUN base1 used=base1 VOID \[coverage_p99\]$"],
     real=["hk2|RefDet13.exe|bench1|600|3000|20", "hk3|RefDet13.exe|bench1|600|3000|20", "base1|RefDet12.exe|bench1|600|3000|20"])

# ---------- made from real frame and site files ----------
case("good", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VALID", r"RUN sf1_ros_3 used=sf1_ros_3 VALID",
                 r"SCENE cordon RESULT: DOMINANT alife", r"SCENE rostok RESULT: NO SINGLE DOMINANT CLASS; largest render",
                 r"^SF1 RESULT: cordon=DOMINANT alife 70%; rostok=MIXED largest render 4[45]%"], cor=G3, ros=G3)
case("good_crlf", 0, [r"SCENE cordon RESULT: DOMINANT alife", r"SCENE rostok RESULT: NO SINGLE DOMINANT CLASS; largest render"],
     cor=[{"crlf": True}] * 3, ros=[{"crlf": True}] * 3)
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
case("missing_folder", 0, [r"RUN sf1_cor_3 used=sf1_cor_3 VOID \[missing\]$", r"MISSING folder .*/sf1_cor_3$"],
     cor=[{}, {}, {"absent": True}], ros=G3)
case("empty_slow", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VOID \[empty\]$", r"EMPTY .*/perf_slow_sf1_cor_1\.csv"],
     cor=[{"empty": ("slow",)}, {}, {}], ros=G3)
case("malformed_row", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VOID \[malformed\]$"], cor=[{"malformed": True}, {}, {}], ros=G3)
case("framecount", 0, [r"RUN sf1_ros_1 used=sf1_ros_1 VOID \[framecount\]"], cor=G3, ros=[{"drop_last": True}, {}, {}])
case("threshold_header", 0, [r"RUN sf1_cor_2 used=sf1_cor_2 VOID \[threshold_header\]"], cor=[{}, {"header_T": 7.0}, {}], ros=G3)
case("simtick_on", 0, [r"RUN sf1_ros_2 used=sf1_ros_2 VOID \[stock_loop\]"], cor=G3,
     ros=[{}, {"sites_add": ("Sim tick,synthetic,1014,100,0.1000,0.1000,0,0.0000,0.0000",)}, {}])
case("seqframe_count", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VOID \[stock_loop\]", r"'Process seqFrame' main calls 2995 != frames 3000"],
     cor=[{"seqframe_delta": -5}, {}, {}], ros=G3)
case("frame_limiter", 0, [r"RUN sf1_cor_3 used=sf1_cor_3 VOID \[frame_limiter\]"],
     cor=[{}, {}, {"sites_add": ("Eco Render,synthetic,758,3000,9000.0,9000.0,0,0.0000,0.0000",)}], ros=G3)
case("not_clean_no_rr", 0, [r"RUN sf1_cor_1 used=sf1_cor_1 VOID \[not_clean\]$"],
     cor=[{"verdict": "VERDICT: CONFOUNDED (other heavy process present)"}, {}, {}], ros=G3)
case("not_clean_rr_clean", 0, [r"RUN sf1_cor_1 used=sf1_cor_1_rr VALID \(substituted: sf1_cor_1 is 'VERDICT: CONFOUNDED",
                               r"SCENE cordon RESULT: DOMINANT alife"],
     cor=[{"verdict": "VERDICT: CONFOUNDED (other heavy process present)", "rr": "VERDICT: clean"}, {}, {}], ros=G3)
case("not_clean_rr_not_clean", 0, [r"RUN sf1_cor_1 used=sf1_cor_1_rr VOID .*\[not_clean\]"],
     cor=[{"verdict": "VERDICT: CONFOUNDED (other heavy process present)", "rr": "VERDICT: CONFOUNDED (other heavy process present)"}, {}, {}], ros=G3)
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
case("out_exists", 2, [r"REFUSED: .*score\.txt exists, not overwriting"], cor=G3, ros=G3, pre_out=True)
case("uppercase_tag", 2, [r"REFUSED: tag SF1_cor_1 is not lowercase"], cor=G3, ros=G3,
     order_extra=lambda L: [L[0].replace("sf1_cor_1", "SF1_cor_1", 1)] + L[1:])
case("two_slots", 2, [r"REFUSED: scene cordon has not exactly 3 slots"], cor=G3, ros=G3, order_extra=lambda L: L[1:])
with open(os.path.join(ST, "cases.tsv"), "w") as f:
    f.write("\n".join(CASES) + "\n")
PYEOF
[ -s "$ST/cases.tsv" ] || { say "SF1 SELFTEST FAIL: fixture builder failed"; exit 1; }

pass=0; fail=0; nreal=0; nmade=0
while IFS=$'\t' read -r name root order out want rest; do
  [ -n "$name" ] || continue
  case "$name" in real_*) nreal=$((nreal+1));; *) nmade=$((nmade+1));; esac
  before=""; [ -e "$out" ] && before=$(sha256sum "$out" | cut -d' ' -f1)
  python3 -I "$SC" root="$root" order="$order" out="$out" > "$(dirname "$order")/stdout.txt" 2>&1; rc=$?
  ok=1; why=""
  [ "$rc" = "$want" ] || { ok=0; why="exit $rc, wanted $want"; }
  if [ -n "$before" ] && [ "$(sha256sum "$out" | cut -d' ' -f1)" != "$before" ]; then ok=0; why="$why; existing out file changed"; fi
  IFS=$'\t' read -r -a pats <<<"$rest"
  for pt in "${pats[@]}"; do
    cat "$(dirname "$order")/stdout.txt" "$out" 2>/dev/null | grep -Eq -- "$pt" || { ok=0; why="$why; no match: $pt"; }
  done
  if [ $ok = 1 ]; then pass=$((pass+1)); say "ok   $name"; else fail=$((fail+1)); say "FAIL $name: $why"; fi
done < "$ST/cases.tsv"
# the real fixture folders must be untouched: nothing in them newer than the start of this self-test
touched=$(grep -h -o '^[a-z0-9_]*|' "$ST"/cases/real_*/sf1_order.txt | tr -d '|' | sort -u | while read -r t; do [ -d "$REAL/$t" ] && find "$REAL/$t" -newer "$ST/.t0" -print; [ -d "$REAL/${t}_rr" ] && find "$REAL/${t}_rr" -newer "$ST/.t0" -print; done | wc -l)
if [ "$touched" != 0 ]; then fail=$((fail+1)); say "FAIL real fixture folders changed during the self-test ($touched paths)"; else pass=$((pass+1)); say "ok   real fixture folders unchanged"; fi
tot=$((pass+fail))
if [ $fail = 0 ]; then say "SF1 SELFTEST PASS $pass/$tot ($nreal real-folder cases, $nmade made cases, 1 untouched check) in $ST"; exit 0
else say "SF1 SELFTEST FAIL $fail of $tot in $ST"; exit 1; fi
