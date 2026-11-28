#!/usr/bin/env bash
# s4d2 self-test (PREREG-s4d2-npc-pose-DRAFT.md section 8): the scorer s4d2_score.py on SYNTHETIC run folders.
#   nice -n 19 bash s4d2_selftest.sh [<parent folder>]     (default parent: s4d2/selftest-runs) -> <parent>/<stamp>-<pid>/
# LIMIT: no real s4d2 run folder exists (the exe is not built). Every fixture is made here from the witness format in
# the code (wt-simtick c216081fd + the uncommitted W1/W2/F1 edits: device.cpp sim_synth_witness_write, sim_pose.cpp
# sim_pose_witness_print, sim_pose_print_pa, the W1 "uu" and W2 "pj" columns, GameObject.cpp sim_attach_wit_fill with
# the pa form I), and the log
# line formats of earlier real runs (out/det_s4c_s1). It MUST be re-run with a case built on the probe's real bytes
# before registration (prereg 8).
# What it does: copies the scorer into the stamp folder with the PROBE_ constants filled for the synthetic world
# (and ALIFE_MIN_CALLS 50 -> 3, DROP_MIN 300 -> 50, because the synthetic runs are 90-100 ticks long, not ~5440; the
# rules themselves are unchanged), builds one good set of 16 run folders, then one case per check and per must-fail
# (each case must change exactly the verdicts listed for it), CRLF and absent-dash variants, a missing and a malformed
# witness, the heap-layout fixtures (counters differ, every tripwire clean), the retry folder, the joint-rule switch,
# the refusals of the registered scorer, and the probe-mode cases. Unchanged run folders of a case are symlinks to the
# good set, unchanged witness files hard links. Nothing outside the stamp folder is written; nothing is deleted.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SC="$HERE/s4d2_score.py"
[ -s "$SC" ] || { echo "S4D2 SELFTEST: FAIL (0/0) scorer $SC missing"; exit 2; }
PARENT="${1:-$HERE/selftest-runs}"; mkdir -p "$PARENT" && PARENT="$(cd "$PARENT" && pwd)" || { echo "S4D2 SELFTEST: FAIL (0/0) parent folder"; exit 2; }
ST="$PARENT/$(date +%Y%m%d-%H%M%S)-$$"
[ -e "$ST" ] && { echo "S4D2 SELFTEST: FAIL (0/0) $ST exists"; exit 2; }
mkdir -p "$ST"
python3 -I - "$ST" "$SC" <<'PYEOF' 2>&1 | tee "$ST/selftest_output.txt"
import hashlib, importlib.util, os, re, subprocess, sys
ST, SC = sys.argv[1], sys.argv[2]
NPC = "sim_default_duty_428175"
FILL = {"PROBE_P_LERP": 300, "PROBE_P_TICK": 150, "PROBE_UPLOADS": 600, "PROBE_PA_CR": 300, "PROBE_PA_CLUSTERS": 1,
        "PROBE_JOINT_LERP": 1, "PROBE_U_POSED": 300, "PROBE_NPC": NPC, "PROBE_TOL_LD": 0.001}
src = open(SC).read()

def filled(over):
    s = src
    for k, v in dict(FILL, **over).items():
        old = '"%s": "%s",' % (k, k)
        assert s.count(old) == 1, old
        s = s.replace(old, '"%s": %r,' % (k, v))
    for a, b in (("ALIFE_MIN_CALLS = 50 ", "ALIFE_MIN_CALLS = 3 "), ("DROP_MIN = 300 ", "DROP_MIN = 50 ")):
        assert s.count(a) == 1, a
        s = s.replace(a, b)
    return s

SCF = os.path.join(ST, "scorer_filled.py"); open(SCF, "w").write(filled({}))
SCJ = os.path.join(ST, "scorer_filled_jointfallback.py"); open(SCJ, "w").write(filled({"PROBE_JOINT_LERP": 0}))
SCT = os.path.join(ST, "scorer_filled_tolld0.py"); open(SCT, "w").write(filled({"PROBE_TOL_LD": 0.0}))
spec_ = importlib.util.spec_from_file_location("s4d2f", SCF); S = importlib.util.module_from_spec(spec_)
spec_.loader.exec_module(S)

F0, H0, TMAX, CPT = 40, 1000.0, 90, {"cp1": 45, "cp2": 65, "cp3": 85}
CALLS = [3, 19, 36, 53]          # A-Life call ticks (part A reads them as frames): gaps 16,17,17 (one 16)
OFS = (0.1, -0.05, 0.2)          # the item's offset in its placement frame (ld = ls)
PMOVE = (0.02, 0.0, 0.0)         # form I: the parent's move since the placement (wd - ws)
def is_I(T, n):                  # on runs: form I (c216081fd S1 path) on the render-only frames of every tenth tick
    return n == 0 and T % 10 == 3

def H(*a):
    return hashlib.blake2b(repr(a).encode(), digest_size=8).hexdigest()

def r5(v):
    return tuple(round(x, 5) for x in v)

def add(a, b):
    return r5(tuple(x + y for x, y in zip(a, b)))

def tri(u):
    return 4 * u if u < 0.25 else (2 - 4 * u if u < 0.75 else 4 * u - 4)

def X(T, stim):
    if not stim or T < 10:
        return (10.0, 2.0, 5.0)
    return (round(10.0 + 1.5 * tri((((T - 10) * 33) % 2000) / 2000.0), 5), 2.0, 5.0)

def lerp(p, q, a):
    return r5(tuple(x + (y - x) * a for x, y in zip(p, q)))

def hand(T):
    return r5((200.0 + 0.01 * T, 1.2, 90.0 + 0.005 * (T % 7)))

def J(T):
    return r5((0.3 + 0.002 * T, 1.1, 0.1 + 0.001 * (T % 5)))

