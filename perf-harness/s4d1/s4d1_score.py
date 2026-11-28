#!/usr/bin/env python3
# s4d1 scorer (PREREG-s4d1-leftovers-build1-2026-11-28.md). Stdlib only; run with python3 -I. Imports nothing from tools/.
#
#   python3 -I s4d1_score.py root=<folder holding det_<tag> run folders> out=<new output file>     (score, ONCE)
#   python3 -I s4d1_score.py mode=probe root=<folder holding det_s4d_probe and det_s4d_probe15> out=<new output file>
#
# Score exit codes: 0 PASS, 1 FAIL, 3 NO VERDICT, 2 REFUSED (usage, out exists, a PROBE_ constant still a placeholder).
# Probe exit codes: 0 GO, 1 STOP (each failed rule printed as "PROBE STOP <code>: ..."), 2 REFUSED. The probe mode also
# prints the s4 (held item) description and names the registered case the data matches; that part is DESCRIPTIVE and
# never changes the exit code (Overseer 2026-11-27: the nudge is measured, not gated). Prereg Amendment 1 (2026-11-28):
# the stop rule "item" on both probes reads only WHETHER an item was taken and s4 is present (ITEM_SHARE), never its values.
# The output is written to out= in this same call; an existing out file is refused, never overwritten.
# A missing or unreadable input file fails the checks that read it BY NAME (never a silent skip, never read as 0).
# A run folder <root>/det_<tag>_r1 (the series' one retry) is read instead of det_<tag> and the substitution is named.
# Every reader strips \r (the engine log and the Lua-written files are CRLF under Wine; the witness is LF).
#
# Counter reading mirrors tools/counters_diff.py (sha of the copy read: see prereg section 9): read_lines 23-25,
# load_alife 28-38, load_counters 41-50, compare_ids 53-63, and the EXACT part of compare_k 86-90 (stream lines; upd and
# sq ids offline in both runs). Informational items (registered/unregistered totals, info lines, online ids) are not
# in C, as there. Online object diff (reported only) mirrors tools/det_diff.py load 6-15 and the per-object compare 21-28.
# A-Life call frames mirror tools/s2_partA2_score.py line 22 (' alife_simulator ' lines of det_trace_<tag>.txt.sched).
import math
import os
import re
import struct
import sys

# ---- constants the probe supplies (prereg section 2.3; each with its rule there). Scoring REFUSES while any value is
# still the placeholder string. ----
CONST = {
    "PROBE_S2_MOVING": 10270,                      # probe: n=0 frames with |s2 draw - s2 xform| > TOL_POS
    "PROBE_S2_LAG": 0,                             # probe: tick lag L of the drawn pair (0, 1 or 2); -1 = S2 (ii) reported
    "PROBE_S3_FRAMES": 2746,                       # probe: n=0 frames with a tracer drawn and |head - pos| > TOL_POS
    "PROBE_S1_FRAMES": 15405,                      # probe: frames with the s1 columns present
    "PROBE_ALIFE_GAP16": 0,                        # probe: number of 16-tick gaps between A-Life calls (0 or 1)
}
# ---- registered constants (derivations in the prereg, margin table section 7) ----
KP = 3                    # the probe's K
DROP_MIN = 300            # a probe count under this drops its check ("Caden's eyes only") or stops the series
TOL_POS = 1e-4            # positions are printed with 5 decimals: "equal" = every coordinate within 1e-4 m
ULP_K = 4                 # lerp checks add 4 float32 ulps of the largest coordinate (far tracers, see prereg 4.4)
TOL_A = 1.5e-6            # alpha and dt are printed with 6 decimals
TOL_HUD = 2e-3            # hud is printed with 3 decimals
CP1_DIFF = 8              # cp1 frame (part A) or tick (L, S) difference; one A-Life pass = 17 (COPIED from 4b R3)
ALIFE_MIN_CALLS = 50      # A-Life calls traced up to cp1 (stock: 100) -> 2x
WALK_MIN_RANGE = 1.5      # m: the walk spans 3.0 m along x -> 2x
FIRED_MIN = 50            # highest "N bullets fired" line; expected ~171 [G] -> 3.4x, probe-checked
WALLCLOCK_DIFF_MIN = 0.90  # share of l2w lines whose a (and dt) differ from j/K (0.033/K)
TWB2_MIN_N = 30           # brk2: its OWN witness must show >= 30 frames whose first drawn tracer differs from bullet_pos
S4_THETA = 0.05           # m: probe reader, item step "0" vs "d" (a tenth of the ~0.5 m nudge, desk note section 0)
S4_DRAW_THETA = 0.25      # m: probe reader, draw "near" vs "far" (half the ~0.5 m nudge; the walk's offset is taken out)
S4_SHARE = 0.80           # probe reader: share of ticks that must carry the majority signature
S4_ABSENT_SHARE = 0.90    # probe reader: draw "absent" when >= 90 % of item frames have no draw matrix
LAG_SHARE = 0.99
TEARDOWN_MAX = 2          # trailing no-actor witness lines after cp3 dropped per run (the quit's teardown frame, not a
                          # simulated frame): observed 1 per boot on 2 of 2 real boots (det_s4d_probe, det_s4d_probe15,
                          # 2026-11-28) -> 2x margin. More than this: nothing dropped, the existing rules read them.
ITEM_SHARE = 0.50         # probe rule "item" (prereg Amendment 1): share of witness lines with s4 present, from the
                          # stimulus start frame (first witness line with hud >= the start line's `time`, item_share)
                          # to the cp3 frame inclusive. Real window of the 2026-11-28 probes: F 815..16247 = 15,433
                          # lines (s4d_probe), F 815..16229 = 15,415 (s4d_probe15), K = 3, 11 ms of hud per line. The
                          # item is taken at that very update; even if s4 were dashes for a whole draw animation (at
                          # most ~3 s [G] = ~273 lines), share >= (15433 - 273) / 15433 = 0.98. No item held: 0.00 (both
                          # 2026-11-28 probes: s4 on 0 lines). 0.50 = expected / 1.96 and 0.50 above the no-item value:
                          # ~2x from each side.
STOCK_EXE = "stance14.exe"
NEW_EXE = "simtick18.exe"
BASE = ["-det_mode", "-det_passes", "100,200,320", "-det_lua_streams", "-det_counters"]
HARNESS = ("A1", "L3", "S0")   # a FAIL here = harness not usable: dependents NO VERDICT, result NO VERDICT


