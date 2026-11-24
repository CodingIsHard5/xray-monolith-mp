#!/usr/bin/env python3
"""B1-T2 pre-flight boot: a stand-in for ITR2's pose writer (PREREG-B1-T2-xray-side section 5). NOT ITR2's code.
  t2_preflight_pose.py <shm_dir> <out_csv>
Waits for X-Ray's first snapshot with track time >= 0, takes the actor's position from its a line (on-mesh by construction), then writes
pose.txt at 20 Hz in ITRZone's format (binary, LF) with the actor held at that point and the yaw in phases:
  phase A  0-10 s   play, yaw 0.0
  phase B 10-20 s   play, yaw 1.0
  phase C 20-30 s   play, yaw -1.0
  phase D 30-70 s   dead, freeze (40 s: longer than the 30 s heartbeat, so the freeze skip is exercised)         (pause check: do snapshots keep coming, does UNPAUSE follow phase E?)
  phase E 70 s-     play, yaw 0.5, until killed
Each write is logged: utc_ms,seq,phase,yaw. The zone tag is the snapshot's level field.
"""
import sys, time, os
shm, out = sys.argv[1], sys.argv[2]
ARG = sys.argv[3] if len(sys.argv) > 3 else ''
NOFREEZE = ARG == 'nofreeze'
DEND = 30 + (int(ARG[1:]) if ARG.startswith('d') else 40)   # pre-flight 2: 'd300' = a 5-min dead+freeze phase (bridge SIGSTOP)
utc = lambda: time.time_ns() // 1_000_000
def snap():
    try: L = open(os.path.join(shm, 'snap.txt'), 'rb').read().decode().replace('\r', '').split('\n')
    except OSError: return None
    if len(L) < 3 or not L[0].startswith('seq,'): return None
    h = L[0].split(','); a = L[1].split(',')
    if len(h) < 6 or a[0] != 'a' or float(h[3]) < 0: return None   # wait for the warm-up to end (track time >= 0)
    return h[4], float(a[1]), float(a[2]), float(a[3])
s = None
while s is None: s = snap(); time.sleep(0.2)
lvl, x, y, z = s
log = open(out, 'w'); log.write('utc_ms,seq,phase,yaw\n')
t0 = time.time(); n = 0
TRK = None
if ARG.startswith('track:'):   # pre-flight 5 (section 25): play a track csv (t,x,y,z,...) by wall time, ping-ponged at 380 s, state play/keep
    TRK = [[float(v) for v in l.split(',')[:4]] for l in open(ARG[6:], 'rb').read().decode().replace('\r', '').split('\n')[1:] if l]
def trk_at(t):
    t = t % 760.0; t = t if t <= 380.0 else 760.0 - t
    i = max(j for j in range(len(TRK)) if TRK[j][0] <= t) if t >= TRK[0][0] else 0
    a = TRK[i]; b = TRK[min(i + 1, len(TRK) - 1)]; f = 0 if b[0] == a[0] else min(1.0, (t - a[0]) / (b[0] - a[0]))
    return [a[k] + f * (b[k] - a[k]) for k in (1, 2, 3)]
while True:
    t = time.time() - t0
    if ARG == 'pf4':   # pre-flight 4: A play | D dead+freeze 20-50 s (armed) | E play (planned kill follows)
        ph, st, aw, yw = ('A', 'play', 'freeze', 0.0) if t < 20 else ('D', 'dead', 'freeze', 0.0) if t < 50 else ('E', 'play', 'freeze', 0.0)
    elif ARG == 'pf3':   # pre-flight 3: A play (3 s hitch at track 20) | D1 freeze 60 s (unarmed control) | E1 play | D2 freeze 60 s (armed) | E
        ph, st, aw, yw = ('A', 'play', 'freeze', 0.0) if t < 40 else ('D1', 'dead', 'freeze', 0.0) if t < 100 else \
            ('E1', 'play', 'freeze', 0.0) if t < 130 else ('D2', 'dead', 'freeze', 0.0) if t < 190 else ('E', 'play', 'freeze', 0.0)
    else:
     ph, st, aw, yw = ('A', 'play', 'keep', 0.0) if t < 10 else ('B', 'play', 'keep', 1.0) if t < 20 else \
         ('C', 'play', 'keep', -1.0) if t < 30 else ('D', 'dead', 'freeze', -1.0) if t < DEND and not NOFREEZE else ('E', 'play', 'keep', 0.5)
    if TRK: ph, st, aw, yw = 'T', 'play', 'keep', 0.0; x, y, z = trk_at(t)
    n += 1; u = utc()
    body = f"seq,{n},{u},{st},{lvl},{aw}\np,{x:.3f},{y:.3f},{z:.3f},{yw:.4f},1.0\nend,{n}\n"
    with open(os.path.join(shm, 'pose.txt'), 'wb') as f: f.write(body.encode())
    log.write(f'{u},{n},{ph},{yw}\n'); log.flush()
    time.sleep(0.05)
