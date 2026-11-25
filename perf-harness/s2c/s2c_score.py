#!/usr/bin/env python3
"""S2-C E1 scorer (PREREG-S2C-gate-overhead section 5). Run ONCE on the finished series.
  s2c_score.py <out root> [report file]
Arms (two runs each, folders <root>/<tag>_1 and _2, lowercase as the bench names them):
  s2c_c30 stock capped 30 | s2c_g30x31, s2c_g30x60, s2c_g30x120 tick 30 with the frame cap at 31, 60, 120 | s2c_g30u tick 30 uncapped
Reads ONLY the profiler's capture files (perf_sites_<tag>.csv, perf_frames_<tag>.csv) and conditions.txt: no log line.
Per run: U = calls of "Process seqFrame", N = frames, T = capture s; rate = U/T; fpt = N/U (frames per tick);
  sim = ("Process seqFrame" main ms + "Sheduler Deferred" + "seqParallel" + "seqFrameMT" all threads) / U   (PRIMARY, as part B)
  script = self ms of "CScriptBinder::shedule_Update" (all threads) / U; brain = self ms of "stalker/schedule_update/think/brain" / U
VALID run: files present by name, frames file rows = N, "VERDICT: clean", every site present, U > 0, rate within 3 % of 30.
An arm needs both runs valid; its value is their mean; SPREAD = |run1 - run2| / mean.
E(x) = sim(x) / sim(c30) - 1 for x in 31, 60, 120, u.   F(x) = E(x) / E(u).
VERDICT, in this order:
  NO VERDICT             an arm is not valid (named), or an arm's sim SPREAD is over 12 %
  EFFECT NOT REPRODUCED  E(u) < 0.15
  GATE PATH OR BEAT      E(31) > 0.10   (one frame per tick already costs: neither H1 nor H2 as written)
  H2 STEP                F(60) >= 0.70  (two frames per tick already give most of the extra)
  H1 RISE                F(60) <= 0.35 and F(60) <= F(120) <= 1.10
  NOT SEPARATED          anything else
Exit 0 with a verdict, 2 on NO VERDICT."""
import sys, os, statistics
ROOT = sys.argv[1]; OUTF = sys.argv[2] if len(sys.argv) > 2 else None
ARMS = ['s2c_c30', 's2c_g30x31', 's2c_g30x60', 's2c_g30x120', 's2c_g30u']
NEED = ['Process seqFrame', 'Sheduler Deferred', 'seqParallel', 'seqFrameMT', 'CScriptBinder::shedule_Update', 'stalker/schedule_update/think/brain']
def rd(p):
    if not os.path.isfile(p): raise FileNotFoundError(p)
    return open(p, 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n')
def run(tag):
    d = os.path.join(ROOT, tag)
    try:
        L = rd(os.path.join(d, f'perf_sites_{tag}.csv')); FR = rd(os.path.join(d, f'perf_frames_{tag}.csv')); C = rd(os.path.join(d, 'conditions.txt'))
    except FileNotFoundError as e: return dict(tag=tag, valid=False, why=f'file missing: {e}')
    S = {}; N = T = None
    for l in L:
        if l.startswith('#frames,'): N = int(l.split(',')[1])
        elif l.startswith('#total_ms,'): T = float(l.split(',')[1]) / 1000.0
        elif l and not l.startswith(('#', 'name,')):
            f = l.split(',')
            if len(f) >= 9 and f[0] in NEED and f[0] not in S: S[f[0]] = [float(x) for x in f[-6:]]   # calls, incl, self (main) ; calls, incl, self (other)
    rows = sum(1 for l in FR if l and l[0].isdigit())
    if not N or not T: return dict(tag=tag, valid=False, why='no finished capture')
    if rows != N: return dict(tag=tag, valid=False, why=f'frames file has {rows} rows for {N} frames')
    if not any(l.strip() == 'VERDICT: clean' for l in C): return dict(tag=tag, valid=False, why='conditions not clean')
    miss = [s for s in NEED if s not in S]
    if miss: return dict(tag=tag, valid=False, why='sites missing: ' + '; '.join(miss))
    U = S['Process seqFrame'][0]
    if U <= 0: return dict(tag=tag, valid=False, why='no updates')
    rate = U / T
    if abs(rate - 30) > 0.9: return dict(tag=tag, valid=False, why=f'rate {rate:.2f} not within 3 % of 30')
    both = lambda s, i: S[s][i] + S[s][i + 3]
    return dict(tag=tag, valid=True, U=int(U), N=N, rate=rate, fpt=N / U, sim=(S['Process seqFrame'][1] + both('Sheduler Deferred', 1) + both('seqParallel', 1) + both('seqFrameMT', 1)) / U,
                script=both('CScriptBinder::shedule_Update', 2) / U, brain=both('stalker/schedule_update/think/brain', 2) / U)
def main():
    out = []; A = {}; nov = []
    for a in ARMS:
        r = [run(f'{a}_{i}') for i in (1, 2)]
        for x in r:
            if not x['valid']: out.append(f'INVALID {x["tag"]}: {x["why"]}'); nov.append(x['tag'])
        if all(x['valid'] for x in r):
            m = {k: statistics.fmean(x[k] for x in r) for k in ('sim', 'script', 'brain', 'fpt', 'rate')}; m['spread'] = abs(r[0]['sim'] - r[1]['sim']) / m['sim']; A[a] = m
            out.append(f'ARM {a}: frames per tick {m["fpt"]:.2f}; sim {m["sim"]:.3f} ms per tick ({r[0]["sim"]:.3f} / {r[1]["sim"]:.3f}, spread {100 * m["spread"]:.1f} %); script self {m["script"]:.3f}; brain self {m["brain"]:.3f}; updates {r[0]["U"]}/{r[1]["U"]}')
            if m['spread'] > 0.12: nov.append(f'{a} spread {100 * m["spread"]:.1f} %')
    if nov:
        out.append('S2-C E1: NO VERDICT (' + '; '.join(nov) + ')'); code = 2
    else:
        c = A['s2c_c30']; E = {k: A[f's2c_g30{k}']['sim'] / c['sim'] - 1 for k in ('x31', 'x60', 'x120', 'u')}
        for k in ('script', 'brain'): out.append(f'reported, {k} self against capped: ' + ', '.join(f'{x} {100 * (A[f"s2c_g30{x}"][k] / c[k] - 1):+.1f} %' for x in ('x31', 'x60', 'x120', 'u')))
        out.append('E (sim per tick against capped 30): ' + ', '.join(f'{k} {100 * v:+.1f} %' for k, v in E.items()))
        if E['u'] < 0.15: v = 'EFFECT NOT REPRODUCED'
        else:
            F = {k: E[k] / E['u'] for k in ('x31', 'x60', 'x120')}; out.append('F (share of the uncapped extra): ' + ', '.join(f'{k} {v:.2f}' for k, v in F.items()))
            if E['x31'] > 0.10: v = 'GATE PATH OR BEAT'
            elif F['x60'] >= 0.70: v = 'H2 STEP'
            elif F['x60'] <= 0.35 and F['x60'] <= F['x120'] <= 1.10: v = 'H1 RISE'
            else: v = 'NOT SEPARATED'
        out.append('S2-C E1: ' + v); code = 0
    txt = '\n'.join(out) + '\n'; print(txt, end='')
    if OUTF: open(OUTF, 'w').write(txt)
    return code
if __name__ == '__main__': sys.exit(main())