def R(part, exe, k=0, mask=None, wc=0, tw=0, brk=0, stim=0):
    return {"part": part, "exe": exe, "k": k, "mask": mask, "wc": wc, "tw": tw, "brk": brk, "stim": stim}


RUNS = {
    "s4d_s1": R("A", STOCK_EXE), "s4d_o1": R("A", NEW_EXE), "s4d_s2": R("A", STOCK_EXE), "s4d_o2": R("A", NEW_EXE),
    "s4d_s3": R("A", STOCK_EXE), "s4d_o3": R("A", NEW_EXE),
    "s4d_l2a": R("L", NEW_EXE, 2, 0), "s4d_l2b": R("L", NEW_EXE, 2, 0), "s4d_l3": R("L", NEW_EXE, 3, 0),
    "s4d_l1": R("L", NEW_EXE, 1, 0), "s4d_l2w": R("L", NEW_EXE, 2, 0, wc=1),
    "s4d_off2a": R("S", NEW_EXE, 2, 0, tw=1, stim=1), "s4d_on2a": R("S", NEW_EXE, 2, 7, tw=1, stim=1),
    "s4d_off3": R("S", NEW_EXE, 3, 0, tw=1, stim=1), "s4d_on3": R("S", NEW_EXE, 3, 7, tw=1, stim=1),
    "s4d_off2b": R("S", NEW_EXE, 2, 0, tw=1, stim=1), "s4d_on2b": R("S", NEW_EXE, 2, 7, tw=1, stim=1),
    "s4d_brk1": R("S", NEW_EXE, 2, 7, tw=1, brk=1, stim=1), "s4d_brk2": R("S", NEW_EXE, 2, 7, tw=1, brk=2, stim=1),
}
PROBE_TAG = "s4d_probe"                                # mask 7: nudge experiment (bit 8) off, as shipped
PROBE_RUN = R("P", NEW_EXE, KP, 7, tw=1, stim=1)
PROBE15_TAG = "s4d_probe15"                            # mask 15: bit 8 on, only to see the s4 pattern with it
PROBE15_RUN = R("P", NEW_EXE, KP, 15, tw=1, stim=1)
S_OFF = ("s4d_off2a", "s4d_off2b", "s4d_off3")
S_BODY = ("s4d_on2a", "s4d_on2b", "s4d_on3")      # mask 7: bits 1, 2, 4 on (8 off)


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
                 "s3ok", "s1", "s4", "s4d", "tw")


LIT = {0: "F", 2: "T", 4: "j", 6: "K", 8: "n", 10: "a", 12: "dt", 14: "hud", 16: "|", 17: "s2", 27: "|", 28: "s3",
       40: "|", 41: "s1", 47: "|", 48: "s4", 55: "|", 56: "tw"}
NTOK = 58


def vec(t):
    return (float(t[0]), float(t[1]), float(t[2]))


def parse_w(line):
    t = line.split()
    if len(t) != NTOK:
        raise ValueError("%d tokens, expected %d" % (len(t), NTOK))
    for i, s in LIT.items():
        if t[i] != s:
            raise ValueError("token %d is %r, expected %r" % (i, t[i], s))
    r = W()
    r.F, r.T, r.j, r.K, r.n = int(t[1]), int(t[3]), int(t[5]), int(t[7]), int(t[9])
    r.a, r.dt, r.hud = float(t[11]), float(t[13]), float(t[15])
    r.lever = " ".join(t[0:16])
    s2 = t[18:27]
    if all(x == "-" for x in s2):
        r.s2d = r.s2x = r.s2c = None
    else:
        r.s2d, r.s2x, r.s2c = vec(s2[0:3]), vec(s2[3:6]), vec(s2[6:9])
    s3 = t[29:40]
    r.s3h = r.s3p = r.s3s = r.s3ok = None
    if all(x == "-" for x in s3):
        r.s3n = None
    elif s3[0] == "0" and all(x == "-" for x in s3[1:]):
        r.s3n = 0
    else:
        r.s3n = int(s3[0])
        r.s3h, r.s3p, r.s3s = vec(s3[1:4]), vec(s3[4:7]), vec(s3[7:10])
        r.s3ok = int(s3[10])
        if r.s3n < 1 or r.s3ok not in (0, 1):
            raise ValueError("s3 drawn %d stamp %d" % (r.s3n, r.s3ok))
    s1 = t[42:47]
    r.s1 = None if all(x == "-" for x in s1) else (float(s1[0]), vec(s1[1:4]), float(s1[4]))
    s4 = t[49:55]   # item XFORM().c, then where it was drawn this frame; three forms only (device.cpp, 7b1232ffc)
    if all(x == "-" for x in s4):
        r.s4 = r.s4d = None
    elif all(x == "-" for x in s4[3:]):
        r.s4, r.s4d = vec(s4[0:3]), None
    else:
        r.s4, r.s4d = vec(s4[0:3]), vec(s4[3:6])
    r.tw = int(t[57])
    return r


def teardown_split(recs, f3):
    """(kept, dropped, note): the trailing block of lines after the cp3 frame f3 with no actor (s2 all dashes) is the
    quit's teardown, not a simulated frame; dropped when it has 1..TEARDOWN_MAX lines. f3 None: nothing dropped."""
    if f3 is None:
        return recs, 0, None
    i = len(recs)
    while i > 0 and recs[i - 1].F > f3 and recs[i - 1].s2d is None:
        i -= 1
    nb = len(recs) - i
    if nb > TEARDOWN_MAX:
        return recs, 0, "%d trailing no-actor lines after cp3 frame %d (F %d..%d) > TEARDOWN_MAX %d: none dropped" % (
            nb, f3, recs[i].F, recs[-1].F, TEARDOWN_MAX)
    return recs[:i], nb, None


# ---------------------------------------------------------------- engine log
RNG_RE = re.compile(r"det_rng_state (\w+): Random=(-?\d+) Random32=(\d+) alife=(\d+) crt_next=(-?\d+) frame=(\d+) "
                    r"time_ms=(\d+)(?: tick=(\d+))?\s*$")
SYN_RE = re.compile(r"\* det_synth: k (\d+) wallclock (\d+) tw (\d+) break (\d+) mask (\d+)\s*$")
TW_RE = re.compile(r"\* tw (s\d) BAD (\d+)\s*$")
FIRED_RE = re.compile(r"det_synth stimulus: (\d+) bullets fired")
START_RE = re.compile(r"det_synth stimulus: start at update \d+ \(.*?\) time (\d+) ")
# zz_eperf_synth.script:78: "item slot <n> section <name>" or "item NONE (no filled slot)"; :102 installed "..., item <b>)"
ITEM_RE = re.compile(r"det_synth stimulus: item (?:slot (\d+) section (\S+)|(NONE)\b)")
INST_ITEM_RE = re.compile(r"det_synth stimulus: installed \(.*\bitem (true|false)\)")


