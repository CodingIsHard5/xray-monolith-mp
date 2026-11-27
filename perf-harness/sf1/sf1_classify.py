#!/usr/bin/env python3
# sf1 slow-frame classifier (PREREG-sf1-slow-frame-classes-DRAFT.md). Stdlib only; run with python3 -I.
#
#   python3 -I sf1_classify.py root=<folder holding the run folders> order=<order file> out=<new output file>
#   python3 -I sf1_classify.py probe=<probe run folder> order=<order file>   (driver: the PROBE GATE, before any scored run)
#   python3 -I sf1_classify.py check=<scored run folder> order=<order file>  (driver: after every scored run and re-run)
#
# Order file: "tag|exe|save|warm|frames|slow_ms|arm" per line, '#' = comment. arm: probe (one per scene, all probe lines
# before every scored line, never scored) or scored. save cordon1 -> scene cordon, bench1 -> rostok. Each scene must
# have exactly 3 scored slots and exactly one probe.
# Score mode also reads the driver's stop record <folder of order=>/stop.txt: missing, not exactly one physical line, or
# a class other than done -> NO VERDICT [stop] for every scene. A scene whose own probe fails -> NO VERDICT [probe].
# Tags must be all-lowercase and must not end in _r<digits> (that is the re-run folder name).
# A scored slot whose conditions.txt VERDICT is not "clean" is read from <tag>_r1 (the driver's one re-run, the wu3 rule);
# the substitution is named. <tag>_r1 next to a clean <tag> voids the slot (unexpected_rerun).
# Run folder <root>/<tag>/: perf_frames_<tag>.csv, perf_sites_<tag>.csv, perf_slow_<tag>.csv, conditions.txt, xray.log.
# A missing or empty file fails BY NAME and voids that slot; it is never read as 0.
# The output is written to out= in this same call; an existing out file is refused (exit 2), never overwritten.
# Exit (score): 0 = output written (whatever the verdict), 2 = refused (usage, order file, out exists).
# Exit (probe= / check=): 0 = go on, 1 = stop (every failed rule printed as "PROBE STOP <code>: ..." / "CHECK STOP ..."),
#   2 = refused (usage, order file, folder is not the probe / a scored slot or its _r1).
# Every rule and constant below is the registered one (prereg sections 4-6); nothing is read from the environment.
import hashlib
import math
import os
import re
import sys

# ---- registered constants (prereg section 5 and 10; derivations there) ----
THETA = 0.5             # a class explains a frame when its extra self time >= THETA * (frame ms - window p50)
UNEXPLAINED_MAX = 0.5   # pooled share of unexplained slow frames above this -> scene NO VERDICT (zones too coarse)
P99_SPREAD = 0.12       # each run's p99 within +-12 % of the scene's median-run p99, else NO VERDICT (not repeatable)
SHARE_Z = 3.0           # per-run class share within SHARE_Z binomial SE (+1/n) of the pooled share
SHARE_CHECK_MIN = 0.10  # the share repeatability check covers classes with pooled share >= 10 %
DOMINANT = 0.5          # "dominant" = pooled share minus 2 SE above one half of ALL slow frames
COVER_TOL = 0.02        # engine's slow test uses a 10 ms clock calibration; frames within 2 % above T may lack rows
MIN_WINDOW = 0.5        # analysis window (cut at the first uncovered frame) must keep >= half the frames
TRAIN_PASS_MS = 15.0    # a frame whose ALife/* self time sums to >= 15 ms is a load-train pass (INFORMATION.md cp1-4)
TRAIN_GAP_MS = 1000.0   # 3 such frames with gaps <= 1 s of wall time = the after-load train is in the window
TRAIN_MIN = 3
HITCH_MS = 100.0        # reported only
ROW_MS_TOL = 0.0005     # a slow row's frame_ms equals the frames file's ms (both printed to 4 decimals; 0 seen on disk)
# probe gate: median captured frame within the scene's plausible real-time range (prereg 6.1, one derivation per scene)
PROBE_P50 = {"cordon1": (1.5, 8.0), "bench1": (2.0, 10.0)}
STOP_RE = r"class=(done|fault) reason=.*|class=box( interrupted=[a-z0-9_]+)? reason=.*"   # stop record (prereg 6.4)
RUNS_PER_SCENE = 3
SCENES = {"cordon1": "cordon", "bench1": "rostok"}
ARMS = ("probe", "scored")
EXCLUDED = {"mt_FreezeThread", "Check timer"}   # freeze watchdog: a 500 ms Sleep loop, its "self" is sleep
EXPLAINED = ["alife", "script", "render", "gpu_wait", "online_sim", "io_stream", "sound", "other_known"]
UNEXPLAINED = ["gc", "main_unzoned_or_wait", "unknown", "no_data"]
CLASSES = EXPLAINED + UNEXPLAINED
# probe gate: the named sites the rules read, and every class of the map, must appear with calls in perf_sites
REQUIRED_SITES = ("Process seqFrame", "bench: wait for GPU (no present)", "seqLuaGC")
REQUIRED_CLASSES = ("alife", "gc", "gpu_wait", "script", "io_stream", "sound", "render", "online_sim")


