#!/usr/bin/env python3
"""First joint T3 pre-flight, X side (PREREG-B1-T2-xray-side section 40). <out dir> is t2_xray.sh's own out dir (one boot). Pre-flight B's conditions with: P2 >= 6000, HOLD <= 15, STANCE lines >= 10, plus no restart row; VITALS reported. Run ONCE.  t3_pfA_check.py <out dir of t2_preflight.sh> <track.csv>
PASS: (1) no Lua error naming zz_p1_puppet and t2_xray rc 0; (2) >= 2000 P2 lines; (3) no GUARD line and no two
consecutive applied targets more than 5.0 m apart; (4) at most 10 HOLD lines.
Reported: horizontal distance after the set, actor height minus the track height at that x,z time, snaps, rings."""
import sys, os, re, bisect
O, TRK = sys.argv[1], sys.argv[2]
rd = lambda p: open(p, 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n') if os.path.exists(p) else []
M = rd(os.path.join(O, 'boot0/meta_t2.csv')); X = rd(os.path.join(O, 'boot0/tb/xray.log')); LG = [l for l in rd(os.path.join(O, 't2_xray.log')) if l]
P2 = [l.split(',') for l in M if l.startswith('P2,')]; G = [l for l in M if l.startswith('GUARD,')]; H = [l for l in M if l.startswith('HOLD,')]
lua_err = [l for l in X if re.search(r'LUA error|lua error', l) and 'zz_p1_puppet' in l] + [X[i] for i in range(len(X)) if 'zz_p1_puppet.script (line' in X[i] and not any("doesn't exist" in X[j] or 'nil function' in X[j] for j in range(max(0, i - 8), i))]
rc0 = bool(LG) and LG[-1].rstrip().endswith('rc=0')
step = [((float(b[7]) - float(a[7])) ** 2 + (float(b[8]) - float(a[8])) ** 2 + (float(b[9]) - float(a[9])) ** 2) ** .5 for a, b in zip(P2, P2[1:])]
T = [[float(v) for v in l.split(',')[:4]] for l in rd(TRK)[1:] if l]
# height reference: the track point nearest in x,z to the applied target (the writer's clock is not in the P2 line)
import math
def ref_y(x, z):
    best = min(T[:1000], key=lambda r: (r[1] - x) ** 2 + (r[3] - z) ** 2); return best[2], math.hypot(best[1] - x, best[3] - z)
dh = []; 
for q in P2:
    ry, d = ref_y(float(q[7]), float(q[9]))
    if d <= 0.6: dh.append(float(q[14]) - ry)
pc = lambda v, p: sorted(v)[min(len(v) - 1, int(p * len(v)))] if v else None
hd = [float(q[4]) for q in P2]
res = [('1 no Lua error, rc 0', not lua_err and rc0, f'lua errors {len(lua_err)}, rc0 {rc0}'),
       ('2 P2 lines >= 6000', len(P2) >= 6000, str(len(P2))),
       ('3 no GUARD, no step > 5 m', not G and (max(step) if step else 0) <= 5.0, f'GUARD {len(G)}, max step {max(step) if step else 0:.3f} m'),
       ('4 HOLD <= 15', len(H) <= 15, str(len(H))),
       ('5 actor height = node height + 0.05 (+-0.01) on every P2', bool(P2) and all(abs(float(q[14]) - float(q[11]) - 0.05) <= 0.01 for q in P2), f'worst {max((abs(float(q[14]) - float(q[11]) - 0.05) for q in P2), default=None)}'),
       ('6 no RAY_ERROR line', not any(l.startswith('RAY_ERROR') for l in M), '')]
ST = [l for l in M if l.startswith('STANCE,')]; UNS = [l for l in M if l.startswith('STANCE_UNSUPPORTED')]
Q = [q for q in P2 if len(q) > 19]
api_bad = sum(1 for q in Q if q[19] != q[15]); mv_ok = sum(1 for q in Q if q[18] == q[15]); unread = sum(1 for q in Q if q[18] == '-1')
# every stance change: the real movement state must equal the new stance on some pose within the next 40 poses (2 s)
late = 0; chg = 0
for i in range(1, len(Q)):
    if Q[i][15] != Q[i - 1][15] and i + 40 <= len(Q):   # a change in the last 2 s of data cannot be judged
        chg += 1
        if not any(Q[j][18] == Q[i][15] for j in range(i, min(len(Q), i + 40)) if Q[j][15] == Q[i][15]): late += 1
MS = [l for l in M if l.startswith('MOVESUM')]
res += [('7 STANCE lines >= 10 and no STANCE_UNSUPPORTED', len(ST) >= 10 and not UNS, f'{len(ST)} STANCE, {len(UNS)} unsupported'),
        ('8 get_actor_stance = pose stance on every P2', len(Q) == len(P2) and bool(Q) and api_bad == 0, f'{api_bad} differ of {len(Q)} (P2 {len(P2)})'),
        ('9 real movement state = pose stance on >= 80 % of P2, and after every stance change within 2 s', bool(Q) and mv_ok >= 0.8 * len(Q) and chg > 0 and late == 0, f'{mv_ok} of {len(Q)} match; {chg} changes, {late} not followed within 2 s'),
        ('10 movement state readable on every P2', bool(Q) and unread == 0, f'{unread} unread')]
txt = '\n'.join(f'{n}: {"PASS" if ok else "FAIL"} ({d})' for n, ok, d in res)
ab = [abs(v) for v in dh]
txt += f'\nreported: horizontal dist p50 {pc(hd, .5)} p95 {pc(hd, .95)} max {max(hd) if hd else None} m'
txt += f'\nreported: actor height - track height over {len(dh)} poses: p50 {pc(dh, .5):.3f} p05 {pc(dh, .05):.3f} p95 {pc(dh, .95):.3f}; |.| p95 {pc(ab, .95):.3f} max {max(ab):.3f} m' if dh else '\nreported: no height samples'
txt += f'\nreported: snaps {sum(int(q[13]) for q in P2)}; rings 0/1/2 {[sum(1 for q in P2 if q[12] == str(k)) for k in (0, 1, 2)]}; STANCE lines {sum(1 for l in M if l.startswith("STANCE,"))}; STANCE_UNSUPPORTED {sum(1 for l in M if l.startswith("STANCE_UNSUPPORTED"))}'
rayd = [float(q[16]) - float(q[11]) for q in P2 if len(q) > 16 and float(q[16]) > -9000]; dr = [float(q[17]) for q in P2 if len(q) > 17]
txt += (f'\nreported: ray hits {len(rayd)} of {len(P2)}; ray ground - node height p05 {pc(rayd, .05):.3f} p50 {pc(rayd, .5):.3f} p95 {pc(rayd, .95):.3f} min {min(rayd):.3f} max {max(rayd):.3f} m' if rayd else f'\nreported: ray hits 0 of {len(P2)}')
txt += (f'\nreported: height drift between sets: nonzero (>1 mm) in {sum(1 for v in dr if abs(v) > 0.001)} of {len(dr)}; min {min(dr):.3f} max {max(dr):.3f} m' if dr else '')
txt += f'\nreported: {MS[-1] if MS else "no MOVESUM line"}'
RS = [l for l in rd(os.path.join(O, 'restarts.csv'))[1:] if l]
res.append(('11 no restart row', not RS, f'{len(RS)} rows'))
txt = '\n'.join(f'{n}: {"PASS" if ok else "FAIL"} ({d})' for n, ok, d in res) + txt[txt.index('\nreported'):]
txt += '\nreported: ' + next((l for l in M if l.startswith('VITALS')), 'no VITALS line')
txt += f'\nJOINT PRE-FLIGHT X SIDE: {"PASS" if all(r[1] for r in res) else "FAIL"}\n'
print(txt, end=''); open(os.path.join(O, 't3_jpf_xray_check.txt'), 'w').write(txt)