class Log:
    def __init__(self, lines):
        self.cmd = None
        self.syn = []
        self.rng = {}
        self.rng_lines = []
        self.tw = []
        self.installed = self.started = False
        self.inst_item = None      # "true" / "false" from the installed line, None if it has no item part
        self.t0 = None             # time_global (ms) of the stimulus start line
        self.item = None           # None: no item line; "NONE"; or "slot <n> section <name>" (the item taken)
        self.fired = 0
        self.luaerr = []
        for l in lines:
            if self.cmd is None and "command line" in l:
                self.cmd = l.lower().split()
            m = SYN_RE.search(l)
            if m:
                self.syn.append(tuple(int(x) for x in m.groups()))
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
                m = START_RE.search(l)
                if m and self.t0 is None:
                    self.t0 = int(m.group(1))
            m = INST_ITEM_RE.search(l)
            if m:
                self.inst_item = m.group(1)
            m = ITEM_RE.search(l)
            if m and self.item is None:
                self.item = "NONE" if m.group(3) else "slot %s section %s" % (m.group(1), m.group(2))
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
        self.td, self.td_note = None, None   # teardown lines dropped from the witness, and why none were (wit())

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
            out = []
            for i, l in enumerate(rd(path)):
                if not l.strip():
                    continue
                try:
                    out.append(parse_w(l))
                except ValueError as e:
                    raise Missing("malformed witness %s line %d: %s" % (path, i + 1, e))
            try:
                c3 = self.log().rng.get("cp3")
            except Missing:
                c3 = None
            out, self.td, self.td_note = teardown_split(out, c3[0] if c3 else None)
            if not out:
                raise Missing("empty witness %s" % path)
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
def flag_errors(run):
    """F0: the engine's own command line carries exactly the registered switches; the engine's det_synth line."""
    s, lg, err = run.spec, run.log(), []
    if lg.cmd is None:
        return ["no 'command line' line in %s" % run.p("xray.log")]
    toks = lg.cmd

    def has(seq):
        return any(toks[i:i + len(seq)] == seq for i in range(len(toks)))
    if not any(t.endswith(s["exe"]) for t in toks):
        err.append("exe %s not on the command line" % s["exe"])
    want = [[x] for x in BASE if not x[0].isdigit()] + [["-det_tag", run.rtag], ["-det_passes", "100,200,320"]]
    lever = set()
    if s["k"]:
        want += [["-det_synth_k", str(s["k"])], ["-sim_4d_mask", str(s["mask"])]]
        lever |= {"-det_synth_k", "-sim_4d_mask"}
    if s["wc"]:
        want.append(["-det_synth_wallclock"]); lever.add("-det_synth_wallclock")
    if s["tw"]:
        want.append(["-det_synth_tw"]); lever.add("-det_synth_tw")
    if s["brk"]:
        want.append(["-sim_4d_break", str(s["brk"])]); lever.add("-sim_4d_break")
    if s["stim"]:   # prereg Amendment 1: -det_synth_item with the stimulus, and only there (else "unregistered")
        want += [["-det_synth_walk"], ["-det_synth_tracer"], ["-det_synth_item"]]
        lever |= {"-det_synth_walk", "-det_synth_tracer", "-det_synth_item"}
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
    return err


def pattern_errors(run, ad=True):
    """L1 rules on one lever run: the fixed pattern, contiguous frames, and the checkpoints anchored in the witness."""
    K, recs, err = run.spec["k"], run.wit(), []

    def e(msg):
        if len(err) < 6:
            err.append(msg)
        else:
            err[5] = "... more"
    if run.td_note:
        e(run.td_note)
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


def tick_gaps(run):
    """A-Life calls up to cp1 in ticks (via the witness F -> T map): (calls, gaps, first tick, errors)."""
    byF = {r.F: r for r in run.wit()}
    f0 = run.wit()[0].F
    ticks, err = [], []
    for f in run.sched():
        if f < f0:
            continue
        r = byF.get(f)
        if r is None or r.n != 1:
            err.append("A-Life call on frame %d is not a witnessed tick frame" % f)
            continue
        ticks.append(r.T)
    g = [b - a for a, b in zip(ticks, ticks[1:])]
    return ticks, g, err


def gaps_ok(ticks, g, n16):
    return len(ticks) >= ALIFE_MIN_CALLS and all(x in (16, 17, 18) for x in g) and g.count(16) == n16


def tick_frames(recs):
    return {r.T: r for r in recs if r.n == 1}


def drawn_moving(r):
    return r.s2d is not None and cheb(r.s2d, r.s2x) > TOL_POS


def item_txt(lg):
    return "no 'det_synth stimulus: item' line" if lg.item is None else "item " + (
        "NONE (no filled slot)" if lg.item == "NONE" else lg.item)


def item_share(run):
    """prereg Amendment 1: (share, lines with s4, lines, first F, cp3 F) over the witness lines from the stimulus start
    frame to the cp3 frame inclusive. Start frame = the first witness line whose hud >= the start line's `time` (the
    script's time_global at update START_UPD; under the lever hud = dwTimeGlobal of the newest tick + a x 33, NOTES.md section 2 item 14;
    real probes: time 9966 = hud of tick frame F 815, the first frame whose xform moves)."""
    lg = run.log()
    if lg.t0 is None:
        raise Missing("no 'det_synth stimulus: start at update ... time <ms>' line in %s" % run.p("xray.log"))
    f3 = run.cp("cp3")[0]
    recs = run.wit()
    st = next((q for q in recs if q.hud >= lg.t0 - TOL_HUD), None)
    if st is None:
        raise Missing("no witness line with hud >= the stimulus start time %d" % lg.t0)
    win = [q for q in recs if st.F <= q.F <= f3]
    if not win:
        raise Missing("no witness line from the stimulus start F %d to cp3 F %d" % (st.F, f3))
    n4 = sum(1 for q in win if q.s4 is not None)
    return n4 / float(len(win)), n4, len(win), st.F, f3


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


