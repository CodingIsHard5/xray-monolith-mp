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
# Exit code (fixed 2026-11-28, the fault found in s4d1_selftest.sh at registration): this script used to exit 0 after a
# Python crash and after failed cases (last command an echo; `python | tee` without the pipe's status; the Python
# never exited non-zero). Now finish() exits 1 unless every case passed, and the wrapper reads PIPESTATUS. Cases
# selfcheck_crash and selfcheck_fail re-run THIS file with S4D2_SELFTEST_SELFCHECK=crash|fail and require exit 1.
python3 -I - "$ST" "$SC" "$HERE/$(basename "$0")" <<'PYEOF' 2>&1 | tee "$ST/selftest_output.txt"
import hashlib, importlib.util, os, re, subprocess, sys
ST, SC, WRAP = sys.argv[1], sys.argv[2], sys.argv[3]
SELFCHECK = os.environ.get("S4D2_SELFTEST_SELFCHECK", "")
if SELFCHECK == "crash":
    raise RuntimeError("deliberate crash (S4D2_SELFTEST_SELFCHECK=crash)")
assert SELFCHECK in ("", "fail"), SELFCHECK
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


def finish():
    n_ok = sum(1 for _, ok in results if ok)
    good = bool(results) and n_ok == len(results)
    print("S4D2 SELFTEST: %s (%d/%d)" % ("PASS" if good else "FAIL", n_ok, len(results)))
    sys.stdout.flush()
    sys.exit(0 if good else 1)


if SELFCHECK == "fail":
    results.append(("selfcheck_forced_bad_case", False)); print("BAD  selfcheck_forced_bad_case (S4D2_SELFTEST_SELFCHECK=fail)")
    finish()

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

