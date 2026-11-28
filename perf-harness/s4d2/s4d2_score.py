#!/usr/bin/env python3
# s4d2 scorer (PREREG-s4d2-npc-pose-DRAFT.md). Stdlib only; run with python3 -I. Imports nothing from tools/.
# Build 2 = slices S4a (NPC/mutant poses drawn between ticks) + S4b (items on bones follow the drawn pose), one
# player-facing step under sim_4d_mask bit 16. Code: wt-simtick perf/tick-4d, c216081fd + the witness commit W1/W2
# (HEAD_AFTER_W1, to be filled at registration; prereg header).
#
#   python3 -I s4d2_score.py root=<folder holding det_<tag> run folders> out=<new output file>     (score, ONCE)
#   python3 -I s4d2_score.py mode=probe root=<folder holding det_s4p_probe> out=<new output file>
#
# Score exit codes: 0 PASS, 1 FAIL, 3 NO VERDICT, 2 REFUSED (usage, out exists, a PROBE_ constant still a placeholder).
# Probe exit codes: 0 GO, 1 STOP (each failed rule printed as "PROBE STOP <code>: ..."), 2 REFUSED.
# The output is written to out= in this same call; an existing out file is refused, never overwritten (there is no
# override: the prereg forbids a re-score after a checker fix).
# A missing or unreadable input file fails the checks that read it BY NAME (never a silent skip, never read as 0).
# A malformed witness line (any column in a shape the code does not print) fails every check that reads that witness.
# A run folder <root>/det_<tag>_r1 (the series' one retry) is read instead of det_<tag> and the substitution is named.
# -det_synth_item (stimulus 1777fb68..., 2026-11-27) is on every boot; F0 and the probe rule `item` check it took an item.
# Every reader strips \r (the engine log and the Lua-written files are CRLF under Wine; the witness is LF, but CRLF
# is accepted everywhere).
#
# Witness line (device.cpp sim_synth_witness_write + sim_pose.cpp sim_pose_witness_print at c216081fd):
#   58 tokens without -det_synth_pose (build 1 exe, or build 2 without the switch);
#   126 tokens with it at c216081fd: base 56 | p 6 | pw 19 | up 2 | pn 5 | pa 26 | then "tw N";
#   132 tokens with W1 alone (prereg 1.3): "... pa .. | uu <nu> <nud> <nus> <nposed> | tw N";
#   138 tokens as the W1 commit prints it (W1 + W2): "... pa .. | uu <nu> <nud> <nus> <nposed> | pj <bone> <jw xyz> | tw N".
# pa form letters (sim_pose.h SimPoseAttachWit::form): C, R (bone corrections), I (C = identity: bones drawn as read,
# only the parent moved since the placement; c216081fd S1, review F1), O, P, S.
# Counter reading mirrors tools/counters_diff.py (read_lines 23-25, load_alife 28-38, load_counters 41-50,
# compare_ids 53-63, the EXACT part of compare_k 86-90). Online object diff (reported only) mirrors tools/det_diff.py.
# A-Life call frames mirror tools/s2_partA2_score.py line 22. All three copied from s4d1_score.py unchanged.
import math
import os
import re
import struct
import sys

# ---- constants the probe supplies (prereg section 2.3, each with its rule there). Scoring REFUSES while any value is
# still the placeholder string. ----
CONST = {
    "PROBE_P_LERP": "PROBE_P_LERP",            # probe: n=0 frames with pw form L and hd != hl
    "PROBE_P_TICK": "PROBE_P_TICK",            # probe: n=1 frames with pw form L and hp != hc
    "PROBE_UPLOADS": "PROBE_UPLOADS",          # probe: frames with an up column
    "PROBE_PA_CR": "PROBE_PA_CR",              # probe: n=0 frames with pa form C or R and wd != ws
    "PROBE_PA_CLUSTERS": "PROBE_PA_CLUSTERS",  # probe: max number of ld clusters per (kind, ba, bb) group, form C
    "PROBE_JOINT_LERP": "PROBE_JOINT_LERP",    # probe: 1 = jd on the lerp on every form-L line, 0 = fallbacks seen
    "PROBE_U_POSED": "PROBE_U_POSED",          # probe: n=0 frames with uu nposed > 0 (needs W1)
    "PROBE_NPC": "PROBE_NPC",                  # probe: the witness NPC name used (-det_synth_pose_npc)
    "PROBE_TOL_LD": "PROBE_TOL_LD",            # probe: PA2's ld cluster radius by the TOL_LD rule (prereg 7.1); 0 = the
                                               # cluster part of PA2 is reported only (noise and signal within 4x)
}
# ---- registered constants (derivations in the prereg, margin table section 7) ----
KP = 3                    # the probe's K
DROP_MIN = 300            # a probe count under this stops the series (prereg 2.2)
TOL_POS = 1e-4            # positions printed with 5 decimals: "equal" = every coordinate within 1e-4 m
ULP_K = 4                 # lerp checks add 4 float32 ulps of the largest coordinate
TOL_A = 1.5e-6            # alpha, dt and u are printed with 6 decimals
TOL_HUD = 2e-3            # hud is printed with 3 decimals
TOL_LD0 = 1e-3            # m: the probe's ld leader-cluster radius; PA2 uses CONST PROBE_TOL_LD (prereg 7.1 rule)
LD_NOISE_FLOOR = 1e-5     # m: two print roundings of 5 decimals: the smallest noise the witness can show
LD_STEP_PCT = 10          # PROBE_HAND_STEP = this percentile of the per-tick hand step (prereg 7.1)
CP1_DIFF = 8              # cp1 frame (part A) or tick (part S) difference; one A-Life pass = 17 (COPIED, 4b R3)
ALIFE_MIN_CALLS = 50      # part A: A-Life calls traced up to cp1 (stock: 100) -> 2x (COPIED)
WALK_MIN_RANGE = 1.5      # m: the walk spans 3.0 m -> 2x (COPIED; script geometry)
FIRED_MIN = 50            # highest "N bullets fired" line; ~171 expected [G] (COPIED)
WALLCLOCK_DIFF_MIN = 0.90  # PWC: share of lines whose a (and form-L u) differ from j/K
PWC_MIN_L = 30            # PWC: evidence floor of form-L lines in the wallclock run
F5_DIFF_MAX = 0.5         # probe: share of render-only recalcs that are byte-different above which the series stops
NPC_MIN_SHARE = 0.5       # probe: pn present on at least half of the lines
CLUSTERS_MAX = 2          # probe: more ld clusters per group than this stops the series (the record is not the placement)
STOCK_EXE = "stance14.exe"
NEW_EXE = "simtick19.exe"      # build 2 (name proposed; prereg 1.2)
B1_EXE = "simtick18.exe"       # build 1 (s4d1's exe; prereg 1.2)
BASE = ["-det_mode", "-det_passes", "100,200,320", "-det_lua_streams", "-det_counters"]
HARNESS = ("A1", "S0")         # a FAIL here = harness not usable: dependents NO VERDICT, result NO VERDICT
SLICES = ("s2", "s3", "pose", "attach")
HINT_SC = ("named risk 'heap layout' (prereg 10.1): on and off differ in the offline counters with every tripwire "
           "clean; look first at the SimPoseState allocation in sim_pose_capture_body (sim_pose.cpp:316, xr_new, "
           "~21 KB per captured skeleton, on arm only), then for a pointer-ordered simulation container")
HINT_LB = ("named risk 'heap layout' (prereg 10.1), static part: build 2 with bit 16 off differs from build 1; look "
           "first at CGameObject::m_sim_attach (GameObject.h:258, ~280 B in every CGameObject) and "
           "IKinematics::m_sim_pose (Kinematics.h:130), then at the bit-off code paths of c216081fd")


def R(part, exe, k=0, mask=None, wc=0, tw=0, brk=0, stim=0, pose=0, b1=0):
    return {"part": part, "exe": exe, "k": k, "mask": mask, "wc": wc, "tw": tw, "brk": brk, "stim": stim,
            "pose": pose, "b1": b1}


def S(k, mask, **kw):
    return R("S", kw.pop("exe", NEW_EXE), k, mask, tw=1, stim=1, pose=1, **kw)


RUNS = {
    "s4p_s1": R("A", STOCK_EXE), "s4p_o1": R("A", NEW_EXE), "s4p_s2": R("A", STOCK_EXE), "s4p_o2": R("A", NEW_EXE),
    "s4p_s3": R("A", STOCK_EXE), "s4p_o3": R("A", NEW_EXE),
    "s4p_off2a": S(2, 7), "s4p_off2b": S(2, 7), "s4p_on2a": S(2, 23), "s4p_on2b": S(2, 23),
    "s4p_off3": S(3, 7), "s4p_on3": S(3, 23),
    "s4p_b1off2": S(2, 7, exe=B1_EXE, b1=1),
    "s4p_brk16": S(2, 23, brk=16), "s4p_brk32": S(2, 23, brk=32),
    "s4p_wc2": S(2, 23, wc=1),
}
PROBE_TAG = "s4p_probe"
PROBE_RUN = S(KP, 23)
S_ON = ("s4p_on2a", "s4p_on2b", "s4p_on3")
S_OFF = ("s4p_off2a", "s4p_off2b", "s4p_off3")
S_ALL = tuple(t for t, s in RUNS.items() if s["part"] == "S")


