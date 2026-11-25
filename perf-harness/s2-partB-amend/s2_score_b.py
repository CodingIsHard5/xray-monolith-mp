#!/usr/bin/env python3
"""S2 experiment scorer (fixed simulation tick 30/50/60 Hz against stock). Run ONCE on the finished series.
  s2_score.py <out root> <predictions json> [out file]
Reads, per arm folder <out root>/<tag>/: perf_sites_<tag>.csv, perf_frames_<tag>.csv, xray.log, conditions.txt.
Arm kinds by tag: s2_off* stock real time; s2_tNN* tick gate; s2_cNN* stock capped; s2_fNN* every frame one tick
(s2_f33 = stock fixed 33 ms). A trailing L = 4-core run. _1/_2 are replicates, _rr a re-run (replaces its original).
Per arm (U = updates = calls of "Process seqFrame", N = frames, T = capture seconds):
  fps = N/T; rate = U/T; main_tick = "Process seqFrame" main ms / U; gt_tick = ("Sheduler Deferred" + "seqParallel" +
  "seqFrameMT") ms / U; sim_tick = main_tick + gt_tick; sim_s = sim_tick * rate (ms of simulation CPU per second);
  script_tick = "CScriptBinder::shedule_Update" ms (all threads) / U; actor_tick = "CActor shedule_Update" main ms / U;
  phys_steps = "CPHWorld::Step" calls / U; gc_s = "seqLuaGC" ms / T; render = "Process seqRender" main ms / N;
  frame p50/p95/p99/max ms; over25 = frames over 25 ms per 1,000;
  delay = COMPUTED input-to-picture estimate, not measured: half the poll interval (1000/rate/2) + the time from the poll
  to the end of the render. Stock arms: one whole mean frame. Gated arms: "Render: Frame Move" ms per tick + the median
  frame (a render-only frame).
  gated arms also: the [SIMTICK] lines printed inside the capture, less the first of them: share of frames with 0, 1, 2, 3+
  ticks, dropped ticks, mean and largest age of the world state at render start.
VALID arm = capture finished (sites and frames files with N frames), conditions "VERDICT: clean", every site above
present with U > 0, and for tNN and cNN arms the measured rate within 3 % of NN. An invalid arm gives no value.
Replicates are averaged; their two values are printed too.
Predictions: json {"<arm>.<metric>": [low, high], ...} with <arm> without the replicate suffix. Each is HIT, MISS or
NO VALUE. Nothing here passes or fails the engine: the experiment measures, the predictions are scored."""
import sys, os, re, json, statistics
ROOT, PRED = sys.argv[1:3]; OUTF = sys.argv[3] if len(sys.argv) > 3 else os.path.join(ROOT, 's2_score.txt')
rd = lambda p: open(p, 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n') if os.path.exists(p) else []
SITES = ['Process seqFrame', 'Render: Frame Move', 'Sheduler Deferred', 'seqParallel', 'seqFrameMT', 'CScriptBinder::shedule_Update', 'CActor shedule_Update', 'CPHWorld::Step', 'seqLuaGC', 'Process seqRender']
def arm(tag):
    d = os.path.join(ROOT, tag); S = {}; why = []
    L = rd(os.path.join(d, f'perf_sites_{tag}.csv')); nfr = tot = None
    for l in L:
        if l.startswith('#frames,'): nfr = int(l.split(',')[1])
        elif l.startswith('#total_ms,'): tot = float(l.split(',')[1])
        elif l and not l.startswith(('#', 'name,')):
            f = l.split(',')
            if len(f) >= 9 and f[0] in SITES and f[0] not in S: S[f[0]] = dict(cm=float(f[-6]), im=float(f[-5]), co=float(f[-3]), io=float(f[-2]))
    F = [float(l.split(',')[1]) for l in rd(os.path.join(d, f'perf_frames_{tag}.csv')) if l and l[0].isdigit()]
    if not nfr or not tot or not F: why.append('no finished capture')
    elif len(F) != nfr: why.append(f'frames file has {len(F)} rows for {nfr} frames')
    if not any(l.strip() == 'VERDICT: clean' for l in rd(os.path.join(d, 'conditions.txt'))): why.append('conditions not clean')
    miss = [s for s in SITES if s not in S]
    if miss: why.append('sites missing: ' + '; '.join(miss))
    if why: return dict(tag=tag, valid=False, why='; '.join(why))
    N = nfr; T = tot / 1000.0; U = S['Process seqFrame']['cm']
    if U <= 0: return dict(tag=tag, valid=False, why='no updates')
    both = lambda s: S[s]['im'] + S[s]['io']
    m = dict(tag=tag, valid=True, N=N, U=int(U), fps=N / T, rate=U / T)
    m['main_tick'] = S['Process seqFrame']['im'] / U
    m['gt_tick'] = (both('Sheduler Deferred') + both('seqParallel') + both('seqFrameMT')) / U
    m['sim_tick'] = m['main_tick'] + m['gt_tick']; m['sim_s'] = m['sim_tick'] * m['rate']
    m['script_tick'] = both('CScriptBinder::shedule_Update') / U; m['actor_tick'] = S['CActor shedule_Update']['im'] / U
    m['phys_steps'] = (S['CPHWorld::Step']['cm'] + S['CPHWorld::Step']['co']) / U
    m['gc_s'] = both('seqLuaGC') / T; m['render'] = S['Process seqRender']['im'] / N
    F.sort(); q = lambda p: F[min(len(F) - 1, int(len(F) * p))]
    m.update(p50=q(.5), p95=q(.95), p99=q(.99), fmax=F[-1], over25=1000.0 * sum(1 for x in F if x > 25) / len(F))
    k = re.match(r's2_([a-z]+?)(\d*)L?(?:_\d)?(?:_rr)?$', tag); kind, hz = (k.group(1), int(k.group(2) or 0)) if k else ('?', 0)
    gated = kind == 't'
    m['delay'] = 500.0 / m['rate'] + ((S['Render: Frame Move']['im'] / U + m['p50']) if gated else statistics.fmean(F))
    if kind in ('t', 'c') and hz and abs(m['rate'] - hz) > 0.03 * hz: return dict(tag=tag, valid=False, why=f'rate {m["rate"]:.2f} not within 3 % of {hz}')
    if gated:
        X = [re.search(r'\[SIMTICK\] hz (\d+) frames (\d+) ticks (\d+) n0 (\d+) n1 (\d+) n2 (\d+) n3p (\d+) dropped (\d+) age_avg_ms ([\d.]+) age_max_ms (\d+)', l) for l in rd(os.path.join(d, 'xray.log'))]
        # Amended (prereg section 11): only the lines printed INSIDE the capture, less the first of them. Each line
        # covers the 10 s before it, so the first one inside reaches back into the warm-up. Before: every line but the
        # very first, which took one warm-up window and the quit frames in.
        LG = rd(os.path.join(d, 'xray.log'))
        a = next((i for i, l in enumerate(LG) if '[xrprof] capture' in l and 'started' in l), None); b = next((i for i, l in enumerate(LG) if '[xrprof] capture' in l and 'finished' in l), None)
        if a is None or b is None or b < a: return dict(tag=tag, valid=False, why='capture start/finish lines missing in the engine log')
        X = [x for x in X[a:b] if x][1:]
        if not X: return dict(tag=tag, valid=False, why='no whole [SIMTICK] window inside the capture')
        fr = sum(int(x.group(2)) for x in X)
        for i, nm in enumerate(('n0', 'n1', 'n2', 'n3p')): m[nm] = sum(int(x.group(4 + i)) for x in X) / fr
        m['dropped'] = sum(int(x.group(8)) for x in X); m['age_avg'] = sum(float(x.group(9)) * int(x.group(2)) for x in X) / fr; m['age_max'] = max(int(x.group(10)) for x in X)
    return m
tags = sorted(t for t in os.listdir(ROOT) if t.startswith('s2_') and os.path.isdir(os.path.join(ROOT, t)) and t != 's2_state')
rr = {t[:-3] for t in tags if t.endswith('_rr')}; tags = [t for t in tags if t not in rr]     # a re-run replaces its original
# Amended (prereg section 11, Overseer's condition 1): the first s2_f30 failed the per-run check it was registered with.
# It stays excluded whatever else happens; the run under the amended check is s2_f30_rr.
EXCLUDED = {'s2_f30': 'failed its registered per-run check (section 11)'}; XL = [f'EXCLUDED {t}: {EXCLUDED[t]}' for t in sorted(EXCLUDED) if os.path.isdir(os.path.join(ROOT, t))]
tags = [t for t in tags if t not in EXCLUDED]
A = {t: arm(t) for t in tags}
base = lambda t: re.sub(r'(_\d)?(_rr)?$', '', t)
G = {}
for t in tags: G.setdefault(base(t), []).append(A[t])
MET = ['fps', 'rate', 'main_tick', 'gt_tick', 'sim_tick', 'sim_s', 'script_tick', 'actor_tick', 'phys_steps', 'gc_s', 'render', 'p50', 'p95', 'p99', 'fmax', 'over25', 'delay', 'n0', 'n1', 'n2', 'n3p', 'dropped', 'age_avg', 'age_max']
V = {}; out = list(XL)
for b, rs in G.items():
    ok = [r for r in rs if r['valid']]
    for r in rs:
        if not r['valid']: out.append(f'INVALID {r["tag"]}: {r["why"]}')
    if not ok: continue
    out.append(f'ARM {b} ({len(ok)} valid run{"s" if len(ok) > 1 else ""}: {", ".join(r["tag"] for r in ok)}; frames {"/".join(str(r["N"]) for r in ok)}, updates {"/".join(str(r["U"]) for r in ok)})')
    for k in MET:
        vs = [r[k] for r in ok if k in r]
        if not vs: continue
        V[f'{b}.{k}'] = statistics.fmean(vs)
        out.append(f'  {k:12s} {V[f"{b}.{k}"]:10.3f}' + (f'   ({" / ".join(f"{v:.3f}" for v in vs)})' if len(vs) > 1 else ''))
for a, b in (('s2_t60', 's2_t30'), ('s2_t50', 's2_t30'), ('s2_off', 's2_t30'), ('s2_off', 's2_t60'), ('s2_f60', 's2_f30'), ('s2_t60L', 's2_t30L')):
    for k in ('sim_s', 'sim_tick', 'script_tick', 'p99'):
        if f'{a}.{k}' in V and f'{b}.{k}' in V and V[f'{b}.{k}'] > 0:
            V[f'{a}/{b}.{k}'] = V[f'{a}.{k}'] / V[f'{b}.{k}']; out.append(f'RATIO {a}/{b} {k}: {V[f"{a}/{b}.{k}"]:.3f}')
P = json.load(open(PRED)); hit = miss = nov = 0
for key in sorted(P):
    lo, hi = P[key]
    if key not in V: out.append(f'PREDICTION {key} [{lo}, {hi}]: NO VALUE'); nov += 1
    else:
        h = lo <= V[key] <= hi; hit += h; miss += not h
        out.append(f'PREDICTION {key} [{lo}, {hi}]: {V[key]:.3f} {"HIT" if h else "MISS"}')
out.append(f'S2 SCORE: {hit} hit, {miss} miss, {nov} no value, of {len(P)} predictions; arms valid {sum(1 for r in A.values() if r["valid"])} of {len(A)}')
txt = '\n'.join(out) + '\n'; print(txt, end=''); open(OUTF, 'w').write(txt)
