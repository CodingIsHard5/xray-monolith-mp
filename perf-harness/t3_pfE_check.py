#!/usr/bin/env python3
"""T3 pre-flight E check (forced damage at the actor; PREREG-B1-T2-xray-side section 46). Run ONCE per boot.
  t3_pfE_check.py <out dir of t2_preflight.sh> <prot|god_off|god_off_norefill>
All modes: (1) rc 0, no Lua error naming zz_p1_puppet, no FATAL ERROR, the DMG_MODE line names the mode, and the S
line shows g_god=true for prot / false for the witnesses; (2) rad: one DMG_TARGET,rad and >= 40 DMG_AT,rad lines with
the actor <= 1.5 m from the zone centre and the zone online; (3) anom: the same for the anomaly; (4) mut: three
DMG_SPAWN, no DMG_SPAWN_ERROR, and at least one spawned mutant with >= 10 DMG_MUT lines alive, its enemy = the actor
(id 0) and <= 3.0 m away.
prot adds (5): vitals unchanged: health >= 0.999, radiation <= 0.0001, bleeding <= 0.0001 on every DMG_AT / DMG_MUT
line, in all three DMG_PHASE_END lines and in the VITALS line (0 unread); SUMMARY refills = 0.
Witnesses add (W): damage shows in EVERY phase: its DMG_PHASE_END has hits >= 1, or min_health < 0.99, or
max_radiation > 0.001, or max_bleeding > 0.001. god_off_norefill also needs min_health < 0.9 or max_radiation > 0.01
in at least one phase.
E2 (section 49) adds for ALL modes (6): the end of the test: exactly one DMG_RESTORE line with g_god=true, clear=ok and
remove_errors=0, and exactly one DMG_ENDCHECK line (10 s after the last phase) with alive=1, health >= 0.999,
radiation <= 0.0001, bleeding <= 0.0001, mutants_left=0 and g_god=true.
Mode god_off_norefill_norestore is the MUTATION (the restore is skipped on purpose): it is scored as a witness and
must FAIL on (6) or (1).
Reported: targets, per-phase lines, hit lines by hitter section and type, DMG_FLOOR and REFILL counts."""
import sys, os, re, collections
O, MODE = sys.argv[1], sys.argv[2]
rd = lambda p: open(p, 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n') if os.path.exists(p) else []
M = [l for l in rd(f'{O}/run/boot0/meta_t2.csv') if l]; X = rd(f'{O}/run/boot0/tb/xray.log'); LG = [l for l in rd(f'{O}/run/t2_xray.log') if l]
HT = [l.split(',') for l in rd(f'{O}/run/boot0/hits_t2.csv') if l.startswith('h,')]
lua_err = [l for l in X if re.search(r'LUA error|lua error', l) and 'zz_p1_puppet' in l] + [X[i] for i in range(len(X)) if 'zz_p1_puppet.script (line' in X[i] and not any("doesn't exist" in X[j] or 'nil function' in X[j] for j in range(max(0, i - 8), i))]
fatal = [l for l in X if 'FATAL ERROR' in l]; rc0 = bool(LG) and LG[-1].rstrip().endswith('rc=0')
S = next((l for l in M if l.startswith('S,')), ''); god = 'g_god=true' in S
mode_ok = f'DMG_MODE,{MODE}' in M
kv = lambda l: dict(p.split('=') for p in l.split(',') if '=' in p)
res = [('1 clean run, mode line, god mode as registered', rc0 and not lua_err and not fatal and mode_ok and god == (MODE == 'prot'), f'rc0 {rc0}, lua errors {len(lua_err)}, fatal {len(fatal)}, mode line {mode_ok}, g_god {god}')]
AT = [l.split(',') for l in M if l.startswith('DMG_AT,')]; MU = [l.split(',') for l in M if l.startswith('DMG_MUT,')]
for n, ph in (('2', 'rad'), ('3', 'anom')):
    tg = [l for l in M if l.startswith(f'DMG_TARGET,{ph},')]; good = [a for a in AT if a[1] == ph and float(a[3]) <= 1.5 and a[4] == '1']
    res.append((f'{n} {ph}: one target, >= 40 lines inside (<= 1.5 m, zone online)', len(tg) == 1 and len(good) >= 40, f'{len(tg)} target, {len(good)} of {sum(1 for a in AT if a[1] == ph)} lines inside; {",".join(tg[0].split(",")[4:6]) if tg else "none"}'))
SP = [l.split(',') for l in M if l.startswith('DMG_SPAWN,')]; SE = [l for l in M if l.startswith('DMG_SPAWN_ERROR')]
eng = collections.Counter(u[2] for u in MU if u[5] == '1' and u[6] == '0' and 0 <= float(u[4]) <= 3.0)
res.append(('4 mut: three spawned, at least one engaged for >= 10 lines (alive, enemy = actor, <= 3 m)', len(SP) == 3 and not SE and any(v >= 10 for v in eng.values()), f'{len(SP)} spawned, {len(SE)} errors, engaged lines per mutant {dict(eng)}'))
PE = {l.split(',')[1]: kv(l) for l in M if l.startswith('DMG_PHASE_END,')}
VT = [kv(l) for l in M if l.startswith('VITALS,')]; SM = [kv(l) for l in M if l.startswith('SUMMARY,')]
if MODE == 'prot':
    okv = lambda h, r, b: float(h) >= 0.999 and float(r) <= 0.0001 and float(b) <= 0.0001
    bad = sum(1 for a in AT if not okv(a[5], a[6], a[7])) + sum(1 for u in MU if not okv(u[7], u[8], u[9]))
    pe_ok = len(PE) == 3 and all(okv(p['min_health'], p['max_radiation'], p['max_bleeding']) for p in PE.values())
    v_ok = len(VT) == 1 and okv(VT[0]['min_health'], VT[0]['max_radiation'], VT[0]['max_bleeding']) and VT[0]['unread'] == '0'
    res.append(('5 vitals unchanged on every line, in every phase and at the end; no refill', bool(AT) and bool(MU) and bad == 0 and pe_ok and v_ok and bool(SM) and SM[-1]['refills'] == '0', f'{bad} bad lines of {len(AT) + len(MU)}; phases {pe_ok}; VITALS {VT}; refills {SM[-1]["refills"] if SM else None}'))
else:
    shown = {k: (int(p['hits']) >= 1 or float(p['min_health']) < 0.99 or float(p['max_radiation']) > 0.001 or float(p['max_bleeding']) > 0.001) for k, p in PE.items()}
    big = any(float(p['min_health']) < 0.9 or float(p['max_radiation']) > 0.01 for p in PE.values())
    res.append(('W damage shows in every phase with the protection off', len(PE) == 3 and all(shown.values()) and (not MODE.startswith('god_off_norefill') or big), f'{shown}; clear damage {big}'))
RSL = [kv(l) for l in M if l.startswith('DMG_RESTORE,')]; EC = [kv(l) for l in M if l.startswith('DMG_ENDCHECK,')]
r_ok = len(RSL) == 1 and RSL[0].get('g_god') == 'true' and RSL[0].get('clear') == 'ok' and RSL[0].get('remove_errors') == '0'
e = EC[0] if len(EC) == 1 else {}
e_ok = bool(e) and e['alive'] == '1' and float(e['health']) >= 0.999 and float(e['radiation']) <= 0.0001 and float(e['bleeding']) <= 0.0001 and e['mutants_left'] == '0' and e['g_god'] == 'true'
res.append(('6 end of test: protection restored, mutants removed; 10 s later alive, full health, not bleeding, god mode on', r_ok and e_ok, f'{len(RSL)} RESTORE {RSL[:1]}; {len(EC)} ENDCHECK {EC[:1]}'))
txt = '\n'.join(f'{n}: {"PASS" if ok else "FAIL"} ({d})' for n, ok, d in res)
txt += '\nreported: ' + '\nreported: '.join([l for l in M if l.startswith(('DMG_TARGET', 'DMG_NOTARGET', 'DMG_SPAWN', 'DMG_PHASE_END', 'DMG_REMOVE', 'DMG_RESTORE', 'DMG_NORESTORE', 'DMG_ENDCHECK', 'VITALS', 'SUMMARY'))] or ['no DMG lines'])
txt += f'\nreported: hit lines {len(HT)} by hitter section and type: {dict(collections.Counter((h[3], h[5]) for h in HT).most_common(12))}'
txt += f'\nreported: DMG_FLOOR {sum(1 for l in M if l.startswith("DMG_FLOOR"))}, REFILL {sum(1 for l in M if l.startswith("REFILL"))}'
txt += f'\nPRE-FLIGHT E ({MODE}): {"PASS" if all(r[1] for r in res) else "FAIL"}\n'
print(txt, end=''); open(os.path.join(O, f't3_pfE_check_{MODE}.txt'), 'w').write(txt)
