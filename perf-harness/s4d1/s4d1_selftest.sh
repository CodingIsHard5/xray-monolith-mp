#!/usr/bin/env bash
# s4d1 self-test (PREREG-s4d1-leftovers-build1-2026-11-28.md section 8): the scorer s4d1_score.py on SYNTHETIC run folders.
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
# 2026-11-28 (after the GO re-probe, prereg 2.5): the scorer's CONST is filled with the probe's readings; filled() now
# replaces whatever value each CONST key has (placeholder or number) and refuse_placeholders puts a placeholder back in
# a copy. New real-bytes cases (REAL2) from the GO probes, which ran WITH -det_synth_item: teardown on the real item
# tails, rule "item" in mode=probe on real cuts (around the stimulus start F 815 + the tail) and mutants of them.
# Exit status: 0 only when every case passed; any failed case or any crash of the embedded Python gives exit 1 and the
# last line "S4D1 SELFTEST: FAIL (self-test exit: ...)". The cases selfcheck_crash / selfcheck_fail re-run THIS wrapper
# with S4D1_SELFTEST_SELFCHECK=crash|fail and require that exit and last line.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SC="$HERE/s4d1_score.py"
[ -s "$SC" ] || { echo "S4D1 SELFTEST: FAIL (0/0) scorer $SC missing"; exit 2; }
PARENT="${1:-$HERE/selftest-runs}"; mkdir -p "$PARENT" && PARENT="$(cd "$PARENT" && pwd)" || { echo "S4D1 SELFTEST: FAIL (0/0) parent folder"; exit 2; }
ST="$PARENT/$(date +%Y%m%d-%H%M%S)-$$"
[ -e "$ST" ] && { echo "S4D1 SELFTEST: FAIL (0/0) $ST exists"; exit 2; }
mkdir -p "$ST"
python3 -I - "$ST" "$SC" "$HERE/$(basename "$0")" <<'PYEOF' 2>&1 | tee "$ST/selftest_output.txt"
import importlib.util, os, re, subprocess, sys
ST, SC, WRAP = sys.argv[1], sys.argv[2], sys.argv[3]
results = []

def finish():
    """the one summary line and the exit status of the embedded Python: 0 only when every case passed."""
    n_ok = sum(1 for _, ok in results if ok)
    good = bool(results) and n_ok == len(results)
    print("S4D1 SELFTEST: %s (%d/%d)" % ("PASS" if good else "FAIL", n_ok, len(results)))
    sys.stdout.flush()
    sys.exit(0 if good else 1)

# self-check of the exit path (cases selfcheck_crash / selfcheck_fail below re-run this wrapper with this variable)
SELFCHECK = os.environ.get("S4D1_SELFTEST_SELFCHECK", "")
if SELFCHECK == "crash":
    raise RuntimeError("deliberate crash (S4D1_SELFTEST_SELFCHECK=crash)")
if SELFCHECK == "fail":
    results.append(("selfcheck_forced_bad_case", False)); print("BAD  selfcheck_forced_bad_case (S4D1_SELFTEST_SELFCHECK=fail)")
    finish()
assert SELFCHECK == "", SELFCHECK

FILL = {"PROBE_S2_MOVING": 600, "PROBE_S2_LAG": 1, "PROBE_S3_FRAMES": 600, "PROBE_S1_FRAMES": 900,
        "PROBE_ALIFE_GAP16": 1}
src = open(SC).read()

def set_const(s, k, lit):
    """the CONST entry of key k, whatever its value now (the placeholder string "k" or an integer), set to the Python
    literal lit; exactly one match per key."""
    s2, n = re.subn(r'"%s": (?:"%s"|-?[0-9]+),' % (k, k), lambda m: '"%s": %s,' % (k, lit), s)
    assert n == 1, (k, n)
    return s2

def filled(over):
    s = src
    for k, v in dict(FILL, **over).items():
        s = set_const(s, k, repr(v))
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

# refusals of the REGISTERED scorer. Since the registered scorer's CONST is filled (prereg 2.5), the placeholder refusal
# runs on a copy of the registered file with placeholder strings put back (set_const, one match per key); every other
# byte is the registered scorer's.
def refuse(name, args, must, scorer=SC):
    p = subprocess.run([sys.executable, "-I", scorer] + args, capture_output=True, text=True)
    ok = p.returncode == 2 and must in p.stdout
    results.append((name, ok)); print("%-4s %-22s exit %d (want 2) %s" % ("ok" if ok else "BAD", name, p.returncode, p.stdout.strip()[-120:]))
SCP = os.path.join(ST, "scorer_registered_one_placeholder.py")
open(SCP, "w").write(set_const(src, "PROBE_S2_LAG", '"PROBE_S2_LAG"'))
refuse("refuse_placeholders", ["root=" + GOOD, "out=" + os.path.join(ST, "refused.txt")], "REFUSED (placeholders", scorer=SCP)
s_ = src
for k_ in FILL:
    s_ = set_const(s_, k_, '"%s"' % k_)
SCPA = os.path.join(ST, "scorer_registered_all_placeholders.py"); open(SCPA, "w").write(s_)
refuse("refuse_placeholders_all", ["root=" + GOOD, "out=" + os.path.join(ST, "refused_all.txt")],
       "REFUSED (placeholders not filled: %s)" % ", ".join(FILL), scorer=SCPA)
# the registered scorer itself: CONST holds integers equal to the probe reading's "CONST values to register" lines
# (s4d1/probe_readings.txt, the one scoring run of the GO probes), so it does not refuse by placeholders
sp_ = importlib.util.spec_from_file_location("s4d1reg", SC); R_ = importlib.util.module_from_spec(sp_); sp_.loader.exec_module(R_)
pr_ = open(os.path.join(os.path.dirname(SC), "probe_readings.txt")).read().replace("\r", "")
want_ = {m.group(1): int(m.group(2)) for m in re.finditer(r'^    "(PROBE_[A-Z0-9_]+)": (-?[0-9]+),$', pr_, re.M)}
ok = "S4D1 PROBE: GO" in pr_ and set(want_) == set(FILL) and R_.CONST == want_ and all(type(v) is int for v in R_.CONST.values())
results.append(("registered_const_filled", ok))
print("%-4s %-22s scorer CONST %s; probe_readings.txt %s" % ("ok" if ok else "BAD", "registered_const_filled", R_.CONST, want_))
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

def td_folder(name, tag, wit_fn=None, log_fn=None, src=None):
    """src: (witness lines, log lines, witness sha256, log sha256) of real bytes; default the item-less REAL tails."""
    src = src or REAL[tag]
    wl, xl = list(src[0]), list(src[1])
    if wit_fn:
        wit_fn(wl)
    if log_fn:
        log_fn(xl)
    d = os.path.join(TD, name, "det_" + tag); os.makedirs(d)
    write(os.path.join(d, "synth_%s.txt" % tag), wl, False)     # LF, as the real witness
    write(os.path.join(d, "xray.log"), xl, True)                # CRLF, as the real engine log
    if not wit_fn and not log_fn:
        assert hashlib.sha256(open(os.path.join(d, "synth_%s.txt" % tag), "rb").read()).hexdigest() == src[2]
        assert hashlib.sha256(open(os.path.join(d, "xray.log"), "rb").read()).hexdigest() == src[3]
    return os.path.join(TD, name)