# ---------------------------------------------------------------- scoring
def score(root):
    V, REP = [], []          # (id, verdict, text); reported lines
    runs = {t: Run(root, t, s) for t, s in RUNS.items()}
    n = lambda k: int(CONST[k])
    S1_REG = n("PROBE_S1_FRAMES") >= DROP_MIN
    S3_REG = n("PROBE_S3_FRAMES") >= DROP_MIN
    B2_REG = S3_REG
    LAG = n("PROBE_S2_LAG")
    mins = lambda key, K, per_tick=True: int(math.floor(n(key) * ((K - 1) if per_tick else K)
                                                       / float((KP - 1) if per_tick else KP) / 3.0))
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

    def drop(cid, why):
        V.append((cid, "DROPPED", "Caden's eyes only (%s)" % why))

    # F0 flags
    def f0():
        bad = []
        for t, r in runs.items():
            if t == "s4d_brk2" and not B2_REG:
                continue
            try:
                e = flag_errors(r)
            except Missing as m:
                e = [str(m)]
            bad += ["%s: %s" % (t, x) for x in e]
        return not bad, "; ".join(bad[:8]) or "all %d runs carry exactly their registered switches" % len(runs)
    check("F0", f0)

    # ---- part A
    st = [runs[t] for t in ("s4d_s1", "s4d_s2", "s4d_s3")]
    of = [runs[t] for t in ("s4d_o1", "s4d_o2", "s4d_o3")]

    def a1():
        c = {r.tag: C3(st[0], r) for r in st[1:]}
        return all(v == 0 for x in c.values() for v in x), "C at cp1,2,3 against s4d_s1: %s" % c
    check("A1", a1)

    def a2():
        c = {r.tag: C3(st[0], r) for r in of}
        return all(v == 0 for x in c.values() for v in x), "C at cp1,2,3 against s4d_s1: %s" % c
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
        first = fr["s4d_s1"][0] if fr["s4d_s1"] else None
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
            if lg.syn:
                bad.append("%s: engine det_synth line present" % r.tag)
            if os.path.exists(r.p("synth_{t}.txt")):
                bad.append("%s: witness file present" % r.tag)
            for cp in ("cp1", "cp2", "cp3"):
                if r.cp(cp)[1] is not None:
                    bad.append("%s: %s has tick=" % (r.tag, cp))
        return not bad, "; ".join(bad) or "no lever line, no witness, no tick= suffix in the three off runs"
    check("A5", a5)

    # ---- part L
    def l1():
        bad = []
        for t in ("s4d_l2a", "s4d_l2b", "s4d_l3", "s4d_l1"):
            bad += ["%s: %s" % (t, x) for x in pattern_errors(runs[t])]
        return not bad, "; ".join(bad[:8]) or "pattern holds on every line of l2a, l2b, l3, l1; cp1-3 anchored"
    check("L1", l1)

    def l1w():
        w, a = runs["s4d_l2w"], runs["s4d_l2a"]
        bad = ["pattern: %s" % x for x in pattern_errors(w, ad=False)]
        recs = w.wit()
        de = dt_exp(2)
        fa = sum(1 for r in recs if abs(r.a - r.j / 2.0) > TOL_A) / float(len(recs))
        fd = sum(1 for r in recs if abs(r.dt - de) > TOL_A) / float(len(recs))
        if fa < WALLCLOCK_DIFF_MIN:
            bad.append("a differs from j/K on %.3f of lines (must-fail needs >= %.2f)" % (fa, WALLCLOCK_DIFF_MIN))
        if fd < WALLCLOCK_DIFF_MIN:
            bad.append("dt differs from 0.033/K on %.3f of lines (must-fail needs >= %.2f)" % (fd, WALLCLOCK_DIFF_MIN))
        fmax = min(w.cp("cp3")[0], a.cp("cp3")[0])
        pw = [(r.F, r.T, r.j, r.K, r.n) for r in recs if r.F <= fmax]
        pa = [(r.F, r.T, r.j, r.K, r.n) for r in a.wit() if r.F <= fmax]
        if pw != pa:
            bad.append("pattern columns F T j K n differ from l2a's (%d vs %d lines to frame %d)" % (len(pw), len(pa), fmax))
        return not bad, "; ".join(bad[:6]) or "MUST-FAIL as predicted: pattern as l2a's, a differs on %.4f, dt on %.4f of lines" % (fa, fd)
    check("L1W", l1w)

    def l2():
        a, b = runs["s4d_l2a"], runs["s4d_l2b"]
        fmax = min(a.cp("cp3")[0], b.cp("cp3")[0])
        la = [r.lever for r in a.wit() if r.F <= fmax]
        lb = [r.lever for r in b.wit() if r.F <= fmax]
        if la == lb and la and a.wit()[-1].F >= fmax and b.wit()[-1].F >= fmax:
            return True, "%d lines identical in F..hud to frame %d" % (len(la), fmax)
        i = next((i for i, (x, y) in enumerate(zip(la, lb)) if x != y), min(len(la), len(lb)))
        return False, "lines %d vs %d; first difference at line %d: [%s] vs [%s]" % (
            len(la), len(lb), i + 1, la[i] if i < len(la) else "-", lb[i] if i < len(lb) else "-")
    check("L2", l2)

    def l3():
        a, b = runs["s4d_l2a"], runs["s4d_l2b"]
        bad = []
        c = C3(a, b)
        if any(c):
            bad.append("C l2a vs l2b %s" % c)
        dt = a.cp_tick("cp1") - b.cp_tick("cp1")
        if abs(dt) > CP1_DIFF:
            bad.append("cp1 tick difference %d" % dt)
        firsts = {}
        for t in ("s4d_l2a", "s4d_l2b", "s4d_l3", "s4d_l1"):
            ticks, g, e = tick_gaps(runs[t])
            bad += ["%s: %s" % (t, x) for x in e[:2]]
            if not gaps_ok(ticks, g, n("PROBE_ALIFE_GAP16")):
                bad.append("%s: calls %d gaps %s sixteens %d (registered %d)" % (t, len(ticks), sorted(set(g)), g.count(16), n("PROBE_ALIFE_GAP16")))
            firsts[t] = ticks[0] if ticks else None
        if len(set(firsts.values())) != 1:
            bad.append("first A-Life call tick differs %s" % firsts)
        return not bad, "; ".join(bad[:6]) or "C l2a vs l2b %s; cp1 tick difference %d; A-Life gaps in ticks as registered" % (c, dt)
    check("L3", l3)

    for lab, x, y in (("L4 K=1 vs stock (reported)", "s4d_l1", "s4d_s1"), ("L5 K=2 vs K=3 (reported)", "s4d_l2a", "s4d_l3"),
                      ("wallclock vs l2a (reported)", "s4d_l2w", "s4d_l2a")):
        try:
            REP.append("%s: C at cp1,2,3 %s" % (lab, C3(runs[x], runs[y])))
        except Missing as m:
            REP.append("%s: %s" % (lab, m))

    # ---- part S (all NO VERDICT if L3 does not pass)
    def s0():
        a, b = runs["s4d_off2a"], runs["s4d_off2b"]
        c = C3(a, b)
        dt = a.cp_tick("cp1") - b.cp_tick("cp1")
        return not any(c) and abs(dt) <= CP1_DIFF, "C off2a vs off2b %s; cp1 tick difference %d" % (c, dt)
    check("S0", s0, ("L3",))

    def s0b():
        bad = []
        for t, r in runs.items():
            if r.spec["part"] != "S" or (t == "s4d_brk2" and not B2_REG):
                continue
            lg = r.log()
            if not (lg.installed and lg.started):
                bad.append("%s: stimulus installed %s start %s" % (t, lg.installed, lg.started))
            if lg.item is None or lg.item == "NONE":   # prereg Amendment 1: the item must be taken
                bad.append("%s: %s" % (t, item_txt(lg)))
            if lg.fired < FIRED_MIN:
                bad.append("%s: highest 'bullets fired' %d < %d" % (t, lg.fired, FIRED_MIN))
            if lg.luaerr:
                bad.append("%s: script error line %s" % (t, lg.luaerr[0]))
            xs = [r2.s2x for r2 in r.wit() if r2.s2x is not None]
            rng = max(max(v[i] for v in xs) - min(v[i] for v in xs) for i in (0, 2)) if xs else 0.0
            if rng < WALK_MIN_RANGE:
                bad.append("%s: actor x/z range %.3f m < %.1f" % (t, rng, WALK_MIN_RANGE))
        return not bad, "; ".join(bad[:6]) or "stimulus installed, started, item taken, >= %d bullets, no script error, walk >= %.1f m in every part-S run" % (FIRED_MIN, WALK_MIN_RANGE)
    check("S0B", s0b, ("L3",))

    def sc():
        res, bad = {}, []
        for on, off in (("s4d_on2a", "s4d_off2a"), ("s4d_on2b", "s4d_off2a"), ("s4d_on3", "s4d_off3")):
            c = C3(runs[on], runs[off])
            dt = runs[on].cp_tick("cp1") - runs[off].cp_tick("cp1")
            res["%s/%s" % (on[4:], off[4:])] = (c, dt)
            if any(c) or abs(dt) > CP1_DIFF:
                bad.append("%s vs %s: C %s cp1 tick diff %d" % (on, off, c, dt))
        return not bad, "; ".join(bad) or "C = 0 and cp1 within %d ticks: %s" % (CP1_DIFF, res)
    check("SC", sc, ("L3", "S0"))

    def tw():
        bad = []
        for t, r in runs.items():
            if r.spec["part"] != "S" or (t == "s4d_brk2" and not B2_REG):
                continue
            if not r.spec["brk"] and r.log().tw:
                bad.append("%s: %d BAD lines (first %s)" % (t, len(r.log().tw), r.log().tw[0]))
            bad += ["%s: %s" % (t, x) for x in tw_consistency(r)]
        return not bad, "; ".join(bad[:6]) or "0 BAD lines in every non-break run; log and witness tw column agree in all part-S runs"
    check("TW", tw, ("L3",))

    def twb(tag, want, other, mn):
        def f():
            r = runs[tag]
            frames = {fr for s, fr in r.log().tw if s == want}
            oth = [x for x in r.log().tw if x[0] == other]
            ok = len(frames) >= mn and not oth
            return ok, "%s: %s BAD on %d frames (need >= %d), %s BAD lines %d (need 0)%s" % (
                tag, want, len(frames), mn, other, len(oth), " - MUST-FAIL as predicted" if ok else "")
        return f
    check("TWB1", twb("s4d_brk1", "s2", "s3", mins("PROBE_S2_MOVING", 2)), ("L3",))
    def twb2():
        # bound from the break run's OWN witness (no assumption about how long pulled-back bullets live): the witness
        # takes head and bullet_pos BEFORE the break writes (Level_Bullet_Manager.cpp Render), so on every frame whose
        # first drawn tracer has head != pos the break changes a hashed field and an s3 BAD line must be on that frame
        r = runs["s4d_brk2"]
        need = {q.F for q in r.wit() if q.s3n and cheb(q.s3h, q.s3p) > TOL_POS and q.F <= r.cp("cp3")[0]}
        bad3 = {fr for s, fr in r.log().tw if s == "s3"}
        miss = sorted(need - bad3)
        oth = [x for x in r.log().tw if x[0] == "s2"]
        ok = len(need) >= TWB2_MIN_N and not miss and not oth
        return ok, "s4d_brk2: %d frames with a drawn tracer off bullet_pos in its own witness (need >= %d), %d of them "\
            "without an s3 BAD line (need 0; first %s), s3 BAD on %d frames, s2 BAD lines %d (need 0)%s" % (
                len(need), TWB2_MIN_N, len(miss), miss[:3], len(bad3), len(oth), " - MUST-FAIL as predicted" if ok else "")
    if B2_REG:
        check("TWB2", twb2, ("L3",))
    else:
        drop("TWB2", "probe: %d tracer frames < %d, s4d_brk2 not booted" % (n("PROBE_S3_FRAMES"), DROP_MIN))

    def s2():
        bad, info = [], []
        for t in S_BODY:
            r = runs[t]
            K, recs = r.spec["k"], r.wit()
            X = {T: q.s2x for T, q in tick_frames(recs).items() if q.s2x is not None}
            mov = [q for q in recs if q.n == 0 and drawn_moving(q)]
            mn = mins("PROBE_S2_MOVING", K)
            if len(mov) < mn:
                bad.append("%s: %d moving n=0 frames < %d" % (t, len(mov), mn))
            nl = ncam = 0
            for q in mov:
                if cheb(q.s2d, q.s2c) > TOL_POS:
                    ncam += 1
                if LAG >= 0 and (q.T - LAG - 1) in X and (q.T - LAG) in X:
                    p0, p1 = X[q.T - LAG - 1], X[q.T - LAG]
                    if cheb(q.s2d, lerp(p0, p1, q.a)) > tol_lerp(p0, p1, q.s2d):
                        nl += 1
            if ncam:
                bad.append("%s: drawn != camera on %d frames" % (t, ncam))
            if LAG >= 0 and nl:
                bad.append("%s: drawn off the lerp of ticks T-%d-1, T-%d on %d frames" % (t, LAG, LAG, nl))
            info.append("%s %d" % (t[4:], len(mov)))
        for t in S_OFF:
            m = sum(1 for q in runs[t].wit() if drawn_moving(q))
            if m:
                bad.append("%s (body off): drawn != xform on %d frames" % (t, m))
        txt = "moving n=0 frames %s; lerp rule %s" % (", ".join(info), "lag %d" % LAG if LAG >= 0 else "reported only (probe)")
        return not bad, "; ".join(bad[:6]) or txt
    check("S2", s2, ("L3",))

    def s3():
        bad, info = [], []
        for t in S_BODY:
            r = runs[t]
            K, nn, nlerp, nstamp0 = r.spec["k"], 0, 0, 0
            for q in r.wit():
                if not q.s3n:
                    continue
                if q.s3ok == 1:
                    if cheb(q.s3h, lerp(q.s3s, q.s3p, q.a)) > tol_lerp(q.s3s, q.s3p, q.s3h):
                        nlerp += 1
                else:
                    nstamp0 += 1
                    if cheb(q.s3h, q.s3p) > TOL_POS:
                        nlerp += 1
                if q.n == 0 and cheb(q.s3h, q.s3p) > TOL_POS:
                    nn += 1
            mn = mins("PROBE_S3_FRAMES", K)
            if nn < mn:
                bad.append("%s: %d n=0 frames with head != pos < %d" % (t, nn, mn))
            if nlerp:
                bad.append("%s: head off its rule on %d frames" % (t, nlerp))
            info.append("%s %d (stamp 0: %d)" % (t[4:], nn, nstamp0))
        for t in S_OFF:
            m = sum(1 for q in runs[t].wit() if q.s3n and cheb(q.s3h, q.s3p) > TOL_POS)
            if m:
                bad.append("%s (tracers off): head != pos on %d frames" % (t, m))
        return not bad, "; ".join(bad[:6]) or "n=0 frames drawn between: %s" % ", ".join(info)
    if S3_REG:
        check("S3", s3, ("L3",))
    else:
        drop("S3", "probe: %d tracer frames < %d" % (n("PROBE_S3_FRAMES"), DROP_MIN))

    def s1():
        bad, info = [], []
        for t in S_BODY + S_OFF:
            r = runs[t]
            K, on = r.spec["k"], bool(r.spec["mask"] & 4)
            fr = [q for q in r.wit() if q.s1 is not None]
            mn = mins("PROBE_S1_FRAMES", K, per_tick=False)
            if len(fr) < mn:
                bad.append("%s: %d crosshair frames < %d" % (t, len(fr), mn))
            if on:
                m = sum(1 for q in fr if abs(q.s1[0] - dt_exp(K)) > TOL_A)
            else:
                m = sum(1 for q in fr if abs(q.s1[0] - (f32(0.033) if q.n == 1 else 0.0)) > TOL_A)
            if m:
                bad.append("%s: dt_used off its rule (%s) on %d frames" % (t, "0.033/K" if on else "fTimeDelta", m))
            info.append("%s %d" % (t[4:], len(fr)))
        return not bad, "; ".join(bad[:6]) or "crosshair frames %s" % ", ".join(info)
    if S1_REG:
        check("S1", s1, ("L3",))
    else:
        drop("S1", "probe: %d crosshair frames < %d" % (n("PROBE_S1_FRAMES"), DROP_MIN))

    # ---- reported
    for t, r in runs.items():   # prereg Amendment 1: the item each part-S run took
        if r.spec["part"] != "S" or (t == "s4d_brk2" and not B2_REG):
            continue
        try:
            REP.append("item taken %s: %s" % (t, item_txt(r.log())))
        except Missing as m:
            REP.append("item taken %s: %s" % (t, m))
    for t in S_BODY + S_OFF:
        try:
            REP.append("s4 %s: %s" % (t, s4_summary(runs[t].wit())))
        except Missing as m:
            REP.append("s4 %s: %s" % (t, m))
    for a, b in (("s4d_on2a", "s4d_on3"), ("s4d_off2a", "s4d_off3")):
        try:
            A, B = tick_frames(runs[a].wit()), tick_frames(runs[b].wit())
            f1 = min(runs[a].cp("cp1")[0], runs[b].cp("cp1")[0])
            Ta = {T for T, q in A.items() if q.s4 is not None and q.F <= runs[a].cp("cp1")[0]}
            com = sorted(Ta & {T for T, q in B.items() if q.s4 is not None and q.F <= runs[b].cp("cp1")[0]})
            eq = sum(1 for T in com if cheb(A[T].s4, B[T].s4) <= TOL_POS)
            mx = max([eucl(A[T].s4, B[T].s4) for T in com] or [0.0])
            REP.append("S4X cross-rate %s vs %s: tick frames to cp1 %d, s4 equal on %d (%.3f), max diff %.5f m" % (
                a, b, len(com), eq, eq / float(len(com)) if com else 0.0, mx))
        except Missing as m:
            REP.append("S4X %s vs %s: %s" % (a, b, m))
    for a, b in (("s4d_off2a", "s4d_off2b"), ("s4d_on2a", "s4d_off2a"), ("s4d_on3", "s4d_off3"), ("s4d_brk1", "s4d_off2a"),
                 ("s4d_brk2", "s4d_off2a")):
        try:
            od = [online_diff(runs[a], runs[b], k) for k in (1, 2, 3)]
            same = runs[a].log().rng_lines == runs[b].log().rng_lines
            REP.append("online objects differing %s vs %s at cp1,2,3: %s; generator/Lua state lines %s; C %s" % (
                a, b, od, "IDENTICAL" if same else "DIFFER", C3(runs[a], runs[b])))
        except Missing as m:
            REP.append("online diff %s vs %s: %s" % (a, b, m))
    REP.append(td_line(runs.values()))
    return V, REP


