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
# the refusals of the registered scorer (placeholders, out exists), and fourteen mode=probe cases over both probe boots
# (one fixture per registered s4 case, a garbage and a no-item fixture, stop rules, and since prereg Amendment 1 the
# rule "item": item NONE, item line missing, s4 share 0.40 and 0.60). Prereg Amendment 1 (2026-11-28): every stimulus
# run of the fixtures carries -det_synth_item, the script's item line (default "slot 3 section wpn_ppsh41") and, where
# the s4 case models an item, s4 on every line (s4_model). Unchanged run folders of a case are symlinks to
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
             bad_extra=(), no_bad=False, no_witness=False, s4case="weapon", raw_fn=None, drop_bad=None,
             item="slot 3 section wpn_ppsh41", cmd_add=None):
    rtag = rtag or tag
    d = os.path.join(root, "det_" + rtag); os.makedirs(d)
    K, lever = spec["k"], spec["k"] > 0
    ext = "-det_passes 100,200,320 -det_lua_streams -det_counters"
    if lever:
        ext += " -det_synth_k %d -sim_4d_mask %d" % (K, spec["mask"])
        ext += " -det_synth_wallclock" * spec["wc"] + " -det_synth_tw" * spec["tw"]
        ext += (" -sim_4d_break %d" % spec["brk"]) if spec["brk"] else ""
        ext += " -det_synth_walk -det_synth_tracer -det_synth_item" * spec["stim"]   # prereg Amendment 1
    cmd = "command line z:\\home\\caden\\projects\\stalkergameengine\\run\\bin\\%s -nointro -dbg -start server(bench1/single/alife/load) client(localhost) -det_mode -det_tag %s -det_interval 60 -det_checkpoints 3 %s" % (spec["exe"], rtag, ext)
    if cmd_drop:
        cmd = cmd.replace(" " + cmd_drop, "")
    if cmd_add:
        cmd += " " + cmd_add
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
        # line texts of zz_eperf_synth.script:66, :78, :94, :102. `time` = the hud of the tick frame of T = 10, where X()
        # starts the walk (H0 + 33 x 10), as on the real probes (time 9966 = hud of F 815, the first moving tick frame)
        L += ["* det_synth stimulus: installed (walk true, tracer true, item %s)" % ("true" if spec["stim"] else "false"),
              "* det_synth stimulus: start at update 300 (10.000 2.000 5.000) time %d walk true tracer true ammo ammo_5.45x39_fmj" % int(H0 + 33 * 10)]
        if item is not None:
            L.append("* det_synth stimulus: item " + ("NONE (no filled slot)" if item == "NONE" else item))
        L += ["* det_synth stimulus: 1 bullets fired (update 300)", "* det_synth stimulus: 50 bullets fired (update 1770)",
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

# ---- prereg Amendment 1 in score mode: the item switch in F0 (by name, both ways), the item line in S0B, the report
run_score("m_f0_item_switch_missing", case_root("m_f0_item_switch_missing", {"s4d_on2a": {"cmd_drop": "-det_synth_item"}}),
          {"F0": "FAIL"}, 1, must=("s4d_on2a: missing switch -det_synth_item",))
run_score("m_f0_item_switch_in_L", case_root("m_f0_item_switch_in_L", {"s4d_l3": {"cmd_add": "-det_synth_item"}}),
          {"F0": "FAIL"}, 1, must=("s4d_l3: unregistered switch -det_synth_item",))
run_score("m_s0b_item_none", case_root("m_s0b_item_none", {"s4d_off3": {"item": "NONE"}}),
          {"S0B": "FAIL"}, 1, must=("s4d_off3: item NONE (no filled slot)", "reported: item taken s4d_off3: item NONE"))
run_score("m_s0b_item_line_missing", case_root("m_s0b_item_line_missing", {"s4d_brk1": {"item": None}}),
          {"S0B": "FAIL"}, 1, must=("s4d_brk1: no 'det_synth stimulus: item' line",))
run_score("good_item_reported", GOOD, {}, 0, must=("reported: item taken s4d_on2a: item slot 3 section wpn_ppsh41",
          "reported: item taken s4d_brk2: item slot 3 section wpn_ppsh41", "item taken, >= 50 bullets"))
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
          " | ".join(l for l in txt.split("\n") if "STOP" in l or "S4 CASE" in l or "PROBE: GO" in l
                     or (l.startswith("item s4d_probe:") and "share" in l))[:400]))
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
# Amendment 1 changed this expectation (exit 0 -> 1, plus the "item" stop text): the no-item fixture has s4 dashes on
# every line, so its s4 share from the stimulus start to cp3 is 0.00 < ITEM_SHARE 0.50 on both probes, which the
# amended prereg (2.2 rule "item", part ii) makes a PROBE STOP. Its item log line is the default (taken), so only part
# (ii) fires. The s4 description it checks is unchanged (descriptive, never gated).
probe_case("probe_case_no_item", 1, must=(CASE + "none of the registered cases", "item step no item",
           "PROBE STOP item: s4d_probe: s4 share 0.0000", "PROBE STOP item: s4d_probe15: s4 share 0.0000"), s4case="noitem")