# ---- class map (prereg section 4.2): first matching rule wins; unmatched named zones are other_known ----
def zone_class(name):
    n = name
    if n in EXCLUDED:
        return None
    if n.startswith("ALife/"):
        return "alife"
    if n == "seqLuaGC":
        return "gc"
    if n.startswith("bench: wait for GPU"):
        return "gpu_wait"
    if n in ("CScriptBinder::shedule_Update", "CScriptZone::shedule_Update") or "script" in n.lower() or "lua" in n.lower():
        return "script"
    if (n.startswith(("Sound: Load", "r_open_impl", "CInifile::", "CTexture::Load", "ProcessPrefetchEvents",
                      "ProcessSpawnEvents", "ProcessGameEvents", "Loading...", "LoadDraw"))):
        return "io_stream"
    if n.startswith(("CSoundRender", "SoundEvent", "Sound")):
        return "sound"
    if (n.startswith(("r_dsgraph", "R_constant", "R_sun", "Render", "phase_", "CDetailManager", "MT_CALC details",
                      "RCache", "CBackend::", "set_Textures", "SEND_MATRICES", "FTreeVisual", "Fvisual", "CSkeletonX",
                      "EnvironmentRender", "LODS", "IGame_Level::OnRender", "Process seqRender", "seqParallelRender",
                      "CUIGameCustom::Render", "ISpatial_DB::q_frustum", "Particle", "Process Particles",
                      "CEffect_Rain", "Draw statistics", "Render: ", "r_"))):
        return "render"
    if (n.startswith(("stalker/", "Level_Graph", "COLLIDER", "xrCDB", "ISpatial_DB::q_ray", "ISpatial_DB::q_box",
                      "Build Path", "Memory Manager", "CMemoryManager", "CEnemyManager", "DangerManager",
                      "feel_vision", "CPH", "Collide", "CCF_", "graph_engine", "Covers", "Restricted Object",
                      "CustomMonster", "CBaseMonster", "CCustomZone", "CArtefact", "CEntityAlive", "CGameObject",
                      "CSheduler", "Sheduler", "net_Update", "CObjectList", "CActor", "Object Handler", "IK_"))):
        return "online_sim"
    return "other_known"


class Bad(Exception):
    pass


class Refused(Exception):
    pass


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


def lines_of(path):
    """File lines with CR stripped (engine output may be CRLF). Missing/empty fail by name."""
    if not os.path.exists(path):
        raise Bad("missing: MISSING %s" % path)
    if os.path.getsize(path) == 0:
        raise Bad("empty: EMPTY %s" % path)
    with open(path, "rb") as f:
        raw = f.read().decode("ascii", "replace")
    return [l.rstrip("\r") for l in raw.split("\n") if l.rstrip("\r") != ""]


def pct(sorted_v, p):
    """Linear interpolation between closest ranks (numpy's default)."""
    k = (len(sorted_v) - 1) * p
    i = int(math.floor(k))
    j = min(i + 1, len(sorted_v) - 1)
    return sorted_v[i] + (sorted_v[j] - sorted_v[i]) * (k - i)


def read_frames(d, tag):
    p = os.path.join(d, "perf_frames_%s.csv" % tag)
    L = lines_of(p)
    if L[0] != "frame,ms":
        raise Bad("malformed: %s header %r" % (p, L[0]))
    ms = []
    for k, l in enumerate(L[1:]):
        a = l.split(",")
        if len(a) != 2 or a[0] != str(k):
            raise Bad("malformed: %s row %d %r" % (p, k + 1, l))
        ms.append(float(a[1]))
    if not ms:
        raise Bad("empty: EMPTY rows %s" % p)
    return ms


def read_sites(d, tag):
    p = os.path.join(d, "perf_sites_%s.csv" % tag)
    L = lines_of(p)
    if len(L) < 3 or not L[0].startswith("#frames,") or not L[1].startswith("#total_ms,"):
        raise Bad("malformed: %s header" % p)
    nfr = int(L[0].split(",")[1])
    if L[2] != "name,file,line,calls_main,incl_main_ms,self_main_ms,calls_other,incl_other_ms,self_other_ms":
        raise Bad("malformed: %s column header" % p)
    sites = {}
    for l in L[3:]:
        a = l.split(",")
        if len(a) != 9:
            raise Bad("malformed: %s row %r" % (p, l))
        key = (a[0], a[1], a[2])
        sites[key] = dict(name=a[0], cm=int(a[3]), sm=float(a[5]), co=int(a[6]), so=float(a[8]))
    return nfr, sites


