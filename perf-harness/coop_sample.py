#!/usr/bin/env python3
"""Sample processor time once a second: coop_sample.py <seconds> <out.csv> <label> <server_pid> [client_pid ...]
Per second: server main thread (tid == pid) cpu ms, server all threads cpu ms, busiest other server thread ms,
server RSS MiB, each client's cpu ms, idle % of cpus 0-1."""
import sys, time, os
secs = int(sys.argv[1]); out = sys.argv[2]; label = sys.argv[3]; sv = int(sys.argv[4]); cl = [int(x) for x in sys.argv[5:]]
HZ = os.sysconf('SC_CLK_TCK')
def tcpu(pid, tid=None):
    p = f'/proc/{pid}/task/{tid}/stat' if tid else f'/proc/{pid}/stat'
    try:
        f = open(p).read().rsplit(')', 1)[1].split()
        return (int(f[11]) + int(f[12])) * 1000.0 / HZ
    except Exception: return None
def threads(pid):
    d = {}
    try:
        for t in os.listdir(f'/proc/{pid}/task'):
            v = tcpu(pid, t)
            if v is not None: d[int(t)] = v
    except Exception: pass
    return d
def rss(pid):
    try:
        for l in open(f'/proc/{pid}/status'):
            if l.startswith('VmRSS'): return int(l.split()[1]) / 1024
    except Exception: return -1
def desk():
    r = []
    for l in open('/proc/stat'):
        if l.startswith('cpu0 ') or l.startswith('cpu1 '):
            f = [int(x) for x in l.split()[1:]]; r.append((f[3] + f[4], sum(f)))
    return r
new = not os.path.exists(out)
o = open(out, 'a')
if new: o.write('label,t,sv_main_ms,sv_all_ms,sv_other_max_ms,sv_rss_mib,clients_ms,desk_idle_pct,sv_net_ms\n')
def net_tid():
    try:
        for t in os.listdir(f'/proc/{sv}/task'):
            if open(f'/proc/{sv}/task/{t}/comm').read().strip() == 'xrnet-enet-sv': return int(t)
    except Exception: pass
    return None
NT = net_tid()
pt = threads(sv); pc = [tcpu(c) for c in cl]; pd = desk(); t0 = time.time()
for i in range(secs):
    time.sleep(max(0, t0 + i + 1 - time.time()))
    nt = threads(sv); nc = [tcpu(c) for c in cl]; nd = desk()
    if not nt: o.write(f'{label},{i},DEAD\n'); break
    main = nt.get(sv, 0) - pt.get(sv, 0); allc = sum(nt[t] - pt.get(t, 0) for t in nt)
    oth = max([nt[t] - pt.get(t, 0) for t in nt if t != sv] or [0])
    idle = sum(a[0] - b[0] for a, b in zip(nd, pd)); tot = sum(a[1] - b[1] for a, b in zip(nd, pd))
    cs = ';'.join('%.0f' % ((a or 0) - (b or 0)) for a, b in zip(nc, pc))
    net = (nt.get(NT, 0) - pt.get(NT, 0)) if NT else -1
    o.write('%s,%d,%.0f,%.0f,%.0f,%.0f,%s,%.0f,%.0f\n' % (label, i, main, allc, oth, rss(sv), cs, 100.0 * idle / max(1, tot), net))
    o.flush(); pt, pc, pd = nt, nc, nd
