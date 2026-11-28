#!/usr/bin/env bash
# s4d1 self-test (PREREG-s4d1-leftovers-build1-DRAFT.md section 8): the scorer s4d1_score.py on SYNTHETIC run folders.
#   nice -n 19 bash s4d1_selftest.sh [<parent folder>]     (default parent: s4d1/selftest-runs) -> <parent>/<stamp>-<pid>/
# LIMIT: there are no real s4d1 run folders yet (the exe is not built). Every fixture here is made by this script from
# the witness format in the code (wt-simtick 7b1232ffc, sim_synth_witness_write: six-value s4) and the log line formats
# of earlier real runs (out/det_s4c_s1). The self-test MUST be re-run with the probe's real bytes (a fixture case built
# on the probe folder) before the prereg is registered (prereg section 8).
# What it does: copies the scorer into the stamp folder twice with the PROBE_ constants filled for the synthetic world
# (and ALIFE_MIN_CALLS 50 -> 3, because the synthetic runs are 120 ticks long, not 5450; the rule itself is unchanged),
# builds one good set of 19 run folders, then one case per check and per must-fail (each mutant must change
# exactly the verdicts listed for it, nothing else), CRLF variants, a missing file, the retry folder, a dropped check,
# the refusals of the registered scorer (placeholders, out exists), and ten mode=probe cases over both probe boots
# (one fixture per registered s4 case, a garbage and a no-item fixture, stop rules). Unchanged run folders of a case are symlinks to
# the good set, unchanged witness files hard links: the output stays at a few MB. Nothing outside the stamp folder is
# written; nothing is deleted.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SC="$HERE/s4d1_score.py"
[ -s "$SC" ] || { echo "S4D1 SELFTEST: FAIL (0/0) scorer $SC missing"; exit 2; }
PARENT="${1:-$HERE/selftest-runs}"; mkdir -p "$PARENT" && PARENT="$(cd "$PARENT" && pwd)" || { echo "S4D1 SELFTEST: FAIL (0/0) parent folder"; exit 2; }
ST="$PARENT/$(date +%Y%m%d-%H%M%S)-$$"
[ -e "$ST" ] && { echo "S4D1 SELFTEST: FAIL (0/0) $ST exists"; exit 2; }
mkdir -p "$ST"
python3 -I - "$ST" "$SC" <<'PYEOF' 2>&1 | tee "$ST/selftest_output.txt"
import importlib.util, os, re, subprocess, sys
ST, SC = sys.argv[1], sys.argv[2]
FILL = {"PROBE_S2_MOVING": 600, "PROBE_S2_LAG": 1, "PROBE_S3_FRAMES": 600, "PROBE_S1_FRAMES": 900,
        "PROBE_ALIFE_GAP16": 1}
src = open(SC).read()

def filled(over):
    s = src
    for k, v in dict(FILL, **over).items():
        old = '"%s": "%s",' % (k, k)
        assert s.count(old) == 1, old
        s = s.replace(old, '"%s": %r,' % (k, v))
    assert s.count("ALIFE_MIN_CALLS = 50 ") == 1
    return s.replace("ALIFE_MIN_CALLS = 50 ", "ALIFE_MIN_CALLS = 3 ")

SCF = os.path.join(ST, "scorer_filled.py"); open(SCF, "w").write(filled({}))
SCD = os.path.join(ST, "scorer_filled_s1drop.py"); open(SCD, "w").write(filled({"PROBE_S1_FRAMES": 100}))
spec_ = importlib.util.spec_from_file_location("s4d1f", SCF); S = importlib.util.module_from_spec(spec_)
spec_.loader.exec_module(S)

F0, H0, TMAX, CPT = 40, 1000.0, 120, {"cp1": 60, "cp2": 85, "cp3": 110}
CALLS = [5, 21, 38, 55]          # A-Life call ticks to cp1: gaps 16,17,17 (one 16)

def tri(u):
    return 4 * u if u < 0.25 else (2 - 4 * u if u < 0.75 else 4 * u - 4)

def X(T, stim):
    if not stim or T < 10:
        return (10.0, 2.0, 5.0)
    return (round(10.0 + 1.5 * tri((((T - 10) * 33) % 2000) / 2000.0), 5), 2.0, 5.0)