def td_case(name, tag, want_code, must=(), mustnot=(), wit_fn=None, log_fn=None, scorer=SCF, src=None):
    td_folder(name, tag, wit_fn, log_fn, src)
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

# ---- REAL2 bytes (cut 2026-11-28 from the GO probes out/det_s4d_probe, out/det_s4d_probe15, booted WITH
# -det_synth_item; read only, cut by a script from the files' bytes, not retyped). Per tag: "tail" = the last 30 witness
# lines (LF; the last one is the teardown line, F after cp3, s2 dashes), "head" = witness lines F 809..821 around the
# stimulus start frame F 815 (the first line with hud >= the start line's time 9966; s4 dashes up to F 814, the item
# from F 815 on), "log" = the xray.log lines (CRLF) command line, det_synth lines, installed, start, the real item line,
# bullets fired, cp1-cp3, lua cp3, eperf dump 3 and the three lines after it ('- Disconnect' and the one after).
# Each sha256 is of the cut's bytes in the real file (log: the selected lines, each with its CRLF).
REAL2 = {
    's4d_probe': {
        'tail': ([
            'F 16191 T 5368 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179102.000 | s2 206.62025 0.02667 88.42514 206.98122 0.02846 88.42514 206.62025 0.02667 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 0.00377 -0.11458 5.20997 1.000000 | s4 206.57204 1.60167 88.61200 206.21107 1.59988 88.61199 | tw 0',
            'F 16192 T 5368 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179113.000 | s2 206.80075 0.02757 88.42514 206.98122 0.02846 88.42514 206.80075 0.02757 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 -0.06173 -0.00471 5.38029 1.000000 | s4 206.57204 1.60167 88.61200 206.39157 1.60077 88.61199 | tw 0',
            'F 16193 T 5369 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179124.000 | s2 206.98122 0.02846 88.42514 206.44028 0.02807 88.42514 206.98122 0.02846 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 -0.06546 -0.00019 5.38055 1.000000 | s4 206.88435 1.60097 88.52757 207.42531 1.60136 88.52756 | tw 0',
            'F 16194 T 5369 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179135.000 | s2 206.80090 0.02833 88.42514 206.44028 0.02807 88.42514 206.80090 0.02833 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 -0.00003 -0.11152 5.21066 1.000000 | s4 206.03110 1.60097 88.61218 206.39172 1.60123 88.61218 | tw 0',
            'F 16195 T 5369 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179146.000 | s2 206.62061 0.02820 88.42514 206.44028 0.02807 88.42514 206.62061 0.02820 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 0.06538 -0.22282 5.04078 1.000000 | s4 206.03110 1.60097 88.61218 206.21143 1.60110 88.61218 | tw 0',
            'F 16196 T 5370 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179157.000 | s2 206.44028 0.02807 88.42514 207.07063 0.02765 88.42514 206.44028 0.02807 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 0.06591 -0.22464 5.03512 1.000000 | s4 206.70422 1.60029 88.52773 206.07385 1.60072 88.52773 | tw 0',
            'F 16197 T 5370 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179168.000 | s2 206.65039 0.02793 88.42514 207.07063 0.02765 88.42514 206.65039 0.02793 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 -0.01034 -0.09433 5.23299 1.000000 | s4 206.66147 1.60029 88.61235 206.24121 1.60058 88.61235 | tw 0',
            'F 16198 T 5370 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179179.000 | s2 206.86050 0.02779 88.42514 207.07063 0.02765 88.42514 206.86050 0.02779 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 -0.08658 0.03600 5.43090 1.000000 | s4 206.66147 1.60029 88.61235 206.45132 1.60043 88.61235 | tw 0',
            'F 16199 T 5371 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179190.000 | s2 207.07063 0.02765 88.42514 206.44501 0.03514 88.42514 207.07063 0.02765 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 -0.07489 0.01466 5.39019 1.000000 | s4 206.94411 1.60759 88.52785 207.56973 1.60010 88.52785 | tw 0',
            'F 16200 T 5371 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179201.000 | s2 206.86208 0.03015 88.42514 206.44501 0.03514 88.42514 206.86208 0.03015 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 0.00077 -0.11953 5.19444 1.000000 | s4 206.03583 1.60759 88.61246 206.45290 1.60260 88.61246 | tw 0',
            'F 16201 T 5371 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179212.000 | s2 206.65353 0.03265 88.42514 206.44501 0.03514 88.42514 206.65353 0.03265 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 0.07645 -0.25370 4.99874 1.000000 | s4 206.03583 1.60759 88.61246 206.24435 1.60509 88.61246 | tw 0',
            'F 16202 T 5372 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179223.000 | s2 206.44501 0.03514 88.42514 205.88733 0.02466 88.04162 206.44501 0.03514 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 0.07791 -0.25148 5.01035 1.000000 | s4 206.73711 1.59689 88.52797 207.29478 1.60737 88.91148 | tw 0',
            'F 16203 T 5372 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179234.000 | s2 206.25909 0.03165 88.29729 205.88733 0.02466 88.04162 206.25909 0.03165 88.29729 | s3 0 - - - - - - - - - - | s1 0.011000 0.41555 -0.34579 4.85488 1.000000 | s4 205.47812 1.59689 88.22906 205.84988 1.60388 88.48473 | tw 0',
            'F 16204 T 5372 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179245.000 | s2 206.07321 0.02815 88.16946 205.88733 0.02466 88.04162 206.07321 0.02815 88.16946 | s3 0 - - - - - - - - - - | s1 0.011000 0.75312 -0.44006 4.69941 1.000000 | s4 205.47812 1.59689 88.22906 205.66400 1.60038 88.35690 | tw 0',
            'F 16205 T 5373 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179256.000 | s2 205.88733 0.02466 88.04162 206.31953 0.02795 88.48486 205.88733 0.02466 88.04162 | s3 0 - - - - - - - - - - | s1 0.011000 0.34062 -0.21224 5.05409 1.000000 | s4 206.15672 1.59998 88.27242 205.72453 1.59668 87.82918 | tw 0',
            'F 16206 T 5373 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179267.000 | s2 206.03139 0.02576 88.18936 206.31953 0.02795 88.48486 206.03139 0.02576 88.18936 | s3 1 207.11699 14.09166 67.47495 207.11700 23.90241 50.47675 207.11700 9.18629 75.97405 1 | s1 0.011000 -0.02386 -0.14105 5.16619 1.000000 | s4 205.91026 1.59998 88.67245 205.62213 1.59778 88.37695 | tw 0',
            'F 16207 T 5373 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179278.000 | s2 206.17546 0.02685 88.33711 206.31953 0.02795 88.48486 206.17546 0.02685 88.33711 | s3 1 207.11700 18.99704 58.97585 207.11700 23.90241 50.47675 207.11700 9.18629 75.97405 1 | s1 0.011000 -0.38838 -0.06985 5.27833 1.000000 | s4 205.91026 1.59998 88.67245 205.76620 1.59888 88.52469 | tw 0',
            'F 16208 T 5374 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179289.000 | s2 206.31953 0.02795 88.48486 205.76855 0.02665 88.14808 206.31953 0.02795 88.48486 | s3 1 207.11700 23.90241 50.47675 207.11700 45.06852 13.78848 207.11700 23.90241 50.47675 1 | s1 0.011000 -0.36142 -0.04679 5.31719 1.000000 | s4 206.25893 1.59852 88.44019 206.80991 1.59983 88.77695 | tw 0',
            'F 16209 T 5374 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179300.000 | s2 206.13586 0.02752 88.37260 205.76855 0.02665 88.14808 206.13586 0.02752 88.37260 | s3 1 207.11699 30.95778 38.24732 207.11700 45.06852 13.78848 207.11700 23.90241 50.47675 1 | s1 0.011000 -0.05755 -0.14761 5.16220 1.000000 | s4 205.35924 1.59852 88.33577 205.72655 1.59939 88.56029 | tw 0',
            'F 16210 T 5374 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179311.000 | s2 205.95221 0.02708 88.26035 205.76855 0.02665 88.14808 205.95221 0.02708 88.26035 | s3 1 207.11700 38.01315 26.01790 207.11700 45.06852 13.78848 207.11700 23.90241 50.47675 1 | s1 0.011000 0.24633 -0.24845 5.00723 1.000000 | s4 205.35924 1.59852 88.33577 205.54289 1.59896 88.44803 | tw 0',
            'F 16211 T 5375 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179322.000 | s2 205.76855 0.02665 88.14808 206.18881 0.02613 88.62977 205.76855 0.02665 88.14808 | s3 1 207.11700 45.06852 13.78848 207.11700 65.49683 -21.64022 207.11700 45.06852 13.78848 1 | s1 0.011000 0.30753 -0.21874 5.07849 1.000000 | s4 206.03566 1.59787 88.36349 205.61542 1.59838 87.88179 | tw 0',
            'F 16212 T 5375 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179333.000 | s2 205.90863 0.02648 88.30864 206.18881 0.02613 88.62977 205.90863 0.02648 88.30864 | s3 1 207.11699 51.87795 1.97891 207.11700 65.49683 -21.64022 207.11700 45.06852 13.78848 1 | s1 0.011000 -0.08260 -0.14875 5.18444 1.000000 | s4 205.77948 1.59787 88.81753 205.49930 1.59821 88.49640 | tw 0',
            'F 16213 T 5375 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179344.000 | s2 206.04874 0.02630 88.46921 206.18881 0.02613 88.62977 206.04874 0.02630 88.46921 | s3 1 207.11700 58.68740 -9.83066 207.11700 65.49683 -21.64022 207.11700 45.06852 13.78848 1 | s1 0.011000 -0.47273 -0.07876 5.29038 1.000000 | s4 205.77948 1.59787 88.81753 205.63940 1.59804 88.65697 | tw 0',
            'F 16214 T 5376 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179355.000 | s2 206.18881 0.02613 88.62977 205.72566 0.02892 88.15687 206.18881 0.02613 88.62977 | s3 1 207.11700 65.49683 -21.64022 207.11700 85.18738 -55.80936 207.11700 65.49683 -21.64022 1 | s1 0.011000 -0.38582 -0.04713 5.31194 1.000000 | s4 206.13217 1.60060 88.57237 206.59532 1.59781 89.04527 | tw 0',
            'F 16215 T 5376 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179366.000 | s2 206.03442 0.02706 88.47214 205.72566 0.02892 88.15687 206.03442 0.02706 88.47214 | s3 1 207.11699 72.06035 -33.02994 207.11700 85.18738 -55.80936 207.11700 65.49683 -21.64022 1 | s1 0.011000 0.00331 -0.12783 5.19228 1.000000 | s4 205.31631 1.60060 88.34465 205.62508 1.59874 88.65992 | tw 0',
            'F 16216 T 5376 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179377.000 | s2 205.88005 0.02799 88.31450 205.72566 0.02892 88.15687 205.88005 0.02799 88.31450 | s3 1 207.11700 78.62386 -44.41965 207.11700 85.18738 -55.80936 207.11700 65.49683 -21.64022 1 | s1 0.011000 0.39244 -0.20852 5.07262 1.000000 | s4 205.31631 1.60060 88.34465 205.47070 1.59967 88.50228 | tw 0',
            'F 16217 T 5377 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179388.000 | s2 205.72566 0.02892 88.15687 206.22047 0.02876 88.60558 205.72566 0.02892 88.15687 | s3 1 207.11700 85.18738 -55.80936 207.11700 104.14011 -88.71890 207.11700 85.18738 -55.80936 1 | s1 0.011000 0.39405 -0.19608 5.03671 1.000000 | s4 205.96347 1.60040 88.41763 205.46866 1.60056 87.96892 | tw 0',
            'F 16218 T 5377 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179399.000 | s2 205.89059 0.02887 88.30643 206.22047 0.02876 88.60558 205.89059 0.02887 88.30643 | s3 1 207.11699 91.50495 -66.77921 207.11700 104.14011 -88.71890 207.11700 85.18738 -55.80936 1 | s1 0.011000 0.01814 -0.10981 5.16786 1.000000 | s4 205.81111 1.60040 88.79333 205.48122 1.60051 88.49419 | tw 0',
            'F 16219 T 5377 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179410.000 | s2 206.05554 0.02881 88.45601 206.22047 0.02876 88.60558 206.05554 0.02881 88.45601 | s3 1 207.11700 97.82253 -77.74905 207.11700 104.14011 -88.71890 207.11700 85.18738 -55.80936 1 | s1 0.011000 -0.35778 -0.02350 5.29905 1.000000 | s4 205.81111 1.60040 88.79333 205.64616 1.60046 88.64376 | tw 0',
            'F 16240 T 5378 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179421.000 | s2 - - - - - - - - - | s3 - - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
        ], 'f9a5fd9120f275d662d7dc2cea84365c67310c1424c5212083486a23b61204e0'),
        'head': ([
            'F 809 T 241 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 9900.000 | s2 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 810 T 241 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 9911.000 | s2 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 207.11699 -0.00179 88.42514 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 811 T 241 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 9922.000 | s2 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 812 T 242 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 9933.000 | s2 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 813 T 242 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 9944.000 | s2 207.11699 -0.00179 88.42514 207.11700 -0.00179 88.42514 207.11699 -0.00179 88.42514 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 814 T 242 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 9955.000 | s2 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 815 T 243 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 9966.000 | s2 207.11700 -0.00179 88.42514 207.75806 0.02429 88.42514 207.11700 -0.00179 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 0.00004 -0.00000 5.46519 1.000000 | s4 207.60109 1.17755 88.64130 - - - | tw 0',
            'F 816 T 243 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 9977.000 | s2 207.33067 0.00691 88.42514 207.75806 0.02429 88.42514 207.33067 0.00691 88.42514 | s3 1 207.11699 14.09166 67.47495 207.11700 23.90241 50.47675 207.11700 9.18629 75.97405 1 | s1 0.011000 -0.07748 0.11441 5.66902 1.000000 | s4 207.74936 1.17755 88.72592 - - - | tw 0',
            'F 817 T 243 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 9988.000 | s2 207.54437 0.01560 88.42514 207.75806 0.02429 88.42514 207.54437 0.01560 88.42514 | s3 1 207.11700 18.99704 58.97585 207.11700 23.90241 50.47675 207.11700 9.18629 75.97405 1 | s1 0.011000 -0.15503 0.22882 5.87288 1.000000 | s4 207.74936 1.17755 88.72592 - - - | tw 0',
            'F 818 T 244 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 9999.000 | s2 207.75806 0.02429 88.42514 207.21600 0.03515 88.42514 207.75806 0.02429 88.42514 | s3 1 207.11700 23.90241 50.47675 207.11700 45.06852 13.78848 207.11700 23.90241 50.47675 1 | s1 0.011000 0.22679 -1.29747 0.47182 1.000000 | s4 208.02846 1.18840 88.64130 208.57050 1.17755 88.64131 | tw 0',
            'F 819 T 244 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 10010.000 | s2 207.57736 0.02791 88.42514 207.21600 0.03515 88.42514 207.57736 0.02791 88.42514 | s3 1 207.11699 30.95778 38.24732 207.11700 45.06852 13.78848 207.11700 23.90241 50.47675 1 | s1 0.011000 0.43985 -1.68498 0.30261 1.000000 | s4 207.20692 1.18870 88.72579 207.56828 1.18146 88.72579 | tw 0',
            'F 820 T 244 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 10021.000 | s2 207.39670 0.03153 88.42514 207.21600 0.03515 88.42514 207.39670 0.03153 88.42514 | s3 1 207.11700 38.01315 26.01790 207.11700 45.06852 13.78848 207.11700 23.90241 50.47675 1 | s1 0.011000 0.65287 -2.07240 0.13348 1.000000 | s4 207.20692 1.18870 88.72579 207.38762 1.18508 88.72579 | tw 0',
            'F 821 T 245 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 10032.000 | s2 207.21600 0.03515 88.42514 207.80458 0.02584 88.42514 207.21600 0.03515 88.42514 | s3 1 207.11700 45.06852 13.78848 207.11700 65.49683 -21.64022 207.11700 45.06852 13.78848 1 | s1 0.011000 -0.89670 -1.84647 0.60181 1.000000 | s4 207.88000 1.17968 88.64104 207.29141 1.18900 88.64104 | tw 0',
        ], '43cfcbce3398ac16def8edad14148b80fd391e65a56343391de24ef1a511fc60'),
        'log': ([
            'command line z:\\home\\caden\\projects\\stalkergameengine\\run\\bin\\simtick18.exe -nointro -dbg -start server(bench1/single/alife/load) client(localhost) -det_mode -det_tag s4d_probe -det_interval 60 -det_checkpoints 3 -det_passes 100,200,320 -det_lua_streams -det_counters -det_synth_k 3 -sim_4d_mask 7 -det_synth_tw -det_synth_walk -det_synth_tracer -det_synth_item',
            '* det_synth: k 3 wallclock 0 tw 1 break 0 mask 7',
            '* det_synth stimulus: installed (walk true, tracer true, item true)',
            '* det_synth: K 3, witness z:/home/caden/projects/stalkergameengine/run\\appdata\\logs\\synth_s4d_probe.txt',
            '* det_synth stimulus: start at update 300 (207.117 -0.002 88.425) time 9966 walk true tracer true ammo ammo_5.45x39_fmj',
            '* det_synth stimulus: item slot 3 section wpn_ppsh41',
            '* det_synth stimulus: 1 bullets fired (update 300)',
            '* det_rng_state cp1: Random=1335924771 Random32=0 alife=302455169 crt_next=6334 frame=4979 time_ms=55770 tick=1631',
            '* det_synth stimulus: 50 bullets fired (update 1770)',
            '* det_synth stimulus: 100 bullets fired (update 3270)',
            '* det_rng_state cp2: Random=639127946 Random32=0 alife=302455169 crt_next=26500 frame=10088 time_ms=111969 tick=3334',
            '* det_synth stimulus: 150 bullets fired (update 4770)',
            '* det_rng_state cp3: Random=-361617133 Random32=0 alife=302455169 crt_next=19169 frame=16217 time_ms=179388 tick=5377',
            '* det_lua_state cp3: 42271744',
            '* eperf dump: checkpoint 3 written, 30463 objects',
            '* Log file has been saved successfully!',
            '- Disconnect',
            '[TutorialMonitor] Ended tutorial state tutorial_campfire_extinguish at 179421',
        ], 'c3d230507cee14d5681423250eda4aa762864daf16e289b037094aae29a728b9'),
    },
    's4d_probe15': {
        'tail': ([
            'F 16218 T 5377 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179399.000 | s2 205.89560 0.02903 88.34575 206.27228 0.02993 88.62798 205.89560 0.02903 88.34575 | s3 1 207.11699 91.50495 -66.77921 207.11700 104.14011 -88.71890 207.11700 85.18738 -55.80936 1 | s1 0.011000 -0.03714 -0.11663 5.16955 0.981672 | s4 205.86383 1.60582 88.81345 205.48715 1.60492 88.53122 | tw 0',
            'F 16219 T 5377 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179410.000 | s2 206.08395 0.02948 88.48686 206.27228 0.02993 88.62798 206.08395 0.02948 88.48686 | s3 1 207.11700 97.82253 -77.74905 207.11700 104.14011 -88.71890 207.11700 85.18738 -55.80936 1 | s1 0.011000 -0.40368 -0.01599 5.32432 0.659736 | s4 205.86383 1.60582 88.81345 205.67551 1.60537 88.67233 | tw 0',
            'F 16220 T 5378 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179421.000 | s2 206.27228 0.02993 88.62798 205.75200 0.02659 88.24551 206.27228 0.02993 88.62798 | s3 1 207.11700 104.14011 -88.71890 207.11700 122.35507 -120.36887 207.11700 104.14011 -88.71890 1 | s1 0.011000 -0.36420 0.04606 2.60511 0.809452 | s4 206.16824 1.60241 88.58765 206.68851 1.60575 88.97013 | tw 0',
            'F 16221 T 5378 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179432.000 | s2 206.09885 0.02882 88.50049 205.75200 0.02659 88.24551 206.09885 0.02882 88.50049 | s3 1 207.11699 110.21176 -99.26889 207.11700 122.35507 -120.36887 207.11700 104.14011 -88.71890 1 | s1 0.011000 -0.03186 -0.04543 2.46204 0.893293 | s4 205.34351 1.60241 88.43092 205.69035 1.60464 88.68591 | tw 0',
            'F 16222 T 5378 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179443.000 | s2 205.92542 0.02770 88.37300 205.75200 0.02659 88.24551 205.92542 0.02770 88.37300 | s3 1 207.11700 116.28342 -109.81888 207.11700 122.35507 -120.36887 207.11700 104.14011 -88.71890 1 | s1 0.011000 0.30046 -0.13693 2.31895 0.940244 | s4 205.34351 1.60241 88.43092 205.51692 1.60352 88.55842 | tw 0',
            'F 16223 T 5379 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179454.000 | s2 205.75200 0.02659 88.24551 205.65300 0.29762 88.27362 205.75200 0.02659 88.24551 | s3 1 207.11700 122.35507 -120.36887 207.11700 139.83223 -150.75926 207.11700 122.35507 -120.36887 1 | s1 0.011000 0.33477 -0.14411 2.24749 0.966537 | s4 206.00964 1.87333 88.47377 206.10863 1.60229 88.44567 | tw 0',
            'F 16224 T 5379 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179465.000 | s2 205.71899 0.11693 88.25488 205.65300 0.29762 88.27362 205.71899 0.11693 88.25488 | s3 1 207.11699 128.18079 -130.49899 207.11700 139.83223 -150.75926 207.11700 122.35507 -120.36887 1 | s1 0.011000 0.32696 -0.19936 2.21970 0.981261 | s4 205.24445 1.87333 88.45901 205.31044 1.69264 88.44028 | tw 0',
            'F 16225 T 5379 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179476.000 | s2 205.68600 0.20728 88.26425 205.65300 0.29762 88.27362 205.68600 0.20728 88.26425 | s3 1 207.11700 134.00652 -140.62914 207.11700 139.83223 -150.75926 207.11700 122.35507 -120.36887 1 | s1 0.011000 0.31914 -0.25460 2.19193 0.989506 | s4 205.24445 1.87333 88.45901 205.27745 1.78298 88.44965 | tw 0',
            'F 16226 T 5380 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179487.000 | s2 205.65300 0.29762 88.27362 205.68001 0.02410 88.20462 205.65300 0.29762 88.27362 | s3 1 207.11700 139.83223 -150.75926 207.11700 151.76012 -171.51430 207.11700 139.83223 -150.75926 1 | s1 0.011000 -0.00169 -0.21825 5.44886 0.994123 | s4 205.77016 1.59963 88.36505 205.74315 1.87316 88.43404 | tw 0',
            'F 16227 T 5380 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179498.000 | s2 205.66199 0.20645 88.25062 205.68001 0.02410 88.20462 205.66199 0.20645 88.25062 | s3 1 207.11699 143.80820 -157.67761 207.11700 151.76012 -171.51430 207.11700 139.83223 -150.75926 1 | s1 0.011000 0.04366 -0.24408 5.46586 0.996709 | s4 205.27138 1.59963 88.39004 205.25336 1.78198 88.43603 | tw 0',
            'F 16228 T 5380 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179509.000 | s2 205.67101 0.11527 88.22762 205.68001 0.02410 88.20462 205.67101 0.11527 88.22762 | s3 1 207.11700 147.78415 -164.59595 207.11700 151.76012 -171.51430 207.11700 139.83223 -150.75926 1 | s1 0.011000 0.08901 -0.23841 5.47842 0.998157 | s4 205.27138 1.59963 88.39004 205.26237 1.69080 88.41302 | tw 0',
            'F 16229 T 5381 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179520.000 | s2 205.68001 0.02410 88.20462 205.93762 0.30429 88.46686 205.68001 0.02410 88.20462 | s3 0 - - - - - - - - - - | s1 0.011000 0.05159 0.07359 5.63784 0.998968 | s4 205.75505 1.87958 88.32852 205.49744 1.59939 88.06628 | tw 0',
            'F 16230 T 5381 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179531.000 | s2 205.76587 0.11749 88.29203 205.93762 0.30429 88.46686 205.76587 0.11749 88.29203 | s3 0 - - - - - - - - - - | s1 0.011000 -0.16427 -0.07404 5.73192 0.999422 | s4 205.52888 1.87958 88.65238 205.35715 1.69279 88.47755 | tw 0',
            'F 16231 T 5381 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179542.000 | s2 205.85175 0.21089 88.37944 205.93762 0.30429 88.46686 205.85175 0.21089 88.37944 | s3 0 - - - - - - - - - - | s1 0.011000 -0.38014 -0.22167 5.82604 0.999676 | s4 205.52888 1.87958 88.65238 205.44302 1.78619 88.56496 | tw 0',
            'F 16232 T 5382 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179553.000 | s2 205.93762 0.30429 88.46686 205.37938 0.04360 88.26921 205.93762 0.30429 88.46686 | s3 0 - - - - - - - - - - | s1 0.011000 -0.20877 -0.11646 6.03372 0.669819 | s4 205.93568 1.61861 88.48050 206.49393 1.87931 88.67814 | tw 0',
            'F 16233 T 5382 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179564.000 | s2 205.75153 0.21740 88.40097 205.37938 0.04360 88.26921 205.75153 0.21740 88.40097 | s3 0 - - - - - - - - - - | s1 0.011000 -0.00201 -0.20554 5.86636 0.485098 | s4 204.97054 1.61861 88.45489 205.34270 1.79241 88.58664 | tw 0',
            'F 16234 T 5382 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179575.000 | s2 205.56546 0.13050 88.33509 205.37938 0.04360 88.26921 205.56546 0.13050 88.33509 | s3 0 - - - - - - - - - - | s1 0.011000 0.20472 -0.13693 5.67652 0.711655 | s4 204.97054 1.61861 88.45489 205.15663 1.70551 88.52076 | tw 0',
            'F 16235 T 5383 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179586.000 | s2 205.37938 0.04360 88.26921 205.99086 0.03620 88.41077 205.37938 0.04360 88.26921 | s3 0 - - - - - - - - - - | s1 0.011000 0.21317 -0.03068 5.52628 0.838527 | s4 205.67401 1.38763 88.46611 205.06253 1.39502 88.32456 | tw 0',
            'F 16236 T 5383 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179597.000 | s2 205.58319 0.04113 88.31639 205.99086 0.03620 88.41077 205.58319 0.04113 88.31639 | s3 0 - - - - - - - - - - | s1 0.011000 0.03886 0.11826 5.71022 0.909575 | s4 205.60663 1.38763 88.62640 205.19896 1.39256 88.53203 | tw 0',
            'F 16237 T 5383 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179608.000 | s2 205.78702 0.03867 88.36359 205.99086 0.03620 88.41077 205.78702 0.03867 88.36359 | s3 0 - - - - - - - - - - | s1 0.011000 -0.13516 0.27094 5.89386 0.949362 | s4 205.60663 1.38763 88.62640 205.40279 1.39009 88.57922 | tw 0',
            'F 16238 T 5384 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179619.000 | s2 205.99086 0.03620 88.41077 206.62628 0.02847 88.42514 205.99086 0.03620 88.41077 | s3 0 - - - - - - - - - - | s1 0.011000 -0.17116 0.09887 5.36379 0.641643 | s4 205.91928 1.20032 88.51725 205.28384 1.20805 88.50287 | tw 0',
            'F 16239 T 5384 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179630.000 | s2 206.20265 0.03362 88.41556 206.62628 0.02847 88.42514 206.20265 0.03362 88.41556 | s3 0 - - - - - - - - - - | s1 0.011000 -0.25797 0.26404 5.56135 0.469320 | s4 206.26302 1.20032 88.66389 205.83937 1.20547 88.65430 | tw 0',
            'F 16240 T 5384 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179641.000 | s2 206.41446 0.03105 88.42035 206.62628 0.02847 88.42514 206.41446 0.03105 88.42035 | s3 0 - - - - - - - - - - | s1 0.011000 -0.34431 0.43108 5.75870 0.372819 | s4 206.26302 1.20032 88.66389 206.05118 1.20289 88.65910 | tw 0',
            'F 16241 T 5385 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179652.000 | s2 206.62628 0.02847 88.42514 206.10782 0.03344 88.42514 206.62628 0.02847 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 -0.08839 0.18045 3.10943 0.648779 | s4 206.56787 1.06517 88.59181 207.08632 1.06020 88.59181 | tw 0',
            'F 16242 T 5385 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179663.000 | s2 206.45345 0.03013 88.42514 206.10782 0.03344 88.42514 206.45345 0.03013 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 -0.02694 0.08216 2.94757 0.803316 | s4 205.76254 1.06517 88.68224 206.10815 1.06186 88.68224 | tw 0',
            'F 16243 T 5385 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179674.000 | s2 206.28064 0.03178 88.42514 206.10782 0.03344 88.42514 206.28064 0.03178 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 0.03428 -0.01841 2.78587 0.889857 | s4 205.76254 1.06517 88.68224 205.93535 1.06351 88.68224 | tw 0',
            'F 16244 T 5386 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179685.000 | s2 206.10782 0.03344 88.42514 206.72545 0.02829 88.42514 206.10782 0.03344 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 0.05193 -0.00703 2.46227 0.938320 | s4 206.43843 1.05640 88.59594 205.82080 1.06155 88.59593 | tw 0',
            'F 16245 T 5386 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 179696.000 | s2 206.31369 0.03172 88.42514 206.72545 0.02829 88.42514 206.31369 0.03172 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 -0.02111 0.14204 2.65448 0.965459 | s4 206.38158 1.05640 88.68208 205.96982 1.05984 88.68208 | tw 0',
            'F 16246 T 5386 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 179707.000 | s2 206.51956 0.03000 88.42514 206.72545 0.02829 88.42514 206.51956 0.03000 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 -0.09396 0.28972 2.84662 0.980657 | s4 206.38158 1.05640 88.68208 206.17569 1.05812 88.68208 | tw 0',
            'F 16267 T 5387 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 179718.000 | s2 - - - - - - - - - | s3 - - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
        ], 'f811c4cc29c793ab03aabbd079575dbcd1b1a79daa09c1e534853c53f89dc30a'),
        'head': ([
            'F 809 T 241 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 9900.000 | s2 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 810 T 241 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 9911.000 | s2 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 207.11699 -0.00179 88.42514 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 811 T 241 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 9922.000 | s2 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 812 T 242 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 9933.000 | s2 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 813 T 242 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 9944.000 | s2 207.11699 -0.00179 88.42514 207.11700 -0.00179 88.42514 207.11699 -0.00179 88.42514 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 814 T 242 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 9955.000 | s2 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 207.11700 -0.00179 88.42514 | s3 0 - - - - - - - - - - | s1 - - - - - | s4 - - - - - - | tw 0',
            'F 815 T 243 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 9966.000 | s2 207.11700 -0.00179 88.42514 207.72215 0.05076 88.42514 207.11700 -0.00179 88.42514 | s3 0 - - - - - - - - - - | s1 0.011000 0.00004 -0.00000 5.46519 1.000000 | s4 207.60109 1.20402 88.64130 - - - | tw 0',
            'F 816 T 243 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 9977.000 | s2 207.31871 0.01573 88.42514 207.72215 0.05076 88.42514 207.31871 0.01573 88.42514 | s3 1 207.11699 14.09166 67.47495 207.11700 23.90241 50.47675 207.11700 9.18629 75.97405 1 | s1 0.011000 -0.07314 0.09100 5.66002 1.000000 | s4 207.71346 1.20402 88.72592 - - - | tw 0',
            'F 817 T 243 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 9988.000 | s2 207.52045 0.03325 88.42514 207.72215 0.05076 88.42514 207.52045 0.03325 88.42514 | s3 1 207.11700 18.99704 58.97585 207.11700 23.90241 50.47675 207.11700 9.18629 75.97405 1 | s1 0.011000 -0.14633 0.18201 5.85489 1.000000 | s4 207.71346 1.20402 88.72592 - - - | tw 0',
            'F 818 T 244 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 9999.000 | s2 207.72215 0.05076 88.42514 207.21600 0.03515 88.42514 207.72215 0.05076 88.42514 | s3 1 207.11700 23.90241 50.47675 207.11700 45.06852 13.78848 207.11700 23.90241 50.47675 1 | s1 0.011000 0.24530 -1.40949 0.46623 1.000000 | s4 208.00453 1.18840 88.64130 208.51068 1.20402 88.64130 | tw 0',
            'F 819 T 244 j 1 K 3 n 0 a 0.333333 dt 0.011000 hud 10010.000 | s2 207.55344 0.04556 88.42514 207.21600 0.03515 88.42514 207.55344 0.04556 88.42514 | s3 1 207.11699 30.95778 38.24732 207.11700 45.06852 13.78848 207.11700 23.90241 50.47675 1 | s1 0.011000 0.44427 -1.71408 0.30577 1.000000 | s4 207.20692 1.18146 88.72579 207.54436 1.19187 88.72579 | tw 0',
            'F 820 T 244 j 2 K 3 n 0 a 0.666667 dt 0.011000 hud 10021.000 | s2 207.38474 0.04035 88.42514 207.21600 0.03515 88.42514 207.38474 0.04035 88.42514 | s3 1 207.11700 38.01315 26.01790 207.11700 45.06852 13.78848 207.11700 23.90241 50.47675 1 | s1 0.011000 0.64320 -2.01868 0.14532 1.000000 | s4 207.20692 1.18146 88.72579 207.37566 1.18666 88.72579 | tw 0',
            'F 821 T 245 j 0 K 3 n 1 a 0.000000 dt 0.011000 hud 10032.000 | s2 207.21600 0.03515 88.42514 207.80458 0.02584 88.42514 207.21600 0.03515 88.42514 | s3 1 207.11700 45.06852 13.78848 207.11700 65.49683 -21.64022 207.11700 45.06852 13.78848 1 | s1 0.011000 -0.91038 -1.80603 0.61456 1.000000 | s4 207.86804 1.17949 88.64104 207.27945 1.18880 88.64104 | tw 0',
        ], '5c9f58618a150a2c7f3a17f05eba3d67ed44d6debdef1273288721e209d033c5'),
        'log': ([
            'command line z:\\home\\caden\\projects\\stalkergameengine\\run\\bin\\simtick18.exe -nointro -dbg -start server(bench1/single/alife/load) client(localhost) -det_mode -det_tag s4d_probe15 -det_interval 60 -det_checkpoints 3 -det_passes 100,200,320 -det_lua_streams -det_counters -det_synth_k 3 -sim_4d_mask 15 -det_synth_tw -det_synth_walk -det_synth_tracer -det_synth_item',
            '* det_synth: k 3 wallclock 0 tw 1 break 0 mask 15',
            '* det_synth stimulus: installed (walk true, tracer true, item true)',
            '* det_synth: K 3, witness z:/home/caden/projects/stalkergameengine/run\\appdata\\logs\\synth_s4d_probe15.txt',
            '* det_synth stimulus: start at update 300 (207.117 -0.002 88.425) time 9966 walk true tracer true ammo ammo_5.45x39_fmj',
            '* det_synth stimulus: item slot 3 section wpn_ppsh41',
            '* det_synth stimulus: 1 bullets fired (update 300)',
            '* det_rng_state cp1: Random=-43230087 Random32=0 alife=302455169 crt_next=6334 frame=4976 time_ms=55737 tick=1630',
            '* det_synth stimulus: 50 bullets fired (update 1770)',
            '* det_synth stimulus: 100 bullets fired (update 3270)',
            '* det_rng_state cp2: Random=1093068115 Random32=0 alife=302455169 crt_next=26500 frame=10091 time_ms=112002 tick=3335',
            '* det_synth stimulus: 150 bullets fired (update 4770)',
            '* det_rng_state cp3: Random=826200083 Random32=0 alife=302455169 crt_next=19169 frame=16244 time_ms=179685 tick=5386',
            '* det_lua_state cp3: 647845102',
            '* eperf dump: checkpoint 3 written, 30463 objects',
            '* Log file has been saved successfully!',
            '- Disconnect',
            'Task State Monitor shut down at 179718',
        ], 'a91f199b01819bdaaf501de359d1a21365c7dddb88d4320ac2d1340083cd8e4f'),
    },
}
for tag_, cuts_ in REAL2.items():   # the literal above is the real bytes, unedited
    for part_, (lines_, sha_) in cuts_.items():
        sep_ = "\r\n" if part_ == "log" else "\n"
        assert hashlib.sha256((sep_.join(lines_) + sep_).encode()).hexdigest() == sha_, (tag_, part_)
