#!/usr/bin/env python3
"""Self-test of tools/s4c_input_score.py. Usage: python3 -I tools/s4c_input_selftest.py <out root> <empty work dir>
Fixtures: the real 4b run folders s4b_n30_1 and s4b_n60_1 under the names the bench gives the part C runs, with the
sites the 4b build does not have added per case."""
import sys, os, shutil, subprocess
SRC, WORK = sys.argv[1], sys.argv[2]; HERE = os.path.dirname(os.path.abspath(__file__))
MAP = {'s4c_i30_1': 's4b_n30_1', 's4c_i60_1': 's4b_n60_1'}
def build(name, f, mut=None):
    root = os.path.join(WORK, name)
    for tag, src in MAP.items():
        d = os.path.join(root, tag); os.makedirs(d)
        for x in os.listdir(os.path.join(SRC, src)):
            if x.startswith(('perf_sites_', 'perf_frames_')) or x == 'conditions.txt': shutil.copy(os.path.join(SRC, src, x), os.path.join(d, x.replace(src, tag)))
        p = os.path.join(d, f'perf_sites_{tag}.csv'); b = open(p, 'rb').read().decode('latin-1')
        N = int(b.split('#frames,')[1].split()[0].strip('\r')); U = int([l for l in b.split('\n') if l.startswith('Process seqFrame,')][0].split(',')[3])
        for site, v in zip(('sim_hud_anim', 'sim_mouse_poll', 'sim_mouse_read'), f(tag, N, U)):
            if v is not None: b += f'{site},x.cpp,1,{v},1.0000,1.0000,0,0.0000,0.0000\r\n'
        open(p, 'wb').write(b.encode('latin-1'))
    if mut: mut(root)
    return root
def run(root):
    r = subprocess.run([sys.executable, '-I', os.path.join(HERE, 's4c_input_score.py'), root], capture_output=True, text=True); return r.returncode, r.stdout
def cut_frames(r):
    p = os.path.join(r, 's4c_i60_1', 'perf_frames_s4c_i60_1.csv'); L = open(p, 'rb').read().split(b'\n'); open(p, 'wb').write(b'\n'.join(L[:len(L) // 2]))
CASES = [
 ('good, mouse read succeeded', lambda t, N, U: (N, N - U, N - U), None, 0, 'RESULT: PASS'),
 ('good, mouse never read (device not acquired)', lambda t, N, U: (N, N - U, None), None, 0, 'mouse reads that succeeded 0'),
 ('mouse poll never reached', lambda t, N, U: (N, 0, None), None, 1, 's4c_i30_1: FAIL'),
 ('mouse poll site missing (input path off)', lambda t, N, U: (N, None, None), None, 2, 'site missing: sim_mouse_poll'),
 ('animation step only per tick', lambda t, N, U: (U, N - U, None), None, 1, 's4c_i30_1: FAIL'),
 ('run died mid capture (frames file short)', lambda t, N, U: (N, N - U, None), cut_frames, 2, 's4c_i60_1: frames file has'),
 ('run folder missing', lambda t, N, U: (N, N - U, None), lambda r: shutil.rmtree(os.path.join(r, 's4c_i30_1')), 2, 'missing file'),
 ('not clean is reported, not gated', lambda t, N, U: (N, N - U, None), lambda r: open(os.path.join(r, 's4c_i30_1', 'conditions.txt'), 'w').write('VERDICT: CONFOUNDED\n'), 0, 'bench verdict [VERDICT: CONFOUNDED]'),
]
ok = 0
for i, (name, f, mut, rc, needle) in enumerate(CASES):
    got, out = run(build(f'case{i}', f, mut)); hit = got == rc and needle in out; ok += hit
    print(('ok   ' if hit else 'BAD  ') + f'{name}: exit {got} (want {rc}), "{needle}" ' + ('found' if needle in out else 'NOT found'))
    if not hit: print(out)
print(f"SELFTEST {'PASS' if ok == len(CASES) else 'FAIL'} {ok}/{len(CASES)}"); sys.exit(0 if ok == len(CASES) else 1)
