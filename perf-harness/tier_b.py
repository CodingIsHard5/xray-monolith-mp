#!/usr/bin/env python3
"""Tier (b) equivalence statistics (PREREG-tierB): six pair statistics between A-Life state dumps.

Reads a run directory (out/tb_<tag>/ from tools/tb_run.sh, or an old out/det_<tag>/) holding, per dump k:
  alife_dump_<tag>_<k>.txt   overlay state dump (both modes; k=0 = start point, tb runs always have it)
  tb_extra_<tag>_<k>.txt     tb side file (sq/cr/sm/info lines: squad level + game vertex, passes, skip counters)
  det_counters_<tag>_<k>.txt old det runs with -det_counters only ("info passes")

Statistics for a pair of runs (x, y) at dump k:
  P  sum over smart terrains of |assigned npc count x - y| (smart_npcs field of the main dump; missing smart = 0)
  L  squad ids present in both whose level differs                      (needs tb_extra: level per squad)
  V  squad ids present in both, same level, different game vertex        (needs tb_extra)
  M  sum over object sections of |count x - count y| (all objects in the main dump)
  D  |deaths_x - deaths_y|; death = creature id (alive= field, actor id 0 excluded) alive at dump 0 and absent or
     alive=false at dump k                                               (needs dump 0)
  U  |passes_x - passes_y| / mean(passes_x, passes_y) at dump k (0 if both 0)
     (needs tb_extra "info passes" or det_counters "info passes")
A statistic that needs a field a run lacks is reported as "n/a (field missing: ...)"; nothing is guessed.

Commands
  pair A B [--k 3]
  score --r1 d.. --r2 d.. --c d.. [--k 3] [--band 90] [--stats P,L,V,M,D,U]
  null --ref d.. --n 5 [--k 3] [--band 90] [--max-splits 2000] [--seed 1] [--stats ...]
  lost-rate <run dir>

Decision rule per statistic (score; null applies the same function):
  noise = the |R1|x|R2| values d(r1, r2); cand = the |C|x|R1| values d(c, r1).
  threshold = band-th percentile of noise, linear interpolation between order statistics (numpy's default
  "linear" method: position (band/100)*(m-1) in the sorted list of m values).
  Rule A: median(cand) <= threshold.
  Rule B, permutation test: pool = C + R1 (labels exchangeable under H0 "candidate = reference"). For a labelling
  that calls n of the pooled runs "C" (G1) and the rest "R1" (G0), the test statistic is
      T = median{ d(x, r2) : x in G1, r2 in R2 } - median{ d(x, r2) : x in G0, r2 in R2 }
  i.e. the difference of median distances to the R2 runs. Two-sided p = fraction of labellings with
  |T| >= |T_observed| - 1e-12, the observed labelling included. Exact enumeration of all C(|pool|, |C|) labellings
  when that number is <= 20000 (n=5: 252), else 20000 seeded random labellings plus the observed one.
  PASS for a statistic = Rule A AND p > 0.05. Overall PASS only if every selected statistic passes; a statistic that
  is n/a makes the overall verdict NOT SCORABLE (never PASS).

null: from K >= 3n reference runs, disjoint ordered splits (pseudo R1, R2, C, each of size n). All splits are
  enumerated if their number is <= --max-splits, else --max-splits distinct splits are drawn with random.Random(seed).
  Each split is scored with the full rule; reported: fail fraction overall and per statistic at --band, and the
  overall false-fail rate for bands 80, 85, 90, 95, 97.5, 99, 100.
"""
import argparse
import glob
import itertools
import math
import os
import random
import re
import sys

STATS = ["P", "L", "V", "M", "D", "U"]
PERM_EXACT_MAX = 20000
BANDS = [80, 85, 90, 95, 97.5, 99, 100]


class NA(Exception):
    pass


# ---------------------------------------------------------------- reading