def pose_cols(r, spec, T, j, n, a):
    if spec["mask"] & 16:
        if T >= 2:
            u = a
            hp, hc = H("pose", T - 1), H("pose", T)
            hd = hp if u == 0 else H("draw", T, j, round(u, 6))
            r["pw"] = dict(form="L", u=u, pt=T - 1, ct=T, hd=hd, hl=hc, hp=hp, hc=hc, hu=H("up", hd), hdu=H("up", hd),
                           jd=lerp(J(T - 1), J(T), u), jp=J(T - 1), jc=J(T))
            r["p"] = [3, 2, 0, 0, 0, 1 if n == 0 else 0]
            r["pn"] = ["L", u, H("pn", T, j), H("pnl", T), H("pns", T)]
            if is_I(T, n):
                h = hand(T)
                r["pa"] = dict(kind="W", form="I", cls="c", ba=12, bb=30, u=u, wd=add(h, OFS), ws=add(add(h, OFS), PMOVE),
                               hd=h, hr=h, ld=OFS, ls=OFS, hx=H("x", T, j), hxe=H("x", T, j))
            elif n == 0:
                hd_, hr = lerp(hand(T - 1), hand(T), u), hand(T)
                r["pa"] = dict(kind="W", form="C", cls="c", ba=12, bb=30, u=u, wd=add(hd_, OFS), ws=add(hr, OFS),
                               hd=hd_, hr=hr, ld=OFS, ls=OFS, hx=H("x", T, j), hxe=H("x", T, j))
            else:
                h = hand(T - 1)
                r["pa"] = dict(kind="W", form="P", cls="p", ba=12, bb=30, u=u, wd=add(h, OFS), ws=add(h, OFS),
                               hd=h, hr=h, ld=None, ls=None, hx=H("x", T, j), hxe=H("x", T, j))
            r["pj"] = [12, r["pa"]["hd"]]   # W2: the mesh's drawn hand = the item's drawn bone
            r["up"] = [r["pw"]["hu"], hc]
        else:
            r["pw"] = dict(form="Sn", u=None, pt=1, ct=1, hd=None, hl=H("pose", 1), hp=H("pose", 1), hc=H("pose", 1),
                           hu=H("live", T, j), hdu=None, jd=None, jp=None, jc=None)
            r["p"] = [1, 0, 0, 0, 0, 0]
            r["pn"] = ["Sn", None, None, H("pnl", T), H("pns", T)]
            r["pa"] = dict(kind="-", form="S", cls="-", ba=None, bb=None, u=None, wd=add(hand(T), OFS),
                           ws=add(hand(T), OFS), hd=None, hr=None, ld=None, ls=None, hx=H("x", T, j), hxe=H("x", T, j))
            r["up"] = [H("live", T, j), H("live", T, j)]
            r["pj"] = None
    else:
        r["p"] = [0, 0, 0, 0, 0, 0]
        r["pw"] = None
        r["up"] = [H("live", T, j), H("live", T, j)]
        r["pn"] = ["X", None, None, H("pnl", T), H("pns", T)]
        r["pa"] = dict(kind="-", form="P", cls="-", ba=None, bb=None, u=None, wd=add(hand(T), OFS), ws=add(hand(T), OFS),
                       hd=None, hr=None, ld=None, ls=None, hx=H("x", T, j), hxe=H("x", T, j))
        r["pj"] = None
    r["uu"] = [2, 0, 0, 1]

def records(spec, tmax):
    K, mask, stim, wc, brk = spec["k"], spec["mask"], spec["stim"], spec["wc"], spec["brk"]
    recs, bad = [], []
    for T in range(1, tmax + 1):
        for j in range(K):
            r = {"F": F0 + (T - 1) * K + j, "T": T, "j": j, "K": K, "n": 1 if j == 0 else 0}
            a = j / float(K) if not wc else round(0.3 + 0.01 * j + 0.001 * (T % 7), 6)
            r["a"], r["dt"] = a, (S.dt_exp(K) if not wc else 0.014321)
            r["hud"] = H0 + 33 * T + a * 33
            x = X(T, stim)
            cam = lerp(X(T - 2, stim), X(T - 1, stim), a) if T >= 3 else x
            r["s2"] = [cam if mask & 1 else x, x, cam]
            if stim and T >= 11:
                m = T % 8 + 1
                pos, start = (100.0 + 40 * m, 20.0, 50.0), (100.0 + 40 * (m - 1), 20.0, 50.0)
                r["s3"] = [1, lerp(start, pos, a) if mask & 2 else pos, pos, start, 1]
            else:
                r["s3"] = 0
            r["s1"] = [S.dt_exp(K) if mask & 4 else (S.f32(0.033) if r["n"] else 0.0), (0.1, 0.2, 0.3), 0.5]
            r["tw"] = 0
            if spec["pose"] and not spec["b1"]:
                pose_cols(r, spec, T, j, r["n"], a)
                if brk == 16 and r["pw"] and r["pw"]["form"] == "L" and r["pw"]["hd"] != r["pw"]["hl"]:
                    bad.append(("pose", r["F"])); r["tw"] += 1
                if brk == 32 and r["pa"]["form"] in ("C", "R", "I"):   # the break fires on every corrected placement
                    r["pa"]["hxe"] = H("broken", T, j)
                    bad.append(("attach", r["F"])); r["tw"] += 1
            recs.append(r)
    return recs, bad

def v3(v):
    return "%.5f %.5f %.5f" % tuple(v)

def opt(v, f):
    return "-" if v is None else (f % v if isinstance(f, str) else f(v))

