#!/usr/bin/env python3
"""B1-T1 watcher (perf-harness/PREREG-B1-T1-2026-11-23.md, section 3). Runs on Linux beside both engines.
  t1_watch.py <snap.txt> <out_dir> <proc_pattern> [<proc_pattern> ...]   (stops on SIGTERM/SIGINT)
- Every 2 ms reads snap.txt; for each new COMPLETE snapshot (first line "seq,N,...", last line "end,N", N newer than the
  last one logged) writes "<utc_ms>,<seq>" to written.csv. Reads where the two seqs differ, or with no end line, are
  counted as torn (torn.csv: utc_ms,header_seq,end_seq). UTC wall clock, the same clock ITR2's UE4SS log stamps use.
- Every 5 s writes resources.csv: utc_ms, MemAvailable_MB, SwapUsed_MB, load1, and per matched process (first match of
  each pattern in /proc/*/cmdline): pid, RSS_MB, RssAnon_MB, CPU % since the last sample (100 = one full core).
"""
import os, signal, sys, time

snap, out = sys.argv[1], sys.argv[2]
pats = sys.argv[3:]
os.makedirs(out, exist_ok=True)
stop = False
def _stop(*_):
    global stop; stop = True
signal.signal(signal.SIGTERM, _stop); signal.signal(signal.SIGINT, _stop)
HZ = os.sysconf('SC_CLK_TCK')
utc_ms = lambda: time.time_ns() // 1_000_000

def meminfo():
    d = {}
    for l in open('/proc/meminfo'):
        k, v = l.split(':', 1); d[k] = int(v.split()[0])
    return d['MemAvailable'] // 1024, (d['SwapTotal'] - d['SwapFree']) // 1024

def find_pid(pat):
    best = None
    for p in os.listdir('/proc'):
        if not p.isdigit(): continue
        try: cl = open(f'/proc/{p}/cmdline', 'rb').read().replace(b'\0', b' ').decode('utf-8', 'replace')
        except OSError: continue
        if pat.lower() in cl.lower() and 't1_watch' not in cl:
            try: rss = int(next(l for l in open(f'/proc/{p}/status') if l.startswith('VmRSS')).split()[1])
            except (OSError, StopIteration): continue
            if best is None or rss > best[1]: best = (int(p), rss)   # the biggest match = the engine, not a launcher
    return best[0] if best else None

def proc_sample(pid):
    try:
        st = {l.split(':')[0]: l.split()[1] for l in open(f'/proc/{pid}/status') if l.startswith(('VmRSS', 'RssAnon'))}
        f = open(f'/proc/{pid}/stat').read().rsplit(')', 1)[1].split()
        return int(st['VmRSS']) // 1024, int(st['RssAnon']) // 1024, int(f[11]) + int(f[12])
    except (OSError, KeyError, IndexError):
        return None

wf = open(f'{out}/written.csv', 'w'); wf.write('utc_ms,seq\n')
tf = open(f'{out}/torn.csv', 'w'); tf.write('utc_ms,header_seq,end_seq\n')
rf = open(f'{out}/resources.csv', 'w')
rf.write('utc_ms,mem_available_mb,swap_used_mb,load1,' + ','.join(f'{i}_pid,{i}_rss_mb,{i}_anon_mb,{i}_cpu_pct' for i in range(len(pats))) + '\n')
last_seq, last_res, prev = 0, 0, {}
torn = 0
while not stop:
    now = utc_ms()
    try:
        with open(snap, 'rb') as f: data = f.read()
    except OSError:
        data = b''
    if data:
        lines = data.decode('utf-8', 'replace').rstrip('\n').split('\n')
        h = lines[0].split(','); e = lines[-1].split(',')
        hs = int(h[1]) if h[0] == 'seq' and len(h) > 1 and h[1].isdigit() else -1
        es = int(e[1]) if e[0] == 'end' and len(e) > 1 and e[1].isdigit() else -1
        if hs > 0 and hs == es:
            if hs > last_seq: wf.write(f'{now},{hs}\n'); last_seq = hs
        elif hs != last_seq:
            torn += 1; tf.write(f'{now},{hs},{es}\n')
    if now - last_res >= 5000:
        last_res = now
        ma, sw = meminfo(); row = [str(now), str(ma), str(sw), open('/proc/loadavg').read().split()[0]]
        for i, p in enumerate(pats):
            pid = find_pid(p); s = proc_sample(pid) if pid else None
            if s:
                cpu = ''
                if pid in prev:
                    dt = (now - prev[pid][0]) / 1000
                    cpu = f'{100 * (s[2] - prev[pid][1]) / HZ / dt:.1f}' if dt > 0 else ''
                prev[pid] = (now, s[2]); row += [str(pid), str(s[0]), str(s[1]), cpu]
            else:
                row += ['', '', '', '']
        rf.write(','.join(row) + '\n'); rf.flush(); wf.flush(); tf.flush()
    time.sleep(0.002)
wf.close(); tf.close(); rf.close()
print(f'watcher: last seq {last_seq}, torn reads {torn}')