def read_slow(d, tag):
    p = os.path.join(d, "perf_slow_%s.csv" % tag)
    L = lines_of(p)
    if not L[0].startswith("#threshold_ms,"):
        raise Bad("malformed: %s header" % p)
    thr = float(L[0].split(",")[1])
    if len(L) < 2 or L[1] != "frame,frame_ms,thread,self_ms,calls,name,file,line":
        raise Bad("malformed: %s column header" % p)
    rows = {}
    for l in L[2:]:
        a = l.split(",")
        if len(a) != 8 or a[2] not in ("main", "other"):
            raise Bad("malformed: %s row %r" % (p, l))
        rows.setdefault(int(a[0]), []).append(dict(fms=float(a[1]), thread=a[2], self=float(a[3]),
                                                   calls=int(a[4]), name=a[5], key=(a[5], a[6], a[7])))
    return thr, rows


def verdict_of(d):
    p = os.path.join(d, "conditions.txt")
    for l in lines_of(p):
        if l.startswith("VERDICT"):
            return l.strip()
    raise Bad("malformed: %s has no VERDICT line" % p)


def heavy_of(d):
    """Reported only (fleet desk rule): the bench's start / end heavy_others fields of conditions.txt."""
    out = []
    try:
        for l in lines_of(os.path.join(d, "conditions.txt")):
            m = re.match(r"^(start|end)\b.*?(heavy_others=\[[^\]]*\])", l)
            if m:
                out.append("%s %s" % (m.group(1), m.group(2)))
    except Bad:
        return "conditions unreadable"
    return "; ".join(out) if out else "no heavy_others fields"


def cmdline_reasons(log, tag, slot):
    """The engine's own record of how it was started (xray.log 'command line'): exe, save, capture, real time."""
    cl = [l for l in log if l.startswith("command line ")]
    if not cl:
        return ["cmdline: no 'command line' line in xray.log"]
    toks = cl[0][len("command line "):].split()
    bad = []
    if not toks or not toks[0].lower().endswith("\\" + slot["exe"].lower()):
        bad.append("cmdline: exe is %r, registered %s" % (toks[0] if toks else "", slot["exe"]))
    def after(flag):
        return toks[toks.index(flag) + 1] if flag in toks and toks.index(flag) + 1 < len(toks) else None
    cap = "%d,%d,%s" % (slot["warm"], slot["frames"], tag)
    if after("-perf_capture") != cap:
        bad.append("cmdline: -perf_capture %r, registered %r" % (after("-perf_capture"), cap))
    s = after("-perf_slow")
    try:
        sv = float(s)
    except (TypeError, ValueError):
        sv = None
    if sv is None or abs(sv - slot["slow"]) > 0.0005:
        bad.append("cmdline: -perf_slow %r, registered %.1f" % (s, slot["slow"]))
    for f in ("-perf_noinput", "-perf_nopresent", "-perf_quit", "-nointro"):
        if f not in toks:
            bad.append("cmdline: %s missing" % f)
    srv = "server(%s/single/alife/load)" % slot["save"]
    if srv not in toks:
        bad.append("cmdline: %s missing" % srv)
    if "-perf_fixed_dt" in toks:
        bad.append("cmdline: -perf_fixed_dt present (fixed game step, not real time)")
    return bad


