#!/usr/bin/env python3
# wu1 scorer: does the engine lever -alife_warmup remove the slow frames after a level load?
# (PREREG-wu1-alife-warmup-DRAFT.md). Stdlib only; run with python3 -I. DRAFT, not registered.
#
#   python3 -I wu1_score.py root=<folder holding the load folders> order=<wu1_order.txt> out=<new report file>
#   python3 -I wu1_score.py parse=<xray.log>        (log parser only, prints what it found; used by the self-test)
#
# Order file: "tag|exe|save|warm|frames|slow_ms|arm" per line, '#' = comment; arm on / off / prime (prime is not read).
# Load folder <root>/<tag>/: perf_frames_<tag>.csv, perf_sites_<tag>.csv, perf_slow_<tag>.csv, conditions.txt, xray.log.
# Every file is read with CR stripped (engine output is CRLF). A missing, empty or unparsable file is a FAIL, by name.
# The report is written to out= in this same call; an existing out file is refused, never overwritten.
# Exit: 0 PASS, 1 FAIL, 3 NO VERDICT, 2 refused (usage, order file, duplicate or uppercase tag, out exists).
# Every rule and constant is the registered one (prereg sections 4-7); nothing is read from the environment.
import hashlib
import os
import re
import sys

# ---- registered constants (prereg section 6, derivation beside each there) ----
WINDOW_MS = 30000.0       # headline window: first 30 s of play after the loading screen (Revision 2: widened from
                          #   the supervisor's ~15 s so it holds stock's hitch (6.5-7.8 s) AND its whole train (to ~22 s))
WINDOW2_MS = 15000.0      # the supervisor's ~15 s window, reported only beside the headline (sees the train's start only)
SLOW1_MS = 20.0           # "slow frame" for the train count
SLOW2_MS = 33.3           # "slow frame" at 30 fps
SLOW_HEADER_MS = 20.0     # -perf_slow value the driver passes (perf_slow threshold header)
LEGACY_WIDE = 50          # alife_slow lines in ONE frame that mark a wide pass (20-per-pass gives <= 20, wide 128-185)
WIDE_N = 20               # an alife_pass with n > 20 is wide (stock limits are 3 and 20; n <= limit)
LIMIT_FULL = 4294967295   # the forced pass's limit u32(-1)
LIMIT_WIDE = 65534        # the stock script's widening value (bind_stalker.script:200)
P_MIN = 3                 # forced pass must see precache_left >= 3 (the hold keeps the countdown at 3)
N_M_MIN = 0.9             # forced pass must update N >= 0.9 * M objects
HOLD_CAP = 300            # device.cpp alife_warmup_hold_cap (reported against)
MIN_ON = 8                # valid ON loads needed
MIN_OFFP = 3              # valid OFF-IN-PLAY loads needed
STOCK_HITCH_MIN = 150.0   # median OFF-IN-PLAY worst frame (30 s) must reach this, else the window missed stock's hitch
STOCK_TRAIN_MIN = 7       # median OFF-IN-PLAY frames > 20 ms (30 s) must reach this, else the window missed the train
                          #   (expected 16-19, prereg 6; = N20_DROP so H2 stays decidable)
WORST_RATIO = 0.5         # H1: median ON worst <= 0.5 x median OFF-IN-PLAY worst ...
WORST_STRAY = 1           #     ... and at most 1 ON load with worst >= median OFF-IN-PLAY worst
N20_DROP = 7              # H2: median OFF-IN-PLAY frames > 20 ms - median ON frames > 20 ms >= 7 (30 s; expected 14-19)
ALIKE_MIN_OFFL = 2        # stock's two orders must differ in the window (checked when >= 2 OFF-IN-LOADING loads):
                          #   median OFF-IN-LOADING worst <= 0.5 x median OFF-IN-PLAY worst and n20 differ by >= 7
PRICE_MAX_S = 2.0         # H3: median ON load time - median OFF-IN-PLAY load time <= 2.0 s
LOADTIME_MIN_ON = 8       # load time must be measurable in this many ON loads ...
LOADTIME_MIN_OFFP = 3     # ... and this many OFF-IN-PLAY loads, else NO VERDICT on the price
SAME_WORST_X = 1.25       # secondary (reported only): ON "same as stock's good order" if median ON worst
SAME_WORST_ADD = 10.0     #   <= 1.25 x median OFF-IN-LOADING worst + 10 ms,
SAME_N20_ADD = 2          #   median ON n20 <= median OFF-IN-LOADING n20 + 2,
SAME_PRICE_S = 1.0        #   and |median ON load - median OFF-IN-LOADING load| <= 1.0 s
SAME_MIN_OFFL = 2
SLOW_CAP_ROWS = 600 * 14 - 14   # profiler_agg.cpp:42-43, 190: a slow frame is kept only while rows + 14 <= 8400, so a file
                                #   with more than 8386 rows may have lost slow frames: VOID slow_cap (Revision 2)
SAVE_ZONES = ("CALifeStorageManager::save", "CALifeStorageManager::prepare_objects_for_save")
ARMS = ("on", "off", "prime")


