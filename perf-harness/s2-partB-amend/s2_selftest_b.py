#!/usr/bin/env python3
"""Self-test of s2_score.py. Fixtures: a REAL past bench run (out/r1b_ros_off_1: stock fixed 33 ms, 3,000 frames) copied
as arm s2_f33, and synthetic arms made from it with known numbers. Usage: s2_selftest_b.py <scorer> <real run dir> <work dir> [registered scorer]\nAmended for prereg section 11: every synthetic log has warm-up, capture and quit phases."""
import sys, os, shutil, subprocess, json, re
SC, REAL, W = sys.argv[1:4]; os.makedirs(W); OLD = sys.argv[4] if len(sys.argv) > 4 else None
rt = os.path.basename(REAL.rstrip('/'))
sites = open(os.path.join(REAL, f'perf_sites_{rt}.csv'), 'rb').read().decode('utf-8', 'replace')
frames = open(os.path.join(REAL, f'perf_frames_{rt}.csv'), 'rb').read().decode()
def mk(root, tag, N=3000, U=3000, total_ms=None, frame_ms=None, scale=None, simtick=None, verdict='VERDICT: clean', drop_site=None, phys=None, markers=True):
    d = os.path.join(root, tag); os.makedirs(d); out = []
    for l in sites.replace('\r', '').split('\n'):
        f = l.split(',')
        if l.startswith('#frames,'): l = f'#frames,{N}'
        elif l.startswith('#total_ms,') and total_ms: l = f'#total_ms,{total_ms}'
        elif len(f) >= 9 and not l.startswith('name,'):
            if f[0] == drop_site: continue
            cm, co = float(f[-6]), float(f[-3])
            if f[0] == 'Process seqFrame' or f[0] == 'Render: Frame Move': cm = U
            if f[0] == 'CPHWorld::Step' and phys is not None: co = phys
            if scale and f[0] in scale: f[-5] = f'{float(f[-5]) * scale[f[0]]:.4f}'; f[-2] = f'{float(f[-2]) * scale[f[0]]:.4f}'
            f[-6] = str(int(cm)); f[-3] = str(int(co)); l = ','.join(f)
        out.append(l)
    open(os.path.join(d, f'perf_sites_{tag}.csv'), 'w').write('\r\n'.join(out))          # CRLF, as the engine writes under Wine
    fr = frames if frame_ms is None else 'frame,ms\n' + ''.join(f'{i},{frame_ms[i % len(frame_ms)]}\n' for i in range(N))
    open(os.path.join(d, f'perf_frames_{tag}.csv'), 'w').write(fr)
    open(os.path.join(d, 'conditions.txt'), 'w').write('start x\n' + verdict + '\n')
    # the log has the three PHASES of a real run. Gated arms get a POISONED line (dropped 999, age 9999, all frames 3+
    # ticks) in the warm-up, as the first line inside the capture (its 10 s window reaches back into the warm-up) and
    # in the quit frames: a scorer that lets any of them in gives wrong shares, drops and ages in every gated test below.
    st = list(simtick or []); log = ['boot'] + st[:1] + ([POISON] if st else [])
    if markers: log += [f"* [xrprof] capture '{tag}' started: {N} frames"]
    log += ([POISON] if st else []) + st[1:]
    if markers: log += [f"* [xrprof] capture '{tag}' finished: {N} frames, 1.0 ms total, 1.000 ms/frame avg"]
    log += ([POISON] if st else [])
    open(os.path.join(d, 'xray.log'), 'w').write(''.join(s + '\r\n' for s in log))
ST = lambda hz, fr, tk, n, dr=0, age=2.5, mx=20: f'* [SIMTICK] hz {hz} frames {fr} ticks {tk} n0 {n[0]} n1 {n[1]} n2 {n[2]} n3p {n[3]} dropped {dr} age_avg_ms {age:.2f} age_max_ms {mx} time_global 1 real_ms 1'
POISON = ST(30, 5000, 15000, (0, 0, 0, 5000), dr=999, age=9999.0, mx=9999)
def run(root, pred, sc=None):
    p = os.path.join(root, 'pred.json'); json.dump(pred, open(p, 'w'))
    return subprocess.run([sys.executable, '-I', sc or SC, root, p, os.path.join(root, 'score.txt')], capture_output=True, text=True).stdout
