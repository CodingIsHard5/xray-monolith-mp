#!/usr/bin/env python3
"""Pre-flight L (lost pose) checker. Run ONCE per boot.
  t3_pfL_check.py <out dir of t3_pfL.sh> <keep|freeze|cross>
The mutation boots are scored as the case they copy: mut_off as freeze, mut_sess as keep; both must FAIL.
All cases: (1) runner rc 0, no restart row, no Lua error naming the script, no FATAL ERROR, no DMG line; (V) every
VITALS line clean, no REFILL, and on every P2 stance read-back = pose stance, movement state readable, height = node
height + 0.05 m (+-0.01).
keep / freeze: (2) negative control: a STALE..RESUME pair before the LOST line and no LOST during it; (3) exactly one
LOST line, age 5000..5600 ms, last state play, policy as the case; (4) no P2, HOLD or GUARD line between LOST and FOUND;
(5) exactly one EPOCH line (two different session numbers, new seq <= 50), then FOUND, and at least 400 P2 after it;
(6) runner: exactly one row in lost.csv with the case's policy and a "lost end" log line.
  keep:   (7) no STOP row in freeze.csv, no ABSORB_ARM line.
  freeze: (7) exactly one STOP row with state `lost`, at most 4000 ms after the runner's lost row; one CONT row after
          the writer's restart; game time CONT - STOP <= 1000 ms; exactly one "[FREEZE] absorbed" engine line; one
          ABSORB_ARM line at the LOST line's game time.
cross: (2) one CROSS_REQ and one CROSS_RESUME; (3) exactly one LOST line, last state cross, at 120000..122500 ms of
game time after the CROSS_RESUME line; (4) no P2 after the CROSS_REQ; no EPOCH, no FOUND; (6) runner: one row in
lost.csv with state cross, and a "cross hold end" log line marked (lost); (7) no STOP row.
"""
import sys, os, re
O, CASE = sys.argv[1], sys.argv[2]
R = os.path.join(O, 'run'); B = os.path.join(R, 'boot0')
rd = lambda p: open(p, 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n') if os.path.exists(p) else []
M = [l for l in rd(f'{B}/meta_t2.csv') if l]; X = rd(f'{B}/tb/xray.log'); LG = [l for l in rd(f'{R}/t2_xray.log') if l]
RS = [l for l in rd(f'{R}/restarts.csv')[1:] if l]; LC = [l.split(',') for l in rd(f'{R}/lost.csv')[1:] if l]
FZ = [l.split(',') for l in rd(f'{R}/freeze.csv')[1:] if l]; WE = [l.split(',', 2) for l in rd(f'{O}/writer_events.csv')[1:] if l]
idx = lambda k: [i for i, l in enumerate(M) if l.startswith(k + ',')]
F = lambda i: M[i].split(',')
lua_err = [l for l in X if re.search(r'LUA error|lua error', l) and 'zz_p1_puppet' in l] + [X[i] for i in range(len(X)) if 'zz_p1_puppet.script (line' in X[i] and not any("doesn't exist" in X[j] or 'nil function' in X[j] for j in range(max(0, i - 8), i))]
fatal = [l for l in X if 'FATAL ERROR' in l]; rc0 = bool(LG) and LG[-1].rstrip().endswith('rc=0'); dmg = [l for l in M if l.startswith('DMG_')]
res = [('1 rc 0, no restart, no Lua error, no FATAL, no DMG line', rc0 and not RS and not lua_err and not fatal and not dmg, f'rc0 {rc0}, restart rows {len(RS)}, lua errors {len(lua_err)}, fatal {len(fatal)}, DMG lines {len(dmg)}')]
P2 = [F(i) for i in idx('P2')]; Q = [q for q in P2 if len(q) > 19]
st_bad = sum(1 for q in Q if q[19] != q[15]); unread = sum(1 for q in Q if q[18] == '-1'); h_bad = sum(1 for q in P2 if abs(float(q[14]) - float(q[11]) - 0.05) > 0.01)
kv = lambda l: dict(p.split('=') for p in l.split(',')[1:] if '=' in p)
VT = [kv(M[i]) for i in idx('VITALS')]; RF = len(idx('REFILL'))
v_ok = bool(VT) and all(float(v['min_health']) >= 0.999 and float(v['max_radiation']) <= 0.0001 and float(v['max_bleeding']) <= 0.0001 and v['unread'] == '0' for v in VT)
res.append(('V vitals clean, no refill; stance, state and height on every P2', v_ok and not RF and bool(P2) and len(Q) == len(P2) and not st_bad and not unread and not h_bad, f'{len(VT)} VITALS lines clean {v_ok}, REFILL {RF}; P2 {len(P2)}: stance differ {st_bad}, unread {unread}, height off {h_bad}'))
LO, FO, EP, ST, RE = idx('LOST'), idx('FOUND'), idx('EPOCH'), idx('STALE'), idx('RESUME')
stops = [r for r in FZ if r[1] == 'STOP']; conts = [r for r in FZ if r[1] == 'CONT']; arm = idx('ABSORB_ARM')
one = len(LO) == 1; lo = LO[0] if LO else None; L = F(lo) if one else None
if CASE in ('keep', 'freeze'):
    pair = [(s, r) for s in ST for r in RE if s < r and (lo is None or r < lo) and not any(s < x < r for x in LO)]
    res.append(('2 negative control: a STALE..RESUME pair before LOST, no LOST in it', bool(pair) and one, f'{len(pair)} pair(s) before LOST; STALE {len(ST)}, RESUME {len(RE)}, LOST {len(LO)}'))
    ok3 = one and 5000 <= int(L[4]) <= 5600 and L[5] == 'play' and L[6] == CASE
    res.append((f'3 one LOST line, age 5000..5600 ms, state play, policy {CASE}', ok3, f'{[M[i] for i in LO]}'))
    fo = next((i for i in FO if lo is not None and i > lo), None)
    between = [l for l in M[lo:fo] if l.split(',')[0] in ('P2', 'P', 'HOLD', 'GUARD')] if one and fo else None
    res.append(('4 no pose applied between LOST and FOUND', between is not None and not between, f'FOUND after LOST: {fo is not None}; lines between: {None if between is None else len(between)}'))
    ep = F(EP[0]) if len(EP) == 1 else None
    after = sum(1 for i in idx('P2') if fo and i > fo)
    ok5 = ep is not None and ep[3] != ep[4] and int(ep[6]) <= 50 and one and lo < EP[0] and fo is not None and EP[0] < fo and len(FO) == 1 and after >= 400
    res.append(('5 one EPOCH (new session, new seq <= 50) before FOUND; >= 400 P2 after', ok5, f'EPOCH {[M[i] for i in EP]}; FOUND {[M[i] for i in FO]}; P2 after FOUND {after}'))
    lend = sum(1 for l in LG if 'lost end:' in l)
    res.append((f'6 runner: one lost.csv row with policy {CASE}; a "lost end" line', len(LC) == 1 and LC[0][3] == CASE and lend == 1, f'lost.csv {LC}; "lost end" lines {lend}'))
    if CASE == 'keep':
        res.append(('7 no STOP row, no ABSORB_ARM', not stops and not arm, f'STOP {len(stops)}, ABSORB_ARM {len(arm)}'))
    else:
        rst = next((int(w[0]) for w in WE if w[1] == 'restart'), None); absorbed = sum(1 for l in X if '[FREEZE] absorbed' in l)
        ok7 = len(stops) == 1 and len(conts) == 1 and stops[0][2] == 'lost' and len(LC) == 1 and 0 <= int(stops[0][0]) - int(LC[0][0]) <= 4000 \
            and rst is not None and int(conts[0][0]) >= rst and stops[0][4] != '' and conts[0][4] != '' and (int(conts[0][4]) - int(stops[0][4])) % 2**32 <= 1000 \
            and absorbed == 1 and len(arm) == 1 and one and F(arm[0])[1] == L[2]
        res.append(('7 one STOP (lost) within 4 s of the runner\'s LOST, CONT after the restart, game time across <= 1000 ms, one absorbed line, armed at LOST', ok7, f'freeze.csv {FZ}; lost.csv {LC}; restart utc {rst}; absorbed lines {absorbed}; ABSORB_ARM {[M[i] for i in arm]}'))
else:
    RQ, RSM = idx('CROSS_REQ'), idx('CROSS_RESUME')
    ok2 = len(RQ) == 1 and len(RSM) == 1 and RQ[0] < RSM[0]
    res.append(('2 one CROSS_REQ, then one CROSS_RESUME', ok2, f'{[M[i] for i in RQ + RSM]}'))
    d = int(L[2]) - int(F(RSM[0])[1]) if one and ok2 else None
    ok3 = d is not None and L[5] == 'cross' and 120000 <= d <= 122500 and lo > RSM[0]
    res.append(('3 one LOST line, state cross, 120000..122500 ms after the resume', ok3, f'{[M[i] for i in LO]}; game ms after resume {d}'))
    after = sum(1 for i in idx('P2') if RQ and i > RQ[0])
    res.append(('4 no P2 after the CROSS_REQ; no EPOCH, no FOUND', bool(RQ) and after == 0 and not EP and not FO, f'P2 after request {after}, EPOCH {len(EP)}, FOUND {len(FO)}'))
    che = sum(1 for l in LG if 'cross hold end:' in l and '(lost)' in l)
    res.append(('6 runner: one lost.csv row with state cross; cross hold ended as lost', len(LC) == 1 and LC[0][2] == 'cross' and che == 1, f'lost.csv {LC}; "cross hold end ... (lost)" lines {che}'))
    res.append(('7 no STOP row', not stops, f'STOP {len(stops)}'))
ok = all(r[1] for r in res)
out = [f'{n}: {"PASS" if p else "FAIL"} ({d})' for n, p, d in res]
for k in ('STALE', 'LOST', 'EPOCH', 'FOUND', 'RESUME', 'ABSORB_ARM', 'ABSORB_DISARM', 'T2SUM', 'VITALS'): out += [f'reported: {M[i]}' for i in idx(k)]
out += [f'reported: writer {",".join(w)}' for w in WE] + [f'reported: runner {l}' for l in LG if 'LOST' in l or 'lost' in l or 'freeze' in l or 'cross hold' in l]
out.append(f'PRE-FLIGHT L ({CASE}): {"PASS" if ok else "FAIL"}')
print('\n'.join(out)); open(os.path.join(O, f't3_pfL_check_{CASE}.txt'), 'w').write('\n'.join(out) + '\n')