def on(name):
    """Self-test mutation hook: every check below is guarded by on("<name>"). Always True in this file."""
    return True


class Bad(Exception):
    """A file that is missing, empty or unparsable: FAIL by name."""
    def __init__(self, kind, text):
        Exception.__init__(self, "%s: %s" % (kind, text))
        self.kind = kind


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


def lines_of(path):
    if not os.path.exists(path):
        raise Bad("missing", "MISSING %s" % path)
    if os.path.getsize(path) == 0:
        raise Bad("empty", "EMPTY %s" % path)
    with open(path, "rb") as f:
        raw = f.read().decode("ascii", "replace")
    out = []
    for l in raw.split("\n"):
        if on("cr_strip"):
            l = l.rstrip("\r")
        if l != "":
            out.append(l)
    if not out:
        raise Bad("empty", "EMPTY %s (no lines)" % path)
    return out


def median(v):
    s = sorted(v)
    n = len(s)
    if n == 0:
        return None
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


# ---------------- files ----------------
def read_frames(d, tag):
    p = os.path.join(d, "perf_frames_%s.csv" % tag)
    L = lines_of(p)
    if L[0] != "frame,ms":
        raise Bad("unparsable", "UNPARSABLE %s header %r" % (p, L[0]))
    ms = []
    for k, l in enumerate(L[1:]):
        a = l.split(",")
        try:
            if len(a) != 2 or int(a[0]) != k:
                raise ValueError
            v = float(a[1])
            if not v >= 0.0:
                raise ValueError
        except ValueError:
            raise Bad("unparsable", "UNPARSABLE %s row %d %r" % (p, k + 1, l))
        ms.append(v)
    if not ms:
        raise Bad("empty", "EMPTY %s (header only)" % p)
    return ms


def read_sites(d, tag):
    p = os.path.join(d, "perf_sites_%s.csv" % tag)
    L = lines_of(p)
    try:
        if not L[0].startswith("#frames,") or not L[1].startswith("#total_ms,"):
            raise ValueError
        nfr = int(L[0].split(",")[1])
        if L[2] != "name,file,line,calls_main,incl_main_ms,self_main_ms,calls_other,incl_other_ms,self_other_ms":
            raise ValueError
    except (ValueError, IndexError):
        raise Bad("unparsable", "UNPARSABLE %s header" % p)
    calls = {}
    for l in L[3:]:
        a = l.split(",")
        try:
            if len(a) != 9:
                raise ValueError
            c = int(a[3]) + int(a[6])
            cm = int(a[3])
            float(a[5]); float(a[8])
        except ValueError:
            raise Bad("unparsable", "UNPARSABLE %s row %r" % (p, l))
        t = calls.setdefault(a[0], [0, 0])
        t[0] += cm
        t[1] += c
    return nfr, calls


def read_slow(d, tag):
    p = os.path.join(d, "perf_slow_%s.csv" % tag)
    L = lines_of(p)
    try:
        if not L[0].startswith("#threshold_ms,"):
            raise ValueError
        thr = float(L[0].split(",")[1])
        if len(L) < 2 or L[1] != "frame,frame_ms,thread,self_ms,calls,name,file,line":
            raise ValueError
    except (ValueError, IndexError):
        raise Bad("unparsable", "UNPARSABLE %s header" % p)
    rows = []
    for l in L[2:]:
        a = l.split(",")
        try:
            if len(a) != 8 or a[2] not in ("main", "other"):
                raise ValueError
            rows.append((int(a[0]), float(a[1]), a[2], float(a[3]), a[5]))
        except ValueError:
            raise Bad("unparsable", "UNPARSABLE %s row %r" % (p, l))
    return thr, rows


def read_verdict(d):
    p = os.path.join(d, "conditions.txt")
    for l in lines_of(p):
        if l.startswith("VERDICT"):
            return l.strip()
    raise Bad("unparsable", "UNPARSABLE %s has no VERDICT line" % p)


# ---------------- xray.log ----------------
RE_FULL = re.compile(r"^\* \[alife_warmup\] full pass: (\d+) of (\d+) objects, frame (\d+), precache_left (\d+), ([0-9.]+) ms$")
RE_HELD = re.compile(r"^\* \[alife_warmup\] precache held (\d+) frames$")
RE_CAP = re.compile(r"^! \[alife_warmup\] hold cap hit after (\d+) frames, pass not run$")
RE_PASS = re.compile(r"^\* alife_pass frame (\d+) time_global (\d+) precache (\d+) n (\d+) of (\d+) limit (\d+)$")
RE_OPU = re.compile(r"^\* alife_opu frame (\d+) time_global (\d+) precache (\d+) value (\d+)$")
RE_SLOW = re.compile(r"^\* alife_slow frame (\d+) id (\d+) .* ([0-9.]+) ms$")
INTRO = "intro_start game_loaded"
HANDLED = "* [x-ray]: Handled Necessary Textures Destruction"