class Run:
    def __init__(self, d):
        if not os.path.isdir(d):
            raise SystemExit("ERROR: not a directory: %s" % d)
        self.dir = d
        tags = set()
        for p in glob.glob(os.path.join(d, "alife_dump_*_*.txt")):
            m = re.match(r"alife_dump_(.+)_(\d+)\.txt$", os.path.basename(p))
            if m:
                tags.add(m.group(1))
        if len(tags) != 1:
            raise SystemExit("ERROR: %s: expected dumps of exactly one tag, found %s" % (d, sorted(tags) or "none"))
        self.tag = tags.pop()
        self._dump, self._extra = {}, {}

    def path(self, kind, k):
        return os.path.join(self.dir, "%s_%s_%s.txt" % (kind, self.tag, k))

    def dump(self, k):
        if k not in self._dump:
            p = self.path("alife_dump", k)
            if not os.path.isfile(p):
                self._dump[k] = None
            else:
                self._dump[k] = parse_dump(p)
        return self._dump[k]

    def extra(self, k):
        if k not in self._extra:
            p = self.path("tb_extra", k)
            self._extra[k] = parse_extra(p) if os.path.isfile(p) else None
        return self._extra[k]

    def passes(self, k):
        e = self.extra(k)
        if e and "passes" in e["info"]:
            return int(e["info"]["passes"])
        p = self.path("det_counters", k)
        if os.path.isfile(p):
            with open(p, encoding="utf-8", errors="replace") as f:
                for ln in f:
                    t = ln.split()
                    if len(t) >= 3 and t[0] == "info" and t[1] == "passes":
                        return int(t[2])
        return None


def parse_dump(p):
    d = {"section": {}, "alive": {}, "smart_npcs": {}, "complete": False}
    with open(p, encoding="utf-8", errors="replace") as f:
        for ln in f:
            ln = ln.rstrip("\n")
            if ln.startswith("#"):
                if ln.startswith("#actor"):
                    d["complete"] = True
                continue
            t = ln.split("\t")
            if len(t) < 8 or not t[0].isdigit():
                continue
            oid = int(t[0])
            d["section"][oid] = t[1]
            kv = dict(x.split("=", 1) for x in t[8:] if "=" in x)
            if "alive" in kv and oid != 0:
                d["alive"][oid] = kv["alive"] == "true"
            if "smart_npcs" in kv:
                d["smart_npcs"][oid] = int(kv["smart_npcs"])
    if not d["complete"]:
        print("WARNING: %s has no #actor line (truncated dump?)" % p, file=sys.stderr)
    return d


def parse_extra(p):
    e = {"info": {}, "sq": {}, "cr": {}, "sm": {}}
    with open(p, encoding="utf-8", errors="replace") as f:
        for ln in f:
            t = ln.rstrip("\n").split("\t") if "\t" in ln else ln.split()
            if not t:
                continue
            if t[0] == "info" and len(t) >= 3:
                e["info"][t[1]] = t[2]
            elif t[0] == "sq" and len(t) >= 7:
                e["sq"][int(t[1])] = {"section": t[2], "level": t[3], "gv": t[4], "smart": t[5], "npcs": t[6]}
            elif t[0] == "cr" and len(t) >= 9:
                e["cr"][int(t[1])] = {"section": t[2], "kind": t[3], "alive": t[4] == "1", "level": t[5], "gv": t[6],
                                      "squad": t[7], "smart": t[8]}
            elif t[0] == "sm" and len(t) >= 5:
                e["sm"][int(t[1])] = {"name": t[2], "level": t[3], "npcs": int(t[4])}
    return e


# ---------------------------------------------------------------- statistics

def need_dump(r, k):
    d = r.dump(k)
    if d is None:
        raise NA("alife_dump k=%s in %s" % (k, r.dir))
    return d


def need_extra(r, k):
    e = r.extra(k)
    if e is None:
        raise NA("tb_extra k=%s (squad level) in %s" % (k, r.dir))
    return e


def stat_P(a, b, k):
    da, db = need_dump(a, k), need_dump(b, k)
    if not da["smart_npcs"] or not db["smart_npcs"]:
        raise NA("smart_npcs")
    ids = set(da["smart_npcs"]) | set(db["smart_npcs"])
    return sum(abs(da["smart_npcs"].get(i, 0) - db["smart_npcs"].get(i, 0)) for i in ids)


def _common_squads(a, b, k):
    ea, eb = need_extra(a, k), need_extra(b, k)
    common = set(ea["sq"]) & set(eb["sq"])
    return ea["sq"], eb["sq"], common


def stat_L(a, b, k):
    sa, sb, common = _common_squads(a, b, k)
    return sum(1 for i in common if sa[i]["level"] != sb[i]["level"])


def stat_V(a, b, k):
    sa, sb, common = _common_squads(a, b, k)
    return sum(1 for i in common if sa[i]["level"] == sb[i]["level"] and sa[i]["gv"] != sb[i]["gv"])


