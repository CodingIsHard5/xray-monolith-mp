#!/usr/bin/env python3
"""R1b-1 scorer (PREREG-R1b-1-2026-11-23.md + amendments 1, 2 + run plan). Run once, after the slice.
Per run: main-thread render feed = "Process seqRender" inclusive main ms / frame; S_static = ("Render Cascade: SMAP
traverse" - "r1b_cap_dynamic") inclusive main ms / frame on OFF runs; frame time mean and p99 from perf_frames.
Decisions:
  Bar A: mean S_static of the Rostok OFF arm >= 0.15 ms, else STOP (ON not scored).
  Gain G = mean(seqRender OFF) - mean(seqRender ON), Rostok. Slice stop if G < 0.03; 1b-2 eligible only if G >= 0.06.
  Metric spread (registered "spread < 0.03 ms"): max-min of seqRender within each arm.
  Frame time no worse: mean(ON means) <= mean(OFF means) + (max-min of OFF means); same rule for p99.
  Check 4 (4-core): seqRender ON <= OFF.
  Check 3(iii): per scene, the set of error/warning lines (numbers masked) and the set of [LSS] spawn lines equal
  across all OFF and ON runs of that scene.
"""
import csv, re, statistics as st, sys, os
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'out')

def sites(tag):
    d, frames = {}, None
    with open(f'{OUT}/{tag}/perf_sites_{tag}.csv', encoding='utf-8', errors='replace') as f:
        for row in csv.reader(f):
            if row and row[0] == '#frames': frames = int(row[1]); continue
            if len(row) >= 9 and row[3].isdigit(): d[row[0]] = (int(row[3]), float(row[4]), int(row[6]), float(row[7]))
    return d, frames

def frametimes(tag):
    with open(f'{OUT}/{tag}/perf_frames_{tag}.csv') as f:
        v = sorted(float(r['ms']) for r in csv.DictReader(f))
    return st.mean(v), v[min(len(v) - 1, int(0.99 * len(v)))]

def run(tag):
    d, n = sites(tag)
    inc = lambda k: d.get(k, (0, 0.0, 0, 0.0))[1] / n
    m, p99 = frametimes(tag)
    return dict(tag=tag, seq=inc('Process seqRender'), sstat=inc('Render Cascade: SMAP traverse') - inc('r1b_cap_dynamic'),
                cdyn=inc('r1b_cap_dynamic'), join=inc('r1b_join'), mean=m, p99=p99, frames=n)

def norm(line):
    return re.sub(r'0x[0-9a-fA-F]+|\d+(\.\d+)?', 'N', line.strip())

def logsets(tag):
    ew, lss = set(), set()
    with open(f'{OUT}/{tag}/xray.log', encoding='utf-8', errors='replace') as f:
        for l in f:
            if 'r1b_selftest' in l: continue
            if l.startswith('!') or re.search(r'error|warning', l, re.I): ew.add(norm(l))
            if '[LSS] Spawning object' in l: lss.add(norm(l))
    return ew, lss

def arm(scene, kind, n=3): return [run(f'r1b_{scene}_{kind}_{i}') for i in range(1, n + 1)]
def mm(rs, k): return st.mean(r[k] for r in rs)
def sp(rs, k): v = [r[k] for r in rs]; return max(v) - min(v)

out = []
P = lambda s='': (out.append(s), print(s))
for scene in ('ros', 'cor'):
    for kind in ('off', 'on'):
        for r in arm(scene, kind):
            P(f"{r['tag']:16s} seqRender {r['seq']:.4f}  S_static {r['sstat']:.4f}  cap_dyn {r['cdyn']:.4f}  join {r['join']:.4f}  frame mean {r['mean']:.4f} p99 {r['p99']:.4f}")
P()
off, on = arm('ros', 'off'), arm('ros', 'on')
bar = mm(off, 'sstat')
P(f"BAR A (Rostok OFF S_static mean) = {bar:.4f} ms, bar 0.15 -> {'PASS' if bar >= 0.15 else 'FAIL: STOP, ON not scored'}")
if bar < 0.15: open(f'{OUT}/r1b1_score.txt', 'w').write('\n'.join(out) + '\n'); sys.exit(0)
for scene in ('ros', 'cor'):
    off, on = arm(scene, 'off'), arm(scene, 'on')
    G = mm(off, 'seq') - mm(on, 'seq')
    P(f"[{scene}] seqRender OFF {mm(off,'seq'):.4f} (spread {sp(off,'seq'):.4f})  ON {mm(on,'seq'):.4f} (spread {sp(on,'seq'):.4f})  GAIN {G:+.4f} ms")
    for k in ('mean', 'p99'):
        lim = mm(off, k) + sp(off, k)
        P(f"[{scene}] frame {k}: OFF {mm(off,k):.4f} (spread {sp(off,k):.4f})  ON {mm(on,k):.4f}  limit {lim:.4f} -> {'no worse' if mm(on,k) <= lim else 'WORSE'}")
    if scene == 'ros':
        P(f"[ros] slice rule (G >= 0.03): {'continue' if G >= 0.03 else 'STOP R1b'}; 1b-2 rule (G >= 0.06): {'eligible' if G >= 0.06 else ('no 1b-2' if G < 0.03 else 'no 1b-2, Overseer decides')}")
    sets = {r['tag']: logsets(r['tag']) for r in off + on}
    ew0, lss0 = next(iter(sets.values()))
    ok_ew = all(s[0] == ew0 for s in sets.values()); ok_lss = all(s[1] == lss0 for s in sets.values())
    P(f"[{scene}] check 3(iii): error/warning sets equal {ok_ew} ({len(ew0)} distinct); LSS spawn sets equal {ok_lss} ({len(lss0)} distinct)")
    if not ok_ew:
        for t, s in sets.items():
            if s[0] != ew0: P(f"    {t}: +{sorted(s[0]-ew0)[:5]} -{sorted(ew0-s[0])[:5]}")
a, b = run('r1b_ros4_off'), run('r1b_ros4_on')
P(f"[4-core] seqRender OFF {a['seq']:.4f} ON {b['seq']:.4f} -> {'not worse' if b['seq'] <= a['seq'] else 'WORSE'}; frame mean OFF {a['mean']:.4f} ON {b['mean']:.4f}; p99 OFF {a['p99']:.4f} ON {b['p99']:.4f}")
open(f'{OUT}/r1b1_score.txt', 'w').write('\n'.join(out) + '\n')
