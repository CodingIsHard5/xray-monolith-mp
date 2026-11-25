#!/usr/bin/env python3
"""Self-test of s2_partA2_score.py on the eight runs of part A (real engine bytes) and on mutants built from them."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s2_partA2_score as S
O = sys.argv[1]; D = lambda t: os.path.join(O, 'det_' + t); bad = []
def want(name, got, exp):
    print(f"{'ok  ' if got == exp else 'BAD '} {name}: {got}");  bad.append(name) if got != exp else None
ST = ['s2ref_a', 's2ref_b', 's2ref_c', 's2ref_d']; OF = ['s2off_a', 's2off_b', 's2off_c', 's2off_d']
tr = [S.alife_frames(D(t), t) for t in ST + OF]; fr = {t: S.cframes(D(t)) for t in ST + OF}
want('R4 on the eight real runs', S.r4(tr)[0], True)
m = [list(x) for x in tr]; m[5] = m[5][:40] + [m[5][40] - 9] + m[5][40:]          # an extra call between two calls
want('R4 mutant: one extra A-Life call in one run', S.r4(m)[0], False)
m = [list(x) for x in tr]; m[6] = [f + 1 for f in m[6]]
want('R4 mutant: first call one frame late in one run', S.r4(m)[0], False)
m = [list(x) for x in tr]; m[2] = m[2][:30] + [f + 17 for f in m[2][30:]]          # a call skipped (gap 34)
want('R4 mutant: one call skipped', S.r4(m)[0], False)
# R3 on real traces: frame of the 100th A-Life call stands in for the checkpoint frame (time checkpoints have equal frames)
f = lambda L: [{1: x[99], 2: x[99], 3: x[99]} for x in L]
ok, d = S.r3(f(tr[:4]), f(tr[4:])); want(f'R3 on the real runs, 100th call (diff {d[1]:+.2f})', ok, True)
ok, d = S.r3(f(tr[:4]), [{k: v - 17 for k, v in x.items()} for x in f(tr[4:])]); want(f'R3 mutant: off runs one pass (17 frames) early (diff {d[1]:+.2f})', ok, False)
ok, d = S.r3(f(tr[:4]), [{k: v + 17 for k, v in x.items()} for x in f(tr[4:])]); want('R3 mutant: off runs one pass late', ok, False)
# counters on real data: the two stock runs agree, the off runs of the time-checkpoint retry do not (section 8)
want('counters stock c vs d', S.counters(D('s2ref_c'), D('s2ref_d')), {1: 0, 2: 0, 3: 0})
want('counters stock c vs off c (the known C=20)', S.counters(D('s2ref_c'), D('s2off_c')), {1: 0, 2: 20, 3: 20})
want('counters with a missing file -> None', S.counters(D('s2ref_a'), D('s2ref_b')), None)
print('SELFTEST ' + ('PASS' if not bad else f'FAIL {bad}')); sys.exit(1 if bad else 0)