def stat_M(a, b, k):
    da, db = need_dump(a, k), need_dump(b, k)
    ca, cb = {}, {}
    for c, d in ((ca, da), (cb, db)):
        for sec in d["section"].values():
            c[sec] = c.get(sec, 0) + 1
    return sum(abs(ca.get(s, 0) - cb.get(s, 0)) for s in set(ca) | set(cb))


def deaths(r, k):
    d0 = r.dump(0)
    if d0 is None:
        raise NA("alife_dump k=0 (start point) in %s" % r.dir)
    dk = need_dump(r, k)
    if not d0["alive"]:
        raise NA("alive")
    return sum(1 for i, al in d0["alive"].items() if al and not dk["alive"].get(i, False))


def stat_D(a, b, k):
    return abs(deaths(a, k) - deaths(b, k))


def stat_U(a, b, k):
    pa, pb = a.passes(k), b.passes(k)
    if pa is None or pb is None:
        raise NA("passes (tb_extra or det_counters) k=%s in %s" % (k, a.dir if pa is None else b.dir))
    m = (pa + pb) / 2.0
    return 0.0 if m == 0 else abs(pa - pb) / m


FUNCS = {"P": stat_P, "L": stat_L, "V": stat_V, "M": stat_M, "D": stat_D, "U": stat_U}


def stat(name, a, b, k):
    """value, or None plus reason"""
    try:
        return FUNCS[name](a, b, k), None
    except NA as e:
        return None, str(e)


def fmt(v):
    if v is None:
        return "n/a"
    if isinstance(v, float):
        return "%.4f" % v
    return str(v)


# ---------------------------------------------------------------- rule

def median(xs):
    s = sorted(xs)
    n = len(s)
    if n == 0:
        raise ValueError("median of nothing")
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2.0


def percentile(xs, q):
    s = sorted(xs)
    pos = (q / 100.0) * (len(s) - 1)
    lo = int(math.floor(pos))
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (pos - lo)


def perm_p(dist, C, R1, R2, seed=1):
    """dist(x, y) -> value. Two-sided permutation p, see module docstring."""
    pool = list(C) + list(R1)
    nc = len(C)
    to_r2 = {x: [dist(x, r) for r in R2] for x in pool}

    def T(g1):
        g1s = set(g1)
        a = [v for x in pool if x in g1s for v in to_r2[x]]
        b = [v for x in pool if x not in g1s for v in to_r2[x]]
        return median(a) - median(b)

    t_obs = abs(T(pool[:nc]))
    total = math.comb(len(pool), nc)
    if total <= PERM_EXACT_MAX:
        labellings = itertools.combinations(range(len(pool)), nc)
        n = total
    else:
        rng = random.Random(seed)
        labellings = itertools.chain([tuple(range(nc))], (tuple(rng.sample(range(len(pool)), nc)) for _ in range(PERM_EXACT_MAX)))
        n = PERM_EXACT_MAX + 1
    hits = 0
    for lab in labellings:
        if abs(T([pool[i] for i in lab])) >= t_obs - 1e-12:
            hits += 1
    return hits / float(n)


def score_stat(dist, R1, R2, C, bands, seed=1):
    """dist: callable(x, y) -> number. Returns dict with noise, cand, med, thr per band, p."""
    noise = [dist(x, y) for x in R1 for y in R2]
    cand = [dist(c, r) for c in C for r in R1]
    res = {"noise": noise, "cand": cand, "med": median(cand), "thr": {b: percentile(noise, b) for b in bands}}
    res["p"] = perm_p(dist, C, R1, R2, seed)
    return res


def verdict(res, band):
    a = res["med"] <= res["thr"][band] + 1e-12
    b = res["p"] > 0.05
    return a and b, a, b


# ---------------------------------------------------------------- commands

def cmd_pair(a):
    A, B = Run(a.A), Run(a.B)
    print("pair %s (%s) vs %s (%s) at k=%s" % (A.dir, A.tag, B.dir, B.tag, a.k))
    for s in STATS:
        v, why = stat(s, A, B, a.k)
        print("  %s  %s" % (s, fmt(v) if why is None else "n/a (field missing: %s)" % why))


def matrix(runs, names, k):
    """pairwise values per statistic over runs (symmetric); value None = n/a, reason kept."""
    M, why = {}, {}
    for s in names:
        M[s] = {}
        for i, j in itertools.combinations(range(len(runs)), 2):
            v, w = stat(s, runs[i], runs[j], k)
            if w is not None:
                why[s] = w
                break
            M[s][(i, j)] = M[s][(j, i)] = v
    return M, why


