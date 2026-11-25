#!/usr/bin/env python3
"""Phase test (Overseer, 2026-11-25): the amended per-run check (s2_series_b.sh --check) and the amended scorer
(s2_score_b.py) against EVERY run type of part B, each log with its three phases: warm-up, capture, quit.
  s2_phase_selftest.py <out root with the finished runs> <work dir>
Base = REAL finished run folders (engine bytes). Types with no finished run yet are made from the nearest real one
(f60, f50 from f30; t30L, t60L from t30, t60; offL is the REAL 4-core run out/s2_offl). Folder and file names are
lowercased as the bench does it; the check is called with the tag as the order file spells it.
For every type: (1) the check passes and the scorer finds the arm valid; (2) a POISONED [SIMTICK] line (dropped 999,
age 9999) is put into the warm-up and into the quit phase:
  - switch on (t, f, tL types): the check's answer and every scorer value must be IDENTICAL to the clean run;
  - switch off (off, c, f33, offL): the check must FAIL (the gate printed in a run that asked for stock);
(3) gated types only: the same line put in the MIDDLE of the capture must change the scorer's dropped count (the
scorer still reads the capture)."""
import sys, os, shutil, subprocess, re
ROOT, W = sys.argv[1:3]; HERE = os.path.dirname(os.path.abspath(__file__)); os.makedirs(W); bad = []
POISON = '* [SIMTICK] hz 30 frames 5000 ticks 15000 n0 0 n1 0 n2 0 n3p 5000 dropped 999 age_avg_ms 9999.00 age_max_ms 9999 time_global 1 real_ms 1'
#        type      real base     cfg                  fixed frames
TYPES = [('s2_off_1',  's2_off_1',  '',                  0, 12000), ('s2_t30_1', 's2_t30_1', 'sim_tick=30', 0, 12000),
         ('s2_t60_1',  's2_t60_1',  'sim_tick=60',       0, 12000), ('s2_t50_1', 's2_t50_1', 'sim_tick=50', 0, 12000),
         ('s2_c30',    's2_c30',    'r__framelimit=30',  0, 1800),  ('s2_c60',   's2_c60',   'r__framelimit=60', 0, 3600),
         ('s2_c50',    's2_c50',    'r__framelimit=50',  0, 3000),  ('s2_f33',   's2_f33',   '',            1, 3000),
         ('s2_f30_rr', 's2_f30',    'sim_tick=30',       1, 3000),  ('s2_f60',   's2_f30',   'sim_tick=60', 1, 3000),
         ('s2_f50',    's2_f30',    'sim_tick=50',       1, 3000),  ('s2_offl_rr', 's2_offl', '',           0, 12000),
         ('s2_t30l',   's2_t30_1',  'sim_tick=30',       0, 12000), ('s2_t60l',  's2_t60_1', 'sim_tick=60', 0, 12000)]