probe_case("probe_crlf", 0, must=(CASE + "weapon, desk prediction",), crlf_log=False, crlf_wit=True)
probe_case("probe_no_stimulus", 1, must=("PROBE STOP stimulus",), no_stim=True)
probe_case("probe_no_crosshair", 0, must=("S1 DROPPED: Caden's eyes only",), rec_fn=p_nos1)
probe_case("probe_missing_probe15", 1, must=("PROBE STOP input: s4d_probe15", CASE + "none of the registered cases"), skip15=True)
# ---- prereg Amendment 1, probe rule "item" (both boots): (i) item line present and not NONE, (ii) s4 share >= 0.50
def p_share(keep):   # s4 kept on `keep` of every 10 window lines (stimulus start F 67 = hud 1330 to cp3 F 367: 301 lines)
    def f(recs):
        w = [r for r in recs if r["hud"] >= H0 + 33 * 10 - 1e-3 and r["F"] <= F0 + (CPT["cp3"] - 1) * 3]
        for i, r in enumerate(w):
            if i % 10 >= keep:
                r["s4"] = r["s4d"] = None
    return f
probe_case("probe_item_none", 1, must=("PROBE STOP item: s4d_probe: item NONE (no filled slot)",
           "PROBE STOP item: s4d_probe15: item NONE (no filled slot)", "share 1.0000"), item="NONE")
probe_case("probe_item_line_missing", 1, must=("PROBE STOP item: s4d_probe: no 'det_synth stimulus: item' line",
           "PROBE STOP item: s4d_probe15: no 'det_synth stimulus: item' line"), item=None)
probe_case("probe_item_share_040", 1, must=("PROBE STOP item: s4d_probe: s4 share 0.4020 (121 of 301 lines F 67..367) < 0.50",
           "PROBE STOP item: s4d_probe15: s4 share 0.4020"), rec_fn=p_share(4))
probe_case("probe_item_share_060", 0, must=("item s4d_probe: item slot 3 section wpn_ppsh41 (installed line: item true); s4 on 181 of 301 witness lines F 67..367",
           "share 0.6013 (rule >= 0.50)", "S4D1 PROBE: GO"), rec_fn=p_share(6))

TD_SPEC = {"s4d_probe": S.PROBE_RUN, "s4d_probe15": S.PROBE15_RUN}
# Prereg Amendment 1: these real tails were cut from boots WITHOUT -det_synth_item (and without its log line), so in
# mode=probe they also STOP by "flags" and "item"; the td_* cases test only the teardown / pattern rule (td_case runs
# pattern_errors alone; td_probe reads only the "PROBE STOP pattern" lines and the dropped count, exit 1 either way).
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
# mode=probe on both real tails: the scorer prints the dropped count itself. A 30-line tail STOPs other probe rules
# (walk, A-Life input, ...), so the exit is 1 either way; the PROBE STOP pattern lines are what is read here.
def td_probe(name, scorer, must, mustnot=()):
    root = td_folder(name, "s4d_probe"); td_folder(name, "s4d_probe15")
    out = os.path.join(root, "probe.txt")
    p = subprocess.run([sys.executable, "-I", scorer, "mode=probe", "root=" + root, "out=" + out], capture_output=True, text=True)
    txt = open(out).read() if os.path.exists(out) else p.stdout
    pat = [l for l in txt.split("\n") if l.startswith("PROBE STOP pattern")]
    ok = p.returncode == 1 and all(s in txt for s in must) and not any(s in l for l in pat for s in mustnot)
    results.append((name, ok))
    print("%-4s %-22s exit %d (want 1) %s" % ("ok" if ok else "BAD", name, p.returncode,
          " | ".join(l for l in txt.split("\n") if l.startswith(TDL) or l.startswith("PROBE STOP pattern"))[:400]))
    if not ok:
        print("     " + "\n     ".join(txt.split("\n")))
td_probe("td_probe_mode_count", SCF, (TDL + "s4d_probe 1, s4d_probe15 1",), ("follows", "trailing"))
td_probe("td_probe_mode_old_rule", SCN, (TDL.replace("at most 2", "at most 0") + "s4d_probe 0, s4d_probe15 0", "PROBE STOP pattern: s4d_probe: F 16270 follows F 16249",
         "PROBE STOP pattern: s4d_probe15: F 16252 follows F 16231"))

n_ok = sum(1 for _, ok in results if ok)
print("S4D1 SELFTEST: %s (%d/%d)" % ("PASS" if n_ok == len(results) else "FAIL", n_ok, len(results)))
PYEOF
echo "output folder: $ST ($(du -sh "$ST" | cut -f1))"