ITEM_LINE = "* det_synth stimulus: item slot 3 section wpn_ppsh41"
for tag_ in REAL2:
    assert REAL2[tag_]["log"][0].count(ITEM_LINE) == 1   # the real line, as cut (compared, not used to build fixtures)
CP3_2 = {"s4d_probe": 16217, "s4d_probe15": 16244}   # cp3 frames of the GO probes (their cp3 lines in the log cuts)
def r2(tag):           # (tail witness, log, sha, sha) for td_folder
    c = REAL2[tag]; return (c["tail"][0], c["log"][0], c["tail"][1], c["log"][1])

# (a) teardown: the real tails WITH the item, one teardown line each, dropped and counted; no pattern error
td_case("td2_real_item_probe", "s4d_probe", 0, must=("dropped 1",), mustnot=("ERR", "follows"), src=r2("s4d_probe"))
td_case("td2_real_item_probe15", "s4d_probe15", 0, must=("dropped 1",), mustnot=("ERR", "follows"), src=r2("s4d_probe15"))
def td2_more(n):       # n more trailing no-actor lines after the real teardown line, frames following it
    def f(wl):
        t0 = wl[-1].split(); assert t0[18:27] == ["-"] * 9   # the real last line has no actor
        wl += [td_no_actor(wl[-1], int(t0[1]) + i) for i in range(1, n + 1)]
    return f
