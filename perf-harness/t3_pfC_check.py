#!/usr/bin/env python3
"""T3 pre-flight C check (R3 level change; PREREG-B1-T2-xray-side section 38). Run ONCE.
  t3_pfC_check.py <out dir of t2_preflight.sh> <from level> <dest level>
PASS needs all of: (1) t2_xray rc 0, no Lua error naming zz_p1_puppet, no FATAL ERROR; (2) exactly one CROSS_REQ
<from> -> <dest>, no CROSS_NOCHANGER / CROSS_ERROR; (3) exactly one CROSS_RESUME naming <dest>, after the CROSS_REQ;
(4) the writer saw a snapshot naming <dest> (phase Y rows) at most 180 s after it first sent `cross`; (5) after the
resume: at least 200 P2 lines and no GUARD line; (6) the runner logged "cross hold" and restarts.csv has no row.
Reported: load time, the CHANGER table, lines before/after, T2SUM / MOVESUM lines, HOLD count after the resume."""
import sys, os, re
O, FROM, DEST = sys.argv[1:4]
rd = lambda p: open(p, 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n') if os.path.exists(p) else []
M = [l for l in rd(f'{O}/run/boot0/meta_t2.csv') if l]; X = rd(f'{O}/run/boot0/tb/xray.log'); LG = [l for l in rd(f'{O}/run/t2_xray.log') if l]
W = [l.split(',') for l in rd(f'{O}/pose_written.csv')[1:] if l]; RS = [l for l in rd(f'{O}/run/restarts.csv')[1:] if l]
lua_err = [l for l in X if re.search(r'LUA error|lua error', l) and 'zz_p1_puppet' in l] + [X[i] for i in range(len(X)) if 'zz_p1_puppet.script (line' in X[i] and not any("doesn't exist" in X[j] or 'nil function' in X[j] for j in range(max(0, i - 8), i))]
fatal = [l for l in X if 'FATAL ERROR' in l]; rc0 = bool(LG) and LG[-1].rstrip().endswith('rc=0')
req = [i for i, l in enumerate(M) if l.startswith('CROSS_REQ,')]; res_ = [i for i, l in enumerate(M) if l.startswith('CROSS_RESUME,')]
bad = [l for l in M if l.startswith('CROSS_NOCHANGER') or l.startswith('CROSS_ERROR')]
rq = M[req[0]].split(',') if req else []
ok2 = len(req) == 1 and not bad and rq[3:5] == [FROM, DEST]
ok3 = len(res_) == 1 and bool(req) and res_[0] > req[0] and M[res_[0]].split(',')[2] == DEST
tx = next((int(w[0]) for w in W if w[2] == 'X'), None); ty = next((int(w[0]) for w in W if w[2] == 'Y'), None)
load = (ty - tx) / 1000 if tx and ty else None
after = M[res_[0]:] if res_ else []
p2a = sum(1 for l in after if l.startswith('P2,')); ga = sum(1 for l in after if l.startswith('GUARD,')); ha = sum(1 for l in after if l.startswith('HOLD,'))
res = [('1 rc 0, no Lua error, no FATAL', rc0 and not lua_err and not fatal, f'rc0 {rc0}, lua errors {len(lua_err)}, fatal {len(fatal)}'),
       (f'2 one CROSS_REQ {FROM} -> {DEST}, no NOCHANGER/ERROR', ok2, f'{len(req)} CROSS_REQ {rq[3:6]}, {len(bad)} bad'),
       (f'3 one CROSS_RESUME on {DEST} after it', ok3, M[res_[0]] if res_ else 'none'),
       ('4 new level in the snapshot <= 180 s after `cross`', load is not None and load <= 180, f'{load} s'),
       ('5 after the resume: >= 200 P2, no GUARD', p2a >= 200 and ga == 0, f'{p2a} P2, {ga} GUARD'),
       ('6 runner logged the cross hold, no restart', any('cross hold' in l for l in LG) and not RS, f'{len(RS)} restart rows')]
txt = '\n'.join(f'{n}: {"PASS" if ok else "FAIL"} ({d})' for n, ok, d in res)
txt += f'\nreported: meta lines before the request {req[0] if req else None}, after the resume {len(after)}; HOLD after the resume {ha}'
txt += '\nreported: ' + '\nreported: '.join([l for l in M if l.startswith(('CHANGER,', 'CROSS_', 'T2SUM', 'MOVESUM', 'CHANGER_ERROR'))] or ['no changer / summary lines'])
txt += f'\nPRE-FLIGHT C: {"PASS" if all(r[1] for r in res) else "FAIL"}\n'
print(txt, end=''); open(os.path.join(O, 't3_pfC_check.txt'), 'w').write(txt)