def parse_stats(txt):
    names = [x.strip().upper() for x in txt.split(",") if x.strip()]
    bad = [x for x in names if x not in STATS]
    if bad:
        raise SystemExit("ERROR: unknown statistic(s) %s" % bad)
    return names


def distinct(dirs):
    real = [os.path.realpath(d) for d in dirs]
    dup = sorted(set(d for d in real if real.count(d) > 1))
    if dup:
        raise SystemExit("ERROR: run directory given more than once: %s" % dup)


def cmd_score(a):
    names = parse_stats(a.stats)
    distinct(a.r1 + a.r2 + a.c)
    R1, R2, C = [Run(d) for d in a.r1], [Run(d) for d in a.r2], [Run(d) for d in a.c]
    runs = R1 + R2 + C
    i1, i2, ic = list(range(len(R1))), list(range(len(R1), len(R1) + len(R2))), list(range(len(R1) + len(R2), len(runs)))
    M, why = matrix(runs, names, a.k)
    print("tier (b) score at k=%s band=%s  R1=%d R2=%d C=%d" % (a.k, a.band, len(R1), len(R2), len(C)))
    overall, scorable = True, True
    for s in names:
        if s in why:
            print("%s  n/a (field missing: %s)" % (s, why[s]))
            scorable = False
            continue
        dist = lambda x, y, s=s: M[s][(x, y)]
        res = score_stat(dist, i1, i2, ic, [a.band], a.seed)
        print("%s  R1xR2 noise: %s" % (s, " ".join(fmt(v) for v in res["noise"])))
        print("%s  C xR1 cand : %s" % (s, " ".join(fmt(v) for v in res["cand"])))
        ok, ra, rb = verdict(res, a.band)
        overall = overall and ok
        print("%s  median(CxR1)=%s  P%s(R1xR2)=%s  rule_A=%s  perm_p=%.4f rule_B=%s  -> %s" % (
            s, fmt(res["med"]), a.band, fmt(res["thr"][a.band]), "ok" if ra else "FAIL", res["p"], "ok" if rb else "FAIL",
            "PASS" if ok else "FAIL"))
    if not scorable:
        print("OVERALL: NOT SCORABLE (a selected statistic is n/a)")
    else:
        print("OVERALL: %s" % ("PASS" if overall else "FAIL"))


def cmd_null(a):
    names = parse_stats(a.stats)
    distinct(a.ref)
    refs = [Run(d) for d in a.ref]
    n, K = a.n, len(refs)
    if K < 3 * n:
        raise SystemExit("ERROR: need K >= 3n reference runs (K=%d, n=%d)" % (K, n))
    bands = sorted(set(BANDS + [a.band]))
    M, why = matrix(refs, names, a.k)
    usable = [s for s in names if s not in why]
    for s in names:
        if s in why:
            print("%s  n/a (field missing: %s) - left out of the overall rule below" % (s, why[s]))
    if not usable:
        raise SystemExit("ERROR: no computable statistic")
    total = math.comb(K, n) * math.comb(K - n, n) * math.comb(K - 2 * n, n)
    if total <= a.max_splits:
        splits = []
        for r1 in itertools.combinations(range(K), n):
            rest = [i for i in range(K) if i not in r1]
            for r2 in itertools.combinations(rest, n):
                rest2 = [i for i in rest if i not in r2]
                for c in itertools.combinations(rest2, n):
                    splits.append((r1, r2, c))
        how = "all %d splits enumerated" % total
    else:
        rng = random.Random(a.seed)
        seen, splits = set(), []
        while len(splits) < a.max_splits:
            perm = rng.sample(range(K), 3 * n)
            sp = (tuple(sorted(perm[:n])), tuple(sorted(perm[n:2 * n])), tuple(sorted(perm[2 * n:])))
            if sp not in seen:
                seen.add(sp)
                splits.append(sp)
        how = "%d of %d splits drawn (seed %d)" % (len(splits), total, a.seed)
    print("null false-fail rate: K=%d n=%d k=%s, %s, statistics %s" % (K, n, a.k, how, ",".join(usable)))
    fail_stat = {s: {b: 0 for b in bands} for s in usable}
    fail_all = {b: 0 for b in bands}
    for r1, r2, c in splits:
        failed = {b: False for b in bands}
        for s in usable:
            res = score_stat(lambda x, y, s=s: M[s][(x, y)], r1, r2, c, bands, a.seed)
            for b in bands:
                if not verdict(res, b)[0]:
                    fail_stat[s][b] += 1
                    failed[b] = True
        for b in bands:
            fail_all[b] += failed[b]
    N = float(len(splits))
    print("at band %s: overall false-fail %.3f (%d/%d)" % (a.band, fail_all[a.band] / N, fail_all[a.band], len(splits)))
    for s in usable:
        print("  %s  false-fail %.3f" % (s, fail_stat[s][a.band] / N))
    print("band   overall_false_fail  " + "  ".join("%-6s" % s for s in usable))
    for b in bands:
        print("%-6s %-19.3f %s" % (b, fail_all[b] / N, "  ".join("%-6.3f" % (fail_stat[s][b] / N) for s in usable)))
    ok = [b for b in bands if fail_all[b] / N <= 0.05]
    print("smallest band with overall false-fail <= 5%%: %s" % (ok[0] if ok else "none"))


