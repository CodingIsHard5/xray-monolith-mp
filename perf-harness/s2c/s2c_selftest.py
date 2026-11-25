#!/usr/bin/env python3
"""Self-test of s2c_score.py on REAL part B run folders (engine bytes: out/s2_c30, out/s2_t30_1, out/s2_t30_2), copied
under the E1 tags as the bench would name them, with the update-side times of the middle arms scaled to known shapes.
  s2c_selftest.py <out root with the part B runs> <work dir>"""
import sys, os, shutil, subprocess
ROOT, W = sys.argv[1:3]; HERE = os.path.dirname(os.path.abspath(__file__)); os.makedirs(W); bad = []
UPD = ('Process seqFrame', 'Render: Frame Move', 'Sheduler Deferred', 'seqParallel', 'seqFrameMT', 'CScriptBinder::shedule_Update', 'stalker/schedule_update/think/brain')
def mk(root, tag, base, scale=1.0, updates=None, verdict=None, drop=None, rows=None):
    d = os.path.join(root, tag); os.makedirs(d); s = os.path.join(ROOT, base); out = []
    for l in open(os.path.join(s, f'perf_sites_{base}.csv'), 'rb').read().decode('utf-8', 'replace').split('\n'):
        f = l.rstrip('\r').split(',')
        if len(f) >= 9 and f[0] in UPD:
            if f[0] == drop: continue
            for i in (-5, -4, -2, -1): f[i] = f'{float(f[i]) * scale:.4f}'
            if updates and f[0] == 'Process seqFrame': f[-6] = str(updates)
            l = ','.join(f) + '\r'
        out.append(l)
    open(os.path.join(d, f'perf_sites_{tag}.csv'), 'wb').write('\n'.join(out).encode())
    fr = open(os.path.join(s, f'perf_frames_{base}.csv'), 'rb').read()
    open(os.path.join(d, f'perf_frames_{tag}.csv'), 'wb').write(fr if rows is None else b'\n'.join(fr.split(b'\n')[:rows]))
    c = open(os.path.join(s, 'conditions.txt'), 'rb').read().decode()
    open(os.path.join(d, 'conditions.txt'), 'w').write(c if verdict is None else c.replace('VERDICT: clean', verdict))
def series(name, e31, e60, e120, eu=None, **kw):
    """c30 real; uncapped = the real gated runs (their real extra, or scaled to eu); the capped gated arms = the real
    capped run scaled so that their extra over c30 is e31 / e60 / e120 (their rate check uses the capped run's own 30/s)."""
    r = os.path.join(W, name); os.makedirs(r)
    for i in (1, 2):
        mk(r, f's2c_c30_{i}', 's2_c30')
        mk(r, f's2c_g30x31_{i}', 's2_c30', 1 + e31); mk(r, f's2c_g30x60_{i}', 's2_c30', 1 + e60); mk(r, f's2c_g30x120_{i}', 's2_c30', 1 + e120)
        if eu is None: mk(r, f's2c_g30u_{i}', f's2_t30_{i}')
        else: mk(r, f's2c_g30u_{i}', 's2_c30', 1 + eu)
    for tag, k in kw.get('edit', {}).items():
        shutil.rmtree(os.path.join(r, tag))
        if k != 'remove': mk(r, tag, 's2_c30', **k)
    p = subprocess.run([sys.executable, '-I', os.path.join(HERE, 's2c_score.py'), r], capture_output=True, text=True)
    return p.stdout, p.returncode
def want(name, t, rc, verdict, code, also=''):
    ok = t.strip().split('\n')[-1].startswith('S2-C E1: ' + verdict) and rc == code and also in t
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else '\n' + t[-900:])); bad.append(name) if not ok else None
t, rc = series('real_step', 0.00, 0.27, 0.29); want('real gated runs as the uncapped arm (about +30 %), middle arms a STEP', t, rc, 'H2 STEP', 0)
print('     ' + [l for l in t.split('\n') if l.startswith('E ')][0])
t, rc = series('rise', 0.00, 0.05, 0.15); want('middle arms a RISE (x60 +5 %, x120 +15 %)', t, rc, 'H1 RISE', 0)
t, rc = series('middle', 0.00, 0.15, 0.25); want('x60 at half the extra', t, rc, 'NOT SEPARATED', 0)
t, rc = series('notmono', 0.00, 0.08, 0.02); want('x120 below x60 (not a rise)', t, rc, 'NOT SEPARATED', 0)
t, rc = series('absent', 0.00, 0.02, 0.05, eu=0.08); want('uncapped extra only 8 %', t, rc, 'EFFECT NOT REPRODUCED', 0)
t, rc = series('beat', 0.20, 0.27, 0.29); want('one frame per tick already +20 %', t, rc, 'GATE PATH OR BEAT', 0)
t, rc = series('m_missing', 0, .05, .15, edit={'s2c_g30x60_2': 'remove'}); want('a run folder missing: named, no verdict', t, rc, 'NO VERDICT', 2, 'INVALID s2c_g30x60_2: file missing')
t, rc = series('m_dirty', 0, .05, .15, edit={'s2c_c30_1': dict(verdict='VERDICT: CONFOUNDED (x)')}); want('conditions not clean', t, rc, 'NO VERDICT', 2, 'INVALID s2c_c30_1: conditions not clean')
t, rc = series('m_rate', 0, .05, .15, edit={'s2c_g30x31_1': dict(updates=1500)}); want('rate off (1500 updates)', t, rc, 'NO VERDICT', 2, 'not within 3 % of 30')
t, rc = series('m_site', 0, .05, .15, edit={'s2c_g30u_2': dict(drop='seqFrameMT')}); want('a site missing', t, rc, 'NO VERDICT', 2, 'sites missing: seqFrameMT')
t, rc = series('m_rows', 0, .05, .15, edit={'s2c_g30x120_1': dict(rows=500)}); want('frames file short', t, rc, 'NO VERDICT', 2, 'frames file has')
t, rc = series('m_spread', 0, .05, .15, edit={'s2c_g30x60_1': dict(scale=1.30)}); want('replicates 22 % apart', t, rc, 'NO VERDICT', 2, 'spread')
print('S2C SELFTEST ' + ('PASS' if not bad else 'FAIL: ' + '; '.join(bad))); sys.exit(1 if bad else 0)