def parse_log(path):
    L = lines_of(path)
    g = dict(cmd=None, intro=None, handled=None, cap_start={}, cap_fin={}, full=[], held=[], cap=[], passes=[],
             opus=[], slows=[], n_lines=len(L))
    for i, raw in enumerate(L):
        l = raw.rstrip()
        if g["cmd"] is None and l.startswith("command line "):
            g["cmd"] = l[len("command line "):]
            continue
        if l == INTRO and g["intro"] is None:
            g["intro"] = i
            continue
        if l == HANDLED and g["handled"] is None:
            g["handled"] = i
            continue
        m = re.match(r"^\* \[xrprof\] capture '([^']*)' (started|finished): (\d+) frames", l)
        if m:
            (g["cap_start"] if m.group(2) == "started" else g["cap_fin"]).setdefault(m.group(1), (i, int(m.group(3))))
            continue
        if "[alife_warmup]" in l:
            m = RE_FULL.match(l)
            if m:
                g["full"].append(dict(i=i, N=int(m.group(1)), M=int(m.group(2)), F=int(m.group(3)), P=int(m.group(4)),
                                      X=float(m.group(5))))
                continue
            m = RE_HELD.match(l)
            if m:
                g["held"].append(dict(i=i, K=int(m.group(1))))
                continue
            m = RE_CAP.match(l)
            if m:
                g["cap"].append(dict(i=i, K=int(m.group(1))))
                continue
            raise Bad("unparsable", "UNPARSABLE %s line %d %r" % (path, i + 1, l))
        if l.startswith("* alife_pass "):
            m = RE_PASS.match(l)
            if not m:
                raise Bad("unparsable", "UNPARSABLE %s line %d %r" % (path, i + 1, l))
            g["passes"].append(dict(i=i, F=int(m.group(1)), T=int(m.group(2)), P=int(m.group(3)), n=int(m.group(4)),
                                    M=int(m.group(5)), lim=int(m.group(6))))
            continue
        if l.startswith("* alife_opu "):
            m = RE_OPU.match(l)
            if not m:
                raise Bad("unparsable", "UNPARSABLE %s line %d %r" % (path, i + 1, l))
            g["opus"].append(dict(i=i, F=int(m.group(1)), T=int(m.group(2)), P=int(m.group(3)), V=int(m.group(4))))
            continue
        if l.startswith("* alife_slow "):
            m = RE_SLOW.match(l)
            if not m:
                raise Bad("unparsable", "UNPARSABLE %s line %d %r" % (path, i + 1, l))
            g["slows"].append(dict(i=i, F=int(m.group(1)), ms=float(m.group(3))))
            continue
    if g["cmd"] is None:
        raise Bad("unparsable", "UNPARSABLE %s has no 'command line' line" % path)
    if g["intro"] is None:
        raise Bad("unparsable", "UNPARSABLE %s has no '%s' line (loading never ended)" % (path, INTRO))
    if g["handled"] is None:
        raise Bad("unparsable", "UNPARSABLE %s has no '%s' line" % (path, HANDLED))
    # legacy order from alife_slow lines alone (works on logs older than the alife_pass/alife_opu lines)
    pre, anyf = {}, {}
    for s in g["slows"]:
        anyf[s["F"]] = anyf.get(s["F"], 0) + 1
        if s["i"] < g["intro"]:
            pre[s["F"]] = pre.get(s["F"], 0) + 1
    g["slow_pre_max"] = max(pre.values()) if pre else 0
    g["slow_any_max"] = max(anyf.values()) if anyf else 0
    if g["slow_pre_max"] >= LEGACY_WIDE:
        g["legacy"] = "L"
    elif g["slow_any_max"] < LEGACY_WIDE:
        g["legacy"] = "P"
    else:
        g["legacy"] = "W"   # a wide pass, but after loading ended
    return g


def classify_off(g):
    """Prereg 4.2. Returns (group, reason). group: P (in-play), L (in-loading), OTHER."""
    if not g["passes"] and not g["opus"]:
        return "OTHER", "no alife_pass / alife_opu lines"
    wide = [p for p in g["passes"] if p["n"] > WIDE_N]
    in_loading = [p for p in wide if (p["P"] > 0 and p["i"] < g["intro"]) or not on("l_in_loading")]
    reinit = next((o for o in g["opus"] if o["V"] == LIMIT_WIDE), None)
    reset = next((o for o in g["opus"] if reinit is not None and o["i"] > reinit["i"] and o["V"] < LIMIT_WIDE), None)
    if in_loading:
        if g["legacy"] == "L" or not on("legacy_agree"):
            return "L", "wide pass at frame %d precache %d before intro_start (n %d of %d)" % (
                in_loading[0]["F"], in_loading[0]["P"], in_loading[0]["n"], in_loading[0]["M"])
        return "OTHER", "pass lines say in-loading, alife_slow lines say %s (max %d lines in one frame before intro)" % (
            g["legacy"], g["slow_pre_max"])
    if wide:
        return "OTHER", "wide pass only after loading ended (frame %d precache %d)" % (wide[0]["F"], wide[0]["P"])
    if reinit is None or reset is None:
        return "OTHER", "no widening (65534) and narrowing alife_opu pair"
    if on("cover") and not (g["passes"] and max(p["T"] for p in g["passes"]) >= reset["T"]):
        return "OTHER", "pass lines end before the narrowing at time_global %d: a wide pass could be unlogged" % reset["T"]
    if g["legacy"] != "P" and on("legacy_agree"):
        return "OTHER", "pass lines say in-play, alife_slow lines say %s (max %d lines in one frame)" % (
            g["legacy"], g["slow_any_max"])
    return "P", "no wide pass; narrowed at frame %d precache %d (%s)" % (
        reset["F"], reset["P"], "inside loading" if reset["P"] > 0 else "in play")


