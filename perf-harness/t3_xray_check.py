#!/usr/bin/env python3
"""T3 proper, X side (PREREG-B1-T2-xray-side section 43). <out dir> is t2_xray.sh's own out dir (one boot). Run ONCE.
  t3_xray_check.py <out dir> <track.csv>
PASS needs all of 1-14: the joint pre-flight's 1-11 with P2 >= 17000, HOLD <= 45, STANCE lines >= 25; (12) freeze:
exactly one STOP row with a CONT after it, game_ms CONT - STOP (mod 2^32) <= 1000, exactly one "[FREEZE] absorbed"
engine line; (13) vitals: one VITALS line with min_health >= 0.999, max_radiation <= 0.0001, max_bleeding <= 0.0001,
unread 0; (14) the SUMMARY line has hits = cancelled and refills = 0."""
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
       ('2 P2 lines >= 17000', len(P2) >= 17000, str(len(P2))),
       ('3 no GUARD, no step > 5 m', not G and (max(step) if step else 0) <= 5.0, f'GUARD {len(G)}, max step {max(step) if step else 0:.3f} m'),
       ('4 HOLD <= 45', len(H) <= 45, str(len(H))),
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
res += [('7 STANCE lines >= 25 and no STANCE_UNSUPPORTED', len(ST) >= 25 and not UNS, f'{len(ST)} STANCE, {len(UNS)} unsupported'),
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
def csv(p):
    L = [l for l in rd(p) if l]; return [dict(zip(L[0].split(','), l.split(','))) for l in L[1:]] if L else []
fz = csv(os.path.join(O, 'freeze.csv')); stops = [r for r in fz if r.get('event') == 'STOP']; conts = [r for r in fz if r.get('event') == 'CONT']
absorbed = sum(1 for l in X if '[FREEZE] absorbed' in l); gd = None
if len(stops) == 1:
    c = next((c for c in conts if int(c['utc_ms']) > int(stops[0]['utc_ms'])), None)
    if c and stops[0].get('game_ms') and c.get('game_ms'): gd = (int(c['game_ms']) - int(stops[0]['game_ms'])) % 2**32
res.append(('12 one STOP..CONT pair, game time across it <= 1000 ms, one absorbed line', len(stops) == 1 and gd is not None and gd <= 1000 and absorbed == 1, f'{len(stops)} STOP, {len(conts)} CONT, game_ms delta {gd}, absorbed lines {absorbed}'))
VT = [l for l in M if l.startswith('VITALS,')]; kv = lambda l: dict(p.split('=') for p in l.split(',')[1:] if '=' in p)
v = kv(VT[0]) if len(VT) == 1 else {}
res.append(('13 vitals: health >= 0.999, radiation and bleeding <= 0.0001, 0 unread', bool(v) and float(v['min_health']) >= 0.999 and float(v['max_radiation']) <= 0.0001 and float(v['max_bleeding']) <= 0.0001 and v['unread'] == '0', VT[0] if len(VT) == 1 else f'{len(VT)} VITALS lines'))
SM = [l for l in M if l.startswith('SUMMARY,')]; sv = kv(SM[-1]) if SM else {}
res.append(('14 every hit cancelled, no refill', bool(sv) and sv['hits'] == sv['cancelled'] and sv['refills'] == '0', f"hits {sv.get('hits')}, cancelled {sv.get('cancelled')}, refills {sv.get('refills')}"))
txt = '\n'.join(f'{n}: {"PASS" if ok else "FAIL"} ({d})' for n, ok, d in res) + txt[txt.index('\nreported'):]
txt += '\nreported: ' + next((l for l in M if l.startswith('VITALS')), 'no VITALS line')
txt += f'\nT3 X SIDE: {"PASS" if all(r[1] for r in res) else "FAIL"}\n'
print(txt, end=''); open(os.path.join(O, 't3_xray_check.txt'), 'w').write(txt)