# the real tails are build 1 witness lines (58 tokens, K 3): read with the b1 layout
TD_SPEC = {"s4d_probe": S.S(3, 7, exe=S.B1_EXE, b1=1), "s4d_probe15": S.S(3, 15, exe=S.B1_EXE, b1=1)}
# ---- teardown witness lines (the quit after cp3 writes one line with no actor; scorer teardown_split): REAL bytes
import hashlib, json
# ---- REAL bytes (cut 2026-11-28, read only): the last 30 witness lines of out/det_<tag>/synth_<tag>.txt (LF, as in
# the file) and the matching xray.log lines (CRLF, as in the file): command line, det_synth lines, cp1-cp3, the lines
# from cp3 to '- Disconnect' and the one after it. The teardown line is the last witness line (F after cp3, s2 dashes).
REAL = {
    's4d_probe': (
        [
            'F 16221 T 5378 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179432.000 | s2 206.05209 0.02860 88.48257 205.71548 0.02830 88.23655 206.05209 0.02860 88.48257 | s3 1 207.11699 110.21176 -99.26889 207.11700 122.35507 -120.36887 207.11700 104.14011 -88.71890 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16222 T 5378 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179443.000 | s2 205.88379 0.02845 88.35956 205.71548 0.02830 88.23655 205.88379 0.02845 88.35956 | s3 1 207.11700 116.28342 -109.81888 207.11700 122.35507 -120.36887 207.11700 104.14011 -88.71890 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16223 T 5379 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179454.000 | s2 205.71548 0.02830 88.23655 205.65300 0.06273 88.14102 205.71548 0.02830 88.23655 | s3 1 207.11700 122.35507 -120.36887 207.11700 139.83223 -150.75926 207.11700 122.35507 -120.36887 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16224 T 5379 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179465.000 | s2 205.69464 0.03978 88.20470 205.65300 0.06273 88.14102 205.69464 0.03978 88.20470 | s3 1 207.11699 128.18079 -130.49899 207.11700 139.83223 -150.75926 207.11700 122.35507 -120.36887 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16225 T 5379 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179476.000 | s2 205.67383 0.05125 88.17287 205.65300 0.06273 88.14102 205.67383 0.05125 88.17287 | s3 1 207.11700 134.00652 -140.62914 207.11700 139.83223 -150.75926 207.11700 122.35507 -120.36887 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16226 T 5380 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179487.000 | s2 205.65300 0.06273 88.14102 205.68001 0.25653 88.29777 205.65300 0.06273 88.14102 | s3 1 207.11700 139.83223 -150.75926 207.11700 151.76012 -171.51430 207.11700 139.83223 -150.75926 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16227 T 5380 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179498.000 | s2 205.66199 0.12733 88.19327 205.68001 0.25653 88.29777 205.66199 0.12733 88.19327 | s3 1 207.11699 143.80820 -157.67761 207.11700 151.76012 -171.51430 207.11700 139.83223 -150.75926 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16228 T 5380 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179509.000 | s2 205.67101 0.19193 88.24552 205.68001 0.25653 88.29777 205.67101 0.19193 88.24552 | s3 1 207.11700 147.78415 -164.59595 207.11700 151.76012 -171.51430 207.11700 139.83223 -150.75926 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16229 T 5381 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179520.000 | s2 205.68001 0.25653 88.29777 206.22160 0.02574 88.47267 205.68001 0.25653 88.29777 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16230 T 5381 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179531.000 | s2 205.86053 0.17960 88.35606 206.22160 0.02574 88.47267 205.86053 0.17960 88.35606 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16231 T 5381 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179542.000 | s2 206.04108 0.10267 88.41437 206.22160 0.02574 88.47267 206.04108 0.10267 88.41437 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16232 T 5382 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179553.000 | s2 206.22160 0.02574 88.47267 205.68605 0.03019 88.26493 206.22160 0.02574 88.47267 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16233 T 5382 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179564.000 | s2 206.04309 0.02722 88.40342 205.68605 0.03019 88.26493 206.04309 0.02722 88.40342 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16234 T 5382 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179575.000 | s2 205.86456 0.02870 88.33418 205.68605 0.03019 88.26493 205.86456 0.02870 88.33418 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16235 T 5383 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179586.000 | s2 205.68605 0.03019 88.26493 206.26761 0.02933 88.42514 205.68605 0.03019 88.26493 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16236 T 5383 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179597.000 | s2 205.87990 0.02990 88.31833 206.26761 0.02933 88.42514 205.87990 0.02990 88.31833 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16237 T 5383 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179608.000 | s2 206.07376 0.02962 88.37173 206.26761 0.02933 88.42514 206.07376 0.02962 88.37173 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16238 T 5384 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179619.000 | s2 206.26761 0.02933 88.42514 205.70790 0.02483 88.09684 206.26761 0.02933 88.42514 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16239 T 5384 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179630.000 | s2 206.08104 0.02783 88.31570 205.70790 0.02483 88.09684 206.08104 0.02783 88.31570 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16240 T 5384 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179641.000 | s2 205.89447 0.02633 88.20627 205.70790 0.02483 88.09684 205.89447 0.02633 88.20627 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16241 T 5385 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179652.000 | s2 205.70790 0.02483 88.09684 206.19948 0.03344 88.43396 205.70790 0.02483 88.09684 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16242 T 5385 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179663.000 | s2 205.87177 0.02770 88.20921 206.19948 0.03344 88.43396 205.87177 0.02770 88.20921 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16243 T 5385 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179674.000 | s2 206.03561 0.03057 88.32159 206.19948 0.03344 88.43396 206.03561 0.03057 88.32159 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16244 T 5386 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179685.000 | s2 206.19948 0.03344 88.43396 206.83578 0.02741 88.42514 206.19948 0.03344 88.43396 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16245 T 5386 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179696.000 | s2 206.41158 0.03143 88.43102 206.83578 0.02741 88.42514 206.41158 0.03143 88.43102 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16246 T 5386 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179707.000 | s2 206.62369 0.02942 88.42809 206.83578 0.02741 88.42514 206.62369 0.02942 88.42809 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16247 T 5387 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179718.000 | s2 206.83578 0.02741 88.42514 206.33240 0.02586 88.42514 206.83578 0.02741 88.42514 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16248 T 5387 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179729.000 | s2 206.66798 0.02690 88.42514 206.33240 0.02586 88.42514 206.66798 0.02690 88.42514 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16249 T 5387 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179740.000 | s2 206.50018 0.02638 88.42514 206.33240 0.02586 88.42514 206.50018 0.02638 88.42514 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16270 T 5388 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179751.000 | s2 - - - - - - - - - | s3 - - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
        ],
        [
            'command line z:\\home\\caden\\projects\\stalkergameengine\\run\\bin\\simtick18.exe -nointro -dbg -start server(bench1/single/alife/load) client(localhost) -det_mode -det_tag s4d_probe -det_interval 60 -det_checkpoints 3 -det_passes 100,200,320 -det_lua_streams -det_counters -det_synth_k 3 -sim_4d_mask 7 -det_synth_tw -det_synth_walk -det_synth_tracer',
            '* det_synth: k 3 wallclock 0 tw 1 break 0 mask 7',
            '* det_synth: K 3, witness z:/home/caden/projects/stalkergameengine/run\\appdata\\logs\\synth_s4d_probe.txt',
            '* det_rng_state cp1: Random=852712388 Random32=0 alife=302455169 crt_next=6334 frame=4982 time_ms=55803 tick=1632',
            '* det_rng_state cp2: Random=656316615 Random32=0 alife=302455169 crt_next=26500 frame=10112 time_ms=112233 tick=3342',
            '* det_rng_state cp3: Random=1936983972 Random32=0 alife=302455169 crt_next=19169 frame=16247 time_ms=179718 tick=5387',
            '* det_lua_state cp3: 937625969',
            '* eperf dump: checkpoint 3 written, 30464 objects',
            '* Log file has been saved successfully!',
            '- Disconnect',
            '[TutorialMonitor] Ended tutorial state tutorial_campfire_extinguish at 179751',
        ],
        '4c4d06c5a07c8c6a68671a40ecc4198a3462035a198eadd6c56ec8bdadfda469', 'be736f00f9db2b21ffc3c0134fbbeee70ddbecbb34580497e1d5b874e4081b96'),
    's4d_probe15': (
        [
            'F 16203 T 5372 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179234.000 | s2 206.25909 0.03165 88.29730 205.88731 0.02466 88.04163 206.25909 0.03165 88.29730 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16204 T 5372 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179245.000 | s2 206.07321 0.02815 88.16946 205.88731 0.02466 88.04163 206.07321 0.02815 88.16946 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16205 T 5373 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179256.000 | s2 205.88731 0.02466 88.04163 206.31953 0.02795 88.48488 205.88731 0.02466 88.04163 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16206 T 5373 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179267.000 | s2 206.03137 0.02576 88.18938 206.31953 0.02795 88.48488 206.03137 0.02576 88.18938 | s3 1 207.11699 14.09166 67.47495 207.11700 23.90241 50.47675 207.11700 9.18629 75.97405 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16207 T 5373 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179278.000 | s2 206.17545 0.02685 88.33713 206.31953 0.02795 88.48488 206.17545 0.02685 88.33713 | s3 1 207.11700 18.99704 58.97585 207.11700 23.90241 50.47675 207.11700 9.18629 75.97405 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16208 T 5374 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179289.000 | s2 206.31953 0.02795 88.48488 205.76857 0.02665 88.14809 206.31953 0.02795 88.48488 | s3 1 207.11700 23.90241 50.47675 207.11700 45.06852 13.78848 207.11700 23.90241 50.47675 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16209 T 5374 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179300.000 | s2 206.13586 0.02752 88.37261 205.76857 0.02665 88.14809 206.13586 0.02752 88.37261 | s3 1 207.11699 30.95778 38.24732 207.11700 45.06852 13.78848 207.11700 23.90241 50.47675 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16210 T 5374 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179311.000 | s2 205.95221 0.02708 88.26035 205.76857 0.02665 88.14809 205.95221 0.02708 88.26035 | s3 1 207.11700 38.01315 26.01790 207.11700 45.06852 13.78848 207.11700 23.90241 50.47675 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16211 T 5375 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179322.000 | s2 205.76857 0.02665 88.14809 206.26228 0.02856 88.57005 205.76857 0.02665 88.14809 | s3 1 207.11700 45.06852 13.78848 207.11700 65.49683 -21.64022 207.11700 45.06852 13.78848 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16212 T 5375 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179333.000 | s2 205.93314 0.02729 88.28874 206.26228 0.02856 88.57005 205.93314 0.02729 88.28874 | s3 1 207.11699 51.87795 1.97891 207.11700 65.49683 -21.64022 207.11700 45.06852 13.78848 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16213 T 5375 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179344.000 | s2 206.09772 0.02792 88.42940 206.26228 0.02856 88.57005 206.09772 0.02792 88.42940 | s3 1 207.11700 58.68740 -9.83066 207.11700 65.49683 -21.64022 207.11700 45.06852 13.78848 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16214 T 5376 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179355.000 | s2 206.26228 0.02856 88.57005 205.70720 0.02859 88.20445 206.26228 0.02856 88.57005 | s3 1 207.11700 65.49683 -21.64022 207.11700 85.18738 -55.80936 207.11700 65.49683 -21.64022 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16215 T 5376 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179366.000 | s2 206.07724 0.02857 88.44818 205.70720 0.02859 88.20445 206.07724 0.02857 88.44818 | s3 1 207.11699 72.06035 -33.02994 207.11700 85.18738 -55.80936 207.11700 65.49683 -21.64022 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16216 T 5376 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179377.000 | s2 205.89223 0.02858 88.32632 205.70720 0.02859 88.20445 205.89223 0.02858 88.32632 | s3 1 207.11700 78.62386 -44.41965 207.11700 85.18738 -55.80936 207.11700 65.49683 -21.64022 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16217 T 5377 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179388.000 | s2 205.70720 0.02859 88.20445 206.27199 0.02952 88.62603 205.70720 0.02859 88.20445 | s3 1 207.11700 85.18738 -55.80936 207.11700 104.14011 -88.71890 207.11700 85.18738 -55.80936 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16218 T 5377 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179399.000 | s2 205.89545 0.02890 88.34497 206.27199 0.02952 88.62603 205.89545 0.02890 88.34497 | s3 1 207.11699 91.50495 -66.77921 207.11700 104.14011 -88.71890 207.11700 85.18738 -55.80936 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16219 T 5377 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179410.000 | s2 206.08372 0.02921 88.48550 206.27199 0.02952 88.62603 206.08372 0.02921 88.48550 | s3 1 207.11700 97.82253 -77.74905 207.11700 104.14011 -88.71890 207.11700 85.18738 -55.80936 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16220 T 5378 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179421.000 | s2 206.27199 0.02952 88.62603 205.75200 0.02659 88.24551 206.27199 0.02952 88.62603 | s3 1 207.11700 104.14011 -88.71890 207.11700 122.35507 -120.36887 207.11700 104.14011 -88.71890 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16221 T 5378 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179432.000 | s2 206.09865 0.02854 88.49919 205.75200 0.02659 88.24551 206.09865 0.02854 88.49919 | s3 1 207.11699 110.21176 -99.26889 207.11700 122.35507 -120.36887 207.11700 104.14011 -88.71890 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16222 T 5378 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179443.000 | s2 205.92532 0.02757 88.37235 205.75200 0.02659 88.24551 205.92532 0.02757 88.37235 | s3 1 207.11700 116.28342 -109.81888 207.11700 122.35507 -120.36887 207.11700 104.14011 -88.71890 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16223 T 5379 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179454.000 | s2 205.75200 0.02659 88.24551 205.65300 0.24626 88.13755 205.75200 0.02659 88.24551 | s3 1 207.11700 122.35507 -120.36887 207.11700 139.83223 -150.75926 207.11700 122.35507 -120.36887 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16224 T 5379 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179465.000 | s2 205.71899 0.09981 88.20952 205.65300 0.24626 88.13755 205.71899 0.09981 88.20952 | s3 1 207.11699 128.18079 -130.49899 207.11700 139.83223 -150.75926 207.11700 122.35507 -120.36887 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16225 T 5379 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179476.000 | s2 205.68600 0.17304 88.17354 205.65300 0.24626 88.13755 205.68600 0.17304 88.17354 | s3 1 207.11700 134.00652 -140.62914 207.11700 139.83223 -150.75926 207.11700 122.35507 -120.36887 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16226 T 5380 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179487.000 | s2 205.65300 0.24626 88.13755 205.68001 0.08178 88.28903 205.65300 0.24626 88.13755 | s3 1 207.11700 139.83223 -150.75926 207.11700 151.76012 -171.51430 207.11700 139.83223 -150.75926 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16227 T 5380 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179498.000 | s2 205.66199 0.19143 88.18804 205.68001 0.08178 88.28903 205.66199 0.19143 88.18804 | s3 1 207.11699 143.80820 -157.67761 207.11700 151.76012 -171.51430 207.11700 139.83223 -150.75926 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16228 T 5380 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179509.000 | s2 205.67101 0.13660 88.23854 205.68001 0.08178 88.28903 205.67101 0.13660 88.23854 | s3 1 207.11700 147.78415 -164.59595 207.11700 151.76012 -171.51430 207.11700 139.83223 -150.75926 1 | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16229 T 5381 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179520.000 | s2 205.68001 0.08178 88.28903 206.07033 0.25164 88.50457 205.68001 0.08178 88.28903 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16230 T 5381 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179531.000 | s2 205.81010 0.13840 88.36088 206.07033 0.25164 88.50457 205.81010 0.13840 88.36088 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16231 T 5381 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179542.000 | s2 205.94022 0.19502 88.43272 206.07033 0.25164 88.50457 205.94022 0.19502 88.43272 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 16252 T 5382 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179553.000 | s2 - - - - - - - - - | s3 - - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
        ],
        [
            'command line z:\\home\\caden\\projects\\stalkergameengine\\run\\bin\\simtick18.exe -nointro -dbg -start server(bench1/single/alife/load) client(localhost) -det_mode -det_tag s4d_probe15 -det_interval 60 -det_checkpoints 3 -det_passes 100,200,320 -det_lua_streams -det_counters -det_synth_k 3 -sim_4d_mask 15 -det_synth_tw -det_synth_walk -det_synth_tracer',
            '* det_synth: k 3 wallclock 0 tw 1 break 0 mask 15',
            '* det_synth: K 3, witness z:/home/caden/projects/stalkergameengine/run\\appdata\\logs\\synth_s4d_probe15.txt',
            '* det_rng_state cp1: Random=-1195896386 Random32=0 alife=302455169 crt_next=6334 frame=4982 time_ms=55803 tick=1632',
            '* det_rng_state cp2: Random=-1324912062 Random32=0 alife=302455169 crt_next=26500 frame=10091 time_ms=112002 tick=3335',
            '* det_rng_state cp3: Random=-2079694359 Random32=0 alife=302455169 crt_next=19169 frame=16229 time_ms=179520 tick=5381',
            '* det_lua_state cp3: 920508979',
            '* eperf dump: checkpoint 3 written, 30462 objects',
            '* Log file has been saved successfully!',
            '- Disconnect',
            '[TutorialMonitor] Ended tutorial state tutorial_campfire_extinguish at 179553',
        ],
        'b62958c7f9522dcc4c95b81a298c5df570cb305e0b1cf11974e449bdaa1c847d', '7ecacd8f7fc16e2bbca945a11ba5e6945dac6be4fa7321a0238d25266807f8c3'),
}
for tag_, (wl_, xl_, wsha_, xsha_) in REAL.items():   # the literal above is the real bytes, unedited
    assert hashlib.sha256(("\n".join(wl_) + "\n").encode()).hexdigest() == wsha_, tag_
    assert hashlib.sha256(("\r\n".join(xl_) + "\r\n").encode()).hexdigest() == xsha_, tag_