class Missing(Exception):
    pass


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def dt_exp(k):
    return f32(f32(0.033) / float(k))


def cheb(a, b):
    return max(abs(x - y) for x, y in zip(a, b))


def eucl(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def norm(a):
    return math.sqrt(sum(x * x for x in a))


def lerp(p, q, a):
    return tuple(x + (y - x) * a for x, y in zip(p, q))


def tol_lerp(*vs):
    m = max(abs(c) for v in vs for c in v)
    return TOL_POS + ULP_K * m * 2.0 ** -23


def rd(path):
    if not os.path.isfile(path):
        raise Missing("missing file %s" % path)
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace").replace("\r", "").split("\n")


# ---------------------------------------------------------------- witness
class W:
    __slots__ = ("F", "T", "j", "K", "n", "a", "dt", "hud", "lever", "s2d", "s2x", "s2c", "s3n", "s3h", "s3p", "s3s",
                 "s3ok", "s1", "s4", "s4d", "tw", "pose", "p", "pw", "up", "pn", "pa", "uu", "pj", "ntok")


class O:   # a plain record for the pose columns
    def __init__(self, **kw):
        self.__dict__.update(kw)


BASE_LIT = {0: "F", 2: "T", 4: "j", 6: "K", 8: "n", 10: "a", 12: "dt", 14: "hud", 16: "|", 17: "s2", 27: "|",
            28: "s3", 40: "|", 41: "s1", 47: "|", 48: "s4", 55: "|"}
POSE_LIT = {56: "p", 63: "|", 64: "pw", 84: "|", 85: "up", 88: "|", 89: "pn", 95: "|", 96: "pa", 123: "|"}
NTOK_B1, NTOK_POSE, NTOK_UU, NTOK_PJ = 58, 126, 132, 138
HEX = re.compile(r"^[0-9a-f]{16}$")
PW_FORMS = ("L", "Sn", "Sb", "Sg", "Ss")
PA_FORMS = ("C", "R", "I", "O", "P", "S")
PA_CORR = ("C", "R", "I")        # corrected forms (ld, ls present; the break 32 fires on them)
PA_BONE = ("C", "R")             # bone corrections: only these count as the S4b correction (review F1)


def vec(t):
    return (float(t[0]), float(t[1]), float(t[2]))


def dash(t):
    return all(x == "-" for x in t)


def hx(s, opt=False):
    if opt and s == "-":
        return None
    if not HEX.match(s):
        raise ValueError("hash %r" % s)
    return s


def uint(s):
    v = int(s)
    if v < 0:
        raise ValueError("negative count %r" % s)
    return v


def vec_or_none(t):
    if dash(t):
        return None
    return vec(t)


def parse_pw(t):   # 19 values (sim_pose.cpp sim_pose_witness_print)
    if dash(t):
        return None
    f = t[0]
    if f not in PW_FORMS:
        raise ValueError("pw form %r" % f)
    L = f == "L"
    have = t[2] != "-"
    if have != (t[3] != "-"):
        raise ValueError("pw ptick/ctick half absent")
    if L and not have:
        raise ValueError("pw form L without ptick/ctick")
    r = O(form=f, u=float(t[1]) if L else None, pt=uint(t[2]) if have else None, ct=uint(t[3]) if have else None)
    if not L and t[1] != "-":
        raise ValueError("pw u present on form %s" % f)
    r.hd = hx(t[4]) if L else None
    if not L and t[4] != "-":
        raise ValueError("pw hd present on form %s" % f)
    r.hl = hx(t[5])
    r.hp = hx(t[6]) if have else None
    r.hc = hx(t[7]) if have else None
    if not have and (t[6] != "-" or t[7] != "-"):
        raise ValueError("pw hp/hc without ptick")
    r.hu = hx(t[8], opt=True)
    r.hdu = hx(t[9], opt=True)
    if not L and r.hdu is not None:
        raise ValueError("pw hdu present on form %s" % f)
    r.jd, r.jp, r.jc = vec_or_none(t[10:13]), vec_or_none(t[13:16]), vec_or_none(t[16:19])
    if L != (r.jd is not None) or L != (r.jp is not None) or L != (r.jc is not None):
        raise ValueError("pw joints present %s on form %s" % (r.jd is not None, f))
    return r


def parse_pn(t):   # 5 values
    if dash(t):
        return None
    f = t[0]
    if f not in PW_FORMS + ("X",):
        raise ValueError("pn form %r" % f)
    L = f == "L"
    if L == (t[1] == "-") or L == (t[2] == "-"):
        raise ValueError("pn u/hd presence on form %s" % f)
    return O(form=f, u=float(t[1]) if L else None, hd=hx(t[2]) if L else None, hl=hx(t[3]), hsim=hx(t[4]))


def parse_pa(t):   # 26 values (sim_pose.cpp sim_pose_print_pa, GameObject.cpp sim_attach_wit_fill)
    if dash(t):
        return None
    kind, form, cls = t[0], t[1], t[2]
    if kind not in ("-", "B", "T", "W", "N", "X", "?") or form not in PA_FORMS or \
            cls not in ("c", "p", "o", "-"):
        raise ValueError("pa kind/form/cls %r %r %r" % (kind, form, cls))
    ba = None if t[3] == "-" else uint(t[3])
    bb = None if t[4] == "-" else uint(t[4])
    have_u = t[5] != "-"
    r = O(kind=kind, form=form, cls=cls, ba=ba, bb=bb, u=float(t[5]) if have_u else None,
          wd=vec(t[6:9]), ws=vec(t[9:12]))
    r.hd, r.hr = vec_or_none(t[12:15]), vec_or_none(t[15:18])
    if have_u != (r.hd is not None) or have_u != (r.hr is not None):
        raise ValueError("pa hd/hr presence differs from u")
    r.ld, r.ls = vec_or_none(t[18:21]), vec_or_none(t[21:24])
    have_l = r.ld is not None
    if have_l != (r.ls is not None) or have_l != (form in PA_CORR):
        raise ValueError("pa ld/ls presence %s on form %s" % (have_l, form))
    r.hx = hx(t[24])
    r.hxe = hx(t[25], opt=True)
    return r


def parse_pj(t):   # 4 values (W2, sim_pose.cpp sim_pose_print_pj): bone, the joint as drawn in world
    if dash(t):
        return None
    return O(bone=uint(t[0]), jw=vec(t[1:4]))


def parse_w(line, layout):
    t = line.split()
    if layout == "b1":
        if len(t) != NTOK_B1:
            raise ValueError("%d tokens, expected %d (no pose columns)" % (len(t), NTOK_B1))
        lit = dict(BASE_LIT)
        lit[56] = "tw"
    else:
        if len(t) not in (NTOK_POSE, NTOK_UU, NTOK_PJ):
            raise ValueError("%d tokens, expected %d, %d or %d (pose columns)" % (len(t), NTOK_POSE, NTOK_UU, NTOK_PJ))
        lit = dict(BASE_LIT)
        lit.update(POSE_LIT)
        if len(t) == NTOK_POSE:
            lit[124] = "tw"
        elif len(t) == NTOK_UU:
            lit.update({124: "uu", 129: "|", 130: "tw"})
        else:
            lit.update({124: "uu", 129: "|", 130: "pj", 135: "|", 136: "tw"})
    for i, s in lit.items():
        if t[i] != s:
            raise ValueError("token %d is %r, expected %r" % (i, t[i], s))
    r = W()
    r.ntok = len(t)
    r.F, r.T, r.j, r.K, r.n = int(t[1]), int(t[3]), int(t[5]), int(t[7]), int(t[9])
    r.a, r.dt, r.hud = float(t[11]), float(t[13]), float(t[15])
    r.lever = " ".join(t[0:16])
    s2 = t[18:27]
    if dash(s2):
        r.s2d = r.s2x = r.s2c = None
    else:
        r.s2d, r.s2x, r.s2c = vec(s2[0:3]), vec(s2[3:6]), vec(s2[6:9])
    s3 = t[29:40]
    r.s3h = r.s3p = r.s3s = r.s3ok = None
    if dash(s3):
        r.s3n = None
    elif s3[0] == "0" and dash(s3[1:]):
        r.s3n = 0
    else:
        r.s3n = int(s3[0])
        r.s3h, r.s3p, r.s3s = vec(s3[1:4]), vec(s3[4:7]), vec(s3[7:10])
        r.s3ok = int(s3[10])
        if r.s3n < 1 or r.s3ok not in (0, 1):
            raise ValueError("s3 drawn %d stamp %d" % (r.s3n, r.s3ok))
    s1 = t[42:47]
    r.s1 = None if dash(s1) else (float(s1[0]), vec(s1[1:4]), float(s1[4]))
    s4 = t[49:55]   # build 1's question; parsed for format only (three forms)
    if dash(s4):
        r.s4 = r.s4d = None
    elif dash(s4[3:]):
        r.s4, r.s4d = vec(s4[0:3]), None
    else:
        r.s4, r.s4d = vec(s4[0:3]), vec(s4[3:6])
    r.pose = layout != "b1"
    r.p = r.pw = r.up = r.pn = r.pa = r.uu = r.pj = None
    if r.pose:
        r.p = tuple(uint(x) for x in t[57:63])
        if r.p[1] + r.p[2] + r.p[3] > r.p[0]:
            raise ValueError("p: L+Sg+Ss %d > evaluated %d" % (r.p[1] + r.p[2] + r.p[3], r.p[0]))
        r.pw = parse_pw(t[65:84])
        if (r.pw is not None) != (r.p[0] > 0):
            raise ValueError("pw present %s with %d skeletons evaluated" % (r.pw is not None, r.p[0]))
        up = t[86:88]
        r.up = None if dash(up) else (hx(up[0]), hx(up[1]))
        r.pn = parse_pn(t[90:95])
        r.pa = parse_pa(t[97:123])
        if len(t) in (NTOK_UU, NTOK_PJ):
            r.uu = tuple(uint(x) for x in t[125:129])
            if r.uu[1] > r.uu[0] or r.uu[2] > r.uu[0] or r.uu[3] > r.uu[0]:
                raise ValueError("uu counts %s exceed nu" % (r.uu,))
        if len(t) == NTOK_PJ:
            r.pj = parse_pj(t[131:135])
            if r.pj is not None and (r.pn is None or r.pn.form != "L"):
                raise ValueError("pj present without pn form L")
    r.tw = int(t[-1])
    return r


# ---------------------------------------------------------------- engine log
RNG_RE = re.compile(r"det_rng_state (\w+): Random=(-?\d+) Random32=(\d+) alife=(\d+) crt_next=(-?\d+) frame=(\d+) "
                    r"time_ms=(\d+)(?: tick=(\d+))?\s*$")
SYN_RE = re.compile(r"\* det_synth: k (\d+) wallclock (\d+) tw (\d+) break (\d+) mask (\d+)\s*$")
POSE_RE = re.compile(r"\* det_synth: pose witness on, npc (\S+)\s*$")
TW_RE = re.compile(r"\* tw (s2|s3|pose|attach) BAD (\d+)\s*$")
FIRED_RE = re.compile(r"det_synth stimulus: (\d+) bullets fired")
# -det_synth_item (stimulus 1777fb68..., 2026-11-27): "item slot <n> section <s>" or "item NONE (no filled slot)"
ITEM_RE = re.compile(r"det_synth stimulus: item (?:slot (\d+) section (\S+)\s*$|(NONE)\b)")


class Log:
    def __init__(self, lines):
        self.cmd = None
        self.syn, self.posel = [], []
        self.rng = {}
        self.rng_lines = []
        self.tw = []
        self.installed = self.started = False
        self.fired = 0
        self.luaerr = []
        self.items = []
        for l in lines:
            if self.cmd is None and "command line" in l:
                self.cmd = l.lower().split()
            m = SYN_RE.search(l)
            if m:
                self.syn.append(tuple(int(x) for x in m.groups()))
            m = POSE_RE.search(l)
            if m:
                self.posel.append(m.group(1).lower())
            m = RNG_RE.search(l)
            if m:
                self.rng[m.group(1)] = (int(m.group(6)), None if m.group(8) is None else int(m.group(8)))
            if "det_rng_state" in l or "det_lua_state" in l:
                self.rng_lines.append(re.sub(r"^.*?(det_(rng|lua)_state)", r"\1", l.strip()))
            m = TW_RE.search(l)
            if m:
                self.tw.append((m.group(1), int(m.group(2))))
            if "det_synth stimulus: installed" in l:
                self.installed = True
            if "det_synth stimulus: start at update" in l:
                self.started = True
            m = ITEM_RE.search(l)
            if m:
                self.items.append("NONE" if m.group(3) else (int(m.group(1)), m.group(2)))
            m = FIRED_RE.search(l)
            if m:
                self.fired = max(self.fired, int(m.group(1)))
            if "zz_eperf_synth" in l and "error" in l.lower():
                self.luaerr.append(l.strip()[:160])


# ---------------------------------------------------------------- run folder
class Run:
    def __init__(self, root, tag, spec):
        self.tag, self.spec, self.sub = tag, spec, False
        d = os.path.join(root, "det_" + tag)
        if os.path.isdir(d + "_r1"):
            self.dir, self.rtag, self.sub = d + "_r1", tag + "_r1", True
        else:
            self.dir, self.rtag = d, tag
        self._log = self._wit = None

    def p(self, pattern):
        if not os.path.isdir(self.dir):
            raise Missing("missing run folder %s" % self.dir)
        return os.path.join(self.dir, pattern.replace("{t}", self.rtag))

    def log(self):
        if self._log is None:
            self._log = Log(rd(self.p("xray.log")))
        return self._log

    def wit(self):
        if self._wit is None:
            path = self.p("synth_{t}.txt")
            layout = "b1" if (self.spec["b1"] or not self.spec["pose"]) else "pose"
            out = []
            for i, l in enumerate(rd(path)):
                if not l.strip():
                    continue
                try:
                    out.append(parse_w(l, layout))
                except ValueError as e:
                    raise Missing("malformed witness %s line %d: %s" % (path, i + 1, e))
            if not out:
                raise Missing("empty witness %s" % path)
            if layout == "pose" and len({q.ntok for q in out}) != 1:
                raise Missing("malformed witness %s: lines of different lengths (uu / pj column on some lines only)" % path)
            self._wit = out
        return self._wit

    def cp(self, name):
        r = self.log().rng.get(name)
        if r is None:
            raise Missing("no det_rng_state %s line in %s" % (name, self.p("xray.log")))
        return r

    def cp_tick(self, name):
        f, t = self.cp(name)
        if t is None:
            raise Missing("det_rng_state %s line without tick= in %s" % (name, self.p("xray.log")))
        return t

    def to_cp3(self):
        f3 = self.cp("cp3")[0]
        return [q for q in self.wit() if q.F <= f3]

    def sched(self):
        out = []
        for l in rd(self.p("det_trace_{t}.txt.sched")):
            if " alife_simulator " in l:
                out.append(int(l.split(" ", 1)[0]))
        return out


# ---------------------------------------------------------------- counters (mirror of tools/counters_diff.py)
def read_lines(path):
    return [ln.split() for ln in rd(path) if ln.strip()]


def load_alife(path):
    totals, upd = {}, {}
    for t in read_lines(path):
        if t[0] in ("registered", "unregistered") and len(t) >= 2:
            totals[t[0]] = t[1]
        elif t[0] == "upd" and len(t) >= 4:
            online = t[-1] == "1"
            name = " ".join(t[3:-1]) or "?"
            upd[int(t[1])] = (int(t[2]), name, online)
    return totals, upd


def load_counters(path):
    streams, sq, info = {}, {}, {}
    for t in read_lines(path):
        if t[0] == "stream" and len(t) >= 2:
            streams[t[1]] = " ".join(t[2:])
        elif t[0] == "sq" and len(t) >= 7:
            sq[int(t[1])] = (" ".join(t[2:6]), t[6] == "1")
        elif t[0] == "info" and len(t) >= 2:
            info[t[1]] = " ".join(t[2:])
    return streams, sq, info


def compare_ids(a, b, values, online, exact):
    for i in sorted(set(a) | set(b)):
        ra, rb = a.get(i), b.get(i)
        va = values(ra) if ra else None
        vb = values(rb) if rb else None
        if va == vb:
            continue
        on = (ra is not None and online(ra)) or (rb is not None and online(rb))
        if not on:
            exact.append(i)


def C(ra, rb, k):
    _, ua = load_alife(ra.p("det_alife_counts_{t}_%d.txt" % k))
    _, ub = load_alife(rb.p("det_alife_counts_{t}_%d.txt" % k))
    sa, qa, _ = load_counters(ra.p("det_counters_{t}_%d.txt" % k))
    sb, qb, _ = load_counters(rb.p("det_counters_{t}_%d.txt" % k))
    exact = [n for n in sorted(set(sa) | set(sb)) if sa.get(n) != sb.get(n)]
    compare_ids(ua, ub, lambda r: r[0], lambda r: r[2], exact)
    compare_ids(qa, qb, lambda r: r[0], lambda r: r[1], exact)
    return len(exact)


def C3(ra, rb):
    return [C(ra, rb, k) for k in (1, 2, 3)]


def online_diff(ra, rb, k):   # reported only (mirror of tools/det_diff.py)
    def load(r):
        objs = {}
        for l in rd(r.p("alife_dump_{t}_%d.txt" % k)):
            if l and not l.startswith("#"):
                p = l.split("\t")
                objs[p[0]] = p
        return objs
    oa, ob = load(ra), load(rb)
    return sum(1 for i in set(oa) & set(ob) if oa[i] != ob[i]) + len(set(oa) ^ set(ob))


# ---------------------------------------------------------------- shared rules
def flag_errors(run, npc):
    """F0: the engine's own command line carries exactly the registered switches; the engine's det_synth lines."""
    s, lg, err = run.spec, run.log(), []
    if lg.cmd is None:
        return ["no 'command line' line in %s" % run.p("xray.log")]
    toks = lg.cmd

    def has(seq):
        return any(toks[i:i + len(seq)] == seq for i in range(len(toks)))
    if not any(t.endswith(s["exe"]) for t in toks):
        err.append("exe %s not on the command line" % s["exe"])
    want = [[x] for x in BASE if not x[0].isdigit()] + [["-det_tag", run.rtag], ["-det_passes", "100,200,320"]]
    # -det_synth_item is on EVERY s4d2 boot, both arms of every pair, part A included (prereg 2.1, 3, 4)
    want.append(["-det_synth_item"])
    lever = {"-det_synth_item"}
    if s["k"]:
        want += [["-det_synth_k", str(s["k"])], ["-sim_4d_mask", str(s["mask"])]]
        lever |= {"-det_synth_k", "-sim_4d_mask"}
    if s["wc"]:
        want.append(["-det_synth_wallclock"]); lever.add("-det_synth_wallclock")
    if s["tw"]:
        want.append(["-det_synth_tw"]); lever.add("-det_synth_tw")
    if s["brk"]:
        want.append(["-sim_4d_break", str(s["brk"])]); lever.add("-sim_4d_break")
    if s["stim"]:
        want += [["-det_synth_walk"], ["-det_synth_tracer"]]; lever |= {"-det_synth_walk", "-det_synth_tracer"}
    if s["pose"]:
        want += [["-det_synth_pose"], ["-det_synth_pose_npc", npc]]
        lever |= {"-det_synth_pose", "-det_synth_pose_npc"}
    for w in want:
        if not has(w):
            err.append("missing switch %s" % " ".join(w))
    for t in toks:
        if (t.startswith("-det_synth") or t.startswith("-sim_4d") or t == "-sim_tick") and t not in lever:
            err.append("unregistered switch %s" % t)
    if s["k"]:
        exp = (s["k"], s["wc"], s["tw"], s["brk"], s["mask"])
        if lg.syn != [exp]:
            err.append("engine det_synth line %s, expected [%s]" % (lg.syn, exp))
    if s["pose"] and not s["b1"]:
        if lg.posel != [npc]:
            err.append("engine pose witness line %s, expected [%s]" % (lg.posel, npc))
    elif lg.posel:
        err.append("engine pose witness line %s in a run whose exe must not print it" % lg.posel)
    return err


def item_errors(run):
    """The -det_synth_item stimulus took an item: exactly one item line, a filled slot, no script error."""
    lg, err = run.log(), []
    if not lg.installed:
        err.append("no 'det_synth stimulus: installed' line")
    if len(lg.items) != 1:
        err.append("%d 'det_synth stimulus: item' lines, expected 1" % len(lg.items))
    elif lg.items[0] == "NONE":
        err.append("item NONE: no filled slot of 3,2,1,5,6, so no item is active and the HUD hands are not drawn")
    if lg.luaerr:
        err.append("script error line %s" % lg.luaerr[0])
    return err


def pattern_errors(run, ad=True):
    """the lever pattern (s4d1 L1, COPIED): fixed pattern, contiguous frames, checkpoints anchored in the witness"""
    K, recs, err = run.spec["k"], run.wit(), []

    def e(msg):
        if len(err) < 6:
            err.append(msg)
        else:
            err[5] = "... more"
    prev, hud_tick, de = None, None, dt_exp(K)
    for r in recs:
        if r.K != K:
            e("F %d: K %d" % (r.F, r.K))
        if r.n not in (0, 1) or (r.n == 1) != (r.j == 0):
            e("F %d: n %d with j %d" % (r.F, r.n, r.j))
        if prev is not None:
            if r.F != prev.F + 1:
                e("F %d follows F %d" % (r.F, prev.F))
            if r.j != (prev.j + 1) % K:
                e("F %d: j %d after j %d" % (r.F, r.j, prev.j))
            if r.T != prev.T + (1 if r.n == 1 else 0):
                e("F %d: T %d after T %d (n %d)" % (r.F, r.T, prev.T, r.n))
        if ad:
            if abs(r.a - float(r.j) / K) > TOL_A:
                e("F %d: a %.6f, expected j/K" % (r.F, r.a))
            if abs(r.dt - de) > TOL_A:
                e("F %d: dt %.6f, expected %.6f" % (r.F, r.dt, de))
            if r.n == 1:
                if hud_tick is not None and abs(r.hud - hud_tick - 33.0) > TOL_HUD:
                    e("F %d: hud step %.3f, expected 33" % (r.F, r.hud - hud_tick))
                hud_tick = r.hud
            elif hud_tick is not None and abs(r.hud - (hud_tick + r.a * 33.0)) > TOL_HUD:
                e("F %d: hud %.3f, expected tick + a*33" % (r.F, r.hud))
        prev = r
    byF = {r.F: r for r in recs}
    for cp in ("cp1", "cp2", "cp3"):
        f, t = run.cp(cp)
        if t is None:
            e("%s: no tick= on the det_rng_state line" % cp)
            continue
        r = byF.get(f)
        if r is None or r.n != 1 or r.T != t:
            e("%s frame %d tick %d: witness line %s" % (cp, f, t, "absent" if r is None else "n %d T %d" % (r.n, r.T)))
    return err


def tw_consistency(run):
    """Every '* tw' log line is on a witnessed frame and the per-frame count equals the witness tw column (to cp3)."""
    f3 = run.cp("cp3")[0]
    per = {}
    for _, f in run.log().tw:
        per[f] = per.get(f, 0) + 1
    byF = {r.F: r for r in run.wit()}
    bad = []
    for f, n in per.items():
        if f <= f3 and (f not in byF or byF[f].tw != n):
            bad.append("frame %d: log %d, witness %s" % (f, n, byF[f].tw if f in byF else "absent"))
    for r in run.wit():
        if r.F <= f3 and r.tw != per.get(r.F, 0):
            bad.append("frame %d: witness %d, log %d" % (r.F, r.tw, per.get(r.F, 0)))
    return bad[:5]


def pw_L(q):
    return q.pw is not None and q.pw.form == "L"


def lerp_moving(q):
    """S4a's outcome on one frame: the first evaluated skeleton was drawn from a pair, and not as the live bones"""
    return pw_L(q) and q.pw.hd != q.pw.hl


def tick_moving(q):
    return pw_L(q) and q.pw.hp != q.pw.hc


def pa_moved(q):
    """the S4b bone correction moved the item (forms C, R only: form I is the parent-only S1 path, review F1)"""
    return q.pa is not None and q.pa.form in PA_BONE and cheb(q.pa.wd, q.pa.ws) > TOL_POS


def pa_identity_moved(q):
    return q.pa is not None and q.pa.form == "I" and cheb(q.pa.wd, q.pa.ws) > TOL_POS


def pj_link(recs):
    """W2 (reported): lines where the item's drawn bone (pa hd, world) and the mesh's drawn hand (pj, world) are the same
    bone of the same NPC in the same frame: pa has u, cls c or p, ba == pj bone. Returns (count, max Chebyshev)."""
    d = [cheb(q.pa.hd, q.pj.jw) for q in recs if q.pj is not None and q.pa is not None and q.pa.hd is not None
         and q.pa.cls in ("c", "p") and q.pa.ba == q.pj.bone]
    return len(d), (max(d) if d else None)


def hand_steps(recs):
    """per-tick hand step of the witness NPC's item bone on form-C lines: |hd - hr| / (1 - u) for cls c (hr = the newest
    capture), / u for cls p (hr = the previous one); n=0 frames, 0 < u < 1"""
    out = []
    for q in recs:
        a = q.pa
        if q.n != 0 or a is None or a.form != "C" or a.u is None or a.hd is None or not (0.0 < a.u < 1.0):
            continue
        if a.cls == "c":
            out.append(cheb(a.hd, a.hr) / (1.0 - a.u))
        elif a.cls == "p":
            out.append(cheb(a.hd, a.hr) / a.u)
    return out


def pct(v, p):
    if not v:
        return None
    v = sorted(v)
    return v[min(len(v) - 1, int(math.floor(p / 100.0 * (len(v) - 1))))]


def tol_ld_rule(noise, step):
    """prereg 7.1: (a) 2N <= 1e-3 <= S/2: 1e-3; (b) else 4N <= S: sqrt(N S) (both sides >= 2x); (c) else 0 (the cluster
    part of PA2 reported only). N is floored at two print roundings."""
    if step is None:
        return 0.0, "c (no hand step measured)"
    n_ = max(noise, LD_NOISE_FLOOR)
    if 2 * n_ <= TOL_LD0 <= step / 2.0:
        return TOL_LD0, "a"
    if 4 * n_ <= step:
        return math.sqrt(n_ * step), "b"   # unrounded: >= 2x over N and under S exactly
    return 0.0, "c"


def joint_ok(pw, allow_fallback):
    """jd on lerp(jp, jc, u); with allow_fallback also jd == jc (a bone the blend drew as cur: hidden, mirrored...)"""
    if cheb(pw.jd, lerp(pw.jp, pw.jc, pw.u)) <= tol_lerp(pw.jp, pw.jc, pw.jd):
        return True
    return allow_fallback and cheb(pw.jd, pw.jc) <= TOL_POS


def ld_groups(recs):
    groups = {}
    for q in recs:
        if q.pa is not None and q.pa.form == "C":
            groups.setdefault((q.pa.kind, q.pa.ba, q.pa.bb), []).append(q.pa.ld)
    return groups


def ld_leaders(lds, tol):
    leaders, members = [], []
    for v in lds:
        for i, l in enumerate(leaders):
            if cheb(v, l) <= tol:
                members[i].append(v)
                break
        else:
            leaders.append(v)
            members.append([v])
    return leaders, members


def ld_clusters(recs, tol):
    """form-C lines grouped by (kind, ba, bb); leader clusters of ld with radius tol (Chebyshev); per group count"""
    return {g: len(ld_leaders(lds, tol)[0]) for g, lds in ld_groups(recs).items()}


def ld_noise(recs):
    """PROBE_LD_NOISE: per group, the largest leader cluster at radius TOL_LD0; its per-coordinate spread (max - min);
    the max over groups (0.0 with no form-C line)"""
    best = 0.0
    for lds in ld_groups(recs).values():
        _, members = ld_leaders(lds, TOL_LD0)
        m = max(members, key=len)
        best = max(best, max(max(v[i] for v in m) - min(v[i] for v in m) for i in range(3)))
    return best


# ---------------------------------------------------------------- scoring
def score(root):
    V, REP = [], []          # (id, verdict, text); reported lines
    runs = {t: Run(root, t, s) for t, s in RUNS.items()}
    n = lambda k: int(CONST[k])
    NPC = str(CONST["PROBE_NPC"]).lower()
    TOL_LD = float(CONST["PROBE_TOL_LD"])   # 0 = PA2's cluster part reported only (prereg 7.1 branch c)
    JALLOW = n("PROBE_JOINT_LERP") == 0

    def mins(key, K, kind):
        v = n(key)
        if kind == "n0":       # render-only frames scale with K-1 per tick
            return int(math.floor(v * (K - 1) / float(KP - 1) / 3.0))
        if kind == "frame":    # every frame: scales with K
            return int(math.floor(v * K / float(KP) / 3.0))
        return int(math.floor(v / 3.0))   # tick frames: independent of K
    for t, r in runs.items():
        if r.sub:
            REP.append("substitution: %s read from %s" % (t, r.dir))

    def check(cid, fn, nv_if=()):
        for dep in nv_if:
            if any(v[0] == dep and v[1] in ("FAIL", "NO VERDICT") for v in V):
                V.append((cid, "NO VERDICT", "registered consequence of %s not passing" % dep))
                return
        try:
            ok, txt = fn()
        except Missing as m:
            ok, txt = False, str(m)
        V.append((cid, "PASS" if ok else "FAIL", txt))

    # F0 flags
    def f0():
        bad = []
        taken = {}
        for t, r in runs.items():
            try:
                e = flag_errors(r, NPC) + item_errors(r)
                if len(r.log().items) == 1 and r.log().items[0] != "NONE":
                    taken[t] = r.log().items[0]
            except Missing as m:
                e = [str(m)]
            bad += ["%s: %s" % (t, x) for x in e]
        if len(set(taken.values())) > 1:
            bad.append("runs took different items: %s" % sorted(set(taken.values())))
        it = sorted(set(taken.values()))
        return not bad, "; ".join(bad[:8]) or "all %d runs carry exactly their registered switches; every run took item %s" % (
            len(runs), "slot %d section %s" % it[0] if it else "?")
    check("F0", f0)

    # ---- part A (COPIED from s4d1 section 3)
    st = [runs[t] for t in ("s4p_s1", "s4p_s2", "s4p_s3")]
    of = [runs[t] for t in ("s4p_o1", "s4p_o2", "s4p_o3")]

    def a1():
        c = {r.tag: C3(st[0], r) for r in st[1:]}
        return all(v == 0 for x in c.values() for v in x), "C at cp1,2,3 against s4p_s1: %s" % c
    check("A1", a1)

    def a2():
        c = {r.tag: C3(st[0], r) for r in of}
        return all(v == 0 for x in c.values() for v in x), "C at cp1,2,3 against s4p_s1: %s" % c
    check("A2", a2, ("A1",))

    def a3():
        d = {}
        for cp in ("cp1", "cp2", "cp3"):
            d[cp] = sum(r.cp(cp)[0] for r in of) / 3.0 - sum(r.cp(cp)[0] for r in st) / 3.0
        return abs(d["cp1"]) <= CP1_DIFF, "off mean minus stock mean, frames: cp1 %+.1f (gate %d); reported cp2 %+.1f cp3 %+.1f" % (
            d["cp1"], CP1_DIFF, d["cp2"], d["cp3"])
    check("A3", a3, ("A1",))

    def a4():
        fr = {r.tag: r.sched() for r in st + of}
        bad = []
        first = fr["s4p_s1"][0] if fr["s4p_s1"] else None
        for t, F in fr.items():
            g = [b - a for a, b in zip(F, F[1:])]
            if len(F) < ALIFE_MIN_CALLS or F[0] != first or any(x not in (16, 17, 18) for x in g) or g.count(16) != 1:
                bad.append("%s: calls %d first %s gaps %s sixteens %d" % (t, len(F), F[0] if F else None, sorted(set(g)), g.count(16)))
        return not bad, "; ".join(bad) or "calls %s, first frame %s, gaps 16/17/18 with one 16" % ([len(x) for x in fr.values()], first)
    check("A4", a4, ("A1",))

    def a5():
        bad = []
        for r in of:
            lg = r.log()
            if lg.syn or lg.posel:
                bad.append("%s: engine det_synth line present" % r.tag)
            if os.path.exists(r.p("synth_{t}.txt")):
                bad.append("%s: witness file present" % r.tag)
            for cp in ("cp1", "cp2", "cp3"):
                if r.cp(cp)[1] is not None:
                    bad.append("%s: %s has tick=" % (r.tag, cp))
        return not bad, "; ".join(bad) or "no lever line, no witness, no tick= suffix in the three off runs"
    check("A5", a5)

    # ---- part S
    def pat():
        bad = []
        for t in S_ALL:
            if t == "s4p_wc2":
                continue
            bad += ["%s: %s" % (t, x) for x in pattern_errors(runs[t])]
        return not bad, "; ".join(bad[:8]) or "lever pattern holds on every line of %d part-S runs; cp1-3 anchored" % (len(S_ALL) - 1)
    check("PAT", pat)

    def pwc():
        w, a = runs["s4p_wc2"], runs["s4p_on2a"]
        bad = ["pattern: %s" % x for x in pattern_errors(w, ad=False)]
        recs = w.wit()
        fa = sum(1 for r in recs if abs(r.a - r.j / 2.0) > TOL_A) / float(len(recs))
        L = [q for q in recs if pw_L(q)]
        u_off_a = sum(1 for q in L if abs(q.pw.u - q.a) > TOL_A)
        fu = sum(1 for q in L if abs(q.pw.u - q.j / 2.0) > TOL_A) / float(len(L)) if L else 0.0
        if fa < WALLCLOCK_DIFF_MIN:
            bad.append("a differs from j/K on %.3f of lines (must-fail needs >= %.2f)" % (fa, WALLCLOCK_DIFF_MIN))
        if len(L) < PWC_MIN_L:
            bad.append("%d form-L pw lines < %d" % (len(L), PWC_MIN_L))
        if fu < WALLCLOCK_DIFF_MIN:
            bad.append("pose u differs from j/K on %.3f of form-L lines (must-fail needs >= %.2f)" % (fu, WALLCLOCK_DIFF_MIN))
        if u_off_a:
            bad.append("pose u != the frame's alpha on %d form-L lines" % u_off_a)
        fmax = min(w.cp("cp3")[0], a.cp("cp3")[0])
        pw_ = [(r.F, r.T, r.j, r.K, r.n) for r in recs if r.F <= fmax]
        pa_ = [(r.F, r.T, r.j, r.K, r.n) for r in a.wit() if r.F <= fmax]
        if pw_ != pa_:
            bad.append("pattern columns F T j K n differ from on2a's (%d vs %d lines to frame %d)" % (len(pw_), len(pa_), fmax))
        return not bad, "; ".join(bad[:6]) or "MUST-FAIL as predicted: a off j/K on %.4f of lines, pose u off j/K on %.4f of %d form-L lines and = a on all" % (fa, fu, len(L))
    check("PWC", pwc)

    def s0():
        a, b = runs["s4p_off2a"], runs["s4p_off2b"]
        c = C3(a, b)
        dt = a.cp_tick("cp1") - b.cp_tick("cp1")
        return not any(c) and abs(dt) <= CP1_DIFF, "C off2a vs off2b %s; cp1 tick difference %d" % (c, dt)
    check("S0", s0)

    def s0b():
        bad = []
        for t in S_ALL:
            r = runs[t]
            lg = r.log()
            if not (lg.installed and lg.started):
                bad.append("%s: stimulus installed %s start %s" % (t, lg.installed, lg.started))
            if lg.fired < FIRED_MIN:
                bad.append("%s: highest 'bullets fired' %d < %d" % (t, lg.fired, FIRED_MIN))
            if lg.luaerr:
                bad.append("%s: script error line %s" % (t, lg.luaerr[0]))
            xs = [q.s2x for q in r.wit() if q.s2x is not None]
            rng = max(max(v[i] for v in xs) - min(v[i] for v in xs) for i in (0, 2)) if xs else 0.0
            if rng < WALK_MIN_RANGE:
                bad.append("%s: actor x/z range %.3f m < %.1f" % (t, rng, WALK_MIN_RANGE))
        return not bad, "; ".join(bad[:6]) or "stimulus installed, started, >= %d bullets, no script error, walk >= %.1f m in every part-S run" % (FIRED_MIN, WALK_MIN_RANGE)
    check("S0B", s0b)

    def tw_clean(tags):
        try:
            return all(not runs[t].log().tw for t in tags)
        except Missing:
            return False

    def sc():
        res, bad = {}, []
        for on, off in (("s4p_on2a", "s4p_off2a"), ("s4p_on2b", "s4p_off2a"), ("s4p_on3", "s4p_off3")):
            c = C3(runs[on], runs[off])
            dt = runs[on].cp_tick("cp1") - runs[off].cp_tick("cp1")
            res["%s/%s" % (on[4:], off[4:])] = (c, dt)
            if any(c) or abs(dt) > CP1_DIFF:
                bad.append("%s vs %s: C %s cp1 tick diff %d%s" % (on, off, c, dt,
                           (" [" + HINT_SC + "]") if tw_clean((on, off)) else " (tripwire lines present: see TW)"))
        return not bad, "; ".join(bad) or "C = 0 and cp1 within %d ticks: %s" % (CP1_DIFF, res)
    check("SC", sc, ("S0",))

    def lb():
        a, b = runs["s4p_b1off2"], runs["s4p_off2a"]
        c = C3(a, b)
        dt = a.cp_tick("cp1") - b.cp_tick("cp1")
        ok = not any(c) and abs(dt) <= CP1_DIFF
        return ok, "build 1 (b1off2) vs build 2 bit 16 off (off2a): C %s; cp1 tick difference %d%s" % (
            c, dt, "" if ok else " [" + HINT_LB + "]")
    check("LB", lb, ("S0",))

    def tw():
        bad = []
        for t in S_ALL:
            r = runs[t]
            if not r.spec["brk"] and r.log().tw:
                bad.append("%s: %d BAD lines (first %s)" % (t, len(r.log().tw), r.log().tw[0]))
            bad += ["%s: %s" % (t, x) for x in tw_consistency(r)]
        return not bad, "; ".join(bad[:6]) or "0 BAD lines (s2, s3, pose, attach) in every non-break run; log and witness tw column agree in all part-S runs"
    check("TW", tw)

    def twb(tag, want, need_fn, mn, what):
        def f():
            r = runs[tag]
            need = {q.F for q in r.to_cp3() if need_fn(q)}
            badw = {fr for s, fr in r.log().tw if s == want}
            miss = sorted(need - badw)
            oth = [x for x in r.log().tw if x[0] != want]
            ok = len(need) >= mn and not miss and not oth
            return ok, "%s: %d frames %s in its own witness (need >= %d), %d of them without a %s BAD line (need 0; first %s), %s BAD on %d frames, other slices' BAD lines %d (need 0)%s" % (
                tag, len(need), what, mn, len(miss), want, miss[:3], want, len(badw), len(oth), " - MUST-FAIL as predicted" if ok else "")
        return f
    check("TWB16", twb("s4p_brk16", "pose", lerp_moving, mins("PROBE_P_TICK", 2, "tick"),
                       "with pw form L and hd != hl"))

    def brk32_need(q):   # the break fires on every corrected placement, form I included (GameObject.cpp, `corrected`)
        return q.pa is not None and q.pa.form in PA_CORR and q.pa.hxe is not None and q.pa.hx != q.pa.hxe
    f32chk = twb("s4p_brk32", "attach", brk32_need, mins("PROBE_PA_CR", 2, "n0"),
                 "with pa form C/R/I and hx != hxe")

    def twb32():
        ok, txt = f32chk()
        r = runs["s4p_brk32"]
        same = sum(1 for q in r.to_cp3() if q.pa is not None and q.pa.form in PA_CORR and q.pa.hx == q.pa.hxe)
        return ok, txt + "; reported: form C/R/I frames with hx == hxe %d" % same
    check("TWB32", twb32)

    def p1():
        bad, info = [], []
        for t in S_ON:
            r = runs[t]
            K = r.spec["k"]
            m = sum(1 for q in r.wit() if q.n == 0 and lerp_moving(q))
            mn = mins("PROBE_P_LERP", K, "n0")
            if m < mn:
                bad.append("%s: %d n=0 frames drawn between ticks < %d" % (t, m, mn))
            info.append("%s %d (min %d)" % (t[4:], m, mn))
        return not bad, "; ".join(bad) or "n=0 frames with pw form L and hd != hl: %s" % ", ".join(info)
    check("P1", p1)

    def p2():
        bad, info = [], []
        for t in S_ON:
            r = runs[t]
            cnt = {"lines": 0, "hu": 0, "fallback": 0}
            errs = []
            for q in r.wit():
                if not pw_L(q):
                    continue
                w = q.pw
                cnt["lines"] += 1
                e = []
                if w.ct != w.pt + 1:
                    e.append("ctick %s != ptick %s + 1" % (w.ct, w.pt))
                if w.ct != q.T:
                    e.append("ctick %s != T %d" % (w.ct, q.T))
                if abs(w.u - q.a) > TOL_A:
                    e.append("u %.6f != a %.6f" % (w.u, q.a))
                if w.hl != w.hc:
                    e.append("hl != hc (guard)")
                if w.hu is not None:
                    cnt["hu"] += 1
                    if w.hdu is None or w.hu != w.hdu:
                        e.append("hu %s != hdu %s" % (w.hu, w.hdu))
                if not joint_ok(w, JALLOW):
                    e.append("jd %s off lerp(jp, jc, u)" % (w.jd,))
                elif cheb(w.jd, lerp(w.jp, w.jc, w.u)) > tol_lerp(w.jp, w.jc, w.jd):
                    cnt["fallback"] += 1
                if e:
                    errs.append("F %d: %s" % (q.F, ", ".join(e)))
            if errs:
                bad.append("%s: %d form-L lines off the rules (first %s)" % (t, len(errs), errs[0]))
            info.append("%s %d lines (hu present %d, joint fallbacks %d)" % (t[4:], cnt["lines"], cnt["hu"], cnt["fallback"]))
        return not bad, "; ".join(bad[:6]) or "every form-L pw line: pair of consecutive ticks ending at T, u = a, guard, hu = hdu, joint on the lerp%s: %s" % (
            " or drawn as cur (probe saw fallbacks)" if JALLOW else "", ", ".join(info))
    check("P2", p2)

    def p3():
        bad, info = [], []
        for t in S_ON:
            r = runs[t]
            L1 = [q for q in r.wit() if q.n == 1 and pw_L(q)]
            off = [q.F for q in L1 if q.pw.hd != q.pw.hp]
            m = sum(1 for q in L1 if tick_moving(q))
            mn = mins("PROBE_P_TICK", r.spec["k"], "tick")
            if off:
                bad.append("%s: tick frames with form L and hd != hp: %d (first F %d)" % (t, len(off), off[0]))
            if m < mn:
                bad.append("%s: %d tick frames with form L and hp != hc < %d" % (t, m, mn))
            info.append("%s %d" % (t[4:], m))
        return not bad, "; ".join(bad) or "tick frames draw the previous tick's pose (hd = hp); moving tick frames %s" % ", ".join(info)
    check("P3", p3)

    def p4():
        bad, info = [], []
        for t in S_OFF:
            r = runs[t]
            recs = r.wit()
            nz = [q.F for q in recs if any(q.p) or q.pw is not None]
            upd = [q.F for q in recs if q.up is not None and q.up[0] != q.up[1]]
            nup = sum(1 for q in recs if q.up is not None)
            mn = mins("PROBE_UPLOADS", r.spec["k"], "frame")
            if nz:
                bad.append("%s: p column not all zero or pw present on %d lines (first F %d)" % (t, len(nz), nz[0]))
            if upd:
                bad.append("%s: uploaded != live on %d frames (first F %d)" % (t, len(upd), upd[0]))
            if nup < mn:
                bad.append("%s: %d frames with an upload < %d" % (t, nup, mn))
            info.append("%s %d" % (t[4:], nup))
        return not bad, "; ".join(bad) or "bit 16 off: no skeleton evaluated, uploaded = live on every upload frame (%s)" % ", ".join(info)
    check("P4", p4)

    def u1():
        bad, info = [], []
        for t in S_ON + S_OFF:
            r = runs[t]
            recs = r.wit()
            if any(q.uu is None for q in recs):
                bad.append("%s: no uu column (required witness addition W1 not in this exe)" % t)
                continue
            nd = [q.F for q in recs if q.uu[1] or q.uu[2]]
            if nd:
                bad.append("%s: an unmarked skeleton drawn off its live bones or holding storage on %d frames (first F %d)" % (t, len(nd), nd[0]))
            if t in S_ON:
                m = sum(1 for q in recs if q.n == 0 and q.uu[3] > 0)
                mn = mins("PROBE_U_POSED", r.spec["k"], "n0")
                if m < mn:
                    bad.append("%s: %d n=0 frames with an unmarked skeleton posed between ticks < %d" % (t, m, mn))
                info.append("%s %d" % (t[4:], m))
        return not bad, "; ".join(bad[:6]) or "unmarked skeletons uploaded live, never stored; posed between ticks with bit 16 on: %s" % ", ".join(info)
    check("U1", u1)

    def pa1():
        bad = []
        for t in S_ON + S_OFF:
            r = runs[t]
            on = bool(r.spec["mask"] & 16)
            errs = []
            for q in r.wit():
                a = q.pa
                if a is None:
                    continue
                e = []
                if a.hxe is not None and a.hx != a.hxe:
                    e.append("hx != hxe (XFORM changed after the child rule)")
                if a.form in ("P", "O", "S") and cheb(a.wd, a.ws) > TOL_POS:
                    e.append("form %s with wd != ws" % a.form)
                if on:
                    if a.form in PA_CORR:
                        if a.cls not in ("c", "p"):
                            e.append("form %s with cls %s" % (a.form, a.cls))
                        if a.u is None or abs(a.u - q.a) > TOL_A:
                            e.append("form %s with u %s != a %.6f" % (a.form, a.u, q.a))
                    if a.form == "I" and a.hd is not None and cheb(a.hd, a.hr) > TOL_POS:
                        # I needs A_d == A_c bitwise; every frame's origin is bone_a's (GameObject.cpp sim_attach_hands
                        # F.c = mR.c, or da / ta, or translate(da.c)), so the drawn and the read bone_a coincide
                        e.append("form I with hd != hr (not the bones-as-read path)")
                    if a.form == "O" and a.cls != "o":
                        e.append("form O with cls %s" % a.cls)
                else:
                    if a.form not in ("S", "P") or a.cls != "-" or a.u is not None:
                        e.append("bit 16 off: form %s cls %s u %s" % (a.form, a.cls, a.u))
                if e:
                    errs.append("F %d: %s" % (q.F, ", ".join(e)))
            if errs:
                bad.append("%s: %d pa lines off the rules (first %s)" % (t, len(errs), errs[0]))
        return not bad, "; ".join(bad[:6]) or "pa lines in their registered forms (I with hd = hr); XFORM unchanged by the render; uncorrected forms at the 4b position"
    check("PA1", pa1)

    def pa2():
        bad, info = [], []
        mxc = n("PROBE_PA_CLUSTERS")
        for t in S_ON:
            r = runs[t]
            recs = r.wit()
            m = sum(1 for q in recs if q.n == 0 and pa_moved(q))
            mn = mins("PROBE_PA_CR", r.spec["k"], "n0")
            if m < mn:
                bad.append("%s: %d n=0 frames with the item moved onto the drawn hand < %d" % (t, m, mn))
            if TOL_LD > 0:
                cl = ld_clusters(recs, TOL_LD)
                over = {g: c for g, c in cl.items() if c > mxc}
                if over:
                    bad.append("%s: ld leader-clusters (radius %g) per (kind, ba, bb) %s > %d" % (t, TOL_LD, over, mxc))
                info.append("%s %d frames, clusters %s" % (t[4:], m, sorted(cl.values())))
            else:
                cl = ld_clusters(recs, TOL_LD0)
                info.append("%s %d frames, clusters at %g %s (cluster part reported only)" % (t[4:], m, TOL_LD0, sorted(cl.values())))
        return not bad, "; ".join(bad) or "the item is moved by a bone correction (C/R) and %s: %s" % (
            "drawn at a fixed offset in the drawn placement frame" if TOL_LD > 0 else
            "PA2 cluster part reported only (PROBE_TOL_LD 0, prereg 7.1 branch c)", "; ".join(info))
    check("PA2", pa2)

    # ---- reported
    for t in S_ON + ("s4p_brk16", "s4p_brk32", "s4p_wc2"):
        try:
            recs = runs[t].wit()
            n0 = [q for q in recs if q.n == 0]
            rod = sum(q.p[4] for q in n0)
            roe = sum(q.p[5] for q in n0)
            ev = sum(q.p[0] for q in recs)
            pwf = {}
            for q in recs:
                k_ = q.pw.form if q.pw else "-"
                pwf[k_] = pwf.get(k_, 0) + 1
            paf, cls = {}, {}
            for q in recs:
                if q.pa:
                    paf[q.pa.kind + q.pa.form] = paf.get(q.pa.kind + q.pa.form, 0) + 1
                    cls[q.pa.cls] = cls.get(q.pa.cls, 0) + 1
            ldls = max([cheb(q.pa.ld, q.pa.ls) for q in recs if q.pa and q.pa.ld] or [0.0])
            rig = [abs(eucl(q.pa.wd, q.pa.hd) - norm(q.pa.ld)) for q in recs
                   if q.pa and q.pa.form == "C" and q.pa.kind in ("B", "T", "N")]
            pn = sum(1 for q in recs if q.pn is not None)
            nI = sum(1 for q in recs if q.pa and q.pa.form == "I")
            nIm = sum(1 for q in recs if q.n == 0 and pa_identity_moved(q))
            if any(q.ntok == NTOK_PJ for q in recs):
                ln, lmax = pj_link(recs)
                w2 = "W2 hand link: %d comparable lines, max |pa hd - pj| %s" % (ln, "%.5f" % lmax if lmax is not None else "-")
            else:
                w2 = "W2 hand link: pj column absent"
            REP.append("%s: form I (parent moved, bones as read; not a bone correction) %d lines, %d n=0 moved; %s" % (t, nI, nIm, w2))
            REP.append("%s: skeletons evaluated %d (guard %d, snap %d); render-only recalcs byte-different %d, byte-equal %d (F5); "
                       "pw forms %s; pa kind+form %s; cls %s; max |ld - ls| %.5f (tautology, rounding only); "
                       "B/T/N | |wd-hd| - |ld| | max %s; pn present on %d of %d lines" % (
                           t, ev, sum(q.p[2] for q in recs), sum(q.p[3] for q in recs), rod, roe, pwf, paf, cls, ldls,
                           "%.5f" % max(rig) if rig else "-", pn, len(recs)))
        except Missing as m:
            REP.append("%s: %s" % (t, m))
    for a, b in (("s4p_on2a", "s4p_off2a"), ("s4p_on3", "s4p_off3")):
        try:
            A = {q.T: q for q in runs[a].wit() if q.n == 1 and q.pn}
            B = {q.T: q for q in runs[b].wit() if q.n == 1 and q.pn}
            com = sorted(set(A) & set(B))
            eq = sum(1 for T in com if A[T].pn.hsim == B[T].pn.hsim)
            REP.append("pn hsim on tick frames %s vs %s: common %d, equal %d (online state, not gated)" % (a, b, len(com), eq))
        except Missing as m:
            REP.append("pn hsim %s vs %s: %s" % (a, b, m))
    for lab, x, y in (("K=2 vs K=3 on", "s4p_on2a", "s4p_on3"), ("K=2 vs K=3 off", "s4p_off2a", "s4p_off3"),
                      ("build 1 vs build 2 off, second arm", "s4p_b1off2", "s4p_off2b"),
                      ("wallclock vs on2a", "s4p_wc2", "s4p_on2a")):
        try:
            REP.append("%s (%s vs %s): C at cp1,2,3 %s" % (lab, x, y, C3(runs[x], runs[y])))
        except Missing as m:
            REP.append("%s: %s" % (lab, m))
    for a, b in (("s4p_off2a", "s4p_off2b"), ("s4p_on2a", "s4p_off2a"), ("s4p_on3", "s4p_off3"),
                 ("s4p_b1off2", "s4p_off2a"), ("s4p_brk16", "s4p_off2a"), ("s4p_brk32", "s4p_off2a")):
        try:
            od = [online_diff(runs[a], runs[b], k) for k in (1, 2, 3)]
            same = runs[a].log().rng_lines == runs[b].log().rng_lines
            REP.append("online objects differing %s vs %s at cp1,2,3: %s; generator/Lua state lines %s; C %s" % (
                a, b, od, "IDENTICAL" if same else "DIFFER", C3(runs[a], runs[b])))
        except Missing as m:
            REP.append("online diff %s vs %s: %s" % (a, b, m))
    return V, REP


# ---------------------------------------------------------------- probe readings (mode=probe)
def probe_mode(root):
    out, stop = [], []

    def st(code, msg):
        stop.append("PROBE STOP %s: %s" % (code, msg))
    r = Run(root, PROBE_TAG, PROBE_RUN)
    try:
        lg = r.log()
        npc = lg.posel[0] if lg.posel else "?"
        for e in flag_errors(r, npc):
            st("flags", e)
        for e in pattern_errors(r):
            st("pattern", e)
        recs = r.wit()
    except Missing as m:
        st("input", "%s: %s" % (r.tag, m))
        out.append("S4D2 PROBE: no readings")
        return out, stop
    if any(q.uu is None for q in recs):
        st("w1", "no uu column: the required witness addition W1 is not in this exe; U1 cannot be scored (prereg 1.3)")
    if not (lg.installed and lg.started) or lg.fired < FIRED_MIN or lg.luaerr:
        st("stimulus", "installed %s start %s fired %d script errors %s" % (lg.installed, lg.started, lg.fired, lg.luaerr[:1]))
    for e in item_errors(r):
        st("item", e)
    xs = [q.s2x for q in recs if q.s2x is not None]
    rng = max(max(v[i] for v in xs) - min(v[i] for v in xs) for i in (0, 2)) if xs else 0.0
    if rng < WALK_MIN_RANGE:
        st("walk", "actor x/z range %.3f m < %.1f" % (rng, WALK_MIN_RANGE))
    if lg.tw:
        st("tripwire", "%d BAD lines with no break (first %s)" % (len(lg.tw), lg.tw[0]))
    n0 = [q for q in recs if q.n == 0]
    # (a) skeletons drawn and blended headless
    ev = sum(q.p[0] for q in recs)
    ev0 = sum(q.p[0] for q in n0)
    nl0 = sum(q.p[1] for q in n0)
    p_lerp = sum(1 for q in n0 if lerp_moving(q))
    p_tick = sum(1 for q in recs if q.n == 1 and tick_moving(q))
    uploads = sum(1 for q in recs if q.up is not None)
    if p_lerp < DROP_MIN or p_tick < DROP_MIN:
        st("pose", "pw: %d n=0 frames drawn between ticks, %d moving tick frames (each needs >= %d): no skeleton is "
           "shown blended headless (design D9: camera-heading lever, or Caden's eyes only)" % (p_lerp, p_tick, DROP_MIN))
    if uploads < DROP_MIN:
        st("uploads", "%d frames with an up column < %d" % (uploads, DROP_MIN))
    # (b) F5: render-only-frame recalculations byte-equal vs different
    rod = sum(q.p[4] for q in n0)
    roe = sum(q.p[5] for q in n0)
    f5 = rod / float(rod + roe) if rod + roe else 0.0
    if f5 > F5_DIFF_MAX:
        st("f5", "render-only recalcs byte-different %d of %d (%.3f > %.2f): armed NPCs lose their history every frame "
           "(design D6: fix F5 in xrGame before registration)" % (rod, rod + roe, f5, F5_DIFF_MAX))
    # (c) the witness NPC, its item, the pa forms and classes
    pn = sum(1 for q in recs if q.pn is not None)
    pa = [q for q in recs if q.pa is not None]
    paf, cls, kinds = {}, {}, {}
    for q in pa:
        paf[q.pa.form] = paf.get(q.pa.form, 0) + 1
        cls[q.pa.cls] = cls.get(q.pa.cls, 0) + 1
        kinds[q.pa.kind] = kinds.get(q.pa.kind, 0) + 1
    if pn < NPC_MIN_SHARE * len(recs):
        st("npc", "pn present on %d of %d lines: the witness NPC %s is not found (re-probe once with the fallback NPC, prereg 2.2)" % (pn, len(recs), npc))
    pa_cr = sum(1 for q in n0 if pa_moved(q))
    if pa_cr < DROP_MIN:
        st("attach", "%d n=0 frames with pa form C/R (bone corrections; form I not counted) and wd != ws < %d: the S4b correction is not shown on the witness "
           "NPC's item (re-probe once with the fallback NPC; then the Overseer)" % (pa_cr, DROP_MIN))
    cl = ld_clusters(recs, TOL_LD0)
    mxc = max(cl.values()) if cl else 0
    noise = ld_noise(recs)
    steps = hand_steps(recs)
    step = pct(steps, LD_STEP_PCT)
    tol, branch = tol_ld_rule(noise, step)
    cl_t = ld_clusters(recs, tol) if tol > 0 else cl
    mxc_t = max(cl_t.values()) if cl_t else 0
    nI = sum(1 for q in pa if q.pa.form == "I")
    nIm = sum(1 for q in n0 if pa_identity_moved(q))
    w2n, w2max = pj_link(recs)
    if mxc > CLUSTERS_MAX or mxc_t > CLUSTERS_MAX:
        st("ld", "ld leader-clusters per group %s (radius %g), %s (radius %g) > %d: the recorded bones are not the ones the "
           "placement used (back to the code)" % (cl, TOL_LD0, cl_t, tol, CLUSTERS_MAX))
    hxd = [q.F for q in pa if q.pa.hxe is not None and q.pa.hx != q.pa.hxe]
    if hxd:
        st("hx", "hx != hxe on %d pa lines without a break (first F %d): the item's XFORM changes after the child rule in stock; PA1's hx rule is unusable" % (len(hxd), hxd[0]))
    L = [q for q in recs if pw_L(q)]
    jfall = sum(1 for q in L if not joint_ok(q.pw, False))
    uposed = sum(1 for q in n0 if q.uu is not None and q.uu[3] > 0)
    if all(q.uu is not None for q in recs) and uposed < DROP_MIN:
        st("optin", "%d n=0 frames with an unmarked skeleton posed between ticks < %d: no stand-in for a self-posed model "
           "on screen (with -det_synth_item in the stimulus the HUD hands should be drawn; see the item line, prereg 2.2)" % (uposed, DROP_MIN))
    ud = sum(1 for q in recs if q.uu is not None and (q.uu[1] or q.uu[2]))
    vals = {"PROBE_P_LERP": p_lerp, "PROBE_P_TICK": p_tick, "PROBE_UPLOADS": uploads, "PROBE_PA_CR": pa_cr,
            "PROBE_PA_CLUSTERS": mxc_t, "PROBE_JOINT_LERP": 0 if jfall else 1, "PROBE_U_POSED": uposed, "PROBE_NPC": npc,
            "PROBE_TOL_LD": tol}
    out.append("probe readings, %s (rules: prereg section 2.3):" % PROBE_TAG)
    out.append("  (a) skeletons evaluated %d (on n=0 frames %d, of them L %d); pw forms L %d; n=0 drawn between ticks %d; moving tick frames %d; upload frames %d" % (
        ev, ev0, nl0, len(L), p_lerp, p_tick, uploads))
    out.append("  (b) F5: render-only recalcs byte-different %d, byte-equal %d (share different %.3f)" % (rod, roe, f5))
    out.append("  (c) witness NPC %s: pn on %d of %d lines; pa on %d lines; pa kinds %s; forms %s; cls %s; n=0 C/R moved %d; ld clusters %s" % (
        npc, pn, len(recs), len(pa), kinds, paf, cls, pa_cr, cl))
    out.append("  form I (parent moved, bones as read; NOT counted as a correction): %d pa lines, %d n=0 moved" % (nI, nIm))
    out.append("  TOL_LD rule (prereg 7.1): PROBE_LD_NOISE %.6f (floor %g), PROBE_HAND_STEP (p%d of %d steps) %s -> branch %s, "
               "PROBE_TOL_LD %g%s; clusters at it %s" % (noise, LD_NOISE_FLOOR, LD_STEP_PCT, len(steps),
               "%.6f" % step if step is not None else "-", branch, tol,
               " (cluster part of PA2 REPORTED only)" if tol == 0 else "", cl_t))
    out.append("  W2 hand link: %s" % ("%d comparable lines, max |pa hd - pj| %s" % (w2n, "%.5f" % w2max if w2max is not None else "-")
               if any(q.ntok == NTOK_PJ for q in recs) else "pj column absent"))
    out.append("  opt-in (W1): %s; unmarked posed between ticks on %d n=0 frames; frames with an unmarked skeleton off live or stored %d" % (
        "uu present" if all(q.uu is not None for q in recs) else "uu ABSENT", uposed, ud))
    out.append("  joint: form-L lines with jd off the lerp %d of %d; hx != hxe lines %d" % (jfall, len(L), len(hxd)))
    out.append("  (d) build 1's held-item question (s4): parsed for format only, not re-tested here")
    out.append("  highest 'bullets fired': %d; actor x/z range %.3f m; witness lines %d" % (lg.fired, rng, len(recs)))
    out.append("  item (-det_synth_item): %s" % ("; ".join("NONE" if i == "NONE" else "slot %d section %s" % i for i in lg.items) or "no item line"))
    out.append("CONST values to register:")
    for k_, v in vals.items():
        out.append('    "%s": %r,' % (k_, v))
    return out, stop


def main(argv):
    args = dict(a.split("=", 1) for a in argv if "=" in a)
    if len(args) != len(argv) or "out" not in args or "root" not in args or set(args) - {"out", "root", "mode"} \
            or args.get("mode", "score") not in ("score", "probe"):
        print("usage: s4d2_score.py root=<dir> out=<file> | mode=probe root=<dir> out=<file>")
        print("S4D2 RESULT: REFUSED (usage)")
        return 2
    outp = args["out"]
    if os.path.exists(outp):
        print("S4D2 RESULT: REFUSED (out file %s exists; never overwritten)" % outp)
        return 2
    if args.get("mode") == "probe":
        if not os.path.isdir(args["root"]):
            print("S4D2 PROBE: REFUSED (root %s is not a folder)" % args["root"])
            return 2
        out, stop = probe_mode(args["root"])
        txt = "\n".join(out + stop + ["S4D2 PROBE: " + ("STOP (%d rules)" % len(stop) if stop else "GO")])
        with open(outp, "w") as f:
            f.write(txt + "\n")
        print(txt)
        return 1 if stop else 0
    ph = [k for k, v in CONST.items() if isinstance(v, str) and v.startswith("PROBE_")]
    if ph:
        print("S4D2 RESULT: REFUSED (placeholders not filled: %s)" % ", ".join(ph))
        return 2
    if not os.path.isdir(args["root"]):
        print("S4D2 RESULT: REFUSED (root %s is not a folder)" % args["root"])
        return 2
    V, REP = score(args["root"])
    fails = [v for v in V if v[1] == "FAIL" and v[0] not in HARNESS]
    hfail = [v for v in V if v[1] == "FAIL" and v[0] in HARNESS]
    nv = [v for v in V if v[1] == "NO VERDICT"]
    if fails:
        res, code = "FAIL", 1
    elif hfail or nv:
        res, code = "NO VERDICT", 3
    else:
        res, code = "PASS", 0
    lines = ["%s %s: %s" % v for v in V] + ["reported: " + x for x in REP]
    lines.append("S4D2 RESULT: %s (%d of %d gated checks PASS; FAIL %s; harness FAIL %s; NO VERDICT %s)" % (
        res, sum(1 for v in V if v[1] == "PASS"), len(V), [v[0] for v in fails] or "-",
        [v[0] for v in hfail] or "-", [v[0] for v in nv] or "-"))
    txt = "\n".join(lines)
    with open(outp, "w") as f:
        f.write(txt + "\n")
    print(txt)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
