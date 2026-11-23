#!/usr/bin/env python3
"""B1-T2 X-checks (perf-harness/PREREG-B1-T2-JOINT-2026-11-23.md, Part X). Run ONCE per run, after the slot.
  t2_xray_check.py <out_dir of t2_xray.sh> <r1|r2>
X-kill  (r1): restarts.csv has exactly one planned=1 row; the first watcher stamp of the next boot is <= 90 s after it.
X-noplan    : no planned=0 row in restarts.csv, and t2_xray.log ends with "rc=0".
X-freeze    : r2: every STOP row has a CONT row after it; game_ms CONT - STOP (mod 2^32) <= 1000 for each pair; one
              "[FREEZE] absorbed" engine line per STOP (all boots). r1 (no deaths): no STOP row (reported, PASS if none).
X-track     : P lines (all boots): actor-to-pose distance after apply, p95 <= 1.0 m. Off-mesh HOLDs and TORN reads reported.
Every engine-written file is read with \\r stripped (memory: wine-crlf-readers).
"""
import sys, os, re, glob
O, RUN = sys.argv[1], sys.argv[2]
def lines(p):
    try: return open(p, 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n')
    except OSError: return []
def csv(p):
    L = [l for l in lines(p) if l]; return [dict(zip(L[0].split(','), l.split(','))) for l in L[1:]] if L else []
res = {}
rs = csv(os.path.join(O, 'restarts.csv'))
planned = [r for r in rs if r.get('planned') == '1']; unpl = [r for r in rs if r.get('planned') == '0']
boots = sorted(glob.glob(os.path.join(O, 'boot*')), key=lambda b: int(re.sub(r'\D', '', os.path.basename(b)) or 0))
if RUN == 'r1':
    if len(planned) == 1:
        k = int(planned[0]['boot']); ku = int(planned[0]['utc_ms'])
        nb = os.path.join(O, f'boot{k + 1}', 'watch', 'written.csv'); w = csv(nb)
        # the new boot's watcher first stamps the KILLED boot's last snapshot still in snap.txt (its seq is high);
        # the new boot's own snapshots restart at seq 1, so take the first stamp whose seq is below its predecessor's
        # (or the first row, if that is already a low seq). Found on pre-flight 4's data: the naive first row gave 2.8 s.
        first = None
        for i, r in enumerate(w):
            sq = int(r['seq'])
            if (i == 0 and sq <= 5) or (i > 0 and sq < int(w[i - 1]['seq'])): first = r; break
        dt = (int(first['utc_ms']) - ku) if first else None
        res['X-kill'] = (dt is not None and dt <= 90000, f'kill at {ku}, boot {k + 1} first snapshot after {dt} ms')
    else:
        res['X-kill'] = (False, f'{len(planned)} planned rows (need exactly 1)')
log = [l for l in lines(os.path.join(O, 't2_xray.log')) if l]
rc0 = bool(log) and log[-1].rstrip().endswith('rc=0')
res['X-noplan'] = (not unpl and rc0, f'{len(unpl)} unplanned restarts ({"; ".join(r["reason"] for r in unpl)}); last line rc=0: {rc0}')
fz = csv(os.path.join(O, 'freeze.csv'))
stops = [r for r in fz if r.get('event') == 'STOP']; conts = [r for r in fz if r.get('event') == 'CONT']
absorbed = sum(sum(1 for l in lines(os.path.join(b, 'tb', 'xray.log')) if '[FREEZE] absorbed' in l) for b in boots)
if RUN == 'r2':
    pairs, bad = [], []
    for s in stops:
        c = next((c for c in conts if int(c['utc_ms']) > int(s['utc_ms'])), None)
        if not c or not s.get('game_ms') or not c.get('game_ms'): bad.append(s['utc_ms']); continue
        d = (int(c['game_ms']) - int(s['game_ms'])) % 2**32; pairs.append(d)
        if d > 1000: bad.append(f"{s['utc_ms']}:{d}")
    ok = bool(stops) and not bad and absorbed == len(stops)
    res['X-freeze'] = (ok, f'{len(stops)} STOP, {len(conts)} CONT; game_ms deltas {pairs}; bad {bad}; [FREEZE] absorbed lines {absorbed}')
else:
    res['X-freeze'] = (not stops, f'r1: {len(stops)} STOP rows (none expected); absorbed lines {absorbed}')
P, holds, torn = [], 0, 0
for b in boots:
    for l in lines(os.path.join(b, 'meta_t2.csv')):
        f = l.split(',')
        if f[0] == 'P' and len(f) >= 5: P.append(float(f[4]))
        holds += f[0] == 'HOLD'; torn += f[0] == 'TORN'
Ps = sorted(P); p95 = Ps[min(len(Ps) - 1, int(0.95 * len(Ps)))] if Ps else None
res['X-track'] = (p95 is not None and p95 <= 1.0, f'{len(P)} poses applied; dist p50 {Ps[len(Ps) // 2] if Ps else None} p95 {p95} max {Ps[-1] if Ps else None} m; off-mesh holds {holds}; torn {torn}')
ok = all(v[0] for v in res.values())
txt = '\n'.join(f'{k} {"PASS" if v[0] else "FAIL"}: {v[1]}' for k, v in res.items()) + f'\nX SIDE ({RUN}): {"PASS" if ok else "FAIL"}\n'
print(txt, end=''); open(os.path.join(O, f't2_xray_check_{RUN}.txt'), 'w').write(txt)
