#!/usr/bin/env python3
"""T3 no-bridge check (PREREG-B1-T2-xray-side section 37). Run ONCE.  t3_nobridge_check.py <out dir of t3_nobridge.sh>
PASS needs all of, for BOTH boots (Stance14, Absorb14): (1) t1_xray ended with rc=0 and track_end=1, no FATAL ERROR in
xray.log; (2) no line starting STANCE in meta_t1.csv and no 'set_actor_stance' in xray.log; (3) MOVESUM present with
crouch=0, low=0, unread=0 and frames >= 10000; and (4) stance_api = -1 on Stance14 and = na on Absorb14."""
import sys, os, re
O = sys.argv[1]
rd = lambda p: open(p, 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n') if os.path.exists(p) else []
res = []; rep = []
for E, api in (('Stance14', '-1'), ('Absorb14', 'na')):
    M = rd(f'{O}/{E}/meta_t1.csv'); X = rd(f'{O}/{E}/tb/xray.log'); L = [l for l in rd(f'{O}/{E}/t1_xray.log') if l]
    end = L[-1] if L else ''
    ok1 = ' rc=0 ' in end + ' ' and 'track_end=1' in end and 'tb_run rc=0' in end and not any('FATAL ERROR' in l for l in X)
    res.append((f'1 {E}: track ended, rc 0, no FATAL', ok1, end[:120]))
    st = sum(1 for l in M if l.startswith('STANCE')); sx = sum(1 for l in X if 'set_actor_stance' in l)
    res.append((f'2 {E}: no STANCE line, no set_actor_stance in the log', st == 0 and sx == 0, f'{st} meta, {sx} log'))
    ms = [l for l in M if l.startswith('MOVESUM')]; kv = dict(p.split('=') for p in ms[-1].split(',')[1:]) if ms else {}
    ok3 = bool(kv) and kv.get('crouch') == '0' and kv.get('low') == '0' and kv.get('unread') == '0' and int(kv.get('frames', 0)) >= 10000
    res.append((f'3 {E}: MOVESUM crouch=0 low=0 unread=0 frames>=10000', ok3, ms[-1] if ms else 'no MOVESUM line'))
    res.append((f'4 {E}: stance_api = {api}', kv.get('stance_api') == api, kv.get('stance_api', 'missing')))
    rep.append(f'reported {E}: ' + next((l for l in M if l.startswith('SUMMARY')), 'no SUMMARY'))
txt = '\n'.join(f'{n}: {"PASS" if ok else "FAIL"} ({d})' for n, ok, d in res) + '\n' + '\n'.join(rep)
txt += f'\nNO-BRIDGE CHECK: {"PASS" if all(r[1] for r in res) else "FAIL"}\n'
print(txt, end=''); open(os.path.join(O, 't3_nobridge_check.txt'), 'w').write(txt)