TD = os.path.join(ST, "teardown"); os.makedirs(TD)
s_ = filled({}); assert s_.count("TEARDOWN_MAX = 2 ") == 1
SCN = os.path.join(ST, "scorer_filled_noteardown.py")   # mutation: the OLD rule (teardown drop disabled, max 2 -> 0)
open(SCN, "w").write(s_.replace("TEARDOWN_MAX = 2 ", "TEARDOWN_MAX = 0 "))
DRV = os.path.join(ST, "teardown_pattern.py")   # runs the scorer copy's pattern_errors on one fixture run folder
open(DRV, "w").write('''import importlib.util, json, re, sys
sc, root, tag, spec = sys.argv[1], sys.argv[2], sys.argv[3], json.loads(sys.argv[4])
sp = importlib.util.spec_from_file_location("sc", sc); M = importlib.util.module_from_spec(sp); sp.loader.exec_module(M)
r = M.Run(root, tag, spec)
try:
    r.wit()
    print("dropped %d" % r.td)
    errs = M.pattern_errors(r)
except M.Missing as m:
    if r._wit is None:
        print("dropped %s" % r.td)
    print("MISSING %s" % m); sys.exit(2)
# the fixture holds only the last 30 lines: cp1 and cp2 are absent from it by construction (the cut, not the rule)
cut = [e for e in errs if re.match(r"^cp[12] frame [0-9]+ tick [0-9]+: witness line absent$", e)]
for e in errs:
    print("%s %s" % ("CUT" if e in cut else "ERR", e))
sys.exit(1 if len(errs) > len(cut) else 0)
''')