def lerp(p, q, a):
    return tuple(round(x + (y - x) * a, 5) for x, y in zip(p, q))

def s4_model(case, bit8, n, j, x, off):
    """(item, draw) of the held item for one frame; B = item rebuilt from the bones, d = the nudge (-0.5, 0, 0);
    off = the body's draw offset (s2 draw - s2 xform). Cases as registered (prereg 2.5)."""
    B = (round(x[0] + 0.3, 5), x[1] + 1.2, x[2])
    sh = lambda v, k: (round(v[0] - 0.5 * k, 5), v[1], v[2])
    dr = lambda v: tuple(round(a + b, 5) for a, b in zip(v, off))
    if case == "noitem":
        return None, None
    if case == "noshadow":
        return B, None
    if case == "weapon":
        it = sh(B, 1 if n else 0); return it, dr(it)
    if case == "adds":
        it = sh(B, 1) if bit8 else sh(B, j + 1); return it, dr(it)
    if case == "bolt":
        it = sh(B, 1 if (n or not bit8) else 0); return it, dr(B)
    if case == "garbage":
        it = sh(B, (1, 2, 4)[j]); return it, dr(it)
    raise ValueError(case)


def records(spec, tmax, s4case="weapon"):
    K, mask, stim, wc, brk = spec["k"], spec["mask"], spec["stim"], spec["wc"], spec["brk"]
    recs, bad = [], []
    for T in range(1, tmax + 1):
        for j in range(K):
            r = {"F": F0 + (T - 1) * K + j, "T": T, "j": j, "K": K, "n": 1 if j == 0 else 0}
            a = j / float(K) if not wc else 0.3 + 0.01 * j + 0.001 * (T % 7)
            r["a"], r["dt"] = a, (S.dt_exp(K) if not wc else 0.014321)
            r["hud"] = H0 + 33 * T + a * 33
            x = X(T, stim)
            cam = lerp(X(T - 2, stim), X(T - 1, stim), a) if T >= 3 else x
            r["s2"] = [cam if mask & 1 else x, x, cam]
            if stim and T >= 11:
                m = T % 8 + 1
                pos, start = (100.0 + 40 * m, 20.0, 50.0), (100.0 + 40 * (m - 1), 20.0, 50.0)
                if brk == 2 and r["n"] == 0:
                    pos = start   # the tick frame's break pulled bullet_pos back to the step's start
                r["s3"] = [1, lerp(start, pos, a) if mask & 2 else pos, pos, start, 1]
            else:
                r["s3"] = 0
            r["s1"] = [S.dt_exp(K) if mask & 4 else (S.f32(0.033) if r["n"] else 0.0), (0.1, 0.2, 0.3), 0.5]
            off = tuple(round(p - q, 5) for p, q in zip(r["s2"][0], x))
            r["s4"], r["s4d"] = s4_model(s4case, bool(mask & 8), r["n"], j, x, off)
            r["tw"] = 0
            if brk == 1 and r["n"] == 0 and r["s2"][0] != x:
                bad.append(("s2", r["F"])); r["tw"] += 1
            if brk == 2 and r["n"] == 1 and r["s3"]:
                bad.append(("s3", r["F"])); r["tw"] += 1
            recs.append(r)
    return recs, bad

def v3(v):
    return "%.5f %.5f %.5f" % tuple(v)

def fmt(r):
    s = "F %d T %d j %d K %d n %d a %.6f dt %.6f hud %.3f |" % (r["F"], r["T"], r["j"], r["K"], r["n"], r["a"], r["dt"], r["hud"])
    s += " s2 %s %s %s |" % tuple(v3(v) for v in r["s2"]) if r["s2"] else " s2" + " -" * 9 + " |"
    if r["s3"] is None:
        s += " s3" + " -" * 11 + " |"
    elif r["s3"] == 0:
        s += " s3 0" + " -" * 10 + " |"
    else:
        q = r["s3"]; s += " s3 %d %s %s %s %d |" % (q[0], v3(q[1]), v3(q[2]), v3(q[3]), q[4])
    s += " s1 %.6f %s %.6f |" % (r["s1"][0], v3(r["s1"][1]), r["s1"][2]) if r["s1"] else " s1" + " -" * 5 + " |"
    if r["s4"] is None:
        s += " s4 - - - - - - |"
    elif r.get("s4d") is None:
        s += " s4 %s - - - |" % v3(r["s4"])
    else:
        s += " s4 %s %s |" % (v3(r["s4"]), v3(r["s4d"]))
    return s + " tw %d" % r["tw"]