def td_line(runs):
    return "teardown witness lines dropped (trailing no-actor lines after cp3, at most %d): %s" % (TEARDOWN_MAX, ", ".join(
        "%s %s" % (r.tag, "witness not read" if r._wit is None else r.td) for r in runs if r.spec["k"]))


# ---------------------------------------------------------------- held item (s4): description only, never gated
# Registered cases (prereg 2.5; plan-facts/shadow-nudge-desk-read-2026-11-27.md section 6), K = 3, B = the item as
# rebuilt from the bones, d = the nudge (~0.5 m). Item step = |item(j) - item(j=0) of the same tick| for j = 1, 2:
# "0,0" none, "d,d" one nudge on both, "d,2d" one then two. Draw = |draw matrix - item - body offset| (draw_off):
# near / far / absent.
S4_CASES = [
    ("weapon, desk prediction (nudge wiped by the render's rebuild: B+d, B, B)", ("d,d", "near"), ("d,d", "near")),
    ("nudge adds up (the premise of c253fafdb: B+d, B+2d, B+3d / B+d x3)", ("d,2d", None), ("0,0", None)),
    ("bolt active, desk prediction (B+d x3 / B+d, B, B)", ("0,0", "far"), ("d,d", None)),
    ("no shadow pass reaches the item (B x3, never drawn)", ("0,0", "absent"), ("0,0", "absent")),
]