def td_folder(name, tag, wit_fn=None, log_fn=None):
    wl, xl = list(REAL[tag][0]), list(REAL[tag][1])
    if wit_fn:
        wit_fn(wl)
    if log_fn:
        log_fn(xl)
    d = os.path.join(TD, name, "det_" + tag); os.makedirs(d)
    write(os.path.join(d, "synth_%s.txt" % tag), wl, False)     # LF, as the real witness
    write(os.path.join(d, "xray.log"), xl, True)                # CRLF, as the real engine log
    if not wit_fn and not log_fn:
        assert hashlib.sha256(open(os.path.join(d, "synth_%s.txt" % tag), "rb").read()).hexdigest() == REAL[tag][2]
        assert hashlib.sha256(open(os.path.join(d, "xray.log"), "rb").read()).hexdigest() == REAL[tag][3]
    return os.path.join(TD, name)

def td_case(name, tag, want_code, must=(), mustnot=(), wit_fn=None, log_fn=None, scorer=SCF):
    td_folder(name, tag, wit_fn, log_fn)
    p = subprocess.run([sys.executable, "-I", DRV, scorer, os.path.join(TD, name), tag, json.dumps(TD_SPEC[tag])],
                       capture_output=True, text=True)
    txt = p.stdout + p.stderr
    open(os.path.join(TD, name, "pattern.txt"), "w").write(txt)
    ok = p.returncode == want_code and all(s in txt for s in must) and not any(s in txt for s in mustnot)
    results.append((name, ok))
    print("%-4s %-22s exit %d (want %d) %s" % ("ok" if ok else "BAD", name, p.returncode, want_code,
          " | ".join(l for l in txt.split("\n") if l.startswith(("dropped", "ERR", "MISSING")))[:300]))
    if not ok:
        print("     " + "\n     ".join(txt.split("\n")))