def run_rules(d, tag, slot):
    """The as-registered rules of one load (prereg 6 R0-R4, R8, R9). Returns (data or None, reasons)."""
    if not os.path.isdir(d):
        return None, ["missing: MISSING folder %s" % d]
    reasons = []
    try:
        ms = read_frames(d, tag)
        nfr, sites = read_sites(d, tag)
        thr, rows = read_slow(d, tag)
        lines_of(os.path.join(d, "conditions.txt"))
        log = lines_of(os.path.join(d, "xray.log"))
    except Bad as e:
        return None, [str(e)]
    N = len(ms)
    if N != slot["frames"] or nfr != slot["frames"]:
        reasons.append("framecount: frames file %d rows, sites header %d, registered %d" % (N, nfr, slot["frames"]))
    if abs(thr - slot["slow"]) > 0.0005:
        reasons.append("threshold_header: slow file threshold %.3f, registered %.3f" % (thr, slot["slow"]))
    bad_idx = sorted(f for f in rows if f < 0 or f >= N)
    if bad_idx:
        reasons.append("malformed: slow rows for frame(s) outside the capture, e.g. %d" % bad_idx[0])
    else:
        off = [(f, r["fms"]) for f in sorted(rows) for r in rows[f]
               if abs(r["fms"] - ms[f]) > ROW_MS_TOL or r["fms"] < thr * (1 - COVER_TOL)]
        if off:
            reasons.append("slow_rows: frame %d row frame_ms %.4f, frames file %.4f, threshold %.3f (%d rows off)"
                           % (off[0][0], off[0][1], ms[off[0][0]], thr, len(off)))
    seq = [s for s in sites.values() if s["name"] == "Process seqFrame"]
    if not seq:
        reasons.append("stock_loop: site 'Process seqFrame' missing from perf_sites")
    elif seq[0]["cm"] != N:
        reasons.append("stock_loop: 'Process seqFrame' main calls %d != frames %d" % (seq[0]["cm"], N))
    st = sum(s["cm"] + s["co"] for s in sites.values() if s["name"] == "Sim tick")
    if st:
        reasons.append("stock_loop: site 'Sim tick' has %d calls (fixed tick on)" % st)
    eco = sum(s["cm"] + s["co"] for s in sites.values() if s["name"] == "Eco Render")
    if eco:
        reasons.append("frame_limiter: site 'Eco Render' has %d calls (capped run)" % eco)
    reasons += cmdline_reasons(log, tag, slot)
    fat = [k for k, l in enumerate(log) if "FATAL" in l]
    if fat:
        reasons.append("fatal: xray.log line %d contains FATAL: %r" % (fat[0] + 1, log[fat[0]][:120]))
    return dict(ms=ms, nfr=nfr, sites=sites, thr=thr, rows=rows, N=N), reasons


def window_rules(D, R):
    """Prereg 4.1 and R6 / R7: the analysis window, its p50 / p99, coverage and the after-load train."""
    ms, rows, thr, N = D["ms"], D["rows"], D["thr"], D["N"]
    cut = N
    for i, v in enumerate(ms):
        if v > thr * (1 + COVER_TOL) and i not in rows:
            cut = i
            break
    R["cut"] = cut
    if cut < N:
        R["notes"].append("window cut at frame %d of %d (first frame above T without rows)" % (cut, N))
    if cut < MIN_WINDOW * N:
        R["reasons"].append("window_short: window %d frames < %.0f %% of %d" % (cut, MIN_WINDOW * 100, N))
        return None
    w = ms[:cut]
    sw = sorted(w)
    p50, p99 = pct(sw, 0.5), pct(sw, 0.99)
    R.update(p50=p50, p99=p99, mean=sum(ms) / N, n_window=cut, max=max(w))
    if p99 < thr * (1 + COVER_TOL):
        R["reasons"].append("coverage_p99: threshold %.3f ms above window p99 %.3f ms (frames between have no zone rows)"
                            % (thr, p99))
    t = 0.0
    cand = []
    for i, v in enumerate(w):
        if i in rows:
            a = sum(r["self"] for r in rows[i] if r["name"].startswith("ALife/"))
            if a >= TRAIN_PASS_MS:
                cand.append((i, t))
        t += v
    run_len, worst = 1 if cand else 0, 1 if cand else 0
    for k in range(1, len(cand)):
        run_len = run_len + 1 if cand[k][1] - cand[k - 1][1] <= TRAIN_GAP_MS else 1
        worst = max(worst, run_len)
    if worst >= TRAIN_MIN:
        R["reasons"].append("train: %d A-Life passes >= %.0f ms within <= %.0f ms gaps (first at frame %d)"
                            % (worst, TRAIN_PASS_MS, TRAIN_GAP_MS, cand[0][0]))
    R["hitches"] = sum(1 for v in w if v > HITCH_MS)
    return w


def probe_rules(d, tag, slot):
    """The PROBE GATE (prereg 6.1): every as-registered rule, the window rules, and what the classifier relies on."""
    R = dict(tag=tag, reasons=[], notes=[])
    D, R["reasons"] = run_rules(d, tag, slot)
    if D is None:
        return R
    if not D["rows"]:
        R["reasons"].append("slow_rows: perf_slow has no rows (no frame above the threshold was dumped)")
    have = {}
    named = set()
    for s in D["sites"].values():
        if s["cm"] + s["co"] > 0:
            named.add(s["name"])
            c = zone_class(s["name"])
            have[c] = have.get(c, 0) + 1
    miss = [n for n in REQUIRED_SITES if n not in named] + ["class " + c for c in REQUIRED_CLASSES if not have.get(c)]
    if miss:
        R["reasons"].append("zones: not in perf_sites with calls: %s" % ", ".join(miss))
    p50_all = pct(sorted(D["ms"]), 0.5)
    R["p50_all"] = p50_all
    lo, hi = PROBE_P50[slot["save"]]
    if not (lo <= p50_all <= hi):
        R["reasons"].append("frame_range: median frame %.3f ms outside the plausible real-time range [%.1f, %.1f] ms of %s"
                            % (p50_all, lo, hi, slot["save"]))
    window_rules(D, R)
    return R


