#!/usr/bin/env python3
# wu3 scorer: does the engine lever -alife_warmup remove the slow frames after a level load?
# (PREREG-wu3-alife-warmup-2026-11-27.md, DRAFT, not registered). Stdlib only; run with python3 -I.
# wu3 = wu2 (PREREG-wu2-alife-warmup-2026-11-26.md, scorer sha 69cd8112...) with ONE change: how a load that the bench
# marks not clean is handled. Every per-load rule, constant, window and hypothesis is wu2's.
#   - wu2: no re-run; the 3rd non-clean load stopped the series; FAIL stopped_series.
#   - wu3: a scored slot <tag> whose load is not clean is run ONCE more as <tag>_r1 (driver wu3_run.sh), at most R_MAX
#     re-runs in the series. The re-run is scored in the slot's place with every per-load rule; not clean twice = the
#     slot is VOID. The driver writes <state>/stop.txt "class=<done|box|fault> reason=<text>" on every exit; a series
#     the driver stopped for a box reason (not quiet, budget, slice time, ...) is NO VERDICT box_not_quiet, a fault stop
#     is FAIL (prereg wu3 section 3).
#
#   python3 -I wu3_score.py root=<folder holding the load folders> order=<wu3_order.txt> stop=<state>/stop.txt out=<new report file>
#   python3 -I wu3_score.py parse=<xray.log>        (log parser only, prints what it found; used by the self-test)
#   python3 -I wu3_score.py probe=<out/<prime tag>> order=<wu3_order.txt>   (driver: after the PRIME load, before any
#        scored load: does the real log hold every line the scorer needs, and is the end of loading found? exit 0 yes,
#        1 no = stop the series)
#   python3 -I wu3_score.py check=<out/<tag> or out/<tag>_r1> order=<wu3_order.txt>   (driver: after each scored load
#        and each re-run: the same per-load rules as scoring; exit 1 = a FAIL or a stop-VOID, the driver stops the series)
#
# Order file: "tag|exe|save|warm|frames|slow_ms|arm" per line, '#' = comment; arm on / off / prime (prime is not read).
# Load folder <root>/<tag>/ (a re-run: <root>/<tag>_r1/, its files named with <tag>_r1): perf_frames_<tag>.csv,
# perf_sites_<tag>.csv, perf_slow_<tag>.csv, conditions.txt, xray.log.
# Every file is read with CR stripped (engine output is CRLF). A missing, empty or unparsable file is a FAIL, by name.
# The report is written to out= in this same call; an existing out file is refused, never overwritten.
# Exit: 0 PASS, 1 FAIL, 3 NO VERDICT, 2 refused (usage, order file, duplicate or uppercase tag, out exists).
# A load the scorer cannot read (any exception) is a named FAIL; the report is always written.
# Every rule and constant is the registered one (prereg sections 4-7); nothing is read from the environment.
import hashlib
import math
import os
import re
import sys

# ---- registered constants (prereg section 6, derivation beside each there) ----
WINDOW_MS = 30000.0       # headline window: first 30 s of play after the end of loading (wu2: from capture frame E, 5.1)
WINDOW2_MS = 15000.0      # the supervisor's ~15 s window, reported only beside the headline
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
WORST_RATIO = 0.5         # H1: median ON worst <= 0.5 x median OFF-IN-PLAY worst ...
WORST_STRAY = 1           #     ... and at most 1 ON load with worst >= median OFF-IN-PLAY worst
N20_DROP = 7              # H2: median OFF-IN-PLAY frames > 20 ms - median ON frames > 20 ms >= 7 (30 s)
ALIKE_MIN_OFFL = 2        # stock's two orders must differ in the window (checked when >= 2 OFF-IN-LOADING loads)
PRICE_MAX_S = 2.0         # H3: median ON - median OFF-IN-PLAY IN-CAPTURE loading time (capture 0..E-1, frames file) <= 2.0 s
SAME_WORST_X = 1.25       # secondary (reported only): ON "same as stock's good order" if median ON worst
SAME_WORST_ADD = 10.0     #   <= 1.25 x median OFF-IN-LOADING worst + 10 ms,
SAME_N20_ADD = 2          #   median ON n20 <= median OFF-IN-LOADING n20 + 2,
SAME_PRICE_S = 1.0        #   and |median ON - median OFF-IN-LOADING in-capture loading time| <= 1.0 s
SAME_MIN_OFFL = 2
SLOW_CAP_ROWS = 600 * 14 - 14   # profiler_agg.cpp:42-43, 190: a slow frame is kept only while rows + 14 <= 8400
# ---- wu2: the end of loading inside the capture (prereg 5.1, 5.4, 6) ----
CLOCK_TOL_MS = 15.0       # |time_global difference - sum of captured frame ms| between two clock lines (6: 3.5x the
                          #   4.27 ms max seen over 23 pairs in wu1_prime; a 1-frame shift in the loading block: >= 1213 ms)
RACE_FRAMES = 1           # a countdown line may read precache one lower (A-Life thread reads after End(): 5.4)
SLOW_MS_EQ = 0.0005       # perf_slow frame_ms = perf_frames ms of that frame (same ticks, same scale, 4 decimals)
EV_SINGLE = ("CLevel::net_start1", "CLevel::net_start2", "CLevel::net_start3", "CLevel::net_start4",
             "CLevel::net_start5", "CLevel::net_start6", "CLevel::net_start_client1", "CLevel::net_start_client2",
             "CLevel::net_start_client3", "CLevel::net_start_client4", "CLevel::net_start_client5")
EV_RETRY = "CLevel::net_start_client6"   # repeated while synchronize_map_data() is false (Level_network_start_client.cpp:244-245)
EV_FRAME1 = "CALifeSimulator::CALifeSimulator"   # built in net_start2, the 2nd loading event: always > 20 ms (5.7 s here)
INTRO_SITE = "CGamePersistent OnFrame"    # intro_start's frame (reported only: lua GC after intro_start, 52 ms in wu1_prime)
VERDICTS = ("VERDICT: clean", "VERDICT: CONFOUNDED (other heavy process present)",
            "VERDICT: functional run, timings are not measurements")   # the only three tools/s2_bench.sh:72-74 writes
STOP_VOIDS = ("framecount", "capture_lines", "stock_loop", "frame_limiter", "threshold_header",
              "loading_frames", "clock_fit", "loading_end", "clock_order", "frames_slow")   # check=: driver stops
CLOCK_BACK_MS = 1         # clock_order: a value of one clock kind may be at most this far below an earlier one of the
                          #   SAME kind in log order (both are frame samples that only go forward by code: prereg 4.3)
MEASURE_FLAGS = ("-perf_nopresent", "-perf_noinput")   # registered measurement flags (prereg 2): absent = arm_mismatch
# ---- wu3: re-runs and the stop record (prereg wu3 section 3) ----
R_MAX = 8                 # wu3_run.sh R_MAX (the self-test checks the two agree): re-runs allowed in the whole series (one
                          #   per scored slot at most); more <tag>_r1 folders than this is FAIL rerun_budget
VOID_ARM_DIFF = 4         # reported only: NOTE void_arms_differ when the ON and OFF void-slot counts differ by more
STOP_CLASSES = ("done", "box", "fault")
RERUN_RE = re.compile(r"^(.+)_r(\d+)$")
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


def num(t):
    """float that must be finite (inf / nan in a timing field are UNPARSABLE, never a number)."""
    v = float(t)
    if on("finite") and not math.isfinite(v):
        raise ValueError(t)
    return v


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


def lines_of(path, keep_empty=False):
    """keep_empty (xray.log): empty lines are kept, so list index + 1 is the file's own line number."""
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
        if l != "" or keep_empty:
            out.append(l)
    if not [l for l in out if l != ""]:
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
            v = num(a[1])
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
            num(a[4]); num(a[5]); num(a[7]); num(a[8])
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
        thr = num(L[0].split(",")[1])
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
            rows.append((int(a[0]), num(a[1]), a[2], num(a[3]), a[5]))
        except ValueError:
            raise Bad("unparsable", "UNPARSABLE %s row %r" % (p, l))
    return thr, rows


