#!/usr/bin/env python3
"""Offline model of the T2 pose on-mesh test (PREREG-B1-T2-xray-side, section 24). Reads a level.ai (version 10,
23-byte nodes; xrEngine/xrLevel.h, xrGame/level_graph_inline.h:98) and a track csv (t,x,y,z,node_dist), samples the
track at 20 Hz like ITRZone's writer (linear interpolation), and reports for each rule the share of HELD poses per
track-time window.  OLD rule (zz_p1_puppet.script:176-178 at 2bf1d2692): a node in the pose's own cell, chosen like
CLevelGraph::vertex_id (closest at or below pose y, else closest above), with |node y - pose y| <= 2.
NEW rule: any node within +-N cells in x and z whose y is within H of the pose y.
  t2_mesh_offline.py <level.ai> <track.csv> [span_s]
Node y is the stored cell-centre height (the plane tilt inside a 0.7 m cell is ignored)."""
import sys, struct, math
ai, trk = sys.argv[1], sys.argv[2]
d = open(ai, 'rb').read()
ver, cnt, cell, fy = struct.unpack_from('<IIff', d, 0); bx = struct.unpack_from('<6f', d, 16)
mnx, mny, mnz, mxx, mxy, mxz = bx; HDR = 56; NS = 23
assert ver == 10 and HDR + cnt * NS == len(d), (ver, cnt, len(d))
row = int(math.floor((mxz - mnz) / cell + 1e-5 + 1.5))
cells = {}
for i in range(cnt):
    o = HDR + i * NS + 18
    xz = d[o] | d[o + 1] << 8 | d[o + 2] << 16; y = d[o + 3] | d[o + 4] << 8
    cells.setdefault(xz, []).append(y / 65535.0 * fy + mny)
print(f'level.ai v{ver} nodes {cnt} cell {cell} factor_y {fy:.2f} box x {mnx:.1f}..{mxx:.1f} y {mny:.1f}..{mxy:.1f} z {mnz:.1f}..{mxz:.1f} row {row}')
def cx(x): return int(math.floor((x - mnx) / cell + .5))
def cz(z): return int(math.floor((z - mnz) / cell + .5))
def old(x, y, z):
    ys = cells.get(cx(x) * row + cz(z))
    if not ys: return 'nocell'
    below = [v for v in ys if v <= y]
    c = max(below) if below else min(ys)
    return None if abs(c - y) <= 2.0 else 'ydiff'
def new(x, y, z, N, H):
    X, Z = cx(x), cz(z)
    for dx in range(-N, N + 1):
        for dz in range(-N, N + 1):
            for v in cells.get((X + dx) * row + Z + dz, ()):
                if abs(v - y) <= H: return None
    return 'nonear'
T = [[float(v) for v in l.split(',')[:5]] for l in open(trk, 'rb').read().decode().replace('\r', '').split('\n')[1:] if l]
def at(t):
    lo, hi = 0, len(T) - 1
    while hi - lo > 1:
        m = (lo + hi) // 2
        if T[m][0] <= t: lo = m
        else: hi = m
    a, b = T[lo], T[hi]; f = 0 if b[0] == a[0] else min(1, max(0, (t - a[0]) / (b[0] - a[0])))
    return [a[i] + f * (b[i] - a[i]) for i in (1, 2, 3)]
def window(a, b, label):
    n = 0; res = {'old': 0, 'old_nocell': 0, 'old_ydiff': 0}; runs = []; inr = False
    news = {(1, 2.0): 0, (2, 2.0): 0, (3, 2.0): 0, (2, 1.0): 0}
    k = int(a * 20)
    while k / 20.0 < b:
        t = k / 20.0; p = at(t); n += 1; r = old(*p)
        if r:
            res['old'] += 1; res['old_' + r] += 1
            if not inr: runs.append([k, k]); inr = True
            runs[-1][1] = k
        else: inr = False
        for key in news: news[key] += new(*p, *key) is not None
        k += 1
    print(f'{label} [{a}-{b} s] poses {n}: OLD held {res["old"]} ({100*res["old"]/n:.1f} %; no node in cell {res["old_nocell"]}, y diff {res["old_ydiff"]})')
    for (N, H), v in news.items(): print(f'    NEW +-{N} cells ({N*cell:.1f} m), height +-{H} m: held {v} ({100*v/n:.1f} %)')
    return runs
span = float(sys.argv[3]) if len(sys.argv) > 3 else 380.0
runs = window(0, span, 'on-mesh part')
print('    OLD held runs as pose seq (= 20 x track s, first pass):', ' '.join(f'{a}-{b}' for a, b in runs[:25]))
window(410, 600, 'off-mesh leg (witness)')
window(380, 410, 'transition')