def score_run(root, tag, slot):
    """Returns dict with valid flag, reasons list, and the per-frame results."""
    R = dict(tag=tag, reasons=[], notes=[], frames=[])
    D, R["reasons"] = run_rules(os.path.join(root, tag), tag, slot)
    if D is None:
        return R
    w = window_rules(D, R)
    if w is None or R["reasons"]:
        return R
    rows, sites, nfr = D["rows"], D["sites"], D["nfr"]
    p50, p99 = R["p50"], R["p99"]
    t = 0.0
    for i, v in enumerate(w):
        if v > p99:
            if i in rows:
                c, sc, mainshare, top = classify_frame(v, p50, R["mean"], rows[i], sites, nfr)
            else:
                c, sc, mainshare, top = "no_data", 0.0, 0.0, []
            R["frames"].append(dict(i=i, t=t, ms=v, E=v - p50, cls=c, score=sc, main=mainshare, top=top))
        t += v
    R["valid"] = True
    return R


def classify_frame(ms, p50, mean_frame, rows, sites, nfr):
    """Prereg 4.3. Returns (class, winner score, main share of winner, top names)."""
    E = ms - p50
    score = {c: 0.0 for c in CLASSES}
    main_part = {c: 0.0 for c in CLASSES}
    names = {c: {} for c in CLASSES}
    main_delta_signed = 0.0
    for r in rows:
        c = zone_class(r["name"])
        if c is None:
            continue
        s = sites.get(r["key"])
        base = 0.0
        if s is not None:
            base = (s["sm"] if r["thread"] == "main" else s["so"]) / nfr
        d = r["self"] - base
        if r["thread"] == "main":
            main_delta_signed += d
        if d > 0:
            score[c] += d
            names[c][r["name"]] = names[c].get(r["name"], 0.0) + d
            if r["thread"] == "main":
                main_part[c] += d
    contest = [c for c in EXPLAINED + ["gc"]]
    win = max(contest, key=lambda c: (score[c], -contest.index(c)))
    if E > 0 and score[win] >= THETA * E:
        top = sorted(names[win].items(), key=lambda kv: -kv[1])[:2]
        return win, score[win], (main_part[win] / score[win] if score[win] else 0.0), top
    resid_delta = (ms - mean_frame) - main_delta_signed
    if E > 0 and resid_delta >= THETA * E:
        # reported only (not a rule): the largest positive deltas of any class in this frame
        allv = {}
        for c in CLASSES:
            for k, v in names[c].items():
                allv[k] = allv.get(k, 0.0) + v
        return "main_unzoned_or_wait", resid_delta, 1.0, sorted(allv.items(), key=lambda kv: -kv[1])[:2]
    return "unknown", score[win], 0.0, []


def read_order(path):
    """The order file's slots, the probe first. Raises Refused with the reason."""
    try:
        olines = [l for l in lines_of(path) if not l.lstrip().startswith("#")]
    except Bad as e:
        raise Refused("order file: %s" % e)
    slots = []
    for l in olines:
        a = l.split("|")
        if len(a) != 7:
            raise Refused("order line %r is not tag|exe|save|warm|frames|slow_ms|arm" % l)
        tag, exe, save, warm, frames, slow, arm = [x.strip() for x in a]
        if tag != tag.lower():
            raise Refused("tag %s is not lowercase" % tag)
        if re.search(r"_r[0-9]+$", tag):
            raise Refused("tag %s looks like a re-run folder name (<tag>_r<n>)" % tag)
        if save not in SCENES:
            raise Refused("save %s is not one of %s" % (save, sorted(SCENES)))
        if arm not in ARMS:
            raise Refused("arm %s of %s is not one of %s" % (arm, tag, "/".join(ARMS)))
        try:
            slots.append(dict(tag=tag, exe=exe, save=save, scene=SCENES[save], warm=int(warm), frames=int(frames),
                              slow=float(slow), arm=arm))
        except ValueError:
            raise Refused("order line %r has a non-numeric warm / frames / slow_ms" % l)
    if len({s["tag"] for s in slots}) != len(slots):
        raise Refused("a tag is listed twice")
    arms = [s["arm"] for s in slots]
    first_scored = arms.index("scored") if "scored" in arms else len(arms)
    if "probe" in arms[first_scored:]:
        raise Refused("every probe line must come before the first scored line")
    scored = [s for s in slots if s["arm"] == "scored"]
    if not scored:
        raise Refused("no scored slot")
    for sc in sorted({s["scene"] for s in slots}):
        if sum(1 for s in scored if s["scene"] == sc) != RUNS_PER_SCENE:
            raise Refused("scene %s has not exactly %d slots" % (sc, RUNS_PER_SCENE))
        if sum(1 for s in slots if s["arm"] == "probe" and s["scene"] == sc) != 1:
            raise Refused("scene %s has not exactly one probe line" % sc)
    return slots


