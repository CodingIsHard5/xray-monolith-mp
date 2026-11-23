#!/usr/bin/env python3
"""B1-T1 X-Ray-side scorer (perf-harness/PREREG-B1-T1-2026-11-23.md, section 5). Run once, after the slot.
  t1_xray_check.py <out_dir of t1_xray.sh>
Checks (PASS = all of X1-X4; X5 reported):
  X1 ticks: snapshots stamped by the watcher, outside the stall window [STOP, CONT + 2 s], >= 95 % of the expected 10 Hz
     ticks over the same span (first to last stamp, minus the window).
  X2 export cost: per-tick W lines (meta_t1.csv), mean < 1000 us and p99 < 2000 us.
  X3 memory (joint cap, the same figures as ITRZone's T1c): min MemAvailable >= 8192 MB; swap growth <= 1024 MB.
  X4 X-Ray continues after the stall: stall.csv has STOP and CONT, a new seq is stamped within 1 s after CONT,
     meta_t1.csv has TRACK_END, and t1_xray.log ends with rc=0.
  X5 (reported): torn reads, X-Ray and ITR2 peak RSS and mean CPU %, snapshot size range.
"""
import csv, statistics as st, sys, os, re
O = sys.argv[1]
res = {}
def rows(p): return list(csv.DictReader(open(os.path.join(O, p))))
wr = [(int(r['utc_ms']), int(r['seq'])) for r in rows('watch/written.csv')]
stall = {r['event']: int(r['utc_ms']) for r in rows('stall.csv')} if os.path.exists(os.path.join(O, 'stall.csv')) else {}
s0, s1 = stall.get('STOP'), (stall['CONT'] + 2000 if 'CONT' in stall else None)
inwin = lambda t: s0 is not None and s1 is not None and s0 <= t <= s1
out = [t for t, _ in wr if not inwin(t)]
span = (wr[-1][0] - wr[0][0]) - ((s1 - s0) if s0 and s1 else 0)
exp = span / 100 + 1
res['X1'] = (len(out) >= 0.95 * exp, f'{len(out)} stamped outside the stall of {exp:.0f} expected ({100 * len(out) / exp:.1f} %)')
W = [float(l.split(',')[2]) for l in open(os.path.join(O, 'meta_t1.csv')) if l.startswith('W,')]
Ws = sorted(W); p99 = Ws[min(len(Ws) - 1, int(0.99 * len(Ws)))] if Ws else 1e9
res['X2'] = (bool(W) and st.mean(W) < 1000 and p99 < 2000, f'{len(W)} ticks, mean {st.mean(W) if W else 0:.0f} us, p99 {p99:.0f} us, max {max(W) if W else 0:.0f} us')
R = rows('watch/resources.csv')
ma = min(int(r['mem_available_mb']) for r in R); sw = [int(r['swap_used_mb']) for r in R]
res['X3'] = (ma >= 8192 and max(sw) - sw[0] <= 1024, f'min MemAvailable {ma} MB; swap {sw[0]} -> max {max(sw)} MB (growth {max(sw) - sw[0]})')
after = [t for t, _ in wr if 'CONT' in stall and t > stall['CONT']]
first = (after[0] - stall['CONT']) if after else None
log = open(os.path.join(O, 't1_xray.log')).read().strip().splitlines()
te = any(l.startswith('TRACK_END') for l in open(os.path.join(O, 'meta_t1.csv')))
rc0 = bool(log) and (re.findall(r'\brc=(\d+)', log[-1]) or ['x'])[-1] == '0'   # the LAST rc= is t1_xray.sh's own exit code
res['X4'] = ('STOP' in stall and 'CONT' in stall and first is not None and first <= 1000 and te and rc0,
             f'STOP/CONT {"yes" if "CONT" in stall else "NO"}; first seq {first} ms after CONT; TRACK_END {te}; rc=0 {rc0}')
torn = sum(1 for _ in open(os.path.join(O, 'watch/torn.csv'))) - 1
def col(i, k): return [float(r[f'{i}_{k}']) for r in R if r.get(f'{i}_{k}')]
x5 = f'torn polls {torn}; X-Ray RSS max {max(col(0, "rss_mb") or [0]):.0f} MB, CPU mean {st.mean(col(0, "cpu_pct") or [0]):.0f} %; ' \
     f'ITR2 RSS max {max(col(1, "rss_mb") or [0]):.0f} MB, CPU mean {st.mean(col(1, "cpu_pct") or [0]):.0f} %'
ok = all(v[0] for v in res.values())
txt = '\n'.join(f'{k} {"PASS" if v[0] else "FAIL"}: {v[1]}' for k, v in res.items()) + f'\nX5: {x5}\nX-RAY SIDE: {"PASS" if ok else "FAIL"}\n'
print(txt, end=''); open(os.path.join(O, 't1_xray_check.txt'), 'w').write(txt)