TDL = "teardown witness lines dropped (trailing no-actor lines after cp3, at most 2): "
def td_no_actor(l, F):    # the real teardown line's columns with frame F
    t = l.split(); t[1] = str(F); return " ".join(t)
def td_three(wl):         # (b) three trailing no-actor lines after cp3: one more than TEARDOWN_MAX
    wl += [td_no_actor(wl[-1], 16271), td_no_actor(wl[-1], 16272)]
def td_middle(wl):        # (c) the no-actor line in the middle, before the cp3 line: not trailing
    wl.insert(10, wl.pop())
def td_at_cp3(wl):        # (c') a trailing no-actor line whose frame is at cp3 (16247), not after it
    wl[-1] = td_no_actor(wl[-1], 16247)
def td_actor(wl):         # (d) the trailing line with a frame jump but WITH actor columns (those of F 16249)
    t, a = wl[-1].split(), wl[-2].split(); t[18:27] = a[18:27]; wl[-1] = " ".join(t)
def td_nocp3(xl):         # no cp3 line in the engine log: nothing dropped
    xl[:] = [l for l in xl if not l.startswith("* det_rng_state cp3:")]
td_case("td_real_probe", "s4d_probe", 0, must=("dropped 1",), mustnot=("ERR", "follows"))
td_case("td_real_probe15", "s4d_probe15", 0, must=("dropped 1",), mustnot=("ERR", "follows"))
td_case("td_mut_old_rule", "s4d_probe", 1, must=("dropped 0", "ERR F 16270 follows F 16249", "> TEARDOWN_MAX 0: none dropped"), scorer=SCN)
td_case("td_mut_old_rule15", "s4d_probe15", 1, must=("dropped 0", "ERR F 16252 follows F 16231"), scorer=SCN)
td_case("td_three_trailing", "s4d_probe", 1, wit_fn=td_three, must=("dropped 0", "ERR F 16270 follows F 16249",
        "ERR 3 trailing no-actor lines after cp3 frame 16247 (F 16270..16272) > TEARDOWN_MAX 2: none dropped"))