def one_load(folder, order, probe):
    """probe= / check= for the driver: one load folder against its order line. 0 go on, 1 stop, 2 refused."""
    try:
        slots = read_order(order)
    except Refused as e:
        print("REFUSED: %s" % e)
        return 2
    tag = os.path.basename(os.path.normpath(folder))
    base = tag[:-3] if tag.endswith("_r1") else tag
    s = [x for x in slots if x["tag"] == base]
    if not s or (s[0]["arm"] == "probe") != probe or (probe and base != tag):
        print("REFUSED: %s is not %s in %s" % (tag, "the probe load" if probe else "a scored slot or its _r1", order))
        return 2
    word = "PROBE" if probe else "CHECK"
    if probe:
        R = probe_rules(folder, tag, s[0])
        reasons = R["reasons"]
    else:
        D, reasons = run_rules(folder, tag, s[0])
    if reasons:
        for r in reasons:
            print("%s STOP %s" % (word, r))
        print("%s %s FAIL [%s]" % (word, tag, "] [".join(r.split(":")[0] for r in reasons)))
        return 1
    extra = ""
    if probe:
        extra = " (median frame %.3f ms, window p99 %.3f ms, window %d frames, slow threshold %.1f, %s)" % (
            R["p50_all"], R["p99"], R["n_window"], s[0]["slow"], s[0]["save"])
    print("%s %s ok%s" % (word, tag, extra))
    return 0