def fmt_pose(r, no_uu, no_pj):
    s = " p %d %d %d %d %d %d |" % tuple(r["p"])
    w = r["pw"]
    if w is None:
        s += " pw" + " -" * 19 + " |"
    else:
        s += " pw %s %s %s %s %s %s %s %s %s %s %s %s %s |" % (
            w["form"], opt(w["u"], "%.6f"), opt(w["pt"], "%d"), opt(w["ct"], "%d"), opt(w["hd"], "%s"), w["hl"],
            opt(w["hp"], "%s"), opt(w["hc"], "%s"), opt(w["hu"], "%s"), opt(w["hdu"], "%s"),
            opt(w["jd"], v3) if w["jd"] else "- - -", opt(w["jp"], v3) if w["jp"] else "- - -",
            opt(w["jc"], v3) if w["jc"] else "- - -")
    s += " up %s %s |" % tuple(r["up"]) if r["up"] else " up - - |"
    if r["pn"] is None:
        s += " pn - - - - - |"
    else:
        q = r["pn"]; s += " pn %s %s %s %s %s |" % (q[0], opt(q[1], "%.6f"), opt(q[2], "%s"), q[3], q[4])
    a = r["pa"]
    if a is None:
        s += " pa" + " -" * 26 + " |"
    else:
        vv = lambda v: v3(v) if v else "- - -"
        s += " pa %s %s %s %s %s %s %s %s %s %s %s %s %s %s |" % (
            a["kind"], a["form"], a["cls"], opt(a["ba"], "%d"), opt(a["bb"], "%d"), opt(a["u"], "%.6f"), v3(a["wd"]),
            v3(a["ws"]), vv(a["hd"]), vv(a["hr"]), vv(a["ld"]), vv(a["ls"]), a["hx"], opt(a["hxe"], "%s"))
    if not no_uu:
        s += " uu %d %d %d %d |" % tuple(r["uu"])
        if not no_pj:
            s += " pj - - - - |" if r.get("pj") is None else " pj %d %s |" % (r["pj"][0], v3(r["pj"][1]))
    return s

def fmt(r, pose, no_uu=False, no_pj=False):
    s = "F %d T %d j %d K %d n %d a %.6f dt %.6f hud %.3f |" % (r["F"], r["T"], r["j"], r["K"], r["n"], r["a"], r["dt"], r["hud"])
    s += " s2 %s %s %s |" % tuple(v3(v) for v in r["s2"]) if r["s2"] else " s2" + " -" * 9 + " |"
    if r["s3"] is None:
        s += " s3" + " -" * 11 + " |"
    elif r["s3"] == 0:
        s += " s3 0" + " -" * 10 + " |"
    else:
        q = r["s3"]; s += " s3 %d %s %s %s %d |" % (q[0], v3(q[1]), v3(q[2]), v3(q[3]), q[4])
    s += " s1 %.6f %s %.6f |" % (r["s1"][0], v3(r["s1"][1]), r["s1"][2]) if r["s1"] else " s1" + " -" * 5 + " |"
    s += " s4 - - - - - - |"
    if pose:
        s += fmt_pose(r, no_uu, no_pj)
    return s + " tw %d" % r["tw"]

def write(path, lines, crlf):
    with open(path, "wb") as f:
        f.write(("\r\n" if crlf else "\n").join(lines).encode() + (b"\r\n" if crlf else b"\n"))

def make_run(root, tag, spec, rtag=None, tmax=TMAX, crlf_log=True, crlf_wit=False, rec_fn=None, link_wit=None,
             cmd_drop=None, counter_change=None, cp_shift=0, sched_extra=False, syn_line=False, no_stim=False,
             bad_extra=(), no_bad=False, no_witness=False, raw_fn=None, drop_bad=None, no_uu=False, no_pose_line=False,
             no_pj=False, item="slot 3 section wpn_ak74"):
    rtag = rtag or tag
    d = os.path.join(root, "det_" + rtag); os.makedirs(d)
    K, lever = spec["k"], spec["k"] > 0
    ext = "-det_passes 100,200,320 -det_lua_streams -det_counters"
    if lever:
        ext += " -det_synth_k %d -sim_4d_mask %d" % (K, spec["mask"])
        ext += " -det_synth_wallclock" * spec["wc"] + " -det_synth_tw" * spec["tw"]
        ext += (" -sim_4d_break %d" % spec["brk"]) if spec["brk"] else ""
        ext += " -det_synth_walk -det_synth_tracer" * spec["stim"]
        ext += (" -det_synth_pose -det_synth_pose_npc %s" % NPC) * spec["pose"]
    ext += " -det_synth_item"   # every s4d2 boot, part A included (prereg 2.1, 3, 4)
    cmd = "command line z:\\home\\caden\\projects\\stalkergameengine\\run\\bin\\%s -nointro -dbg -start server(bench1/single/alife/load) client(localhost) -det_mode -det_tag %s -det_interval 60 -det_checkpoints 3 %s" % (spec["exe"], rtag, ext)
    if cmd_drop:
        cmd = cmd.replace(" " + cmd_drop, "")
    L = ["* xray log (synthetic, s4d2 self-test)", cmd]
    cpf = lambda T: (F0 + (T - 1) * K) if lever else 32 + T
    suf = lambda T: (" tick=%d" % T) if lever else ""
    if lever or syn_line:
        L.append("* det_synth: k %d wallclock %d tw %d break %d mask %d" % (K, spec["wc"], spec["tw"], spec["brk"], spec["mask"] or 0))
    if lever and spec["pose"] and not spec["b1"] and not no_pose_line:
        L.append("* det_synth: pose witness on, npc %s" % NPC)
    if lever:
        L.append("* det_synth: K %d, witness z:\\run\\appdata\\logs\\synth_%s.txt" % (K, rtag))
    L.append("* det_rng_state pre: Random=1685459848 Random32=0 alife=302455169 crt_next=22912 frame=32 time_ms=99" + (" tick=0" if lever else ""))
    for cp, T in CPT.items():
        L.append("* det_rng_state %s: Random=-1528111566 Random32=0 alife=302455169 crt_next=6334 frame=%d time_ms=%d%s" % (cp, cpf(T) + cp_shift, 99 + 33 * T, suf(T)))
        L.append("* det_lua_state %s: 815687002" % cp)
    st = "true" if spec["stim"] else "false"
    L.append("* det_synth stimulus: installed (walk %s, tracer %s, item true)" % (st, st))
    if not (spec["stim"] and no_stim):   # no_stim: the walk/tracer part never started (installed and item lines kept)
        L.append("* det_synth stimulus: start at update 300 (10.000 2.000 5.000) time 9999 walk %s tracer %s ammo ammo_5.45x39_fmj" % (st, st))
    if item is not None:
        L.append("* det_synth stimulus: item " + ("NONE (no filled slot)" if item == "NONE" else item))
    if spec["stim"] and not no_stim:
        L += ["* det_synth stimulus: 1 bullets fired (update 300)", "* det_synth stimulus: 50 bullets fired (update 1770)",
              "* det_synth stimulus: 150 bullets fired (update 4770)"]
    if lever:
        recs, bad = records(spec, tmax)
        if no_bad:
            bad = []
            for r in recs:
                r["tw"] = 0
        bad = [b for b in bad if b != drop_bad] + list(bad_extra)
        if rec_fn:
            rec_fn(recs)
        L += ["* tw %s BAD %d" % b for b in bad]
        if not no_witness:
            p = os.path.join(d, "synth_%s.txt" % rtag)
            if link_wit:
                os.link(link_wit, p)
            else:
                lines = [fmt(r, spec["pose"] and not spec["b1"], no_uu, no_pj) for r in recs]
                if raw_fn:
                    raw_fn(lines)
                write(p, lines, crlf_wit)
    write(os.path.join(d, "xray.log"), L, crlf_log)
    for k in (1, 2, 3):
        ac = ["registered 30462", "unregistered 0", "upd 392 4 mil_smart_terrain_2_1 0",
              "upd 393 %d mil_smart_terrain_2_10 0" % (5 if counter_change == k else 4), "upd 900 %d online_thing 1" % (7 + k)]
        write(os.path.join(d, "det_alife_counts_%s_%d.txt" % (rtag, k)), ac, True)
        write(os.path.join(d, "det_counters_%s_%d.txt" % (rtag, k)), ["stream alife 589 889035392 863416037", "sq 15457 4 0 1 0 0", "info x 1"], True)
        write(os.path.join(d, "alife_dump_%s_%d.txt" % (rtag, k)), ["#checkpoint %d" % k, "1\tsec\tname\t65535\t1\t2\t1.0,2.0,3.0\tfalse"], True)
    sched = []
    calls = list(CALLS) + ([46] if sched_extra else [])
    for T in sorted(calls):
        sched.append("%d alife_simulator 33 452 0.500000" % cpf(T))
        sched.append("%d autosave_manager 33 3000 0.500000" % cpf(T))
    write(os.path.join(d, "det_trace_%s.txt.sched" % rtag), sched, True)
    return d