def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(p * (len(v) - 1) + 0.5))]


def dist_txt(v):
    if not v:
        return "n 0"
    return "n %d min %.5f median %.5f p90 %.5f max %.5f" % (len(v), min(v), pct(v, .5), pct(v, .9), max(v))


def draw_off(q):
    """|draw - (item + body draw offset)|: the child rule draws the item at actor_draw * inv(actor XFORM) * item, so the
    walk's own offset (s2 draw - s2 xform; no turning headless) is taken out and only an item-side offset is left"""
    if q.s2d is None:
        return eucl(q.s4d, q.s4)
    return eucl(q.s4d, tuple(i + d - x for i, d, x in zip(q.s4, q.s2d, q.s2x)))


def s4_by(recs):
    by = {}
    for q in recs:
        if q.s4 is not None:
            by.setdefault(q.T, {})[q.j] = q
    return by


def s4_summary(recs):
    """reported line per part-S run"""
    by, K = s4_by(recs), recs[0].K
    parts = []
    for j in range(1, K):
        d = [eucl(t[j].s4, t[0].s4) for t in by.values() if 0 in t and j in t]
        parts.append("j=%d |item-item(j=0)| median %s" % (j, "%.5f" % pct(d, .5) if d else "-"))
    items = [q for q in recs if q.s4 is not None]
    dr = [draw_off(q) for q in items if q.s4d is not None]
    return "item on %d of %d frames; %s; |draw-item-body offset| median %s; draw absent on %d item frames" % (
        len(items), len(recs), "; ".join(parts) or "K=1", "%.5f" % pct(dr, .5) if dr else "-", len(items) - len(dr))


