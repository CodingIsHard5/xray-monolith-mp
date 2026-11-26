#!/usr/bin/env python3
# sf1 slow-frame classifier (PREREG-sf1-slow-frame-classes-DRAFT.md). Stdlib only; run with python3 -I.
#
#   python3 -I sf1_classify.py root=<folder holding the run folders> order=<order file> out=<new output file>
#
# Order file: "tag|exe|save|warm|frames|slow_ms" per line, '#' = comment. save cordon1 -> scene cordon, bench1 -> rostok.
# Each scene present must have exactly 3 slots. Tags must be all-lowercase. A slot whose conditions.txt VERDICT is not
# "clean" is read from <tag>_rr (the driver's re-run, same rule as tools/s4c_series.sh); the substitution is named.
# Run folder <root>/<tag>/: perf_frames_<tag>.csv, perf_sites_<tag>.csv, perf_slow_<tag>.csv, conditions.txt.
# A missing or empty file fails BY NAME and voids that slot; it is never read as 0.
# The output is written to out= in this same call; an existing out file is refused (exit 2), never overwritten.
# Exit: 0 = output written (whatever the verdict), 2 = refused (usage, order file, out exists).
# Every rule and constant below is the registered one (prereg sections 4-6); nothing is read from the environment.
import hashlib
import math
import os
import sys

# ---- registered constants (prereg section 5; derivations there) ----
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
RUNS_PER_SCENE = 3
SCENES = {"cordon1": "cordon", "bench1": "rostok"}
EXCLUDED = {"mt_FreezeThread", "Check timer"}   # freeze watchdog: a 500 ms Sleep loop, its "self" is sleep
EXPLAINED = ["alife", "script", "render", "gpu_wait", "online_sim", "io_stream", "sound", "other_known"]
UNEXPLAINED = ["gc", "main_unzoned_or_wait", "unknown", "no_data"]
CLASSES = EXPLAINED + UNEXPLAINED

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
    if not L[0].startswith("#frames,") or not L[1].startswith("#total_ms,"):
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


def score_run(root, tag, frames_reg, slow_reg):
    """Returns dict with valid flag, reasons list, and the per-frame results."""
    R = dict(tag=tag, reasons=[], notes=[], frames=[])
    d = os.path.join(root, tag)
    if not os.path.isdir(d):
        R["reasons"].append("missing: MISSING folder %s" % d)
        return R
    try:
        ms = read_frames(d, tag)
        nfr, sites = read_sites(d, tag)
        thr, rows = read_slow(d, tag)
    except Bad as e:
        R["reasons"].append(str(e))
        return R
    N = len(ms)
    if N != frames_reg or nfr != frames_reg:
        R["reasons"].append("framecount: frames file %d rows, sites header %d, registered %d" % (N, nfr, frames_reg))
    if abs(thr - slow_reg) > 0.0005:
        R["reasons"].append("threshold_header: slow file threshold %.3f, registered %.3f" % (thr, slow_reg))
    bad_idx = [f for f in rows if f < 0 or f >= N]
    if bad_idx:
        R["reasons"].append("malformed: slow rows for frame(s) outside the capture, e.g. %d" % bad_idx[0])
    # stock frame loop, uncapped (outcome inside the capture, prereg 6 R3/R4)
    seq = [s for s in sites.values() if s["name"] == "Process seqFrame"]
    if not seq:
        R["reasons"].append("stock_loop: site 'Process seqFrame' missing from perf_sites")
    elif seq[0]["cm"] != N:
        R["reasons"].append("stock_loop: 'Process seqFrame' main calls %d != frames %d" % (seq[0]["cm"], N))
    st = sum(s["cm"] + s["co"] for s in sites.values() if s["name"] == "Sim tick")
    if st:
        R["reasons"].append("stock_loop: site 'Sim tick' has %d calls (fixed tick on)" % st)
    eco = sum(s["cm"] + s["co"] for s in sites.values() if s["name"] == "Eco Render")
    if eco:
        R["reasons"].append("frame_limiter: site 'Eco Render' has %d calls (capped run)" % eco)
    # analysis window: cut at the first frame clearly above T without zone rows (dump cap reached or a gap)
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
        return R
    w = ms[:cut]
    sw = sorted(w)
    p50, p99 = pct(sw, 0.5), pct(sw, 0.99)
    R.update(p50=p50, p99=p99, mean=sum(ms) / N, n_window=cut, max=max(w))
    if p99 < thr * (1 + COVER_TOL):
        R["reasons"].append("coverage_p99: threshold %.3f ms above window p99 %.3f ms (frames between have no zone rows)"
                            % (thr, p99))
    # after-load train in the window (prereg 6 R6)
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
    if R["reasons"]:
        return R
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


