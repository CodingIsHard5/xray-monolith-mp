#!/usr/bin/env python3
"""Lost-pose pre-flight L: a stand-in for ITR2's pose writer that can be killed and started again. NOT ITR2's code.
  t2_lost_pose.py <shm_dir> <out_csv> <track csv> [key=value ...]
Writes pose v3 at 20 Hz:  seq,<n>,<utc_ms>,<state>,<zone>,<keep|freeze>,<session>  /  p,x,yfloor,z,yaw,hp,stance  /
[c,<changer>]  /  end,<n>.   seq starts at 1 in every process, as ITR2's does.
Keys:
  aw=keep|freeze     away policy written in every pose (default keep)
  sess=<n>           session number (default: this process's start time in UTC ms, as ITR2 would send)
  t0=<s>             track time at which this process starts playing (a restarted writer continues the route)
  pause=<a>-<b>      no write while the track time is in [a, b) (the negative control: a gap under the limit)
  crossat=<s>        from this track time on: state `cross`, zone <dest>, with a `c,<changer>` line
  dest=<level>  changer=<name>
The stance cycles 0,1,2 every 10 s of track time. Each write is appended to <out_csv>: utc_ms,seq,phase,session,track_s.
The driver kills this process with SIGKILL; it never stops by itself.
"""
import sys, time, os
shm, out, trk = sys.argv[1:4]
K = dict(a.split('=', 1) for a in sys.argv[4:])
utc = lambda: time.time_ns() // 1_000_000
AW = K.get('aw', 'keep'); SESS = K.get('sess') or str(utc()); T0 = float(K.get('t0', 0))
PA, PB = (float(v) for v in K['pause'].split('-')) if 'pause' in K else (None, None)
CX = float(K['crossat']) if 'crossat' in K else None
def snap():
    try: L = open(os.path.join(shm, 'snap.txt'), 'rb').read().decode('utf-8', 'replace').replace('\r', '').split('\n')
    except OSError: return None
    if len(L) < 3 or not L[0].startswith('seq,'): return None
    h = L[0].split(',')
    if len(h) < 6 or float(h[3]) < 0: return None   # wait for the warm-up to end (track time >= 0)
    return h[4]
lvl = None
while lvl is None: lvl = snap(); time.sleep(0.2)
TRK = [[float(v) for v in l.split(',')[:4]] for l in open(trk, 'rb').read().decode().replace('\r', '').split('\n')[1:] if l]
def at(t):
    i = max(j for j in range(len(TRK)) if TRK[j][0] <= t) if t >= TRK[0][0] else 0
    a = TRK[i]; b = TRK[min(i + 1, len(TRK) - 1)]; f = 0 if b[0] == a[0] else min(1.0, (t - a[0]) / (b[0] - a[0]))
    return [a[k] + f * (b[k] - a[k]) for k in (1, 2, 3)]
new = not os.path.exists(out)
log = open(out, 'a')
if new: log.write('utc_ms,seq,phase,session,track_s\n')
w0 = time.time(); n = 0
while True:
    t = T0 + time.time() - w0
    if PA is not None and PA <= t < PB: time.sleep(0.05); continue
    x, y, z = at(t); st, zn, ph, cl = 'play', lvl, 'T', ''
    if CX is not None and t >= CX: st, zn, ph, cl = 'cross', K['dest'], 'X', f"c,{K['changer']}\n"; x, y, z = at(CX)
    n += 1; u = utc()
    body = f"seq,{n},{u},{st},{zn},{AW},{SESS}\np,{x:.3f},{y + 0.3:.3f},{z:.3f},0.0000,1.0,{int(t // 10) % 3}\n{cl}end,{n}\n"
    with open(os.path.join(shm, 'pose.txt'), 'wb') as f: f.write(body.encode())
    log.write(f'{u},{n},{ph},{SESS},{t:.2f}\n'); log.flush()
    time.sleep(0.05)
