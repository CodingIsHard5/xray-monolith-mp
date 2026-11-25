#!/usr/bin/env python3
"""R3 arm C (X-Ray killed inside its level load, restarted, recovered by ITR2's cross pose), X side. Run ONCE.
  t3_armC_xray_check.py <t2_xray out dir> <changer table csv>
Fixed: home l01_escape, dest l02_garbage, changer esc_level_changer_to_garbage_1 for both requests.
PASS needs all of:
 1  the runner ended with rc=0 (ran to the end, no unplanned restart); no Lua error naming zz_p1_puppet and no
    FATAL ERROR in either boot's engine log; no DMG line.
 2  boot 0: exactly one CROSS_REQ, by the changer with its table values; NO CROSS_RESUME (the kill landed inside the
    level load); no CROSS_NOCHANGER / CROSS_ERROR in either boot.
 3  restarts.csv: exactly one row, planned=1, "planned kill (arm C) <n> s after the cross pose" with n 4..6; the runner
    logged the restart wait; AND the outcome the marker rule protects: the runner's unconditional line "boot 1: crossing
    marker at restart: was <n|absent> bytes, now <0|absent> bytes" shows the marker empty or absent after the runner's
    step, and boot 1 logs no CROSS_RESUME before its own request (no false arrival). (c1's checker asked for a line the
    runner wrote only for a non-empty marker; in c1 the new level's script had already taken it.)
 4  boot 1's log is its own (not appended): exactly one CROSS_REQ (home -> dest, the changer, table values) and exactly
    one CROSS_RESUME, on dest, after it; no CROSS_RESUME before the request.
 5  boot 1: the first applied play pose is written by ITR2 at most 90 s after the `cross` pose of the request.
 6  applied poses: boot 0 at least 400 before its request; boot 1 at least 600 after the resume; no GUARD in either
    boot; at most 15 HOLD in all.
 7  boot 1: the first applied pose after the resume is within 5 m (x,z) of the changer's destination.
 8  on every applied pose of both boots: stance read-back = pose stance, movement state readable, height = node + 0.05.
 9  VITALS: boot 0 none or one, boot 1 exactly two; each min_health >= 0.999, radiation and bleeding <= 0.0001,
    unread 0; no REFILL line.
Reported, not required: ZONE_MISMATCH count in boot 1 (expected 0: the cross pose is there at the first read), game
time from boot 1's first S line to its CROSS_REQ (the level change soon after a load), poses applied on Cordon in boot 1."""
import sys, os, re, math
O, TAB = sys.argv[1:3]
HOME, DEST, CH = 'l01_escape', 'l02_garbage', 'esc_level_changer_to_garbage_1'
rd = lambda p: open(p, 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n') if os.path.exists(p) else []
M = [[l for l in rd(f'{O}/boot{k}/meta_t2.csv') if l] for k in (0, 1)]; X = [rd(f'{O}/boot{k}/tb/xray.log') for k in (0, 1)]
LG = [l for l in rd(f'{O}/t2_xray.log') if l]; RS = [l for l in rd(f'{O}/restarts.csv')[1:] if l]
T = {f[1]: f for f in (l.split(',') for l in rd(TAB) if l.startswith('CHANGER,'))}.get(CH)
def lua_err(x): return [l for l in x if re.search(r'LUA error|lua error', l) and 'zz_p1_puppet' in l] + [x[i] for i in range(len(x)) if 'zz_p1_puppet.script (line' in x[i] and not any("doesn't exist" in x[j] or 'nil function' in x[j] for j in range(max(0, i - 8), i))]
le = sum(len(lua_err(x)) for x in X); fatal = sum(1 for x in X for l in x if 'FATAL ERROR' in l); dmg = sum(1 for m in M for l in m if l.startswith('DMG_'))
rc0 = bool(LG) and LG[-1].rstrip().endswith('rc=0') and 'ran to the end' in LG[-1] and 'unplanned restarts 0' in LG[-1]
idx = lambda m, p: [i for i, l in enumerate(m) if l.startswith(p)]
req = [idx(m, 'CROSS_REQ,') for m in M]; rsm = [idx(m, 'CROSS_RESUME,') for m in M]
bad = sum(1 for m in M for l in m if l.startswith(('CROSS_NOCHANGER', 'CROSS_ERROR')))
def req_ok(l):
    f = l.split(',')
    return bool(T) and f[3:6] == [HOME, DEST, CH] and all(abs(float(f[6 + k]) - float(T[4 + k])) <= 0.002 for k in range(3)) and f[9:11] == T[7:9]
ok2 = len(req[0]) == 1 and req_ok(M[0][req[0][0]]) and not rsm[0] and bad == 0
km = re.search(r'planned kill \(arm C\) (\d+) s after the cross pose', RS[0]) if len(RS) == 1 else None
MK = [x for x in (re.search(r'boot 1: crossing marker at restart: was (\d+|absent) bytes, now (\d+|absent) bytes', l) for l in LG) if x]
_rq1 = [i for i, l in enumerate(M[1]) if l.startswith('CROSS_REQ,')]; _rs1 = [i for i, l in enumerate(M[1]) if l.startswith('CROSS_RESUME,')]
no_false = not any(i < (_rq1[0] if _rq1 else 10**9) for i in _rs1)
ok3 = bool(km) and RS[0].rstrip().endswith(',1') and 4 <= int(km.group(1)) <= 6 and any('restart wait: ' in l for l in LG) and len(MK) == 1 and MK[0].group(2) in ('0', 'absent') and no_false
ok4 = len(req[1]) == 1 and len(rsm[1]) == 1 and req_ok(M[1][req[1][0]]) and req[1][0] < rsm[1][0] and M[1][rsm[1][0]].split(',')[2] == DEST
P = lambda ls: [l.split(',') for l in ls if l.startswith('P2,')]
p0 = P(M[0][:req[0][0]]) if req[0] else P(M[0]); p0x = P(M[0][req[0][0]:]) if req[0] else []
p1a = P(M[1][:rsm[1][0]]) if ok4 else P(M[1]); p1b = P(M[1][rsm[1][0]:]) if ok4 else []
ALL = p0 + p0x + p1a + p1b
dt = (int(p1b[0][2]) - int(M[1][req[1][0]].split(',')[11])) / 1000 if ok4 and p1b else None
G = sum(1 for m in M for l in m if l.startswith('GUARD,')); H = sum(1 for m in M for l in m if l.startswith('HOLD,'))
d = math.hypot(float(p1b[0][7]) - float(T[4]), float(p1b[0][9]) - float(T[6])) if T and p1b else None
Q = [q for q in ALL if len(q) > 19]
st_bad = sum(1 for q in Q if q[19] != q[15]); unread = sum(1 for q in Q if q[18] == '-1'); h_bad = sum(1 for q in ALL if abs(float(q[14]) - float(q[11]) - 0.05) > 0.01)
kv = lambda l: dict(p.split('=') for p in l.split(',')[1:] if '=' in p)
VT = [[kv(l) for l in m if l.startswith('VITALS,')] for m in M]; RF = sum(1 for m in M for l in m if l.startswith('REFILL,'))
clean = lambda v: float(v['min_health']) >= 0.999 and float(v['max_radiation']) <= 0.0001 and float(v['max_bleeding']) <= 0.0001 and v['unread'] == '0'
ok9 = len(VT[0]) <= 1 and len(VT[1]) == 2 and all(clean(v) for vs in VT for v in vs) and RF == 0
res = [('1 runner rc 0 and ran to the end, no Lua error, no FATAL, test mode off', rc0 and le == 0 and fatal == 0 and dmg == 0, f'rc0 {rc0}, lua errors {le}, fatal {fatal}, DMG lines {dmg}'),
       ('2 boot 0: one CROSS_REQ by the changer, NO CROSS_RESUME (kill inside the load), no NOCHANGER/ERROR', ok2, f'requests {len(req[0])}, resumes {len(rsm[0])}, bad {bad}'),
       ('3 one planned kill 4..6 s after the cross pose; restart wait logged; marker empty or absent after the runner\'s step; no false arrival in boot 1', ok3, ('; '.join(RS) or 'no restart row') + '; marker line: ' + ('; '.join(x.group(0) for x in MK) or 'NONE') + f'; resume before the request in boot 1: {not no_false}'),
       ('4 boot 1 own log: one CROSS_REQ home -> dest by the changer, then one CROSS_RESUME on dest', ok4, '; '.join(M[1][i] for i in req[1] + rsm[1]) or 'none'),
       ('5 boot 1: first applied play pose <= 90 s after the cross pose of the request (ITR2 clock)', dt is not None and 0 <= dt <= 90, f'{dt} s'),
       ('6 applied poses: boot 0 >= 400 before the request, boot 1 >= 600 after the resume; no GUARD; HOLD <= 15', len(p0) >= 400 and len(p1b) >= 600 and G == 0 and H <= 15, f'P2 boot 0 {len(p0)} (+{len(p0x)} after the request), boot 1 {len(p1a)} before / {len(p1b)} after the resume; GUARD {G}, HOLD {H}'),
       ('7 boot 1: first applied pose after the resume within 5 m of the changer destination', d is not None and d <= 5.0, f'{d} m'),
       ('8 stance read-back, movement state, height on every applied pose', bool(ALL) and len(Q) == len(ALL) and st_bad == 0 and unread == 0 and h_bad == 0, f'{st_bad} stance differ, {unread} unread, {h_bad} height off, of {len(ALL)}'),
       ('9 VITALS: boot 0 none or one, boot 1 two, each clean; no refill', ok9, f'{VT}; REFILL {RF}')]
txt = '\n'.join(f'{n}: {"PASS" if ok else "FAIL"} ({dd})' for n, ok, dd in res)
S1 = idx(M[1], 'S,'); gap = (int(M[1][req[1][0]].split(',')[1]) - int(M[1][S1[0]].split(',')[1])) / 1000 if S1 and req[1] else None
txt += f'\nreported: boot 1 ZONE_MISMATCH lines {sum(1 for l in M[1] if l.startswith("ZONE_MISMATCH,"))}; boot 1 request {gap} s of game time after its first S line'
for k in (0, 1): txt += f'\nreported boot {k}: ' + f'\nreported boot {k}: '.join([l for l in M[k] if l.startswith(('CROSS_', 'VITALS', 'T2SUM', 'CHANGER_ERROR'))] or ['no lines'])
txt += '\nreported runner: ' + '\nreported runner: '.join(l for l in LG if re.search(r'PLANNED KILL|restart wait|crossing marker|RESTART|cross hold|kill timer| end:', l))
txt += f'\nR3 arm C X SIDE: {"PASS" if all(r[1] for r in res) else "FAIL"}\n'
print(txt, end=''); open(os.path.join(O, 't3_armC2_xray_check.txt'), 'w').write(txt)