def s4_signature(recs):
    sig = {}
    for t in s4_by(recs).values():
        if not all(j in t for j in (0, 1, 2)):
            continue
        d1, d2 = eucl(t[1].s4, t[0].s4), eucl(t[2].s4, t[0].s4)
        if d1 <= S4_THETA and d2 <= S4_THETA:
            g = "0,0"
        elif d1 > S4_THETA and d2 > S4_THETA and 0.75 <= d2 / d1 <= 1.25:
            g = "d,d"
        elif d1 > S4_THETA and d2 > S4_THETA and 1.5 <= d2 / d1 <= 2.5:
            g = "d,2d"
        else:
            g = "other"
        sig[g] = sig.get(g, 0) + 1
    tot = sum(sig.values())
    item = max(sig, key=lambda g: sig[g]) if tot else "no item"
    if tot and sig[item] < S4_SHARE * tot:
        item = "other"
    items = [q for q in recs if q.s4 is not None]
    dr = [draw_off(q) for q in items if q.s4d is not None]
    if not items:
        draw = "no item"
    elif len(items) - len(dr) >= S4_ABSENT_SHARE * len(items):
        draw = "absent"
    else:
        draw = "near" if pct(dr, .5) <= S4_DRAW_THETA else "far"
    return item, draw, sig