def main(argv):
    args = dict(a.split("=", 1) for a in argv if "=" in a)
    for mode in ("probe", "check"):
        if mode in args:
            if set(args) != {mode, "order"} or len(argv) != 2:
                break
            return one_load(args[mode], args["order"], mode == "probe")
    if set(args) != {"root", "order", "out"} or len(argv) != 3:
        print("usage: python3 -I sf1_classify.py root=<dir> order=<order file> out=<new file>   |   "
              "probe=<probe folder> order=<order file>   |   check=<run folder> order=<order file>")
        return 2
    out = args["out"]
    if os.path.exists(out):
        print("REFUSED: %s exists, not overwriting" % out)
        return 2
    try:
        slots = read_order(args["order"])
    except Refused as e:
        print("REFUSED: %s" % e)
        return 2
    o = []
    P = o.append
    P("sf1 slow-frame classification (PREREG-sf1-slow-frame-classes)")
    P("scorer %s sha256 %s" % (os.path.abspath(__file__), sha(os.path.abspath(__file__))))
    P("order  %s sha256 %s" % (os.path.abspath(args["order"]), sha(args["order"])))
    P("root   %s" % os.path.abspath(args["root"]))
    P("constants THETA=%.2f UNEXPLAINED_MAX=%.2f P99_SPREAD=%.2f SHARE_Z=%.1f DOMINANT=%.2f COVER_TOL=%.2f "
      "MIN_WINDOW=%.2f TRAIN=%d x >=%.0f ms within %.0f ms PROBE_P50=%s"
      % (THETA, UNEXPLAINED_MAX, P99_SPREAD, SHARE_Z, DOMINANT, COVER_TOL, MIN_WINDOW, TRAIN_MIN, TRAIN_PASS_MS,
         TRAIN_GAP_MS, sorted(PROBE_P50.items())))
    # the driver's stop record (prereg 6.4): exactly one physical line, class done; else every scene NO VERDICT [stop]
    sp = os.path.join(os.path.dirname(os.path.abspath(args["order"])), "stop.txt")
    stop_bad = None
    if not os.path.isfile(sp):
        stop_bad = "missing: no stop record %s" % sp
    else:
        raw = open(sp, "rb").read().decode("ascii", "replace")
        body = raw[:-1] if raw.endswith("\n") else None
        if body is not None and body.endswith("\r"):
            body = body[:-1]
        if body is None or "\n" in body or "\r" in body:
            stop_bad = "lines: %s is not exactly one physical line ended by LF (%d line ends, %d bytes)" % (
                sp, raw.count("\n"), len(raw))
        elif not re.fullmatch(STOP_RE, body):
            stop_bad = "malformed: %r is not class=<done|box|fault>[ interrupted=<tag>] reason=<text>" % body
        elif not body.startswith("class=done "):
            stop_bad = "class: the series did not finish (%s)" % body
        P("STOP RECORD %s: %s" % (sp, body if body is not None else repr(raw[:200])))
    if stop_bad:
        P("STOP RECORD NOT DONE [%s]: %s" % tuple(stop_bad.split(": ", 1)))
    # each scene's probe load (never scored): the same gate the driver ran; failed or missing = NO VERDICT [probe]
    # for THAT scene only (the driver stops the whole series on either; the scorer judges each scene on its own probe)
    probe_ok = {}
    for pr in [x for x in slots if x["arm"] == "probe"]:
        PR = probe_rules(os.path.join(args["root"], pr["tag"]), pr["tag"], pr)
        probe_ok[pr["scene"]] = (not PR["reasons"], pr["tag"])
        if not PR["reasons"]:
            P("PROBE %s ok (median frame %.3f ms, window p99 %.3f ms; %s)" % (pr["tag"], PR["p50_all"], PR["p99"],
                                                                              heavy_of(os.path.join(args["root"], pr["tag"]))))
        else:
            P("PROBE %s STOP [%s]" % (pr["tag"], "] [".join(r.split(":")[0] for r in PR["reasons"])))
            for r in PR["reasons"]:
                P("    %s" % r)
    scored = [s for s in slots if s["arm"] == "scored"]
    scenes = []
    for s in scored:
        if s["scene"] not in scenes:
            scenes.append(s["scene"])
    final = []
    for sc in scenes:
        P("")
        P("== scene %s ==" % sc)
        runs = []
        for s in [x for x in scored if x["scene"] == sc]:
            used = s["tag"]
            sub = ""
            d = os.path.join(args["root"], s["tag"])
            rr = s["tag"] + "_r1"
            pre = []
            if os.path.isdir(d):
                try:
                    v = verdict_of(d)
                    if v != "VERDICT: clean":
                        if os.path.isdir(os.path.join(args["root"], rr)):
                            used = rr
                            sub = " (substituted: %s is %r)" % (s["tag"], v)
                            try:
                                v2 = verdict_of(os.path.join(args["root"], rr))
                                if v2 != "VERDICT: clean":
                                    pre.append("not_clean: %s is %r and its re-run is %r" % (s["tag"], v, v2))
                            except Bad as e:
                                pre.append(str(e))
                        else:
                            pre.append("not_clean: %s is %r and there is no %s" % (s["tag"], v, rr))
                    elif os.path.isdir(os.path.join(args["root"], rr)):
                        pre.append("unexpected_rerun: %s is clean but %s exists (the driver re-runs only a not-clean "
                                   "load)" % (s["tag"], rr))
                except Bad as e:
                    pre.append(str(e))
            R = score_run(args["root"], used, s)
            R["reasons"] = pre + R["reasons"]
            if R["reasons"]:
                R["valid"] = False
            R["slot"] = s["tag"]
            runs.append(R)
            hv = heavy_of(os.path.join(args["root"], used)) if os.path.isdir(os.path.join(args["root"], used)) else "-"
            if R.get("valid"):
                P("RUN %s used=%s VALID%s window=%d p50=%.3f p99=%.3f max=%.1f slow=%d hitches>100ms=%d %s"
                  % (s["tag"], used, sub, R["n_window"], R["p50"], R["p99"], R["max"], len(R["frames"]),
                     R["hitches"], "; ".join(R["notes"])))
            else:
                P("RUN %s used=%s VOID%s [%s]" % (s["tag"], used, sub,
                                                  "] [".join(r.split(":")[0] for r in R["reasons"])))
                for r in R["reasons"]:
                    P("    %s" % r)
            P("    conditions %s: %s" % (used, hv))
        valid = [R for R in runs if R.get("valid")]
        if stop_bad:
            P("SCENE %s NO VERDICT [stop]: %s" % (sc, stop_bad))
            final.append("%s=NO VERDICT (stop)" % sc)
            continue
        if not probe_ok[sc][0]:
            P("SCENE %s NO VERDICT [probe]: the probe load %s did not pass the gate" % (sc, probe_ok[sc][1]))
            final.append("%s=NO VERDICT (probe)" % sc)
            continue
        if len(valid) != RUNS_PER_SCENE:
            P("SCENE %s NO VERDICT [runs]: %d of %d runs valid" % (sc, len(valid), RUNS_PER_SCENE))
            final.append("%s=NO VERDICT (runs %d/%d valid)" % (sc, len(valid), RUNS_PER_SCENE))
            continue
        # per-frame listing
        for R in valid:
            for f in R["frames"]:
                P("  frame %s#%d ms=%.2f over_p50=%.2f class=%s score=%.2f main_share=%.2f top=%s"
                  % (R["slot"], f["i"], f["ms"], f["E"], f["cls"], f["score"], f["main"],
                     ",".join("%s:%.2f" % kv for kv in f["top"])))
        p99s = sorted(R["p99"] for R in valid)
        med = p99s[1]
        spread_ok = all(abs(x - med) <= P99_SPREAD * med for x in p99s)
        P("p99 per run %s; median %.3f; bound +-%.0f %% -> %s" % (", ".join("%.3f" % x for x in [R["p99"] for R in valid]),
                                                                 med, P99_SPREAD * 100, "ok" if spread_ok else "FAIL"))
        allf = [f for R in valid for f in R["frames"]]
        n = len(allf)
        cnt = {c: sum(1 for f in allf if f["cls"] == c) for c in CLASSES}
        share = {c: (cnt[c] / n if n else 0.0) for c in CLASSES}
        unexpl = sum(share[c] for c in UNEXPLAINED)
        nwin = sum(R["n_window"] for R in valid)
        P("class table (pooled, %d slow frames of %d window frames):" % (n, nwin))
        P("  %-22s %5s %7s %13s %15s %11s" % ("class", "n", "share", "mean_over_p50", "ms/1000frames", "main_share"))
        for c in CLASSES:
            fs = [f for f in allf if f["cls"] == c]
            if not fs:
                continue
            P("  %-22s %5d %6.1f%% %13.2f %15.2f %11.2f" % (c, len(fs), 100 * share[c], sum(f["E"] for f in fs) / len(fs),
                                                         1000 * sum(f["E"] for f in fs) / nwin,
                                                         sum(f["main"] for f in fs) / len(fs)))
        P("unexplained share %.1f%% (bound %.0f%%)" % (100 * unexpl, 100 * UNEXPLAINED_MAX))
        rep_ok = True
        for c in CLASSES:
            if share[c] < SHARE_CHECK_MIN:
                continue
            for R in valid:
                nr = len(R["frames"])
                pr_ = sum(1 for f in R["frames"] if f["cls"] == c) / nr if nr else 0.0
                b = SHARE_Z * math.sqrt(share[c] * (1 - share[c]) / nr) + 1.0 / nr if nr else 1.0
                ok = abs(pr_ - share[c]) <= b
                rep_ok &= ok
                P("  repeat %-20s %s share %.3f pooled %.3f bound %.3f %s" % (c, R["slot"], pr_, share[c], b,
                                                                            "ok" if ok else "FAIL"))
        gaps = []
        for R in valid:
            ts = [f["t"] for f in R["frames"] if f["cls"] == "alife"]
            gaps += [b - a for a, b in zip(ts, ts[1:])]
        if gaps:
            P("reported: wall ms between consecutive alife-class slow frames: median %.0f (n=%d)"
              % (pct(sorted(gaps), 0.5), len(gaps)))
        if not spread_ok:
            P("SCENE %s NO VERDICT [p99_spread]: runs not repeatable" % sc)
            final.append("%s=NO VERDICT (p99 spread)" % sc)
            continue
        if n == 0 or unexpl > UNEXPLAINED_MAX:
            P("SCENE %s NO VERDICT [coarse]: %.1f%% of slow frames unexplained > %.0f%%: the profiler zones are too "
              "coarse to say what the tail is made of" % (sc, 100 * unexpl, 100 * UNEXPLAINED_MAX))
            final.append("%s=NO VERDICT (unexplained %.0f%%)" % (sc, 100 * unexpl))
            continue
        if not rep_ok:
            P("SCENE %s NO VERDICT [share_repeat]: class shares differ between runs beyond the bound" % sc)
            final.append("%s=NO VERDICT (shares not repeatable)" % sc)
            continue
        ranked = sorted(EXPLAINED, key=lambda c: -share[c])
        top = ranked[0]
        lo = share[top] - 2 * math.sqrt(share[top] * (1 - share[top]) / n)
        if lo > DOMINANT:
            P("SCENE %s RESULT: DOMINANT %s %.1f%% (2-SE low %.1f%% > 50%%); next %s %.1f%%; unexplained %.1f%%"
              % (sc, top, 100 * share[top], 100 * lo, ranked[1], 100 * share[ranked[1]], 100 * unexpl))
            final.append("%s=DOMINANT %s %.0f%%" % (sc, top, 100 * share[top]))
        else:
            P("SCENE %s RESULT: NO SINGLE DOMINANT CLASS; largest %s %.1f%%, next %s %.1f%%; unexplained %.1f%%"
              % (sc, top, 100 * share[top], ranked[1], 100 * share[ranked[1]], 100 * unexpl))
            final.append("%s=MIXED largest %s %.0f%%" % (sc, top, 100 * share[top]))
    P("")
    P("SF1 RESULT: " + "; ".join(final))
    with open(out, "x") as f:
        f.write("\n".join(o) + "\n")
    print("\n".join(o[-1:]))
    print("written %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