# a second trailing no-actor line: 2 = TEARDOWN_MAX, still inside the rule (Amendment 2: "at most 2"), both dropped
td_case("td2_two_trailing", "s4d_probe", 0, wit_fn=td2_more(1), must=("dropped 2",), mustnot=("ERR", "follows"), src=r2("s4d_probe"))
# a third: 3 > TEARDOWN_MAX, none dropped, fails by the TEARDOWN_MAX message (and the frame jump the rule then reads)
td_case("td2_three_trailing", "s4d_probe", 1, wit_fn=td2_more(2), src=r2("s4d_probe"), must=("dropped 0",
        "ERR 3 trailing no-actor lines after cp3 frame 16217 (F 16240..16242) > TEARDOWN_MAX 2: none dropped", "ERR F 16240 follows F 16219"))
td_case("td2_three_trailing15", "s4d_probe15", 1, wit_fn=td2_more(2), src=r2("s4d_probe15"), must=("dropped 0",
        "ERR 3 trailing no-actor lines after cp3 frame 16244 (F 16267..16269) > TEARDOWN_MAX 2: none dropped", "ERR F 16267 follows F 16246"))

# (b), (c) rule "item" in mode=probe on real cuts of both probes: witness = head + tail (LF), log = the log cut (CRLF).
# The cut makes other probe rules STOP (pattern: the gap after F 821; input: no sched file; ...), so each mutant is
# read as a DIFFERENCE: its "PROBE STOP" lines must be exactly the baseline's plus the ones named for it.
I2 = os.path.join(ST, "item_real")
def i2_folder(name, wfn=None, lfn=None):
    root = os.path.join(I2, name)
    for tag, c in REAL2.items():
        wl, xl = list(c["head"][0]) + list(c["tail"][0]), list(c["log"][0])
        if wfn and tag in wfn:
            wfn[tag](wl)
        if lfn and tag in lfn:
            lfn[tag](xl)
        d = os.path.join(root, "det_" + tag); os.makedirs(d)
        write(os.path.join(d, "synth_%s.txt" % tag), wl, False)
        write(os.path.join(d, "xray.log"), xl, True)
        if not (wfn and tag in wfn):
            assert open(os.path.join(d, "synth_%s.txt" % tag), "rb").read() == (
                "\n".join(c["head"][0]) + "\n" + "\n".join(c["tail"][0]) + "\n").encode()
        if not (lfn and tag in lfn):
            assert hashlib.sha256(open(os.path.join(d, "xray.log"), "rb").read()).hexdigest() == c["log"][1]
    return root