def want(name, ok, detail=''):
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else f'   <- {detail}'));  bad.append(name) if not ok else None
def make(root, tag, base, cfg, where=None):
    tag = tag.lower()   # as tools/s2_bench.sh does (TAG=${TAG,,}): folder and file names are lowercase, whatever the order file says
    d = os.path.join(root, tag); os.makedirs(d); s = os.path.join(ROOT, base)
    for f in ('perf_sites_', 'perf_frames_'): shutil.copy(os.path.join(s, f'{f}{base}.csv'), os.path.join(d, f'{f}{tag}.csv'))
    shutil.copy(os.path.join(s, 'conditions.txt'), d)
    hz = dict(p.split('=') for p in cfg.split(';') if p).get('sim_tick', '0')
    lp = os.path.join(s, 'user.ltx.after')   # out/s2_offl has none (the first 4-core run's copy went to the wrong folder name): the run dir's file stands in
    ltx = open(lp if os.path.exists(lp) else os.path.join(HERE, '..', 'run', 'appdata', 'user.ltx'), 'rb').read().decode('utf-8', 'replace')
    ltx = re.sub(r'(?m)^r__framelimit \d+', 'r__framelimit ' + dict(p.split('=') for p in cfg.split(';') if p).get('r__framelimit', '0'), ltx)
    open(os.path.join(d, 'user.ltx.after'), 'wb').write(re.sub(r'(?m)^sim_tick \d+', f'sim_tick {hz}', ltx).encode())
    L = open(os.path.join(s, 'xray.log'), 'rb').read().decode('utf-8', 'replace').split('\n')
    a = next(i for i, l in enumerate(L) if '[xrprof] capture' in l and 'started' in l); b = next(i for i, l in enumerate(L) if '[xrprof] capture' in l and 'finished' in l)
    if where == 'edges': L = L[:a] + [POISON + '\r'] + L[a:b + 1] + [POISON + '\r'] + L[b + 1:]
    if where == 'middle':
        st = [i for i in range(a, b) if '[SIMTICK]' in L[i]]; k = st[len(st) // 2]; L = L[:k + 1] + [POISON + '\r'] + L[k + 1:]
    open(os.path.join(d, 'xray.log'), 'wb').write('\n'.join(L).encode())
def check(root, tag, cfg, fixed, frames):
    p = subprocess.run([os.path.join(HERE, 's2_series_b.sh'), '--check', tag, cfg, str(fixed), str(frames)], capture_output=True, text=True, env=dict(os.environ, S2_EXE_SHA='x', S2_CHECK_ROOT=root))
    return p.stdout.strip()
def score(root):
    pj = os.path.join(root, '..', 'empty.json'); open(pj, 'w').write('{}')
    t = subprocess.run([sys.executable, '-I', os.path.join(HERE, 's2_score_b.py'), root, pj, os.path.join(root, '..', 'score.txt')], capture_output=True, text=True).stdout
    return [l for l in t.split('\n') if not l.startswith('S2 SCORE')]
for tag, base, cfg, fixed, frames in TYPES:
    on = 'sim_tick' in cfg; gated = on and not fixed; R = {}
    for v in ('clean', 'edges') + (('middle',) if gated else ()):
        r = os.path.join(W, tag, v, 'root'); os.makedirs(r); make(r, tag, base, cfg, None if v == 'clean' else v)
        R[v] = (check(r, tag, cfg, fixed, frames), score(r))
    c0, s0 = R['clean']; n0 = re.sub(r'SIMTICK lines \d+', 'SIMTICK lines N', c0)
    want(f'{tag}: check passes, scorer finds the arm valid', c0.startswith('check ok') and any(l.startswith('ARM ') for l in s0) and not any('INVALID' in l for l in s0), f'{c0} | {s0[:1]}')
    arm = re.sub(r'^(s2_(?:off|t\d+))l$', r'\1L', re.sub(r'(_\d)?(_rr)?$', '', tag))   # the registered arm name, as the prediction keys spell it (capital L for 4 cores)
    want(f'{tag}: the scorer files the run under the registered arm name {arm}', any(l.startswith(f'ARM {arm} ') for l in s0), [l for l in s0 if l.startswith('ARM')])
    c1, s1 = R['edges']
    if on: want(f'{tag}: warm-up and quit lines change neither the check nor any scorer value', re.sub(r'SIMTICK lines \d+', 'SIMTICK lines N', c1) == n0 and s1 == s0, f'{c1} | {[l for l in s1 if l not in s0][:3]}')
    else: want(f'{tag}: a gate line in a run that asked for stock FAILS the check', c1.startswith('check FAIL') and 'SIMTICK-line-in-stock-run' in c1, c1)
    if gated:
        dr = lambda S: next((float(l.split()[1]) for l in S if l.strip().startswith('dropped')), None)
        want(f'{tag}: the same line inside the capture IS seen by the scorer (dropped {dr(s0)} -> {dr(R["middle"][1])})', dr(R['middle'][1]) is not None and dr(s0) is not None and dr(R['middle'][1]) >= dr(s0) + 999)
# the REAL 4-core run folder as the bench left it (lowercase), checked under its registered tag
c = check(os.path.join(W, 's2_offl_rr', 'clean', 'root'), 's2_offl_rr', '', 0, 12000)
want('the real 4-core run (copy of out/s2_offl) checked as s2_offl_rr: passes with 12000 updates', c.startswith('check ok (updates 12000 of 12000'), c)
c = subprocess.run([os.path.join(HERE, 's2_series_b.sh'), '--check', 's2_nosuchL', '', '0', '12000'], capture_output=True, text=True, env=dict(os.environ, S2_EXE_SHA='x', S2_CHECK_ROOT=ROOT)).stdout.strip()
want('a run whose files are missing FAILS by name, not as "0 updates"', c.startswith('check FAIL: run files missing'), c)
print('PHASE SELFTEST ' + ('PASS' if not bad else 'FAIL: ' + '; '.join(bad))); sys.exit(1 if bad else 0)
