#!/usr/bin/env python3
"""T3 pre-flight D check (R3 level change BY NAME, there and back; PREREG-B1-T2-xray-side section 42). Run ONCE.
  t3_pfD_check.py <out dir of t2_preflight.sh> <changer table csv> <home> <dest> <changer out> <changer back>
PASS needs all of: (1) t2_xray rc 0, no Lua error naming zz_p1_puppet, no FATAL ERROR; (2) exactly two CROSS_REQ, in
order <home> -> <dest> through <changer out> and <dest> -> <home> through <changer back>, each with the destination
x,y,z, level vertex and game vertex of that changer's row in the table; no CROSS_NOCHANGER / CROSS_ERROR; (3) exactly
two CROSS_RESUME, on <dest> then on <home>, each after its request; (4) for each leg the writer saw the new level in
the snapshot at most 180 s after it first sent `cross`; (5) after each resume (up to the next request): at least 200
P2 lines and no GUARD; (6) the first P2 after each resume is within 5 m (x,z) of that changer's destination; (7) the
runner logged "cross hold" twice and restarts.csv has no row.
Reported: load times, lines per part, VITALS / SUMMARY / MOVESUM lines, HOLD counts."""
import sys, os, re, math
O, TAB, HOME, DEST, C1, C2 = sys.argv[1:7]
rd = lambda p: open(p, 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n') if os.path.exists(p) else []
M = [l for l in rd(f'{O}/run/boot0/meta_t2.csv') if l]; X = rd(f'{O}/run/boot0/tb/xray.log'); LG = [l for l in rd(f'{O}/run/t2_xray.log') if l]
W = [l.split(',') for l in rd(f'{O}/pose_written.csv')[1:] if l]; RS = [l for l in rd(f'{O}/run/restarts.csv')[1:] if l]
TB = {f[1]: f for f in (l.split(',') for l in rd(TAB) if l.startswith('CHANGER,'))}
lua_err = [l for l in X if re.search(r'LUA error|lua error', l) and 'zz_p1_puppet' in l] + [X[i] for i in range(len(X)) if 'zz_p1_puppet.script (line' in X[i] and not any("doesn't exist" in X[j] or 'nil function' in X[j] for j in range(max(0, i - 8), i))]
fatal = [l for l in X if 'FATAL ERROR' in l]; rc0 = bool(LG) and LG[-1].rstrip().endswith('rc=0')
req = [i for i, l in enumerate(M) if l.startswith('CROSS_REQ,')]; rsm = [i for i, l in enumerate(M) if l.startswith('CROSS_RESUME,')]
bad = [l for l in M if l.startswith('CROSS_NOCHANGER') or l.startswith('CROSS_ERROR')]
want = [(HOME, DEST, C1), (DEST, HOME, C2)]
def req_ok(i, w):
    f = M[i].split(','); t = TB.get(w[2])
    return bool(t) and f[3:6] == list(w) and all(abs(float(f[6 + k]) - float(t[4 + k])) <= 0.002 for k in range(3)) and f[9:11] == t[7:9]
ok2 = len(req) == 2 and not bad and all(req_ok(i, w) for i, w in zip(req, want))
ok3 = len(rsm) == 2 and len(req) == 2 and req[0] < rsm[0] < req[1] < rsm[1] and M[rsm[0]].split(',')[2] == DEST and M[rsm[1]].split(',')[2] == HOME
loads = []
for k in ('1', '2'):
    tx = next((int(w[0]) for w in W if w[2] == 'X' + k), None); ty = next((int(w[0]) for w in W if w[2] == 'Y' + k), None)
    loads.append((ty - tx) / 1000 if tx and ty else None)
parts = [M[rsm[0]:req[1]], M[rsm[1]:]] if ok3 else [[], []]
p2 = [[l.split(',') for l in p if l.startswith('P2,')] for p in parts]; g = [sum(1 for l in p if l.startswith('GUARD,')) for p in parts]
def near(k):
    t = TB.get(want[k][2])
    return math.hypot(float(p2[k][0][7]) - float(t[4]), float(p2[k][0][9]) - float(t[6])) if t and p2[k] else None
d = [near(0), near(1)]
holds = sum(1 for l in LG if 'cross hold:' in l)
res = [('1 rc 0, no Lua error, no FATAL', rc0 and not lua_err and not fatal, f'rc0 {rc0}, lua errors {len(lua_err)}, fatal {len(fatal)}'),
       ('2 two CROSS_REQ by the named changers with the table values, no NOCHANGER/ERROR', ok2, '; '.join(','.join(M[i].split(',')[3:11]) for i in req) + f'; {len(bad)} bad'),
       ('3 two CROSS_RESUME, on the destination then home, each after its request', ok3, '; '.join(M[i] for i in rsm) or 'none'),
       ('4 each new level in the snapshot <= 180 s after `cross`', all(v is not None and v <= 180 for v in loads), f'{loads} s'),
       ('5 after each resume: >= 200 P2, no GUARD', all(len(q) >= 200 for q in p2) and g == [0, 0], f'P2 {[len(q) for q in p2]}, GUARD {g}'),
       ('6 first P2 after each resume within 5 m of the changer destination', all(v is not None and v <= 5.0 for v in d), f'{d} m'),
       ('7 runner logged two cross holds, no restart', holds == 2 and not RS, f'{holds} holds, {len(RS)} restart rows')]
txt = '\n'.join(f'{n}: {"PASS" if ok else "FAIL"} ({dd})' for n, ok, dd in res)
txt += f'\nreported: HOLD per part {[sum(1 for l in p if l.startswith("HOLD,")) for p in parts]}; meta lines {len(M)}'
txt += '\nreported: ' + '\nreported: '.join([l for l in M if l.startswith(('CROSS_', 'VITALS', 'SUMMARY', 'MOVESUM', 'T2SUM', 'CHANGER_ERROR'))] or ['no lines'])
txt += f'\nPRE-FLIGHT D: {"PASS" if all(r[1] for r in res) else "FAIL"}\n'
print(txt, end=''); open(os.path.join(O, 't3_pfD_check.txt'), 'w').write(txt)
