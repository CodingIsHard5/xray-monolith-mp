#!/usr/bin/env python3
"""R1 step 1a (PREREG-R1-step1a-2026-11-22.md): main-scene submission zones per run, main thread ms per frame.
static0, dynamic0 (Part 0 / G-buffer); forward = r1a_forward_a + r1a_forward_b (forward pass minus the particle wait);
main = static0 + dynamic0 + forward. Also (b) from r1_split for a sanity check (main must be <= b).
Usage: r1a_zones.py <run dir> [...]  -> one line per run, then MEAN."""
import csv, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import r1_split
Z = ("r1a_static0", "r1a_dynamic0", "r1a_forward_a", "r1a_forward_b")
def zones(d):
    tag = os.path.basename(os.path.normpath(d))
    rows = open(os.path.join(d, "perf_sites_%s.csv" % tag)).read().splitlines(); fr = int(rows[0].split(",")[1])
    v = {z: 0.0 for z in Z}; n = {z: 0 for z in Z}
    for r in csv.DictReader(rows[2:]):
        if r["name"] in v: v[r["name"]] += float(r["incl_main_ms"]) / fr; n[r["name"]] += 1
    miss = [z for z in Z if n[z] != 1]
    if miss: raise SystemExit("%s: zone(s) missing or duplicated: %s" % (d, miss))
    s = r1_split.run(d)
    o = dict(tag=tag, static0=v[Z[0]], dynamic0=v[Z[1]], forward=v[Z[2]] + v[Z[3]], b=s["b"], frame=s["frame"])
    o["main"] = o["static0"] + o["dynamic0"] + o["forward"]; return o
K = ("static0", "dynamic0", "forward", "main", "b", "frame")
rs = [zones(d) for d in sys.argv[1:]]
for r in rs: print("%-12s " % r["tag"] + "  ".join("%s=%.3f" % (k, r[k]) for k in K))
if len(rs) > 1: print("%-12s " % "MEAN" + "  ".join("%s=%.3f" % (k, sum(r[k] for r in rs) / len(rs)) for k in K))