fails = []
def want(name, cond, txt):
    print(('ok   ' if cond else 'FAIL ') + name)
    if not cond: fails.append(name); print(txt[-1500:])
val = lambda txt, arm, k: float(re.search(rf'ARM {re.escape(arm)} .*?\n(?:  .*\n)*?  {k}\s+([-\d.]+)', txt).group(1))
# 1. the real run as s2_f33: values known from the file itself (3000 updates in 16,667.631 ms)
r = os.path.join(W, 'real'); os.makedirs(r); mk(r, 's2_f33')
t = run(r, {'s2_f33.main_tick': [1.55, 1.58], 's2_f33.phys_steps': [3.29, 3.31], 's2_f33.rate': [179, 181], 's2_f33.nothing': [0, 1]})
want('real run: main_tick 1.563 (4688.0362 / 3000)', abs(val(t, 's2_f33', 'main_tick') - 4688.0362 / 3000) < 0.001, t)
want('real run: phys steps 3.3 (9900 / 3000)', abs(val(t, 's2_f33', 'phys_steps') - 3.3) < 0.001, t)
want('real run: rate = fps = 3000 / 16.667631 s', abs(val(t, 's2_f33', 'rate') - 3000 / 16.667631) < 0.01 and abs(val(t, 's2_f33', 'fps') - val(t, 's2_f33', 'rate')) < 1e-6, t)
want('real run: 3 hits, 0 miss, 1 no value', 'S2 SCORE: 3 hit, 0 miss, 1 no value, of 4 predictions; arms valid 1 of 1' in t, t)
# 2. synthetic series: gated 30 and 60 with known numbers, two replicates, a capped arm, an off arm
r = os.path.join(W, 'syn'); os.makedirs(r)
g30 = [ST(30, 2700, 300, (2400, 300, 0, 0))] * 4; g60 = [ST(60, 2500, 600, (1910, 580, 10, 0), dr=1, age=1.5, mx=30)] * 4
mk(r, 's2_t30_1', N=9000, U=1000, total_ms=33333.3, frame_ms=[3.0] * 8 + [6.0], simtick=g30, phys=3333)
mk(r, 's2_t30_2', N=9000, U=1000, total_ms=33333.3, frame_ms=[3.0] * 8 + [6.0], simtick=g30, phys=3333, scale={'Process seqFrame': 1.1})
mk(r, 's2_t60_1', N=9000, U=2000, total_ms=33333.3, frame_ms=[3.0] * 7 + [6.0, 30.0], simtick=g60, phys=3334)
mk(r, 's2_c30', N=1800, U=1800, total_ms=60000.0, frame_ms=[33.3])
mk(r, 's2_off_1', N=9000, U=9000, total_ms=45000.0, frame_ms=[5.0])
t = run(r, {'s2_t30.rate': [29.9, 30.1], 's2_t60/s2_t30.sim_s': [0.5, 1.1], 's2_t60.over25': [0, 1]})
m1 = 4688.0362 / 1000
want('gated: rate 30.0 and 60.0', abs(val(t, 's2_t30', 'rate') - 30.0) < 0.01 and abs(val(t, 's2_t60', 'rate') - 60.0) < 0.01, t)
want('gated: replicates averaged (main_tick = mean of x1.0 and x1.1)', abs(val(t, 's2_t30', 'main_tick') - m1 * 1.05) < 0.002, t)
want('gated: phys steps 3.333 and 1.667', abs(val(t, 's2_t30', 'phys_steps') - 3.333) < 0.001 and abs(val(t, 's2_t60', 'phys_steps') - 1.667) < 0.001, t)
want('gated: first SIMTICK line dropped, shares from the rest (n0 0.889; 60 Hz dropped 3, age max 30)', abs(val(t, 's2_t30', 'n0') - 2400 / 2700) < 0.001 and val(t, 's2_t60', 'dropped') == 3 and val(t, 's2_t60', 'age_max') == 30, t)
want('frames: p50 3.0, p99 6.0 at 30 Hz; over25 = 111.1 per 1000 at 60 Hz', val(t, 's2_t30', 'p50') == 3.0 and val(t, 's2_t30', 'p99') == 6.0 and abs(val(t, 's2_t60', 'over25') - 111.111) < 0.01, t)
want('delay gated 30 Hz = 16.667 + Frame Move per tick + p50', abs(val(t, 's2_t30', 'delay') - (500 / 30.0 + float(re.search(r'Render: Frame Move,.*?,\d+,(\d+),([\d.]+)', sites).group(2)) / 1000 + 3.0)) < 0.01, t)
want('delay stock capped = half interval + mean frame', abs(val(t, 's2_c30', 'delay') - (500 / 30.0 + 33.3)) < 0.01, t)
want('ratio line and prediction MISS shown (over25 111 not in [0, 1])', 'RATIO s2_t60/s2_t30 sim_s' in t and 'PREDICTION s2_t60.over25 [0, 1]: 111.111 MISS' in t, t)
if OLD:   # the registered scorer on the same phased logs: it must be WRONG here, or these fixtures would not catch the leak
    t0 = run(r, {}, sc=OLD); want('the registered scorer lets the warm-up/quit lines in on these logs (dropped >= 999)', val(t0, 's2_t60', 'dropped') >= 999 and val(t0, 's2_t30', 'age_max') == 9999, t0)