def main(argv):
    args = dict(a.split("=", 1) for a in argv if "=" in a)
    if set(args) != {"root", "order", "out"} or len(argv) != 3:
        print("usage: python3 -I sf1_classify.py root=<dir> order=<order file> out=<new file>")
        return 2
    out = args["out"]
    if os.path.exists(out):
        print("REFUSED: %s exists, not overwriting" % out)
        return 2
    try:
        olines = [l for l in lines_of(args["order"]) if not l.lstrip().startswith("#")]
    except Bad as e:
        print("REFUSED: order file: %s" % e)
        return 2
    slots = []
    for l in olines:
        a = l.split("|")
        if len(a) != 6:
            print("REFUSED: order line %r is not tag|exe|save|warm|frames|slow_ms" % l)
            return 2
        tag, exe, save, warm, frames, slow = [x.strip() for x in a]
        if tag != tag.lower():
            print("REFUSED: tag %s is not lowercase" % tag)
            return 2
        if save not in SCENES:
            print("REFUSED: save %s is not one of %s" % (save, sorted(SCENES)))
            return 2
        slots.append(dict(tag=tag, exe=exe, scene=SCENES[save], frames=int(frames), slow=float(slow)))
    scenes = []
    for s in slots:
        if s["scene"] not in scenes:
            scenes.append(s["scene"])
    for sc in scenes:
        if sum(1 for s in slots if s["scene"] == sc) != RUNS_PER_SCENE:
            print("REFUSED: scene %s has not exactly %d slots" % (sc, RUNS_PER_SCENE))
            return 2
    o = []
    P = o.append
    P("sf1 slow-frame classification (PREREG-sf1-slow-frame-classes)")
    P("scorer %s sha256 %s" % (os.path.abspath(__file__), sha(os.path.abspath(__file__))))
    P("order  %s sha256 %s" % (os.path.abspath(args["order"]), sha(args["order"])))
    P("root   %s" % os.path.abspath(args["root"]))
    P("constants THETA=%.2f UNEXPLAINED_MAX=%.2f P99_SPREAD=%.2f SHARE_Z=%.1f DOMINANT=%.2f COVER_TOL=%.2f "
      "MIN_WINDOW=%.2f TRAIN=%d x >=%.0f ms within %.0f ms" % (THETA, UNEXPLAINED_MAX, P99_SPREAD, SHARE_Z, DOMINANT,
                                                             COVER_TOL, MIN_WINDOW, TRAIN_MIN, TRAIN_PASS_MS, TRAIN_GAP_MS))
    final = []
    for sc in scenes:
        P("")
        P("== scene %s ==" % sc)
        runs = []
        for s in [x for x in slots if x["scene"] == sc]:
            used = s["tag"]
            sub = ""
            d = os.path.join(args["root"], s["tag"])
            pre = []
            if os.path.isdir(d):
                try:
                    v = verdict_of(d)
                    if v != "VERDICT: clean":
                        rr = s["tag"] + "_rr"
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
                except Bad as e:
                    pre.append(str(e))
            R = score_run(args["root"], used, s["frames"], s["slow"])
            R["reasons"] = pre + R["reasons"]
            if R["reasons"]:
                R["valid"] = False
            R["slot"] = s["tag"]
            runs.append(R)
            if R.get("valid"):
                P("RUN %s used=%s VALID%s window=%d p50=%.3f p99=%.3f max=%.1f slow=%d hitches>100ms=%d %s"
                  % (s["tag"], used, sub, R["n_window"], R["p50"], R["p99"], R["max"], len(R["frames"]),
                     R["hitches"], "; ".join(R["notes"])))
            else:
                P("RUN %s used=%s VOID%s [%s]" % (s["tag"], used, sub,
                                                  "] [".join(r.split(":")[0] for r in R["reasons"])))
                for r in R["reasons"]:
                    P("    %s" % r)
        valid = [R for R in runs if R.get("valid")]
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
                pr = sum(1 for f in R["frames"] if f["cls"] == c) / nr if nr else 0.0
                b = SHARE_Z * math.sqrt(share[c] * (1 - share[c]) / nr) + 1.0 / nr if nr else 1.0
                ok = abs(pr - share[c]) <= b
                rep_ok &= ok
                P("  repeat %-20s %s share %.3f pooled %.3f bound %.3f %s" % (c, R["slot"], pr, share[c], b,
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