def load_time(g, ms):
    """Prereg 5.4: engine clock (time_global, ms since device init) at the first frame after the loading screen."""
    pts = [(x["F"], x["T"], x["P"], x["i"]) for x in g["passes"] + g["opus"]]
    pre = [x for x in pts if x[2] > 0]
    if not pre:
        return None, "no measurement line during the loading countdown"
    last = max(pre, key=lambda x: (x[0], x[3]))
    F0 = last[0] + last[2]
    play = [x for x in pts if x[2] == 0 and x[0] >= F0]
    if not play:
        return None, "no measurement line in play after frame %d" % F0
    first = min(play, key=lambda x: (x[0], x[3]))
    k = first[0] - F0
    if k >= len(ms):
        return None, "first in-play line is past the capture"
    t_end = first[1] - sum(ms[:k])
    if t_end + 1.0 < last[1] or t_end > first[1] + 0.5:
        return None, "inconsistent: %.0f ms not between %d and %d" % (t_end, last[1], first[1])
    return t_end / 1000.0, "frame0=%d from line at frame %d precache %d; first in-play line frame %d (+%d frames)" % (
        F0, last[0], last[2], first[0], k)


def window(ms, excl, limit):
    t = 0.0
    worst = worst_all = 0.0
    n1 = n2 = 0
    first = last_end = None
    for i, v in enumerate(ms):
        if t >= limit:
            break
        worst_all = max(worst_all, v)
        if i not in excl:
            worst = max(worst, v)
            if v > SLOW1_MS:
                n1 += 1
                if first is None:
                    first = t
                last_end = t + v
            if v > SLOW2_MS:
                n2 += 1
        t += v
    return dict(covered=sum(ms) >= limit, worst=worst, worst_all=worst_all, n20=n1, n33=n2,
                span=(last_end - first) / 1000.0 if first is not None else 0.0)