def i2_run(name, **kw):
    root = i2_folder(name, **kw); out = os.path.join(root, "probe.txt")
    p = subprocess.run([sys.executable, "-I", SCF, "mode=probe", "root=" + root, "out=" + out], capture_output=True, text=True)
    txt = open(out).read() if os.path.exists(out) else p.stdout + p.stderr
    txt = txt.replace(root, "<root>")   # STOP lines naming a missing file carry the case's own folder
    return p.returncode, txt, [l for l in txt.split("\n") if l.startswith("PROBE STOP ")]
# window per probe: head F 815..821 (7 lines) + tail lines up to cp3 (27: the tail's last 3 lines are F > cp3) = 34
ITEM_OK = ["item %s: item slot 3 section wpn_ppsh41 (installed line: item true); s4 on 34 of 34 witness lines F 815..%d "
           "(stimulus start to cp3): share 1.0000 (rule >= 0.50)" % (t, CP3_2[t]) for t in REAL2]
B_code, B_txt, B_stop = i2_run("baseline")
ok = (B_code == 1 and all(s in B_txt.split("\n") for s in ITEM_OK) and TDL + "s4d_probe 1, s4d_probe15 1" in B_txt
      and not any(l.startswith(("PROBE STOP item", "PROBE STOP flags")) for l in B_stop))