td_case("td_middle", "s4d_probe", 1, wit_fn=td_middle, must=("dropped 0", "ERR F 16270 follows F 16230", "ERR F 16231 follows F 16270"))
td_case("td_trailing_at_cp3", "s4d_probe", 1, wit_fn=td_at_cp3, must=("dropped 0", "ERR F 16247 follows F 16249"))
td_case("td_jump_with_actor", "s4d_probe", 1, wit_fn=td_actor, must=("dropped 0", "ERR F 16270 follows F 16249"))
td_case("td_no_cp3", "s4d_probe", 2, log_fn=td_nocp3, must=("dropped 0", "MISSING no det_rng_state cp3 line"))
# the scorer prints the dropped count itself in score mode (the synthetic good set has no teardown line: 0 per run)
run_score("good_teardown_count", GOOD, {}, 0, must=(TDL + "s4p_off2a 0, s4p_off2b 0, ",))



def selfcheck(mode, must):
    """Re-run this same file with the self-check switch: exit 1 and the wrapper's FAIL last line are required."""
    par = os.path.join(ST, "selfcheck_" + mode); os.makedirs(par)
    p = subprocess.run(["bash", WRAP, par], env=dict(os.environ, S4D2_SELFTEST_SELFCHECK=mode), capture_output=True,
                       text=True, errors="replace")
    out = p.stdout + p.stderr
    lines = [l for l in out.splitlines() if l.strip()]
    last = lines[-1] if lines else ""
    ok = p.returncode == 1 and last.startswith("S4D2 SELFTEST: FAIL (self-test exit: python 1, tee 0") and all(m in out for m in must)
    results.append(("selfcheck_" + mode, ok))
    print("%-4s %-22s exit %d (want 1) last line: %s" % ("ok" if ok else "BAD", "selfcheck_" + mode, p.returncode, last[:120]))


selfcheck("crash", ("Traceback", "RuntimeError: deliberate crash"))
selfcheck("fail", ("BAD  selfcheck_forced_bad_case", "S4D2 SELFTEST: FAIL (0/1)"))

finish()
PYEOF
ST_PIPE=("${PIPESTATUS[@]}")   # [0] = the embedded Python, [1] = tee (a pipeline's own status is its LAST command's)
echo "output folder: $ST ($(du -sh "$ST" | cut -f1))"
if [ "${ST_PIPE[0]}" != 0 ] || [ "${ST_PIPE[1]}" != 0 ]; then
    echo "S4D2 SELFTEST: FAIL (self-test exit: python ${ST_PIPE[0]}, tee ${ST_PIPE[1]}; a crash or a failed case, see $ST/selftest_output.txt)" | tee -a "$ST/selftest_output.txt"
    exit 1
fi
exit 0
