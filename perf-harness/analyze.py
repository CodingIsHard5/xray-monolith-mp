#!/usr/bin/env python3
"""Summarise CSV profiler captures.  usage: analyze.py out/<tag> [out/<tag2> ...]
Per capture: frame-time percentiles, self time per frame grouped by system (main thread / other threads),
and the top sites. Self times add up; inclusive times do not."""
import csv, sys, os, glob, statistics as st

def system(name, f):
    f = f.replace('/', '\\').lower(); n = name.lower()
    if 'mt_freezethread' in n: return None                      # watchdog thread sleeping, not work
    if 'xr_input' in f: return 'input'
    if 'luagc' in n or 'script' in f or 'lua' in f or 'script' in n: return 'lua/scripts'
    if 'alife' in f or 'a-life' in n or 'xrserver' in f: return 'a-life/server'
    if 'xrphysics' in f or '\\ph' in f or 'physic' in n: return 'physics'
    if 'xrsound' in f or 'sound' in n: return 'sound'
    if 'xrparticles' in f or 'particle' in n: return 'particles'
    if 'xrcdb' in f: return 'collision/spatial queries'
    if '\\ai\\' in f or 'stalker' in n or 'monster' in f or 'memory' in f or 'vision' in f or 'path' in f or 'cover' in f: return 'ai'
    if 'skeleton' in f or 'kinematics' in n or 'bones' in n: return 'animation/bones'
    if 'detail' in f or 'detail' in n: return 'grass/details'
    if 'layers' in f or 'render' in n: return 'render'
    if 'ui' in f or 'hud' in f: return 'ui/hud'
    if 'xrgame' in f: return 'game objects (other)'
    return 'engine (other)'

for d in sys.argv[1:]:
    sites = glob.glob(os.path.join(d, 'perf_sites_*.csv'))[0]; frames = glob.glob(os.path.join(d, 'perf_frames_*.csv'))[0]
    ms = sorted(float(r[1]) for r in list(csv.reader(open(frames)))[1:]); F = len(ms)
    q = lambda p: ms[min(F - 1, int(p * F))]
    cond = os.path.join(d, 'conditions.txt')
    print('== %s  (%s)' % (d, open(cond).read().strip().splitlines()[-1] if os.path.exists(cond) else 'no conditions file'))
    print('frames %d  mean %.2f  p50 %.2f  p95 %.2f  p99 %.2f  max %.2f ms' % (F, st.mean(ms), q(.5), q(.95), q(.99), ms[-1]))
    R = [r for r in csv.reader(open(sites)) if not r[0].startswith('#')][1:]
    grp = {}; rows = []
    for name, f, line, c0, i0, s0, c1, i1, s1 in R:
        g = system(name, f)
        if g is None: continue
        a = grp.setdefault(g, [0.0, 0.0]); a[0] += float(s0) / F; a[1] += float(s1) / F
        rows.append((name, f.split('src\\')[-1], int(c0) / F, float(i0) / F, float(s0) / F, int(c1) / F, float(s1) / F))
    tm = sum(a[0] for a in grp.values()); to = sum(a[1] for a in grp.values())
    print('%-28s %10s %6s %12s' % ('system (self ms/frame)', 'main', '%frame', 'other thr'))
    for g, a in sorted(grp.items(), key=lambda kv: -kv[1][0]):
        print('%-28s %10.3f %5.1f%% %12.3f' % (g, a[0], 100 * a[0] / st.mean(ms), a[1]))
    print('%-28s %10.3f %5.1f%% %12.3f' % ('TOTAL attributed', tm, 100 * tm / st.mean(ms), to))
    print('%-28s %10.3f %5.1f%%' % ('main thread unattributed', st.mean(ms) - tm, 100 * (st.mean(ms) - tm) / st.mean(ms)))
    print('worker-thread work per frame / main-thread frame time = %.2f (1.0 = one extra core fully busy)' % (to / st.mean(ms)))
    print('top main-thread sites (self ms/frame, incl, calls/frame):')
    for r in sorted(rows, key=lambda r: -r[4])[:15]: print('  %7.3f %7.3f %9.1f  %-44s %s' % (r[4], r[3], r[2], r[0][:44], r[1][-38:]))
    print('top other-thread sites (self ms/frame, calls/frame):')
    for r in sorted(rows, key=lambda r: -r[6])[:10]: print('  %7.3f %9.1f  %-44s %s' % (r[6], r[5], r[0][:44], r[1][-38:]))