GOOD = os.path.join(ST, "good")
os.makedirs(GOOD)
for t, sp in S.RUNS.items():
    make_run(GOOD, t, sp)

WIT_KEYS = {"rec_fn", "crlf_wit", "no_witness", "no_bad", "bad_extra", "raw_fn", "drop_bad", "no_uu", "no_pj"}

def case_root(name, changes):
    """changes: {tag: kwargs for make_run}; every other run folder is a symlink to the good set."""
    root = os.path.join(ST, "cases", name, "runs"); os.makedirs(root)
    for t, sp in S.RUNS.items():
        if t in changes:
            kw = dict(changes[t])
            if not set(kw) & WIT_KEYS and sp["k"]:
                kw["link_wit"] = os.path.join(GOOD, "det_" + t, "synth_%s.txt" % t)
            make_run(root, t, sp, **kw)
        else:
            os.symlink(os.path.join(GOOD, "det_" + t), os.path.join(root, "det_" + t))
    return root

IDS = ["F0", "A1", "A2", "A3", "A4", "A5", "PAT", "PWC", "S0", "S0B", "SC", "LB", "TW", "TWB16", "TWB32", "P1", "P2",
       "P3", "P4", "U1", "PA1", "PA2"]
WREAD = ("PAT", "S0B", "TW", "P1", "P2", "P3", "U1", "PA1", "PA2")   # checks that read an on run's witness
results = []

def run_score(name, root, expect, code, scorer=SCF, must=()):
    out = os.path.join(ST, "cases", name, "score.txt") if os.path.isdir(os.path.join(ST, "cases", name)) else os.path.join(ST, name + "_score.txt")
    p = subprocess.run([sys.executable, "-I", scorer, "root=" + root, "out=" + out], capture_output=True, text=True)
    got = {}
    txt = open(out).read() if os.path.exists(out) else ""
    for l in txt.split("\n"):
        m = re.match(r"^([A-Z0-9]+) (PASS|FAIL|NO VERDICT|DROPPED): ", l)
        if m:
            got[m.group(1)] = m.group(2)
    want = {i: expect.get(i, "PASS") for i in IDS}
    ok = got == want and p.returncode == code and all(s in txt for s in must)
    diff = {i: (want[i], got.get(i)) for i in IDS if want[i] != got.get(i)}
    results.append((name, ok))
    print("%-4s %-34s exit %d (want %d) %s%s" % ("ok" if ok else "BAD", name, p.returncode, code,
          "" if not diff else "verdicts differ %s " % diff, "" if all(s in txt for s in must) else "missing text %s" % [s for s in must if s not in txt]))
    if not ok:
        print("     " + "\n     ".join(txt.split("\n")[-30:]) + "\n     stderr: " + p.stderr[-800:])

K2F = lambda T, j: F0 + (T - 1) * 2 + j
K3F = lambda T, j: F0 + (T - 1) * 3 + j

def first(recs, cond):
    return next(r for r in recs if cond(r))