def cmd_lost_rate(a):
    d = a.run
    log = os.path.join(d, "xray.log")
    pat = re.compile(r"\* tb_skip: done n=(\d+) skip_at_ms=(\d+) first_ms=(\d+) last_ms=(\d+)")
    hits = []
    if os.path.isfile(log):
        with open(log, encoding="utf-8", errors="replace") as f:
            for ln in f:
                m = pat.search(ln)
                if m:
                    hits.append(tuple(int(x) for x in m.groups()))
    if hits:
        for n, at, first, last in hits:
            report_rate(n, n, at, first, last, "log line")
        return
    # fallback: no completion line; use the last tb_extra (skips may be incomplete)
    r = Run(d)
    ks = sorted(int(re.search(r"_(\d+)\.txt$", p).group(1)) for p in glob.glob(os.path.join(d, "tb_extra_%s_*.txt" % r.tag)))
    if not ks:
        raise SystemExit("ERROR: no '* tb_skip: done' line in %s and no tb_extra files" % log)
    info = r.extra(ks[-1])["info"]
    n, done = int(info.get("skip_n", 0)), int(info.get("skip_done", 0))
    if n == 0:
        raise SystemExit("ERROR: run %s was not a -tb_skip run" % d)
    first, last = info.get("skip_first_ms", "-"), info.get("skip_last_ms", "-")
    if first == "-":
        print("tb_skip N=%d: no update skipped by dump k=%d" % (n, ks[-1]))
        return
    report_rate(n, done, int(info["skip_at_ms"]), int(first), int(last), "tb_extra k=%d (INCOMPLETE: %d of %d)" % (ks[-1], done, n))


def report_rate(n, done, at, first, last, src):
    span = (last - at) / 1000.0
    print("source: %s" % src)
    print("N=%d skipped=%d  skip_at=%.1fs first=%.1fs last=%.1fs  window=%.1fs" % (n, done, at / 1000.0, first / 1000.0, last / 1000.0, span))
    if span > 0:
        print("lost updates per minute within the skip window: %.1f" % (done / (span / 60.0)))
    else:
        print("lost updates per minute within the skip window: n/a (window 0 s)")
    print("lost updates per minute averaged from skip_at to the 900 s dump: %.2f (skipped / %.1f min)" % (
        done / ((900.0 - at / 1000.0) / 60.0) if at < 900000 else float("nan"), (900.0 - at / 1000.0) / 60.0))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0], formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pair")
    p.add_argument("A")
    p.add_argument("B")
    p.add_argument("--k", type=int, default=3)
    p = sub.add_parser("score")
    for g in ("--r1", "--r2", "--c"):
        p.add_argument(g, nargs="+", required=True)
    p.add_argument("--k", type=int, default=3)
    p.add_argument("--band", type=float, default=90)
    p.add_argument("--stats", default=",".join(STATS))
    p.add_argument("--seed", type=int, default=1)
    p = sub.add_parser("null")
    p.add_argument("--ref", nargs="+", required=True)
    p.add_argument("--n", type=int, default=5)
    p.add_argument("--k", type=int, default=3)
    p.add_argument("--band", type=float, default=90)
    p.add_argument("--max-splits", type=int, default=2000)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--stats", default=",".join(STATS))
    p = sub.add_parser("lost-rate")
    p.add_argument("run")
    a = ap.parse_args()
    {"pair": cmd_pair, "score": cmd_score, "null": cmd_null, "lost-rate": cmd_lost_rate}[a.cmd](a)


if __name__ == "__main__":
    main()
