#!/usr/bin/env python3
"""S2 part A2 (PREREG-S2-simtick section 9): off = stock, checkpoints at A-Life PASS COUNTS. Score ONCE.
  s2_partA2_score.py <out dir> <stock tag,stock tag,stock tag> <off tag,off tag,off tag> [report file]
R1 the stock runs agree: counters_diff C = 0 at checkpoints 1, 2, 3 for every stock run against the first.
R2 every switch-off run agrees with the first stock run: C = 0 at 1, 2, 3.
R3 NO HIDDEN EXTRA PASS, by the frame at which checkpoint 1's pass count is reached (engine log, det_rng_state cp1
   frame=): |mean of the off runs - mean of the stock runs| <= 8 frames (one pass = 17 frames). Checkpoints 2 and 3 are
   reported, not gated (the spread between identical runs grows with time).
R4 NO HIDDEN EXTRA PASS, by the scheduler trace up to checkpoint 1: in every run the first A-Life call is on the same
   frame, every gap between A-Life calls is 16, 17 or 18 frames, and exactly one gap is 16.
Exit 0 = all PASS, 1 = a FAIL, 2 = data missing (NO VERDICT)."""
import sys, os, re, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
rd = lambda p: open(p, 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n')
def cframes(d):
    r = {}
    for l in rd(os.path.join(d, 'xray.log')):
        m = re.search(r'det_rng_state cp(\d+):.* frame=(\d+) time_ms=(\d+)', l)
        if m: r[int(m.group(1))] = int(m.group(2))
    return r
def alife_frames(d, tag):
    return [int(l.split(' ', 1)[0]) for l in rd(os.path.join(d, f'det_trace_{tag}.txt.sched')) if ' alife_simulator ' in l]
def counters(a, b):
    p = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'counters_diff.py'), a, b, '--k', '1,2,3'], capture_output=True, text=True)
    c = {int(k): int(v) for k, v in re.findall(r'COUNTERS k=(\d+) C=(\d+)', p.stdout)}
    return c if p.returncode == 0 and sorted(c) == [1, 2, 3] else None
def r3(stock, off):   # lists of {cp: frame}
    mean = lambda L, k: sum(x[k] for x in L) / len(L)
    d = {k: mean(off, k) - mean(stock, k) for k in (1, 2, 3)}
    return abs(d[1]) <= 8, d
def r4(frames):       # list of A-Life call frame lists, one per run
    bad = []
    for i, F in enumerate(frames):
        g = [b - a for a, b in zip(F, F[1:])]
        if len(F) < 50 or F[0] != frames[0][0] or any(x not in (16, 17, 18) for x in g) or g.count(16) != 1:
            bad.append((i, len(F), F[0] if F else None, sorted(set(g)), g.count(16)))
    return not bad, bad
def main():
    O, ST, OF = sys.argv[1], sys.argv[2].split(','), sys.argv[3].split(',')
    D = lambda t: os.path.join(O, 'det_' + t); out = []; ok = True
    try:
        c1 = {t: counters(D(ST[0]), D(t)) for t in ST[1:]}; c2 = {t: counters(D(ST[0]), D(t)) for t in OF}
        fs = [cframes(D(t)) for t in ST]; fo = [cframes(D(t)) for t in OF]; tr = [alife_frames(D(t), t) for t in ST + OF]
    except OSError as e:
        print(f'NO VERDICT: {e}'); return 2
    if any(v is None for v in list(c1.values()) + list(c2.values())) or any(sorted(x) != [1, 2, 3] for x in fs + fo):
        print(f'NO VERDICT: counters or checkpoint frames missing ({c1} {c2} {fs} {fo})'); return 2
    p = all(v == 0 for c in c1.values() for v in c.values()); ok &= p; out.append(f"R1 stock runs agree (C = 0): {'PASS' if p else 'FAIL'} ({c1})")
    p = all(v == 0 for c in c2.values() for v in c.values()); ok &= p; out.append(f"R2 switch-off runs agree with stock (C = 0): {'PASS' if p else 'FAIL'} ({c2})")
    p, d = r3(fs, fo); ok &= p; out.append(f"R3 frame at checkpoint 1, off mean minus stock mean within 8: {'PASS' if p else 'FAIL'} (cp1 {d[1]:+.1f}, reported cp2 {d[2]:+.1f}, cp3 {d[3]:+.1f}; stock {fs}; off {fo})")
    p, bad = r4(tr); ok &= p; out.append(f"R4 A-Life call spacing to checkpoint 1 (first frame equal, gaps 16/17/18, one 16): {'PASS' if p else 'FAIL'} (calls {[len(x) for x in tr]}, last frames {[x[-1] for x in tr]}; bad {bad})")
    out.append('S2 PART A2: ' + ('PASS' if ok else 'FAIL')); txt = '\n'.join(out); print(txt)
    if len(sys.argv) > 4: open(sys.argv[4], 'w').write(txt + '\n')
    return 0 if ok else 1
if __name__ == '__main__': sys.exit(main())