# ---- good sets
run_score("good", GOOD, {}, 0)
run_score("good_crlf", case_root("good_crlf", {t: {"crlf_log": False, "crlf_wit": True} for t in
          ("s4p_o1", "s4p_on2a", "s4p_on3", "s4p_off2b", "s4p_b1off2", "s4p_brk32")}), {}, 0)
def m_absent(recs):   # absent-value dashes: no pa, no pn, no upload on some frames of an on run
    for r in recs:
        if 30 <= r["T"] <= 40:
            r["pa"] = r["pn"] = r["up"] = r["pj"] = None
run_score("good_absent_dashes", case_root("good_absent_dashes", {"s4p_on2a": {"rec_fn": m_absent}}), {}, 0)

# ---- mutants
def m_pat(recs):
    first(recs, lambda r: r["n"] == 0 and r["T"] == 50)["hud"] += 1.0
def m_pwc(recs):
    for r in recs:
        r["a"], r["dt"] = r["j"] / 2.0, S.dt_exp(2)
        r["hud"] = H0 + 33 * r["T"] + r["a"] * 33
        if r["pw"] and r["pw"]["form"] == "L":
            r["pw"]["u"] = r["a"]
def m_tw(recs):
    first(recs, lambda r: r["F"] == K2F(50, 1))["tw"] = 1
def m_twc(recs):
    first(recs, lambda r: r["n"] == 0 and r["T"] == 50)["tw"] = 1
def m_tw_minus(F):
    def f(recs):
        first(recs, lambda r: r["F"] == F)["tw"] -= 1
    return f
def m_tw_plus(F):
    def f(recs):
        first(recs, lambda r: r["F"] == F)["tw"] += 1
    return f
def m_p1(recs):
    for r in recs:
        if r["n"] == 0 and r["pw"] and r["pw"]["form"] == "L":
            w = r["pw"]; w["hd"] = w["hc"]; w["hu"] = w["hdu"] = H("up", w["hc"]); r["up"][0] = w["hu"]
def m_p2_u(recs):
    first(recs, lambda r: r["n"] == 0 and r["T"] == 40)["pw"]["u"] += 0.01
def m_p2_hu(recs):
    first(recs, lambda r: r["n"] == 0 and r["T"] == 41)["pw"]["hdu"] = H("other")
def m_p2_joint(recs):
    w = first(recs, lambda r: r["n"] == 0 and r["T"] == 42)["pw"]; w["jd"] = (w["jd"][0] + 0.01, w["jd"][1], w["jd"][2])
def m_joint_fallback(recs):
    w = first(recs, lambda r: r["n"] == 0 and r["T"] == 42)["pw"]; w["jd"] = w["jc"]
def m_p3(recs):
    w = first(recs, lambda r: r["n"] == 1 and r["T"] == 43)["pw"]; w["hd"] = H("x3"); w["hu"] = w["hdu"] = H("up", w["hd"])
def m_p4_p(recs):
    first(recs, lambda r: r["T"] == 44 and r["n"] == 0)["p"][4] = 1
def m_p4_up(recs):
    first(recs, lambda r: r["T"] == 44)["up"] = [H("a"), H("b")]
def m_u1(recs):
    first(recs, lambda r: r["T"] == 45 and r["n"] == 0)["uu"] = [2, 1, 0, 1]
def m_pa1_ws(recs):
    a = first(recs, lambda r: r["T"] == 46 and r["n"] == 1)["pa"]; a["ws"] = (a["ws"][0] + 0.01, a["ws"][1], a["ws"][2])
def m_pa1_hx(recs):
    first(recs, lambda r: r["T"] == 47)["pa"]["hxe"] = H("moved")
def m_pa2(recs):   # the recorded bones are not those the placement used: the item wanders in its drawn frame
    for r in recs:
        a = r["pa"]
        if a and a["form"] == "C":
            a["ld"] = a["ls"] = r5((OFS[0] + 0.01 * (r["T"] % 4), OFS[1], OFS[2]))
def m_pa1_I_hd(recs):   # an I line whose drawn bone is not the read one: not the bones-as-read path
    a = first(recs, lambda r: is_I(r["T"], r["n"]) and r["T"] == 53)["pa"]; a["hd"] = (a["hd"][0] + 0.01, a["hd"][1], a["hd"][2])
    first(recs, lambda r: is_I(r["T"], r["n"]) and r["T"] == 53)["pj"][1] = a["hd"]
def m_pa1_I_off(recs):  # form I in an off run (bit 16 off: nothing is corrected)
    r = first(recs, lambda r: r["T"] == 53 and r["n"] == 0)
    r["pa"].update(form="I", cls="c", u=r["a"], hd=hand(53), hr=hand(53), ld=OFS, ls=OFS)
def m_pa2_identity_only(recs):   # every correction is the parent-only I path: no bone correction shown
    for r in recs:
        a = r["pa"]
        if a and a["form"] == "C":
            a["form"] = "I"; a["hd"] = a["hr"]; a["ws"] = add(a["wd"], PMOVE)
            r["pj"][1] = a["hd"]
def m_u1_storage(recs):
    first(recs, lambda r: r["T"] == 45 and r["n"] == 0)["uu"] = [2, 0, 1, 1]
def m_u1_never_posed(recs):
    for r in recs:
        r["uu"][3] = 0
def m_pj_shift(recs):   # W2 is reported only: a mismatch shows in the reported max, no verdict changes
    r = first(recs, lambda r: r["T"] == 55 and r["n"] == 0); r["pj"][1] = (r["pj"][1][0] + 0.01, r["pj"][1][1], r["pj"][1][2])
def m_pj_without_pn(recs):   # pj printed while pn is not form L: not a shape the code prints
    first(recs, lambda r: r["T"] == 55 and r["n"] == 0)["pn"] = None
def m_pose_bad(lines):   # an unknown pw form: not a shape the code prints
    i = 37; t = lines[i].split(); t[65] = "Q"; lines[i] = " ".join(t)

