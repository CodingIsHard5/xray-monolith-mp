#!/usr/bin/env python3
"""Compare two ::Random draw traces (det_trace_<tag>.txt): find the first differing draw.
usage: det_trace_cmp.py A.txt B.txt [context]
Lines: "<frame> <M|W> 0x<addr>"; header "#draws <total> recorded <n>"."""
import sys, collections
def load(p):
    hdr, rows = "", []
    for l in open(p, errors="replace"):
        l = l.strip()
        if not l: continue
        if l.startswith("#"): hdr = l; continue
        f, t, a = l.split()[:3]; rows.append((int(f), t, a))
    return hdr, rows
ha, A = load(sys.argv[1]); hb, B = load(sys.argv[2]); ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 8
print("A:", ha, len(A)); print("B:", hb, len(B))
n = min(len(A), len(B)); i = 0
while i < n and A[i] == B[i]: i += 1
if i == n and len(A) == len(B): print("IDENTICAL"); sys.exit(0)
print(f"first difference at draw index {i}")
for j in range(max(0, i - ctx), min(n, i + ctx)):
    m = " " if A[j] == B[j] else "*"
    print(f"{m}{j:7d}  A {A[j][0]:6d} {A[j][1]} {A[j][2]}   B {B[j][0]:6d} {B[j][1]} {B[j][2]}")
# per-frame summary around the split: count of draws per (thread, site)
fr = A[i][0] if i < len(A) else B[i][0]
for name, R in (("A", A), ("B", B)):
    c = collections.Counter((t, a) for f, t, a in R if f == fr)
    print(f"{name} frame {fr}: {sum(c.values())} draws;", ", ".join(f"{t}{a}x{k}" for (t, a), k in c.most_common(12)))
wa = sum(1 for r in A if r[1] == "W"); wb = sum(1 for r in B if r[1] == "W")
print(f"worker-thread draws: A {wa}  B {wb}")
sys.exit(1)