# 3. invalid arms
def inv(name, expect, **kw):
    kw.setdefault('frame_ms', [3.0])
    r = os.path.join(W, name); os.makedirs(r); mk(r, kw.pop('tag', 's2_t30_1'), **kw); t = run(r, {'s2_t30.rate': [29, 31]})
    want(f'invalid: {name} (and no other reason)', f'INVALID s2_t30_1: {expect}' in t and t.count(';', t.index('INVALID'), t.index('\n', t.index('INVALID'))) == expect.count(';') and 'arms valid 0 of 1' in t and 'NO VALUE' in t, t)
inv('m_rate', 'rate 33.00 not within 3 % of 30', N=9000, U=1100, total_ms=33333.3, simtick=g30)
inv('m_dirty', 'conditions not clean', N=9000, U=1000, total_ms=33333.3, simtick=g30, verdict='VERDICT: CONFOUNDED (other heavy process present)')
inv('m_nosite', 'sites missing: Sheduler Deferred', N=9000, U=1000, total_ms=33333.3, simtick=g30, drop_site='Sheduler Deferred')
inv('m_nosimtick', 'no whole [SIMTICK] window inside the capture', N=9000, U=1000, total_ms=33333.3, simtick=g30[:1])
inv('m_nomarkers', 'capture start/finish lines missing in the engine log', N=9000, U=1000, total_ms=33333.3, simtick=g30, markers=False)
r = os.path.join(W, 'm_rows'); os.makedirs(r); d = os.path.join(r, 's2_t30_1'); mk(r, 's2_t30_1', N=9000, U=1000, total_ms=33333.3, simtick=g30)
open(os.path.join(d, 'perf_frames_s2_t30_1.csv'), 'w').write(frames); t = run(r, {'s2_t30.rate': [29, 31]})
want('invalid: frames file rows differ', 'frames file has 3000 rows for 9000 frames' in t and 'arms valid 0 of 1' in t, t)
# 4. a re-run replaces its original
r = os.path.join(W, 'rr'); os.makedirs(r)
mk(r, 's2_c30', N=1800, U=1800, total_ms=60000.0, frame_ms=[33.3], verdict='VERDICT: CONFOUNDED (x)'); mk(r, 's2_c30_rr', N=1800, U=1800, total_ms=60000.0, frame_ms=[20.0])
t = run(r, {'s2_c30.p50': [19, 21]})
want('re-run replaces the original (no INVALID line, p50 20)', 'INVALID' not in t and 'PREDICTION s2_c30.p50 [19, 21]: 20.000 HIT' in t, t)
print('SELFTEST ' + ('PASS' if not fails else 'FAIL: ' + '; '.join(fails)))