def write(path, lines, crlf):
    with open(path, "wb") as f:
        f.write(("\r\n" if crlf else "\n").join(lines).encode() + (b"\r\n" if crlf else b"\n"))

def make_run(root, tag, spec, rtag=None, tmax=TMAX, crlf_log=True, crlf_wit=False, rec_fn=None, link_wit=None,
             cmd_drop=None, counter_change=None, cp_shift=0, sched_extra=False, syn_line=False, no_stim=False,
             bad_extra=(), no_bad=False, no_witness=False, s4case="weapon", raw_fn=None, drop_bad=None):
    rtag = rtag or tag
    d = os.path.join(root, "det_" + rtag); os.makedirs(d)
    K, lever = spec["k"], spec["k"] > 0
    ext = "-det_passes 100,200,320 -det_lua_streams -det_counters"
    if lever:
        ext += " -det_synth_k %d -sim_4d_mask %d" % (K, spec["mask"])
        ext += " -det_synth_wallclock" * spec["wc"] + " -det_synth_tw" * spec["tw"]
        ext += (" -sim_4d_break %d" % spec["brk"]) if spec["brk"] else ""
        ext += " -det_synth_walk -det_synth_tracer" * spec["stim"]
    cmd = "command line z:\\home\\caden\\projects\\stalkergameengine\\run\\bin\\%s -nointro -dbg -start server(bench1/single/alife/load) client(localhost) -det_mode -det_tag %s -det_interval 60 -det_checkpoints 3 %s" % (spec["exe"], rtag, ext)
    if cmd_drop:
        cmd = cmd.replace(" " + cmd_drop, "")
    L = ["* xray log (synthetic, s4d1 self-test)", cmd]
    cpf = lambda T: (F0 + (T - 1) * K) if lever else 32 + T
    suf = lambda T: (" tick=%d" % T) if lever else ""
    if lever or syn_line:
        L.append("* det_synth: k %d wallclock %d tw %d break %d mask %d" % (K, spec["wc"], spec["tw"], spec["brk"], spec["mask"] or 0))
    if lever:
        L.append("* det_synth: K %d, witness z:\\run\\appdata\\logs\\synth_%s.txt" % (K, rtag))
    L.append("* det_rng_state pre: Random=1685459848 Random32=0 alife=302455169 crt_next=22912 frame=32 time_ms=99" + (" tick=0" if lever else ""))
    for cp, T in CPT.items():
        L.append("* det_rng_state %s: Random=-1528111566 Random32=0 alife=302455169 crt_next=6334 frame=%d time_ms=%d%s" % (cp, cpf(T) + cp_shift, 99 + 33 * T, suf(T)))
        L.append("* det_lua_state %s: 815687002" % cp)
    if spec["stim"] and not no_stim:
        L += ["* det_synth stimulus: installed (walk true, tracer true)",
              "* det_synth stimulus: start at update 300 (10.000 2.000 5.000) time 9999 walk true tracer true ammo ammo_5.45x39_fmj",
              "* det_synth stimulus: 1 bullets fired (update 300)", "* det_synth stimulus: 50 bullets fired (update 1770)",
              "* det_synth stimulus: 150 bullets fired (update 4770)"]
    if lever:
        recs, bad = records(spec, tmax, s4case)
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
                lines = [fmt(r) for r in recs]
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