CASES = [
    ("m_f0_switch", {"s4p_on3": {"cmd_drop": "-det_synth_pose_npc %s" % NPC}}, {"F0": "FAIL"}, 1, ()),
    ("m_f0_pose_line", {"s4p_on2a": {"no_pose_line": True}}, {"F0": "FAIL"}, 1, ("engine pose witness line",)),
    ("m_f0_item_switch_partA", {"s4p_o2": {"cmd_drop": "-det_synth_item"}}, {"F0": "FAIL"}, 1, ("missing switch -det_synth_item",)),
    ("m_f0_item_none", {"s4p_s2": {"item": "NONE"}}, {"F0": "FAIL"}, 1, ("item NONE",)),
    ("m_f0_item_line_missing", {"s4p_b1off2": {"item": None}}, {"F0": "FAIL"}, 1, ("0 'det_synth stimulus: item' lines",)),
    ("m_f0_item_differs", {"s4p_on2b": {"item": "slot 2 section wpn_pm"}}, {"F0": "FAIL"}, 1, ("runs took different items",)),
    ("m_a1", {"s4p_s2": {"counter_change": 2}}, {"A1": "FAIL", "A2": "NO VERDICT", "A3": "NO VERDICT", "A4": "NO VERDICT"}, 3, ()),
    ("m_a2", {"s4p_o2": {"counter_change": 3}}, {"A2": "FAIL"}, 1, ()),
    ("m_a3", {t: {"cp_shift": 20} for t in ("s4p_o1", "s4p_o2", "s4p_o3")}, {"A3": "FAIL"}, 1, ()),
    ("m_a4", {"s4p_o1": {"sched_extra": True}}, {"A4": "FAIL"}, 1, ()),
    ("m_a5", {"s4p_o2": {"syn_line": True}}, {"A5": "FAIL"}, 1, ()),
    ("m_pat", {"s4p_on3": {"rec_fn": m_pat}}, {"PAT": "FAIL"}, 1, ()),
    ("m_pwc_mustfail_did_not_fail", {"s4p_wc2": {"rec_fn": m_pwc}}, {"PWC": "FAIL"}, 1, ()),
    ("m_s0", {"s4p_off2b": {"counter_change": 2}}, {"S0": "FAIL", "SC": "NO VERDICT", "LB": "NO VERDICT"}, 3, ()),
    ("m_s0b", {"s4p_on2a": {"no_stim": True}}, {"S0B": "FAIL"}, 1, ()),
    ("m_sc_heap_layout_tripwires_clean", {"s4p_on2a": {"counter_change": 1}}, {"SC": "FAIL"}, 1,
     ("heap layout", "sim_pose.cpp:316")),
    ("m_lb_layout", {"s4p_b1off2": {"counter_change": 2}}, {"LB": "FAIL"}, 1, ("m_sim_attach", "GameObject.h:258")),
    ("m_tw_bad_line", {"s4p_on2b": {"rec_fn": m_tw, "bad_extra": [("pose", K2F(50, 1))]}}, {"TW": "FAIL"}, 1, ()),
    ("m_tw_consistency", {"s4p_off3": {"rec_fn": m_twc}}, {"TW": "FAIL"}, 1, ()),
    ("m_twb16_mustfail_did_not_fail", {"s4p_brk16": {"no_bad": True}}, {"TWB16": "FAIL"}, 1, ()),
    ("m_twb16_frame_not_caught", {"s4p_brk16": {"rec_fn": m_tw_minus(K2F(60, 1)), "drop_bad": ("pose", K2F(60, 1))}}, {"TWB16": "FAIL"}, 1, ()),
    ("m_twb16_wrong_slice", {"s4p_brk16": {"rec_fn": m_tw_plus(K2F(61, 0)), "bad_extra": [("attach", K2F(61, 0))]}}, {"TWB16": "FAIL"}, 1, ()),
    ("m_twb32_mustfail_did_not_fail", {"s4p_brk32": {"no_bad": True}}, {"TWB32": "FAIL"}, 1, ()),
    ("m_twb32_frame_not_caught", {"s4p_brk32": {"rec_fn": m_tw_minus(K2F(60, 1)), "drop_bad": ("attach", K2F(60, 1))}}, {"TWB32": "FAIL"}, 1, ()),
    ("m_twb32_wrong_slice", {"s4p_brk32": {"rec_fn": m_tw_plus(K2F(61, 1)), "bad_extra": [("pose", K2F(61, 1))]}}, {"TWB32": "FAIL"}, 1, ()),
    ("m_p1_never_blended", {"s4p_on2a": {"rec_fn": m_p1}}, {"P1": "FAIL"}, 1, ()),
    ("m_p2_u", {"s4p_on3": {"rec_fn": m_p2_u}}, {"P2": "FAIL"}, 1, ()),
    ("m_p2_u_crlf", {"s4p_on3": {"rec_fn": m_p2_u, "crlf_wit": True, "crlf_log": False}}, {"P2": "FAIL"}, 1, ()),
    ("m_p2_hu_vs_hdu", {"s4p_on2b": {"rec_fn": m_p2_hu}}, {"P2": "FAIL"}, 1, ()),
    ("m_p2_joint", {"s4p_on2a": {"rec_fn": m_p2_joint}}, {"P2": "FAIL"}, 1, ()),
    ("m_p2_joint_fallback_strict", {"s4p_on2a": {"rec_fn": m_joint_fallback}}, {"P2": "FAIL"}, 1, ()),
    ("m_p3", {"s4p_on2a": {"rec_fn": m_p3}}, {"P3": "FAIL"}, 1, ()),
    ("m_p4_p_column", {"s4p_off2a": {"rec_fn": m_p4_p}}, {"P4": "FAIL"}, 1, ()),
    ("m_p4_upload", {"s4p_off3": {"rec_fn": m_p4_up}}, {"P4": "FAIL"}, 1, ()),
    ("m_u1_unmarked_drawn_off_live", {"s4p_on3": {"rec_fn": m_u1}}, {"U1": "FAIL"}, 1, ()),
    ("m_u1_no_w1_column", {"s4p_on2a": {"no_uu": True}}, {"U1": "FAIL"}, 1, ("required witness addition W1",)),
    ("m_pa1_ws", {"s4p_on2a": {"rec_fn": m_pa1_ws}}, {"PA1": "FAIL"}, 1, ()),
    ("m_pa1_hx", {"s4p_off2b": {"rec_fn": m_pa1_hx}}, {"PA1": "FAIL"}, 1, ()),
    ("m_pa2_ld_wanders", {"s4p_on3": {"rec_fn": m_pa2}}, {"PA2": "FAIL"}, 1, ()),
    ("m_pa1_I_hd_ne_hr", {"s4p_on2a": {"rec_fn": m_pa1_I_hd}}, {"PA1": "FAIL"}, 1, ("form I with hd != hr",)),
    ("m_pa1_I_in_off_run", {"s4p_off2a": {"rec_fn": m_pa1_I_off}}, {"PA1": "FAIL"}, 1, ("bit 16 off: form I",)),
    ("m_pa2_identity_only_F1", {"s4p_on2b": {"rec_fn": m_pa2_identity_only}}, {"PA2": "FAIL"}, 1, ("moved onto the drawn hand",)),
    ("m_twb32_I_not_caught", {"s4p_brk32": {"rec_fn": m_tw_minus(K2F(63, 1)), "drop_bad": ("attach", K2F(63, 1))}}, {"TWB32": "FAIL"}, 1, ()),
    ("m_u1_unmarked_with_storage", {"s4p_off2b": {"rec_fn": m_u1_storage}}, {"U1": "FAIL"}, 1, ()),
    ("m_u1_never_posed", {"s4p_on2b": {"rec_fn": m_u1_never_posed}}, {"U1": "FAIL"}, 1, ("posed between ticks <",)),
    ("w2_pj_mismatch_reported", {"s4p_on2a": {"rec_fn": m_pj_shift}}, {}, 0, ("max |pa hd - pj| 0.01000",)),
    ("w1_only_no_pj_column", {"s4p_on3": {"no_pj": True}}, {}, 0, ("W2 hand link: pj column absent",)),
]
for name, ch, exp, code, must in CASES:
    run_score(name, case_root(name, ch), exp, code, must=must)

