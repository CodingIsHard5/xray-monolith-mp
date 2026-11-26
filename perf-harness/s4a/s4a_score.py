#!/usr/bin/env python3
"""S2 stage 4a scorer (PREREG-S2-4a section 3). Usage: python3 -I tools/s4a_score.py <out root> [<result file>]
Reads <root>/<tag>/perf_sites_<tag>.csv, perf_frames_<tag>.csv, conditions.txt for the ten tags of tools/s4a_order.txt.
A missing file, site or clean verdict is named and gives NO VERDICT (exit 2); nothing missing is read as 0.
Checks (n = new build SimTick15, p = previous build SimTick14):
 V1 view every frame: each n30/n60 run: camera calls in N-2..N, actor updates == updates U, U < N
 V2 off is the stock call pattern: s4a_noff_1: camera calls == actor updates == U == N
 V3 frame time: mean(n)/mean(p) <= 1.05 at 30 and at 60 (two runs each; arm spread > 8 % = NO VERDICT for that rate)
 V4 tick cost: sim ms/tick mean(n)/mean(p) <= 1.05 at 30 and at 60
 V5 off cost: frame time noff/poff within 0.95..1.05
Exit 0 all PASS, 1 any FAIL, 2 NO VERDICT."""
import sys, os
ROOT = sys.argv[1]; OUT = sys.argv[2] if len(sys.argv) > 2 else None
CAM = 'CActor::cam_Update'; ACT = 'CActor UpdateCL'; UPD = 'Process seqFrame'
GT = ['Sheduler Deferred', 'seqParallel', 'seqFrameMT']
NEW = ['s4a_n30_1', 's4a_n30_2', 's4a_n60_1', 's4a_n60_2', 's4a_noff_1']
OLD = ['s4a_p30_1', 's4a_p30_2', 's4a_p60_1', 's4a_p60_2', 's4a_poff_1']
lines = []; miss = []; subs = []
def say(s): lines.append(s)
def rd(p):
    if not os.path.isfile(p): miss.append('missing file ' + p); return []
    return open(p, 'rb').read().decode('latin-1').replace('\r', '').split('\n')
def arm(name, new):
    # Amended before any run (prereg 3.1): the series script (s2_series_b.sh) excludes a run whose conditions are not
    # clean and re-runs it in the slice as <tag>_rr. The scorer follows that one rule: such a run is read from <tag>_rr.
    tag = name; c = os.path.join(ROOT, name, 'conditions.txt')
    if os.path.isfile(c) and not any(l.strip() == 'VERDICT: clean' for l in open(c, 'rb').read().decode('latin-1').replace('\r', '').split('\n')) \
       and os.path.isdir(os.path.join(ROOT, name + '_rr')):
        tag = name + '_rr'; subs.append(f'{name}: not clean, read from its in-slice re-run {tag}')
    d = os.path.join(ROOT, tag); S = {}; n = tot = None; n0 = len(miss)
    for l in rd(os.path.join(d, f'perf_sites_{tag}.csv')):
        if l.startswith('#frames,'): n = int(l.split(',')[1])
        elif l.startswith('#total_ms,'): tot = float(l.split(',')[1])
        elif l and not l.startswith(('#', 'name,')):
            f = l.split(',')
            if len(f) >= 9 and f[0] not in S: S[f[0]] = (float(f[-6]), float(f[-5]), float(f[-3]), float(f[-2]))
    F = [float(l.split(',')[1]) for l in rd(os.path.join(d, f'perf_frames_{tag}.csv')) if l and l[0].isdigit()]
    if not any(l.strip() == 'VERDICT: clean' for l in rd(os.path.join(d, 'conditions.txt'))): miss.append(f'{tag}: conditions not clean')
    for s in [UPD, ACT] + GT + ([CAM] if new else []):
        if s not in S: miss.append(f'{tag}: site missing: {s}')
    if not n or not tot or not F: miss.append(f'{tag}: no finished capture')
    elif len(F) != n: miss.append(f'{tag}: frames file has {len(F)} rows for {n} frames')
    if len(miss) > n0: return None
    U = S[UPD][0]
    if U <= 0: miss.append(f'{tag}: no updates'); return None
    F.sort()
    return dict(tag=name, N=n, U=int(U), act=int(S[ACT][0]), cam=int(S[CAM][0]) if new else None, cam_ms=S[CAM][1] if new else None,
                ft=tot / n, p99=F[int(0.99 * (len(F) - 1))], tick=(S[UPD][1] + sum(S[g][1] + S[g][3] for g in GT)) / U)
A = {t: arm(t, True) for t in NEW}; A.update({t: arm(t, False) for t in OLD})
def finish(rc):
    txt = '\n'.join(lines) + '\n'; sys.stdout.write(txt)
    if OUT: open(OUT, 'w').write(txt)
    sys.exit(rc)
if miss:
    for m in miss: say('NO VERDICT: ' + m)
    finish(2)
for x in subs: say('SUBSTITUTED ' + x)
say('tag          frames updates actor  camera  frame ms  p99 ms  sim ms/tick  camera ms/call')
for t in NEW + OLD:
    a = A[t]; say(f"{t:12s} {a['N']:6d} {a['U']:6d} {a['act']:6d} {str(a['cam']) if a['cam'] is not None else '-':>6s}  {a['ft']:7.3f} {a['p99']:7.2f}  {a['tick']:7.3f}      " + (f"{a['cam_ms'] / max(a['cam'], 1):.4f}" if a['cam'] else '-'))
res = []; nov = False
def chk(name, ok, text): res.append(ok); say(f"{name}: {'PASS' if ok else 'FAIL'}  {text}")
for t in NEW[:4]:
    a = A[t]; chk(f'V1 {t}', a['N'] - 2 <= a['cam'] <= a['N'] and a['act'] == a['U'] and a['U'] < a['N'], f"camera {a['cam']} of {a['N']} frames, actor {a['act']} of {a['U']} updates")
a = A['s4a_noff_1']; chk('V2 s4a_noff_1', a['cam'] == a['act'] == a['U'] == a['N'], f"camera {a['cam']}, actor {a['act']}, updates {a['U']}, frames {a['N']}")
for hz in ('30', '60'):
    n = [A[f's4a_n{hz}_{i}'] for i in (1, 2)]; p = [A[f's4a_p{hz}_{i}'] for i in (1, 2)]
    for key, name, lim in (('ft', 'V3 frame time', 1.05), ('tick', 'V4 tick cost', 1.05)):
        sp = max(abs(x[0][key] - x[1][key]) / min(x[0][key], x[1][key]) for x in (n, p))
        r = (n[0][key] + n[1][key]) / (p[0][key] + p[1][key])
        if sp > 0.08: nov = True; say(f'{name} tick {hz}: NO VERDICT  arm spread {sp * 100:.1f} % > 8 % (ratio {r:.3f})')
        else: chk(f'{name} tick {hz}', r <= lim, f'new/previous = {r:.3f} (limit {lim}), arm spread {sp * 100:.1f} %')
r = A['s4a_noff_1']['ft'] / A['s4a_poff_1']['ft']; chk('V5 off frame time', 0.95 <= r <= 1.05, f'new/previous = {r:.3f} (one run each)')
say('RESULT: ' + ('FAIL' if not all(res) else 'NO VERDICT (part)' if nov else 'PASS') + f" ({sum(res)} of {len(res)} checks passed)")
finish(1 if not all(res) else 2 if nov else 0)