def case_root(name, changes):
    """changes: {tag: kwargs for make_run}; every other run folder is a symlink to the good set."""
    root = os.path.join(ST, "cases", name, "runs"); os.makedirs(root)
    for t, sp in S.RUNS.items():
        if t in changes:
            kw = dict(changes[t])
            if not set(kw) & {"rec_fn", "crlf_wit", "no_witness", "no_bad", "bad_extra", "raw_fn", "drop_bad"} and sp["k"]:
                kw["link_wit"] = os.path.join(GOOD, "det_" + t, "synth_%s.txt" % t)
            make_run(root, t, sp, **kw)
        else:
            os.symlink(os.path.join(GOOD, "det_" + t), os.path.join(root, "det_" + t))
    return root

IDS = ["F0", "A1", "A2", "A3", "A4", "A5", "L1", "L1W", "L2", "L3", "S0", "S0B", "SC", "TW", "TWB1", "TWB2", "S2", "S3", "S1"]
SPART = [i for i in IDS if i.startswith("S") or i.startswith("TW")]
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
    print("%-4s %-22s exit %d (want %d) %s%s" % ("ok" if ok else "BAD", name, p.returncode, code,
          "" if not diff else "verdicts differ %s " % diff, "" if all(s in txt for s in must) else "missing text %s" % [s for s in must if s not in txt]))
    if not ok:
        print("     " + "\n     ".join(txt.split("\n")[-26:]))

def recmod(fn):
    return fn

# ---- good sets (the CRLF variant flips the line ends of five runs: witness CRLF, engine log LF)
run_score("good", GOOD, {}, 0)
run_score("good_crlf", case_root("good_crlf", {t: {"crlf_log": False, "crlf_wit": True} for t in
          ("s4d_o1", "s4d_l2a", "s4d_on2a", "s4d_on3", "s4d_brk1")}), {}, 0)

# ---- one mutant per check
def first(recs, cond):
    return next(r for r in recs if cond(r))

def m_l1(recs):
    r = first(recs, lambda r: r["n"] == 0 and r["T"] == 50); r["T"] += 1
def m_l1w(recs):
    for r in recs:
        r["a"], r["dt"] = r["j"] / 2.0, S.dt_exp(2)
def m_l2(recs):
    for r in recs:
        r["hud"] += 33
def m_tw(recs):
    first(recs, lambda r: r["F"] == K2F(70, 1))["tw"] = 1
def m_twc(recs):
    first(recs, lambda r: r["n"] == 0 and r["T"] == 70)["tw"] = 1
def m_twb2(recs):
    for r in recs:
        if r["n"] == 0 and r["T"] in (60, 61):
            r["tw"] += 1
def m_s2(recs):
    r = first(recs, lambda r: r["n"] == 0 and r["T"] == 40); d = r["s2"][0]; r["s2"][0] = (d[0] + 0.01, d[1], d[2])
def m_s3(recs):
    r = first(recs, lambda r: r["n"] == 0 and r["s3"]); q = r["s3"]; q[1] = lerp(q[3], q[2], 1.2)
def m_s1(recs):
    first(recs, lambda r: r["n"] == 0 and r["T"] == 33)["s1"][0] = S.f32(0.033)
def m_s4_bad(lines):   # draw values without item values: not one of the three forms the code prints
    i = 37; t = lines[i].split(); t[49:52] = ["-", "-", "-"]; lines[i] = " ".join(t)
def m_twb2_miss(recs):  # one frame whose tracer the break moved has no BAD line (witness tw and log both without it)
    r = first(recs, lambda r: r["n"] == 1 and r["T"] == 70); r["tw"] -= 1

