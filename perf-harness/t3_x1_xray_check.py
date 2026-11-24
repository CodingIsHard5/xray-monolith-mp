#!/usr/bin/env python3
"""R3 joint crossing pre-flight (ITRZone arm x1), X side (PREREG-B1-T2-xray-side section 48). Run ONCE.
  t3_x1_xray_check.py <t2_xray out dir> <changer table csv> <home> <dest> <changer out> <changer back>
PASS needs all of: (1) rc 0, no Lua error naming zz_p1_puppet, no FATAL ERROR, no DMG_MODE line; (2) exactly two
CROSS_REQ, in order, each through the named changer with that changer's destination and vertices from the table; no
CROSS_NOCHANGER / CROSS_ERROR; (3) exactly two CROSS_RESUME, on <dest> then on <home>, each after its request;
(4) for each crossing, the first applied play pose is written by ITR2 at most 90 s after the `cross` pose that
started it (both times are ITR2's own utc stamps); (5) P2 lines: at least 400 before the first request and at least
600 after each resume; no GUARD line anywhere; at most 15 HOLD; (6) the first P2 after each resume is within 5 m (x,z)
of that changer's destination; (7) the runner logged exactly two cross holds and restarts.csv has no row; (8) on
every P2: get_actor_stance = pose stance, the movement state is readable, actor height = node height + 0.05 m
(+-0.01); (9) vitals: exactly three VITALS lines, each with min_health >= 0.999, max_radiation <= 0.0001,
max_bleeding <= 0.0001, unread 0; no REFILL line.
Reported: times, lines per part, SUMMARY / MOVESUM lines, HOLD count."""
import sys, os, re, math
O, TAB, HOME, DEST, C1, C2 = sys.argv[1:7]
B = os.path.join(O, 'boot0') if os.path.isdir(os.path.join(O, 'boot0')) else os.path.join(O, 'run', 'boot0'); R = os.path.dirname(B)
rd = lambda p: open(p, 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n') if os.path.exists(p) else []
M = [l for l in rd(f'{B}/meta_t2.csv') if l]; X = rd(f'{B}/tb/xray.log'); LG = [l for l in rd(f'{R}/t2_xray.log') if l]; RS = [l for l in rd(f'{R}/restarts.csv')[1:] if l]
TB = {f[1]: f for f in (l.split(',') for l in rd(TAB) if l.startswith('CHANGER,'))}
lua_err = [l for l in X if re.search(r'LUA error|lua error', l) and 'zz_p1_puppet' in l] + [X[i] for i in range(len(X)) if 'zz_p1_puppet.script (line' in X[i] and not any("doesn't exist" in X[j] or 'nil function' in X[j] for j in range(max(0, i - 8), i))]
fatal = [l for l in X if 'FATAL ERROR' in l]; rc0 = bool(LG) and LG[-1].rstrip().endswith('rc=0'); dmg = [l for l in M if l.startswith('DMG_')]
req = [i for i, l in enumerate(M) if l.startswith('CROSS_REQ,')]; rsm = [i for i, l in enumerate(M) if l.startswith('CROSS_RESUME,')]
bad = [l for l in M if l.startswith('CROSS_NOCHANGER') or l.startswith('CROSS_ERROR')]
want = [(HOME, DEST, C1), (DEST, HOME, C2)]
def req_ok(i, w):
    f = M[i].split(','); t = TB.get(w[2])
    return bool(t) and f[3:6] == list(w) and all(abs(float(f[6 + k]) - float(t[4 + k])) <= 0.002 for k in range(3)) and f[9:11] == t[7:9]
ok2 = len(req) == 2 and not bad and all(req_ok(i, w) for i, w in zip(req, want))
ok3 = len(rsm) == 2 and len(req) == 2 and req[0] < rsm[0] < req[1] < rsm[1] and M[rsm[0]].split(',')[2] == DEST and M[rsm[1]].split(',')[2] == HOME
parts = [M[:req[0]], M[rsm[0]:req[1]], M[rsm[1]:]] if ok3 else [M, [], []]
p2 = [[l.split(',') for l in p if l.startswith('P2,')] for p in parts]; ALL = [q for p in p2 for q in p]
G = sum(1 for l in M if l.startswith('GUARD,')); H = sum(1 for l in M if l.startswith('HOLD,'))
dt = [(int(p2[k + 1][0][2]) - int(M[req[k]].split(',')[11])) / 1000 if ok3 and p2[k + 1] else None for k in (0, 1)]
def near(k):
    t = TB.get(want[k][2])
    return math.hypot(float(p2[k + 1][0][7]) - float(t[4]), float(p2[k + 1][0][9]) - float(t[6])) if t and p2[k + 1] else None
d = [near(0), near(1)]
holds = sum(1 for l in LG if 'cross hold:' in l)
Q = [q for q in ALL if len(q) > 19]
st_bad = sum(1 for q in Q if q[19] != q[15]); unread = sum(1 for q in Q if q[18] == '-1'); h_bad = sum(1 for q in ALL if abs(float(q[14]) - float(q[11]) - 0.05) > 0.01)
kv = lambda l: dict(p.split('=') for p in l.split(',')[1:] if '=' in p)
VT = [kv(l) for l in M if l.startswith('VITALS,')]; RF = sum(1 for l in M if l.startswith('REFILL,'))
v_ok = len(VT) == 3 and all(float(v['min_health']) >= 0.999 and float(v['max_radiation']) <= 0.0001 and float(v['max_bleeding']) <= 0.0001 and v['unread'] == '0' for v in VT)
res = [('1 rc 0, no Lua error, no FATAL, test mode off', rc0 and not lua_err and not fatal and not dmg, f'rc0 {rc0}, lua errors {len(lua_err)}, fatal {len(fatal)}, DMG lines {len(dmg)}'),
       ('2 two CROSS_REQ by the named changers with the table values, no NOCHANGER/ERROR', ok2, '; '.join(','.join(M[i].split(',')[3:11]) for i in req) + f'; {len(bad)} bad'),
       ('3 two CROSS_RESUME, on the destination then home, each after its request', ok3, '; '.join(M[i] for i in rsm) or 'none'),
       ('4 first applied play pose <= 90 s after each `cross` start (ITR2 clock)', all(v is not None and 0 <= v <= 90 for v in dt), f'{dt} s'),
       ('5 P2 >= 400 before, >= 600 after each resume; no GUARD; HOLD <= 15', len(p2[0]) >= 400 and len(p2[1]) >= 600 and len(p2[2]) >= 600 and G == 0 and H <= 15, f'P2 {[len(q) for q in p2]}, GUARD {G}, HOLD {H}'),
       ('6 first P2 after each resume within 5 m of the changer destination', all(v is not None and v <= 5.0 for v in d), f'{d} m'),
       ('7 two runner cross holds, no restart', holds == 2 and not RS, f'{holds} holds, {len(RS)} restart rows'),
       ('8 stance read-back, movement state, height on every P2', bool(ALL) and len(Q) == len(ALL) and st_bad == 0 and unread == 0 and h_bad == 0, f'{st_bad} stance differ, {unread} unread, {h_bad} height off, of {len(ALL)}'),
       ('9 three VITALS lines, each clean; no refill', v_ok and RF == 0, f'{VT}; REFILL {RF}')]
txt = '\n'.join(f'{n}: {"PASS" if ok else "FAIL"} ({dd})' for n, ok, dd in res)
txt += '\nreported: ' + '\nreported: '.join([l for l in M if l.startswith(('CROSS_', 'VITALS', 'SUMMARY', 'MOVESUM', 'T2SUM', 'CHANGER_ERROR'))] or ['no lines'])
txt += f'\nR3 x1 X SIDE: {"PASS" if all(r[1] for r in res) else "FAIL"}\n'
print(txt, end=''); open(os.path.join(O, 't3_x1_xray_check.txt'), 'w').write(txt)