def read_verdict(d):
    p = os.path.join(d, "conditions.txt")
    v = [l.strip() for l in lines_of(p) if l.startswith("VERDICT")]
    if not v:
        raise Bad("unparsable", "UNPARSABLE %s has no VERDICT line" % p)
    if on("verdict_form") and (len(v) != 1 or v[0] not in VERDICTS):
        raise Bad("unparsable", "UNPARSABLE %s VERDICT line(s) %r are not one of the bench's three forms" % (p, v))
    return v[0]


# ---------------- xray.log ----------------
RE_FULL = re.compile(r"^\* \[alife_warmup\] full pass: (\d+) of (\d+) objects, frame (\d+), precache_left (\d+), ([0-9.]+) ms$")
RE_HELD = re.compile(r"^\* \[alife_warmup\] precache held (\d+) frames$")
RE_CAP = re.compile(r"^! \[alife_warmup\] hold cap hit after (\d+) frames, pass not run$")
RE_PASS = re.compile(r"^\* alife_pass frame (\d+) time_global (\d+) precache (\d+) n (\d+) of (\d+) limit (\d+)$")
RE_OPU = re.compile(r"^\* alife_opu frame (\d+) time_global (\d+) precache (\d+) value (\d+)$")
RE_SLOW = re.compile(r"^\* alife_slow frame (\d+) id (\d+) .* ([0-9.]+) ms$")
RE_TCONT = re.compile(r"^Time continual is:(\d+)$")   # mcm_log stamp: dwTimeContinual (TimerMM, device.cpp:1051)
INTRO = "intro_start game_loaded"
HANDLED = "* [x-ray]: Handled Necessary Textures Destruction"


def parse_log(path):
    L = lines_of(path, keep_empty=True)
    try:
        return parse_lines(path, L)
    except ValueError as e:
        if on("log_num"):
            raise Bad("unparsable", "UNPARSABLE %s line %d %r (not a finite number)" % (path, e.args[1] + 1, e.args[2]))
        raise


def lnum(t, i, l):
    try:
        return num(t)
    except ValueError:
        raise ValueError(t, i, l)


def parse_lines(path, L):
    g = dict(cmd=None, intro=None, handled=None, cap_start={}, cap_fin={}, full=[], held=[], cap=[], passes=[],
             opus=[], slows=[], tcont=[], n_lines=len(L))
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
        m = RE_TCONT.match(l)
        if m:
            g["tcont"].append(dict(i=i, T=int(m.group(1))))
            continue
        if "[alife_warmup]" in l:
            m = RE_FULL.match(l)
            if m:
                g["full"].append(dict(i=i, N=int(m.group(1)), M=int(m.group(2)), F=int(m.group(3)), P=int(m.group(4)),
                                      X=lnum(m.group(5), i, l)))
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
                                    M=int(m.group(5)), lim=int(m.group(6)), k="pass"))
            continue
        if l.startswith("* alife_opu "):
            m = RE_OPU.match(l)
            if not m:
                raise Bad("unparsable", "UNPARSABLE %s line %d %r" % (path, i + 1, l))
            g["opus"].append(dict(i=i, F=int(m.group(1)), T=int(m.group(2)), P=int(m.group(3)), V=int(m.group(4)), k="opu"))
            continue
        if l.startswith("* alife_slow "):
            m = RE_SLOW.match(l)
            if not m:
                raise Bad("unparsable", "UNPARSABLE %s line %d %r" % (path, i + 1, l))
            g["slows"].append(dict(i=i, F=int(m.group(1)), ms=lnum(m.group(3), i, l)))
            continue
    if g["cmd"] is None:
        raise Bad("unparsable", "UNPARSABLE %s has no 'command line' line" % path)
    if g["intro"] is None:
        raise Bad("unparsable", "UNPARSABLE %s has no '%s' line (loading never ended)" % (path, INTRO))
    if g["handled"] is None:
        raise Bad("unparsable", "UNPARSABLE %s has no '%s' line" % (path, HANDLED))
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
        g["legacy"] = "W"
    return g


def classify_off(g):
    """Prereg 4.2 (unchanged from wu1). Returns (group, reason). group: P (in-play), L (in-loading), OTHER."""
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


