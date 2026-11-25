#!/usr/bin/env python3
"""R3 arm D (ITR2 restarts while X-Ray runs; pose v3), X side. DESK DRAFT, not registered. Run ONCE per run.
  t3_armD_xray_check.py <t2_xray out dir> <changer table csv> <play|cross|v2|v2r> <keep|freeze> [gap=<min>-<max> s] [spot=<x>,<z>] [c1=<changer out>]
play  = ITR2 is killed during play and comes back as a new session (seq from 1).
cross = ITR2 is killed during a crossing out; X-Ray finishes its level change alone, the pose is LOST 120 s after the
        resume, the new ITR2 session appears on the home level and sends the recover crossing. keep only.
v2    = run d0, X-Ray only: a stand-in writer sends pose VERSION 2 (no session field) to the v3 reader, is killed and
        comes back with its seq at 1 again, as an old ITR2 mod would. keep only. This is run d0a, the CONTROL
        (T2_V2RESET=0): the returning writer is taken only when its seq has passed the old one.
v2r   = run d0b, the same with T2_V2RESET=1: checks 1-3 and 5-7 as v2; (4) no EPOCH; exactly one RESET line after
        LOST (old seq = the LOST line's, new seq <= 200), then exactly one FOUND with seq <= 200 and age 25..45 s;
        (8) the last T2SUM says lost=1, epochs=1. In v2 (control) there must be NO RESET line.
All: (1) runner rc 0, engine log over 100 lines, no restart row, no Lua error naming the script, no FATAL, no DMG line;
(V) VITALS lines (1 in play and v2, 3 in cross) all clean, no REFILL; stance read-back, movement state and height on every P2.
play: (2) exactly one LOST, age 5000..5600 ms, state play, the policy; (3) nothing applied between LOST and FOUND;
 (4) exactly one EPOCH (two session numbers, new seq <= 200) after LOST, then exactly one FOUND; the gap without a pose, by the runner's
 wall clock (lost limit + "lost end ... after"), is inside gap (default 60-420 s); (5) P2 >= 400 before LOST and >= 600 after FOUND, GUARD 0, HOLD <= 15; (6) runner: one
 lost.csv row with the policy, one "lost end" line; (7) keep: no STOP row, no ABSORB_ARM. freeze: the first STOP row
 has state lost and comes <= 4000 ms after the runner's lost row; every STOP..CONT pair has game time across <= 1000 ms;
 as many "[FREEZE] absorbed" engine lines as STOP rows; an ABSORB_ARM at the LOST line's game time; (8) the last
 T2SUM says lost=1, epochs=1; (9) with spot=: the first P2 after FOUND within 5 m (x,z) of it.
v2: (2) every row of the writer's own record pose_written.csv has session `none` (at least 1000 rows); (3) exactly one
 LOST, age 5000..5600 ms, state play, keep; (4) no EPOCH line at all; exactly one FOUND after LOST, its seq 1 to 3
 above the seq of the LOST line (the returning writer is taken only when its seq has passed the old one), its age
 60..120 s; (5) nothing applied between LOST and FOUND; P2 >= 400 before LOST and >= 400 after FOUND, GUARD 0,
 HOLD <= 15; (6) runner: one lost.csv row with policy keep, one "lost end" line; (7) no STOP row, no ABSORB_ARM;
 (8) the last T2SUM says lost=1, epochs=0.
cross: (2) CROSS_REQ, CROSS_RESUME, CROSS_REQ, CROSS_RESUME in this order: out by esc_level_changer_to_garbage_1, back
 by gar_level_changer_to_escape_1 or _2, each with its table values; no CROSS_NOCHANGER / CROSS_ERROR; (3) exactly one
 LOST, state cross, 120000..122500 ms of game time after the first resume; (4) THE WRITER RETURNS AFTER A LOST
 CROSSING: exactly one EPOCH (new seq <= 200) and one FOUND, after LOST and before the second request, the runner's gap
 inside gap; at least 20 ZONE_MISMATCH lines naming home between FOUND and the second request, none anywhere else;
 no P2 between the first request and the second resume; (5) P2 >= 400 before the first request, >= 600 after the
 second resume, GUARD 0, HOLD <= 15; (6) runner: one lost.csv row with state cross, one "cross hold end" marked
 (lost), one "lost end" line, two "cross hold:" lines; (7) no STOP row; (8) one T2SUM says lost=1 and epochs=1 (the counts are per
 Lua state: it is the dest level's T2SUM); (9) with spot=: the first P2 after the second resume within 5 m."""