results.append(("item2_real_baseline", ok))
print("%-4s %-22s exit %d (want 1) item rule holds on both real cuts, flags hold, teardown 1+1; cut STOPs: %s" % (
      "ok" if ok else "BAD", "item2_real_baseline", B_code, sorted({l.split(":")[0] for l in B_stop})))
if not ok:
    print("     " + "\n     ".join(B_txt.split("\n")))
def i2_case(name, want_new, must=(), **kw):
    code, txt, stop = i2_run(name, **kw)
    new, gone = [l for l in stop if l not in B_stop], [l for l in B_stop if l not in stop]
    ok = code == 1 and new == list(want_new) and not gone and all(m in txt.split("\n") for m in must)
    results.append((name, ok))
    print("%-4s %-22s exit %d (want 1) new STOP %s%s" % ("ok" if ok else "BAD", name, code, new, (" GONE %s" % gone) if gone else ""))
    if not ok:
        print("     want new %s" % list(want_new)); print("     " + "\n     ".join(txt.split("\n")))
    return txt
def i2_drop_item(xl):            # the real item line removed
    i = [k for k, l in enumerate(xl) if l == ITEM_LINE]; assert len(i) == 1; del xl[i[0]]
def i2_item_none(xl):            # the real item line, its item part changed to the NONE text of zz_eperf_synth.script:78
    i = [k for k, l in enumerate(xl) if l == ITEM_LINE]; assert len(i) == 1
    xl[i[0]] = xl[i[0]].replace("item slot 3 section wpn_ppsh41", "item NONE (no filled slot)")