# ---------------- wu2: the end of loading inside the capture (prereg 5.1, 5.4) ----------------
def loading_layout(ms, N, calls, rows, g, tag):
    """Where loading, precache and play lie in the capture, and the engine clock at the end of loading.
    Returns dict: void (list of named VOIDs), E (first play frame, capture index) or None, and the numbers printed.
    Code facts (prereg 5.1): every captured frame is one on_idle; an on_idle with a pending loading event runs that one
    event and returns before FrameMove (device.cpp:693-704), so it has no seqFrame and no time_global update (its
    LoadDraw may bump dwFrame, x_ray.cpp:1590-1591: not relied on, the offset D below is fitted on FrameMove frames
    only, where dwFrame moves once a frame, device.cpp:1048); the 12 events of a save load run in a fixed order (Level_start.cpp:95-100, 221-235: net_start1..4, then
    net_start_client1..6 pushed to the front, then net_start5..6), net_start_client6 repeated while the map sync is not
    done; the precache countdown is counted down by End() (device.cpp:157-185) and "Handled Necessary Textures
    Destruction" is printed in the End() that reaches 0; 'Precache frame' (device.cpp:773) is called once in every
    FrameMove frame with the countdown > 0, 'Start xrSASH Benchmark' (device.cpp:709) once in every FrameMove frame with
    it at 0 before the frame."""
    R = dict(void=[], E=None, L=None, Pc=None, D=None, lt=None, lt_why="", info=[], est=[], fit=None, fit2=None, cross=None)
    c = lambda n: calls.get(n, [0, 0])[0]
    # ---- (a) loading frames: capture frames 0 .. L-1, proven by the event counts and the frame-1 marker ----
    L, Pc, c6 = c("Loading..."), c("Precache frame"), c(EV_RETRY)
    R["L"], R["Pc"] = L, Pc
    lbad = []
    if on("loading_frames"):
        if L != c("LoadDraw"):
            lbad.append("'Loading...' %d calls != 'LoadDraw' %d" % (L, c("LoadDraw")))
        once = [n for n in EV_SINGLE if c(n) != 1]
        if once:
            lbad.append("loading events not called exactly once: %s" % ", ".join("%s %d" % (n, c(n)) for n in once))
        if c6 < 1 or L != len(EV_SINGLE) + c6:
            lbad.append("'Loading...' %d calls != %d single events + %d %s calls" % (L, len(EV_SINGLE), c6, EV_RETRY))
        f1 = sorted(set(r[0] for r in rows if r[4] == EV_FRAME1))
        if f1 != [1]:
            lbad.append("perf_slow names %s in capture frame(s) %s, required exactly frame 1 (the 2nd loading event)"
                        % (EV_FRAME1, f1 or "none"))
        if Pc < 1 or c("Start xrSASH Benchmark") != N - L - Pc:
            lbad.append("'Precache frame' %d + 'Start xrSASH Benchmark' %d + loading %d != frames %d"
                        % (Pc, c("Start xrSASH Benchmark"), L, N))
    if lbad:
        R["void"].append("loading_frames: " + "; ".join(lbad))
        return R
    E = L + Pc
    if E >= len(ms):
        R["void"].append("loading_frames: end of loading %d not inside the %d captured frames" % (E, len(ms)))
        return R
    R["E"] = E
    pre = [0.0]
    for v in ms:
        pre.append(pre[-1] + v)
    R["pre"] = pre
    # ---- (b) the clock lines placed in the capture: dwFrame = capture index + D, D found from the times alone ----
    fin = g["cap_fin"].get(tag)
    cl = sorted([x for x in g["passes"] + g["opus"] if fin is None or x["i"] < fin[0]], key=lambda x: (x["F"], x["i"]))
    if len(cl) < 2:
        R["void"].append("clock_fit: %d clock line(s) (alife_pass / alife_opu) inside the capture, need 2" % len(cl))
        return R
    Fmin, Fmax = cl[0]["F"], cl[-1]["F"]
    fits = []
    for D in range(Fmax - (len(ms) - 1), Fmin - L + 1):   # every clock line in a FrameMove frame of the capture
        worst = 0.0
        for a, b in zip(cl, cl[1:]):
            r = abs((b["T"] - a["T"]) - (pre[b["F"] - D] - pre[a["F"] - D]))
            if r > worst:
                worst = r
                if fits and len(fits) >= 2 and worst > max(fits[0][0], fits[1][0]):
                    break
        fits.append((worst, D))
        fits.sort()
        fits = fits[:2]
    if not fits:
        R["void"].append("clock_fit: clock lines span frames %d..%d, more than the capture's play/precache frames" % (Fmin, Fmax))
        return R
    (r1, D), r2 = fits[0], (fits[1][0] if len(fits) > 1 else float("inf"))
    R["D"], R["fit"], R["fit2"] = D, r1, (r2, fits[1][1] if len(fits) > 1 else None)
    R["ncl"] = len(cl)
    if on("clock_fit") and r1 > CLOCK_TOL_MS:
        R["void"].append("clock_fit: no device-frame offset puts the clock lines on the captured frames: best D=%d leaves "
                         "%.1f ms > %.0f ms between time_global and the frame ms" % (D, r1, CLOCK_TOL_MS))
        return R
    if on("clock_unique") and r2 <= CLOCK_TOL_MS:
        R["void"].append("clock_fit: ambiguous: D=%d (%.1f ms) and D=%d (%.1f ms) both fit within %.0f ms"
                         % (D, r1, fits[1][1], r2, CLOCK_TOL_MS))
        return R
    cap = lambda x: x["F"] - D
    # ---- (c) the countdown lines must say the same end of loading as the engine's own counters ----
    K = sum(h["K"] for h in g["held"]) + sum(x["K"] for x in g["cap"])
    est = []
    for x in cl:
        if x["P"] == 0 or (K and x["P"] == 3 and on("held_k")):
            continue
        e = cap(x) + x["P"] + (K if (K and x["P"] > 3 and on("held_k")) else 0)
        est.append((x, e))
    R["est"], R["K"] = est, K
    if on("loading_end"):
        if not est:
            R["void"].append("loading_end: no clock line during the precache countdown (precache > 0%s)"
                             % (", not 3 in a hold" if K else ""))
            return R
        off = [(x, e) for x, e in est if not E - RACE_FRAMES <= e <= E]
        if off:
            R["void"].append("loading_end: engine counters put the first play frame at capture %d, countdown line(s) at %s"
                             % (E, ", ".join("frame %d precache %d -> %d" % (x["F"], x["P"], e) for x, e in off[:4])))
            return R
    # ---- (d) log order: the engine's loading-end line between the clock lines; the clocks never go back ----
    before = [cap(x) for x in cl if x["i"] < g["handled"]]
    after = [cap(x) for x in cl if x["i"] > g["handled"]]
    R["bracket"] = (max(before) if before else None, min(after) if after else None)
    ob = []
    if on("handled_bracket"):
        if before and max(before) > E - 1:
            ob.append("a clock line of capture frame %d is logged before the loading-end line (printed in frame %d)"
                      % (max(before), E - 1))
        if after and min(after) < E - 2:
            ob.append("a clock line of capture frame %d is logged after the loading-end line (printed in frame %d)"
                      % (min(after), E - 1))
    # Each clock kind alone never goes back in log order (stop). Code (wt-simtick d77d70b5c): dwTimeGlobal and
    # dwTimeContinual are written only in FrameMove (device.cpp:1051, 1112); every alife_pass line copies them at the
    # start of its update and is logged at its end inside the GameThread task, which the main thread joins at
    # device.cpp:918 before the next on_idle; alife_opu and the Lua stamp read them live in the same frame. So in log
    # order each kind is a run of frame samples that only goes forward (TimerGlobal: frozen while paused, never back;
    # dwTimeContinual goes back only at an app re-activation, device.cpp:1248, after a deactivation that pauses the game,
    # GamePersistent.cpp:959). A wide or full pass and held frames only make an update longer inside its own frame.
    # Across kinds (stamp vs time_global) the gap is TimerMM - TimerGlobal: 0 by code (Device_Initialize.cpp:40-41)
    # unless the timer was paused (device.cpp:1153-1155), the app deactivated, or time_factor changed (device.h:363-367);
    # whether a normal load does any of these is not settled by the source (a UI sequence's pause flag, a script's
    # time_factor): REPORTED only (clock_cross), never a stop. MEASURED-PRIME: 0 ms in all 3 same-frame pairs.
    seqs = {"time_global": sorted((x["i"], x["T"]) for x in g["passes"] + g["opus"]),
            "Time continual": sorted((x["i"], x["T"]) for x in g["tcont"])}
    if on("clock_monotone"):
        for kind in ("time_global", "Time continual"):
            hi = None
            for i, T in seqs[kind]:
                if hi is not None and T < hi[0] - CLOCK_BACK_MS:
                    ob.append("log line %d %s %d after log line %d %s %d: %s goes back" % (i + 1, kind, T, hi[1] + 1, kind, hi[0], kind))
                    break
                if hi is None or T > hi[0]:
                    hi = (T, i)
    if ob:
        R["void"].append("clock_order: " + "; ".join(ob))
        return R
    if on("clock_cross"):
        seq = sorted([(i, T, "time_global") for i, T in seqs["time_global"]] + [(i, T, "Time continual") for i, T in seqs["Time continual"]])
        hi, worst = None, None
        for i, T, kind in seq:
            if hi is not None and T < hi[0] - CLOCK_BACK_MS and (worst is None or hi[0] - T > worst[0]):
                worst = (hi[0] - T, i, kind, T, hi)
            if hi is None or T > hi[0]:
                hi = (T, i, kind)
        if worst:
            d, i, kind, T, h = worst
            R["cross"] = ("clock_cross (reported only): log line %d %s %d after log line %d %s %d: %d ms back across the two "
                          "clocks (a timer pause, app deactivation or time_factor change, or a shifted time_global)"
                          % (i + 1, kind, T, h[1] + 1, h[2], h[0], d))
    # ---- (e) engine time at the first play frame (load time, 5.4): an ESTIMATE ----
    # A clock line of capture frame c holds time_global read at device.cpp:1112, o_c ms after frame c's PROF_FRAME
    # (device.cpp:675; the frames file's ms run PROF_FRAME to PROF_FRAME on the same QPC clock, profiler_agg.cpp:119-121),
    # so 0 <= o_c <= ms[c]. The estimate is the start of frame E + o_c; the engine's own value at E is that + o_E. Error
    # = o_c - o_E, in [-ms[E], +ms[c]], plus 1 ms of integer time_global; valid only with no timer pause between c and E.
    # The clock fit does not narrow it (it compares offsets at clock-line frames only, never at E).
    aft = [x for x in cl if cap(x) >= E]
    bef = [x for x in cl if cap(x) < E]
    tA = (aft[0]["T"] - (pre[cap(aft[0])] - pre[E])) if aft else None
    tB = (bef[-1]["T"] + (pre[E] - pre[cap(bef[-1])])) if bef else None
    t = tA if tA is not None else tB
    c0 = cap(aft[0]) if aft else cap(bef[-1])
    R["lt"] = t / 1000.0
    R["lt_bound"] = (ms[E] + 1.0, ms[c0] + 1.0)
    R["lt_why"] = "time_global %.0f ms at capture %d (%s%s; estimate: -%.1f / +%.1f ms = frame E %.1f ms / frame %d %.1f ms, + 1 ms)" % (
        t, E, ("from frame %d T %d minus %.1f ms" % (aft[0]["F"], aft[0]["T"], pre[cap(aft[0])] - pre[E])) if aft else
        ("from frame %d T %d plus %.1f ms" % (bef[-1]["F"], bef[-1]["T"], pre[E] - pre[cap(bef[-1])])),
        ("; from the last countdown line %.0f ms" % tB) if (aft and tB is not None) else "",
        ms[E] + 1.0, ms[c0] + 1.0, ms[E], c0, ms[c0])
    # ---- reported: where line HANDLED falls; the intro frame marker ----
    intro = sorted(set(r[0] for r in rows if r[4] == INTRO_SITE and L <= r[0] < E and r[3] >= SLOW1_MS))
    R["intro_marker"] = intro
    return R


