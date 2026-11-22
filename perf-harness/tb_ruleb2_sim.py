import sys, itertools, random, math
sys.path.insert(0, 'tools'); import tier_b
R = lambda t: tier_b.Run('out/tb_' + t)
REF = ['r01','r02_rr','r03_rr','r04_rr','r05','r06','r07','r08','r09','r10','r11','r12','r13','r14','r15']
OTHER_STOCK = ['h01_rr','h02','h03','h04','h05','s01_rr','s02','s03','s04','s05','ri01','ri02','ri03','ri04','ri05','r02','r03','r04','h01']
STOCK = REF + OTHER_STOCK
STATS = ['P','D','C']
cache = {}
def d(s, a, b):
    k = (s, min(a,b), max(a,b))
    if k not in cache: cache[k] = tier_b.FUNCS[s](R(a), R(b), 3)
    return cache[k]
def med(x): x = sorted(x); n = len(x); return x[n//2] if n % 2 else (x[n//2-1]+x[n//2])/2
def thrA(s, R1, R2):
    return tier_b.percentile([d(s,a,b) for a in R1 for b in R2], 97.5)
def binom_tail(n, k, p): return sum(math.comb(n,i)*p**i*(1-p)**(n-i) for i in range(k, n+1))
def kcrit(p0, n=5, alpha=0.05):
    for k in range(0, n+2):
        if binom_tail(n, k, p0) <= alpha: return k
    return n+1
# Rule B': per candidate run c, deviates if median_r1 d(c, r1) > T_A. p0 = deviation rate of stock runs outside R1/R2.
R1, R2 = REF[:5], REF[5:10]
pool = [t for t in STOCK if t not in R1 + R2]
print('stock pool outside R1/R2:', len(pool), 'runs; all stock:', len(STOCK))
P0 = {}
for s in STATS:
    T = thrA(s, R1, R2)
    dev = [t for t in pool if med([d(s,t,r) for r in R1]) > T]
    P0[s] = (len(dev) + 0.5) / (len(pool) + 1)       # add-half so p0 is never 0
    print('%s T_A=%g deviating stock runs %d/%d %s  p0=%.3f  k_crit=%d' % (s, T, len(dev), len(pool), dev, P0[s], kcrit(P0[s])))
# null false-fail of B' alone and of A AND B' (full rule) by resampling: random R1, R2, C (5 each) from ALL stock runs
random.seed(1); N = 2000; fB = {s: 0 for s in STATS}; fA = {s: 0 for s in STATS}; fAny = 0
for _ in range(N):
    sp = random.sample(STOCK, 15); r1, r2, c = sp[:5], sp[5:10], sp[10:]
    rest = [t for t in STOCK if t not in r1 + r2]
    anyf = False
    for s in STATS:
        T = thrA(s, r1, r2)
        p0 = (sum(med([d(s,t,r) for r in r1]) > T for t in rest if t not in c) + 0.5) / (len(rest) - 5 + 1)
        k = sum(med([d(s,t,r) for r in r1]) > T for t in c)
        b = k >= kcrit(p0); a = med([d(s,x,r) for x in c for r in r1]) > T
        fB[s] += b; fA[s] += a or b; anyf |= (a or b)
    fAny += anyf
print('null (2000 random splits of %d stock runs): B-prime alone' % len(STOCK), {s: round(fB[s]/N,3) for s in STATS}, ' A or B-prime', {s: round(fA[s]/N,3) for s in STATS}, ' any of P,D,C fails', round(fAny/N,3))
# power on existing doses (R1/R2 as registered)
for g in ['d10','d100','d1000','lr1','lr10','lr100']:
    C = ['%s_0%d' % (g, i) for i in range(1,6)]
    C = [c if c not in ('d10_01','d10_02','d100_01','d1000_01') else c + '_rr' for c in C]
    C = [c if not g.startswith('lr') else c for c in C]
    out = []
    for s in STATS:
        T = thrA(s, R1, R2); k = sum(med([d(s,t,r) for r in R1]) > T for t in C)
        out.append('%s k=%d/5 %s' % (s, k, 'FAIL' if k >= kcrit(P0[s]) else 'pass'))
    print(g, ' | '.join(out))