def i2_dash(n, tag):             # s4 dashed (six '-') on the first n lines of the window F 815..cp3
    def f(wl):
        w = [k for k, l in enumerate(wl) if 815 <= int(l.split()[1]) <= CP3_2[tag]]; assert len(w) == 34
        for k in w[:n]:
            t = wl[k].split(); assert t[48] == "s4" and t[55] == "|"; t[49:55] = ["-"] * 6; wl[k] = " ".join(t)
    return f
i2_case("item2_line_removed", ["PROBE STOP item: s4d_probe: no 'det_synth stimulus: item' line (installed line: item true)"],
        lfn={"s4d_probe": i2_drop_item})
i2_case("item2_line_none", ["PROBE STOP item: s4d_probe15: item NONE (no filled slot) (installed line: item true)"],
        lfn={"s4d_probe15": i2_item_none})
# s4 dashed on 18 of the 34 window lines (more than half): share 16/34 = 0.4706 < 0.50, both probes
i2_case("item2_s4_dashed_18of34", ["PROBE STOP item: s4d_probe: s4 share 0.4706 (16 of 34 lines F 815..16217) < 0.50",
        "PROBE STOP item: s4d_probe15: s4 share 0.4706 (16 of 34 lines F 815..16244) < 0.50"],
        wfn={t: i2_dash(18, t) for t in REAL2})