def window(ms, excl, limit, start):
    """Frames from capture index start (the first play frame) that START before limit ms after it."""
    t = 0.0
    worst = worst_all = 0.0
    n1 = n2 = 0
    first = last_end = None
    i0 = start if on("window_from_e") else 0
    for i in range(i0, len(ms)):
        v = ms[i]
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
    return dict(covered=sum(ms[i0:]) >= limit, worst=worst, worst_all=worst_all, n20=n1, n33=n2, first=i0,
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
        # wu3 fix F: a VERDICT that was read stays in the not-clean counts when another file fails (still a FAIL)
        if on("verdict_kept") and "verdict" in got and got["verdict"] != "VERDICT: clean" and on("not_clean"):
            R["void"].append("not_clean: %r (wu3: a not-clean scored load is re-run once as <tag>_r1)" % got["verdict"])
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
    for need in MEASURE_FLAGS:
        if on("measure_flags") and need not in tok:
            arm_bad.append("%s absent (registered measurement flag)" % need)
    if ("-alife_warmup" in tok) != (s["arm"] == "on"):
        arm_bad.append("-alife_warmup %s on an %s load" % ("present" if "-alife_warmup" in tok else "absent", s["arm"].upper()))
    if on("arm_cmdline") and arm_bad:
        R["fatal"].append("arm_mismatch: " + "; ".join(arm_bad))
    if on("measure_lines") and (not g["passes"] or not g["opus"]):
        R["fatal"].append("no_measure_lines: %d alife_pass and %d alife_opu lines (each must be >= 1 in a Warm18 log)"
                          % (len(g["passes"]), len(g["opus"])))
    if s["arm"] in ("off", "prime") and on("off_not_stock") and (g["full"] or g["held"] or g["cap"]):
        R["fatal"].append("off_not_stock: %d [alife_warmup] line(s) in an OFF load" % (
            len(g["full"]) + len(g["held"]) + len(g["cap"])))
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
        R["void"].append("not_clean: %r (wu3: a not-clean scored load is re-run once as <tag>_r1)" % verdict)
    if (len(ms) != N or nfr != N) and on("framecount"):
        R["void"].append("framecount: frames file %d rows, sites header %d, registered %d" % (len(ms), nfr, N))
    cs, cf = g["cap_start"].get(tag), g["cap_fin"].get(tag)
    # wu2: in log order, capture started < textures handled (the end of loading, INSIDE the capture) < finished
    if on("capture_lines") and (cs is None or cf is None or cs[1] != N or cf[1] != N or
                                (on("capture_order") and not cs[0] < g["handled"] < cf[0])):
        R["void"].append("capture_lines: xrprof started/finished lines for '%s' with %d frames not found"
                         " (or not in the order started < loading end < finished)" % (tag, N))
    L = calls.get("Loading...", [0, 0])[0]
    if on("stock_loop"):
        sf = calls.get("Process seqFrame", [0, 0])[0]
        want = N - L if on("stock_loop_l") else N
        if sf != want:
            R["void"].append("stock_loop: 'Process seqFrame' main calls %d != frames %d - loading frames %d = %d"
                             % (sf, N, L, N - L))
        if calls.get("Sim tick", [0, 0])[1]:
            R["void"].append("stock_loop: 'Sim tick' has %d calls" % calls["Sim tick"][1])
    if on("frame_limiter") and calls.get("Eco Render", [0, 0])[1]:
        R["void"].append("frame_limiter: 'Eco Render' has %d calls (capped run)" % calls["Eco Render"][1])
    if on("threshold") and abs(thr - SLOW_HEADER_MS) > 0.0005:
        R["void"].append("threshold_header: perf_slow threshold %.3f, registered %.3f" % (thr, SLOW_HEADER_MS))
    # wu2: the slow rows and the frames file are the same frames (catches a frames file shifted against the profiler's)
    if on("frames_slow"):
        mis = [r for r in rows if not (0 <= r[0] < len(ms)) or abs(r[1] - ms[r[0]]) > SLOW_MS_EQ]
        if mis:
            r = mis[0]
            R["void"].append("frames_slow: %d perf_slow row(s) disagree with perf_frames (first: frame %d frame_ms %.4f, "
                             "frames file %s)" % (len(mis), r[0], r[1], ("%.4f" % ms[r[0]]) if 0 <= r[0] < len(ms) else "no such row"))
    # ---- wu2: end of loading inside the capture ----
    Y = loading_layout(ms, N, calls, rows, g, tag)
    R["layout"] = Y
    R["void"] += Y["void"]
    E = Y["E"] if Y["E"] is not None and not Y["void"] else None
    if E is None:
        return R
    if Y.get("cross"):
        R["notes"].append(Y["cross"])
    # ---- the scripted save (cordon1's start-of-game autosave) is excluded from the frame numbers ----
    if on("slow_cap") and len(rows) > SLOW_CAP_ROWS:
        R["void"].append("slow_cap: perf_slow has %d rows > %d: the profiler's 600-frame slow dump may be full, a save "
                         "frame could be missing from it" % (len(rows), SLOW_CAP_ROWS))
    save_frames = sorted(set(r[0] for r in rows if r[4] in SAVE_ZONES and 0 <= r[0] < len(ms)))
    save_calls = sum(calls.get(z, [0, 0])[1] for z in SAVE_ZONES)
    if on("save_unlocated") and save_calls and not save_frames:
        R["void"].append("save_unlocated: %d save-zone calls in the capture but no slow-frame row names them" % save_calls)
    R["save_frames"] = save_frames
    excl = set(save_frames) if on("save_exclude") else set()
    R["save"] = ", ".join("#%d %.0f ms" % (i, ms[i]) for i in save_frames) or "none in capture"
    R["w1"] = window(ms, excl, WINDOW_MS, E)
    R["w2"] = window(ms, excl, WINDOW2_MS, E)
    if on("window_short") and not R["w1"]["covered"]:
        R["void"].append("window_short: capture covers %.1f s after the end of loading < %.0f s"
                         % (sum(ms[E:]) / 1000.0, WINDOW_MS / 1000.0))
    R["lt"], R["lt_why"] = Y["lt"], Y["lt_why"]
    R["lc"] = Y["pre"][E] / 1000.0   # H3's measure (prereg 5.4): in-capture loading time, frames file (QPC wall clock)
    pre_slow = sum(1 for v in ms[:E] if v > SLOW1_MS)
    R["before"] = "before the window: %d frames (%d loading 0..%d, %d precache %d..%d), %.1f ms, %d of them > 20 ms, worst %.1f ms" % (
        E, Y["L"], Y["L"] - 1, Y["Pc"], Y["L"], E - 1, Y["pre"][E], pre_slow, max(ms[:E]))
    # ---- group ----
    if s["arm"] == "on":
        R["group"] = "ON"
    else:
        grp, why = classify_off(g)
        R["group"], R["why"] = {"P": "OFF-IN-PLAY", "L": "OFF-IN-LOADING", "OTHER": "OFF-OTHER"}[grp], why
    R["wide"] = sum(1 for p in g["passes"] if p["n"] > WIDE_N)
    R["legacy"] = "%s (max %d alife_slow lines in one frame before intro_start)" % (g["legacy"], g["slow_pre_max"])
    return R


def layout_lines(R, word, tag, g_handled_line=None):
    """The numbers the probe prints (and the report): located end of loading and the engine line's bracket."""
    Y = R.get("layout")
    out = []
    if not Y or Y.get("E") is None or "pre" not in Y:
        return out
    E, L, Pc = Y["E"], Y["L"], Y["Pc"]
    out.append("%s layout %s loading frames 0..%d (%d = %d single events + %d net_start_client6 calls; %s in frame 1), "
               "precache frames %d..%d (%d), first play frame E=%d" % (word, tag, L - 1, L, len(EV_SINGLE), L - len(EV_SINGLE),
                                                                       EV_FRAME1, L, E - 1, Pc, E))
    if Y.get("D") is not None:
        r2, D2 = Y["fit2"]
        out.append("%s clock %s D=%d (dwFrame = capture index + %d) from %d clock lines: max |time_global diff - frame ms sum| "
                   "%.2f ms (tolerance %.0f); next best D=%s leaves %s ms" % (
                       word, tag, Y["D"], Y["D"], Y.get("ncl", 0), Y["fit"], CLOCK_TOL_MS, D2,
                       ("%.1f" % r2) if r2 != float("inf") else "none"))
    if Y.get("est"):
        ec = max(e for _, e in Y["est"])
        out.append("%s loading_end %s located: first play frame E=%d by the engine's counters, %d by the countdown lines "
                   "(%s)%s; %.1f ms after capture start; engine time at E %s" % (
                       word, tag, E, ec, ", ".join("frame %d precache %d -> %d" % (x["F"], x["P"], e) for x, e in Y["est"]),
                       (" + %d held" % Y["K"]) if Y.get("K") else "", Y["pre"][E],
                       ("%.0f ms" % (Y["lt"] * 1000)) if Y.get("lt") is not None else "n/a"))
    if Y.get("bracket"):
        b, a = Y["bracket"]
        im = Y.get("intro_marker") or []
        out.append("%s handled %s engine line '%s'%s is printed in the End() of the last precache frame: capture %d "
                   "(engine counters) / %s (countdown lines); log order puts it after the clock line of capture %s and "
                   "before that of capture %s; intro marker (%s >= 20 ms) at capture %s (expected E-2 = %d)" % (
                       word, tag, HANDLED[2:], (" (log line %d)" % g_handled_line) if g_handled_line else "", E - 1,
                       (max(e for _, e in Y["est"]) - 1) if Y.get("est") else "n/a",
                       b if b is not None else "none", a if a is not None else "none", INTRO_SITE,
                       ",".join(str(x) for x in im) or "none", E - 2))
    return out


def score_safe(root, s):
    """score_load, but a load the scorer cannot read is a named FAIL (scorer_error), never an exit without the report."""
    try:
        return score_load(root, s)
    except Exception as e:  # noqa: BLE001 - deliberately every exception: the report must always be written
        return dict(tag=s["tag"], arm=s["arm"], fatal=["scorer_error: %s: %r" % (type(e).__name__, e)], void=[],
                    notes=[], group=None, why="", lt=None)


STOP_RE = re.compile(r"^class=(\S+)(?: interrupted=([a-z0-9_]+))? reason=(.*\S.*)$")


def read_stop(path):
    """wu3: the driver's stop record: exactly ONE physical line, ended by LF or CRLF:
    'class=<done|box|fault> [interrupted=<tag> ]reason=<text>'. interrupted= (fix B) is written only with class=box,
    when the stop came while a bench was live; it names that load's tag.
    Returns (class, reason, interrupted or None, None) or (None, None, None, why it is unusable)."""
    if not os.path.exists(path):
        return None, None, None, "missing: MISSING %s" % path
    try:
        with open(path, "rb") as f:
            raw = f.read().decode("ascii", "replace")
    except OSError as e:
        return None, None, None, "UNREADABLE %s (%s)" % (path, type(e).__name__)
    if raw == "":
        return None, None, None, "empty: EMPTY %s" % path
    # fix E: physical lines are counted; one line and its LF / CRLF end, nothing else (a blank line is a second line)
    body = raw[:-1] if raw.endswith("\n") else None
    if body is not None and body.endswith("\r"):
        body = body[:-1]
    if on("stop_lines"):
        if body is None:
            return None, None, None, "%s has no line end (the driver writes one line ended by LF)" % path
        n = body.count("\n") + 1
        if "\n" in body or "\r" in body:
            return None, None, None, "%s holds %d physical lines, the driver writes exactly one" % (path, n)
    else:
        body = [l for l in raw.replace("\r", "").split("\n") if l][0] if raw.strip() else ""
    m = STOP_RE.match(body)
    if not m or m.group(1) not in STOP_CLASSES or (m.group(2) and m.group(1) != "box"):
        return None, None, None, "UNPARSABLE %s %r (not 'class=<%s> [interrupted=<tag> ]reason=<text>', interrupted= only " \
                                 "with class=box)" % (path, body, "|".join(STOP_CLASSES))
    return m.group(1), m.group(3).strip(), m.group(2), None


def read_order(path):
    """Returns (slots, None) or (None, refusal text)."""
    try:
        olines = [l for l in lines_of(path) if not l.lstrip().startswith("#")]
    except Bad as e:
        return None, "REFUSED: order file: %s" % e
    slots, seen = [], set()
    for l in olines:
        a = [x.strip() for x in l.split("|")]
        if len(a) != 7:
            return None, "REFUSED: order line %r is not tag|exe|save|warm|frames|slow_ms|arm" % l
        tag, exe, save, warm, frames, slow, arm = a
        if tag != tag.lower() and on("lowercase"):
            return None, "REFUSED: tag %s is not lowercase" % tag
        if arm not in ARMS:
            return None, "REFUSED: arm %r of %s is not one of %s" % (arm, tag, ", ".join(ARMS))
        if RERUN_RE.match(tag) and on("rerun_tag"):
            return None, "REFUSED: tag %s ends in _r<number>, the name of a re-run folder (wu3)" % tag
        if tag in seen and on("dup_tag"):
            return None, "REFUSED: tag %s listed twice" % tag
        seen.add(tag)
        try:
            slots.append(dict(tag=tag, exe=exe, save=save, warm=int(warm), frames=int(frames), slow=float(slow), arm=arm))
        except ValueError:
            return None, "REFUSED: order line %r has a non-number in warm/frames/slow_ms" % l
    return slots, None


def one_load(folder, order, probe):
    """probe= / check= modes for the driver. Same rules as scoring (score_load), one load. Exit 0 = go on,
    1 = stop the series, 2 = refused. check=: stop on any FAIL and on the stop-VOIDs (STOP_VOIDS); not on not_clean
    (the driver counts those), window_short, save_unlocated, slow_cap.
    probe= (the PRIME load, never scored): also needs a save frame located and the whole 30 s headline window after E
    covered (window_short is a PROBE STOP); prints the located end of loading and the engine loading-end line's bracket
    as numbers."""
    slots, err = read_order(order)
    if err:
        print(err)
        return 2
    folder = folder.rstrip("/")
    tag = os.path.basename(folder)
    s = next((x for x in slots if x["tag"] == tag), None)
    m = RERUN_RE.match(tag)
    if s is None and m and on("r1_lookup"):
        # wu3: a re-run folder <slot>_r1 is checked with its slot's registered line, under its own tag (its files and
        # its -perf_capture tag are <slot>_r1). Only _r1 and only of a scored slot: anything else is refused.
        base = next((x for x in slots if x["tag"] == m.group(1)), None)
        if base is not None and m.group(2) == "1" and base["arm"] != "prime":
            s = dict(base, tag=tag)
    if s is None or (s["arm"] == "prime") != probe:
        print("REFUSED: %s is not %s load in %s" % (tag, "the prime" if probe else "a scored", order))
        return 2
    R = score_safe(os.path.dirname(folder) or ".", s)
    stop = list(R["fatal"]) + [v for v in R["void"] if v.split(":")[0] in STOP_VOIDS]
    if probe and on("probe_save") and "w1" in R and not R.get("save_frames"):
        stop.append("no_save_frame: no perf_slow row names %s inside the capture" % " / ".join(SAVE_ZONES))
    if probe and on("probe_save") and "w1" in R:
        stop += [v for v in R["void"] if v.split(":")[0] == "save_unlocated"]
    # the probe must establish that the capture covers the whole 30 s headline window after E (prereg 8)
    if probe and on("probe_window"):
        stop += [v for v in R["void"] if v.split(":")[0] == "window_short"]
    word = "PROBE" if probe else "CHECK"
    for r in stop:
        print("%s STOP %s %s" % (word, tag, r))
    for r in R["void"] + [n for n in R["notes"] if n.startswith("clock_cross")]:
        if r not in stop:
            print("%s note %s %s" % (word, tag, r))
    hl = None
    try:
        hl = parse_log(os.path.join(folder, "xray.log"))["handled"] + 1
    except Exception:  # noqa: BLE001 - only for the printed line number
        pass
    for l in layout_lines(R, word, tag, hl):
        print(l)
    if "w1" in R:
        w = R["w1"]
        print("%s window %s 30 s from capture %d: worst %.1f ms, n20 %d, n33 %d, span %.2f s (save excluded: %s); %s"
              % (word, tag, w["first"], w["worst"], w["n20"], w["n33"], w["span"], R["save"], R["before"]))
        print("%s info %s group=%s save=%s loading=%.2f s in capture (capture 0..%d, frames file: H3's measure) engine=%s s (%s)"
              % (word, tag, R["group"], R["save"], R["lc"], R["layout"]["E"] - 1, fmt(R["lt"], "%.2f"), R["lt_why"]))
    print("%s %s %s" % (word, tag, "FAIL" if stop else "ok"))
    return 1 if stop else 0


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
              "alife_pass=%d alife_opu=%d warmup_full=%d held=%d cap=%d time_continual=%d"
              % (args["parse"], g["n_lines"], g["intro"] + 1, g["handled"] + 1, g["legacy"], g["slow_pre_max"],
                 g["slow_any_max"], len(g["slows"]), len(g["passes"]), len(g["opus"]), len(g["full"]),
                 len(g["held"]), len(g["cap"]), len(g["tcont"])))
        return 0
    for mode in ("probe", "check"):
        if set(args) == {mode, "order"} and len(argv) == 2:
            return one_load(args[mode], args["order"], mode == "probe")
    if set(args) != {"root", "order", "stop", "out"} or len(argv) != 4:
        print("usage: python3 -I wu3_score.py root=<dir> order=<order file> stop=<stop.txt> out=<new file>   |   "
              "parse=<xray.log>   |   probe=<prime load folder> order=<order file>   |   check=<load folder> order=<order file>")
        return 2
    out = args["out"]
    if os.path.exists(out) and on("refuse_out"):
        print("REFUSED: %s exists, not overwriting" % out)
        return 2
    slots, err = read_order(args["order"])
    if err:
        print(err)
        return 2
    if not any(s["arm"] == "on" for s in slots) or not any(s["arm"] == "off" for s in slots):
        print("REFUSED: the order file needs both on and off loads")
        return 2
    root = args["root"]
    o = []
    P = o.append
    P("wu3 alife_warmup lever (PREREG-wu3-alife-warmup, DRAFT)")
    P("scorer %s sha256 %s" % (os.path.abspath(__file__), sha(os.path.abspath(__file__))))
    P("order  %s sha256 %s" % (os.path.abspath(args["order"]), sha(args["order"])))
    P("root   %s" % os.path.abspath(root))
    P("stop   %s" % os.path.abspath(args["stop"]))
    P("constants window %.0f s from the end of loading (headline; %.0f s reported only), slow %.1f/%.1f ms, H1 worst ratio %.2f, "
      "H2 n20 drop >= %d, H3 price <= %.1f s, min ON %d, min OFF-IN-PLAY %d, stock hitch >= %.0f ms, stock train >= %d, "
      "legacy wide %d, N/M >= %.2f, P >= %d, clock tolerance %.0f ms, countdown race %d frame, re-runs <= %d, void-arm note > %d"
      % (WINDOW_MS / 1000, WINDOW2_MS / 1000, SLOW1_MS, SLOW2_MS, WORST_RATIO, N20_DROP, PRICE_MAX_S, MIN_ON, MIN_OFFP,
         STOCK_HITCH_MIN, STOCK_TRAIN_MIN, LEGACY_WIDE, N_M_MIN, P_MIN, CLOCK_TOL_MS, RACE_FRAMES, R_MAX, VOID_ARM_DIFF))
    stop_cls, stop_reason, stop_int, stop_err = read_stop(args["stop"])
    P("STOP RECORD %s" % (("class=%s%s reason=%s" % (stop_cls, (" interrupted=%s" % stop_int) if stop_int else "", stop_reason))
                          if stop_cls else ("unusable: %s" % stop_err)))
    P("")
    fails = []      # (load tag or "series", reason)
    try:
        present = set(n for n in os.listdir(root) if os.path.isdir(os.path.join(root, n)))
    except OSError as e:
        present = set()
        fails.append(("series", "missing: cannot list the root %s (%s)" % (root, type(e).__name__)))

    # ---- fix B: a box stop while a bench was live names that load (interrupted=<tag>); its folder, whatever it holds,
    # is treated as not run. It must be the last folder in run order (prime, then each slot T, T_r1), else FAIL ----
    run_order = [s["tag"] for s in slots if s["arm"] == "prime"]
    for s in slots:
        if s["arm"] != "prime":
            run_order += [s["tag"], s["tag"] + "_r1"]
    if stop_cls == "box" and stop_int:
        ro = [t for t in run_order if t in present]
        if stop_int not in run_order:
            if on("interrupted_last"):
                fails.append(("series", "stop_contradiction: interrupted tag %s is not a load of this order" % stop_int))
        elif stop_int in present and ro[-1] != stop_int and on("interrupted_last"):
            fails.append(("series", "stop_contradiction: interrupted tag %s is not the last folder in run order (the last is "
                                    "%s): the driver only stops a bench that is running" % (stop_int, ro[-1])))
        elif on("interrupted_not_run"):
            present.discard(stop_int)
            P("INTERRUPTED %s: the bench was live at the box stop; its folder (%s) is treated as not run"
              % (stop_int, "present" if stop_int in ro else "absent"))

    def reruns_of(tag):
        return sorted(n for n in present if RERUN_RE.match(n) and RERUN_RE.match(n).group(1) == tag)

    def report(R, extra=()):
        state = "FAIL" if R["fatal"] else ("VOID" if R["void"] else "VALID")
        reasons = [r.split(":")[0] for r in R["fatal"] + R["void"]]
        head = "LOAD %s %s %s%s" % (R["tag"], R["arm"].upper(), state, (" [" + "] [".join(reasons) + "]") if reasons else "")
        if "w1" in R:
            w, w2 = R["w1"], R["w2"]
            head += (" group=%s worst=%.1f n20=%d n33=%d span=%.2fs worst_incl_save=%.1f save=%s | 15s: worst=%s n20=%s "
                     "n33=%s span=%s | loading=%.2f s in capture, engine clock %s s" % (R["group"], w["worst"], w["n20"], w["n33"], w["span"], w["worst_all"],
                                                    R["save"], fmt(w2["worst"]) if w2["covered"] else "n/a",
                                                    w2["n20"] if w2["covered"] else "n/a",
                                                    w2["n33"] if w2["covered"] else "n/a",
                                                    ("%.2fs" % w2["span"]) if w2["covered"] else "n/a",
                                                    R["lc"], fmt(R["lt"], "%.2f")))
        P(head)
        for x in extra:
            P("    %s" % x)
        for r in R["fatal"] + R["void"]:
            P("    %s" % r)
        if "w1" in R:
            if R["group"] != "ON":
                P("    order: %s; wide passes logged %d; legacy %s" % (R["why"], R["wide"], R["legacy"]))
            for n in R["notes"]:
                P("    %s" % n)
            P("    window from capture %d; %s" % (R["w1"]["first"], R["before"]))
            P("    load time: in capture %.1f ms (capture 0..%d, frames file; H3's measure); engine clock %s" % (
                R["lc"] * 1000.0, R["layout"]["E"] - 1, R["lt_why"]))

    not_clean = lambda R: any(v.startswith("not_clean:") for v in R["void"])
    prime_present = False
    for s in slots:
        if s["arm"] != "prime":
            continue
        prime_present = prime_present or s["tag"] in present
        if s["tag"] not in present:
            P("LOAD %s prime: not run (no folder%s)" % (s["tag"], ", interrupted" if s["tag"] == stop_int else ""))
            continue
        try:
            pv = read_verdict(os.path.join(root, s["tag"]))
        except Bad as e:
            pv = "unreadable (%s)" % e
        except OSError as e:   # wu2 review pass 2: exists but cannot be read (permissions, I/O): reported, report kept
            if not on("prime_oserror"):
                raise
            pv = "unreadable (%s: %s)" % (type(e).__name__, e)
        P("LOAD %s prime: not scored (%s)%s" % (s["tag"], pv, "" if pv == "VERDICT: clean" else
                                                "; not clean is allowed for the prime: never re-run, not counted"))
        for n in reruns_of(s["tag"]):
            if on("unexpected_rerun"):
                fails.append((n, "unexpected_rerun: %s is a re-run of the prime load %s; the prime is never re-run" % (n, s["tag"])))
    # ---- wu3: each scored slot is its first load, or, when that was not clean, its one re-run <slot>_r1 ----
    S = []          # per slot: dict(slot, status: scored | rerun | unresolved | missing, first, rerun, scored)
    nrr = []
    for s in slots:
        if s["arm"] == "prime":
            continue
        T, r1 = s["tag"], s["tag"] + "_r1"
        for n in reruns_of(T):
            if n != r1 and on("second_rerun"):
                fails.append((n, "second_rerun: %s: at most one re-run per slot, named %s" % (n, r1)))
        has_r1 = r1 in present
        if has_r1:
            nrr.append(r1)
        st = dict(slot=s, status=None, first=None, rerun=None, scored=None, failed=False)
        S.append(st)
        if T not in present:
            st["status"] = "missing"
            P("LOAD %s %s NOT RUN (%s)" % (T, s["arm"].upper(), ("interrupted by the box stop: folder %s ignored" if T == stop_int
                                                             else "no folder %s") % os.path.join(root, T)))
            if has_r1 and on("stop_contradiction"):
                fails.append((r1, "stop_contradiction: re-run %s present but its slot's first load %s is missing" % (r1, T)))
            continue
        R0 = score_safe(root, s)
        st["first"] = R0
        # a FAIL of the first attempt is never dropped, also when a re-run is scored in its place (prereg wu3 3.5)
        if not (has_r1 and not_clean(R0) and not on("first_fatal_kept")):
            fails += [(T, r) for r in R0["fatal"]]
        st["failed"] = bool(R0["fatal"])
        if not not_clean(R0):
            st["status"], st["scored"] = "scored", R0
            report(R0)
            if has_r1 and on("unexpected_rerun"):
                fails.append((r1, "unexpected_rerun: %s exists but the slot's first load %s was clean: only a not-clean "
                                  "load is re-run" % (r1, T)))
        elif has_r1 and on("rerun_scored"):
            R1 = score_safe(root, dict(s, tag=r1))
            st["status"], st["rerun"], st["scored"] = "rerun", R1, R1
            st["failed"] = st["failed"] or bool(R1["fatal"])
            # fix A: the driver re-runs only a first load that passed check= (no FAIL, no stop-VOID)
            r0stop = list(R0["fatal"]) + [v for v in R0["void"] if v.split(":")[0] in STOP_VOIDS]
            if r0stop and on("rerun_after_check"):
                fails.append((r1, "stop_contradiction: re-run %s exists but the first load %s failed its check (%s): the "
                                  "driver never re-runs after a failed check" % (r1, T, ", ".join(sorted(set(v.split(":")[0] for v in r0stop))))))
            report(R0, ["first attempt not clean: re-run %s is scored in its place" % r1])
            report(R1, ["re-run of slot %s, scored in its place (every per-load rule)%s" % (
                T, "; not clean again: the slot is VOID" if not_clean(R1) else "")])
            fails += [(r1, r) for r in R1["fatal"]]
        else:
            st["status"], st["scored"] = "unresolved", R0
            report(R0, ["first attempt not clean and no re-run %s: the slot is VOID" % r1])
    if len(nrr) > R_MAX and on("rerun_budget"):
        fails.append(("series", "rerun_budget: %d re-run folders (%s) > R_MAX %d" % (len(nrr), ", ".join(nrr), R_MAX)))
    # ---- the stop record against the folders (prereg wu3 3.4) ----
    missing = [st for st in S if st["status"] == "missing"]
    unresolved = [st for st in S if st["status"] == "unresolved"]
    k = next((i for i, st in enumerate(S) if st["status"] in ("missing", "unresolved")), None)
    if k is not None and on("stop_contradiction"):
        later = [st["slot"]["tag"] for st in S[k + 1:] if st["status"] != "missing"]
        if later:
            fails.append(("series", "stop_contradiction: slot %s is %s, but later slot(s) %s have folders: the driver runs "
                                    "the slots in order and never goes past a slot it did not finish"
                          % (S[k]["slot"]["tag"], "not run" if S[k]["status"] == "missing" else "not clean without a re-run",
                             ", ".join(later[:4]) + (" ..." if len(later) > 4 else ""))))
    if not prime_present and any(st["status"] != "missing" for st in S) and on("stop_contradiction"):
        fails.append(("series", "stop_contradiction: scored loads are present but the prime load is not"))
    if stop_cls is None:
        if on("stop_record"):
            fails.append(("series", "stop_record: no usable stop record (%s): the driver writes one on every exit" % stop_err))
    elif stop_cls == "fault":
        if on("fault_stop"):
            fails.append(("series", "fault_stop: the driver stopped the series on a fault: %s" % stop_reason))
    elif stop_cls == "done":
        if (missing or unresolved) and on("stop_contradiction"):
            fails.append(("series", "stop_contradiction: the stop record says done (list finished), but %d slot(s) were not "
                                    "run and %d are not clean without a re-run" % (len(missing), len(unresolved))))
    elif stop_cls == "box":
        sv = sorted(set(R["tag"] for st in S for R in (st["first"], st["rerun"]) if R is not None
                        and any(v.split(":")[0] in STOP_VOIDS for v in R["void"])))
        if sv and on("stop_contradiction"):
            fails.append(("series", "stop_contradiction: the stop record says box, but load(s) %s failed a stop check "
                                    "(the driver stops on those as a fault)" % ", ".join(sv)))
    box = stop_cls == "box" and on("box_nv")
    if not box:
        for st in missing:
            fails.append((st["slot"]["tag"], "missing: MISSING folder %s" % os.path.join(root, st["slot"]["tag"])))
    # ---- slots, attempts and voids per arm (reported) ----
    P("")
    arm_n = {}
    for a in ("on", "off"):
        A = [st for st in S if st["slot"]["arm"] == a]
        att = [R for st in A for R in (st["first"], st["rerun"]) if R is not None]
        nc = sum(1 for R in att if not_clean(R))
        # fix D: a slot is failed when EITHER attempt failed; only a slot with no failed attempt counts as valid or void
        fl = lambda st: (st["failed"] if on("slot_failed_either") else bool(st["scored"]["fatal"]))
        sc = [st["scored"] for st in A if st["scored"] is not None and not fl(st)]
        arm_n[a] = dict(slots=len(A), att=len(att), nc=nc, valid=sum(1 for R in sc if not R["void"]),
                        void=sum(1 for R in sc if R["void"]), void_nc=sum(1 for R in sc if not_clean(R)),
                        failed=sum(1 for st in A if st["scored"] is not None and fl(st)),
                        notrun=sum(1 for st in A if st["status"] == "missing"))
        n = arm_n[a]
        P("SLOTS %-3s %d slots: %d valid, %d void (%d of them not clean on every attempt), %d failed, %d not run; "
          "attempts run %d, not clean %d (%s)" % (a.upper(), n["slots"], n["valid"], n["void"], n["void_nc"], n["failed"],
                                                  n["notrun"], n["att"], n["nc"],
                                                  ("%.0f %%" % (100.0 * n["nc"] / n["att"])) if n["att"] else "n/a"))
    P("RE-RUNS %d of R_MAX %d%s" % (len(nrr), R_MAX, (": " + ", ".join(nrr)) if nrr else ""))
    if abs(arm_n["on"]["void"] - arm_n["off"]["void"]) > VOID_ARM_DIFF and on("void_arms_note"):
        P("NOTE void_arms_differ (reported only, not a verdict rule): void slots ON %d, OFF %d differ by more than %d: the "
          "valid loads of the two arms may not be alike" % (arm_n["on"]["void"], arm_n["off"]["void"], VOID_ARM_DIFF))
    loads = [st["scored"] for st in S if st["scored"] is not None]
    for st in S:   # fix D: the metrics use the chosen capture only when neither attempt failed
        if st["scored"] is not None and st["failed"] and on("slot_failed_either"):
            st["scored"]["slot_failed"] = True
    # ---------------- verdict (prereg wu2 7, wu3 3.5) ----------------
    valid = [R for R in loads if not R["fatal"] and not R["void"] and not R.get("slot_failed")]
    for R in valid:   # wu2: a valid load always has a load time (E and the clock fit are stop-VOID conditions, 5.4)
        if R["lt"] is None:
            R["fatal"].append("scorer_error: valid load without a load time")
            fails.append((R["tag"], R["fatal"][-1]))
    valid = [R for R in valid if not R["fatal"]]
    on_v = [R for R in valid if R["group"] == "ON"]
    offv = [R for R in valid if R["arm"] == "off"]
    offP = [R for R in offv if R["group"] == "OFF-IN-PLAY"] if on("grouping") else list(offv)
    offL = [R for R in offv if R["group"] == "OFF-IN-LOADING"]
    offO = [R for R in offv if R["group"] == "OFF-OTHER"]
    P("")
    P("GROUPS (valid loads; the two stock groups are never pooled): ON %d, OFF-IN-PLAY %d, OFF-IN-LOADING %d, "
      "OFF-OTHER %d (reported only); void %d; failed %d"
      % (len(on_v), len(offP), len(offL), len(offO), sum(1 for R in loads if R["void"] and not R["fatal"] and not R.get("slot_failed")),
         sum(1 for R in loads if R["fatal"] or R.get("slot_failed"))))

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
                     ("loading_in_capture_s", lambda R: R["lc"]), ("engine_clock_load_s", lambda R: R["lt"]),
                     ("pass_ms", lambda R: R.get("pass_ms")), ("held_frames", lambda R: R.get("held"))):
            M[(name, k)], txt = stats(G, f)
            if txt != "n/a" or name != "OFF-OTHER":
                P("  %-15s %-20s %s" % (name, k, txt))
    final, code = "", 0
    # ---- load time (prereg 5.4): H3's measure is the in-capture loading time; the engine clock at E is reported beside it ----
    LKEY = "loading_in_capture_s" if on("h3_capture") else "engine_clock_load_s"
    LNAME = "in-capture loading time" if LKEY == "loading_in_capture_s" else "engine clock at E"
    if on_v and offP:
        dc = M[("ON", "loading_in_capture_s")] - M[("OFF-IN-PLAY", "loading_in_capture_s")]
        de = M[("ON", "engine_clock_load_s")] - M[("OFF-IN-PLAY", "engine_clock_load_s")]
        # Bound on de - dc (prereg 5.4): per load, engine clock at E - in-capture time = G + e, G = engine time at the capture
        # start (minus any timer pause / absorb inside 0..E-1), e = the estimate's error in [-(ms[E] + 1), +(ms[c] + 1)] ms.
        # With G the same in every load, a median being monotone in each value gives de - dc in
        # [-(max ON lo + max OFF up), +(max ON up + max OFF lo)]. G varies from load to load: not covered (prereg 6.1).
        lo = lambda G: max(R["layout"]["lt_bound"][0] for R in G)
        up = lambda G: max(R["layout"]["lt_bound"][1] for R in G)
        bl, bu = -(lo(on_v) + up(offP)) / 1000.0, (up(on_v) + lo(offP)) / 1000.0
        dd = de - dc
        differ = on("lt_differ") and not bl <= dd <= bu
        P("")
        P("LOAD TIME (H3's measure: in-capture loading time = sum of the frames file's ms of capture 0..E-1, QPC wall clock; "
          "the engine clock at E beside it)")
        P("  loading takes %s with the lever (median in-capture loading time ON %.2f s, OFF-IN-PLAY %.2f s)"
          % (("%.2f s %s" % (abs(dc), "longer" if dc > 0 else "shorter")) if abs(dc) >= 0.005 else "the same time (+0.00 s)",
             M[("ON", "loading_in_capture_s")], M[("OFF-IN-PLAY", "loading_in_capture_s")]))
        P("  engine clock at E (estimate, reported only): median ON %.2f s - median OFF-IN-PLAY %.2f s = %+.2f s"
          % (M[("ON", "engine_clock_load_s")], M[("OFF-IN-PLAY", "engine_clock_load_s")], de))
        P("  difference of the two ON - OFF-IN-PLAY deltas (engine clock - in capture): %+.3f s; bound %+.3f / %+.3f s (the "
          "estimates' errors from the frame sizes, prereg 5.4) -> %s" % (dd, bl, bu, "loadtime_measures_differ" if differ else "agree"))
        if differ:
            P("NOTE loadtime_measures_differ (reported only, not a verdict rule): the engine clock's ON - OFF-IN-PLAY delta is "
              "%+.3f s off the in-capture delta, outside %+.3f / %+.3f s: the engine time before the capture start differed "
              "between the groups, or the engine timer was paused / absorbed inside the loading frames (prereg 5.4, 6.1)"
              % (dd, bl, bu))
    if fails:
        P("")
        for t, r in fails:
            P("FAIL %s %s" % (t, r))
        final, code = "FAIL (%d load(s) failed a registered check%s: %s)" % (
            len(set(t for t, _ in fails if t != "series")), ", a series rule failed" if any(t == "series" for t, _ in fails) else "",
            ", ".join(sorted(set(r.split(":")[0] for _, r in fails)))), 1
    elif box:
        # wu3 (prereg 3.5): the driver stopped for a box reason and no present load failed a check: the box was not quiet
        # enough to finish the list. Nothing is said about the lever.
        P("")
        n_on, n_off = arm_n["on"], arm_n["off"]
        att, nc = n_on["att"] + n_off["att"], n_on["nc"] + n_off["nc"]
        P("NO VERDICT [box_not_quiet: the driver stopped for a box reason: %s; not clean: %d of %d attempts (%s) - ON %d of %d, "
          "OFF %d of %d; re-runs %d of %d; void slots ON %d, OFF %d; slots not run %d]"
          % (stop_reason, nc, att, ("%.0f %%" % (100.0 * nc / att)) if att else "n/a", n_on["nc"], n_on["att"], n_off["nc"],
             n_off["att"], len(nrr), R_MAX, n_on["void"], n_off["void"], n_on["notrun"] + n_off["notrun"]))
        final, code = "NO VERDICT (box_not_quiet)", 3
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
            price = M[("ON", LKEY)] - M[("OFF-IN-PLAY", LKEY)]
            h3 = price <= PRICE_MAX_S
            h2 = h2 or not on("h2")
            h3 = h3 or not on("h3")
            P("H1 worst frame (%.0f s): median ON %.1f ms <= %.2f x median OFF-IN-PLAY %.1f ms = %.1f (%s), and ON loads with "
              "worst >= %.1f ms: %d <= %d (%s) -> %s" % (WINDOW_MS / 1000, wo, WORST_RATIO, wp, WORST_RATIO * wp, "ok" if h1a else "FAIL", wp,
                                                       stray, WORST_STRAY, "ok" if h1b else "FAIL", "ok" if h1 else "FAIL"))
            P("H2 frames > 20 ms (%.0f s): median OFF-IN-PLAY %.1f - median ON %.1f = %.1f >= %d -> %s"
              % (WINDOW_MS / 1000, np_, no, np_ - no, N20_DROP, "ok" if h2 else "FAIL"))
            P("H3 load-time price (%s): median ON %.2f s - median OFF-IN-PLAY %.2f s = %+.2f s <= %.1f s -> %s"
              % (LNAME, M[("ON", LKEY)], M[("OFF-IN-PLAY", LKEY)], price, PRICE_MAX_S, "ok" if h3 else "FAIL"))
            if h1 and h2 and h3:
                final, code = "PASS", 0
            else:
                final, code = "FAIL (%s)" % ", ".join(n for n, h in (("H1", h1), ("H2", h2), ("H3", h3)) if not h), 1
        if len(offL) >= SAME_MIN_OFFL and on_v and M[("ON", LKEY)] is not None and M[("OFF-IN-LOADING", LKEY)] is not None:
            a = M[("ON", "worst_ms")] <= SAME_WORST_X * M[("OFF-IN-LOADING", "worst_ms")] + SAME_WORST_ADD
            b = M[("ON", "n20")] <= M[("OFF-IN-LOADING", "n20")] + SAME_N20_ADD
            c = abs(M[("ON", LKEY)] - M[("OFF-IN-LOADING", LKEY)]) <= SAME_PRICE_S
            P("SAME AS GOOD ORDER (reported only): worst %s, n20 %s, load (%s) %+.2f s %s -> %s"
              % ("ok" if a else "worse", "ok" if b else "worse", LNAME, M[("ON", LKEY)] - M[("OFF-IN-LOADING", LKEY)],
                 "ok" if c else "differs", "SAME" if a and b and c else "DIFFERENT"))
        else:
            P("SAME AS GOOD ORDER (reported only): NO VERDICT (%d valid OFF-IN-LOADING loads, need %d, or load time missing)"
              % (len(offL), SAME_MIN_OFFL))
    P("")
    P("WU3 RESULT: " + final)
    with open(out, "x") as f:
        f.write("\n".join(o) + "\n")
    print(o[-1])
    print("written %s" % out)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