# a missing witness of one on run: every check that reads it FAILS by name
run_score("m_missing_witness", case_root("m_missing_witness", {"s4p_on3": {"no_witness": True}}),
          {i: "FAIL" for i in WREAD}, 1, must=("missing file", "synth_s4p_on3.txt"))
# a malformed pose column in one on run: every check that reads that witness FAILS by name
run_score("m_pose_malformed", case_root("m_pose_malformed", {"s4p_on2b": {"raw_fn": m_pose_bad}}),
          {i: "FAIL" for i in WREAD}, 1, must=("malformed witness", "synth_s4p_on2b.txt line 38"))
# the series' one retry: det_<tag>_r1 is read and named; the first folder is broken
root = case_root("retry_r1", {})
os.unlink(os.path.join(root, "det_s4p_on2a")); os.makedirs(os.path.join(root, "det_s4p_on2a"))
make_run(root, "s4p_on2a", S.RUNS["s4p_on2a"], rtag="s4p_on2a_r1")
run_score("retry_r1", root, {}, 0, must=("substitution: s4p_on2a read from",))
# the joint rule the probe chose: with fallbacks seen in the probe (PROBE_JOINT_LERP 0), a bone drawn as cur passes
# a pj column on a line whose pn is not form L: malformed, every check reading that witness FAILS by name
run_score("m_pj_without_pn", case_root("m_pj_without_pn", {"s4p_on2b": {"rec_fn": m_pj_without_pn}}),
          {i: "FAIL" for i in WREAD}, 1, must=("pj present without pn form L",))
# PROBE_TOL_LD 0 (prereg 7.1 branch c): PA2's cluster part is reported only, so a wandering ld does not fail PA2
run_score("pa2_clusters_reported_tolld0", case_root("pa2_clusters_reported_tolld0", {"s4p_on3": {"rec_fn": m_pa2}}), {}, 0,
          scorer=SCT, must=("cluster part reported only",))
run_score("joint_fallback_allowed", case_root("joint_fallback_allowed", {"s4p_on2a": {"rec_fn": m_joint_fallback}}), {}, 0,
          scorer=SCJ, must=("joint fallbacks 1",))

# refusals of the REGISTERED scorer
def refuse(name, args, must):
    p = subprocess.run([sys.executable, "-I", SC] + args, capture_output=True, text=True)
    ok = p.returncode == 2 and must in p.stdout
    results.append((name, ok)); print("%-4s %-34s exit %d (want 2) %s" % ("ok" if ok else "BAD", name, p.returncode, p.stdout.strip()[-120:]))
refuse("refuse_placeholders", ["root=" + GOOD, "out=" + os.path.join(ST, "refused.txt")], "REFUSED (placeholders")
p = subprocess.run([sys.executable, "-I", SCF, "root=" + GOOD, "out=" + os.path.join(ST, "good_score.txt")], capture_output=True, text=True)
ok = p.returncode == 2 and "exists" in p.stdout; results.append(("refuse_second_score_same_out", ok))
print("%-4s %-34s exit %d (want 2)" % ("ok" if ok else "BAD", "refuse_second_score_same_out", p.returncode))