# ---------------------------------------------------------------- probe readings (mode=probe)
def probe_mode(root):
    out, stop = [], []

    def st(code, msg):
        stop.append("PROBE STOP %s: %s" % (code, msg))
    r7, r15 = Run(root, PROBE_TAG, PROBE_RUN), Run(root, PROBE15_TAG, PROBE15_RUN)
    for r in (r7, r15):            # P1, P2 on both probe boots
        try:
            for e in flag_errors(r):
                st("flags", "%s: %s" % (r.tag, e))
            for e in pattern_errors(r):
                st("pattern", "%s: %s" % (r.tag, e))
        except Missing as m:
            st("input", "%s: %s" % (r.tag, m))
    try:                           # P3-P7 and the placeholders: the mask-7 probe (the shipped mask)
        recs, lg = r7.wit(), r7.log()
        if not (lg.installed and lg.started) or lg.fired < FIRED_MIN or lg.luaerr:
            st("stimulus", "installed %s start %s fired %d script errors %s" % (lg.installed, lg.started, lg.fired, lg.luaerr[:1]))
        xs = [q.s2x for q in recs if q.s2x is not None]
        rng = max(max(v[i] for v in xs) - min(v[i] for v in xs) for i in (0, 2)) if xs else 0.0
        if rng < WALK_MIN_RANGE:
            st("walk", "actor x/z range %.3f m < %.1f" % (rng, WALK_MIN_RANGE))
        if lg.tw:
            st("tripwire", "%d BAD lines with no break (first %s)" % (len(lg.tw), lg.tw[0]))
        ticks, g, e = tick_gaps(r7)
        n16 = g.count(16)
        if e or len(ticks) < ALIFE_MIN_CALLS or any(x not in (16, 17, 18) for x in g) or n16 > 1:
            st("alife", "calls %d gaps %s sixteens %d %s" % (len(ticks), sorted(set(g)), n16, e[:1]))
        X = {T: q.s2x for T, q in tick_frames(recs).items() if q.s2x is not None}
        mov = [q for q in recs if q.n == 0 and drawn_moving(q)]
        lagfit = {}
        for L in (0, 1, 2):
            ok = tot = 0
            for q in mov:
                if (q.T - L - 1) in X and (q.T - L) in X:
                    tot += 1
                    p0, p1 = X[q.T - L - 1], X[q.T - L]
                    ok += cheb(q.s2d, lerp(p0, p1, q.a)) <= tol_lerp(p0, p1, q.s2d)
            lagfit[L] = ok / float(tot) if tot else 0.0
        bestL = max(lagfit, key=lambda L: lagfit[L])
        lag = bestL if lagfit[bestL] >= LAG_SHARE else -1
        if len(mov) < DROP_MIN:
            st("body", "%d moving n=0 frames < %d: the body is not drawn between ticks" % (len(mov), DROP_MIN))
        s3n0 = sum(1 for q in recs if q.n == 0 and q.s3n and cheb(q.s3h, q.s3p) > TOL_POS)
        s3n1 = sum(1 for q in recs if q.n == 1 and q.s3n and cheb(q.s3h, q.s3p) > TOL_POS)
        tt = sorted({q.T for q in recs if q.s3n})
        runs_, cur = [], 0
        for i, T in enumerate(tt):
            cur = cur + 1 if i and T == tt[i - 1] + 1 else 1
            if i == len(tt) - 1 or tt[i + 1] != T + 1:
                runs_.append(cur)
        life = sum(runs_) / float(len(runs_)) if runs_ else 0.0
        s0 = sum(1 for q in recs if q.s3n and q.s3ok == 0)
        s1f = sum(1 for q in recs if q.s1 is not None)
        vals = {"PROBE_S2_MOVING": len(mov), "PROBE_S2_LAG": lag, "PROBE_S3_FRAMES": s3n0, "PROBE_S1_FRAMES": s1f,
                "PROBE_ALIFE_GAP16": n16}
        out.append("probe readings, s4d_probe (rules: prereg section 2.3):")
        out.append("  lag fit (share of moving n=0 frames on the lerp of ticks T-L-1, T-L): %s" % (
            ", ".join("L=%d %.4f" % kv for kv in sorted(lagfit.items()))))
        out.append("  reported: tracer tick frames %d, tracer life %.2f ticks, drawn frames with stamp_ok 0: %d" % (s3n1, life, s0))
        out.append("  highest 'bullets fired': %d; actor x/z range %.3f m; witness lines %d; A-Life calls %d" % (
            lg.fired, rng, len(recs), len(ticks)))
        out.append("  S1 %s (%d frames); S3 and TWB2 %s (%d frames)" % (
            "registered" if s1f >= DROP_MIN else "DROPPED: Caden's eyes only", s1f,
            "registered" if s3n0 >= DROP_MIN else "DROPPED: Caden's eyes only", s3n0))
        out.append("CONST values to register:")
        for k_, v in vals.items():
            out.append('    "%s": %r,' % (k_, v))
    except Missing as m:
        st("input", "%s: %s" % (r7.tag, m))
    for r in (r7, r15):            # prereg Amendment 1, rule "item", both probe boots: (i) item taken, (ii) s4 share
        try:
            lg = r.log()
            if lg.item is None or lg.item == "NONE":
                st("item", "%s: %s (installed line: item %s)" % (r.tag, item_txt(lg), lg.inst_item))
            sh, n4, nw, fa, fb = item_share(r)
            out.append("item %s: %s (installed line: item %s); s4 on %d of %d witness lines F %d..%d (stimulus start to "
                       "cp3): share %.4f (rule >= %.2f)" % (r.tag, item_txt(lg), lg.inst_item, n4, nw, fa, fb, sh, ITEM_SHARE))
            if sh < ITEM_SHARE:
                st("item", "%s: s4 share %.4f (%d of %d lines F %d..%d) < %.2f" % (r.tag, sh, n4, nw, fa, fb, ITEM_SHARE))
        except Missing as m:
            st("item", "%s: %s" % (r.tag, m))
    sigs = {}
    for r in (r7, r15):            # descriptive
        try:
            recs = r.wit()
            by = s4_by(recs)
            out.append("s4 %s (mask %d), descriptive:" % (r.tag, r.spec["mask"]))
            for j in range(r.spec["k"]):
                items_j = [q for q in recs if q.j == j and q.s4 is not None]
                dr = [draw_off(q) for q in items_j if q.s4d is not None]
                if j:
                    out.append("  j=%d |item - item(j=0)|: %s" % (j, dist_txt([eucl(t[j].s4, t[0].s4) for t in by.values() if 0 in t and j in t])))
                out.append("  j=%d |draw - item - body offset|: %s; draw absent on %d of %d item frames" % (j, dist_txt(dr), len(items_j) - len(dr), len(items_j)))
            sig = s4_signature(recs)
            sigs[r.tag] = sig
            out.append("  signature: item step %s (ticks per signature %s), draw %s" % (sig[0], sig[2], sig[1]))
        except Missing as m:
            out.append("s4 %s: %s" % (r.tag, m))
    name = "none of the registered cases"
    if len(sigs) == 2:
        o, f = sigs[PROBE_TAG], sigs[PROBE15_TAG]
        for cname, a, b in S4_CASES:
            if o[0] == a[0] and (a[1] is None or o[1] == a[1]) and f[0] == b[0] and (b[1] is None or f[1] == b[1]):
                name = cname
                break
    out.append("S4 CASE (descriptive, never gated): %s" % name)
    out.append(td_line((r7, r15)))
    return out, stop


def main(argv):
    args = dict(a.split("=", 1) for a in argv if "=" in a)
    if len(args) != len(argv) or "out" not in args or "root" not in args or set(args) - {"out", "root", "mode"} \
            or args.get("mode", "score") not in ("score", "probe"):
        print("usage: s4d1_score.py root=<dir> out=<file> | mode=probe root=<dir> out=<file>")
        print("S4D1 RESULT: REFUSED (usage)")
        return 2
    outp = args["out"]
    if os.path.exists(outp):
        print("S4D1 RESULT: REFUSED (out file %s exists; never overwritten)" % outp)
        return 2
    if args.get("mode") == "probe":
        if not os.path.isdir(args["root"]):
            print("S4D1 PROBE: REFUSED (root %s is not a folder)" % args["root"])
            return 2
        out, stop = probe_mode(args["root"])
        txt = "\n".join(out + stop + ["S4D1 PROBE: " + ("STOP (%d rules)" % len(stop) if stop else "GO")])
        with open(outp, "w") as f:
            f.write(txt + "\n")
        print(txt)
        return 1 if stop else 0
    ph = [k for k, v in CONST.items() if isinstance(v, str) and v.startswith("PROBE_")]
    if ph:
        print("S4D1 RESULT: REFUSED (placeholders not filled: %s)" % ", ".join(ph))
        return 2
    if not os.path.isdir(args["root"]):
        print("S4D1 RESULT: REFUSED (root %s is not a folder)" % args["root"])
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
    gated = [v for v in V if v[1] != "DROPPED"]
    lines = ["%s %s: %s" % v for v in V] + ["reported: " + x for x in REP]
    lines.append("S4D1 RESULT: %s (%d of %d gated checks PASS; FAIL %s; harness FAIL %s; NO VERDICT %s; dropped %s)" % (
        res, sum(1 for v in gated if v[1] == "PASS"), len(gated), [v[0] for v in fails] or "-",
        [v[0] for v in hfail] or "-", [v[0] for v in nv] or "-", [v[0] for v in V if v[1] == "DROPPED"] or "-"))
    txt = "\n".join(lines)
    with open(outp, "w") as f:
        f.write(txt + "\n")
    print(txt)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