K2F = lambda T, j: F0 + (T - 1) * 2 + j
CASES = [
    ("m_f0", {"s4d_l3": {"cmd_drop": "-det_counters"}}, {"F0": "FAIL"}, 1),
    ("m_a1", {"s4d_s2": {"counter_change": 2}}, {"A1": "FAIL", "A2": "NO VERDICT", "A3": "NO VERDICT", "A4": "NO VERDICT"}, 3),
    ("m_a2", {"s4d_o2": {"counter_change": 3}}, {"A2": "FAIL"}, 1),
    ("m_a3", {t: {"cp_shift": 20} for t in ("s4d_o1", "s4d_o2", "s4d_o3")}, {"A3": "FAIL"}, 1),
    ("m_a4", {"s4d_o1": {"sched_extra": True}}, {"A4": "FAIL"}, 1),
    ("m_a5", {"s4d_o2": {"syn_line": True}}, {"A5": "FAIL"}, 1),
    ("m_l1", {"s4d_l3": {"rec_fn": m_l1}}, {"L1": "FAIL"}, 1),
    ("m_l1w_mustfail_did_not_fail", {"s4d_l2w": {"rec_fn": m_l1w}}, {"L1W": "FAIL"}, 1),
    ("m_l2", {"s4d_l2b": {"rec_fn": m_l2}}, {"L2": "FAIL"}, 1),
    ("m_l3", {"s4d_l2b": {"counter_change": 1}}, dict({"L3": "FAIL"}, **{i: "NO VERDICT" for i in SPART}), 3),
    ("m_s0", {"s4d_off2b": {"counter_change": 2}}, {"S0": "FAIL", "SC": "NO VERDICT"}, 3),
    ("m_s0b", {"s4d_on2a": {"no_stim": True}}, {"S0B": "FAIL"}, 1),
    ("m_sc", {"s4d_on3": {"counter_change": 3}}, {"SC": "FAIL"}, 1),
    ("m_tw_bad_line", {"s4d_on2b": {"rec_fn": m_tw, "bad_extra": [("s3", K2F(70, 1))]}}, {"TW": "FAIL"}, 1),
    ("m_tw_consistency", {"s4d_off3": {"rec_fn": m_twc}}, {"TW": "FAIL"}, 1),
    ("m_twb1_mustfail_did_not_fail", {"s4d_brk1": {"no_bad": True}}, {"TWB1": "FAIL"}, 1),
    ("m_twb2_wrong_slice", {"s4d_brk2": {"rec_fn": m_twb2, "bad_extra": [("s2", K2F(60, 1)), ("s2", K2F(61, 1))]}}, {"TWB2": "FAIL"}, 1),
    ("m_s2", {"s4d_on2a": {"rec_fn": m_s2}}, {"S2": "FAIL"}, 1),
    ("m_s3", {"s4d_on3": {"rec_fn": m_s3}}, {"S3": "FAIL"}, 1),
    ("m_s1", {"s4d_on2b": {"rec_fn": m_s1}}, {"S1": "FAIL"}, 1),
    ("m_twb2_mustfail_did_not_fail", {"s4d_brk2": {"no_bad": True}}, {"TWB2": "FAIL"}, 1),
    ("m_twb2_frame_not_caught", {"s4d_brk2": {"rec_fn": m_twb2_miss, "drop_bad": ("s3", K2F(70, 0))}}, {"TWB2": "FAIL"}, 1),
    ("m_s3_crlf", {"s4d_on3": {"rec_fn": m_s3, "crlf_wit": True, "crlf_log": False}}, {"S3": "FAIL"}, 1),
]
for name, ch, exp, code in CASES:
    run_score(name, case_root(name, ch), exp, code)

# missing witness of one run: every check that reads it FAILS by name
run_score("m_missing_witness", case_root("m_missing_witness", {"s4d_on3": {"no_witness": True}}),
          {i: "FAIL" for i in ("S0B", "TW", "S2", "S3", "S1")}, 1, must=("missing file", "synth_s4d_on3.txt"))
# a malformed s4 column in one run: every check that reads that witness FAILS by name
run_score("m_s4_malformed", case_root("m_s4_malformed", {"s4d_off3": {"raw_fn": m_s4_bad}}),
          {i: "FAIL" for i in ("S0B", "TW", "S2", "S3", "S1")}, 1, must=("malformed witness", "synth_s4d_off3.txt line 38"))
# the series' one retry: det_<tag>_r1 is read and named; the first folder is broken
root = case_root("retry_r1", {})
os.unlink(os.path.join(root, "det_s4d_on2a")); os.makedirs(os.path.join(root, "det_s4d_on2a"))
make_run(root, "s4d_on2a", S.RUNS["s4d_on2a"], rtag="s4d_on2a_r1")
run_score("retry_r1", root, {}, 0, must=("substitution: s4d_on2a read from",))
# a dropped check (probe count under DROP_MIN): DROPPED, not counted, result PASS
run_score("drop_s1", GOOD, {"S1": "DROPPED"}, 0, scorer=SCD, must=("Caden's eyes only",))

