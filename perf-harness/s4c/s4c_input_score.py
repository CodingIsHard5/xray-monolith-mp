#!/usr/bin/env python3
"""S2 stage 4c part C scorer (PREREG-S2-4c section 4): the build with input ON. Usage: python3 -I tools/s4c_input_score.py <out root> [<result file>]
Runs s4c_i30_1, s4c_i60_1 (read from <tag>_rr when <tag> is not clean and the re-run exists: the series' rule).
Per run, PASS when all hold: the capture finished (frames file rows = frames); updates U < frames N (gated);
camera calls in N-2..N; first-person animation steps in N-2..N; mouse polls on frames without a tick: N-U-2 <= polls <= N.
Reported, NO gate: mouse reads that succeeded (0 is possible headless: the device may not be acquired), frame ms.
The clean/confounded verdict of the bench is reported, not gated: this part measures no time.
A missing file or site is named and gives NO VERDICT (exit 2); only sim_mouse_read may be absent (= 0)."""
import sys, os
ROOT = sys.argv[1]; OUT = sys.argv[2] if len(sys.argv) > 2 else None
NEED = ['Process seqFrame', 'CActor UpdateCL', 'CActor::cam_Update', 'sim_hud_anim', 'sim_mouse_poll']
lines = []; miss = []; res = []
def rd(p):
    if not os.path.isfile(p): miss.append('missing file ' + p); return []
    return open(p, 'rb').read().decode('latin-1').replace('\r', '').split('\n')
for name in ('s4c_i30_1', 's4c_i60_1'):
    tag = name; c = os.path.join(ROOT, name, 'conditions.txt')
    clean = lambda p: any(l.strip() == 'VERDICT: clean' for l in rd(p))
    if os.path.isfile(c) and not clean(c) and os.path.isdir(os.path.join(ROOT, name + '_rr')): tag = name + '_rr'; lines.append(f'SUBSTITUTED {name}: not clean, read from its in-slice re-run {tag}')
    d = os.path.join(ROOT, tag); S = {}; n = tot = None; n0 = len(miss)
    for l in rd(os.path.join(d, f'perf_sites_{tag}.csv')):
        if l.startswith('#frames,'): n = int(l.split(',')[1])
        elif l.startswith('#total_ms,'): tot = float(l.split(',')[1])
        elif l and not l.startswith(('#', 'name,')):
            f = l.split(',')
            if len(f) >= 9 and f[0] not in S: S[f[0]] = int(float(f[-6]) + float(f[-3]))
    F = [l for l in rd(os.path.join(d, f'perf_frames_{tag}.csv')) if l and l[0].isdigit()]
    v = [l.strip() for l in rd(os.path.join(d, 'conditions.txt')) if l.startswith('VERDICT')]
    for s in NEED:
        if s not in S: miss.append(f'{name}: site missing: {s}')
    if not n or not tot: miss.append(f'{name}: no finished capture')
    elif len(F) != n: miss.append(f'{name}: frames file has {len(F)} rows for {n} frames')
    if len(miss) > n0: continue
    U = S['Process seqFrame']; cam = S['CActor::cam_Update']; hud = S['sim_hud_anim']; poll = S['sim_mouse_poll']; read = S.get('sim_mouse_read', 0)
    ok = 0 < U < n and S['CActor UpdateCL'] == U and n - 2 <= cam <= n and n - 2 <= hud <= n and n - U - 2 <= poll <= n
    res.append(ok)
    lines.append(f"{name}: {'PASS' if ok else 'FAIL'}  frames {n}, updates {U}, camera {cam}, animation steps {hud}, mouse polls {poll} (frames without a tick at least {n - U}), "
                 f"mouse reads that succeeded {read} (reported), frame {tot / n:.2f} ms, bench verdict [{'; '.join(v) or 'none'}]")
if miss:
    lines += ['NO VERDICT: ' + m for m in miss]; rc = 2
else:
    rc = 0 if all(res) else 1
lines.append('RESULT: ' + ('NO VERDICT' if rc == 2 else 'PASS' if rc == 0 else 'FAIL'))
txt = '\n'.join(lines) + '\n'; sys.stdout.write(txt)
if OUT: open(OUT, 'w').write(txt)
sys.exit(rc)