# boundary: 17 of 34 dashed = exactly half: share 0.5000 >= 0.50, no new STOP
i2_case("item2_s4_dashed_17of34", [], wfn={t: i2_dash(17, t) for t in REAL2}, must=["item %s: item slot 3 section wpn_ppsh41 "
        "(installed line: item true); s4 on 17 of 34 witness lines F 815..%d (stimulus start to cp3): share 0.5000 (rule >= 0.50)" % (t, CP3_2[t]) for t in REAL2])

# ---- the exit path of THIS wrapper (job: a self-test that exits 0 when broken passes when broken). Each re-runs the
# real file with S4D1_SELFTEST_SELFCHECK set, into a subfolder of this run; the embedded Python crashes at its start
# (crash) or records one failed case and calls finish() (fail). Both must give exit 1 and the wrapper's FAIL last line.
def selfcheck(mode, must):
    par = os.path.join(ST, "selfcheck_" + mode); os.makedirs(par)
    p = subprocess.run(["bash", WRAP, par], env=dict(os.environ, S4D1_SELFTEST_SELFCHECK=mode), capture_output=True,
                       text=True, timeout=60)
    last = p.stdout.rstrip("\n").split("\n")[-1]
    ok = p.returncode == 1 and last.startswith("S4D1 SELFTEST: FAIL (self-test exit: python 1,") and all(s in p.stdout for s in must)
    results.append(("selfcheck_" + mode, ok))
    print("%-4s %-22s exit %d (want 1) last line: %s" % ("ok" if ok else "BAD", "selfcheck_" + mode, p.returncode, last))
    if not ok:
        print("     " + "\n     ".join(p.stdout.split("\n")[-12:]))
selfcheck("crash", ("Traceback", "RuntimeError: deliberate crash"))
selfcheck("fail", ("BAD  selfcheck_forced_bad_case", "S4D1 SELFTEST: FAIL (0/1)"))

finish()
PYEOF
ST_PIPE=("${PIPESTATUS[@]}")   # [0] = the embedded Python, [1] = tee (a pipeline's own status is its LAST command's)
echo "output folder: $ST ($(du -sh "$ST" | cut -f1))"
if [ "${ST_PIPE[0]}" != 0 ] || [ "${ST_PIPE[1]}" != 0 ]; then
    echo "S4D1 SELFTEST: FAIL (self-test exit: python ${ST_PIPE[0]}, tee ${ST_PIPE[1]}; a crash or a failed case, see $ST/selftest_output.txt)" | tee -a "$ST/selftest_output.txt"
    exit 1
fi
exit 0