# refusals of the REGISTERED scorer
def refuse(name, args, must):
    p = subprocess.run([sys.executable, "-I", SC] + args, capture_output=True, text=True)
    ok = p.returncode == 2 and must in p.stdout
    results.append((name, ok)); print("%-4s %-22s exit %d (want 2) %s" % ("ok" if ok else "BAD", name, p.returncode, p.stdout.strip()[-120:]))
refuse("refuse_placeholders", ["root=" + GOOD, "out=" + os.path.join(ST, "refused.txt")], "REFUSED (placeholders")
open(os.path.join(ST, "exists.txt"), "w").write("x\n")
p = subprocess.run([sys.executable, "-I", SCF, "root=" + GOOD, "out=" + os.path.join(ST, "exists.txt")], capture_output=True, text=True)
ok = p.returncode == 2 and "exists" in p.stdout; results.append(("refuse_out_exists", ok))
print("%-4s %-22s exit %d (want 2)" % ("ok" if ok else "BAD", "refuse_out_exists", p.returncode))

# ---- probe mode: both probe boots (mask 7 and mask 15) per case
PR = os.path.join(ST, "probe")
def probe_case(name, want_code, must=(), s4case="weapon", skip15=False, **kw):
    root = os.path.join(PR, name); os.makedirs(root)
    for tag, sp in (("s4d_probe", S.PROBE_RUN), ("s4d_probe15", S.PROBE15_RUN)):
        if tag == "s4d_probe15" and skip15:
            continue
        make_run(root, tag, sp, tmax=175, s4case=s4case, **kw)
    out = os.path.join(root, "probe.txt")
    p = subprocess.run([sys.executable, "-I", SCF, "mode=probe", "root=" + root, "out=" + out], capture_output=True, text=True)
    txt = open(out).read() if os.path.exists(out) else p.stdout
    ok = p.returncode == want_code and all(s in txt for s in must)
    results.append((name, ok))
    print("%-4s %-22s exit %d (want %d) %s" % ("ok" if ok else "BAD", name, p.returncode, want_code,
          " | ".join(l for l in txt.split("\n") if "STOP" in l or "S4 CASE" in l or "PROBE: GO" in l)[:300]))
    if not ok:
        print("     " + "\n     ".join(txt.split("\n")))
def p_nos1(recs):
    for r in recs:
        r["s1"] = None
CASE = "S4 CASE (descriptive, never gated): "
probe_case("probe_case_weapon", 0, must=(CASE + "weapon, desk prediction", '"PROBE_S2_LAG": 1,', '"PROBE_ALIFE_GAP16": 1,'))
probe_case("probe_case_adds_up", 0, must=(CASE + "nudge adds up",), s4case="adds")
probe_case("probe_case_bolt", 0, must=(CASE + "bolt active",), s4case="bolt")
probe_case("probe_case_no_shadow", 0, must=(CASE + "no shadow pass",), s4case="noshadow")
probe_case("probe_case_garbage", 0, must=(CASE + "none of the registered cases",), s4case="garbage")
probe_case("probe_case_no_item", 0, must=(CASE + "none of the registered cases", "item step no item"), s4case="noitem")
probe_case("probe_crlf", 0, must=(CASE + "weapon, desk prediction",), crlf_log=False, crlf_wit=True)
probe_case("probe_no_stimulus", 1, must=("PROBE STOP stimulus",), no_stim=True)
probe_case("probe_no_crosshair", 0, must=("S1 DROPPED: Caden's eyes only",), rec_fn=p_nos1)
probe_case("probe_missing_probe15", 1, must=("PROBE STOP input: s4d_probe15", CASE + "none of the registered cases"), skip15=True)

n_ok = sum(1 for _, ok in results if ok)
print("S4D1 SELFTEST: %s (%d/%d)" % ("PASS" if n_ok == len(results) else "FAIL", n_ok, len(results)))
PYEOF
echo "output folder: $ST ($(du -sh "$ST" | cut -f1))"
