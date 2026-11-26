#!/usr/bin/env python3
"""Self-test of tools/s4a_score.py. Usage: python3 -I tools/s4a_selftest.py <out root with part B runs> <empty work dir>
Fixtures are REAL part B run folders (s2_t30_1/2, s2_t60_1/s2_t60_2_rr, s2_off_1/2) copied under the names the bench
would give the stage 4a runs, with the one site the old build does not have (the camera site) added per case."""
import sys, os, shutil, subprocess
SRC, WORK = sys.argv[1], sys.argv[2]; HERE = os.path.dirname(os.path.abspath(__file__))
MAP = {'s4a_n30_1': 's2_t30_1', 's4a_n30_2': 's2_t30_2', 's4a_p30_1': 's2_t30_2', 's4a_p30_2': 's2_t30_1', 's4a_n60_1': 's2_t60_1', 's4a_n60_2': 's2_t60_2_rr',
       's4a_p60_1': 's2_t60_2_rr', 's4a_p60_2': 's2_t60_1', 's4a_noff_1': 's2_off_1', 's4a_poff_1': 's2_off_2'}
def build(name, cam=lambda tag, N, U: N, mut=None):
    root = os.path.join(WORK, name)
    for tag, src in MAP.items():
        d = os.path.join(root, tag); os.makedirs(d)
        for f in os.listdir(os.path.join(SRC, src)):
            if f.startswith(('perf_sites_', 'perf_frames_')) or f == 'conditions.txt':
                shutil.copy(os.path.join(SRC, src, f), os.path.join(d, f.replace(src, tag)))
        p = os.path.join(d, f'perf_sites_{tag}.csv'); b = open(p, 'rb').read().decode('latin-1')
        N = int(b.split('#frames,')[1].split()[0].strip('\r')); U = int([l for l in b.split('\n') if l.startswith('Process seqFrame,')][0].split(',')[3])
        c = cam(tag, N, U) if '_n' in tag else None
        if c is not None: b += f'CActor::cam_Update,x.cpp,481,{c},12.0000,12.0000,0,0.0000,0.0000\r\n'
        open(p, 'wb').write(b.encode('latin-1'))
    if mut: mut(root)
    return root
def run(root):
    r = subprocess.run([sys.executable, '-I', os.path.join(HERE, 's4a_score.py'), root], capture_output=True, text=True); return r.returncode, r.stdout
def scale_frames(root, tag, k):
    p = os.path.join(root, tag, f'perf_sites_{tag}.csv'); b = open(p, 'rb').read().decode('latin-1').split('\n')
    b = ['#total_ms,%.3f\r' % (float(l.split(',')[1]) * k) if l.startswith('#total_ms,') else l for l in b]; open(p, 'wb').write('\n'.join(b).encode('latin-1'))
good = lambda t, N, U: U if 'off' in t else N
CASES = [
 ('good: camera every frame, off = per update', dict(cam=good), 0, 'RESULT: PASS'),
 ('camera only per tick in a gated run (stage 4a not active)', dict(cam=lambda t, N, U: U), 1, 'V1 s4a_n30_1: FAIL'),
 ('camera 50 frames short in one run', dict(cam=lambda t, N, U: (U if 'off' in t else N - 50 if t == 's4a_n60_2' else N)), 1, 'V1 s4a_n60_2: FAIL'),
 ('camera twice per frame', dict(cam=lambda t, N, U: (U if 'off' in t else 2 * N)), 1, 'V1 s4a_n30_1: FAIL'),
 ('off run: camera called more than the actor', dict(cam=lambda t, N, U: N + 1 if 'off' in t else N), 1, 'V2 s4a_noff_1: FAIL'),
 ('camera site missing in the new build', dict(cam=lambda t, N, U: None), 2, 'site missing: CActor::cam_Update'),
 ('a run folder missing', dict(cam=good, mut=lambda r: shutil.rmtree(os.path.join(r, 's4a_p60_2'))), 2, 'missing file'),
 ('a run not clean', dict(cam=good, mut=lambda r: open(os.path.join(r, 's4a_n30_2', 'conditions.txt'), 'w').write('VERDICT: NOT clean\n')), 2, 's4a_n30_2: conditions not clean'),
 ('new build 10 % slower frames at 30', dict(cam=good, mut=lambda r: [scale_frames(r, t, 1.10) for t in ('s4a_n30_1', 's4a_n30_2')]), 1, 'V3 frame time tick 30: FAIL'),
 ('one new run 20 % slower (spread)', dict(cam=good, mut=lambda r: scale_frames(r, 's4a_n60_1', 1.20)), 2, 'V3 frame time tick 60: NO VERDICT'),
 ('off run 10 % slower in the new build', dict(cam=good, mut=lambda r: scale_frames(r, 's4a_noff_1', 1.10)), 1, 'V5 off frame time: FAIL'),
]
ok = 0
for i, (name, kw, rc, needle) in enumerate(CASES):
    got, out = run(build(f'case{i}', **kw)); hit = got == rc and needle in out; ok += hit
    print(('ok   ' if hit else 'BAD  ') + f'{name}: exit {got} (want {rc}), "{needle}" ' + ('found' if needle in out else 'NOT found'))
    if not hit: print(out)
print(f"SELFTEST {'PASS' if ok == len(CASES) else 'FAIL'} {ok}/{len(CASES)}"); sys.exit(0 if ok == len(CASES) else 1)