# ---- probe mode (one probe boot, K = 3, mask 23)
PR = os.path.join(ST, "probe")
def probe_case(name, want_code, must=(), skip=False, **kw):
    root = os.path.join(PR, name); os.makedirs(root)
    if not skip:
        make_run(root, "s4p_probe", S.PROBE_RUN, tmax=100, **kw)
    out = os.path.join(root, "probe.txt")
    p = subprocess.run([sys.executable, "-I", SCF, "mode=probe", "root=" + root, "out=" + out], capture_output=True, text=True)
    txt = open(out).read() if os.path.exists(out) else p.stdout
    ok = p.returncode == want_code and all(s in txt for s in must)
    results.append((name, ok))
    print("%-4s %-34s exit %d (want %d) %s" % ("ok" if ok else "BAD", name, p.returncode, want_code,
          " | ".join(l for l in txt.split("\n") if "STOP" in l or "PROBE: GO" in l)[:300]))
    if not ok:
        print("     " + "\n     ".join(txt.split("\n")) + "\n     stderr: " + p.stderr[-800:])
def p_noblend(recs):
    for r in recs:
        if r["pw"] and r["pw"]["form"] == "L":
            w = r["pw"]; w["hd"] = w["hl"] = w["hp"] = w["hc"]; w["hu"] = w["hdu"] = H("up", w["hc"])
def p_f5(recs):
    for r in recs:
        if r["n"] == 0 and r["pw"]:
            r["p"][4], r["p"][5] = 1, 0
def p_nonpc(recs):
    for r in recs:
        r["pn"] = r["pa"] = r["pj"] = None
def p_hx(recs):
    first(recs, lambda r: r["T"] == 50 and r["n"] == 0)["pa"]["hxe"] = H("moved")
def p_noposed(recs):
    for r in recs:
        r["uu"][3] = 0
def p_tw(recs):
    first(recs, lambda r: r["F"] == K3F(50, 1))["tw"] = 1
def p_idle(step, jitter):   # the witness NPC's hand moves `step` m per tick; ld jitters by up to `jitter` m
    def f(recs):
        hi = lambda T: r5((200.0 + step * T, 1.2, 90.0))
        for r in recs:
            a = r["pa"]
            if a and a["form"] == "C":
                T, u = r["T"], a["u"]
                hd_, hr = lerp(hi(T - 1), hi(T), u), hi(T)
                ld = r5((OFS[0] + jitter * (T % 3) / 2.0, OFS[1], OFS[2]))
                a.update(hd=hd_, hr=hr, wd=add(hd_, ld), ws=add(hr, ld), ld=ld, ls=ld)
                r["pj"][1] = hd_
    return f
CONSTS = ('"PROBE_P_LERP": 198,', '"PROBE_P_TICK": 99,', '"PROBE_UPLOADS": 300,', '"PROBE_PA_CR": 178,',
          '"PROBE_PA_CLUSTERS": 1,', '"PROBE_JOINT_LERP": 1,', '"PROBE_U_POSED": 200,', '"PROBE_NPC": \'%s\',' % NPC,
          '"PROBE_TOL_LD": 0.001,')
probe_case("probe_go", 0, must=CONSTS + ("S4D2 PROBE: GO", "(b) F5: render-only recalcs byte-different 0, byte-equal 198",
           "form I (parent moved, bones as read; NOT counted as a correction): 20 pa lines, 20 n=0 moved",
           "-> branch a", "W2 hand link: 297 comparable lines, max |pa hd - pj| 0.00000",
           "item (-det_synth_item): slot 3 section wpn_ak74"))
probe_case("probe_go_crlf", 0, must=CONSTS[:3] + ("S4D2 PROBE: GO",), crlf_log=False, crlf_wit=True)
probe_case("probe_no_w1_column", 1, must=("PROBE STOP w1",), no_uu=True)
probe_case("probe_no_skeleton_blended", 1, must=("PROBE STOP pose",), rec_fn=p_noblend)
probe_case("probe_f5_history_dropped", 1, must=("PROBE STOP f5",), rec_fn=p_f5)
probe_case("probe_no_witness_npc", 1, must=("PROBE STOP npc", "PROBE STOP attach"), rec_fn=p_nonpc)
probe_case("probe_ld_spread", 1, must=("PROBE STOP ld",), rec_fn=m_pa2)
probe_case("probe_hx_changes", 1, must=("PROBE STOP hx",), rec_fn=p_hx)
probe_case("probe_no_unmarked_posed", 1, must=("PROBE STOP optin",), rec_fn=p_noposed)
probe_case("probe_tripwire", 1, must=("PROBE STOP tripwire",), rec_fn=p_tw, bad_extra=[("pose", K3F(50, 1))])
probe_case("probe_attach_identity_only_F1", 1, must=("PROBE STOP attach", "form I not counted"), rec_fn=m_pa2_identity_only)
probe_case("probe_tol_ld_branch_b", 0, must=('"PROBE_TOL_LD": 0.0001224', "-> branch b", "S4D2 PROBE: GO"), rec_fn=p_idle(0.0015, 0.0))
probe_case("probe_tol_ld_idle_hand_c", 0, must=('"PROBE_TOL_LD": 0.0,', "-> branch c", "REPORTED only", "S4D2 PROBE: GO"),
           rec_fn=p_idle(0.001, 0.0006))
probe_case("probe_no_stimulus", 1, must=("PROBE STOP stimulus",), no_stim=True)
probe_case("probe_item_none", 1, must=("PROBE STOP item: item NONE",), item="NONE")
probe_case("probe_item_switch_missing", 1, must=("PROBE STOP flags: missing switch -det_synth_item",), cmd_drop="-det_synth_item")
probe_case("probe_malformed", 1, must=("PROBE STOP input", "malformed witness"), raw_fn=m_pose_bad)
probe_case("probe_missing", 1, must=("PROBE STOP input", "missing run folder"), skip=True)

n_ok = sum(1 for _, ok in results if ok)
print("S4D2 SELFTEST: %s (%d/%d)" % ("PASS" if n_ok == len(results) else "FAIL", n_ok, len(results)))
PYEOF
echo "output folder: $ST ($(du -sh "$ST" | cut -f1))"