import sys, os, re, math
O, TAB, ARM, AW = sys.argv[1:5]; OUT = next((a[4:] for a in sys.argv[5:] if a.startswith('out=')), None); V2R = sys.argv[3] == 'v2r'; ARM = 'v2' if V2R else ARM
assert ARM in ('play', 'cross', 'v2') and AW in ('keep', 'freeze') and not (ARM != 'play' and AW == 'freeze')
opt = dict(a.split('=', 1) for a in sys.argv[5:]);   # out=<file> writes the report there instead of into the run folder
GA, GB = (float(v) for v in opt.get('gap', '60-420').split('-'))
SPOT = [float(v) for v in opt['spot'].split(',')] if 'spot' in opt else None
HOME, DEST, C1, BACK = 'l01_escape', 'l02_garbage', opt.get('c1', 'esc_level_changer_to_garbage_1'), ('gar_level_changer_to_escape_1', 'gar_level_changer_to_escape_2')
R = O if os.path.isdir(os.path.join(O, 'boot0')) else os.path.join(O, 'run'); B = os.path.join(R, 'boot0')
rd = lambda p: open(p, 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n') if os.path.exists(p) else []
M = [l for l in rd(f'{B}/meta_t2.csv') if l]; X = rd(f'{B}/tb/xray.log'); LG = [l for l in rd(f'{R}/t2_xray.log') if l]
RS = [l for l in rd(f'{R}/restarts.csv')[1:] if l]; LC = [l.split(',') for l in rd(f'{R}/lost.csv')[1:] if l]; FZ = [l.split(',') for l in rd(f'{R}/freeze.csv')[1:] if l]
TB = {f[1]: f for f in (l.split(',') for l in rd(TAB) if l.startswith('CHANGER,'))}
idx = lambda k: [i for i, l in enumerate(M) if l.startswith(k + ',')]
F = lambda i: M[i].split(',')
lua_err = [l for l in X if re.search(r'LUA error|lua error', l) and 'zz_p1_puppet' in l] + [X[i] for i in range(len(X)) if 'zz_p1_puppet.script (line' in X[i] and not any("doesn't exist" in X[j] or 'nil function' in X[j] for j in range(max(0, i - 8), i))]
fatal = [l for l in X if 'FATAL ERROR' in l]; rc0 = bool(LG) and LG[-1].rstrip().endswith('rc=0'); dmg = [l for l in M if l.startswith('DMG_')]; xlog = sum(1 for l in X if l.strip())
res = [('1 rc 0, engine log present, no restart, no Lua error, no FATAL, no DMG line', rc0 and xlog > 100 and bool(M) and not RS and not lua_err and not fatal and not dmg, f'rc0 {rc0}, engine log lines {xlog}, restart rows {len(RS)}, lua errors {len(lua_err)}, fatal {len(fatal)}, DMG lines {len(dmg)}')]
P2i = idx('P2'); P2 = [F(i) for i in P2i]; Q = [q for q in P2 if len(q) > 19]
st_bad = sum(1 for q in Q if q[19] != q[15]); unread = sum(1 for q in Q if q[18] == '-1'); h_bad = sum(1 for q in P2 if abs(float(q[14]) - float(q[11]) - 0.05) > 0.01)
kv = lambda l: dict(p.split('=') for p in l.split(',')[1:] if '=' in p)
VT = [kv(M[i]) for i in idx('VITALS')]; RF = len(idx('REFILL')); NV = 3 if ARM == 'cross' else 1
v_ok = len(VT) == NV and all(float(v['min_health']) >= 0.999 and float(v['max_radiation']) <= 0.0001 and float(v['max_bleeding']) <= 0.0001 and v['unread'] == '0' for v in VT)
res.append((f'V {NV} VITALS line(s), clean, no refill; stance, state and height on every P2', v_ok and not RF and bool(P2) and len(Q) == len(P2) and not st_bad and not unread and not h_bad, f'{len(VT)} VITALS lines, clean and counted {v_ok}, REFILL {RF}; P2 {len(P2)}: stance differ {st_bad}, unread {unread}, height off {h_bad}'))
LO, FO, EP = idx('LOST'), idx('FOUND'), idx('EPOCH'); G = len(idx('GUARD')); H = len(idx('HOLD'))
one = len(LO) == 1; lo = LO[0] if one else None; L = F(lo) if one else None
ep = F(EP[0]) if len(EP) == 1 else None; fo = FO[0] if len(FO) == 1 else None
ep_ok = ep is not None and ep[3] != ep[4] and int(ep[6]) <= 200 and one and lo < EP[0] and fo is not None and EP[0] < fo
# the gap is the runner's wall-clock figure ("lost end: new pose ... after <ms> ms", counted from its LOST line) plus the
# lost limit before it; X-Ray's own FOUND age is game time, which stands still while X-Ray is stopped under freeze
LE = [re.search(r'lost end: .* after (\d+) ms', l) for l in LG]; LE = [int(m.group(1)) for m in LE if m]
gap = (LE[0] + (int(LC[0][1]) if LC else 0)) / 1000 if len(LE) == 1 else None; gap_ok = gap is not None and GA <= gap <= GB
T2S = [kv(M[i]) for i in idx('T2SUM')]; stops = [r for r in FZ if r[1] == 'STOP']; conts = [r for r in FZ if r[1] == 'CONT']; arm = idx('ABSORB_ARM')
near = lambda q: math.hypot(float(q[7]) - SPOT[0], float(q[9]) - SPOT[1]) if SPOT and q else None
if ARM == 'play':
    res.append((f'2 one LOST, age 5000..5600 ms, state play, policy {AW}', one and 5000 <= int(L[4]) <= 5600 and L[5] == 'play' and L[6] == AW, f'{[M[i] for i in LO]}'))
    between = [l for l in M[lo:fo] if l.split(',')[0] in ('P2', 'P', 'HOLD', 'GUARD')] if one and fo is not None and fo > lo else None
    res.append(('3 nothing applied between LOST and FOUND', between is not None and not between, f'lines between: {None if between is None else len(between)}'))
    res.append((f'4 one EPOCH (new session, new seq <= 200), then one FOUND; runner gap {GA:g}..{GB:g} s', ep_ok and gap_ok, f'EPOCH {[M[i] for i in EP]}; FOUND {[M[i] for i in FO]}; gap {gap} s'))
    before = sum(1 for i in P2i if one and i < lo); after = [F(i) for i in P2i if fo is not None and i > fo]
    res.append(('5 P2 >= 400 before LOST, >= 600 after FOUND; no GUARD; HOLD <= 15', before >= 400 and len(after) >= 600 and G == 0 and H <= 15, f'P2 {before} before, {len(after)} after; GUARD {G}, HOLD {H}'))
    lend = sum(1 for l in LG if 'lost end:' in l)
    res.append((f'6 runner: one lost.csv row with policy {AW}; one "lost end" line', len(LC) == 1 and LC[0][3] == AW and lend == 1, f'lost.csv {LC}; "lost end" lines {lend}'))
    if AW == 'keep': res.append(('7 no STOP row, no ABSORB_ARM', not stops and not arm, f'STOP {len(stops)}, ABSORB_ARM {len(arm)}'))
    else:
        absorbed = sum(1 for l in X if '[FREEZE] absorbed' in l)
        pairs_ok = len(stops) == len(conts) >= 1 and all(s[4] != '' and c[4] != '' and (int(c[4]) - int(s[4])) % 2**32 <= 1000 for s, c in zip(stops, conts))
        ok7 = pairs_ok and stops[0][2] == 'lost' and len(LC) == 1 and 0 <= int(stops[0][0]) - int(LC[0][0]) <= 4000 and absorbed == len(stops) and one and any(F(a)[1] == L[2] for a in arm)
        res.append(('7 first STOP is `lost`, <= 4 s after the runner\'s LOST; game time across every stop <= 1000 ms; one absorbed line per stop; armed at LOST', ok7, f'freeze.csv {FZ}; lost.csv {LC}; absorbed lines {absorbed}; ABSORB_ARM {[M[i] for i in arm]}'))
    res.append(('8 last T2SUM: lost=1, epochs=1', bool(T2S) and T2S[-1].get('lost') == '1' and T2S[-1].get('epochs') == '1', f'{T2S[-1] if T2S else None}'))
    if SPOT: d = near(after[0] if after else None); res.append(('9 first P2 after FOUND within 5 m of the spot', d is not None and d <= 5, f'{d} m'))
elif ARM == 'v2':
    PW = [l.split(',') for l in rd(os.path.join(O, 'pose_written.csv'))[1:] if l]
    res.append(('2 the writer sent pose version 2: session `none` on every row of its record', len(PW) >= 1000 and all(len(r) > 3 and r[3] == 'none' for r in PW), f'{len(PW)} rows, {sum(1 for r in PW if len(r) > 3 and r[3] != "none")} with a session'))
    res.append(('3 one LOST, age 5000..5600 ms, state play, policy keep', one and 5000 <= int(L[4]) <= 5600 and L[5] == 'play' and L[6] == 'keep', f'{[M[i] for i in LO]}'))
    f1 = F(fo) if fo is not None else None; up = int(f1[3]) - int(L[3]) if f1 and one else None; age = int(f1[4]) / 1000 if f1 else None
    RSi = idx('RESET'); rs = F(RSi[0]) if len(RSi) == 1 else None
    if V2R: res.append(('4 no EPOCH; one RESET after LOST (old seq, new seq <= 200), then one FOUND, seq <= 200, age 25..45 s', not EP and one and rs is not None and fo is not None and lo < RSi[0] < fo and rs[3] == L[3] and int(rs[4]) <= 200 and int(f1[3]) <= 200 and 25 <= age <= 45, f'EPOCH {len(EP)}; RESET {[M[i] for i in RSi]}; FOUND {[M[i] for i in FO]}; age {age} s'))
    else: res.append(('4 no EPOCH, no RESET; one FOUND after LOST, its seq 1..3 above the old one, age 60..120 s', not RSi and not EP and one and fo is not None and fo > lo and up is not None and 1 <= up <= 3 and 60 <= age <= 120, f'EPOCH {len(EP)}; FOUND {[M[i] for i in FO]}; seq above the old one {up}; age {age} s'))
    between = [l for l in M[lo:fo] if l.split(',')[0] in ('P2', 'P', 'HOLD', 'GUARD')] if one and fo is not None and fo > lo else None
    before = sum(1 for i in P2i if one and i < lo); after = sum(1 for i in P2i if fo is not None and i > fo)
    res.append(('5 nothing applied between LOST and FOUND; P2 >= 400 before and >= 400 after; no GUARD; HOLD <= 15', between is not None and not between and before >= 400 and after >= 400 and G == 0 and H <= 15, f'between {None if between is None else len(between)}; P2 {before} before, {after} after; GUARD {G}, HOLD {H}'))
    lend = sum(1 for l in LG if 'lost end:' in l)
    res.append(('6 runner: one lost.csv row with policy keep; one "lost end" line', len(LC) == 1 and LC[0][3] == 'keep' and lend == 1, f'lost.csv {LC}; "lost end" lines {lend}; the runner saw a pose again after {gap} s (reported: it takes any new pose, the script waits for the seq)'))
    res.append(('7 no STOP row, no ABSORB_ARM', not stops and not arm, f'STOP {len(stops)}, ABSORB_ARM {len(arm)}'))
    EX = '1' if V2R else '0'
    res.append((f'8 last T2SUM: lost=1, epochs={EX}', bool(T2S) and T2S[-1].get('lost') == '1' and T2S[-1].get('epochs') == EX, f'{T2S[-1] if T2S else None}'))
else:
    RQ, RSM = idx('CROSS_REQ'), idx('CROSS_RESUME'); bad = [l for l in M if l.startswith(('CROSS_NOCHANGER', 'CROSS_ERROR'))]
    def req_ok(i, a, b, names):
        f = F(i); t = TB.get(f[5])
        return f[5] in names and bool(t) and f[3:5] == [a, b] and all(abs(float(f[6 + k]) - float(t[4 + k])) <= 0.002 for k in range(3)) and f[9:11] == t[7:9]
    order = len(RQ) == 2 and len(RSM) == 2 and RQ[0] < RSM[0] < RQ[1] < RSM[1]
    ok2 = order and not bad and req_ok(RQ[0], HOME, DEST, (C1,)) and req_ok(RQ[1], DEST, HOME, BACK) and F(RSM[0])[2] == DEST and F(RSM[1])[2] == HOME
    res.append(('2 out by the changer, resume on dest, recover by a changer back, resume on home; table values; no NOCHANGER/ERROR', ok2, '; '.join(','.join(F(i)[:6]) for i in sorted(RQ + RSM)) + f'; {len(bad)} bad'))
    d = int(L[2]) - int(F(RSM[0])[1]) if one and order else None
    res.append(('3 one LOST, state cross, 120000..122500 ms after the first resume', d is not None and L[5] == 'cross' and 120000 <= d <= 122500 and RSM[0] < lo < RQ[1], f'{[M[i] for i in LO]}; game ms after the resume {d}'))
    ZM = idx('ZONE_MISMATCH'); zin = [i for i in ZM if order and fo is not None and fo < i < RQ[1] and F(i)[3] == HOME]
    mid = sum(1 for i in P2i if order and RQ[0] < i < RSM[1])
    ok4 = ep_ok and gap_ok and order and fo < RQ[1] and len(zin) >= 20 and len(zin) == len(ZM) and mid == 0
    res.append((f'4 the writer returns after a lost crossing: EPOCH, FOUND (runner gap {GA:g}..{GB:g} s), >= 20 ZONE_MISMATCH before the recover request, no pose applied on dest', ok4, f'EPOCH {[M[i] for i in EP]}; FOUND {[M[i] for i in FO]}; gap {gap} s; ZONE_MISMATCH {len(zin)} in the window of {len(ZM)}; P2 between the first request and the second resume {mid}'))
    before = sum(1 for i in P2i if order and i < RQ[0]); after = [F(i) for i in P2i if order and i > RSM[1]]
    res.append(('5 P2 >= 400 before the first request, >= 600 after the second resume; no GUARD; HOLD <= 15', before >= 400 and len(after) >= 600 and G == 0 and H <= 15, f'P2 {before} before, {len(after)} after; GUARD {G}, HOLD {H}'))
    che = sum(1 for l in LG if 'cross hold end:' in l and '(lost)' in l); lend = sum(1 for l in LG if 'lost end:' in l); ch = sum(1 for l in LG if 'cross hold:' in l)
    res.append(('6 runner: one lost.csv row with state cross; cross hold ended as lost; one "lost end"; two cross holds', len(LC) == 1 and LC[0][2] == 'cross' and che == 1 and lend == 1 and ch == 2, f'lost.csv {LC}; ended as lost {che}; lost end {lend}; cross holds {ch}'))
    res.append(('7 no STOP row', not stops, f'STOP {len(stops)}'))
    res.append(('8 one T2SUM with lost=1 and epochs=1 (the dest level)', sum(1 for t in T2S if t.get('lost') == '1' and t.get('epochs') == '1') == 1, f'{T2S}'))
    if SPOT: dd = near(after[0] if after else None); res.append(('9 first P2 after the second resume within 5 m of the spot', dd is not None and dd <= 5, f'{dd} m'))
ok = all(r[1] for r in res)
out = [f'{n}: {"PASS" if p else "FAIL"} ({d})' for n, p, d in res]
for k in ('CROSS_REQ', 'CROSS_RESUME', 'LOST', 'EPOCH', 'FOUND', 'ABSORB_ARM', 'ABSORB_DISARM', 'RESET', 'T2SUM', 'VITALS'): out += [f'reported: {M[i]}' for i in idx(k)]
out += [f'reported: runner {l}' for l in LG if re.search(r'LOST|lost|freeze|cross hold| end:', l)]
ARM = sys.argv[3]
out.append(f'R3 arm D ({ARM}, {AW}) X SIDE: {"PASS" if ok else "FAIL"}')
print('\n'.join(out)); open(OUT or os.path.join(O, f't3_armD_{ARM}_{AW}_xray_check.txt'), 'w').write('\n'.join(out) + '\n')
sys.exit(0 if ok else 1)