def score_load(root, s):
    """Returns dict: fatal (list), void (list), info."""
    R = dict(tag=s["tag"], arm=s["arm"], fatal=[], void=[], notes=[], group=None, why="", lt=None)
    d = os.path.join(root, s["tag"])
    if not os.path.isdir(d):
        e = Bad("missing", "MISSING folder %s" % d)
        (R["fatal"] if on("missing_fail") else R["void"]).append(str(e))
        return R
    got, errs = {}, []
    for key, fn in (("verdict", lambda: read_verdict(d)), ("ms", lambda: read_frames(d, s["tag"])),
                    ("sites", lambda: read_sites(d, s["tag"])), ("slow", lambda: read_slow(d, s["tag"])),
                    ("g", lambda: parse_log(os.path.join(d, "xray.log")))):
        try:
            got[key] = fn()
        except Bad as e:
            errs.append(str(e))
    if errs:
        (R["fatal"] if on("missing_fail") else R["void"]).extend(errs)
        return R
    verdict, ms, g = got["verdict"], got["ms"], got["g"]
    nfr, calls = got["sites"]
    thr, rows = got["slow"]
    tag, N = s["tag"], s["frames"]
    # ---- the run is the registered condition (prereg 7.1) ----
    tok = g["cmd"].lower().split()
    arm_bad = []
    if not tok or not (tok[0].endswith("\\" + s["exe"].lower()) or tok[0].endswith("/" + s["exe"].lower())):
        arm_bad.append("exe is %r, registered %s" % (tok[0] if tok else "", s["exe"]))
    if "server(%s/single/alife/load)" % s["save"].lower() not in tok:
        arm_bad.append("save is not %s" % s["save"])
    if "-perf_capture" not in tok or tok.index("-perf_capture") + 1 >= len(tok) or \
            tok[tok.index("-perf_capture") + 1] != "%d,%d,%s" % (s["warm"], N, tag):
        arm_bad.append("-perf_capture is not %d,%d,%s" % (s["warm"], N, tag))
    for bad in ("-perf_fixed_dt", "-det_mode", "-sim_tick"):
        if bad in tok:
            arm_bad.append("%s present (not real time / not stock loop)" % bad)
    if ("-alife_warmup" in tok) != (s["arm"] == "on"):
        arm_bad.append("-alife_warmup %s on an %s load" % ("present" if "-alife_warmup" in tok else "absent", s["arm"].upper()))
    if on("arm_cmdline") and arm_bad:
        R["fatal"].append("arm_mismatch: " + "; ".join(arm_bad))
    # ---- lever off = stock: no lever line at all ----
    if s["arm"] == "off" and on("off_not_stock") and (g["full"] or g["held"] or g["cap"]):
        R["fatal"].append("off_not_stock: %d [alife_warmup] line(s) in an OFF load" % (
            len(g["full"]) + len(g["held"]) + len(g["cap"])))
    # ---- per ON load: the full pass ran behind the loading screen (prereg 4.1) ----
    if s["arm"] == "on":
        if len(g["full"]) != 1 and on("lever_count"):
            R["fatal"].append("lever_count: %d '[alife_warmup] full pass' lines, registered exactly 1" % len(g["full"]))
        for f in g["full"][:1]:
            if on("lever_before_intro") and not (f["i"] < g["intro"] and f["i"] < g["handled"]):
                R["fatal"].append("lever_after_intro: full pass at log line %d, intro_start at %d, textures handled at %d"
                                  % (f["i"] + 1, g["intro"] + 1, g["handled"] + 1))
            if on("lever_precache") and f["P"] < P_MIN:
                R["fatal"].append("lever_precache: precache_left %d < %d" % (f["P"], P_MIN))
            if on("lever_partial") and f["N"] < N_M_MIN * f["M"]:
                R["fatal"].append("lever_partial: %d of %d objects < %.0f %%" % (f["N"], f["M"], 100 * N_M_MIN))
            R["notes"].append("full pass %d of %d, frame %d, precache_left %d, %.1f ms" % (f["N"], f["M"], f["F"], f["P"], f["X"]))
            R["pass_ms"] = f["X"]
        if g["cap"] and on("lever_cap"):
            R["fatal"].append("lever_cap: hold cap hit after %d frames (cap %d)" % (g["cap"][0]["K"], HOLD_CAP))
        R["held"] = sum(h["K"] for h in g["held"])
        if g["held"]:
            R["notes"].append("precache held %d frames (cap %d)" % (R["held"], HOLD_CAP))
    # ---- measurement validity (void, not fail) ----
    if verdict != "VERDICT: clean" and on("not_clean"):
        R["void"].append("not_clean: %r (no re-run, registered)" % verdict)
    if (len(ms) != N or nfr != N) and on("framecount"):
        R["void"].append("framecount: frames file %d rows, sites header %d, registered %d" % (len(ms), nfr, N))
    cs, cf = g["cap_start"].get(tag), g["cap_fin"].get(tag)
    if on("capture_lines") and (cs is None or cf is None or cs[1] != N or cf[1] != N or cs[0] < g["handled"]):
        R["void"].append("capture_lines: xrprof started/finished lines for '%s' with %d frames after loading not found" % (tag, N))
    if on("stock_loop"):
        sf = calls.get("Process seqFrame", [0, 0])[0]
        if sf != N:
            R["void"].append("stock_loop: 'Process seqFrame' main calls %d != frames %d" % (sf, N))
        if calls.get("Sim tick", [0, 0])[1]:
            R["void"].append("stock_loop: 'Sim tick' has %d calls" % calls["Sim tick"][1])
    if on("frame_limiter") and calls.get("Eco Render", [0, 0])[1]:
        R["void"].append("frame_limiter: 'Eco Render' has %d calls (capped run)" % calls["Eco Render"][1])
    if on("threshold") and abs(thr - SLOW_HEADER_MS) > 0.0005:
        R["void"].append("threshold_header: perf_slow threshold %.3f, registered %.3f" % (thr, SLOW_HEADER_MS))
    # ---- the scripted save (cordon1's start-of-game autosave) is excluded from the frame numbers ----
    if on("slow_cap") and len(rows) > SLOW_CAP_ROWS:
        R["void"].append("slow_cap: perf_slow has %d rows > %d: the profiler's 600-frame slow dump may be full, a save "
                         "frame could be missing from it" % (len(rows), SLOW_CAP_ROWS))
    save_frames = sorted(set(r[0] for r in rows if r[4] in SAVE_ZONES and 0 <= r[0] < len(ms)))
    save_calls = sum(calls.get(z, [0, 0])[1] for z in SAVE_ZONES)
    if on("save_unlocated") and save_calls and not save_frames:
        R["void"].append("save_unlocated: %d save-zone calls in the capture but no slow-frame row names them" % save_calls)
    excl = set(save_frames) if on("save_exclude") else set()
    R["save"] = ", ".join("#%d %.0f ms" % (i, ms[i]) for i in save_frames) or "none in capture"
    R["w1"] = window(ms, excl, WINDOW_MS)
    R["w2"] = window(ms, excl, WINDOW2_MS)
    if on("window_short") and not R["w1"]["covered"]:
        R["void"].append("window_short: capture covers %.1f s < %.0f s" % (sum(ms) / 1000.0, WINDOW_MS / 1000.0))
    # ---- load time ----
    R["lt"], R["lt_why"] = load_time(g, ms)
    # ---- group ----
    if s["arm"] == "on":
        R["group"] = "ON"
    else:
        grp, why = classify_off(g)
        R["group"], R["why"] = {"P": "OFF-IN-PLAY", "L": "OFF-IN-LOADING", "OTHER": "OFF-OTHER"}[grp], why
    R["wide"] = sum(1 for p in g["passes"] if p["n"] > WIDE_N)
    R["legacy"] = "%s (max %d alife_slow lines in one frame before intro_start)" % (g["legacy"], g["slow_pre_max"])
    return R


