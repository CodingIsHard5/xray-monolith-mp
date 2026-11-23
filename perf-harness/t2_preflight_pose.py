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
while True:
    t = time.time() - t0
    ph, st, aw, yw = ('A', 'play', 'keep', 0.0) if t < 10 else ('B', 'play', 'keep', 1.0) if t < 20 else \
        ('C', 'play', 'keep', -1.0) if t < 30 else ('D', 'dead', 'freeze', -1.0) if t < DEND and not NOFREEZE else ('E', 'play', 'keep', 0.5)
    n += 1; u = utc()
    body = f"seq,{n},{u},{st},{lvl},{aw}\np,{x:.3f},{y:.3f},{z:.3f},{yw:.4f},1.0\nend,{n}\n"
    with open(os.path.join(shm, 'pose.txt'), 'wb') as f: f.write(body.encode())
    log.write(f'{u},{n},{ph},{yw}\n'); log.flush()
    time.sleep(0.05)