def fmt(v, f="%.1f"):
    return "n/a" if v is None else f % v


def main(argv):
    args = dict(a.split("=", 1) for a in argv if "=" in a)
    if set(args) == {"parse"} and len(argv) == 1:
        try:
            g = parse_log(args["parse"])
        except Bad as e:
            print("PARSE FAIL %s" % e)
            return 1
        print("PARSE %s lines=%d intro=%d handled=%d legacy=%s slow_pre_max=%d slow_any_max=%d alife_slow=%d "
              "alife_pass=%d alife_opu=%d warmup_full=%d held=%d cap=%d"
              % (args["parse"], g["n_lines"], g["intro"] + 1, g["handled"] + 1, g["legacy"], g["slow_pre_max"],
                 g["slow_any_max"], len(g["slows"]), len(g["passes"]), len(g["opus"]), len(g["full"]),
                 len(g["held"]), len(g["cap"])))
        return 0
    if set(args) != {"root", "order", "out"} or len(argv) != 3:
        print("usage: python3 -I wu1_score.py root=<dir> order=<order file> out=<new file>   |   parse=<xray.log>")
        return 2
    out = args["out"]
    if os.path.exists(out) and on("refuse_out"):
        print("REFUSED: %s exists, not overwriting" % out)
        return 2
    try:
        olines = [l for l in lines_of(args["order"]) if not l.lstrip().startswith("#")]
    except Bad as e:
        print("REFUSED: order file: %s" % e)
        return 2
    slots, seen = [], set()
    for l in olines:
        a = [x.strip() for x in l.split("|")]
        if len(a) != 7:
            print("REFUSED: order line %r is not tag|exe|save|warm|frames|slow_ms|arm" % l)
            return 2
        tag, exe, save, warm, frames, slow, arm = a
        if tag != tag.lower() and on("lowercase"):
            print("REFUSED: tag %s is not lowercase" % tag)
            return 2
        if arm not in ARMS:
            print("REFUSED: arm %r of %s is not one of %s" % (arm, tag, ", ".join(ARMS)))
            return 2
        if tag in seen and on("dup_tag"):
            print("REFUSED: tag %s listed twice" % tag)
            return 2
        seen.add(tag)
        try:
            slots.append(dict(tag=tag, exe=exe, save=save, warm=int(warm), frames=int(frames), slow=float(slow), arm=arm))
        except ValueError:
            print("REFUSED: order line %r has a non-number in warm/frames/slow_ms" % l)
            return 2
    if not any(s["arm"] == "on" for s in slots) or not any(s["arm"] == "off" for s in slots):
        print("REFUSED: the order file needs both on and off loads")
        return 2
    o = []
    P = o.append
    P("wu1 alife_warmup lever (PREREG-wu1-alife-warmup)")
    P("scorer %s sha256 %s" % (os.path.abspath(__file__), sha(os.path.abspath(__file__))))
    P("order  %s sha256 %s" % (os.path.abspath(args["order"]), sha(args["order"])))
    P("root   %s" % os.path.abspath(args["root"]))
    P("constants window %.0f s (headline; %.0f s reported only), slow %.1f/%.1f ms, H1 worst ratio %.2f, H2 n20 drop >= %d, H3 price <= %.1f s, "
      "min ON %d, min OFF-IN-PLAY %d, stock hitch >= %.0f ms, stock train >= %d, legacy wide %d, N/M >= %.2f, P >= %d"
      % (WINDOW_MS / 1000, WINDOW2_MS / 1000, SLOW1_MS, SLOW2_MS, WORST_RATIO, N20_DROP, PRICE_MAX_S, MIN_ON, MIN_OFFP, STOCK_HITCH_MIN,
         STOCK_TRAIN_MIN, LEGACY_WIDE, N_M_MIN, P_MIN))
    P("")
    loads = []
    for s in slots:
        if s["arm"] == "prime":
            P("LOAD %s prime: not scored" % s["tag"])
            continue
        R = score_load(args["root"], s)
        loads.append(R)
        state = "FAIL" if R["fatal"] else ("VOID" if R["void"] else "VALID")
        reasons = [r.split(":")[0] for r in R["fatal"] + R["void"]]
        head = "LOAD %s %s %s%s" % (s["tag"], s["arm"].upper(), state, (" [" + "] [".join(reasons) + "]") if reasons else "")
        if "w1" in R:
            w, w2 = R["w1"], R["w2"]
            head += (" group=%s worst=%.1f n20=%d n33=%d span=%.2fs worst_incl_save=%.1f save=%s | 15s: worst=%s n20=%s "
                     "n33=%s span=%s | load=%s s" % (R["group"], w["worst"], w["n20"], w["n33"], w["span"], w["worst_all"],
                                                    R["save"], fmt(w2["worst"]) if w2["covered"] else "n/a",
                                                    w2["n20"] if w2["covered"] else "n/a",
                                                    w2["n33"] if w2["covered"] else "n/a",
                                                    ("%.2fs" % w2["span"]) if w2["covered"] else "n/a",
                                                    fmt(R["lt"], "%.2f")))
        P(head)
        for r in R["fatal"] + R["void"]:
            P("    %s" % r)
        if "w1" in R:
            if R["group"] != "ON":
                P("    order: %s; wide passes logged %d; legacy %s" % (R["why"], R["wide"], R["legacy"]))
            for n in R["notes"]:
                P("    %s" % n)
            P("    load time: %s" % (R["lt_why"]))
    # ---------------- verdict (prereg 7) ----------------
    fails = [(R["tag"], r) for R in loads for r in R["fatal"]]
    valid = [R for R in loads if not R["fatal"] and not R["void"]]
    on_v = [R for R in valid if R["group"] == "ON"]
    offv = [R for R in valid if R["arm"] == "off"]
    offP = [R for R in offv if R["group"] == "OFF-IN-PLAY"] if on("grouping") else list(offv)
    offL = [R for R in offv if R["group"] == "OFF-IN-LOADING"]
    offO = [R for R in offv if R["group"] == "OFF-OTHER"]
    P("")
    P("GROUPS (valid loads; the two stock groups are never pooled): ON %d, OFF-IN-PLAY %d, OFF-IN-LOADING %d, "
      "OFF-OTHER %d (reported only); void %d; failed %d"
      % (len(on_v), len(offP), len(offL), len(offO), sum(1 for R in loads if R["void"] and not R["fatal"]),
         sum(1 for R in loads if R["fatal"])))

    def stats(G, key):
        v = [key(R) for R in G]
        v = [x for x in v if x is not None]
        if not v:
            return None, "n/a"
        return median(v), "median %.2f min %.2f max %.2f n %d" % (median(v), min(v), max(v), len(v))

    M = {}
    for name, G in (("ON", on_v), ("OFF-IN-PLAY", offP), ("OFF-IN-LOADING", offL), ("OFF-OTHER", offO)):
        for k, f in (("worst_ms", lambda R: R["w1"]["worst"]), ("n20", lambda R: R["w1"]["n20"]),
                     ("n33", lambda R: R["w1"]["n33"]), ("train_span_s", lambda R: R["w1"]["span"]),
                     ("worst_incl_save_ms", lambda R: R["w1"]["worst_all"]),
                     ("worst15_ms", lambda R: R["w2"]["worst"] if R["w2"]["covered"] else None),
                     ("n20_15", lambda R: R["w2"]["n20"] if R["w2"]["covered"] else None),
                     ("n33_15", lambda R: R["w2"]["n33"] if R["w2"]["covered"] else None),
                     ("load_s", lambda R: R["lt"]), ("pass_ms", lambda R: R.get("pass_ms")),
                     ("held_frames", lambda R: R.get("held"))):
            M[(name, k)], txt = stats(G, f)
            if txt != "n/a" or name != "OFF-OTHER":
                P("  %-15s %-19s %s" % (name, k, txt))
    final, code = "", 0
    if fails:
        P("")
        for t, r in fails:
            P("FAIL %s %s" % (t, r))
        final, code = "FAIL (%d load(s) failed a registered check: %s)" % (
            len(set(t for t, _ in fails)), ", ".join(sorted(set(r.split(":")[0] for _, r in fails)))), 1
    else:
        nv = []
        if on("nv_few_on") and len(on_v) < MIN_ON:
            nv.append("few_on: %d valid ON loads < %d" % (len(on_v), MIN_ON))
        if on("nv_few_inplay") and len(offP) < MIN_OFFP:
            nv.append("few_inplay: %d valid OFF-IN-PLAY loads < %d (stock took the in-play order too rarely)" % (len(offP), MIN_OFFP))
        if offP:
            if on("nv_no_hitch") and M[("OFF-IN-PLAY", "worst_ms")] < STOCK_HITCH_MIN:
                nv.append("no_stock_hitch: median OFF-IN-PLAY worst frame %.1f ms < %.0f ms: the %.0f s window did not catch "
                          "stock's in-play hitch" % (M[("OFF-IN-PLAY", "worst_ms")], STOCK_HITCH_MIN, WINDOW_MS / 1000))
            if on("nv_no_train") and M[("OFF-IN-PLAY", "n20")] < STOCK_TRAIN_MIN:
                nv.append("no_stock_train: median OFF-IN-PLAY frames > 20 ms %.1f < %d: the %.0f s window did not catch "
                          "stock's train" % (M[("OFF-IN-PLAY", "n20")], STOCK_TRAIN_MIN, WINDOW_MS / 1000))
        if offP and len(offL) >= ALIKE_MIN_OFFL and on("nv_alike"):
            wl, nl = M[("OFF-IN-LOADING", "worst_ms")], M[("OFF-IN-LOADING", "n20")]
            if wl > WORST_RATIO * M[("OFF-IN-PLAY", "worst_ms")] or M[("OFF-IN-PLAY", "n20")] - nl < N20_DROP:
                nv.append("stock_orders_alike: in the %.0f s window stock's good order (OFF-IN-LOADING worst %.1f ms, n20 %.1f) is "
                          "not told apart from its in-play order (worst %.1f ms, n20 %.1f): the window cannot show the lever"
                          % (WINDOW_MS / 1000, wl, nl, M[("OFF-IN-PLAY", "worst_ms")], M[("OFF-IN-PLAY", "n20")]))
        lt_on = sum(1 for R in on_v if R["lt"] is not None)
        lt_p = sum(1 for R in offP if R["lt"] is not None)
        if on("nv_loadtime") and (lt_on < LOADTIME_MIN_ON or lt_p < LOADTIME_MIN_OFFP):
            nv.append("loadtime: load time measurable in %d ON (need %d) and %d OFF-IN-PLAY (need %d) loads"
                      % (lt_on, LOADTIME_MIN_ON, lt_p, LOADTIME_MIN_OFFP))
        P("")
        if nv:
            for r in nv:
                P("NO VERDICT [%s]" % r)
            final, code = "NO VERDICT (%s)" % ", ".join(r.split(":")[0] for r in nv), 3
        else:
            wo, wp = M[("ON", "worst_ms")], M[("OFF-IN-PLAY", "worst_ms")]
            stray = sum(1 for R in on_v if R["w1"]["worst"] >= wp)
            h1a = wo <= WORST_RATIO * wp or not on("h1_median")
            h1b = stray <= WORST_STRAY or not on("h1_max")
            h1 = h1a and h1b
            no, np_ = M[("ON", "n20")], M[("OFF-IN-PLAY", "n20")]
            h2 = np_ - no >= N20_DROP
            price = M[("ON", "load_s")] - M[("OFF-IN-PLAY", "load_s")]
            h3 = price <= PRICE_MAX_S
            h2 = h2 or not on("h2")
            h3 = h3 or not on("h3")
            P("H1 worst frame (%.0f s): median ON %.1f ms <= %.2f x median OFF-IN-PLAY %.1f ms = %.1f (%s), and ON loads with "
              "worst >= %.1f ms: %d <= %d (%s) -> %s" % (WINDOW_MS / 1000, wo, WORST_RATIO, wp, WORST_RATIO * wp, "ok" if h1a else "FAIL", wp,
                                                       stray, WORST_STRAY, "ok" if h1b else "FAIL", "ok" if h1 else "FAIL"))
            P("H2 frames > 20 ms (%.0f s): median OFF-IN-PLAY %.1f - median ON %.1f = %.1f >= %d -> %s"
              % (WINDOW_MS / 1000, np_, no, np_ - no, N20_DROP, "ok" if h2 else "FAIL"))
            P("H3 load-time price: median ON %.2f s - median OFF-IN-PLAY %.2f s = %+.2f s <= %.1f s -> %s"
              % (M[("ON", "load_s")], M[("OFF-IN-PLAY", "load_s")], price, PRICE_MAX_S, "ok" if h3 else "FAIL"))
            if h1 and h2 and h3:
                final, code = "PASS", 0
            else:
                final, code = "FAIL (%s)" % ", ".join(n for n, h in (("H1", h1), ("H2", h2), ("H3", h3)) if not h), 1
        # secondary, reported only
        if len(offL) >= SAME_MIN_OFFL and on_v and M[("ON", "load_s")] is not None and M[("OFF-IN-LOADING", "load_s")] is not None:
            a = M[("ON", "worst_ms")] <= SAME_WORST_X * M[("OFF-IN-LOADING", "worst_ms")] + SAME_WORST_ADD
            b = M[("ON", "n20")] <= M[("OFF-IN-LOADING", "n20")] + SAME_N20_ADD
            c = abs(M[("ON", "load_s")] - M[("OFF-IN-LOADING", "load_s")]) <= SAME_PRICE_S
            P("SAME AS GOOD ORDER (reported only): worst %s, n20 %s, load %+.2f s %s -> %s"
              % ("ok" if a else "worse", "ok" if b else "worse", M[("ON", "load_s")] - M[("OFF-IN-LOADING", "load_s")],
                 "ok" if c else "differs", "SAME" if a and b and c else "DIFFERENT"))
        else:
            P("SAME AS GOOD ORDER (reported only): NO VERDICT (%d valid OFF-IN-LOADING loads, need %d, or load time missing)"
              % (len(offL), SAME_MIN_OFFL))
    P("")
    P("WU1 RESULT: " + final)
    with open(out, "x") as f:
        f.write("\n".join(o) + "\n")
    print(o[-1])
    print("written %s" % out)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
